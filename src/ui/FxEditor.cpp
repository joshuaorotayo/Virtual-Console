// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/FxEditor.h"

#include "engine/FxProcessor.h"

namespace vc
{
namespace
{
juce::String slotPath(int slot)
{
    return "/fx/" + juce::String(slot + 1).paddedLeft('0', 2);
}

const char* paramName(int type, int index)
{
    const bool delay = type == (int) FxAlgo::stereoDelay;
    const bool chorus = type == (int) FxAlgo::stereoChorus;
    const bool flanger = type == (int) FxAlgo::stereoFlanger;
    const bool phaser = type == (int) FxAlgo::stereoPhaser;
    const bool tremolo = type == (int) FxAlgo::tremoloPanner;
    static const char* room[] = { "Decay", "Predelay", "Size", "Damping" };
    static const char* delayNames[] = { "Time", "Feedback", "Right", "Damping" };
    static const char* chorusNames[] = { "Rate", "Depth", "Width", "Tone" };
    static const char* flangerNames[] = { "Rate", "Depth", "Feedback", "Manual" };
    static const char* phaserNames[] = { "Rate", "Depth", "Feedback", "Spread" };
    static const char* tremoloNames[] = { "Rate", "Depth", "Phase", "Pan" };

    if (delay)
        return delayNames[index];

    if (chorus)
        return chorusNames[index];

    if (flanger)
        return flangerNames[index];

    if (phaser)
        return phaserNames[index];

    if (tremolo)
        return tremoloNames[index];

    return room[index];
}
} // namespace

FxEditor::FxEditor(ConsoleModel& modelToUse)
    : model(modelToUse)
{
    title.setFont(juce::Font(juce::FontOptions(18.0f)));
    title.setColour(juce::Label::textColourId, juce::Colour(0xffe8edf2));
    title.setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff1c2128));
    title.setEditable(false, true, false);
    title.onTextChange = [this] { model.setFxReturnName(fxReturn, title.getText()); };
    addAndMakeVisible(title);
    sendLabel.setColour(juce::Label::textColourId, juce::Colour(0xff98a2ad));
    addAndMakeVisible(sendLabel);

    for (int type = 0; type <= (int) FxAlgo::tremoloPanner; ++type)
        typeBox.addItem(fxAlgoName(type), type + 1);

    typeBox.onChange = [this]
    {
        if (! applying && typeBox.getSelectedId() > 0)
            applyType(typeBox.getSelectedId() - 1);
    };
    addAndMakeVisible(typeBox);

    enabled.setClickingTogglesState(true);
    enabled.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
    enabled.onClick = [this]
    {
        if (! applying)
            model.setInt(slotPath(slot) + "/on", enabled.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible(enabled);

    for (int i = 0; i < 4; ++i)
    {
        paramLabels[(size_t) i].setColour(juce::Label::textColourId, juce::Colour(0xff98a2ad));
        paramLabels[(size_t) i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(paramLabels[(size_t) i]);
        auto& slider = params[(size_t) i];
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setScrollWheelEnabled(false);
        slider.setDoubleClickReturnValue(true, 0.5);
        const int index = i;
        slider.onValueChange = [this, index]
        {
            if (! applying)
                model.setFloat(slotPath(slot) + "/par/" + juce::String(index + 1).paddedLeft('0', 2),
                               (float) params[(size_t) index].getValue());
        };
        addAndMakeVisible(slider);
    }

    setReturn(0);
}

void FxEditor::setReturn(int returnIndex)
{
    slot = juce::jlimit(0, 3, returnIndex / 2);
    fxReturn = juce::jlimit(0, numFxReturns - 1, returnIndex);
    title.setText(model.getFxReturnName(fxReturn), juce::dontSendNotification);
    sendLabel.setText("Bus " + juce::String(13 + slot) + " is the send. Stereo puts this side on main LR. Mono puts it on the center bus.",
                      juce::dontSendNotification);
    refresh();
}

void FxEditor::applyType(int type)
{
    model.setFxType(slot, type);
    refresh();
}

void FxEditor::showParameters(int type)
{
    const bool chorus = type == (int) FxAlgo::stereoChorus;

    for (int i = 0; i < 4; ++i)
    {
        const bool visible = ! chorus || i < 2;
        paramLabels[(size_t) i].setVisible(visible);
        params[(size_t) i].setVisible(visible);
        paramLabels[(size_t) i].setText(paramName(type, i), juce::dontSendNotification);
    }
}

void FxEditor::refresh()
{
    if (! title.isBeingEdited())
        title.setText(model.getFxReturnName(fxReturn), juce::dontSendNotification);

    sendLabel.setText(model.getBusName(12 + slot) + " is the send. Stereo puts this side on main LR. Mono puts it on the center bus.",
                      juce::dontSendNotification);
    applying = true;
    int type = 0;
    model.getInt(slotPath(slot) + "/type", type);
    typeBox.setSelectedId(type + 1, juce::dontSendNotification);
    int on = 0;
    model.getInt(slotPath(slot) + "/on", on);
    enabled.setToggleState(on != 0, juce::dontSendNotification);
    showParameters(type);

    for (int i = 0; i < 4; ++i)
    {
        if (params[(size_t) i].isMouseButtonDown())
            continue;

        float value = 0.5f;
        model.getFloat(slotPath(slot) + "/par/" + juce::String(i + 1).paddedLeft('0', 2), value);
        params[(size_t) i].setValue(value, juce::dontSendNotification);
    }

    applying = false;
}

void FxEditor::resized()
{
    auto area = getLocalBounds().reduced(16, 12);
    auto header = area.removeFromTop(32);
    title.setBounds(header.removeFromLeft(120));
    enabled.setBounds(header.removeFromRight(64).reduced(0, 4));
    typeBox.setBounds(header.removeFromLeft(220).reduced(8, 4));
    sendLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    for (int i = 0; i < 4; ++i)
    {
        auto row = area.removeFromTop(36);
        paramLabels[(size_t) i].setBounds(row.removeFromLeft(90));
        params[(size_t) i].setBounds(row.reduced(4, 4));
    }
}

} // namespace vc
