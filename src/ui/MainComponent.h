#pragma once

#include "ui/ChannelStrip.h"
#include "ui/ChannelEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace vc
{

class StripBank;

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class SourceList;

    enum class Page
    {
        mixer,
        channel,
        sources
    };

    void timerCallback() override;
    void chooseAudio();
    void filesChosen();
    void refreshOutputs();
    void applyOutput (int comboId);
    void syncOutputSelection();
    void refreshSources();
    void showAudioSetup();
    void showPage (Page next);
    void editChannel (int channelIndex);
    void removeSourceById (int sourceId);
    void syncTransport();
    juce::String statusText();

    struct OutputChoice
    {
        juce::String type;
        juce::String name;
    };

    juce::LookAndFeel_V4 look;
    ConsoleModel model;
    MixEngine engine;
    juce::AudioSourcePlayer player;
    juce::AudioDeviceManager deviceManager;

    juce::Label title { {}, "Virtual Console" };
    juce::TextButton mixerButton { "Mixer" };
    juce::TextButton channelButton { "Channel" };
    juce::TextButton sourcesButton { "Sources" };
    juce::Label outputLabel { {}, "Output" };
    juce::ComboBox outputBox;
    juce::TextButton setupButton { "Setup" };
    juce::Label listenLabel { {}, "Listen" };
    juce::ComboBox listenBox;
    juce::TextButton addButton { "Add audio" };
    juce::TextButton playButton { "Play" };
    juce::TextButton restartButton { "Restart" };
    juce::Slider timeline;
    juce::Label timeLabel { {}, "0:00 / 0:00" };
    juce::Label sourceHeading { {}, "Sources" };
    juce::Label emptySources { {}, "No sources yet. Add audio, then assign it from a channel. One file can feed several channels." };
    std::unique_ptr<SourceList> sourceModel;
    juce::ListBox sourceList { "Sources", nullptr };
    std::unique_ptr<StripBank> strips;
    std::unique_ptr<ChannelEditor> channelEditor;
    juce::Viewport viewport;
    juce::Label mainLabel { {}, "MAIN" };
    MeterView mainMeter;
    ConsoleFader mainFader;
    FaderScale mainScale { mainFader };
    juce::TextButton mainMute { "M" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<OutputChoice> outputs;
    bool applyingOutput = false;
    Page page = Page::mixer;
    int editingChannel = 0;
    bool scrubbing = false;
    bool movingTimeline = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vc
