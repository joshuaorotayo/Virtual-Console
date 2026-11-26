#pragma once

#include "ui/ChannelStrip.h"
#include "ui/ChannelEditor.h"
#include "ui/FxEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace vc
{

class FaderBay;
class OutputRow;
class GroupEditor;
class MatrixEditor;
class RoutingBoard;

class TransportButton : public juce::Button
{
public:
    enum class Icon { play, restart, record };

    explicit TransportButton(Icon iconToDraw);

    void setPlaying(bool shouldPlay);
    void setRecording(bool shouldRecord);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon;
    bool playing = false;
    bool recording = false;
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
    void selectBus (int busIndex);
    void selectMatrix (int matrixIndex);
    void selectSpare (int kind, int index);
    void showInputPage (int page);
    void showOutputPage (int page);
    void syncEffectBuses();
    bool effectsPage() const;
    void applySendMode();
    void closeTopPages();
    int inputPageCount() const;
    void removeSourceById (int sourceId);
    void syncTransport();
    void showRecordChoices();
    void beginRecording();
    void refreshRecordNames();
    juce::String recordingSummary() const;
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
    juce::TextButton routingButton { "Routing" };
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
    juce::Label playbackHeading { {}, "Playback" };
    TransportButton recordButton { TransportButton::Icon::record };
    juce::TextButton recordChoose { "Choose" };
    juce::Label recordSummary { {}, "Main" };
    juce::Label recordTime { {}, "0:00" };
    juce::Label sourceHeading { {}, "Sources" };
    juce::Label emptySources { {}, "No sources yet. Add a file and it lands on the first free local input or aux. Choose that input from Config." };
    std::unique_ptr<SourceList> sourceModel;
    juce::ListBox sourceList { "Sources", nullptr };
    std::unique_ptr<FaderBay> faderBay;
    std::unique_ptr<OutputRow> outputRow;
    std::unique_ptr<RoutingBoard> routingBoard;
    juce::Viewport routingView;
    std::unique_ptr<ChannelEditor> channelEditor;
    std::unique_ptr<MatrixEditor> matrixEditor;
    std::unique_ptr<GroupEditor> groupEditor;
    juce::TextButton inputBankButton[6];
    juce::TextButton widthButton { "16" };
    juce::TextButton sendsButton { "Sends" };
    juce::TextButton outputBankButton[5];
    juce::TextButton mainSelect { "SEL" };
    juce::TextButton centerSelect { "SEL" };
    juce::Label muteHeading { {}, "Mute" };
    juce::Label spareTitle;
    std::unique_ptr<FxEditor> fxEditor;
    juce::Viewport viewport;
    juce::Label mainLabel { {}, "MAIN" };
    MeterView mainMeter;
    ConsoleFader mainFader;
    FaderScale mainScale { mainFader };
    juce::TextButton mainMute { "M" };
    juce::Label centerLabel { {}, "C" };
    ConsoleFader centerFader;
    FaderScale centerScale { centerFader };
    juce::TextButton centerMute { "M" };
    juce::TextButton centerStereo { "ST" };
    juce::TextButton muteGroupButton[numMuteGroups];
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<OutputChoice> outputs;
    bool applyingOutput = false;
    bool sourcesOpen = false;
    bool routingOpen = false;
    int inputWidth = 16;
    int inputPage = 0;
    int outputPage = 4;
    int armedBus = -1;
    int dcaFilter = -1;
    int topEditor = 0;
    int spareKind = 0;
    int spareIndex = 0;
    int selectedChannel = 0;
    int selectedBus = 0;
    int selectedMatrix = 0;
    bool scrubbing = false;
    bool movingTimeline = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace vc
