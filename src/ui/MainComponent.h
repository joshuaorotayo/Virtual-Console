#pragma once

#include "ui/ChannelStrip.h"
#include "ui/ChannelEditor.h"
#include "ui/FxEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace vc
{

class FaderBay;
class GroupEditor;
class MatrixEditor;
class RoutingBoard;

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
    void selectBus (int busIndex);
    void selectMatrix (int matrixIndex);
    void selectSpare (int kind, int index);
    void showBank (int bank);
    void closeTopPages();
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
    juce::TextButton routingButton { "Routing" };
    juce::TextButton busesButton { "Buses" };
    juce::TextButton matricesButton { "Matrices" };
    juce::TextButton dcasButton { "DCAs" };
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
    std::unique_ptr<FaderBay> faderBay;
    std::unique_ptr<RoutingBoard> routingBoard;
    juce::Viewport routingView;
    std::unique_ptr<ChannelEditor> channelEditor;
    std::unique_ptr<MatrixEditor> matrixEditor;
    std::unique_ptr<GroupEditor> groupEditor;
    juce::TextButton bankButton[5];
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
    bool busesOpen = false;
    bool matricesOpen = false;
    bool dcasOpen = false;
    int faderBank = 0;
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
