#include "ui/MainComponent.h"

#include "model/FaderLaw.h"

#include <cmath>

namespace vc
{
namespace
{
juce::Colour background() { return juce::Colour (0xff14181d); }
juce::Colour panel() { return juce::Colour (0xff1e232a); }
juce::Colour text() { return juce::Colour (0xffe8edf2); }
juce::Colour dim() { return juce::Colour (0xff98a2ad); }
juce::Colour muteOn() { return juce::Colour (0xffd25b52); }

juce::String decibelText (float db)
{
    if (db <= -100.0f)
        return "-inf";

    return juce::String (db, 1);
}

juce::String formatTime (double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;

    const int total = (int) std::floor (seconds);
    const int minutes = total / 60;
    const int secs = total % 60;
    return juce::String (minutes) + ":" + juce::String (secs).paddedLeft ('0', 2);
}
} // namespace

class StripBank : public juce::Component
{
public:
    StripBank (ConsoleModel& model, MixEngine& engine)
    {
        for (int i = 0; i < numChannels; ++i)
        {
            strips[(size_t) i] = std::make_unique<ChannelStrip> (model, engine, i);
            addAndMakeVisible (*strips[(size_t) i]);
        }
    }

    void setEditHandler (std::function<void (int)> handler)
    {
        for (int i = 0; i < numChannels; ++i)
            strips[(size_t) i]->onEdit = [handler, i] { if (handler != nullptr) handler (i); };
    }

    void resized() override
    {
        constexpr int width = 128;

        for (int i = 0; i < numChannels; ++i)
            strips[(size_t) i]->setBounds (i * width, 0, width, getHeight());
    }

    void setSourceNames (const juce::StringArray& names)
    {
        for (auto& strip : strips)
            strip->setSourceNames (names);
    }

    int widthForChannels() const { return 128 * numChannels; }

private:
    std::array<std::unique_ptr<ChannelStrip>, numChannels> strips;
};

class SourceRow : public juce::Component
{
public:
    SourceRow()
    {
        name.setColour (juce::Label::textColourId, text());
        name.setFont (juce::Font (juce::FontOptions (14.0f)));
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        path.setColour (juce::Label::textColourId, dim());
        path.setFont (juce::Font (juce::FontOptions (12.0f)));
        path.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (path);

        remove.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        remove.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7edf3));
        remove.setTooltip ("Remove source");
        addAndMakeVisible (remove);
    }

    void setRow (int sourceId, const juce::String& sourceName, const juce::String& file, bool rowSelected, std::function<void (int)> callback)
    {
        id = sourceId;
        selected = rowSelected;
        name.setText (sourceName, juce::dontSendNotification);
        path.setText (file, juce::dontSendNotification);
        path.setTooltip (file);
        onRemove = std::move (callback);
        remove.onClick = [this]
        {
            if (onRemove == nullptr)
                return;

            const int sourceId = id;
            auto callback = onRemove;
            juce::MessageManager::callAsync ([callback, sourceId] { callback (sourceId); });
        };
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (selected ? juce::Colour (0xff31404c) : panel());
        g.setColour (juce::Colour (0xff2c333b));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 6);
        remove.setBounds (area.removeFromRight (32).reduced (2));
        area.removeFromRight (12);
        name.setBounds (area.removeFromLeft (juce::jmin (240, area.getWidth() / 3)));
        path.setBounds (area);
    }

private:
    int id = 0;
    bool selected = false;
    juce::Label name;
    juce::Label path;
    juce::TextButton remove { "X" };
    std::function<void (int)> onRemove;
};

class MainComponent::SourceList : public juce::ListBoxModel
{
public:
    SourceList (ConsoleModel& modelToUse, std::function<void (int)> removeCallback)
        : model (modelToUse), onRemove (std::move (removeCallback)) {}

    int getNumRows() override { return model.getNumSources(); }

    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        if (row < 0 || row >= model.getNumSources())
        {
            delete existing;
            return nullptr;
        }

        auto* item = dynamic_cast<SourceRow*> (existing);

        if (item == nullptr)
        {
            delete existing;
            item = new SourceRow();
        }

        item->setRow (model.getSourceId (row), model.getSourceName (row), model.getSourceFile (row), selected, onRemove);
        return item;
    }

private:
    ConsoleModel& model;
    std::function<void (int)> onRemove;
};

MainComponent::MainComponent()
    : engine (model)
{
    look.setColour (juce::Slider::thumbColourId, juce::Colour (0xffd7dbe2));
    look.setColour (juce::Slider::trackColourId, juce::Colour (0xff3c4450));
    look.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff232830));
    look.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff7eb6d6));
    look.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3c4450));
    look.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a3038));
    look.setColour (juce::ComboBox::textColourId, text());
    look.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3c4450));
    look.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e343d));
    look.setColour (juce::TextButton::textColourOffId, text());
    look.setColour (juce::ListBox::backgroundColourId, panel());
    look.setColour (juce::Label::textColourId, text());
    juce::LookAndFeel::setDefaultLookAndFeel (&look);

    title.setFont (juce::Font (juce::FontOptions (18.0f)));
    title.setColour (juce::Label::textColourId, text());
    addAndMakeVisible (title);

    mixerButton.setClickingTogglesState (true);
    mixerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    mixerButton.onClick = [this] { showPage (Page::mixer); };
    addAndMakeVisible (mixerButton);

    channelButton.setClickingTogglesState (true);
    channelButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    channelButton.onClick = [this] { showPage (Page::channel); };
    addAndMakeVisible (channelButton);

    sourcesButton.setClickingTogglesState (true);
    sourcesButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    sourcesButton.onClick = [this] { showPage (Page::sources); };
    addAndMakeVisible (sourcesButton);

    for (auto* label : { &outputLabel, &listenLabel, &mainLabel })
    {
        label->setColour (juce::Label::textColourId, dim());
        label->setFont (juce::Font (juce::FontOptions (12.0f)));
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }

    mainLabel.setJustificationType (juce::Justification::centred);
    mainLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    mainLabel.setColour (juce::Label::textColourId, text());

    outputBox.onChange = [this]
    {
        if (! applyingOutput)
            applyOutput (outputBox.getSelectedId());
    };
    addAndMakeVisible (outputBox);

    setupButton.onClick = [this] { showAudioSetup(); };
    addAndMakeVisible (setupButton);

    listenBox.addItem ("Auto", 1);
    listenBox.addItem ("Main", 2);
    listenBox.addItem ("Solo", 3);
    listenBox.setSelectedId (1, juce::dontSendNotification);
    listenBox.onChange = [this]
    {
        model.setListenMode ((ListenMode) (listenBox.getSelectedId() - 1));
    };
    listenBox.setTooltip ("Auto plays solo when a channel is soloed, otherwise the main mix");
    addAndMakeVisible (listenBox);

    addButton.onClick = [this] { chooseAudio(); };
    addAndMakeVisible (addButton);

    playButton.setClickingTogglesState (false);
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d8f5a));
    playButton.onClick = [this]
    {
        if (engine.isPlaying())
            engine.pause();
        else
            engine.play();

        syncTransport();
    };
    playButton.setTooltip ("Play or pause every source together");
    addAndMakeVisible (playButton);

    restartButton.onClick = [this]
    {
        engine.restart();
        syncTransport();
    };
    restartButton.setTooltip ("Return to the start and play");
    addAndMakeVisible (restartButton);

    timeline.setSliderStyle (juce::Slider::LinearHorizontal);
    timeline.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    timeline.setRange (0.0, 1.0, 0.0);
    timeline.setScrollWheelEnabled (false);
    timeline.onDragStart = [this] { scrubbing = true; };
    timeline.onDragEnd = [this]
    {
        engine.seek (timeline.getValue());
        scrubbing = false;
    };
    timeline.onValueChange = [this]
    {
        if (movingTimeline)
            return;

        if (scrubbing || timeline.isMouseButtonDown())
            engine.seek (timeline.getValue());
    };
    timeline.setTooltip ("Move the playhead");
    addAndMakeVisible (timeline);

    timeLabel.setColour (juce::Label::textColourId, dim());
    timeLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeLabel);

    sourceHeading.setFont (juce::Font (juce::FontOptions (20.0f)));
    sourceHeading.setColour (juce::Label::textColourId, text());
    sourceHeading.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sourceHeading);

    emptySources.setColour (juce::Label::textColourId, dim());
    emptySources.setFont (juce::Font (juce::FontOptions (14.0f)));
    emptySources.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (emptySources);

    sourceModel = std::make_unique<SourceList> (model, [this] (int sourceId) { removeSourceById (sourceId); });
    sourceList.setModel (sourceModel.get());
    sourceList.setRowHeight (40);
    sourceList.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff3c4450));
    sourceList.setOutlineThickness (1);
    addAndMakeVisible (sourceList);

    strips = std::make_unique<StripBank> (model, engine);
    strips->setEditHandler ([this] (int channel) { editChannel (channel); });
    viewport.setViewedComponent (strips.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    channelEditor = std::make_unique<ChannelEditor> (model);
    addChildComponent (channelEditor.get());

    mainMeter.readLevel = [this] { return engine.getMainMeter(); };
    addAndMakeVisible (mainMeter);

    mainFader.setSliderStyle (juce::Slider::LinearVertical);
    mainFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    mainFader.setRange (0.0, 1.0, 0.0);
    mainFader.setScrollWheelEnabled (false);
    mainFader.setDoubleClickReturnValue (true, 0.75);
    mainFader.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
    mainFader.valueFromTextFunction = [] (const juce::String& text)
    {
        if (text.containsIgnoreCase ("inf"))
            return 0.0;

        return (double) dbToFader (text.getFloatValue());
    };
    float mainValue = 0.75f;
    model.getFloat ("/main/st/mix/fader", mainValue);
    mainFader.setValue (mainValue, juce::dontSendNotification);
    mainFader.onValueChange = [this] { model.setFloat ("/main/st/mix/fader", (float) mainFader.getValue()); };
    addAndMakeVisible (mainFader);
    addAndMakeVisible (mainScale);

    mainMute.setClickingTogglesState (true);
    mainMute.setColour (juce::TextButton::buttonOnColourId, muteOn());
    mainMute.setTooltip ("Mute main");
    int mainOn = 1;
    model.getInt ("/main/st/mix/on", mainOn);
    mainMute.setToggleState (mainOn == 0, juce::dontSendNotification);
    mainMute.onClick = [this] { model.setInt ("/main/st/mix/on", mainMute.getToggleState() ? 0 : 1); };
    addAndMakeVisible (mainMute);

    status.setColour (juce::Label::textColourId, dim());
    status.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (status);

    const auto deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
    player.setSource (&engine);
    deviceManager.addAudioCallback (&player);
    refreshOutputs();
    refreshSources();
    showPage (Page::mixer);
    status.setText (deviceError.isNotEmpty() ? deviceError : statusText(), juce::dontSendNotification);
    startTimerHz (30);
    setSize (1280, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background());
    g.setColour (panel());
    g.fillRect (getLocalBounds().removeFromTop (124));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (48).reduced (10, 8);
    title.setBounds (top.removeFromLeft (160));
    addButton.setBounds (top.removeFromRight (110).reduced (4, 0));
    listenBox.setBounds (top.removeFromRight (110).reduced (4, 0));
    listenLabel.setBounds (top.removeFromRight (48));
    setupButton.setBounds (top.removeFromRight (80).reduced (4, 0));
    outputBox.setBounds (top.removeFromRight (220).reduced (4, 0));
    outputLabel.setBounds (top.removeFromRight (56));

    auto tabs = area.removeFromTop (36).reduced (10, 4);
    mixerButton.setBounds (tabs.removeFromLeft (96));
    tabs.removeFromLeft (6);
    channelButton.setBounds (tabs.removeFromLeft (104));
    tabs.removeFromLeft (6);
    sourcesButton.setBounds (tabs.removeFromLeft (104));

    auto transport = area.removeFromTop (40).reduced (10, 6);
    playButton.setBounds (transport.removeFromLeft (78));
    transport.removeFromLeft (6);
    restartButton.setBounds (transport.removeFromLeft (88));
    transport.removeFromLeft (10);
    timeLabel.setBounds (transport.removeFromRight (118));
    timeline.setBounds (transport.reduced (0, 2));

    status.setBounds (area.removeFromBottom (24).reduced (10, 2));

    const bool sources = page == Page::sources;
    const bool channel = page == Page::channel;
    const bool mixer = page == Page::mixer;
    sourceHeading.setVisible (sources);
    emptySources.setVisible (sources && model.getNumSources() == 0);
    sourceList.setVisible (sources && model.getNumSources() > 0);
    viewport.setVisible (mixer);
    mainLabel.setVisible (mixer);
    mainMeter.setVisible (mixer);
    mainFader.setVisible (mixer);
    mainScale.setVisible (mixer);
    mainMute.setVisible (mixer);
    channelEditor->setVisible (channel);

    if (sources)
    {
        auto pageArea = area.reduced (24, 12);
        sourceHeading.setBounds (pageArea.removeFromTop (36));
        emptySources.setBounds (pageArea.removeFromTop (48));
        sourceList.setBounds (pageArea);
        return;
    }

    if (channel)
    {
        channelEditor->setBounds (area);
        return;
    }

    auto mainColumn = area.removeFromRight (168).reduced (8, 8);
    mainLabel.setBounds (mainColumn.removeFromTop (22));
    mainMute.setBounds (mainColumn.removeFromBottom (28).reduced (36, 0));
    mainScale.setBounds (mainColumn.removeFromLeft (46));
    mainMeter.setBounds (mainColumn.removeFromRight (14).reduced (0, 8));
    mainFader.setBounds (mainColumn.reduced (2, 0));

    viewport.setBounds (area.reduced (4, 8));
    const int stripHeight = std::max (0, viewport.getHeight() - viewport.getScrollBarThickness());
    strips->setSize (strips->widthForChannels(), stripHeight);
}

void MainComponent::showPage (Page next)
{
    page = next;
    mixerButton.setToggleState (page == Page::mixer, juce::dontSendNotification);
    channelButton.setToggleState (page == Page::channel, juce::dontSendNotification);
    sourcesButton.setToggleState (page == Page::sources, juce::dontSendNotification);

    if (page == Page::channel)
        channelEditor->refresh();

    resized();
}

void MainComponent::editChannel (int channelIndex)
{
    editingChannel = channelIndex;
    channelEditor->setChannel (channelIndex);
    showPage (Page::channel);
}

void MainComponent::removeSourceById (int sourceId)
{
    for (int i = 0; i < model.getNumSources(); ++i)
    {
        if (model.getSourceId (i) == sourceId)
        {
            engine.removeSource (i);
            refreshSources();
            return;
        }
    }
}

void MainComponent::timerCallback()
{
    repaint();
    syncOutputSelection();
    const auto next = statusText();

    if (status.getText() != next)
        status.setText (next, juce::dontSendNotification);

    syncTransport();

    if (page == Page::channel)
        channelEditor->refresh();
}

void MainComponent::syncTransport()
{
    const bool playing = engine.isPlaying();
    const juce::String label = playing ? "Pause" : "Play";

    if (playButton.getButtonText() != label)
        playButton.setButtonText (label);

    playButton.setToggleState (playing, juce::dontSendNotification);

    const double duration = engine.getDuration();
    const double position = engine.getPosition();
    timeLabel.setText (formatTime (position) + " / " + formatTime (duration), juce::dontSendNotification);

    if (scrubbing || timeline.isMouseButtonDown())
        return;

    const double maximum = duration > 0.0 ? duration : 1.0;
    movingTimeline = true;

    if (std::abs (timeline.getMaximum() - maximum) > 0.0001)
        timeline.setRange (0.0, maximum, 0.0);

    timeline.setValue (position, juce::dontSendNotification);
    movingTimeline = false;
}

void MainComponent::chooseAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("Add audio sources",
                                                    juce::File(),
                                                    "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");
    constexpr auto browserFlags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync (browserFlags, [this] (const juce::FileChooser&) { filesChosen(); });
}

void MainComponent::filesChosen()
{
    if (chooser == nullptr)
        return;

    for (const auto& file : chooser->getResults())
    {
        const int index = model.addSource (file.getFileNameWithoutExtension(), file.getFullPathName());

        if (index >= 0)
            engine.openSource (index, file);
    }

    refreshSources();
}

void MainComponent::refreshSources()
{
    juce::StringArray names;

    for (int i = 0; i < model.getNumSources(); ++i)
        names.add (model.getSourceName (i));

    strips->setSourceNames (names);
    sourceList.updateContent();
    sourceList.repaint();

    if (page == Page::sources)
        resized();
}

void MainComponent::refreshOutputs()
{
    const auto previous = outputBox.getText();
    applyingOutput = true;
    outputBox.clear (juce::dontSendNotification);
    outputs.clear();
    int id = 1;

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();

        for (const auto& name : type->getDeviceNames (false))
        {
            outputs.push_back ({ type->getTypeName(), name });
            outputBox.addItem (name, id++);
        }
    }

    applyingOutput = false;
    juce::ignoreUnused (previous);
    syncOutputSelection();
}

void MainComponent::syncOutputSelection()
{
    if (outputBox.isPopupActive())
        return;

    const auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    const auto name = device->getName();

    for (int i = 0; i < (int) outputs.size(); ++i)
    {
        if (outputs[(size_t) i].name == name)
        {
            applyingOutput = true;
            outputBox.setSelectedId (i + 1, juce::dontSendNotification);
            applyingOutput = false;
            return;
        }
    }
}

void MainComponent::applyOutput (int comboId)
{
    const int index = comboId - 1;

    if (index < 0 || index >= (int) outputs.size())
        return;

    const auto& choice = outputs[(size_t) index];
    deviceManager.setCurrentAudioDeviceType (choice.type, true);
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    setup.outputDeviceName = choice.name;
    setup.inputDeviceName.clear();
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;
    setup.inputChannels.clear();
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        status.setText (error, juce::dontSendNotification);
}

void MainComponent::showAudioSetup()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 0, 0, 1, 2, false, false, true, false);
    selector->setSize (520, 440);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio output";
    options.dialogBackgroundColour = panel();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

juce::String MainComponent::statusText()
{
    juce::StringArray parts;
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device != nullptr)
    {
        parts.add (device->getName());
        parts.add (juce::String (device->getCurrentSampleRate(), 0) + " Hz");
        parts.add (juce::String (device->getCurrentBufferSizeSamples()) + " samples");
    }
    else
    {
        parts.add ("No output device");
    }

    if (model.getNumSources() == 0)
        parts.add ("Open Sources to add audio, then press Play.");

    for (int i = 0; i < model.getNumSources(); ++i)
    {
        const auto error = engine.sourceError (i);

        if (error.isNotEmpty())
            parts.add (error);
    }

    return parts.joinIntoString ("   ");
}

} // namespace vc
