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
juce::Colour dim() { return juce::Colour (0xff98a2ad); }
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
    name.setText (model.getChannelName (index), juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setEditable (false, true, false);
    name.setColour (juce::Label::textColourId, text());
    name.setFont (juce::Font (juce::FontOptions (13.0f)));
    name.onTextChange = [this]
    {
        model.setChannelName (index, name.getText());
        model.setString (channelLeaf (index, "config/name"), name.getText());
    };
    addAndMakeVisible (name);

    sourceBox.setTextWhenNothingSelected ("None");
    sourceBox.setScrollWheelEnabled (false);
    sourceBox.onChange = [this]
    {
        const int id = sourceBox.getSelectedId();
        model.setChannelSource (index, id <= 1 ? -1 : id - 2);
    };
    addAndMakeVisible (sourceBox);

    trimLabel.setJustificationType (juce::Justification::centred);
    trimLabel.setColour (juce::Label::textColourId, dim());
    trimLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (trimLabel);

    styleRotary (trim);
    trim.setValue (readFloat (model, channelLeaf (index, "preamp/trim"), 0.5f), juce::dontSendNotification);
    trim.onValueChange = [this]
    {
        const float value = (float) trim.getValue();
        model.setFloat (channelLeaf (index, "preamp/trim"), value);
        trim.setTooltip (decibelText (trimToDb (value)) + " dB");
    };
    trim.setTooltip ("0.0 dB");
    addAndMakeVisible (trim);

    panLabel.setJustificationType (juce::Justification::centred);
    panLabel.setColour (juce::Label::textColourId, dim());
    panLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (panLabel);

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
    addAndMakeVisible (pan);

    meter.readLevel = [this] { return engine.getChannelMeter (index); };
    addAndMakeVisible (meter);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
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
    addAndMakeVisible (fader);
    addAndMakeVisible (scale);

    mute.setClickingTogglesState (true);
    mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
    mute.setTooltip ("Mute");
    int enabled = 1;
    model.getInt (channelLeaf (index, "mix/on"), enabled);
    mute.setToggleState (enabled == 0, juce::dontSendNotification);
    mute.onClick = [this] { model.setInt (channelLeaf (index, "mix/on"), mute.getToggleState() ? 0 : 1); };
    addAndMakeVisible (mute);

    solo.setClickingTogglesState (true);
    solo.setColour (juce::TextButton::buttonOnColourId, soloOn());
    solo.setTooltip ("Solo, pre-fader");
    int soloed = 0;
    model.getInt (soloPath (index), soloed);
    solo.setToggleState (soloed != 0, juce::dontSendNotification);
    solo.onClick = [this] { model.setInt (soloPath (index), solo.getToggleState() ? 1 : 0); };
    addAndMakeVisible (solo);

    edit.setTooltip ("Channel processing");
    edit.onClick = [this]
    {
        if (onEdit != nullptr)
            onEdit();
    };
    addAndMakeVisible (edit);
}

void ChannelStrip::setSourceNames (const juce::StringArray& names)
{
    const int selected = model.getChannelSource (index);
    sourceBox.clear (juce::dontSendNotification);
    sourceBox.addItem ("None", 1);

    for (int i = 0; i < names.size(); ++i)
        sourceBox.addItem (names[i], i + 2);

    sourceBox.setSelectedId (selected < 0 ? 1 : selected + 2, juce::dontSendNotification);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    g.fillAll (background());
    g.setColour (stripColour());
    g.fillRoundedRectangle (getLocalBounds().reduced (3).toFloat(), 6.0f);
    g.setColour (juce::Colour::fromHSV (std::fmod (index * 0.17f, 1.0f), 0.45f, 0.75f, 1.0f));
    g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (6);
    area.removeFromTop (10);
    name.setBounds (area.removeFromTop (22));
    sourceBox.setBounds (area.removeFromTop (24).reduced (0, 2));
    edit.setBounds (area.removeFromTop (22).reduced (18, 1));
    trimLabel.setBounds (area.removeFromTop (14));
    trim.setBounds (area.removeFromTop (46));
    panLabel.setBounds (area.removeFromTop (14));
    pan.setBounds (area.removeFromTop (46));

    auto buttons = area.removeFromBottom (28);
    mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2));
    solo.setBounds (buttons.reduced (2));

    area.removeFromBottom (2);
    scale.setBounds (area.removeFromLeft (42));
    meter.setBounds (area.removeFromRight (12).reduced (0, 4));
    fader.setBounds (area.reduced (1, 0));
}

} // namespace vc
