#include "ui/ChannelEditor.h"

#include "model/DspLaw.h"

namespace vc
{
namespace
{
juce::Colour panel() { return juce::Colour(0xff1c2128); }
juce::Colour text() { return juce::Colour(0xffe7edf3); }
juce::Colour dim() { return juce::Colour(0xff9aa3ad); }

juce::String channelPath(int index, const juce::String& leaf)
{
    return "/ch/" + juce::String(index + 1).paddedLeft('0', 2) + "/" + leaf;
}

juce::String hertzText(float hertz)
{
    if (hertz >= 1000.0f)
        return juce::String(hertz / 1000.0f, 2) + " kHz";

    return juce::String(juce::roundToInt(hertz)) + " Hz";
}

juce::String decibelText(float decibels)
{
    return juce::String(decibels, 1) + " dB";
}

juce::String millisecondsText(float milliseconds)
{
    if (milliseconds >= 1000.0f)
        return juce::String(milliseconds / 1000.0f, 2) + " s";

    return juce::String(milliseconds, milliseconds < 10.0f ? 2 : 1) + " ms";
}

class UnitSlider : public juce::Component
{
public:
    UnitSlider(const juce::String& titleText,
               std::function<float()> readValue,
               std::function<void(float)> writeValue,
               std::function<juce::String(float)> formatValue)
        : read(std::move(readValue)),
          write(std::move(writeValue)),
          format(std::move(formatValue))
    {
        title.setText(titleText, juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);
        title.setFont(juce::Font(juce::FontOptions(11.0f)));
        title.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(title);

        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 16);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setScrollWheelEnabled(false);
        slider.textFromValueFunction = [this](double value) { return format((float) value); };
        slider.onValueChange = [this]
        {
            if (write != nullptr)
                write((float) slider.getValue());
        };
        addAndMakeVisible(slider);
    }

    void refresh()
    {
        if (read == nullptr || slider.isMouseButtonDown() || textBoxFocused())
            return;

        slider.setValue(read(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(16));
        slider.setBounds(area);
    }

private:
    bool textBoxFocused() const
    {
        for (auto* child : slider.getChildren())
            if (child->hasKeyboardFocus(true))
                return true;

        return false;
    }

    std::function<float()> read;
    std::function<void(float)> write;
    std::function<juce::String(float)> format;
    juce::Label title;
    juce::Slider slider;
};

class OnButton : public juce::TextButton
{
public:
    OnButton(const juce::String& pathToUse, ConsoleModel& modelToUse, int& indexToUse)
        : juce::TextButton("On"),
          path(pathToUse),
          model(modelToUse),
          index(indexToUse)
    {
        setClickingTogglesState(true);
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
        onClick = [this] { model.setInt(channelPath(index, path), getToggleState() ? 1 : 0); };
    }

    void refresh()
    {
        if (isMouseButtonDown())
            return;

        int enabled = 0;
        model.getInt(channelPath(index, path), enabled);
        setToggleState(enabled != 0, juce::dontSendNotification);
    }

private:
    juce::String path;
    ConsoleModel& model;
    int& index;
};
} // namespace

class ChannelEditor::Content : public juce::Component
{
public:
    Content(ConsoleModel& modelToUse, int& indexToUse)
        : model(modelToUse),
          index(indexToUse)
    {
        auto addSlider = [this](const juce::String& title,
                                const juce::String& path,
                                std::function<juce::String(float)> format)
        {
            auto slider = std::make_unique<UnitSlider>(
                title,
                [this, path]
                {
                    float value = 0.0f;
                    model.getFloat(channelPath(index, path), value);
                    return value;
                },
                [this, path](float value) { model.setFloat(channelPath(index, path), value); },
                std::move(format));
            addAndMakeVisible(*slider);
            sliders.push_back(std::move(slider));
            return sliders.back().get();
        };

        auto addOn = [this](const juce::String& path)
        {
            auto button = std::make_unique<OnButton>(path, model, index);
            addAndMakeVisible(*button);
            switches.push_back(std::move(button));
            return switches.back().get();
        };

        delayOn = addOn("delay/on");
        delayTime = addSlider("Time", "delay/time", [](float value) { return millisecondsText(delaySeconds(value) * 1000.0f); });
        hpfOn = addOn("preamp/hpon");
        hpfFreq = addSlider("HPF", "preamp/hpf", [](float value) { return hertzText(hpfFrequencyHz(value)); });

        gateOn = addOn("gate/on");
        gateThr = addSlider("Thr", "gate/thr", [](float value) { return decibelText(gateThresholdDb(value)); });
        gateRange = addSlider("Range", "gate/range", [](float value) { return decibelText(gateRangeDb(value)); });
        gateAttack = addSlider("Attack", "gate/attack", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 120.0f)); });
        gateRelease = addSlider("Release", "gate/release", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 5.0f, 4000.0f)); });

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            const auto prefix = "eq/" + juce::String(band + 1) + "/";
            auto& row = eq[band];
            row.on = addOn(prefix + "on");
            row.type.addItemList(juce::StringArray { "LCut", "LShv", "PEQ", "VEQ", "HShv", "HCut" }, 1);
            row.type.onChange = [this, band]
            {
                if (eq[band].type.getSelectedId() > 0)
                    model.setInt(channelPath(index, "eq/" + juce::String(band + 1) + "/t"), eq[band].type.getSelectedId() - 1);
            };
            addAndMakeVisible(row.type);
            row.freq = addSlider("Freq", prefix + "f", [](float value) { return hertzText(eqFrequencyHz(value)); });
            row.gain = addSlider("Gain", prefix + "g", [](float value) { return decibelText(eqGainDb(value)); });
            row.q = addSlider("Q", prefix + "q", [](float value) { return juce::String(eqQ(value), 2); });
        }

        compOn = addOn("dyn/on");
        compThr = addSlider("Thr", "dyn/thr", [](float value) { return decibelText(vc::compThresholdDb(value)); });
        compRatio = addSlider("Ratio", "dyn/ratio", [](float value) { return juce::String(vc::compRatio(value), 1) + ":1"; });
        compAttack = addSlider("Attack", "dyn/attack", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 120.0f)); });
        compRelease = addSlider("Release", "dyn/release", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 5.0f, 4000.0f)); });
        compMakeup = addSlider("Makeup", "dyn/mgain", [](float value) { return decibelText(vc::compMakeupDb(value)); });

        for (auto* heading : { &delayHeading, &hpfHeading, &gateHeading, &eqHeading, &compHeading })
        {
            heading->setFont(juce::Font(juce::FontOptions(14.0f)));
            heading->setColour(juce::Label::textColourId, text());
            addAndMakeVisible(*heading);
        }
    }

    void refresh()
    {
        for (auto& slider : sliders)
            slider->refresh();

        for (auto& button : switches)
            button->refresh();

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            if (eq[band].type.isPopupActive())
                continue;

            int type = 2;
            model.getInt(channelPath(index, "eq/" + juce::String(band + 1) + "/t"), type);
            eq[band].type.setSelectedId(juce::jlimit(0, 5, type) + 1, juce::dontSendNotification);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12, 8);
        auto row = [&area](int height)
        {
            auto line = area.removeFromTop(height);
            area.removeFromTop(6);
            return line;
        };

        delayHeading.setBounds(row(20));
        auto delay = row(78);
        delayOn->setBounds(delay.removeFromLeft(64).reduced(4, 24));
        delayTime->setBounds(delay.removeFromLeft(88));

        hpfHeading.setBounds(row(20));
        auto hpf = row(78);
        hpfOn->setBounds(hpf.removeFromLeft(64).reduced(4, 24));
        hpfFreq->setBounds(hpf.removeFromLeft(88));

        gateHeading.setBounds(row(20));
        auto gate = row(78);
        gateOn->setBounds(gate.removeFromLeft(64).reduced(4, 24));
        gateThr->setBounds(gate.removeFromLeft(88));
        gateRange->setBounds(gate.removeFromLeft(88));
        gateAttack->setBounds(gate.removeFromLeft(88));
        gateRelease->setBounds(gate.removeFromLeft(88));

        eqHeading.setBounds(row(20));

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            auto line = row(78);
            eq[band].on->setBounds(line.removeFromLeft(64).reduced(4, 24));
            eq[band].type.setBounds(line.removeFromLeft(88).reduced(4, 26));
            eq[band].freq->setBounds(line.removeFromLeft(88));
            eq[band].gain->setBounds(line.removeFromLeft(88));
            eq[band].q->setBounds(line.removeFromLeft(88));
        }

        compHeading.setBounds(row(20));
        auto comp = row(78);
        compOn->setBounds(comp.removeFromLeft(64).reduced(4, 24));
        compThr->setBounds(comp.removeFromLeft(88));
        compRatio->setBounds(comp.removeFromLeft(88));
        compAttack->setBounds(comp.removeFromLeft(88));
        compRelease->setBounds(comp.removeFromLeft(88));
        compMakeup->setBounds(comp.removeFromLeft(88));
    }

    int contentHeight() const
    {
        return 20 + 78 + 20 + 78 + 20 + 78 + 20 + numChannelEqBands * 84 + 20 + 78 + 48;
    }

private:
    struct EqRow
    {
        OnButton* on = nullptr;
        juce::ComboBox type;
        UnitSlider* freq = nullptr;
        UnitSlider* gain = nullptr;
        UnitSlider* q = nullptr;
    };

    ConsoleModel& model;
    int& index;
    juce::Label delayHeading { {}, "Delay" };
    juce::Label hpfHeading { {}, "High pass" };
    juce::Label gateHeading { {}, "Gate" };
    juce::Label eqHeading { {}, "Equaliser" };
    juce::Label compHeading { {}, "Compressor" };
    OnButton* delayOn = nullptr;
    UnitSlider* delayTime = nullptr;
    OnButton* hpfOn = nullptr;
    UnitSlider* hpfFreq = nullptr;
    OnButton* gateOn = nullptr;
    UnitSlider* gateThr = nullptr;
    UnitSlider* gateRange = nullptr;
    UnitSlider* gateAttack = nullptr;
    UnitSlider* gateRelease = nullptr;
    EqRow eq[numChannelEqBands];
    OnButton* compOn = nullptr;
    UnitSlider* compThr = nullptr;
    UnitSlider* compRatio = nullptr;
    UnitSlider* compAttack = nullptr;
    UnitSlider* compRelease = nullptr;
    UnitSlider* compMakeup = nullptr;
    std::vector<std::unique_ptr<UnitSlider>> sliders;
    std::vector<std::unique_ptr<OnButton>> switches;
};

ChannelEditor::ChannelEditor(ConsoleModel& modelToUse)
    : model(modelToUse)
{
    channelLabel.setColour(juce::Label::textColourId, dim());
    addAndMakeVisible(channelLabel);

    for (int i = 0; i < numChannels; ++i)
        channelBox.addItem(model.getChannelName(i), i + 1);

    channelBox.setSelectedId(1, juce::dontSendNotification);
    channelBox.onChange = [this]
    {
        if (channelBox.getSelectedId() > 0)
            setChannel(channelBox.getSelectedId() - 1);
    };
    addAndMakeVisible(channelBox);

    linkButton.setClickingTogglesState(true);
    linkButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
    linkButton.setTooltip("Link this channel with its odd/even partner. Pan stays independent.");
    linkButton.onClick = [this]
    {
        model.setChannelLinked(index, linkButton.getToggleState());
        refresh();
    };
    addAndMakeVisible(linkButton);

    copyButton.onClick = [this]
    {
        model.copyChannel(index);
        pasteButton.setEnabled(model.hasChannelClipboard());
    };
    addAndMakeVisible(copyButton);

    pasteButton.setEnabled(false);
    pasteButton.onClick = [this]
    {
        model.pasteChannel(index);
        refresh();
    };
    addAndMakeVisible(pasteButton);

    content = std::make_unique<Content>(model, index);
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    refresh();
}

ChannelEditor::~ChannelEditor() = default;

void ChannelEditor::setChannel(int channelIndex)
{
    index = juce::jlimit(0, numChannels - 1, channelIndex);
    channelBox.setSelectedId(index + 1, juce::dontSendNotification);
    refresh();
}

void ChannelEditor::refresh()
{
    linkButton.setToggleState(model.isChannelLinked(index), juce::dontSendNotification);
    pasteButton.setEnabled(model.hasChannelClipboard());

    if (content != nullptr)
        content->refresh();
}

void ChannelEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(44).reduced(12, 8);
    channelLabel.setBounds(bar.removeFromLeft(64));
    channelBox.setBounds(bar.removeFromLeft(160).reduced(0, 2));
    bar.removeFromLeft(12);
    linkButton.setBounds(bar.removeFromLeft(72));
    bar.removeFromLeft(8);
    copyButton.setBounds(bar.removeFromLeft(72));
    bar.removeFromLeft(8);
    pasteButton.setBounds(bar.removeFromLeft(72));
    viewport.setBounds(area);

    if (content != nullptr)
    {
        const int barWidth = viewport.getScrollBarThickness();
        content->setSize(std::max(320, viewport.getWidth() - barWidth), content->contentHeight());
    }
}

} // namespace vc
