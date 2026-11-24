#pragma once

#include "ui/ChannelStrip.h"
#include "ui/ChannelEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace vc
{

class StripBank;
class BusBank;
class MatrixBank;
class MatrixSendPanel;

class TransportButton : public juce::Button
{
public:
    enum class Icon { play, restart };

    explicit TransportButton(Icon iconToDraw);

    void setPlaying(bool shouldPlay);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon;
    bool playing = false;
};

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

    void timerCallback() override;
    void chooseAudio();
    void filesChosen();
    void refreshOutputs();
    void applyOutput (int comboId);
    void syncOutputSelection();
    void refreshSources();
    void showAudioSetup();
    void selectChannel (int channelIndex);
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

    juce::Label title { {}, "Virtual Console 0.2.0" };
    juce::TextButton sourcesButton { "Sources" };
    juce::TextButton busesButton { "Buses" };
    juce::TextButton matricesButton { "Matrices" };
    juce::Label mixLabel { {}, "Mix" };
    juce::ComboBox mixBox;
    juce::Label outputLabel { {}, "Output" };
    juce::ComboBox outputBox;
    juce::TextButton setupButton { "Setup" };
    juce::Label listenLabel { {}, "Listen" };
    juce::ComboBox listenBox;
    juce::TextButton addButton { "Add audio" };
    TransportButton playButton { TransportButton::Icon::play };
    TransportButton restartButton { TransportButton::Icon::restart };
    juce::Slider timeline;
    juce::Label timeLabel { {}, "0:00 / 0:00" };
    juce::Label sourceHeading { {}, "Sources" };
    juce::Label emptySources { {}, "No sources yet. Add a file and it lands on the first free local input or aux. Choose that input from Config." };
    std::unique_ptr<SourceList> sourceModel;
    juce::ListBox sourceList { "Sources", nullptr };
    std::unique_ptr<StripBank> strips;
    std::unique_ptr<BusBank> buses;
    std::unique_ptr<MatrixBank> matrices;
    std::unique_ptr<MatrixSendPanel> matrixSends;
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
    bool sourcesOpen = false;
    bool busesOpen = false;
    bool matricesOpen = false;
    int selectedChannel = 0;
    bool scrubbing = false;
    bool movingTimeline = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vc
