#include "ui/ChannelEditor.h"
#include "ui/ChannelIcons.h"
#include "ui/GroupPad.h"

#include "dsp/FilterDesign.h"
#include "model/DspLaw.h"

#include <array>
#include <cmath>
#include <initializer_list>

namespace vc
{
namespace
{
juce::Colour panel() { return juce::Colour(0xff1c2128); }
juce::Colour text() { return juce::Colour(0xffe7edf3); }
juce::Colour dim() { return juce::Colour(0xff9aa3ad); }
juce::Colour accent() { return juce::Colour(0xff3d6f8f); }
juce::Colour engaged() { return juce::Colour(0xff8a6a32); }

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

class BarSliderLook : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(0.5f, 1.0f);
        const float barWidth = juce::jlimit(12.0f, 18.0f, bounds.getWidth() * 0.34f);
        auto bar = bounds.removeFromRight(barWidth);
        bounds.removeFromRight(4.0f);

        g.setColour(juce::Colour(0xff14181d));
        g.fillRoundedRectangle(bar, 2.0f);

        const float fillTop = juce::jlimit(bar.getY(), bar.getBottom(), sliderPos);
        g.setColour(juce::Colour(0xff7eb6d6));
        g.fillRoundedRectangle(bar.withTop(fillTop), 2.0f);

        g.setColour(juce::Colour(0xffd5e4ee));
        g.drawRoundedRectangle(bar, 2.0f, 1.25f);

        struct Mark
        {
            float db;
            const char* text;
        };

        static constexpr Mark marks[] = {
            { 10.0f, "+10" }, { 5.0f, "+5" }, { 0.0f, "0" }, { -10.0f, "-10" },
            { -20.0f, "-20" }, { -30.0f, "-30" }, { -40.0f, "-40" }, { -60.0f, "-60" }, { -90.0f, "-inf" }
        };

        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        float lastY = -100.0f;

        for (const auto& mark : marks)
        {
            const float proportion = (float) slider.valueToProportionOfLength((double) dbToFader(mark.db));
            const float markY = maxSliderPos + (1.0f - proportion) * (minSliderPos - maxSliderPos);
            const bool unity = mark.db == 0.0f;
            const bool endpoint = mark.db >= 10.0f || mark.db <= -90.0f;

            if (! unity && ! endpoint && std::abs(markY - lastY) < 11.0f)
                continue;

            g.setColour(unity ? juce::Colour(0xfff4f7fb) : juce::Colour(0xff8b95a1));
            const float tick = unity ? 7.0f : 4.0f;
            g.fillRect(bar.getX() - tick, markY - (unity ? 1.0f : 0.5f), tick, unity ? 2.0f : 1.0f);
            g.drawText(mark.text,
                       juce::Rectangle<float>(bounds.getX(), markY - 6.0f, bounds.getWidth(), 12.0f),
                       juce::Justification::centredRight,
                       false);
            lastY = markY;
        }
    }
};

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
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
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

    void setTitle(const juce::String& titleText)
    {
        title.setText(titleText, juce::dontSendNotification);
    }

    ~UnitSlider() override
    {
        slider.setLookAndFeel(nullptr);
    }

    void setVerticalBar(bool shouldBeVertical)
    {
        verticalBar = shouldBeVertical;

        if (verticalBar)
            slider.setLookAndFeel(&barLook);

        resized();
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

        if (verticalBar)
        {
            title.setJustificationType(juce::Justification::centred);
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, juce::jmax(48, area.getWidth() - 4), 16);
            title.setBounds(area.removeFromTop(14));
            slider.setBounds(area);
            return;
        }

        if (area.getWidth() > area.getHeight() && area.getHeight() < 64)
        {
            title.setJustificationType(juce::Justification::centredLeft);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, juce::jmin(16, area.getHeight()));
            title.setBounds(area.removeFromLeft(juce::jmin(68, area.getWidth() / 3)));
            slider.setBounds(area);
            return;
        }

        title.setJustificationType(juce::Justification::centred);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
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
    bool verticalBar = false;
    BarSliderLook barLook;
    juce::Label title;
    juce::Slider slider;
};

juce::Colour eqBandColour(int band)
{
    static const juce::uint32 colours[] = { 0xffe0b040, 0xff3dce7a, 0xff7eb6d6, 0xffd27ad4, 0xffe07a7a, 0xfff2f5f8 };
    return juce::Colour(colours[band % 6]);
}

class ControlBox : public juce::Component
{
public:
    void setTitle(const juce::String& newTitle, juce::Colour outline)
    {
        title = newTitle;
        colour = outline;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff232830));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(colour);
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(title, getLocalBounds().reduced(8, 3).removeFromTop(14), juce::Justification::centredLeft, true);
    }

private:
    juce::String title;
    juce::Colour colour { 0xff3c4450 };
};

class FlagButton : public juce::TextButton
{
public:
    FlagButton(const juce::String& label, const juce::String& pathToUse, ConsoleModel& modelToUse, int& indexToUse)
        : juce::TextButton(label),
          path(pathToUse),
          model(modelToUse),
          index(indexToUse)
    {
        setClickingTogglesState(true);
        setColour(juce::TextButton::buttonOnColourId, accent());
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

    bool isOn() const { return getToggleState(); }

private:
    juce::String path;
    ConsoleModel& model;
    int& index;
};

class ChoiceBox : public juce::Component
{
public:
    ChoiceBox(const juce::String& titleText,
              const juce::String& pathToUse,
              ConsoleModel& modelToUse,
              int& indexToUse,
              const juce::StringArray& items)
        : path(pathToUse),
          model(modelToUse),
          index(indexToUse)
    {
        title.setText(titleText, juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centredLeft);
        title.setFont(juce::Font(juce::FontOptions(11.0f)));
        title.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(title);
        box.addItemList(items, 1);
        box.setScrollWheelEnabled(false);
        box.onChange = [this]
        {
            if (box.getSelectedId() > 0)
                model.setInt(channelPath(index, path), box.getSelectedId() - 1);
        };
        addAndMakeVisible(box);
    }

    void setShowTitle(bool shouldShow)
    {
        showTitle = shouldShow;
        title.setVisible(shouldShow);
        resized();
    }

    void refresh()
    {
        if (box.isPopupActive())
            return;

        int value = 0;
        model.getInt(channelPath(index, path), value);
        box.setSelectedId(juce::jlimit(0, box.getNumItems() - 1, value) + 1, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        if (! showTitle)
        {
            box.setBounds(area.reduced(1, 1));
            return;
        }

        if (area.getHeight() <= 44 && area.getWidth() > 80)
        {
            title.setJustificationType(juce::Justification::centredLeft);
            title.setBounds(area.removeFromLeft(juce::jmin(52, area.getWidth() / 3)));
            box.setBounds(area.reduced(2, 1));
            return;
        }

        title.setJustificationType(juce::Justification::centredLeft);
        title.setBounds(area.removeFromTop(14));
        box.setBounds(area.removeFromTop(24).reduced(1, 0));
    }

private:
    juce::String path;
    ConsoleModel& model;
    int& index;
    bool showTitle = true;
    juce::Label title;
    juce::ComboBox box;
};

class DynamicsGraph : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    enum class Kind { gate, compressor };

    DynamicsGraph(ConsoleModel& modelToUse, Kind graphKind)
        : model(modelToUse), kind(graphKind)
    {
        setTooltip(graphKind == Kind::gate
                       ? "Drag left and right to set the threshold. Drag down to deepen the range."
                       : "Drag left and right to set the threshold. Drag up to raise the ratio.");
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        setMouseCursor(frame().plot.contains(event.position) ? juce::MouseCursor::DraggingHandCursor
                                                              : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto plot = frame().plot;

        if (! plot.contains(event.position))
            return;

        dragging = true;
        anchorY = event.position.y;
        secondaryAtAnchor = readUnit(kind == Kind::gate ? "range" : "ratio", 0.5f);
        writeThreshold(inputForX(event.position.x, plot));
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! dragging)
            return;

        const auto plot = frame().plot;
        writeThreshold(inputForX(event.position.x, plot));
        const float travel = (anchorY - event.position.y) / juce::jmax(1.0f, plot.getHeight());
        const float secondary = kind == Kind::gate ? secondaryAtAnchor - travel : secondaryAtAnchor + travel;
        writeUnit(kind == Kind::gate ? "range" : "ratio", secondary);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragging = false;
    }

    void setChannel(int channelIndex)
    {
        index = channelIndex;
        repaint();
    }

    void setPathRoot(const juce::String& root)
    {
        pathRoot = root;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff101418));
        g.fillRoundedRectangle(bounds, 4.0f);

        const auto laidOut = frame();
        auto readout = laidOut.readout;
        auto axis = laidOut.axis;
        auto inputLane = laidOut.inputLane;
        auto reductionLane = laidOut.reductionLane;
        auto plot = laidOut.plot;

        drawLevelScale(g, inputLane, juce::Colour(0xff3dce7a), { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f });
        drawReductionScale(g, reductionLane);

        if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f)
            return;

        const bool enabled = readInt("on") != 0;
        const auto curveColour = (kind == Kind::gate ? juce::Colour(0xffe07a7a) : juce::Colour(0xffe0b040)).withAlpha(enabled ? 1.0f : 0.35f);
        const float threshold = thresholdDb();
        const float depth = rangeDb();

        g.setColour(juce::Colour(0xff2a313a));

        for (float db : { 10.0f, 0.0f, -20.0f, -40.0f, -60.0f })
        {
            const float y = yForOutput(db, plot);

            if (y < plot.getY() || y > plot.getBottom())
                continue;

            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }

        for (float db : { -60.0f, -40.0f, -20.0f, 0.0f })
        {
            const float x = xForInput(db, plot);
            g.setColour(juce::Colour(0xff2a313a));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
            g.setColour(dim());
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(juce::String(juce::roundToInt(db)),
                       juce::Rectangle<float>(x - 16.0f, axis.getY(), 32.0f, axis.getHeight()),
                       juce::Justification::centred,
                       false);
        }

        g.setColour(juce::Colour(0xffd7dbe2).withAlpha(0.28f));
        juce::Path unity;
        unity.startNewSubPath(xForInput(-80.0f, plot), yForOutput(-80.0f, plot));
        unity.lineTo(xForInput(0.0f, plot), yForOutput(0.0f, plot));
        g.strokePath(unity, juce::PathStrokeType(1.0f));

        juce::Path curve;
        juce::Path fill;
        bool started = false;

        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const float input = inputForX(x, plot);
            const float output = std::clamp(outputDb(input, threshold, depth), outputFloor, outputCeil);
            const float y = yForOutput(output, plot);

            if (! started)
            {
                curve.startNewSubPath(x, y);
                fill.startNewSubPath(x, plot.getBottom());
                fill.lineTo(x, y);
                started = true;
            }
            else
            {
                curve.lineTo(x, y);
                fill.lineTo(x, y);
            }
        }

        if (started)
        {
            fill.lineTo(plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setColour(curveColour.withAlpha(enabled ? 0.18f : 0.06f));
            g.fillPath(fill);
        }

        g.setColour(curveColour);
        g.strokePath(curve, juce::PathStrokeType(2.0f));

        const float markerX = xForInput(threshold, plot);
        const float markerY = yForOutput(std::clamp(outputDb(threshold, threshold, depth), outputFloor, outputCeil), plot);
        g.setColour(curveColour.withAlpha(0.55f));
        g.drawVerticalLine(juce::roundToInt(markerX), plot.getY(), plot.getBottom());
        g.setColour(juce::Colour(0xffd7ecf6));
        g.fillRect(markerX - 4.0f, markerY - 4.0f, 8.0f, 8.0f);
        g.setColour(juce::Colour(0xff101418));
        g.drawRect(markerX - 4.0f, markerY - 4.0f, 8.0f, 8.0f, 1.0f);

        auto label = "THR " + decibelText(threshold);

        if (kind == Kind::gate)
            label << "    RANGE " << decibelText(depth);
        else
            label << "    RATIO " << juce::String(compRatio(readUnit("ratio", 0.5f)), 1) << ":1"
                  << "    KNEE " << juce::String(compKneeDb(readUnit("knee", 0.2f)), 1) << " dB";

        g.setColour(text());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(label, readout, juce::Justification::centredLeft, true);
    }

private:
    struct Frame
    {
        juce::Rectangle<float> plot;
        juce::Rectangle<float> readout;
        juce::Rectangle<float> axis;
        juce::Rectangle<float> inputLane;
        juce::Rectangle<float> reductionLane;
    };

    Frame frame() const
    {
        auto area = getLocalBounds().toFloat().reduced(5.0f);
        Frame laidOut;
        laidOut.readout = area.removeFromBottom(16.0f);
        laidOut.axis = area.removeFromBottom(13.0f);
        laidOut.inputLane = area.removeFromLeft(34.0f);
        laidOut.reductionLane = area.removeFromRight(30.0f);
        laidOut.plot = area.reduced(4.0f, 2.0f);
        return laidOut;
    }

    void writeUnit(const char* leaf, float unit)
    {
        const auto root = kind == Kind::gate ? juce::String("gate/") : juce::String("dyn/");
        model.setFloat(scoped(root + leaf), clamp01(unit));
        repaint();
    }

    void writeThreshold(float decibels)
    {
        if (kind == Kind::gate)
            writeUnit("thr", (decibels + 80.0f) / 80.0f);
        else
            writeUnit("thr", (decibels + 60.0f) / 60.0f);
    }

    static constexpr float inputFloor = -80.0f;
    static constexpr float inputCeil = 0.0f;
    static constexpr float outputFloor = -80.0f;
    static constexpr float outputCeil = 18.0f;

    static float xForInput(float inputDb, juce::Rectangle<float> plot) noexcept
    {
        const float clamped = std::clamp(inputDb, inputFloor, inputCeil);
        return plot.getX() + (clamped - inputFloor) / (inputCeil - inputFloor) * plot.getWidth();
    }

    static float inputForX(float x, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::clamp((x - plot.getX()) / plot.getWidth(), 0.0f, 1.0f);
        return inputFloor + t * (inputCeil - inputFloor);
    }

    static float yForOutput(float outputDb, juce::Rectangle<float> plot) noexcept
    {
        const float clamped = std::clamp(outputDb, outputFloor, outputCeil);
        return plot.getBottom() - (clamped - outputFloor) / (outputCeil - outputFloor) * plot.getHeight();
    }

    static void drawLevelScale(juce::Graphics& g, juce::Rectangle<float> lane, juce::Colour colour, std::initializer_list<float> marks)
    {
        auto bar = lane.removeFromLeft(8.0f).reduced(0.0f, 1.0f);
        g.setColour(juce::Colour(0xff0c0f12));
        g.fillRect(bar);
        g.setColour(colour.withAlpha(0.85f));
        g.drawRect(bar, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(9.0f)));

        for (float db : marks)
        {
            const float y = bar.getBottom() - (db + 60.0f) / 60.0f * bar.getHeight();
            g.setColour(colour.withAlpha(0.9f));
            g.drawHorizontalLine(juce::roundToInt(y), bar.getRight() + 1.0f, bar.getRight() + 4.0f);
            g.setColour(dim());
            g.drawText(juce::String(juce::roundToInt(db)),
                       juce::Rectangle<float>(bar.getRight() + 4.0f, y - 6.0f, lane.getWidth(), 12.0f),
                       juce::Justification::centredLeft,
                       false);
        }
    }

    static void drawReductionScale(juce::Graphics& g, juce::Rectangle<float> lane)
    {
        auto bar = lane.removeFromRight(8.0f).reduced(0.0f, 1.0f);
        g.setColour(juce::Colour(0xff0c0f12));
        g.fillRect(bar);
        g.setColour(juce::Colour(0xffe04b4b).withAlpha(0.9f));
        g.drawRect(bar, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(9.0f)));

        for (float db : { 0.0f, 6.0f, 12.0f, 18.0f })
        {
            const float y = bar.getY() + db / 18.0f * bar.getHeight();
            g.setColour(juce::Colour(0xffe07a7a));
            g.drawHorizontalLine(juce::roundToInt(y), bar.getX() - 4.0f, bar.getX() - 1.0f);
            g.setColour(dim());
            g.drawText(juce::String(juce::roundToInt(db)),
                       juce::Rectangle<float>(lane.getX(), y - 6.0f, bar.getX() - lane.getX() - 4.0f, 12.0f),
                       juce::Justification::centredRight,
                       false);
        }
    }

    float readUnit(const char* leaf, float fallback) const
    {
        float value = fallback;
        const auto root = kind == Kind::gate ? juce::String("gate/") : juce::String("dyn/");
        model.getFloat(scoped(root + leaf), value);
        return value;
    }

    int readInt(const char* leaf) const
    {
        int value = 0;
        const auto root = kind == Kind::gate ? juce::String("gate/") : juce::String("dyn/");
        model.getInt(scoped(root + leaf), value);
        return value;
    }

    float thresholdDb() const
    {
        return kind == Kind::gate ? gateThresholdDb(readUnit("thr", 0.75f)) : compThresholdDb(readUnit("thr", 0.75f));
    }

    float rangeDb() const
    {
        return kind == Kind::gate ? gateRangeDb(readUnit("range", 0.5f)) : 0.0f;
    }

    float outputDb(float input, float threshold, float depth) const
    {
        if (kind == Kind::gate)
            return gateOutputDb(input, readInt("mode"), threshold, depth);

        return compressorOutputDb(input,
                                   readInt("mode"),
                                   threshold,
                                   compRatio(readUnit("ratio", 0.5f)),
                                   compKneeDb(readUnit("knee", 0.2f)),
                                   compMakeupDb(readUnit("mgain", 0.5f)));
    }

    juce::String scoped(const juce::String& leaf) const
    {
        return pathRoot.isNotEmpty() ? pathRoot + "/" + leaf : channelPath(index, leaf);
    }

    ConsoleModel& model;
    Kind kind;
    int index = 0;
    juce::String pathRoot;
    bool dragging = false;
    float anchorY = 0.0f;
    float secondaryAtAnchor = 0.5f;
};

class EqGraph : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit EqGraph(ConsoleModel& modelToUse)
        : model(modelToUse)
    {
        setTooltip("Drag a band to set frequency and gain. Scroll over a band to change its Q.");
    }

    void setChannel(int channelIndex)
    {
        index = channelIndex;
        repaint();
    }

    void setTarget(const juce::String& root, int bands, bool hpf)
    {
        pathRoot = root;
        bandCount = juce::jlimit(1, numEqBands, bands);
        showHpf = hpf;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff15191e));
        g.fillRoundedRectangle(bounds, 6.0f);

        auto plot = bounds.reduced(8.0f, 6.0f);
        plot.removeFromLeft(36.0f);
        plot.removeFromBottom(18.0f);

        if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f)
            return;

        g.setColour(juce::Colour(0xff2a313a));

        for (float db : { 15.0f, 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -15.0f, -20.0f })
        {
            const float y = yForDb(db, plot);
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }

        for (float hz : { 30.0f, 40.0f, 60.0f, 80.0f, 150.0f, 300.0f, 400.0f, 700.0f, 1500.0f, 3000.0f, 4000.0f, 7000.0f, 15000.0f })
        {
            const float x = xForHz(hz, plot);
            g.drawVerticalLine(juce::roundToInt(x), plot.getBottom() - 5.0f, plot.getBottom());
        }

        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForHz(hz, plot);
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        g.setColour(juce::Colour(0xffd7dbe2).withAlpha(0.35f));
        g.drawHorizontalLine(juce::roundToInt(yForDb(0.0f, plot)), plot.getX(), plot.getRight());

        g.setColour(dim());
        g.setFont(juce::Font(juce::FontOptions(10.0f)));

        for (float db : { 15.0f, 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -15.0f, -20.0f })
        {
            const auto label = juce::String(juce::roundToInt(db));
            g.drawText(label, 2, juce::roundToInt(yForDb(db, plot)) - 7, 30, 14, juce::Justification::centredRight, false);
        }

        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForHz(hz, plot);
            const auto label = hz >= 1000.0f ? juce::String(juce::roundToInt(hz / 1000.0f)) + "k" : juce::String(juce::roundToInt(hz));
            g.drawText(label, juce::roundToInt(x) - 16, juce::roundToInt(plot.getBottom()) + 1, 32, 14, juce::Justification::centred, false);
        }

        const auto hpf = readHpf();
        std::array<Band, numEqBands> bands {};
        int eqEnabled = 1;
        model.getInt(scoped("eq/on"), eqEnabled);

        for (int band = 0; band < bandCount; ++band)
            bands[(size_t) band] = readBand(band);

        if (eqEnabled == 0)
        {
            for (auto& bandState : bands)
                bandState.contributes = false;

            g.setColour(juce::Colour(0xffe07a7a));
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("EQ off", plot.reduced(6.0f).removeFromTop(16.0f), juce::Justification::topRight, false);
        }

        auto drawCurve = [&](auto response, juce::Colour colour, float thickness)
        {
            juce::Path path;
            bool started = false;

            for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            {
                const float db = response(hzForX(x, plot));
                const float y = yForDb(db, plot);

                if (! started)
                {
                    path.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    path.lineTo(x, y);
                }
            }

            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(thickness));
            return path;
        };

        for (int band = 0; band < bandCount; ++band)
        {
            const auto& bandState = bands[(size_t) band];

            if (! bandState.contributes)
                continue;

            drawCurve([&](float hz) { return biquadMagnitudeDb(bandState.coeffs, hz, sampleRate); },
                      bandColour(band).withAlpha(band == selected ? 0.95f : 0.55f),
                      band == selected ? 2.0f : 1.25f);
        }

        if (showHpf && hpf.enabled)
            drawCurve([&](float hz) { return biquadMagnitudeDb(hpf.coeffs, hz, sampleRate); },
                      juce::Colour(0xfff2f5f8).withAlpha(0.45f),
                      1.25f);

        const auto sum = drawCurve([&](float hz)
                                    {
                                        float db = showHpf && hpf.enabled ? biquadMagnitudeDb(hpf.coeffs, hz, sampleRate) : 0.0f;

                                        for (const auto& bandState : bands)
                                            if (bandState.contributes)
                                                db += biquadMagnitudeDb(bandState.coeffs, hz, sampleRate);

                                        return db;
                                    },
                                    juce::Colour(0xfff4f7fb),
                                    2.2f);
        juce::ignoreUnused(sum);

        if (showHpf)
            drawHandle(g, plot, xForHz(hpf.frequency, plot), yForDb(0.0f, plot), juce::Colour(0xfff2f5f8), hpf.enabled, selected == hpfId, "H");

        for (int band = 0; band < bandCount; ++band)
        {
            const auto& bandState = bands[(size_t) band];
            const float gain = bandState.usesGain ? bandState.gain : 0.0f;
            drawHandle(g,
                       plot,
                       xForHz(bandState.frequency, plot),
                       yForDb(gain, plot),
                       bandColour(band),
                       bandState.enabled,
                       selected == band,
                       juce::String(band + 1));
        }

        if (showHpf && selected == hpfId)
            drawReadout(g, plot, xForHz(hpf.frequency, plot), yForDb(0.0f, plot), "HPF  " + hertzText(hpf.frequency));
        else if (selected >= 0 && selected < bandCount)
        {
            const auto& bandState = bands[(size_t) selected];
            juce::String readout = "Band " + juce::String(selected + 1) + "  " + hertzText(bandState.frequency);

            if (bandState.usesGain)
                readout << "  " << decibelText(bandState.gain);

            if (bandState.usesQ)
                readout << "  Q " << juce::String(bandState.q, 2);

            drawReadout(g, plot, xForHz(bandState.frequency, plot), yForDb(bandState.usesGain ? bandState.gain : 0.0f, plot), readout);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const int hit = hitTest(event.position);
        setMouseCursor(hit == noneId ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        selected = hitTest(event.position);

        if (selected == noneId)
        {
            repaint();
            return;
        }

        if (selected == hpfId && showHpf)
            model.setInt(scoped("preamp/hpon"), 1);
        else if (selected >= 0)
            model.setInt(scoped("eq/" + juce::String(selected + 1) + "/on"), 1);

        applyDrag(event.position);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (selected != noneId)
            applyDrag(event.position);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const int hit = hitTest(event.position);

        if (hit >= 0 && hit < bandCount)
        {
            model.setFloat(scoped("eq/" + juce::String(hit + 1) + "/g"), 0.5f);
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const int hit = selected != noneId && isMouseButtonDown() ? selected : hitTest(event.position);

        if (hit < 0 || hit >= bandCount)
            return;

        const auto band = readBand(hit);

        if (! band.usesQ)
            return;

        const float next = eqQUnit(band.q) - wheel.deltaY * 0.08f;
        model.setFloat(scoped("eq/" + juce::String(hit + 1) + "/q"), clamp01(next));
        selected = hit;
        repaint();
    }

private:
    static constexpr int noneId = -2;
    static constexpr int hpfId = -1;
    static constexpr double sampleRate = 48000.0;
    static constexpr float dbTop = 18.0f;
    static constexpr float dbBottom = -24.0f;

    struct Band
    {
        bool enabled = false;
        bool contributes = false;
        bool usesGain = true;
        bool usesQ = false;
        int type = 2;
        float frequency = 1000.0f;
        float gain = 0.0f;
        float q = 1.0f;
        BiquadCoeffs coeffs {};
    };

    struct Hpf
    {
        bool enabled = false;
        float frequency = 80.0f;
        BiquadCoeffs coeffs {};
    };

    static juce::Colour bandColour(int band)
    {
        return eqBandColour(band);
    }

    static float yForDb(float db, juce::Rectangle<float> plot) noexcept
    {
        const float clamped = std::clamp(db, dbBottom, dbTop);
        return plot.getY() + (dbTop - clamped) / (dbTop - dbBottom) * plot.getHeight();
    }

    static float dbForY(float y, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::clamp((y - plot.getY()) / plot.getHeight(), 0.0f, 1.0f);
        return dbTop - t * (dbTop - dbBottom);
    }

    static float xForHz(float hz, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::log(std::max(hz, 20.0f) / 20.0f) / std::log(1000.0f);
        return plot.getX() + std::clamp(t, 0.0f, 1.0f) * plot.getWidth();
    }

    static float hzForX(float x, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::clamp((x - plot.getX()) / plot.getWidth(), 0.0f, 1.0f);
        return 20.0f * std::pow(1000.0f, t);
    }

    juce::Rectangle<float> plotArea() const
    {
        auto plot = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
        plot.removeFromLeft(36.0f);
        plot.removeFromBottom(18.0f);
        return plot;
    }

    Hpf readHpf() const
    {
        Hpf hpf;
        int enabled = 0;
        float unit = 0.2f;
        model.getInt(scoped("preamp/hpon"), enabled);
        model.getFloat(scoped("preamp/hpf"), unit);
        hpf.enabled = enabled != 0;
        hpf.frequency = hpfFrequencyHz(unit);
        hpf.coeffs = designHighPass(hpf.frequency, sampleRate);
        return hpf;
    }

    Band readBand(int band) const
    {
        Band state;
        const auto prefix = "eq/" + juce::String(band + 1) + "/";
        int enabled = 1;
        int type = 2;
        float frequency = 0.5f;
        float gain = 0.5f;
        float q = 0.5f;
        model.getInt(scoped(prefix + "on"), enabled);
        model.getInt(scoped(prefix + "t"), type);
        model.getFloat(scoped(prefix + "f"), frequency);
        model.getFloat(scoped(prefix + "g"), gain);
        model.getFloat(scoped(prefix + "q"), q);
        state.enabled = enabled != 0;
        state.type = type;
        state.frequency = eqFrequencyHz(frequency);
        state.gain = eqGainDb(gain);
        state.q = eqQ(q);
        state.usesGain = type != 0 && type != 5;
        state.usesQ = type == 2 || type == 3;
        const bool flat = std::abs(state.gain) < 0.05f && state.usesGain;
        state.contributes = state.enabled && ! flat;

        if (state.contributes)
        {
            if (type == 0)
                state.coeffs = designHighPass(state.frequency, sampleRate);
            else if (type == 1)
                state.coeffs = designShelf(state.frequency, state.gain, false, sampleRate);
            else if (type == 4)
                state.coeffs = designShelf(state.frequency, state.gain, true, sampleRate);
            else if (type == 5)
                state.coeffs = designLowPass(state.frequency, sampleRate);
            else
                state.coeffs = designPeak(state.frequency, state.q, state.gain, sampleRate);
        }

        return state;
    }

    int hitTest(juce::Point<float> position) const
    {
        const auto plot = plotArea();
        int best = noneId;
        float bestDistance = 16.0f;
        const auto hpf = readHpf();
        const float hpfDistance = position.getDistanceFrom({ xForHz(hpf.frequency, plot), yForDb(0.0f, plot) });

        if (showHpf && hpfDistance < bestDistance)
        {
            best = hpfId;
            bestDistance = hpfDistance;
        }

        for (int band = 0; band < bandCount; ++band)
        {
            const auto state = readBand(band);
            const float gain = state.usesGain ? state.gain : 0.0f;
            const float distance = position.getDistanceFrom({ xForHz(state.frequency, plot), yForDb(gain, plot) });

            if (distance < bestDistance)
            {
                best = band;
                bestDistance = distance;
            }
        }

        return best;
    }

    void applyDrag(juce::Point<float> position)
    {
        const auto plot = plotArea();

        if (selected == hpfId)
        {
            model.setFloat(scoped("preamp/hpf"), hpfFrequencyUnit(std::min(400.0f, hzForX(position.x, plot))));
        }
        else if (selected >= 0 && selected < bandCount)
        {
            const auto prefix = "eq/" + juce::String(selected + 1) + "/";
            const auto state = readBand(selected);
            model.setFloat(scoped(prefix + "f"), eqFrequencyUnit(hzForX(position.x, plot)));

            if (state.usesGain)
                model.setFloat(scoped(prefix + "g"), eqGainUnit(dbForY(position.y, plot)));
        }

        repaint();
    }

    static void drawHandle(juce::Graphics& g,
                           juce::Rectangle<float> plot,
                           float x,
                           float y,
                           juce::Colour colour,
                           bool enabled,
                           bool active,
                           const juce::String& label)
    {
        juce::ignoreUnused(plot);
        const float radius = active ? 9.0f : 7.0f;
        g.setColour(enabled ? colour : colour.withAlpha(0.35f));
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff15191e));
        g.drawEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f, active ? 2.0f : 1.0f);
        g.setColour(juce::Colour(0xff15191e));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(label, juce::roundToInt(x - radius), juce::roundToInt(y - radius), juce::roundToInt(radius * 2.0f), juce::roundToInt(radius * 2.0f), juce::Justification::centred, false);
    }

    static void drawReadout(juce::Graphics& g, juce::Rectangle<float> plot, float x, float y, const juce::String& readout)
    {
        const int width = 180;
        int left = juce::roundToInt(x + 12.0f);

        if (left + width > plot.getRight())
            left = juce::roundToInt(x - 12.0f - width);

        g.setColour(juce::Colour(0xff101317).withAlpha(0.85f));
        g.fillRoundedRectangle((float) left, y - 18.0f, (float) width, 16.0f, 3.0f);
        g.setColour(text());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(readout, left + 4, juce::roundToInt(y - 18.0f), width - 8, 16, juce::Justification::centredLeft, false);
    }

    juce::String scoped(const juce::String& leaf) const
    {
        return pathRoot.isNotEmpty() ? pathRoot + "/" + leaf : channelPath(index, leaf);
    }

    ConsoleModel& model;
    int index = 0;
    int selected = noneId;
    juce::String pathRoot;
    int bandCount = numChannelEqBands;
    bool showHpf = true;
};

class SendsBoard : public juce::Component
{
public:
    SendsBoard(ConsoleModel& modelToUse, int& channelIndex)
        : model(modelToUse),
          channel(channelIndex)
    {
    }

    void paint(juce::Graphics& g) override
    {
        const auto laid = layout();
        g.setColour(juce::Colour(0xff171b20));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f);

        auto note = juce::Rectangle<int>(laid.origin.getX(), laid.tapTop, laid.gutter - 8, laid.nameTop - laid.tapTop);
        g.setColour(dim());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawFittedText("Changes affect\nselected\nchannel", note, juce::Justification::centred, 4);

        int icon = -1;
        model.getInt(channelPath(channel, "config/icon"), icon);
        auto badge = juce::Rectangle<int>(laid.origin.getX() + 8, laid.nameTop, 28, 28);
        g.setColour(juce::Colour(0xff101418));
        g.fillRoundedRectangle(badge.toFloat(), 4.0f);
        if (icon >= 0)
            drawChannelIcon(g, badge.toFloat().reduced(2.0f), icon);
        g.setColour(text());
        g.drawFittedText(model.getChannelName(channel),
                         juce::Rectangle<int>(badge.getRight() + 4, laid.nameTop, laid.gutter - 44, 28),
                         juce::Justification::centredLeft,
                         2);

        g.setFont(juce::Font(juce::FontOptions(10.0f)));

        for (int row = 0; row < tapCount; ++row)
            g.drawText(tapLabel[row], laid.origin.getX(), laid.tapTop + row * laid.tapH, laid.gutter - 10, laid.tapH, juce::Justification::centredRight, true);

        for (float db : { 10.0f, 0.0f, -10.0f, -20.0f, -40.0f, -90.0f })
        {
            const float y = levelY(dbToFader(db), laid);
            g.setColour(db == 0.0f ? juce::Colour(0xfff4f7fb) : dim());
            g.drawText(db <= -90.0f ? juce::String("-inf") : juce::String(juce::roundToInt(db)),
                       laid.origin.getX(),
                       juce::roundToInt(y) - 7,
                       laid.gutter - 12,
                       14,
                       juce::Justification::centredRight,
                       false);
        }

        for (int bus = 0; bus < numBuses; ++bus)
        {
            const int x = laid.origin.getX() + laid.gutter + bus * laid.colW;
            auto column = juce::Rectangle<int>(x, laid.tapTop, laid.colW, getHeight() - laid.tapTop - 4);
            int tap = 0;
            model.getInt(channelPath(channel, "mix/" + juce::String(bus + 1).paddedLeft('0', 2) + "/tap"), tap);
            float level = 0.0f;
            model.getFloat(channelPath(channel, "mix/" + juce::String(bus + 1).paddedLeft('0', 2) + "/level"), level);

            for (int row = 0; row < tapCount; ++row)
            {
                const float cx = (float) x + (float) laid.colW * 0.5f;
                const float cy = (float) laid.tapTop + ((float) row + 0.5f) * (float) laid.tapH;
                const bool on = tap == tapValue[row];
                g.setColour(on ? juce::Colour(0xff3ee0d0) : juce::Colour(0xff6d7782));
                g.drawEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.3f);

                if (on)
                    g.fillEllipse(cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
            }

            auto plate = juce::Rectangle<int>(x + 3, laid.nameTop, laid.colW - 6, laid.nameH);
            g.setColour(busPlate(bus));
            g.fillRoundedRectangle(plate.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
            g.drawFittedText(model.getBusName(bus), plate.reduced(3, 1), juce::Justification::centred, 2);

            const float db = faderToDb(level);
            g.setColour(text());
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(db <= -90.0f ? juce::String("-inf") : juce::String(db, 1),
                       x,
                       laid.dbTop,
                       laid.colW,
                       16,
                       juce::Justification::centred,
                       false);

            auto lane = juce::Rectangle<float>((float) x + 8.0f, (float) laid.faderTop, (float) laid.colW - 16.0f, (float) (laid.faderBottom - laid.faderTop));
            g.setColour(juce::Colour(0xff2a313a));
            g.fillRect(lane.getCentreX() - 1.0f, lane.getY(), 2.0f, lane.getHeight());
            const float y = levelY(level, laid);
            g.setColour(juce::Colour(0xff7eb6d6));
            g.fillRoundedRectangle(lane.getX(), y - 3.0f, lane.getWidth(), 6.0f, 2.0f);

            g.setColour(dim());
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText("Bus " + juce::String(bus + 1), x, laid.labelTop, laid.colW, 16, juce::Justification::centred, false);
            juce::ignoreUnused(column);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto hit = hitTest(event.getPosition());

        if (hit.bus < 0)
            return;

        if (hit.tap >= 0)
        {
            model.setInt(channelPath(channel, "mix/" + busNumber(hit.bus) + "/tap"), tapValue[hit.tap]);
            repaint();
            return;
        }

        if (hit.level)
        {
            dragBus = hit.bus;
            writeLevel(hit.bus, event.position.y);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragBus >= 0)
            writeLevel(dragBus, event.position.y);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragBus = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto hit = hitTest(event.getPosition());

        if (hit.level && hit.bus >= 0)
        {
            model.setFloat(channelPath(channel, "mix/" + busNumber(hit.bus) + "/level"), 0.0f);
            repaint();
        }
    }

private:
    struct Board
    {
        juce::Point<int> origin;
        int gutter = 92;
        int colW = 1;
        int tapTop = 0;
        int tapH = 16;
        int nameTop = 0;
        int nameH = 34;
        int dbTop = 0;
        int faderTop = 0;
        int faderBottom = 0;
        int labelTop = 0;
    };

    struct Hit
    {
        int bus = -1;
        int tap = -1;
        bool level = false;
    };

    static constexpr int tapCount = 5;
    static constexpr int tapValue[tapCount] = { 2, 3, 1, 0, 4 };
    static constexpr const char* tapLabel[tapCount] = { "Pre EQ", "Post EQ", "Pre Fader", "Post Fader", "Sub Group" };

    Board layout() const
    {
        Board board;
        auto bounds = getLocalBounds().reduced(4, 2);
        board.origin = bounds.getPosition();
        board.gutter = juce::jlimit(88, 120, bounds.getWidth() / 8);
        board.colW = juce::jmax(1, (bounds.getWidth() - board.gutter) / numBuses);
        board.tapTop = bounds.getY();
        board.tapH = 16;
        board.nameTop = board.tapTop + tapCount * board.tapH + 6;
        board.nameH = 34;
        board.dbTop = board.nameTop + board.nameH;
        board.labelTop = bounds.getBottom() - 16;
        board.faderTop = board.dbTop + 18;
        board.faderBottom = juce::jmax(board.faderTop + 8, board.labelTop - 2);
        return board;
    }

    static juce::Colour busPlate(int bus)
    {
        static const juce::uint32 colours[] = {
            0xff1d4e89, 0xff1f6b45, 0xff8a5a12, 0xff0e6e78,
            0xff8a2f62, 0xff5a3d86, 0xff2f4f6f, 0xff8a3d22,
            0xff245c8a, 0xff2a7a48, 0xff6e4a28, 0xff14686a,
            0xff7a3a58, 0xff4a4a8a, 0xff3d5a3a, 0xff6a3030
        };
        return juce::Colour(colours[bus % 16]);
    }

    static juce::String busNumber(int bus)
    {
        return juce::String(bus + 1).paddedLeft('0', 2);
    }

    static float levelY(float unit, const Board& board)
    {
        const float span = (float) juce::jmax(1, board.faderBottom - board.faderTop);
        return (float) board.faderBottom - clamp01(unit) * span;
    }

    Hit hitTest(juce::Point<int> position) const
    {
        const auto board = layout();
        Hit hit;
        const int local = position.x - board.origin.getX() - board.gutter;

        if (local < 0)
            return hit;

        hit.bus = local / board.colW;

        if (hit.bus < 0 || hit.bus >= numBuses)
        {
            hit.bus = -1;
            return hit;
        }

        if (position.y >= board.tapTop && position.y < board.nameTop)
        {
            const int row = (position.y - board.tapTop) / board.tapH;

            if (row >= 0 && row < tapCount)
                hit.tap = row;
        }
        else if (position.y >= board.faderTop && position.y <= board.faderBottom)
            hit.level = true;

        return hit;
    }

    void writeLevel(int bus, float y)
    {
        const auto board = layout();
        const float span = (float) juce::jmax(1, board.faderBottom - board.faderTop);
        const float unit = ((float) board.faderBottom - y) / span;
        model.setFloat(channelPath(channel, "mix/" + busNumber(bus) + "/level"), clamp01(unit));
        repaint();
    }

    ConsoleModel& model;
    int& channel;
    int dragBus = -1;
};

class BusSendColumn : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    BusSendColumn(ConsoleModel& modelToUse, int& channelToUse)
        : model(modelToUse), channel(channelToUse)
    {
        for (auto& level : levels)
            level = 0.0f;
    }

    void refresh()
    {
        for (int bus = 0; bus < numBuses; ++bus)
        {
            if (bus == dragBus)
                continue;

            float level = 0.0f;
            model.getFloat(channelPath(channel, "mix/" + busNumber(bus) + "/level"), level);
            levels[(size_t) bus] = level;
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().reduced(4, 2);
        g.setColour(text());
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("Bus Sends", bounds.removeFromTop(18), juce::Justification::centredLeft);

        for (int bus = 0; bus < numBuses; ++bus)
        {
            auto row = rowFor(bus);
            auto number = row.removeFromLeft(26);
            int stored = bus % 8;
            model.getInt("/bus/" + busNumber(bus) + "/config/color", stored);
            const auto colour = stripColourFrom(stored);
            auto numberArea = number.toFloat().reduced(3.0f, 1.0f);

            if (stripColourIndex(stored) == 0)
            {
                g.setColour(juce::Colour(0xff101418));
                g.fillRoundedRectangle(numberArea, 3.0f);
                g.setColour(juce::Colours::white);
            }
            else
            {
                g.setColour(colour);
            }

            g.setFont(juce::Font(juce::FontOptions(juce::jmax(11.0f, (float) number.getHeight() * 0.55f))));
            g.drawText(juce::String(bus + 1), number, juce::Justification::centredRight);

            const auto track = trackFor(row);
            g.setColour(juce::Colour(0xff2a3038));
            g.fillRect(track);

            const float value = clamp01(levels[(size_t) bus]);
            auto fill = track.withWidth(juce::jmax(0.0f, track.getWidth() * value));
            g.setColour(colour);
            g.fillRect(fill);

            const float unityX = track.getX() + track.getWidth() * 0.75f;
            g.setColour(juce::Colours::black.withAlpha(0.85f));
            g.drawLine(unityX + 1.0f, track.getY() - 3.0f, unityX + 1.0f, track.getBottom() + 3.0f, 1.0f);
            g.setColour(juce::Colour(0xfff4f7fa));
            g.drawLine(unityX, track.getY() - 3.0f, unityX, track.getBottom() + 3.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragBus = busAt(event.position);
        apply(event.position, event.getNumberOfClicks() > 1);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        apply(event.position, false);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragBus = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        dragBus = busAt(event.position);
        apply(event.position, true);
        dragBus = -1;
    }

private:
    static juce::String busNumber(int bus)
    {
        return juce::String(bus + 1).paddedLeft('0', 2);
    }

    juce::Rectangle<int> rowFor(int bus) const
    {
        auto bounds = getLocalBounds().reduced(4, 2);
        bounds.removeFromTop(18);
        const int rowH = juce::jmax(1, bounds.getHeight() / numBuses);
        const int y = bounds.getY() + bus * rowH;
        const int height = bus == numBuses - 1 ? bounds.getBottom() - y : rowH;
        return { bounds.getX(), y, bounds.getWidth(), juce::jmax(1, height) };
    }

    juce::Rectangle<float> trackFor(juce::Rectangle<int> row) const
    {
        auto track = row.toFloat().reduced(6.0f, 0.0f);
        const float height = juce::jlimit(6.0f, 12.0f, row.getHeight() * 0.46f);
        return track.withSizeKeepingCentre(track.getWidth(), height);
    }

    int busAt(juce::Point<float> position) const
    {
        for (int bus = 0; bus < numBuses; ++bus)
            if (rowFor(bus).contains(position.toInt()))
                return bus;

        return -1;
    }

    void apply(juce::Point<float> position, bool unity)
    {
        if (dragBus < 0)
            return;

        auto row = rowFor(dragBus);
        row.removeFromLeft(26);
        const auto track = trackFor(row);
        const float unit = unity ? 0.75f : clamp01((position.x - track.getX()) / juce::jmax(1.0f, track.getWidth()));
        levels[(size_t) dragBus] = unit;
        model.setFloat(channelPath(channel, "mix/" + busNumber(dragBus) + "/level"), unit);
        setTooltip("Bus " + juce::String(dragBus + 1) + " send  " + decibelText(faderToDb(unit)) + ". The tick is unity");
        repaint();
    }

    ConsoleModel& model;
    int& channel;
    std::array<float, numBuses> levels {};
    int dragBus = -1;
};
} // namespace

class ChannelEditor::Surface : public juce::Component
{
public:
    enum class Page
    {
        channel,
        eq,
        dyn,
        sends
    };

    Surface(ChannelEditor& ownerToUse, ConsoleModel& modelToUse)
        : owner(ownerToUse),
          model(modelToUse),
          eqGraph(modelToUse),
          gateGraph(modelToUse, DynamicsGraph::Kind::gate),
          compGraph(modelToUse, DynamicsGraph::Kind::compressor)
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

        auto addFlag = [this](const juce::String& label, const juce::String& path)
        {
            auto button = std::make_unique<FlagButton>(label, path, model, index);
            addAndMakeVisible(*button);
            flags.push_back(std::move(button));
            return flags.back().get();
        };

        auto addChoice = [this](const juce::String& title, const juce::String& path, const juce::StringArray& items)
        {
            auto choice = std::make_unique<ChoiceBox>(title, path, model, index, items);
            addAndMakeVisible(*choice);
            choices.push_back(std::move(choice));
            return choices.back().get();
        };

        indexLabel.setJustificationType(juce::Justification::centredLeft);
        indexLabel.setFont(juce::Font(juce::FontOptions(16.0f)));
        indexLabel.setColour(juce::Label::textColourId, text());
        addAndMakeVisible(indexLabel);
        channelIcon.onClick = [this]
        {
            openChannelIconPicker(channelIcon, model, index, [this] { refresh(); });
        };
        channelIcon.onDoubleClick = [this]
        {
            openStripColourPicker(channelIcon, model, channelPath(index, "config/color"), [this] { refresh(); });
        };
        addAndMakeVisible(channelIcon);

        name.setJustificationType(juce::Justification::centredLeft);
        name.setEditable(false, true, false);
        name.setFont(juce::Font(juce::FontOptions(16.0f)));
        name.setColour(juce::Label::textColourId, text());
        name.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        name.onTextChange = [this]
        {
            model.setChannelName(index, name.getText());
            model.setString(channelPath(index, "config/name"), name.getText());
        };
        addAndMakeVisible(name);

        sourceReadout.setJustificationType(juce::Justification::centredLeft);
        sourceReadout.setFont(juce::Font(juce::FontOptions(13.0f)));
        sourceReadout.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(sourceReadout);

        for (auto* button : { &previousButton, &nextButton, &linkButton, &copyButton, &pasteButton })
        {
            button->setColour(juce::TextButton::buttonOnColourId, accent());
            addAndMakeVisible(*button);
        }

        previousButton.onClick = [this] { changeChannel(index - 1); };
        nextButton.onClick = [this] { changeChannel(index + 1); };
        linkButton.setClickingTogglesState(true);
        linkButton.setTooltip("Link with the odd/even partner. Pan stays independent.");
        linkButton.onClick = [this]
        {
            model.setChannelLinked(index, linkButton.getToggleState());
            refresh();
        };
        copyButton.onClick = [this]
        {
            model.copyChannel(index);
            pasteButton.setEnabled(model.hasChannelClipboard());
        };
        pasteButton.setEnabled(false);
        pasteButton.onClick = [this]
        {
            model.pasteChannel(index);
            refresh();
        };

        homeTab.onClick = [this] { show(Page::channel); };
        eqTab.onClick = [this] { show(Page::eq); };
        dynTab.onClick = [this] { show(Page::dyn); };
        sendsTab.onClick = [this] { show(Page::sends); };

        for (auto* tab : { &homeTab, &eqTab, &dynTab, &sendsTab })
            addAndMakeVisible(*tab);

        trim = addSlider("Trim", "preamp/trim", [](float value) { return decibelText(trimToDb(value)); });
        polarity = addFlag("Polarity", "preamp/invert");
        phantom = addFlag("+48V", "preamp/phantom");
        phantom->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe23b3b));
        phantom->setTooltip("Phantom power. Stored for a real mixer; it does not change the virtual audio");
        hpfOn = addFlag("HPF", "preamp/hpon");
        eqOn = addFlag("EQ", "eq/on");
        hpfFreq = addSlider("Frequency", "preamp/hpf", [](float value) { return hertzText(hpfFrequencyHz(value)); });

        sourceLabel.setText("Input", juce::dontSendNotification);
        sourceLabel.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(sourceLabel);
        sourceBox.setTextWhenNothingSelected("None");
        sourceBox.setScrollWheelEnabled(false);
        sourceBox.addItem("None", 1);

        for (int input = 0; input < numRoutableInputs; ++input)
            sourceBox.addItem(ConsoleModel::inputName(input), input + 2);

        sourceBox.onChange = [this]
        {
            const int id = sourceBox.getSelectedId();
            model.setChannelSource(index, id <= 1 ? -1 : id - 2);
            refresh();
        };
        addAndMakeVisible(sourceBox);
        mainAssign = addFlag("Stereo", "mix/st");
        monoAssign = addFlag("Mono", "mix/mono");
        monoLevel = addSlider("M Level", "mix/mlevel", [](float value) { return decibelText(faderToDb(value)); });
        delayOn = addFlag("Delay", "delay/on");
        delayTime = addSlider("Time", "delay/time", [](float value) { return millisecondsText(delaySeconds(value) * 1000.0f); });

        gateOn = addFlag("Active", "gate/on");
        gateOn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a7a));
        gateOn->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1c2128));
        gateMode = addChoice("Mode", "gate/mode", juce::StringArray { "GATE", "EXP2", "EXP3", "EXP4", "DUCK" });
        gateThr = addSlider("Threshold", "gate/thr", [](float value) { return decibelText(gateThresholdDb(value)); });
        gateRange = addSlider("Range", "gate/range", [](float value) { return decibelText(gateRangeDb(value)); });
        gateAttack = addSlider("Attack", "gate/attack", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 120.0f)); });
        gateHold = addSlider("Hold", "gate/hold", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 2000.0f)); });
        gateRelease = addSlider("Release", "gate/release", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 5.0f, 4000.0f)); });

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            const auto prefix = "eq/" + juce::String(band + 1) + "/";
            auto& row = eq[band];
            row.on = addFlag(juce::String(band + 1), prefix + "on");
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

        compOn = addFlag("Active", "dyn/on");
        compOn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe0b040));
        compOn->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1c2128));
        compMode = addChoice("Mode", "dyn/mode", juce::StringArray { "COMP", "EXP" });
        compDetect = addChoice("Det", "dyn/det", juce::StringArray { "PEAK", "RMS" });
        compEnvelope = addChoice("Env", "dyn/env", juce::StringArray { "LIN", "LOG" });
        compPosition = addChoice("Pos", "dyn/pos", juce::StringArray { "POST", "PRE" });
        compAuto = addFlag("Auto", "dyn/auto");
        compFilter = addFlag("Filter", "dyn/filter/on");
        compFilterType = addChoice("Filter", "dyn/filter/type", juce::StringArray { "LC6", "LC12", "HC6", "HC12", "Q 1", "Q 2", "Q 3", "Q 5", "Q 10" });
        compThr = addSlider("Threshold", "dyn/thr", [](float value) { return decibelText(vc::compThresholdDb(value)); });
        compRatio = addSlider("Ratio", "dyn/ratio", [](float value) { return juce::String(vc::compRatio(value), 1) + ":1"; });
        compKnee = addSlider("Knee", "dyn/knee", [](float value) { return decibelText(compKneeDb(value)); });
        compMakeup = addSlider("Gain", "dyn/mgain", [](float value) { return decibelText(vc::compMakeupDb(value)); });
        compMix = addSlider("Mix", "dyn/mix", [](float value) { return juce::String(juce::roundToInt(clamp01(value) * 100.0f)) + "%"; });
        compAttack = addSlider("Attack", "dyn/attack", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 120.0f)); });
        compHold = addSlider("Hold", "dyn/hold", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 2000.0f)); });
        compRelease = addSlider("Release", "dyn/release", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 5.0f, 4000.0f)); });
        compFilterFreq = addSlider("Freq", "dyn/filter/f", [](float value) { return hertzText(eqFrequencyHz(value)); });

        auto addBox = [this](ControlBox& box, const juce::String& title, juce::Colour colour)
        {
            box.setTitle(title, colour);
            addAndMakeVisible(box);
            box.toBack();
        };
        addBox(preampBox, "Preamp", juce::Colour(0xff7eb6d6));
        addBox(configBox, "Config", juce::Colour(0xffd7dbe2));
        addBox(delayBox, "Delay", juce::Colour(0xffe0b040));
        addBox(gateBox, "Gate", juce::Colour(0xffe07a7a));
        addBox(dynBox, "Dynamics", juce::Colour(0xffe0b040));
        addBox(hpfBox, "HPF", juce::Colour(0xfff2f5f8));

        for (int band = 0; band < numChannelEqBands; ++band)
            addBox(bandBox[band], "Band " + juce::String(band + 1), eqBandColour(band));

        for (int bus = 0; bus < numBuses; ++bus)
        {
            const auto number = juce::String(bus + 1).paddedLeft('0', 2);
            sendLevel[bus] = addSlider("Level", "mix/" + number + "/level", [](float value) { return decibelText(faderToDb(value)); });
            sendLevel[bus]->setVerticalBar(true);
            sendTap[bus] = addChoice("Tap", "mix/" + number + "/tap", juce::StringArray { "Post", "Pre", "Pre EQ", "Post EQ", "Sub" });
            addBox(sendBox[bus], "Bus " + juce::String(bus + 1), juce::Colour(0xff7eb6d6));
        }

        addAndMakeVisible(eqGraph);
        addAndMakeVisible(gateGraph);
        addAndMakeVisible(compGraph);
        sendsBoard = std::make_unique<SendsBoard>(model, index);
        addAndMakeVisible(*sendsBoard);
        groupPad = std::make_unique<GroupPad>(model);
        addAndMakeVisible(*groupPad);
        busSends = std::make_unique<BusSendColumn>(model, index);
        addAndMakeVisible(*busSends);
        show(Page::channel);
        refresh();
    }

    int getChannel() const noexcept { return index; }

    void setChannel(int channelIndex)
    {
        index = juce::jlimit(0, numChannels - 1, channelIndex);
        eqGraph.setChannel(index);
        gateGraph.setChannel(index);
        compGraph.setChannel(index);
        refresh();
    }

    void setSourceNames(const juce::StringArray& names)
    {
        sourceNames = names;
        refresh();
    }

    void refresh()
    {
        indexLabel.setText(juce::String(index + 1).paddedLeft('0', 2), juce::dontSendNotification);
        int icon = -1;
        model.getInt(channelPath(index, "config/icon"), icon);
        channelIcon.setIcon(icon);
        int colour = 6;
        model.getInt(channelPath(index, "config/color"), colour);
        channelIcon.setStripColour(colour);
        name.setColour(juce::Label::textColourId, stripNameColour(colour));

        if (sendsBoard != nullptr)
            sendsBoard->repaint();

        if (groupPad != nullptr)
            groupPad->setRoot("/ch/" + juce::String(index + 1).paddedLeft('0', 2));

        if (busSends != nullptr)
            busSends->refresh();

        if (! name.isBeingEdited())
            name.setText(model.getChannelName(index), juce::dontSendNotification);

        const int route = model.getChannelSource(index);
        const int patched = model.getInputSource(route);
        juce::String readout = model.inputName(route);

        if (patched >= 0 && patched < sourceNames.size())
            readout += "  ·  " + sourceNames[patched];
        else if (route >= 0)
            readout += "  ·  no source";

        sourceReadout.setText(readout, juce::dontSendNotification);
        const bool auxInput = route >= numLocalInputs && route < numRoutableInputs;
        trim->setTitle(auxInput ? "Trim" : "Gain");

        if (! sourceBox.isPopupActive())
            sourceBox.setSelectedId(route < 0 ? 1 : route + 2, juce::dontSendNotification);

        linkButton.setToggleState(model.isChannelLinked(index), juce::dontSendNotification);
        pasteButton.setEnabled(model.hasChannelClipboard());

        for (auto& slider : sliders)
            slider->refresh();

        for (auto& flag : flags)
            flag->refresh();

        for (auto& choice : choices)
            choice->refresh();

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            if (eq[band].type.isPopupActive())
                continue;

            int type = 2;
            model.getInt(channelPath(index, "eq/" + juce::String(band + 1) + "/t"), type);
            eq[band].type.setSelectedId(juce::jlimit(0, 5, type) + 1, juce::dontSendNotification);
        }

        styleTabs();
        eqGraph.setChannel(index);
        gateGraph.setChannel(index);
        compGraph.setChannel(index);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel());
        int stored = 6;
        model.getInt(channelPath(index, "config/color"), stored);
        g.setColour(stripColourFrom(stored));
        g.fillRect(0, 0, 6, getHeight());
        g.setColour(juce::Colour(0xff2c333b));
        g.fillRect(0, 76, getWidth(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(10);
        auto head = area.removeFromTop(40).reduced(8, 6);
        previousButton.setBounds(head.removeFromLeft(32));
        head.removeFromLeft(4);
        nextButton.setBounds(head.removeFromLeft(32));
        head.removeFromLeft(8);
        indexLabel.setBounds(head.removeFromLeft(36));
        channelIcon.setBounds(head.removeFromLeft(32).reduced(1, 1));
        name.setBounds(head.removeFromLeft(juce::jmin(220, head.getWidth() / 3)));
        sourceReadout.setBounds(head.removeFromLeft(juce::jmin(220, head.getWidth() / 2)).reduced(8, 0));
        pasteButton.setBounds(head.removeFromRight(72));
        head.removeFromRight(6);
        copyButton.setBounds(head.removeFromRight(72));
        head.removeFromRight(6);
        linkButton.setBounds(head.removeFromRight(72));

        auto tabs = area.removeFromTop(36).reduced(8, 4);
        for (auto* tab : { &homeTab, &eqTab, &dynTab, &sendsTab })
        {
            tab->setBounds(tabs.removeFromLeft(92));
            tabs.removeFromLeft(6);
        }

        auto body = area.reduced(12, 8);
        const int knobW = 92;

        auto takeKnob = [knobW](juce::Rectangle<int>& row, UnitSlider* slider)
        {
            if (slider != nullptr)
                slider->setBounds(row.removeFromLeft(knobW).reduced(4, 0));
        };
        auto takeButton = [](juce::Rectangle<int>& row, juce::Component* button, int width, int height)
        {
            if (button != nullptr)
                button->setBounds(row.removeFromLeft(width).withSizeKeepingCentre(width - 8, height));
        };

        if (page == Page::channel)
        {
            auto sendsColumn = body.removeFromRight(juce::jlimit(210, 280, body.getWidth() / 4));

            if (busSends != nullptr)
                busSends->setBounds(sendsColumn);

            auto configArea = body.removeFromTop(juce::jmin(112, juce::jmax(88, body.getHeight() / 4))).reduced(3);
            configBox.setBounds(configArea);

            auto configInner = configArea.reduced(8, 6);
            configInner.removeFromTop(16);
            auto levelColumn = configInner.removeFromRight(juce::jmin(108, juce::jmax(84, configInner.getWidth() / 5)));

            if (monoLevel != nullptr)
                monoLevel->setBounds(levelColumn.reduced(2, 0));

            auto assign = configInner.removeFromRight(168);
            takeButton(assign, mainAssign, 80, 28);
            takeButton(assign, monoAssign, 80, 28);
            sourceLabel.setBounds(configInner.removeFromTop(16));
            sourceBox.setBounds(configInner.removeFromTop(28).reduced(0, 1));

            const int columnWidth = body.getWidth() / 3;
            auto preampArea = body.removeFromLeft(columnWidth).reduced(3);
            auto delayArea = body.removeFromLeft(columnWidth).reduced(3);
            auto dcaArea = body.reduced(3);
            preampBox.setBounds(preampArea);
            delayBox.setBounds(delayArea);

            auto stackKnob = [](juce::Rectangle<int> inner, UnitSlider* knob, juce::Component* button)
            {
                inner.removeFromTop(16);
                auto buttonRow = inner.removeFromBottom(juce::jmin(44, inner.getHeight() / 4));

                if (button != nullptr)
                    button->setBounds(buttonRow.withSizeKeepingCentre(juce::jmin(96, buttonRow.getWidth()), 28));

                if (knob != nullptr)
                    knob->setBounds(inner.reduced(4, 0));
            };

            auto preampInner = preampArea.reduced(8, 6);
            preampInner.removeFromTop(16);
            auto preampButtons = preampInner.removeFromBottom(juce::jmin(44, preampInner.getHeight() / 4));

            if (polarity != nullptr)
                polarity->setBounds(preampButtons.removeFromLeft(preampButtons.getWidth() / 2).reduced(2, 8));

            if (phantom != nullptr)
                phantom->setBounds(preampButtons.reduced(2, 8));

            if (trim != nullptr)
                trim->setBounds(preampInner.reduced(4, 0));

            stackKnob(delayArea.reduced(8, 6), delayTime, delayOn);

            if (groupPad != nullptr)
                groupPad->setBounds(dcaArea.reduced(4, 2));
        }
        else if (page == Page::eq)
        {
            auto controls = body.removeFromBottom(juce::jmin(148, juce::jmax(118, body.getHeight() / 3)));
            body.removeFromBottom(4);
            auto eqSwitch = body.removeFromTop(30);
            eqOn->setBounds(eqSwitch.removeFromLeft(72).reduced(0, 2));
            eqGraph.setBounds(body);
            auto hpfArea = controls.removeFromLeft(juce::jmin(168, controls.getWidth() / (numChannelEqBands + 1))).reduced(3);
            hpfBox.setBounds(hpfArea);
            hpfArea = hpfArea.reduced(6, 4);
            hpfArea.removeFromTop(16);
            takeButton(hpfArea, hpfOn, 64, 28);
            takeKnob(hpfArea, hpfFreq);
            const int bandWidth = juce::jmax(120, controls.getWidth() / juce::jmax(1, numChannelEqBands));

            for (int band = 0; band < numChannelEqBands; ++band)
            {
                auto column = controls.removeFromLeft(juce::jmin(bandWidth, controls.getWidth())).reduced(3);
                bandBox[band].setBounds(column);
                column = column.reduced(6, 4);
                column.removeFromTop(16);
                auto header = column.removeFromTop(26);
                eq[band].on->setBounds(header.removeFromLeft(36).reduced(1, 1));
                eq[band].type.setBounds(header.reduced(2, 1));
                const int eqKnob = juce::jmax(52, column.getWidth() / 3);
                eq[band].freq->setBounds(column.removeFromLeft(eqKnob).reduced(1, 0));
                eq[band].gain->setBounds(column.removeFromLeft(eqKnob).reduced(1, 0));
                eq[band].q->setBounds(column.removeFromLeft(eqKnob).reduced(1, 0));
            }
        }
        else if (page == Page::dyn)
        {
            auto left = body.removeFromLeft(body.getWidth() / 2).reduced(3);
            auto right = body.reduced(3);
            gateBox.setBounds(left);
            dynBox.setBounds(right);

            auto stack = [](juce::Rectangle<int> column, std::initializer_list<juce::Component*> items)
            {
                const int count = (int) items.size();

                if (count <= 0 || column.getHeight() <= 0)
                    return;

                const int row = juce::jmax(1, column.getHeight() / count);

                for (auto* item : items)
                    if (item != nullptr)
                        item->setBounds(column.removeFromTop(row).reduced(2, 1));
            };

            auto gateInner = left.reduced(8, 6);
            gateInner.removeFromTop(16);
            auto gateControls = gateInner.removeFromRight(juce::jmin(210, juce::jmax(168, gateInner.getWidth() / 3)));
            gateGraph.setBounds(gateInner.reduced(2, 0));
            stack(gateControls, { gateOn, gateMode, gateThr, gateRange, gateAttack, gateHold, gateRelease });

            auto compInner = right.reduced(8, 6);
            compInner.removeFromTop(16);
            auto compControls = compInner.removeFromRight(juce::jmin(220, juce::jmax(176, compInner.getWidth() / 3)));
            auto menuRow = compInner.removeFromBottom(36);
            compGraph.setBounds(compInner.reduced(2, 2));
            const int menuWidth = juce::jmax(1, menuRow.getWidth() / 4);

            for (auto* menu : { compMode, compDetect, compEnvelope, compPosition })
                if (menu != nullptr)
                    menu->setBounds(menuRow.removeFromLeft(menuWidth).reduced(2, 2));

            stack(compControls, { compOn, compAuto, compFilter, compFilterType, compFilterFreq,
                                  compThr, compRatio, compKnee, compMakeup, compMix,
                                  compAttack, compHold, compRelease });
        }
        else if (page == Page::sends)
        {
            if (sendsBoard != nullptr)
                sendsBoard->setBounds(body);
        }
    }

private:
    void changeChannel(int channelIndex)
    {
        const int wrapped = (channelIndex % numChannels + numChannels) % numChannels;

        if (owner.onChannelChanged != nullptr)
            owner.onChannelChanged(wrapped);
        else
            setChannel(wrapped);
    }

    void show(Page next)
    {
        page = next;
        const bool channel = page == Page::channel;
        const bool equaliser = page == Page::eq;
        const bool dyn = page == Page::dyn;
        const bool sends = page == Page::sends;

        trim->setVisible(channel);
        polarity->setVisible(channel);
        phantom->setVisible(channel);
        preampBox.setVisible(channel);
        configBox.setVisible(channel);
        delayBox.setVisible(channel);
        gateBox.setVisible(dyn);
        dynBox.setVisible(dyn);
        hpfOn->setVisible(equaliser);
        eqOn->setVisible(equaliser);
        hpfFreq->setVisible(equaliser);
        hpfBox.setVisible(equaliser);
        sourceLabel.setVisible(channel);
        sourceBox.setVisible(channel);
        mainAssign->setVisible(channel);
        monoAssign->setVisible(channel);
        monoLevel->setVisible(channel);
        delayOn->setVisible(channel);
        delayTime->setVisible(channel);
        gateOn->setVisible(dyn);
        gateMode->setVisible(dyn);
        gateThr->setVisible(dyn);
        gateRange->setVisible(dyn);
        gateAttack->setVisible(dyn);
        gateHold->setVisible(dyn);
        gateRelease->setVisible(dyn);
        gateGraph.setVisible(dyn);
        eqGraph.setVisible(equaliser);
        compGraph.setVisible(dyn);
        compOn->setVisible(dyn);
        compMode->setVisible(dyn);
        compDetect->setVisible(dyn);
        compEnvelope->setVisible(dyn);
        compPosition->setVisible(dyn);
        compAuto->setVisible(dyn);
        compFilter->setVisible(dyn);
        compFilterType->setVisible(dyn);
        compThr->setVisible(dyn);
        compRatio->setVisible(dyn);
        compKnee->setVisible(dyn);
        compMakeup->setVisible(dyn);
        compMix->setVisible(dyn);
        compAttack->setVisible(dyn);
        compHold->setVisible(dyn);
        compRelease->setVisible(dyn);
        compFilterFreq->setVisible(dyn);

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            bandBox[band].setVisible(equaliser);
            eq[band].on->setVisible(equaliser);
            eq[band].type.setVisible(equaliser);
            eq[band].freq->setVisible(equaliser);
            eq[band].gain->setVisible(equaliser);
            eq[band].q->setVisible(equaliser);
        }

        for (int bus = 0; bus < numBuses; ++bus)
        {
            sendBox[bus].setVisible(false);
            sendLevel[bus]->setVisible(false);
            sendTap[bus]->setVisible(false);
        }

        if (sendsBoard != nullptr)
            sendsBoard->setVisible(sends);

        if (groupPad != nullptr)
            groupPad->setVisible(channel);

        if (busSends != nullptr)
            busSends->setVisible(channel);

        styleTabs();
        resized();
    }

    void styleTabs()
    {
        auto style = [](juce::TextButton& button, bool active, bool lit)
        {
            button.setColour(juce::TextButton::buttonColourId, active ? accent() : lit ? engaged() : juce::Colour(0xff2e343d));
            button.setColour(juce::TextButton::textColourOffId, text());
        };

        style(homeTab, page == Page::channel, gateOn != nullptr && gateOn->isOn());
        style(eqTab, page == Page::eq, eqOn != nullptr && eqOn->isOn());
        style(dynTab, page == Page::dyn, (compOn != nullptr && compOn->isOn()) || (gateOn != nullptr && gateOn->isOn()));
        style(sendsTab, page == Page::sends, false);
    }

    struct EqBand
    {
        FlagButton* on = nullptr;
        juce::ComboBox type;
        UnitSlider* freq = nullptr;
        UnitSlider* gain = nullptr;
        UnitSlider* q = nullptr;
    };

    ChannelEditor& owner;
    ConsoleModel& model;
    int index = 0;
    Page page = Page::channel;
    juce::StringArray sourceNames;

    juce::TextButton previousButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::Label indexLabel;
    juce::Label name;
    juce::Label sourceReadout;
    juce::TextButton linkButton { "Link" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton pasteButton { "Paste" };
    ChannelIconButton channelIcon;
    std::unique_ptr<SendsBoard> sendsBoard;
    std::unique_ptr<GroupPad> groupPad;
    std::unique_ptr<BusSendColumn> busSends;
    juce::TextButton homeTab { "Channel" };
    juce::TextButton eqTab { "EQ" };
    juce::TextButton dynTab { "Dyn" };
    juce::TextButton sendsTab { "Sends" };

    UnitSlider* trim = nullptr;
    FlagButton* polarity = nullptr;
    FlagButton* phantom = nullptr;
    FlagButton* hpfOn = nullptr;
    FlagButton* eqOn = nullptr;
    UnitSlider* hpfFreq = nullptr;
    juce::Label sourceLabel;
    juce::ComboBox sourceBox;
    FlagButton* mainAssign = nullptr;
    FlagButton* monoAssign = nullptr;
    UnitSlider* monoLevel = nullptr;
    FlagButton* delayOn = nullptr;
    UnitSlider* delayTime = nullptr;
    FlagButton* gateOn = nullptr;
    ChoiceBox* gateMode = nullptr;
    UnitSlider* gateThr = nullptr;
    UnitSlider* gateRange = nullptr;
    UnitSlider* gateAttack = nullptr;
    UnitSlider* gateHold = nullptr;
    UnitSlider* gateRelease = nullptr;
    EqBand eq[numChannelEqBands];
    FlagButton* compOn = nullptr;
    ChoiceBox* compMode = nullptr;
    ChoiceBox* compDetect = nullptr;
    ChoiceBox* compEnvelope = nullptr;
    ChoiceBox* compPosition = nullptr;
    FlagButton* compAuto = nullptr;
    FlagButton* compFilter = nullptr;
    ChoiceBox* compFilterType = nullptr;
    UnitSlider* compThr = nullptr;
    UnitSlider* compRatio = nullptr;
    UnitSlider* compKnee = nullptr;
    UnitSlider* compMakeup = nullptr;
    UnitSlider* compMix = nullptr;
    UnitSlider* compAttack = nullptr;
    UnitSlider* compHold = nullptr;
    UnitSlider* compRelease = nullptr;
    UnitSlider* compFilterFreq = nullptr;
    ControlBox preampBox;
    ControlBox configBox;
    ControlBox delayBox;
    ControlBox gateBox;
    ControlBox dynBox;
    ControlBox hpfBox;
    ControlBox bandBox[numChannelEqBands];
    ControlBox sendBox[numBuses];
    UnitSlider* sendLevel[numBuses] {};
    ChoiceBox* sendTap[numBuses] {};
    EqGraph eqGraph;
    DynamicsGraph gateGraph;
    DynamicsGraph compGraph;
    std::vector<std::unique_ptr<ChoiceBox>> choices;
    std::vector<std::unique_ptr<UnitSlider>> sliders;
    std::vector<std::unique_ptr<FlagButton>> flags;
};

ChannelEditor::ChannelEditor(ConsoleModel& modelToUse)
    : model(modelToUse),
      surface(std::make_unique<Surface>(*this, model))
{
    addAndMakeVisible(*surface);
}

ChannelEditor::~ChannelEditor() = default;

void ChannelEditor::setChannel(int channelIndex)
{
    surface->setChannel(channelIndex);
}

int ChannelEditor::getChannel() const noexcept
{
    return surface->getChannel();
}

void ChannelEditor::refresh()
{
    surface->refresh();
}

void ChannelEditor::setSourceNames(const juce::StringArray& names)
{
    surface->setSourceNames(names);
}

void ChannelEditor::resized()
{
    surface->setBounds(getLocalBounds());
}

class MatrixEditor::Surface : public juce::Component
{
public:
    enum class Page { home, eq, dyn };

    explicit Surface(ConsoleModel& modelToUse)
        : model(modelToUse),
          eqGraph(modelToUse),
          gateGraph(modelToUse, DynamicsGraph::Kind::gate),
          compGraph(modelToUse, DynamicsGraph::Kind::compressor)
    {
        for (auto* tab : { &homeTab, &eqTab, &dynTab })
        {
            tab->setClickingTogglesState(false);
            tab->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff232830));
            tab->setColour(juce::TextButton::textColourOffId, text());
            addAndMakeVisible(*tab);
        }

        homeTab.onClick = [this] { show(Page::home); };
        eqTab.onClick = [this] { show(Page::eq); };
        dynTab.onClick = [this] { show(Page::dyn); };

        title.setFont(juce::Font(juce::FontOptions(16.0f)));
        title.setColour(juce::Label::textColourId, text());
        addAndMakeVisible(title);

        insert.setClickingTogglesState(true);
        insert.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
        insert.setTooltip("Put the EQ, gate, and compressor before or after this matrix fader");
        insert.onClick = [this]
        {
            model.setInt(root + "/insert/pos", insert.getToggleState() ? 1 : 0);
            insert.setButtonText(insert.getToggleState() ? "Post Fader" : "Pre Fader");
        };
        addAndMakeVisible(insert);

        groupPad = std::make_unique<GroupPad>(model);
        addAndMakeVisible(*groupPad);

        eqOn.setClickingTogglesState(true);
        eqOn.setButtonText("EQ");
        eqOn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d8f5a));
        eqOn.onClick = [this] { model.setInt(root + "/eq/on", eqOn.getToggleState() ? 1 : 0); };
        addAndMakeVisible(eqOn);
        addAndMakeVisible(eqGraph);

        gateOn.setClickingTogglesState(true);
        gateOn.setButtonText("Gate");
        gateOn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a7a));
        gateOn.onClick = [this] { model.setInt(root + "/gate/on", gateOn.getToggleState() ? 1 : 0); };
        addAndMakeVisible(gateOn);
        addAndMakeVisible(gateGraph);

        compOn.setClickingTogglesState(true);
        compOn.setButtonText("Comp");
        compOn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe0b040));
        compOn.onClick = [this] { model.setInt(root + "/dyn/on", compOn.getToggleState() ? 1 : 0); };
        addAndMakeVisible(compOn);
        addAndMakeVisible(compGraph);

        auto addKnob = [this](const juce::String& name, const juce::String& leaf, std::function<juce::String(float)> format)
        {
            auto slider = std::make_unique<UnitSlider>(
                name,
                [this, leaf]
                {
                    float value = 0.0f;
                    model.getFloat(root + "/" + leaf, value);
                    return value;
                },
                [this, leaf](float value) { model.setFloat(root + "/" + leaf, value); },
                std::move(format));
            addAndMakeVisible(*slider);
            knobs.push_back(std::move(slider));
            return knobs.back().get();
        };

        const auto ms = [](float value) { return juce::String(value, 0); };
        gateAttack = addKnob("Attack", "gate/attack", ms);
        gateHold = addKnob("Hold", "gate/hold", ms);
        gateRelease = addKnob("Release", "gate/release", ms);
        compKnee = addKnob("Knee", "dyn/knee", [](float value) { return juce::String(compKneeDb(value), 1); });
        compMakeup = addKnob("Gain", "dyn/mgain", [](float value) { return decibelText(compMakeupDb(value)); });
        compAttack = addKnob("Attack", "dyn/attack", ms);
        compHold = addKnob("Hold", "dyn/hold", ms);
        compRelease = addKnob("Release", "dyn/release", ms);

        setMatrix(0);
        show(Page::home);
    }

    void setMatrix(int matrixIndex)
    {
        index = juce::jlimit(0, numMatrices - 1, matrixIndex);
        root = "/mtx/" + number();
        title.setText("Mtx " + juce::String(index + 1), juce::dontSendNotification);
        eqGraph.setTarget(root, numEqBands, false);
        gateGraph.setPathRoot(root);
        compGraph.setPathRoot(root);
        groupPad->setRoot(root);
        refresh();
    }

    void refresh()
    {
        if (! insert.isMouseButtonDown())
        {
            int post = 0;
            model.getInt(root + "/insert/pos", post);
            insert.setToggleState(post != 0, juce::dontSendNotification);
            insert.setButtonText(post != 0 ? "Post Fader" : "Pre Fader");
        }

        if (! eqOn.isMouseButtonDown())
        {
            int enabled = 1;
            model.getInt(root + "/eq/on", enabled);
            eqOn.setToggleState(enabled != 0, juce::dontSendNotification);
        }

        if (! gateOn.isMouseButtonDown())
        {
            int enabled = 0;
            model.getInt(root + "/gate/on", enabled);
            gateOn.setToggleState(enabled != 0, juce::dontSendNotification);
        }

        if (! compOn.isMouseButtonDown())
        {
            int enabled = 0;
            model.getInt(root + "/dyn/on", enabled);
            compOn.setToggleState(enabled != 0, juce::dontSendNotification);
        }

        for (auto* knob : { gateAttack, gateHold, gateRelease, compKnee, compMakeup, compAttack, compHold, compRelease })
            if (knob != nullptr)
                knob->refresh();

        groupPad->refresh();
        eqGraph.repaint();
        gateGraph.repaint();
        compGraph.repaint();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel());
        int stored = index % 8;
        model.getInt(root + "/config/color", stored);
        g.setColour(stripColourFrom(stored));
        g.fillRect(0, 0, getWidth(), 4);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 8);
        area.removeFromTop(6);
        auto tabs = area.removeFromTop(28);
        const int tabWidth = 88;

        for (auto* tab : { &homeTab, &eqTab, &dynTab })
            tab->setBounds(tabs.removeFromLeft(tabWidth).reduced(2, 0));

        area.removeFromTop(6);
        const bool home = page == Page::home;
        const bool equaliser = page == Page::eq;
        const bool dynamics = page == Page::dyn;

        title.setVisible(home);
        insert.setVisible(home);
        groupPad->setVisible(home);
        eqOn.setVisible(equaliser);
        eqGraph.setVisible(equaliser);
        gateOn.setVisible(dynamics);
        gateGraph.setVisible(dynamics);
        compOn.setVisible(dynamics);
        compGraph.setVisible(dynamics);

        for (auto* knob : { gateAttack, gateHold, gateRelease })
            if (knob != nullptr)
                knob->setVisible(dynamics);

        for (auto* knob : { compKnee, compMakeup, compAttack, compHold, compRelease })
            if (knob != nullptr)
                knob->setVisible(dynamics);

        if (home)
        {
            auto header = area.removeFromTop(32);
            title.setBounds(header.removeFromLeft(120));
            insert.setBounds(header.removeFromLeft(120).reduced(4, 2));
            groupPad->setBounds(area.reduced(4, 4));
        }
        else if (equaliser)
        {
            eqOn.setBounds(area.removeFromTop(28).removeFromLeft(72));
            eqGraph.setBounds(area.reduced(2, 4));
        }
        else if (dynamics)
        {
            auto left = area.removeFromLeft(area.getWidth() / 2).reduced(3);
            auto right = area.reduced(3);
            auto gateButtons = left.removeFromTop(28);
            gateOn.setBounds(gateButtons.removeFromLeft(72));
            auto gateKnobs = left.removeFromBottom(juce::jmin(92, left.getHeight() / 3));
            gateGraph.setBounds(left.reduced(2));
            const int gateWidth = gateKnobs.getWidth() / 3;
            gateAttack->setBounds(gateKnobs.removeFromLeft(gateWidth));
            gateHold->setBounds(gateKnobs.removeFromLeft(gateWidth));
            gateRelease->setBounds(gateKnobs);

            auto compButtons = right.removeFromTop(28);
            compOn.setBounds(compButtons.removeFromLeft(72));
            auto compKnobs = right.removeFromBottom(juce::jmin(92, right.getHeight() / 3));
            compGraph.setBounds(right.reduced(2));
            const int compWidth = compKnobs.getWidth() / 5;
            compKnee->setBounds(compKnobs.removeFromLeft(compWidth));
            compMakeup->setBounds(compKnobs.removeFromLeft(compWidth));
            compAttack->setBounds(compKnobs.removeFromLeft(compWidth));
            compHold->setBounds(compKnobs.removeFromLeft(compWidth));
            compRelease->setBounds(compKnobs);
        }

        for (auto* tab : { &homeTab, &eqTab, &dynTab })
        {
            const bool on = (tab == &homeTab && home) || (tab == &eqTab && equaliser) || (tab == &dynTab && dynamics);
            tab->setColour(juce::TextButton::buttonColourId, on ? juce::Colour(0xff3d6f8f) : juce::Colour(0xff232830));
        }
    }

private:
    void show(Page next)
    {
        page = next;
        resized();
    }

    juce::String number() const
    {
        return juce::String(index + 1).paddedLeft('0', 2);
    }

    ConsoleModel& model;
    int index = 0;
    juce::String root;
    Page page = Page::home;
    juce::TextButton homeTab { "Home" };
    juce::TextButton eqTab { "EQ" };
    juce::TextButton dynTab { "Dyn" };
    juce::Label title;
    juce::TextButton insert { "Pre Fader" };
    std::unique_ptr<GroupPad> groupPad;
    juce::TextButton eqOn { "EQ" };
    EqGraph eqGraph;
    juce::TextButton gateOn { "Gate" };
    juce::TextButton compOn { "Comp" };
    DynamicsGraph gateGraph;
    DynamicsGraph compGraph;
    UnitSlider* gateAttack = nullptr;
    UnitSlider* gateHold = nullptr;
    UnitSlider* gateRelease = nullptr;
    UnitSlider* compKnee = nullptr;
    UnitSlider* compMakeup = nullptr;
    UnitSlider* compAttack = nullptr;
    UnitSlider* compHold = nullptr;
    UnitSlider* compRelease = nullptr;
    std::vector<std::unique_ptr<UnitSlider>> knobs;
};

MatrixEditor::MatrixEditor(ConsoleModel& modelToUse)
    : model(modelToUse),
      surface(std::make_unique<Surface>(model))
{
    addAndMakeVisible(*surface);
}

MatrixEditor::~MatrixEditor() = default;

void MatrixEditor::setMatrix(int matrixIndex)
{
    surface->setMatrix(matrixIndex);
}

void MatrixEditor::refresh()
{
    surface->refresh();
}

void MatrixEditor::resized()
{
    surface->setBounds(getLocalBounds());
}

} // namespace vc
