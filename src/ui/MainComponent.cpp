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
    static constexpr int stripWidth = 112;

    StripBank (ConsoleModel& model, MixEngine& engine)
    {
        for (int i = 0; i < numChannels; ++i)
        {
            strips[(size_t) i] = std::make_unique<ChannelStrip> (model, engine, i);
            addAndMakeVisible (*strips[(size_t) i]);
        }
    }

    void setSelectHandler (std::function<void (int)> handler)
    {
        for (int i = 0; i < numChannels; ++i)
            strips[(size_t) i]->onSelect = [handler, i] { if (handler != nullptr) handler (i); };
    }

    void setSelected (int channel)
    {
        for (int i = 0; i < numChannels; ++i)
            strips[(size_t) i]->setSelected (i == channel);
    }

    void refresh()
    {
        for (auto& strip : strips)
            strip->refresh();
    }

    void resized() override
    {
        for (int i = 0; i < numChannels; ++i)
            strips[(size_t) i]->setBounds (i * stripWidth, 0, stripWidth, getHeight());
    }

    int widthForChannels() const { return stripWidth * numChannels; }

private:
    std::array<std::unique_ptr<ChannelStrip>, numChannels> strips;
};

class BusStrip : public juce::Component
{
public:
    BusStrip (ConsoleModel& modelToUse, int busIndex)
        : model (modelToUse), index (busIndex), scale (fader)
    {
        scribble.setButtonText ("Bus " + juce::String (index + 1));
        scribble.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
        scribble.setColour (juce::TextButton::textColourOffId, text());
        scribble.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (scribble);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (0.0, 1.0, 0.0);
        pan.setScrollWheelEnabled (false);
        pan.setDoubleClickReturnValue (true, 0.5);
        pan.onValueChange = [this] { model.setFloat (leaf ("mix/pan"), (float) pan.getValue()); };
        pan.setTooltip ("Balance to main");
        addAndMakeVisible (pan);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 16);
        fader.setRange (0.0, 1.0, 0.0);
        fader.setScrollWheelEnabled (false);
        fader.setDoubleClickReturnValue (true, 0.75);
        fader.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
        fader.valueFromTextFunction = [] (const juce::String& text)
        {
            if (text.containsIgnoreCase ("inf"))
                return 0.0;

            return (double) dbToFader (text.getFloatValue());
        };
        fader.onValueChange = [this] { model.setFloat (leaf ("mix/fader"), (float) fader.getValue()); };
        addAndMakeVisible (fader);
        addAndMakeVisible (scale);

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
        mute.setTooltip ("Mute bus");
        mute.onClick = [this] { model.setInt (leaf ("mix/on"), mute.getToggleState() ? 0 : 1); };
        addAndMakeVisible (mute);

        toMain.setButtonText ("ST");
        toMain.setClickingTogglesState (true);
        toMain.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d8f5a));
        toMain.setTooltip ("Assign this bus to the main stereo bus");
        toMain.onClick = [this] { model.setInt (leaf ("mix/st"), toMain.getToggleState() ? 1 : 0); };
        addAndMakeVisible (toMain);

        toMono.setClickingTogglesState (true);
        toMono.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
        toMono.setTooltip ("Assign this bus to the main mono bus");
        toMono.onClick = [this] { model.setInt (leaf ("mix/mono"), toMono.getToggleState() ? 1 : 0); };
        addAndMakeVisible (toMono);
        refresh();
    }

    void refresh()
    {
        if (! pan.isMouseButtonDown())
            pan.setValue (read ("mix/pan", 0.5f), juce::dontSendNotification);

        if (! fader.isMouseButtonDown() && ! faderHasFocus())
            fader.setValue (read ("mix/fader", 0.75f), juce::dontSendNotification);

        if (! mute.isMouseButtonDown())
        {
            int enabled = 1;
            model.getInt (leaf ("mix/on"), enabled);
            mute.setToggleState (enabled == 0, juce::dontSendNotification);
        }

        if (! toMain.isMouseButtonDown())
        {
            int assigned = 0;
            model.getInt (leaf ("mix/st"), assigned);
            toMain.setToggleState (assigned != 0, juce::dontSendNotification);
        }

        if (! toMono.isMouseButtonDown())
        {
            int assigned = 0;
            model.getInt (leaf ("mix/mono"), assigned);
            toMono.setToggleState (assigned != 0, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background());
        auto bounds = getLocalBounds().reduced (3).toFloat();
        g.setColour (juce::Colour (0xff2a313a));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xff7eb6d6));
        g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        area.removeFromTop (12);
        scribble.setBounds (area.removeFromTop (28).reduced (2, 0));
        pan.setBounds (area.removeFromTop (46).withSizeKeepingCentre (44, 44));
        auto buttons = area.removeFromBottom (26);
        mute.setBounds (buttons.removeFromLeft (34).reduced (1, 0));
        toMain.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        toMono.setBounds (buttons.reduced (1, 0));
        area.removeFromBottom (2);
        scale.setBounds (area.removeFromLeft (36));
        fader.setBounds (area.reduced (1, 0));
    }

private:
    juce::String leaf (const char* name) const
    {
        return "/bus/" + juce::String (index + 1).paddedLeft ('0', 2) + "/" + name;
    }

    float read (const char* name, float fallback) const
    {
        float value = fallback;
        model.getFloat (leaf (name), value);
        return value;
    }

    bool faderHasFocus() const
    {
        for (auto* child : fader.getChildren())
            if (child->hasKeyboardFocus (true))
                return true;

        return false;
    }

    ConsoleModel& model;
    int index = 0;
    juce::TextButton scribble;
    juce::Slider pan;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton toMain { "ST" };
    juce::TextButton toMono { "MO" };
};

class BusBank : public juce::Component
{
public:
    static constexpr int stripWidth = 112;

    explicit BusBank (ConsoleModel& model)
    {
        for (int i = 0; i < numBuses; ++i)
        {
            strips[(size_t) i] = std::make_unique<BusStrip> (model, i);
            addAndMakeVisible (*strips[(size_t) i]);
        }
    }

    void refresh()
    {
        for (auto& strip : strips)
            strip->refresh();
    }

    void resized() override
    {
        for (int i = 0; i < numBuses; ++i)
            strips[(size_t) i]->setBounds (i * stripWidth, 0, stripWidth, getHeight());
    }

    int widthForBuses() const { return stripWidth * numBuses; }

private:
    std::array<std::unique_ptr<BusStrip>, numBuses> strips;
};

class MatrixStrip : public juce::Component
{
public:
    MatrixStrip (ConsoleModel& modelToUse, int matrixIndex)
        : model (modelToUse), index (matrixIndex), scale (fader)
    {
        scribble.setButtonText ("Mtx " + juce::String (index + 1));
        scribble.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
        scribble.setColour (juce::TextButton::textColourOffId, text());
        scribble.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (scribble);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 16);
        fader.setRange (0.0, 1.0, 0.0);
        fader.setScrollWheelEnabled (false);
        fader.setDoubleClickReturnValue (true, 0.75);
        fader.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
        fader.valueFromTextFunction = [] (const juce::String& text)
        {
            if (text.containsIgnoreCase ("inf"))
                return 0.0;

            return (double) dbToFader (text.getFloatValue());
        };
        fader.onValueChange = [this] { model.setFloat (leaf ("mix/fader"), (float) fader.getValue()); };
        addAndMakeVisible (fader);
        addAndMakeVisible (scale);

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
        mute.setTooltip ("Mute matrix");
        mute.onClick = [this] { model.setInt (leaf ("mix/on"), mute.getToggleState() ? 0 : 1); };
        addAndMakeVisible (mute);

        refresh();
    }

    void refresh()
    {
        if (! fader.isMouseButtonDown())
            fader.setValue (read ("mix/fader", 0.75f), juce::dontSendNotification);

        if (! mute.isMouseButtonDown())
        {
            int enabled = 1;
            model.getInt (leaf ("mix/on"), enabled);
            mute.setToggleState (enabled == 0, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background());
        auto bounds = getLocalBounds().reduced (3).toFloat();
        g.setColour (juce::Colour (0xff2a313a));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xffd27ad4));
        g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        area.removeFromTop (12);
        scribble.setBounds (area.removeFromTop (28).reduced (2, 0));
        auto buttons = area.removeFromBottom (26);
        mute.setBounds (buttons.reduced (28, 0));
        area.removeFromBottom (2);
        scale.setBounds (area.removeFromLeft (36));
        fader.setBounds (area.reduced (1, 0));
    }

private:
    juce::String leaf (const char* name) const
    {
        return "/mtx/" + juce::String (index + 1).paddedLeft ('0', 2) + "/" + name;
    }

    float read (const char* name, float fallback) const
    {
        float value = fallback;
        model.getFloat (leaf (name), value);
        return value;
    }

    ConsoleModel& model;
    int index = 0;
    juce::TextButton scribble;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
};

class MatrixBank : public juce::Component
{
public:
    static constexpr int stripWidth = 112;

    explicit MatrixBank (ConsoleModel& model)
    {
        for (int i = 0; i < numMatrices; ++i)
        {
            strips[(size_t) i] = std::make_unique<MatrixStrip> (model, i);
            addAndMakeVisible (*strips[(size_t) i]);
        }
    }

    void refresh()
    {
        for (auto& strip : strips)
            strip->refresh();
    }

    void resized() override
    {
        for (int i = 0; i < numMatrices; ++i)
            strips[(size_t) i]->setBounds (i * stripWidth, 0, stripWidth, getHeight());
    }

    int widthForMatrices() const { return stripWidth * numMatrices; }

private:
    std::array<std::unique_ptr<MatrixStrip>, numMatrices> strips;
};

class MatrixSendPanel : public juce::Component
{
public:
    explicit MatrixSendPanel (ConsoleModel& modelToUse)
        : model (modelToUse)
    {
        heading.setText ("Matrix sends", juce::dontSendNotification);
        heading.setFont (juce::Font (juce::FontOptions (16.0f)));
        heading.setColour (juce::Label::textColourId, text());
        addAndMakeVisible (heading);

        source.addItem ("Main stereo", 1);
        source.addItem ("Main mono", 2);

        for (int bus = 0; bus < numBuses; ++bus)
            source.addItem ("Bus " + juce::String (bus + 1), bus + 3);

        source.setSelectedId (3, juce::dontSendNotification);
        source.onChange = [this] { reload(); };
        addAndMakeVisible (source);

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            auto& slider = level[(size_t) matrix];
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            slider.setRange (0.0, 1.0, 0.0);
            slider.setScrollWheelEnabled (false);
            slider.setDoubleClickReturnValue (true, 0.75);
            slider.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
            slider.onValueChange = [this, matrix]
            {
                if (! applying)
                    model.setFloat (levelPath (matrix), (float) level[(size_t) matrix].getValue());
            };
            addAndMakeVisible (slider);

            auto& button = pre[(size_t) matrix];
            button.setButtonText ("Pre");
            button.setClickingTogglesState (true);
            button.setTooltip ("Take this matrix send before the bus fader");
            button.onClick = [this, matrix]
            {
                if (! applying && source.getSelectedId() >= 3)
                    model.setInt (tapPath (matrix), pre[(size_t) matrix].getToggleState() ? 1 : 0);
            };
            addAndMakeVisible (button);

            auto& title = names[(size_t) matrix];
            title.setText ("Mtx " + juce::String (matrix + 1), juce::dontSendNotification);
            title.setJustificationType (juce::Justification::centred);
            title.setColour (juce::Label::textColourId, dim());
            addAndMakeVisible (title);
        }

        reload();
    }

    void refresh()
    {
        for (auto& slider : level)
            if (slider.isMouseButtonDown())
                return;

        reload();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 8);
        auto header = area.removeFromTop (28);
        heading.setBounds (header.removeFromLeft (140));
        source.setBounds (header.removeFromLeft (160));
        const int width = juce::jmax (80, area.getWidth() / numMatrices);

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            auto column = area.removeFromLeft (width).reduced (4, 0);
            names[(size_t) matrix].setBounds (column.removeFromTop (16));
            pre[(size_t) matrix].setBounds (column.removeFromBottom (24).reduced (8, 0));
            level[(size_t) matrix].setBounds (column);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel());
    }

private:
    juce::String levelPath (int matrix) const
    {
        const auto number = juce::String (matrix + 1).paddedLeft ('0', 2);
        const int id = source.getSelectedId();

        if (id == 1)
            return "/main/st/mix/" + number + "/level";

        if (id == 2)
            return "/main/m/mix/" + number + "/level";

        return "/bus/" + juce::String (id - 2).paddedLeft ('0', 2) + "/mix/" + number + "/level";
    }

    juce::String tapPath (int matrix) const
    {
        const auto number = juce::String (matrix + 1).paddedLeft ('0', 2);
        const int id = source.getSelectedId();

        if (id < 3)
            return {};

        return "/bus/" + juce::String (id - 2).paddedLeft ('0', 2) + "/mix/" + number + "/tap";
    }

    void reload()
    {
        applying = true;
        const bool busSource = source.getSelectedId() >= 3;

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            float value = 0.0f;
            model.getFloat (levelPath (matrix), value);
            level[(size_t) matrix].setValue (value, juce::dontSendNotification);
            pre[(size_t) matrix].setEnabled (busSource);

            int tap = 0;

            if (busSource)
                model.getInt (tapPath (matrix), tap);

            pre[(size_t) matrix].setToggleState (tap != 0, juce::dontSendNotification);
        }

        applying = false;
    }

    ConsoleModel& model;
    bool applying = false;
    juce::Label heading;
    juce::ComboBox source;
    std::array<juce::Label, numMatrices> names;
    std::array<juce::Slider, numMatrices> level;
    std::array<juce::TextButton, numMatrices> pre;
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

        input.setTextWhenNothingSelected ("No input");
        input.setScrollWheelEnabled (false);
        input.addItem ("No input", 1);

        for (int i = 0; i < numRoutableInputs; ++i)
            input.addItem (ConsoleModel::inputName (i), i + 2);

        input.onChange = [this]
        {
            if (applying || model == nullptr || sourceIndex < 0)
                return;

            const int chosen = input.getSelectedId() <= 1 ? -1 : input.getSelectedId() - 2;
            const int current = model->inputForSource (sourceIndex);

            if (current == chosen)
                return;

            if (current >= 0)
                model->setInputSource (current, -1);

            if (chosen >= 0)
                model->setInputSource (chosen, sourceIndex);
        };
        addAndMakeVisible (input);
        remove.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7edf3));
        remove.setTooltip ("Remove source");
        addAndMakeVisible (remove);
    }

    void setRow (ConsoleModel& modelToUse, int sourceId, const juce::String& sourceName, const juce::String& file, bool rowSelected, std::function<void (int)> callback)
    {
        model = &modelToUse;
        sourceIndex = sourceId;
        id = sourceId;
        selected = rowSelected;
        name.setText (sourceName, juce::dontSendNotification);
        path.setText (file, juce::dontSendNotification);
        path.setTooltip (file);
        applying = true;
        const int patched = modelToUse.inputForSource (sourceId);
        input.setSelectedId (patched < 0 ? 1 : patched + 2, juce::dontSendNotification);
        applying = false;
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
        area.removeFromRight (8);
        input.setBounds (area.removeFromRight (150).reduced (0, 2));
        area.removeFromRight (12);
        name.setBounds (area.removeFromLeft (juce::jmin (240, area.getWidth() / 3)));
        path.setBounds (area);
    }

private:
    int id = 0;
    bool selected = false;
    ConsoleModel* model = nullptr;
    int sourceIndex = -1;
    bool applying = false;
    juce::Label name;
    juce::Label path;
    juce::ComboBox input;
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

        item->setRow (model, row, model.getSourceName (row), model.getSourceFile (row), selected, onRemove);
        return item;
    }

private:
    ConsoleModel& model;
    std::function<void (int)> onRemove;
};

TransportButton::TransportButton(Icon iconToDraw)
    : juce::Button({}),
      icon(iconToDraw)
{
}

void TransportButton::setPlaying(bool shouldPlay)
{
    if (playing == shouldPlay)
        return;

    playing = shouldPlay;
    repaint();
}

void TransportButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    auto fill = icon == Icon::play && playing ? juce::Colour(0xff3d8f5a) : juce::Colour(0xff2e343d);

    if (down)
        fill = fill.brighter(0.08f);
    else if (highlighted)
        fill = fill.brighter(0.15f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 4.0f);

    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    g.setColour(juce::Colours::white);

    if (icon == Icon::play && playing)
    {
        g.fillRoundedRectangle(cx - 6.5f, cy - 7.0f, 4.0f, 14.0f, 1.0f);
        g.fillRoundedRectangle(cx + 2.5f, cy - 7.0f, 4.0f, 14.0f, 1.0f);
        return;
    }

    if (icon == Icon::restart)
    {
        g.fillRoundedRectangle(cx - 9.0f, cy - 7.0f, 2.5f, 14.0f, 0.6f);
        juce::Path triangle;
        triangle.addTriangle(cx - 4.0f, cy - 7.0f, cx - 4.0f, cy + 7.0f, cx + 8.0f, cy);
        g.fillPath(triangle);
        return;
    }

    juce::Path triangle;
    triangle.addTriangle(cx - 5.0f, cy - 8.0f, cx - 5.0f, cy + 8.0f, cx + 8.0f, cy);
    g.fillPath(triangle);
}

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

    title.setText ("Virtual Console " + juce::String (JUCE_APPLICATION_VERSION_STRING), juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (18.0f)));
    title.setColour (juce::Label::textColourId, text());
    addAndMakeVisible (title);

    sourcesButton.setClickingTogglesState (true);
    sourcesButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    sourcesButton.onClick = [this]
    {
        sourcesOpen = sourcesButton.getToggleState();
        resized();
    };
    addAndMakeVisible (sourcesButton);
    sourcesButton.setTooltip ("Show the source list above the faders");

    busesButton.setClickingTogglesState (true);
    busesButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    busesButton.setTooltip ("Show the 16 buses on the fader bank");
    busesButton.onClick = [this]
    {
        busesOpen = busesButton.getToggleState();

        if (busesOpen)
        {
            matricesOpen = false;
            matricesButton.setToggleState (false, juce::dontSendNotification);
        }

        resized();
    };
    addAndMakeVisible (busesButton);

    matricesButton.setClickingTogglesState (true);
    matricesButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    matricesButton.setTooltip ("Show the 6 matrices and their sends");
    matricesButton.onClick = [this]
    {
        matricesOpen = matricesButton.getToggleState();

        if (matricesOpen)
        {
            busesOpen = false;
            busesButton.setToggleState (false, juce::dontSendNotification);
        }

        resized();
    };
    addAndMakeVisible (matricesButton);

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

    mixLabel.setJustificationType (juce::Justification::centredRight);
    mixLabel.setColour (juce::Label::textColourId, dim());
    mixLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (mixLabel);
    mixBox.addItem ("LR", 1);

    for (int bus = 1; bus <= numBuses; ++bus)
        mixBox.addItem ("Bus " + juce::String (bus), bus + 1);

    mixBox.setSelectedId (1, juce::dontSendNotification);
    mixBox.onChange = [this] { model.setSpeakerSource (mixBox.getSelectedId() - 1); };
    mixBox.setTooltip ("Signal sent to the output device. LR is the main stereo mix.");
    addAndMakeVisible (mixBox);

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
    sourceList.setRowHeight (48);
    sourceList.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff3c4450));
    sourceList.setOutlineThickness (1);
    addAndMakeVisible (sourceList);

    strips = std::make_unique<StripBank> (model, engine);
    strips->setSelectHandler ([this] (int channel) { selectChannel (channel); });
    buses = std::make_unique<BusBank> (model);
    matrices = std::make_unique<MatrixBank> (model);
    matrixSends = std::make_unique<MatrixSendPanel> (model);
    addChildComponent (matrixSends.get());
    viewport.setViewedComponent (strips.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    channelEditor = std::make_unique<ChannelEditor> (model);
    channelEditor->onChannelChanged = [this] (int channel) { selectChannel (channel); };
    addAndMakeVisible (channelEditor.get());

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
    selectChannel (0);
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
    g.fillRect (0, 0, getWidth(), 78);
    g.setColour (juce::Colour (0xff2c333b));
    g.fillRect (0, 77, getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (42).reduced (10, 6);
    title.setBounds (top.removeFromLeft (210));
    top.removeFromLeft (8);
    sourcesButton.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (6);
    busesButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (6);
    matricesButton.setBounds (top.removeFromLeft (96));
    addButton.setBounds (top.removeFromRight (110).reduced (4, 0));
    listenBox.setBounds (top.removeFromRight (110).reduced (4, 0));
    listenLabel.setBounds (top.removeFromRight (48));
    setupButton.setBounds (top.removeFromRight (80).reduced (4, 0));
    outputBox.setBounds (top.removeFromRight (168).reduced (4, 0));
    outputLabel.setBounds (top.removeFromRight (56));
    mixBox.setBounds (top.removeFromRight (96).reduced (4, 0));
    mixLabel.setBounds (top.removeFromRight (36));

    auto transport = area.removeFromTop (36).reduced (10, 4);
    playButton.setBounds (transport.removeFromLeft (36));
    transport.removeFromLeft (6);
    restartButton.setBounds (transport.removeFromLeft (36));
    transport.removeFromLeft (10);
    timeLabel.setBounds (transport.removeFromRight (118));
    timeline.setBounds (transport.reduced (0, 2));

    auto topHalf = area.removeFromTop (area.getHeight() / 2);
    sourceHeading.setVisible (sourcesOpen);
    emptySources.setVisible (sourcesOpen && model.getNumSources() == 0);
    sourceList.setVisible (sourcesOpen && model.getNumSources() > 0);
    channelEditor->setVisible (! sourcesOpen && ! matricesOpen);
    matrixSends->setVisible (matricesOpen && ! sourcesOpen);

    if (sourcesOpen)
    {
        auto pageArea = topHalf.reduced (16, 8);
        sourceHeading.setBounds (pageArea.removeFromTop (32));
        emptySources.setBounds (pageArea.removeFromTop (40));
        sourceList.setBounds (pageArea);
    }
    else if (matricesOpen)
    {
        matrixSends->setBounds (topHalf.reduced (8, 6));
    }
    else
    {
        channelEditor->setBounds (topHalf.reduced (8, 6));
    }

    auto mainColumn = area.removeFromRight (150).reduced (6, 6);
    mainLabel.setBounds (mainColumn.removeFromTop (20));
    mainMute.setBounds (mainColumn.removeFromBottom (26).reduced (28, 0));
    mainScale.setBounds (mainColumn.removeFromLeft (40));
    mainMeter.setBounds (mainColumn.removeFromRight (12).reduced (0, 6));
    mainFader.setBounds (mainColumn.reduced (2, 0));

    status.setBounds (area.removeFromBottom (18).reduced (10, 0));
    viewport.setBounds (area.reduced (4, 4));
    const int stripHeight = std::max (0, viewport.getHeight() - viewport.getScrollBarThickness());
    auto* bank = matricesOpen ? static_cast<juce::Component*> (matrices.get())
                              : busesOpen ? static_cast<juce::Component*> (buses.get())
                                          : static_cast<juce::Component*> (strips.get());

    if (viewport.getViewedComponent() != bank)
        viewport.setViewedComponent (bank, false);

    const int bankWidth = matricesOpen ? matrices->widthForMatrices()
                                        : busesOpen ? buses->widthForBuses()
                                                    : strips->widthForChannels();
    bank->setSize (bankWidth, stripHeight);
}

void MainComponent::selectChannel (int channelIndex)
{
    selectedChannel = juce::jlimit (0, numChannels - 1, channelIndex);
    const bool wasShowingSources = sourcesOpen;
    sourcesOpen = false;
    sourcesButton.setToggleState (false, juce::dontSendNotification);
    strips->setSelected (selectedChannel);
    channelEditor->setChannel (selectedChannel);

    const int x = selectedChannel * StripBank::stripWidth;
    const auto view = viewport.getViewArea();

    if (x < view.getX() || x + StripBank::stripWidth > view.getRight())
        viewport.setViewPosition (std::max (0, x - StripBank::stripWidth), 0);

    if (wasShowingSources)
        resized();
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
    strips->refresh();
    buses->refresh();
    matrices->refresh();
    matrixSends->refresh();
    channelEditor->refresh();
}

void MainComponent::syncTransport()
{
    playButton.setPlaying (engine.isPlaying());

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
        if (model.findSourceByFile (file.getFullPathName()) >= 0)
            continue;

        const int index = model.addSource (file.getFileNameWithoutExtension(), file.getFullPathName());

        if (index >= 0)
        {
            engine.openSource (index, file);
            const int input = model.firstFreeInput();

            if (input >= 0)
                model.setInputSource (input, index);
        }
    }

    refreshSources();
}

void MainComponent::refreshSources()
{
    juce::StringArray names;

    for (int i = 0; i < model.getNumSources(); ++i)
        names.add (model.getSourceName (i));

    strips->refresh();
    channelEditor->setSourceNames (names);
    sourceList.updateContent();
    sourceList.repaint();

    if (sourcesOpen)
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
