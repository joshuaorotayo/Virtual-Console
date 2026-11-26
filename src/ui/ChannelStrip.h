#pragma once

#include "model/ConsoleModel.h"
#include "model/FaderLaw.h"
#include "engine/MixEngine.h"
#include "ui/ChannelIcons.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

class MeterView : public juce::Component
{
public:
    std::function<float()> readLevel;
    std::function<void()> onPressed;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
};

class ConsoleFader : public juce::Slider
{
public:
    void paint (juce::Graphics& g) override;
};

class FaderScale : public juce::Component
{
public:
    explicit FaderScale (juce::Slider& faderToMark);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;

    std::function<void()> onPressed;

private:
    juce::Slider& fader;
};

class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (ConsoleModel& modelToUse, MixEngine& engineToUse, int channelIndex);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void setSelected (bool shouldSelect);
    void setHighlighted (bool shouldHighlight);
    void setSendBus (int busIndex);
    void refresh();
    std::function<void()> onSelect;

private:
    void choose();
    juce::String levelPath() const;
    juce::String mutePath() const;

    ConsoleModel& model;
    MixEngine& engine;
    int index = 0;
    int sendBus = -1;
    bool selected = false;
    bool highlighted = false;

    class NameLabel : public juce::Label
    {
    public:
        std::function<void()> onSelect;

        NameLabel()
        {
            setEditable(false, true, false);
            setJustificationType(juce::Justification::centred);
            setColour(juce::Label::backgroundColourId, juce::Colour(0xff232830));
            setColour(juce::Label::textColourId, juce::Colour(0xffe8edf2));
            setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff1c2128));
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.getNumberOfClicks() == 1 && onSelect != nullptr)
                onSelect();

            juce::Label::mouseDown(event);
        }
    };

    NameLabel scribble;
    ChannelIconButton icon;
    juce::Slider pan;
    MeterView meter;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };
    juce::TextButton stereo { "ST" };
    juce::TextButton mono { "MO" };
};

} // namespace vc
