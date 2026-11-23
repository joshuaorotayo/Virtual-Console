#pragma once

#include "model/ConsoleModel.h"
#include "model/FaderLaw.h"
#include "engine/MixEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

class MeterView : public juce::Component
{
public:
    std::function<float()> readLevel;
    void paint (juce::Graphics& g) override;
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
    void refresh();
    std::function<void()> onSelect;

private:
    void choose();

    ConsoleModel& model;
    MixEngine& engine;
    int index = 0;
    bool selected = false;

    juce::TextButton scribble;
    juce::Slider pan;
    MeterView meter;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };
};

} // namespace vc
