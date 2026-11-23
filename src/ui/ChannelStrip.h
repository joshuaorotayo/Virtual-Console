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
    void setSourceNames (const juce::StringArray& names);

private:
    ConsoleModel& model;
    MixEngine& engine;
    int index = 0;

    juce::Label name;
    juce::ComboBox sourceBox;
    juce::Label trimLabel { {}, "Trim" };
    juce::Slider trim;
    juce::Label panLabel { {}, "Pan" };
    juce::Slider pan;
    MeterView meter;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };
};

} // namespace vc
