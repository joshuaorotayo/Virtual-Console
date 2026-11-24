#include "ui/ChannelStrip.h"

#include "model/FaderLaw.h"

#include <cmath>

namespace vc
{
namespace
{
juce::Colour background() { return juce::Colour (0xff14181d); }
juce::Colour stripColour() { return juce::Colour (0xff2a313a); }
juce::Colour text() { return juce::Colour (0xffe8edf2); }
juce::Colour soloOn() { return juce::Colour (0xffe0a24a); }
juce::Colour muteOn() { return juce::Colour (0xffd25b52); }

juce::String channelLeaf (int channel, const char* leaf)
{
    return "/ch/" + juce::String (channel + 1).paddedLeft ('0', 2) + "/" + leaf;
}

juce::String soloPath (int channel)
{
    return "/-stat/solosw/" + juce::String (channel + 1).paddedLeft ('0', 2);
}

juce::String decibelText (float db)
{
    if (db <= -100.0f)
        return "-inf";

    return juce::String (db, 1);
}

void styleRotary (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0, 0.0);
    slider.setScrollWheelEnabled (false);
    slider.setDoubleClickReturnValue (true, 0.5);
}

float readFloat (ConsoleModel& model, const juce::String& path, float fallback)
{
    float value = fallback;
    model.getFloat (path, value);
    return value;
}
} // namespace

void MeterView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101317));
    g.fillRoundedRectangle (bounds, 2.0f);

    const float peak = readLevel != nullptr ? readLevel() : 0.0f;
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f));
    auto area = getLocalBounds().reduced (1);
    const int fillHeight = juce::roundToInt ((float) area.getHeight() * norm);
    auto fill = area.removeFromBottom (fillHeight);

    juce::Colour colour (0xff3dce7a);

    if (norm > 0.92f)
        colour = juce::Colour (0xffe04b4b);
    else if (norm > 0.72f)
        colour = juce::Colour (0xffe0b040);

    g.setColour (colour);
    g.fillRect (fill);
}

void ConsoleFader::paint (juce::Graphics& g)
{
    juce::Slider::paint (g);

    const float y = getPositionOfValue (dbToFader (0.0f));
    g.setColour (juce::Colour (0xfff4f7fb));
    g.fillRect (1.0f, y - 1.0f, 6.0f, 2.0f);
    g.fillRect ((float) getWidth() - 7.0f, y - 1.0f, 6.0f, 2.0f);
}

FaderScale::FaderScale (juce::Slider& faderToMark)
    : fader (faderToMark)
{
}

void FaderScale::paint (juce::Graphics& g)
{
    struct Mark
    {
        float db;
        const char* text;
    };

    static constexpr Mark marks[] = {
        { 10.0f, "+10" },
        { 5.0f, "+5" },
        { 0.0f, "0" },
        { -5.0f, "-5" },
        { -10.0f, "-10" },
        { -20.0f, "-20" },
        { -30.0f, "-30" },
        { -40.0f, "-40" },
        { -60.0f, "-60" },
        { -90.0f, "-inf" }
    };

    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    float lastY = -100.0f;

    for (const auto& mark : marks)
    {
        const float y = fader.getPositionOfValue ((double) dbToFader (mark.db));
        const bool unity = mark.db == 0.0f;
        const bool endpoint = mark.db >= 10.0f || mark.db <= -90.0f;

        if (! unity && ! endpoint && std::abs (y - lastY) < 13.0f)
            continue;

        g.setColour (unity ? juce::Colour (0xfff4f7fb) : juce::Colour (0xff8b95a1));
        const float tick = unity ? 12.0f : 6.0f;
        g.fillRect ((float) getWidth() - tick, y - (unity ? 1.0f : 0.5f), tick, unity ? 2.0f : 1.0f);

        if (unity)
        {
            juce::Path notch;
            notch.addTriangle ((float) getWidth() - 11.0f, y - 4.0f,
                               (float) getWidth() - 1.0f, y,
                               (float) getWidth() - 11.0f, y + 4.0f);
            g.fillPath (notch);
        }

        g.drawText (mark.text,
                    0,
                    juce::roundToInt (y) - 7,
                    getWidth() - (int) tick - 1,
                    14,
                    juce::Justification::centredRight,
                    false);
        lastY = y;
    }
}

ChannelStrip::ChannelStrip (ConsoleModel& modelToUse, MixEngine& engineToUse, int channelIndex)
    : model (modelToUse), engine (engineToUse), index (channelIndex), scale (fader)
{
    scribble.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
    scribble.setColour (juce::TextButton::textColourOffId, text());
    scribble.setTooltip ("Select channel");
    scribble.onClick = [this] { choose(); };
    addAndMakeVisible (scribble);
    icon.onClick = [this]
    {
        choose();
        openChannelIconPicker (icon, model, index, [this] { refresh(); });
    };
    addAndMakeVisible (icon);
    refresh();

    styleRotary (pan);
    pan.setValue (readFloat (model, channelLeaf (index, "mix/pan"), 0.5f), juce::dontSendNotification);
    pan.onValueChange = [this]
    {
        const float value = (float) pan.getValue();
        model.setFloat (channelLeaf (index, "mix/pan"), value);
        juce::String label = "C";

        if (value < 0.48f)
            label = "L " + juce::String (juce::roundToInt ((0.5f - value) * 200.0f));
        else if (value > 0.52f)
            label = "R " + juce::String (juce::roundToInt ((value - 0.5f) * 200.0f));

        pan.setTooltip (label);
    };
    pan.setTooltip ("C");
    pan.onDragStart = [this] { choose(); };
    addAndMakeVisible (pan);

    meter.readLevel = [this] { return engine.getChannelMeter (index); };
    addAndMakeVisible (meter);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 16);
    fader.setRange (0.0, 1.0, 0.0);
    fader.setScrollWheelEnabled (false);
    fader.setDoubleClickReturnValue (true, 0.75);
    fader.setTextValueSuffix ("");
    fader.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
    fader.valueFromTextFunction = [] (const juce::String& text)
    {
        if (text.containsIgnoreCase ("inf"))
            return 0.0;

        return (double) dbToFader (text.getFloatValue());
    };
    fader.setValue (readFloat (model, channelLeaf (index, "mix/fader"), 0.75f), juce::dontSendNotification);
    fader.onValueChange = [this] { model.setFloat (channelLeaf (index, "mix/fader"), (float) fader.getValue()); };
    fader.onDragStart = [this] { choose(); };
    addAndMakeVisible (fader);
    addAndMakeVisible (scale);

    mute.setClickingTogglesState (true);
    mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
    mute.setTooltip ("Mute");
    int enabled = 1;
    model.getInt (channelLeaf (index, "mix/on"), enabled);
    mute.setToggleState (enabled == 0, juce::dontSendNotification);
    mute.onClick = [this]
    {
        choose();
        model.setInt (channelLeaf (index, "mix/on"), mute.getToggleState() ? 0 : 1);
    };
    addAndMakeVisible (mute);

    solo.setClickingTogglesState (true);
    solo.setColour (juce::TextButton::buttonOnColourId, soloOn());
    solo.setTooltip ("Solo, pre-fader");
    int soloed = 0;
    model.getInt (soloPath (index), soloed);
    solo.setToggleState (soloed != 0, juce::dontSendNotification);
    solo.onClick = [this]
    {
        choose();
        model.setInt (soloPath (index), solo.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible (solo);

    stereo.setClickingTogglesState (true);
    stereo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d8f5a));
    stereo.setTooltip ("Assign to the main stereo bus");
    int stereoOn = 1;
    model.getInt (channelLeaf (index, "mix/st"), stereoOn);
    stereo.setToggleState (stereoOn != 0, juce::dontSendNotification);
    stereo.onClick = [this]
    {
        choose();
        model.setInt (channelLeaf (index, "mix/st"), stereo.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible (stereo);

    mono.setClickingTogglesState (true);
    mono.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    mono.setTooltip ("Assign to the main mono bus");
    int monoOn = 0;
    model.getInt (channelLeaf (index, "mix/mono"), monoOn);
    mono.setToggleState (monoOn != 0, juce::dontSendNotification);
    mono.onClick = [this]
    {
        choose();
        model.setInt (channelLeaf (index, "mix/mono"), mono.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible (mono);
}

void ChannelStrip::choose()
{
    if (onSelect != nullptr)
        onSelect();
}

void ChannelStrip::setSelected (bool shouldSelect)
{
    if (selected == shouldSelect)
        return;

    selected = shouldSelect;
    repaint();
}

void ChannelStrip::refresh()
{
    const auto channelName = model.getChannelName (index);

    if (scribble.getButtonText() != channelName)
        scribble.setButtonText (channelName);

    int iconIndex = -1;
    model.getInt (channelLeaf (index, "config/icon"), iconIndex);
    icon.setIcon (iconIndex);

    if (! stereo.isMouseButtonDown())
    {
        int enabled = 1;
        model.getInt (channelLeaf (index, "mix/st"), enabled);
        stereo.setToggleState (enabled != 0, juce::dontSendNotification);
    }

    if (! mono.isMouseButtonDown())
    {
        int enabled = 0;
        model.getInt (channelLeaf (index, "mix/mono"), enabled);
        mono.setToggleState (enabled != 0, juce::dontSendNotification);
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent& event)
{
    if (event.originalComponent == this)
        choose();
}

void ChannelStrip::paint (juce::Graphics& g)
{
    g.fillAll (background());
    auto bounds = getLocalBounds().reduced (3).toFloat();
    g.setColour (selected ? juce::Colour (0xff35414c) : stripColour());
    g.fillRoundedRectangle (bounds, 6.0f);
    const auto colour = juce::Colour::fromHSV (std::fmod (index * 0.17f, 1.0f), 0.55f, selected ? 0.9f : 0.7f, 1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, selected ? 6.0f : 4.0f, 2.0f);

    if (selected)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (bounds, 6.0f, 2.0f);
    }
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (5);
    area.removeFromTop (12);
    auto nameRow = area.removeFromTop (28);
    icon.setBounds (nameRow.removeFromLeft (28).reduced (1, 1));
    scribble.setBounds (nameRow.reduced (2, 0));
    pan.setBounds (area.removeFromTop (46).withSizeKeepingCentre (44, 44));

    auto buttons = area.removeFromBottom (26);
    mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
    solo.setBounds (buttons.reduced (2, 0));
    auto assigns = area.removeFromBottom (24);
    stereo.setBounds (assigns.removeFromLeft (assigns.getWidth() / 2).reduced (2, 0));
    mono.setBounds (assigns.reduced (2, 0));

    area.removeFromBottom (2);
    scale.setBounds (area.removeFromLeft (36));
    meter.setBounds (area.removeFromRight (10).reduced (0, 4));
    fader.setBounds (area.reduced (1, 0));
}

} // namespace vc
