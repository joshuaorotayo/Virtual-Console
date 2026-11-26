#include "ui/MainComponent.h"
#include "ui/GroupPad.h"
#include "ui/InsertEditor.h"
#include "ui/RoutingBoard.h"
#include "ui/StripColour.h"

#include "model/DspLaw.h"
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

bool assignedToDca (ConsoleModel& model, const juce::String& root, int dca)
{
    int assigned = 0;
    model.getInt (root + "/grp/dca/" + juce::String (dca + 1).paddedLeft ('0', 2), assigned);
    return assigned != 0;
}

bool sendingToBus (ConsoleModel& model, const juce::String& root, int bus)
{
    float level = 0.0f;
    model.getFloat (root + "/mix/" + juce::String (bus + 1).paddedLeft ('0', 2) + "/level", level);
    return level > 0.001f;
}

void styleSelect (juce::TextButton& button, const juce::String& tip)
{
    button.setButtonText ("SEL");
    button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
    button.setColour (juce::TextButton::textColourOffId, text());
    button.setTooltip (tip);
}

void showSelect (juce::TextButton& button, bool selected, int stored)
{
    button.setColour (juce::TextButton::buttonColourId, selected ? stripColourFrom (stored) : juce::Colour (0xff232830));
    button.setColour (juce::TextButton::textColourOffId, selected ? stripIconInk (stored) : text());
}

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

class StripName : public juce::Label
{
public:
    std::function<void()> onSelect;

    StripName()
    {
        setEditable(false, true, false);
        setJustificationType(juce::Justification::centred);
        setColour(juce::Label::backgroundColourId, juce::Colour(0xff232830));
        setColour(juce::Label::textColourId, text());
        setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff1c2128));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.getNumberOfClicks() == 1 && onSelect != nullptr)
            onSelect();

        juce::Label::mouseDown(event);
    }
};

class BusStrip : public juce::Component
{
public:
    BusStrip (ConsoleModel& modelToUse, int busIndex)
        : model (modelToUse), index (busIndex), scale (fader)
    {
        scribble.setText (model.getBusName (index), juce::dontSendNotification);
        scribble.setTooltip ("Double-click to rename this bus");
        scribble.onSelect = [this] { if (onSelect != nullptr) onSelect(); };
        scribble.onTextChange = [this] { model.setBusName (index, scribble.getText()); };
        addAndMakeVisible (scribble);
        colourIcon.setTooltip ("Double-click to choose this bus colour");
        colourIcon.onClick = [this] { if (onSelect != nullptr) onSelect(); };
        colourIcon.onDoubleClick = [this]
        {
            openStripColourPicker (colourIcon, model, leaf ("config/color"), [this] { refresh(); });
        };
        addAndMakeVisible (colourIcon);

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
        scale.onPressed = [this] { if (onSelect != nullptr) onSelect(); };

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
        mute.setTooltip ("Mute bus");
        mute.onClick = [this] { model.setInt (leaf ("mix/on"), mute.getToggleState() ? 0 : 1); };
        addAndMakeVisible (mute);
        styleSelect (select, "Select this bus. With Sends on Fader, the input faders become the sends to it.");
        select.onClick = [this] { if (onSel != nullptr) onSel(); };
        addAndMakeVisible (select);

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

        if (! scribble.isBeingEdited() && scribble.getText() != model.getBusName (index))
            scribble.setText (model.getBusName (index), juce::dontSendNotification);

        int colour = index % 8;
        model.getInt (leaf ("config/color"), colour);
        colourIcon.setStored (colour);
        scribble.setColour (juce::Label::textColourId, stripNameColour (colour));
        showSelect (select, selected, colour);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background());
        auto bounds = getLocalBounds().reduced (3).toFloat();
        g.setColour (juce::Colour (0xff2a313a));
        g.fillRoundedRectangle (bounds, 6.0f);
        int colour = index % 8;
        model.getInt (leaf ("config/color"), colour);
        g.setColour (stripColourFrom (colour));
        g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);

        if (selected)
        {
            g.setColour (stripColourFrom (colour));
            g.drawRoundedRectangle (bounds, 6.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        area.removeFromTop (12);
        auto nameRow = area.removeFromTop (28);
        colourIcon.setBounds (nameRow.removeFromLeft (28).reduced (1, 1));
        scribble.setBounds (nameRow.reduced (2, 0));
        pan.setBounds (area.removeFromTop (46).withSizeKeepingCentre (44, 44));
        auto buttons = area.removeFromBottom (24);
        mute.setBounds (buttons.removeFromLeft (34).reduced (1, 0));
        toMain.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        toMono.setBounds (buttons.reduced (1, 0));
        select.setBounds (area.removeFromBottom (20).reduced (2, 1));
        area.removeFromBottom (2);
        scale.setBounds (area.removeFromLeft (36));
        fader.setBounds (area.reduced (1, 0));
    }

    void setSelected (bool shouldSelect)
    {
        selected = shouldSelect;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.originalComponent == this && onSelect != nullptr)
            onSelect();
    }

    std::function<void()> onSelect;
    std::function<void()> onSel;

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
    bool selected = false;
    StripName scribble;
    ColourIcon colourIcon;
    juce::Slider pan;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton select;
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

    void setSelectHandler (std::function<void (int)> handler)
    {
        for (int i = 0; i < numBuses; ++i)
            strips[(size_t) i]->onSelect = [handler, i] { if (handler != nullptr) handler (i); };
    }

    void setSelected (int bus)
    {
        for (int i = 0; i < numBuses; ++i)
            strips[(size_t) i]->setSelected (i == bus);
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
        scribble.setText (model.getMatrixName (index), juce::dontSendNotification);
        scribble.setTooltip ("Double-click to rename this matrix");
        scribble.onSelect = [this] { if (onSelect != nullptr) onSelect(); };
        scribble.onTextChange = [this] { model.setMatrixName (index, scribble.getText()); };
        addAndMakeVisible (scribble);
        colourIcon.setTooltip ("Double-click to choose this matrix colour");
        colourIcon.onClick = [this] { if (onSelect != nullptr) onSelect(); };
        colourIcon.onDoubleClick = [this]
        {
            openStripColourPicker (colourIcon, model, leaf ("config/color"), [this] { refresh(); });
        };
        addAndMakeVisible (colourIcon);

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
        scale.onPressed = [this] { if (onSelect != nullptr) onSelect(); };

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
        mute.setTooltip ("Mute matrix");
        mute.onClick = [this] { model.setInt (leaf ("mix/on"), mute.getToggleState() ? 0 : 1); };
        addAndMakeVisible (mute);
        styleSelect (select, "Edit this matrix");
        select.onClick = [this] { if (onSel != nullptr) onSel(); };
        addAndMakeVisible (select);

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

        int colour = index % 8;
        model.getInt (leaf ("config/color"), colour);
        colourIcon.setStored (colour);

        if (! scribble.isBeingEdited())
            scribble.setText (model.getMatrixName (index), juce::dontSendNotification);

        scribble.setColour (juce::Label::textColourId, stripNameColour (colour));
        showSelect (select, selected, colour);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background());
        auto bounds = getLocalBounds().reduced (3).toFloat();
        g.setColour (juce::Colour (0xff2a313a));
        g.fillRoundedRectangle (bounds, 6.0f);
        int colour = index % 8;
        model.getInt (leaf ("config/color"), colour);
        g.setColour (stripColourFrom (colour));
        g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);

        if (selected)
        {
            g.setColour (stripColourFrom (colour));
            g.drawRoundedRectangle (bounds, 6.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        area.removeFromTop (12);
        auto nameRow = area.removeFromTop (28);
        colourIcon.setBounds (nameRow.removeFromLeft (28).reduced (1, 1));
        scribble.setBounds (nameRow.reduced (2, 0));
        auto buttons = area.removeFromBottom (24);
        mute.setBounds (buttons.reduced (28, 0));
        select.setBounds (area.removeFromBottom (20).reduced (2, 1));
        area.removeFromBottom (2);
        scale.setBounds (area.removeFromLeft (36));
        fader.setBounds (area.reduced (1, 0));
    }

    void setSelected (bool shouldSelect)
    {
        selected = shouldSelect;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.originalComponent == this && onSelect != nullptr)
            onSelect();
    }

    std::function<void()> onSelect;
    std::function<void()> onSel;

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
    bool selected = false;
    StripName scribble;
    ColourIcon colourIcon;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton select;
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

    void setSelectHandler (std::function<void (int)> handler)
    {
        for (int i = 0; i < numMatrices; ++i)
            strips[(size_t) i]->onSelect = [handler, i] { if (handler != nullptr) handler (i); };
    }

    void setSelected (int matrix)
    {
        for (int i = 0; i < numMatrices; ++i)
            strips[(size_t) i]->setSelected (i == matrix);
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
            title.setText (model.getMatrixName (matrix), juce::dontSendNotification);
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
            names[(size_t) matrix].setText (model.getMatrixName (matrix), juce::dontSendNotification);
        }

        if (! source.isPopupActive())
        {
            for (int bus = 0; bus < numBuses; ++bus)
                source.changeItemText (bus + 2, model.getBusName (bus));
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

void TransportButton::setRecording(bool shouldRecord)
{
    if (recording == shouldRecord)
        return;

    recording = shouldRecord;
    repaint();
}

void TransportButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    auto fill = icon == Icon::play && playing ? juce::Colour(0xff3d8f5a)
              : icon == Icon::record && recording ? juce::Colour(0xffa33b3b)
              : juce::Colour(0xff2e343d);

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

    if (icon == Icon::record)
    {
        if (recording)
        {
            g.fillRoundedRectangle(cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, 2.0f);
            return;
        }

        g.setColour(juce::Colour(0xffe15b5b));
        g.fillEllipse(cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        return;
    }

    juce::Path triangle;
    triangle.addTriangle(cx - 5.0f, cy - 8.0f, cx - 5.0f, cy + 8.0f, cx + 8.0f, cy);
    g.fillPath(triangle);
}

class DcaStrip : public juce::Component
{
public:
    DcaStrip (ConsoleModel& modelToUse, int dcaIndex)
        : model (modelToUse), index (dcaIndex), scale (fader)
    {
        scribble.setText (model.getDcaName (index), juce::dontSendNotification);
        scribble.setTooltip ("Double-click to rename this DCA");
        scribble.onSelect = [this] { if (onSelect != nullptr) onSelect(); };
        scribble.onTextChange = [this] { model.setDcaName (index, scribble.getText()); };
        addAndMakeVisible (scribble);
        colourIcon.setTooltip ("Double-click to choose this DCA colour");
        colourIcon.onClick = [this] { if (onSelect != nullptr) onSelect(); };
        colourIcon.onDoubleClick = [this]
        {
            openStripColourPicker (colourIcon, model, path ("config/color"), [this] { refresh(); });
        };
        addAndMakeVisible (colourIcon);

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
        fader.onValueChange = [this] { model.setFloat (path ("fader"), (float) fader.getValue()); };
        fader.setTooltip ("DCA level. 0 dB leaves assigned strips at their own faders");
        addAndMakeVisible (fader);
        addAndMakeVisible (scale);
        scale.onPressed = [this] { if (onSelect != nullptr) onSelect(); };

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, muteOn());
        mute.setTooltip ("Mute every strip assigned to this DCA");
        mute.onClick = [this] { model.setInt (path ("on"), mute.getToggleState() ? 0 : 1); };
        addAndMakeVisible (mute);

        solo.setClickingTogglesState (true);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd2b84a));
        solo.setTooltip ("Solo the assigned strips after their faders");
        solo.onClick = [this] { model.setInt (path ("solo"), solo.getToggleState() ? 1 : 0); };
        addAndMakeVisible (solo);
        styleSelect (select, "Show only the inputs assigned to this DCA");
        select.onClick = [this] { if (onSel != nullptr) onSel(); };
        addAndMakeVisible (select);
        refresh();
    }

    void refresh()
    {
        if (! fader.isMouseButtonDown())
        {
            float value = 0.75f;
            model.getFloat (path ("fader"), value);
            fader.setValue (value, juce::dontSendNotification);
        }

        if (! mute.isMouseButtonDown())
        {
            int enabled = 1;
            model.getInt (path ("on"), enabled);
            mute.setToggleState (enabled == 0, juce::dontSendNotification);
        }

        if (! solo.isMouseButtonDown())
        {
            int enabled = 0;
            model.getInt (path ("solo"), enabled);
            solo.setToggleState (enabled != 0, juce::dontSendNotification);
        }

        if (! scribble.isBeingEdited() && scribble.getText() != model.getDcaName (index))
            scribble.setText (model.getDcaName (index), juce::dontSendNotification);

        int colour = index;
        model.getInt (path ("config/color"), colour);
        colourIcon.setStored (colour);
        scribble.setColour (juce::Label::textColourId, stripNameColour (colour));
        showSelect (select, selected, colour);
    }

    void setSelected (bool shouldSelect)
    {
        selected = shouldSelect;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background());
        auto bounds = getLocalBounds().reduced (3).toFloat();
        g.setColour (juce::Colour (0xff2a313a));
        g.fillRoundedRectangle (bounds, 6.0f);
        int colour = index;
        model.getInt (path ("config/color"), colour);
        g.setColour (stripColourFrom (colour));
        g.fillRoundedRectangle (8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);

        if (selected)
        {
            g.setColour (stripColourFrom (colour));
            g.drawRoundedRectangle (bounds, 6.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        area.removeFromTop (12);
        auto nameRow = area.removeFromTop (28);
        colourIcon.setBounds (nameRow.removeFromLeft (28).reduced (1, 1));
        scribble.setBounds (nameRow.reduced (2, 0));
        auto buttons = area.removeFromBottom (24);
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        solo.setBounds (buttons.reduced (1, 0));
        select.setBounds (area.removeFromBottom (20).reduced (2, 1));
        area.removeFromBottom (2);
        scale.setBounds (area.removeFromLeft (36));
        fader.setBounds (area.reduced (1, 0));
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.originalComponent == this && onSelect != nullptr)
            onSelect();
    }

    std::function<void()> onSelect;
    std::function<void()> onSel;

private:
    juce::String path (const char* leaf) const
    {
        return "/dca/" + juce::String (index + 1).paddedLeft ('0', 2) + "/" + leaf;
    }

    ConsoleModel& model;
    int index = 0;
    bool selected = false;
    StripName scribble;
    ColourIcon colourIcon;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };
    juce::TextButton select;
};

class DcaBank : public juce::Component
{
public:
    static constexpr int stripWidth = 112;

    explicit DcaBank (ConsoleModel& model)
    {
        for (int i = 0; i < numDcas; ++i)
        {
            strips[(size_t) i] = std::make_unique<DcaStrip> (model, i);
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
        for (int i = 0; i < numDcas; ++i)
            strips[(size_t) i]->setBounds (i * stripWidth, 0, stripWidth, getHeight());
    }

    int widthForDcas() const { return stripWidth * numDcas; }

private:
    std::array<std::unique_ptr<DcaStrip>, numDcas> strips;
};

class SpareStrip : public juce::Component
{
public:
    SpareStrip(ConsoleModel& modelToUse, const juce::String& pathRoot)
        : model(modelToUse), root(pathRoot), scale(fader)
    {
        juce::String stored;
        model.getString(root + "/config/name", stored);
        scribble.setText(stored, juce::dontSendNotification);
        scribble.setTooltip("Double-click to rename");
        scribble.onSelect = [this] { if (onSelect != nullptr) onSelect(); };
        scribble.onTextChange = [this] { model.setString(root + "/config/name", scribble.getText()); };
        addAndMakeVisible(scribble);
        colourIcon.setTooltip("Double-click to choose this colour");
        colourIcon.onClick = [this] { if (onSelect != nullptr) onSelect(); };
        colourIcon.onDoubleClick = [this]
        {
            openStripColourPicker(colourIcon, model, root + "/config/color", [this] { refresh(); });
        };
        addAndMakeVisible(colourIcon);

        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
        fader.setRange(0.0, 1.0, 0.0);
        fader.setScrollWheelEnabled(false);
        fader.setDoubleClickReturnValue(true, 0.75);
        fader.textFromValueFunction = [](double value) { return decibelText(faderToDb((float) value)); };
        fader.valueFromTextFunction = [](const juce::String& textValue)
        {
            if (textValue.containsIgnoreCase("inf"))
                return 0.0;

            return (double) dbToFader(textValue.getFloatValue());
        };
        fader.onValueChange = [this] { model.setFloat(levelPath(), (float) fader.getValue()); };
        addAndMakeVisible(fader);
        addAndMakeVisible(scale);
        scale.onPressed = [this] { if (onSelect != nullptr) onSelect(); };

        mute.setClickingTogglesState(true);
        mute.setColour(juce::TextButton::buttonOnColourId, muteOn());
        mute.setTooltip("Mute");
        mute.onClick = [this] { model.setInt(mutePath(), mute.getToggleState() ? 0 : 1); };
        addAndMakeVisible(mute);
        toMain.setClickingTogglesState(true);
        toMono.setClickingTogglesState(true);
        toMain.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
        toMono.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
        toMain.setTooltip("Assign this side to main LR");
        toMono.setTooltip("Assign this side to the center bus");
        toMain.onClick = [this] { model.setInt(root + "/mix/st", toMain.getToggleState() ? 1 : 0); };
        toMono.onClick = [this] { model.setInt(root + "/mix/mono", toMono.getToggleState() ? 1 : 0); };
        addAndMakeVisible(toMain);
        addAndMakeVisible(toMono);
        refresh();
    }

    void refresh()
    {
        if (! fader.isMouseButtonDown())
        {
            float value = 0.75f;
            model.getFloat(levelPath(), value);
            fader.setValue(value, juce::dontSendNotification);
        }

        if (! mute.isMouseButtonDown())
        {
            int enabled = 1;
            model.getInt(mutePath(), enabled);
            mute.setToggleState(enabled == 0, juce::dontSendNotification);
        }

        if (! toMain.isMouseButtonDown())
        {
            int assigned = 0;
            model.getInt(root + "/mix/st", assigned);
            toMain.setToggleState(assigned != 0, juce::dontSendNotification);
        }

        if (! toMono.isMouseButtonDown())
        {
            int assigned = 0;
            model.getInt(root + "/mix/mono", assigned);
            toMono.setToggleState(assigned != 0, juce::dontSendNotification);
        }

        int colour = 6;
        model.getInt(root + "/config/color", colour);
        colourIcon.setStored(colour);

        if (! scribble.isBeingEdited())
        {
            juce::String stored;
            model.getString(root + "/config/name", stored);
            scribble.setText(stored, juce::dontSendNotification);
        }

        scribble.setColour(juce::Label::textColourId, stripNameColour(colour));
    }

    void setSelected(bool shouldSelect)
    {
        selected = shouldSelect;
        repaint();
    }

    void setHighlighted(bool shouldHighlight)
    {
        if (highlighted == shouldHighlight)
            return;

        highlighted = shouldHighlight;
        repaint();
    }

    void setSendBus(int busIndex)
    {
        const int next = busIndex >= 0 && busIndex < numBuses ? busIndex : -1;

        if (sendBus == next)
            return;

        sendBus = next;
        fader.setTooltip(sendBus < 0 ? "Return level" : "Send to " + model.getBusName(sendBus));
        mute.setTooltip(sendBus < 0 ? "Mute" : "Mute this strip on " + model.getBusName(sendBus));
        refresh();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.originalComponent == this && onSelect != nullptr)
            onSelect();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(background());
        auto bounds = getLocalBounds().reduced(3).toFloat();
        g.setColour(juce::Colour(0xff2a313a));
        g.fillRoundedRectangle(bounds, 6.0f);
        int colour = 6;
        model.getInt(root + "/config/color", colour);
        g.setColour(stripColourFrom(colour));
        g.fillRoundedRectangle(8.0f, 8.0f, (float) getWidth() - 16.0f, 4.0f, 2.0f);

        if (selected || highlighted)
        {
            g.setColour(highlighted ? juce::Colour(0xffe0b040) : stripColourFrom(colour));
            g.drawRoundedRectangle(bounds, 6.0f, highlighted ? 3.0f : 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(5);
        area.removeFromTop(12);
        auto nameRow = area.removeFromTop(28);
        colourIcon.setBounds(nameRow.removeFromLeft(28).reduced(1, 1));
        scribble.setBounds(nameRow.reduced(2, 0));
        mute.setBounds(area.removeFromBottom(26).reduced(18, 0));
        auto assign = area.removeFromBottom(22);
        toMono.setBounds(assign.removeFromRight(assign.getWidth() / 2).reduced(2, 1));
        toMain.setBounds(assign.reduced(2, 1));
        area.removeFromBottom(2);
        scale.setBounds(area.removeFromLeft(36));
        fader.setBounds(area.reduced(1, 0));
    }

    std::function<void()> onSelect;

private:
    juce::String levelPath() const
    {
        if (sendBus >= 0)
            return root + "/mix/" + juce::String(sendBus + 1).paddedLeft('0', 2) + "/level";

        return root + "/mix/fader";
    }

    juce::String mutePath() const
    {
        if (sendBus >= 0)
            return root + "/mix/" + juce::String(sendBus + 1).paddedLeft('0', 2) + "/on";

        return root + "/mix/on";
    }

    ConsoleModel& model;
    juce::String root;
    int sendBus = -1;
    bool selected = false;
    bool highlighted = false;
    StripName scribble;
    ColourIcon colourIcon;
    ConsoleFader fader;
    FaderScale scale;
    juce::TextButton mute { "M" };
    juce::TextButton toMain { "ST" };
    juce::TextButton toMono { "MO" };
};

class OutputRow : public juce::Component
{
public:
    void setStrips(const std::vector<juce::Component*>& next)
    {
        auto previous = strips;
        strips = next;

        for (auto* strip : previous)
        {
            if (strip->getParentComponent() == this)
            {
                strip->setVisible(false);
                removeChildComponent(strip);
            }
        }

        for (auto* strip : strips)
        {
            addAndMakeVisible(strip);
            strip->setVisible(true);
        }

        resized();
    }

    void resized() override
    {
        const int count = juce::jmax(1, (int) strips.size());
        const int width = juce::jmax(1, getWidth() / count);

        for (int i = 0; i < (int) strips.size(); ++i)
            strips[(size_t) i]->setBounds(i * width, 0, i == count - 1 ? getWidth() - i * width : width, getHeight());
    }

private:
    std::vector<juce::Component*> strips;
};

class FaderBay : public juce::Component
{
public:
    static constexpr int stripWidth = 88;

    FaderBay(ConsoleModel& modelToUse, MixEngine& engine)
        : console(modelToUse)
    {
        for (int i = 0; i < numChannels; ++i)
        {
            channels[(size_t) i] = std::make_unique<ChannelStrip>(modelToUse, engine, i);
            addChildComponent(*channels[(size_t) i]);
            channels[(size_t) i]->onSelect = [this, i] { if (onChannel != nullptr) onChannel(i); };
        }

        for (int i = 0; i < numAuxIns; ++i)
        {
            const auto path = "/auxin/" + juce::String(i + 1).paddedLeft('0', 2);
            aux[(size_t) i] = std::make_unique<SpareStrip>(modelToUse, path);
            addChildComponent(*aux[(size_t) i]);
            aux[(size_t) i]->onSelect = [this, i] { if (onSpare != nullptr) onSpare(0, i); };
        }

        for (int i = 0; i < numUsbReturns; ++i)
        {
            const auto path = "/usb/" + juce::String(i + 1).paddedLeft('0', 2);
            usb[(size_t) i] = std::make_unique<SpareStrip>(modelToUse, path);
            addChildComponent(*usb[(size_t) i]);
            usb[(size_t) i]->onSelect = [this, i] { if (onSpare != nullptr) onSpare(1, i); };
        }

        for (int i = 0; i < numFxReturns; ++i)
        {
            const auto path = "/fxrtn/" + juce::String(i + 1).paddedLeft('0', 2);
            fx[(size_t) i] = std::make_unique<SpareStrip>(modelToUse, path);
            addChildComponent(*fx[(size_t) i]);
            fx[(size_t) i]->onSelect = [this, i] { if (onSpare != nullptr) onSpare(2, i); };
        }

        for (int i = 0; i < numDcas; ++i)
        {
            dcas[(size_t) i] = std::make_unique<DcaStrip>(modelToUse, i);
            addChildComponent(*dcas[(size_t) i]);
            dcas[(size_t) i]->onSelect = [this, i] { if (onSpare != nullptr) onSpare(3, i); };
            dcas[(size_t) i]->onSel = [this, i] { if (onDcaSel != nullptr) onDcaSel(i); };
        }

        for (int i = 0; i < numBuses; ++i)
        {
            buses[(size_t) i] = std::make_unique<BusStrip>(modelToUse, i);
            addChildComponent(*buses[(size_t) i]);
            buses[(size_t) i]->onSelect = [this, i] { if (onBus != nullptr) onBus(i); };
            buses[(size_t) i]->onSel = [this, i] { if (onBusSel != nullptr) onBusSel(i); };
        }

        for (int i = 0; i < numMatrices; ++i)
        {
            matrices[(size_t) i] = std::make_unique<MatrixStrip>(modelToUse, i);
            addChildComponent(*matrices[(size_t) i]);
            matrices[(size_t) i]->onSelect = [this, i] { if (onMatrix != nullptr) onMatrix(i); };
            matrices[(size_t) i]->onSel = [this, i] { if (onMatrixSel != nullptr) onMatrixSel(i); };
        }

        showChannelRange(0, 16);
    }

    void showChannelRange(int first, int count)
    {
        clearInputs();

        for (int i = first; i < first + count && i < numChannels; ++i)
            addInput(channels[(size_t) i].get());

        resized();
    }

    void showReturns(bool auxIns, bool usbReturns, bool fxReturns)
    {
        clearInputs();

        if (auxIns)
            for (auto& strip : aux)
                addInput(strip.get());

        if (usbReturns)
            for (auto& strip : usb)
                addInput(strip.get());

        if (fxReturns)
            for (auto& strip : fx)
                addInput(strip.get());

        resized();
    }

    void showEffects()
    {
        clearInputs();

        for (auto& strip : fx)
            addInput(strip.get());

        for (int bus = 12; bus < numBuses; ++bus)
            addInput(buses[(size_t) bus].get());

        resized();
    }

    void highlightBusStrip(int bus)
    {
        for (int i = 0; i < numBuses; ++i)
            buses[(size_t) i]->setSelected(i == bus);
    }

    void showDcaMembers(int dca)
    {
        clearInputs();

        for (int i = 0; i < numChannels; ++i)
            if (assignedToDca(console, "/ch/" + juce::String(i + 1).paddedLeft('0', 2), dca))
                addInput(channels[(size_t) i].get());

        for (int i = 0; i < numAuxIns; ++i)
            if (assignedToDca(console, "/auxin/" + juce::String(i + 1).paddedLeft('0', 2), dca))
                addInput(aux[(size_t) i].get());

        for (int i = 0; i < numUsbReturns; ++i)
            if (assignedToDca(console, "/usb/" + juce::String(i + 1).paddedLeft('0', 2), dca))
                addInput(usb[(size_t) i].get());

        for (int i = 0; i < numFxReturns; ++i)
            if (assignedToDca(console, "/fxrtn/" + juce::String(i + 1).paddedLeft('0', 2), dca))
                addInput(fx[(size_t) i].get());

        resized();
    }

    void showBuses(OutputRow& row, int first, int count)
    {
        std::vector<juce::Component*> strips;

        for (int i = first; i < first + count && i < numBuses; ++i)
            strips.push_back(buses[(size_t) i].get());

        row.setStrips(strips);
    }

    void showDcas(OutputRow& row)
    {
        std::vector<juce::Component*> strips;

        for (auto& strip : dcas)
            strips.push_back(strip.get());

        row.setStrips(strips);
    }

    void showMatrices(OutputRow& row)
    {
        std::vector<juce::Component*> strips;

        for (auto& strip : matrices)
            strips.push_back(strip.get());

        row.setStrips(strips);
    }

    void clearOutputs(OutputRow& row)
    {
        row.setStrips({});
    }

    void setSendBus(int bus)
    {
        for (auto& strip : channels)
            strip->setSendBus(bus);

        for (auto& strip : aux)
            strip->setSendBus(bus);

        for (auto& strip : usb)
            strip->setSendBus(bus);

        for (auto& strip : fx)
            strip->setSendBus(bus);
    }

    void setHighlightBus(int bus)
    {
        highlightBus = bus;
        applyHighlight();
    }

    void refresh()
    {
        for (auto& strip : channels)
            strip->refresh();

        for (auto& strip : aux)
            strip->refresh();

        for (auto& strip : usb)
            strip->refresh();

        for (auto& strip : fx)
            strip->refresh();

        for (auto& strip : dcas)
            strip->refresh();

        for (auto& strip : buses)
            strip->refresh();

        for (auto& strip : matrices)
            strip->refresh();

        applyHighlight();
    }

    void setSelectedChannel(int channel)
    {
        for (int i = 0; i < numChannels; ++i)
            channels[(size_t) i]->setSelected(i == channel);

        clearGroups();
    }

    void setSelectedBus(int bus)
    {
        for (int i = 0; i < numBuses; ++i)
            buses[(size_t) i]->setSelected(i == bus);

        for (auto& strip : channels)
            strip->setSelected(false);

        for (auto& strip : matrices)
            strip->setSelected(false);

        clearSpares();
    }

    void setSelectedMatrix(int matrix)
    {
        for (int i = 0; i < numMatrices; ++i)
            matrices[(size_t) i]->setSelected(i == matrix);

        for (auto& strip : channels)
            strip->setSelected(false);

        for (auto& strip : buses)
            strip->setSelected(false);

        clearSpares();
    }

    void setSelectedSpare(int kind, int index)
    {
        clearGroups();

        for (auto& strip : channels)
            strip->setSelected(false);

        for (int i = 0; i < numAuxIns; ++i)
            aux[(size_t) i]->setSelected(kind == 0 && i == index);

        for (int i = 0; i < numUsbReturns; ++i)
            usb[(size_t) i]->setSelected(kind == 1 && i == index);

        for (int i = 0; i < numFxReturns; ++i)
            fx[(size_t) i]->setSelected(kind == 2 && i == index);

        for (int i = 0; i < numDcas; ++i)
            dcas[(size_t) i]->setSelected(kind == 3 && i == index);
    }

    int xFor(juce::Component* component) const
    {
        for (int i = 0; i < (int) inputs.size(); ++i)
            if (inputs[(size_t) i] == component)
                return i * stripWidth;

        return 0;
    }

    ChannelStrip* channelStrip(int index) const { return channels[(size_t) index].get(); }
    BusStrip* busStrip(int index) const { return buses[(size_t) index].get(); }
    MatrixStrip* matrixStrip(int index) const { return matrices[(size_t) index].get(); }

    int widthForBank() const { return stripWidth * juce::jmax(1, (int) inputs.size()); }

    void resized() override
    {
        for (int i = 0; i < (int) inputs.size(); ++i)
            inputs[(size_t) i]->setBounds(i * stripWidth, 0, stripWidth, getHeight());
    }

    std::function<void(int)> onChannel;
    std::function<void(int)> onBus;
    std::function<void(int)> onBusSel;
    std::function<void(int)> onMatrix;
    std::function<void(int)> onMatrixSel;
    std::function<void(int)> onDcaSel;
    std::function<void(int, int)> onSpare;

private:
    void clearGroups()
    {
        for (auto& strip : buses)
            strip->setSelected(false);

        for (auto& strip : matrices)
            strip->setSelected(false);

        clearSpares();
    }

    void clearSpares()
    {
        for (auto& strip : aux)
            strip->setSelected(false);

        for (auto& strip : usb)
            strip->setSelected(false);

        for (auto& strip : fx)
            strip->setSelected(false);

        for (auto& strip : dcas)
            strip->setSelected(false);
    }

    void clearInputs()
    {
        for (auto* strip : inputs)
            strip->setVisible(false);

        inputs.clear();
    }

    void addInput(juce::Component* strip)
    {
        addAndMakeVisible(strip);
        strip->setVisible(true);
        inputs.push_back(strip);
    }

    void applyHighlight()
    {
        for (int i = 0; i < numChannels; ++i)
            channels[(size_t) i]->setHighlighted(highlightBus >= 0 && sendingToBus(console, "/ch/" + juce::String(i + 1).paddedLeft('0', 2), highlightBus));

        for (int i = 0; i < numAuxIns; ++i)
            aux[(size_t) i]->setHighlighted(highlightBus >= 0 && sendingToBus(console, "/auxin/" + juce::String(i + 1).paddedLeft('0', 2), highlightBus));

        for (int i = 0; i < numUsbReturns; ++i)
            usb[(size_t) i]->setHighlighted(highlightBus >= 0 && sendingToBus(console, "/usb/" + juce::String(i + 1).paddedLeft('0', 2), highlightBus));

        for (int i = 0; i < numFxReturns; ++i)
            fx[(size_t) i]->setHighlighted(highlightBus >= 0 && sendingToBus(console, "/fxrtn/" + juce::String(i + 1).paddedLeft('0', 2), highlightBus));
    }

    ConsoleModel& console;
    int highlightBus = -1;
    std::vector<juce::Component*> inputs;
    std::array<std::unique_ptr<ChannelStrip>, numChannels> channels;
    std::array<std::unique_ptr<SpareStrip>, numAuxIns> aux;
    std::array<std::unique_ptr<SpareStrip>, numUsbReturns> usb;
    std::array<std::unique_ptr<SpareStrip>, numFxReturns> fx;
    std::array<std::unique_ptr<DcaStrip>, numDcas> dcas;
    std::array<std::unique_ptr<BusStrip>, numBuses> buses;
    std::array<std::unique_ptr<MatrixStrip>, numMatrices> matrices;
};

class GroupEditor : public juce::Component
{
public:
    explicit GroupEditor (ConsoleModel& modelToUse)
        : model (modelToUse)
    {
        title.setFont (juce::Font (juce::FontOptions (16.0f)));
        title.setColour (juce::Label::textColourId, text());
        title.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff1c2128));
        title.setEditable (false, true, false);
        title.onTextChange = [this]
        {
            if (root.startsWith ("/bus/") || root.startsWith ("/mtx/"))
                model.setString (root + "/config/name", title.getText());
        };
        addAndMakeVisible (title);

        for (auto* button : { &stripTarget, &stereoTarget, &centerTarget })
        {
            button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
            button->setColour (juce::TextButton::textColourOffId, text());
            addAndMakeVisible (*button);
        }

        stripTarget.onClick = [this] { setRoot (stripRoot); };
        stereoTarget.setButtonText ("LR");
        stereoTarget.onClick = [this] { setRoot ("/main/st"); };
        stereoTarget.setTooltip ("Edit the main stereo bus");
        centerTarget.setButtonText ("C");
        centerTarget.onClick = [this] { setRoot ("/main/m"); };
        centerTarget.setTooltip ("Edit the center bus");

        eqOn.setClickingTogglesState (true);
        eqOn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d8f5a));
        eqOn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c2128));
        eqOn.onClick = [this] { model.setInt (root + "/eq/on", eqOn.getToggleState() ? 1 : 0); };
        eqOn.setTooltip ("Bypass the 6-band EQ");
        addAndMakeVisible (eqOn);

        compOn.setClickingTogglesState (true);
        compOn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b040));
        compOn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c2128));
        compOn.onClick = [this] { model.setInt (root + "/dyn/on", compOn.getToggleState() ? 1 : 0); };
        compOn.setTooltip ("Turn the compressor on");
        addAndMakeVisible (compOn);

        for (int band = 0; band < numEqBands; ++band)
        {
            auto& slider = bandGain[(size_t) band];
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            slider.setRange (0.0, 1.0, 0.0);
            slider.setScrollWheelEnabled (false);
            slider.setDoubleClickReturnValue (true, 0.5);
            slider.textFromValueFunction = [] (double value) { return juce::String (eqGainDb ((float) value), 1); };
            slider.valueFromTextFunction = [] (const juce::String& text) { return (double) eqGainUnit (text.getFloatValue()); };
            slider.setTooltip ("Band " + juce::String (band + 1) + " gain");
            slider.onValueChange = [this, band] { model.setFloat (root + "/eq/" + juce::String (band + 1) + "/g", (float) bandGain[(size_t) band].getValue()); };
            addAndMakeVisible (slider);
        }

        auto setupComp = [this] (juce::Slider& slider, const juce::String& leaf, const juce::String& tip, double reset)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
            slider.setRange (0.0, 1.0, 0.0);
            slider.setScrollWheelEnabled (false);
            slider.setDoubleClickReturnValue (true, reset);
            slider.setTooltip (tip);
            slider.onValueChange = [this, leaf, &slider] { model.setFloat (root + "/dyn/" + leaf, (float) slider.getValue()); };
            addAndMakeVisible (slider);
        };

        threshold.textFromValueFunction = [] (double value) { return juce::String (compThresholdDb ((float) value), 1); };
        ratio.textFromValueFunction = [] (double value) { return juce::String (compRatio ((float) value), 1) + ":1"; };
        makeup.textFromValueFunction = [] (double value) { return juce::String (compMakeupDb ((float) value), 1); };
        setupComp (threshold, "thr", "Compressor threshold", 0.75);
        setupComp (ratio, "ratio", "Compressor ratio", 0.5);
        setupComp (makeup, "mgain", "Compressor makeup gain", 0.5);
        groupPad = std::make_unique<GroupPad> (model);
        addAndMakeVisible (*groupPad);
        processing = std::make_unique<BusProcessingView> (model);
        addAndMakeVisible (*processing);
        insertButton.setClickingTogglesState (true);
        insertButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
        insertButton.setTooltip ("Insert a graphic EQ from an FX slot");
        insertButton.onClick = [this]
        {
            insertOpen = insertButton.getToggleState();
            refresh();
            resized();
        };
        addAndMakeVisible (insertButton);
        insertEditor = std::make_unique<InsertEditor> (model);
        addAndMakeVisible (*insertEditor);

        matrixHeading.setText ("To Matrix", juce::dontSendNotification);
        matrixHeading.setColour (juce::Label::textColourId, dim());
        matrixHeading.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (matrixHeading);

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            auto& name = matrixName[(size_t) matrix];
            name.setText (model.getMatrixName (matrix), juce::dontSendNotification);
            name.setColour (juce::Label::textColourId, text());
            name.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (name);

            auto& level = matrixLevel[(size_t) matrix];
            level.setSliderStyle (juce::Slider::LinearHorizontal);
            level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 16);
            level.setRange (0.0, 1.0, 0.0);
            level.setScrollWheelEnabled (false);
            level.setDoubleClickReturnValue (true, 0.75);
            level.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
            level.onValueChange = [this, matrix]
            {
                if (! applyingMatrix)
                    model.setFloat (matrixSendPath (matrix) + "/level", (float) matrixLevel[(size_t) matrix].getValue());
            };
            addAndMakeVisible (level);

            auto& pre = matrixPre[(size_t) matrix];
            pre.setButtonText ("Pre");
            pre.setClickingTogglesState (true);
            pre.setTooltip ("Send this bus to the matrix before the bus fader");
            pre.onClick = [this, matrix]
            {
                if (! applyingMatrix)
                    model.setInt (matrixSendPath (matrix) + "/tap", matrixPre[(size_t) matrix].getToggleState() ? 1 : 0);
            };
            addAndMakeVisible (pre);
        }

        showStrip ("/bus/01", "Bus 1");
    }

    void showStrip (const juce::String& newRoot, const juce::String& newTitle)
    {
        stripRoot = newRoot;
        stripTitle = newTitle;
        stripTarget.setButtonText (newTitle);
        setRoot (newRoot);
    }

    void refresh()
    {
        if (root.isEmpty())
            return;

        juce::String shown = root == "/main/st" ? "Main LR" : root == "/main/m" ? "Center" : stripTitle;

        if (root.startsWith ("/bus/") || root.startsWith ("/mtx/"))
        {
            juce::String custom;
            model.getString (root + "/config/name", custom);

            if (custom.isNotEmpty())
                shown = custom;
        }

        if (! title.isBeingEdited())
            title.setText (shown, juce::dontSendNotification);

        if (root.startsWith ("/bus/") || root.startsWith ("/mtx/"))
            stripTarget.setButtonText (shown);
        const auto active = juce::Colour (0xff3d6f8f);
        stripTarget.setColour (juce::TextButton::buttonColourId, root == stripRoot ? active : juce::Colour (0xff232830));
        stereoTarget.setColour (juce::TextButton::buttonColourId, root == "/main/st" ? active : juce::Colour (0xff232830));
        centerTarget.setColour (juce::TextButton::buttonColourId, root == "/main/m" ? active : juce::Colour (0xff232830));

        if (! eqOn.isMouseButtonDown())
        {
            int enabled = 1;
            model.getInt (root + "/eq/on", enabled);
            eqOn.setToggleState (enabled != 0, juce::dontSendNotification);
        }

        if (! compOn.isMouseButtonDown())
        {
            int enabled = 0;
            model.getInt (root + "/dyn/on", enabled);
            compOn.setToggleState (enabled != 0, juce::dontSendNotification);
        }

        for (int band = 0; band < numEqBands; ++band)
        {
            auto& slider = bandGain[(size_t) band];

            if (slider.isMouseButtonDown())
                continue;

            float gain = 0.5f;
            model.getFloat (root + "/eq/" + juce::String (band + 1) + "/g", gain);
            slider.setValue (gain, juce::dontSendNotification);
        }

        if (! threshold.isMouseButtonDown())
        {
            float value = 0.75f;
            model.getFloat (root + "/dyn/thr", value);
            threshold.setValue (value, juce::dontSendNotification);
        }

        if (! ratio.isMouseButtonDown())
        {
            float value = 0.5f;
            model.getFloat (root + "/dyn/ratio", value);
            ratio.setValue (value, juce::dontSendNotification);
        }

        if (! makeup.isMouseButtonDown())
        {
            float value = 0.5f;
            model.getFloat (root + "/dyn/mgain", value);
            makeup.setValue (value, juce::dontSendNotification);
        }

        const bool bus = root.startsWith ("/bus/");
        groupPad->setVisible (bus && ! insertOpen);
        insertEditor->setVisible (insertOpen);
        processing->setVisible (! insertOpen);

        if (insertOpen)
            insertEditor->refresh();

        if (bus)
            groupPad->setRoot (root);

        const bool feedsMatrices = (bus || root.startsWith ("/main/")) && ! insertOpen;
        matrixHeading.setVisible (feedsMatrices);
        applyingMatrix = true;

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            matrixName[(size_t) matrix].setVisible (feedsMatrices);
            matrixName[(size_t) matrix].setText (model.getMatrixName (matrix), juce::dontSendNotification);
            matrixLevel[(size_t) matrix].setVisible (feedsMatrices);
            matrixPre[(size_t) matrix].setVisible (feedsMatrices && bus);

            if (! feedsMatrices)
                continue;

            if (! matrixLevel[(size_t) matrix].isMouseButtonDown())
            {
                float level = 0.0f;
                model.getFloat (matrixSendPath (matrix) + "/level", level);
                matrixLevel[(size_t) matrix].setValue (level, juce::dontSendNotification);
            }

            if (bus && ! matrixPre[(size_t) matrix].isMouseButtonDown())
            {
                int pre = 0;
                model.getInt (matrixSendPath (matrix) + "/tap", pre);
                matrixPre[(size_t) matrix].setToggleState (pre != 0, juce::dontSendNotification);
            }
        }

        applyingMatrix = false;
        processing->refresh();
    }

    void paint (juce::Graphics& g) override
    {
        int stored = 6;
        model.getInt (root + "/config/color", stored);
        const auto colour = stripColourFrom (stored);
        auto bounds = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (panel());
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (colour);
        g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 8.0f);
        g.drawRoundedRectangle (bounds, 8.0f, 3.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 10);
        auto header = area.removeFromTop (32);
        title.setBounds (header.removeFromLeft (160));
        insertButton.setBounds (header.removeFromLeft (72).reduced (4, 2));
        centerTarget.setBounds (header.removeFromRight (52).reduced (3, 2));
        stereoTarget.setBounds (header.removeFromRight (52).reduced (3, 2));
        stripTarget.setBounds (header.removeFromRight (120).reduced (3, 2));

        const bool bus = root.startsWith ("/bus/");
        const bool feedsMatrices = (bus || root.startsWith ("/main/")) && ! insertOpen;

        if (insertOpen)
        {
            insertEditor->setBounds (area.reduced (4, 4));
            return;
        }

        if (bus)
            groupPad->setBounds (area.removeFromRight (juce::jmin (220, area.getWidth() / 4)).reduced (4, 0));

        if (feedsMatrices)
        {
            auto column = area.removeFromRight (juce::jmin (250, area.getWidth() / 3)).reduced (4, 0);
            matrixHeading.setBounds (column.removeFromTop (16));
            const int rowHeight = juce::jmax (1, column.getHeight() / numMatrices);

            for (int matrix = 0; matrix < numMatrices; ++matrix)
            {
                auto row = column.removeFromTop (rowHeight).reduced (0, 1);
                matrixName[(size_t) matrix].setBounds (row.removeFromLeft (72));

                if (bus)
                    matrixPre[(size_t) matrix].setBounds (row.removeFromRight (44).reduced (1, 2));

                matrixLevel[(size_t) matrix].setBounds (row);
            }
        }

        eqOn.setVisible (false);
        compOn.setVisible (false);

        for (auto& slider : bandGain)
            slider.setVisible (false);

        threshold.setVisible (false);
        ratio.setVisible (false);
        makeup.setVisible (false);
        processing->setBounds (area.reduced (4, 4));
    }

private:
    juce::String matrixSendPath (int matrix) const
    {
        return root + "/mix/" + juce::String (matrix + 1).paddedLeft ('0', 2);
    }

    void setRoot (const juce::String& newRoot)
    {
        root = newRoot;
        title.setEditable (false, root.startsWith ("/bus/") || root.startsWith ("/mtx/"), false);
        processing->setRoot (root);
        insertEditor->setRoot (root);
        refresh();
        resized();
    }

    ConsoleModel& model;
    juce::String root;
    juce::String stripRoot;
    juce::String stripTitle;
    juce::Label title;
    juce::TextButton stripTarget;
    juce::TextButton stereoTarget { "LR" };
    juce::TextButton centerTarget { "C" };
    juce::TextButton eqOn { "EQ" };
    juce::TextButton compOn { "Comp" };
    std::array<juce::Slider, numEqBands> bandGain;
    juce::Slider threshold;
    juce::Slider ratio;
    juce::Slider makeup;
    std::unique_ptr<GroupPad> groupPad;
    std::unique_ptr<BusProcessingView> processing;
    juce::TextButton insertButton { "Insert" };
    std::unique_ptr<InsertEditor> insertEditor;
    bool insertOpen = false;
    juce::Label matrixHeading;
    std::array<juce::Label, numMatrices> matrixName;
    std::array<juce::Slider, numMatrices> matrixLevel;
    std::array<juce::TextButton, numMatrices> matrixPre;
    bool applyingMatrix = false;
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

    title.setText ("Virtual Console " + juce::String (JUCE_APPLICATION_VERSION_STRING), juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (18.0f)));
    title.setColour (juce::Label::textColourId, text());
    addAndMakeVisible (title);

    sourcesButton.setClickingTogglesState (true);
    sourcesButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    sourcesButton.onClick = [this]
    {
        sourcesOpen = sourcesButton.getToggleState();

        if (sourcesOpen)
        {
            routingOpen = false;
            routingButton.setToggleState (false, juce::dontSendNotification);
        }

        resized();
    };
    addAndMakeVisible (sourcesButton);
    sourcesButton.setTooltip ("Show the source list above the faders");

    routingButton.setClickingTogglesState (true);
    routingButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6f8f));
    routingButton.onClick = [this]
    {
        routingOpen = routingButton.getToggleState();

        if (routingOpen)
        {
            sourcesOpen = false;
            sourcesButton.setToggleState (false, juce::dontSendNotification);
        }

        resized();
    };
    addAndMakeVisible (routingButton);
    routingButton.setTooltip ("Patch inputs, outputs, and stageboxes");

    for (auto* label : { &outputLabel, &listenLabel, &mainLabel, &centerLabel, &muteHeading })
    {
        label->setColour (juce::Label::textColourId, dim());
        label->setFont (juce::Font (juce::FontOptions (12.0f)));
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }

    mainLabel.setJustificationType (juce::Justification::centred);
    mainLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    mainLabel.setColour (juce::Label::textColourId, text());
    muteHeading.setJustificationType (juce::Justification::centred);
    muteHeading.setColour (juce::Label::textColourId, text());

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
    mixBox.setTooltip ("LR plays local outputs 1 and 2. A bus replaces both speakers.");
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
    playButton.setTooltip ("Play or pause the sources in the mix");
    addAndMakeVisible (playButton);

    restartButton.onClick = [this]
    {
        engine.restart();
        syncTransport();
    };
    restartButton.setTooltip ("Return the sources to the start");
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
    timeline.setTooltip ("Move through the sources");
    addAndMakeVisible (timeline);

    timeLabel.setColour (juce::Label::textColourId, dim());
    timeLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeLabel);
    playbackHeading.setColour (juce::Label::textColourId, text());
    playbackHeading.setFont (juce::Font (juce::FontOptions (13.0f)));
    playbackHeading.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (playbackHeading);

    recordButton.onClick = [this] { beginRecording(); };
    recordButton.setTooltip ("Record the mix outputs you have chosen");
    addAndMakeVisible (recordButton);
    recordChoose.onClick = [this] { showRecordChoices(); };
    recordChoose.setTooltip ("Choose which post-fader mix outputs to record");
    addAndMakeVisible (recordChoose);
    recordSummary.setColour (juce::Label::textColourId, text());
    recordSummary.setFont (juce::Font (juce::FontOptions (13.0f)));
    recordSummary.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (recordSummary);
    recordTime.setColour (juce::Label::textColourId, dim());
    recordTime.setFont (juce::Font (juce::FontOptions (13.0f)));
    recordTime.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (recordTime);

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

    routingBoard = std::make_unique<RoutingBoard> (model);
    routingView.setViewedComponent (routingBoard.get(), false);
    routingView.setScrollBarsShown (true, true);
    addChildComponent (routingView);

    faderBay = std::make_unique<FaderBay> (model, engine);
    faderBay->onChannel = [this] (int channel) { selectChannel (channel); };
    faderBay->onBus = [this] (int bus) { selectBus (bus); };
    faderBay->onBusSel = [this] (int bus) { selectBus (bus); };
    faderBay->onMatrix = [this] (int matrix) { selectMatrix (matrix); };
    faderBay->onMatrixSel = [this] (int matrix)
    {
        showOutputPage (3);
        selectMatrix (matrix);
    };
    faderBay->onDcaSel = [this] (int dca)
    {
        if (dcaFilter == dca)
        {
            showInputPage (inputPage);
            return;
        }

        dcaFilter = dca;
        faderBay->showDcaMembers (dca);
        syncEffectBuses();
        applySendMode();
        selectSpare (3, dca);
        resized();
    };
    faderBay->onSpare = [this] (int kind, int index) { selectSpare (kind, index); };
    viewport.setViewedComponent (faderBay.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    outputRow = std::make_unique<OutputRow>();
    addAndMakeVisible (*outputRow);

    channelEditor = std::make_unique<ChannelEditor> (model);
    channelEditor->onChannelChanged = [this] (int channel) { selectChannel (channel); };
    addAndMakeVisible (channelEditor.get());

    groupEditor = std::make_unique<GroupEditor> (model);
    addChildComponent (groupEditor.get());
    matrixEditor = std::make_unique<MatrixEditor> (model);
    addChildComponent (matrixEditor.get());
    spareTitle.setJustificationType (juce::Justification::centred);
    spareTitle.setFont (juce::Font (juce::FontOptions (18.0f)));
    spareTitle.setColour (juce::Label::textColourId, text());
    addChildComponent (spareTitle);
    fxEditor = std::make_unique<FxEditor> (model);
    addChildComponent (*fxEditor);

    for (int i = 0; i < 6; ++i)
    {
        inputBankButton[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
        inputBankButton[i].setColour (juce::TextButton::textColourOffId, text());
        const int page = i;
        inputBankButton[i].onClick = [this, page] { showInputPage (page); };
        addAndMakeVisible (inputBankButton[i]);
    }

    widthButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
    widthButton.setColour (juce::TextButton::textColourOffId, text());
    widthButton.setTooltip ("Choose 8 or 16 input faders. 8 uses more bank buttons.");
    widthButton.onClick = [this]
    {
        inputWidth = inputWidth == 16 ? 8 : 16;
        showInputPage (0);
    };
    addAndMakeVisible (widthButton);

    sendsButton.setClickingTogglesState (true);
    sendsButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b040));
    sendsButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c2128));
    sendsButton.setTooltip ("Input faders become the sends to the selected bus. Each send keeps the tap chosen on that channel.");
    sendsButton.onClick = [this]
    {
        if (sendsButton.getToggleState() && armedBus < 0)
            selectBus (0);
        else
            applySendMode();
    };
    addAndMakeVisible (sendsButton);

    const char* outputNames[] = { "DCA", "BUS 1-8", "BUS 9-16", "Matrix", "Main" };

    for (int i = 0; i < 5; ++i)
    {
        outputBankButton[i].setButtonText (outputNames[i]);
        outputBankButton[i].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232830));
        outputBankButton[i].setColour (juce::TextButton::textColourOffId, text());
        const int page = i;
        outputBankButton[i].onClick = [this, page] { showOutputPage (page); };
        addAndMakeVisible (outputBankButton[i]);
    }

    outputBankButton[0].setTooltip ("DCA 1 to 8");
    outputBankButton[1].setTooltip ("Mix buses 1 to 8");
    outputBankButton[2].setTooltip ("Mix buses 9 to 16");
    outputBankButton[3].setTooltip ("Matrices 1 to 6, with the center bus");
    outputBankButton[4].setTooltip ("Main output and mute groups");

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
    styleSelect (mainSelect, "Edit the main output");
    mainSelect.onClick = [this]
    {
        showOutputPage (4);
        topEditor = 1;
        closeTopPages();
        groupEditor->showStrip ("/main/st", "Main");
        resized();
    };
    addAndMakeVisible (mainSelect);

    centerLabel.setJustificationType (juce::Justification::centred);
    centerLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    centerLabel.setColour (juce::Label::textColourId, text());

    centerFader.setSliderStyle (juce::Slider::LinearVertical);
    centerFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    centerFader.setRange (0.0, 1.0, 0.0);
    centerFader.setScrollWheelEnabled (false);
    centerFader.setDoubleClickReturnValue (true, 0.75);
    centerFader.textFromValueFunction = [] (double value) { return decibelText (faderToDb ((float) value)); };
    centerFader.valueFromTextFunction = [] (const juce::String& text)
    {
        if (text.containsIgnoreCase ("inf"))
            return 0.0;

        return (double) dbToFader (text.getFloatValue());
    };
    float centerValue = 0.75f;
    model.getFloat ("/main/m/mix/fader", centerValue);
    centerFader.setValue (centerValue, juce::dontSendNotification);
    centerFader.onValueChange = [this] { model.setFloat ("/main/m/mix/fader", (float) centerFader.getValue()); };
    centerFader.setTooltip ("Center bus level");
    addAndMakeVisible (centerFader);
    addAndMakeVisible (centerScale);

    centerMute.setClickingTogglesState (true);
    centerMute.setColour (juce::TextButton::buttonOnColourId, muteOn());
    centerMute.setTooltip ("Mute the center bus");
    int centerOn = 1;
    model.getInt ("/main/m/mix/on", centerOn);
    centerMute.setToggleState (centerOn == 0, juce::dontSendNotification);
    centerMute.onClick = [this] { model.setInt ("/main/m/mix/on", centerMute.getToggleState() ? 0 : 1); };
    addAndMakeVisible (centerMute);

    centerStereo.setClickingTogglesState (true);
    centerStereo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d8f5a));
    centerStereo.setTooltip ("Fold the center bus into the main stereo output");
    int fold = 0;
    model.getInt ("/main/m/mix/st", fold);
    centerStereo.setToggleState (fold != 0, juce::dontSendNotification);
    centerStereo.onClick = [this] { model.setInt ("/main/m/mix/st", centerStereo.getToggleState() ? 1 : 0); };
    addAndMakeVisible (centerStereo);
    styleSelect (centerSelect, "Edit the center bus");
    centerSelect.onClick = [this]
    {
        showOutputPage (3);
        topEditor = 1;
        closeTopPages();
        groupEditor->showStrip ("/main/m", "Center");
        resized();
    };
    addAndMakeVisible (centerSelect);

    for (int group = 0; group < numMuteGroups; ++group)
    {
        auto& button = muteGroupButton[group];
        button.setButtonText (juce::String (group + 1));
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, muteOn());
        button.setTooltip ("Mute group " + juce::String (group + 1));
        button.onClick = [this, group]
        {
            model.setInt ("/config/mute/" + juce::String (group + 1).paddedLeft ('0', 2),
                          muteGroupButton[group].getToggleState() ? 1 : 0);
        };
        addAndMakeVisible (button);
    }

    status.setColour (juce::Label::textColourId, dim());
    status.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (status);

    const auto deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
    player.setSource (&engine);
    deviceManager.addAudioCallback (&player);
    refreshOutputs();
    refreshSources();
    selectChannel (0);
    showOutputPage (4);
    status.setText (deviceError.isNotEmpty() ? deviceError : statusText(), juce::dontSendNotification);
    startTimerHz (30);
    setSize (1680, 860);
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
    g.fillRect (0, 0, getWidth(), 42);
    g.setColour (juce::Colour (0xff2c333b));
    g.fillRect (0, 41, getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (42).reduced (10, 6);
    title.setBounds (top.removeFromLeft (210));
    top.removeFromLeft (8);
    sourcesButton.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (6);
    routingButton.setBounds (top.removeFromLeft (84));
    top.removeFromLeft (6);
    addButton.setBounds (top.removeFromRight (110).reduced (4, 0));
    listenBox.setBounds (top.removeFromRight (110).reduced (4, 0));
    listenLabel.setBounds (top.removeFromRight (48));
    setupButton.setBounds (top.removeFromRight (80).reduced (4, 0));
    outputBox.setBounds (top.removeFromRight (168).reduced (4, 0));
    outputLabel.setBounds (top.removeFromRight (56));
    mixBox.setBounds (top.removeFromRight (96).reduced (4, 0));
    mixLabel.setBounds (top.removeFromRight (36));

    routingView.setVisible (routingOpen);

    if (routingOpen)
    {
        sourceHeading.setVisible (false);
        emptySources.setVisible (false);
        sourceList.setVisible (false);
        channelEditor->setVisible (false);
        groupEditor->setVisible (false);
        matrixEditor->setVisible (false);
        spareTitle.setVisible (false);
        fxEditor->setVisible (false);
        viewport.setVisible (false);
        mainFader.setVisible (false);
        mainMeter.setVisible (false);
        mainScale.setVisible (false);
        centerFader.setVisible (false);
        centerScale.setVisible (false);
        mainMute.setVisible (false);
        centerMute.setVisible (false);
        centerStereo.setVisible (false);
        mainLabel.setVisible (false);
        centerLabel.setVisible (false);
        mainSelect.setVisible (false);
        centerSelect.setVisible (false);
        muteHeading.setVisible (false);
        playButton.setVisible (false);
        restartButton.setVisible (false);
        timeline.setVisible (false);
        timeLabel.setVisible (false);
        playbackHeading.setVisible (false);
        recordButton.setVisible (false);
        recordChoose.setVisible (false);
        recordSummary.setVisible (false);
        recordTime.setVisible (false);
        sendsButton.setVisible (false);
        widthButton.setVisible (false);
        outputRow->setVisible (false);

        for (auto& button : inputBankButton)
            button.setVisible (false);

        for (auto& button : outputBankButton)
            button.setVisible (false);

        for (auto& button : muteGroupButton)
            button.setVisible (false);

        status.setBounds (area.removeFromBottom (18).reduced (10, 0));
        routingView.setBounds (area.reduced (8, 4));
        routingBoard->setSize (std::max (640, routingView.getMaximumVisibleWidth()),
                               std::max (240, routingView.getMaximumVisibleHeight()));
        return;
    }

    viewport.setVisible (true);
    playButton.setVisible (true);
    restartButton.setVisible (true);
    timeline.setVisible (true);
    timeLabel.setVisible (true);
    playbackHeading.setVisible (true);
    recordButton.setVisible (true);
    recordChoose.setVisible (true);
    recordSummary.setVisible (true);
    recordTime.setVisible (true);
    sendsButton.setVisible (true);
    widthButton.setVisible (true);

    for (auto& button : inputBankButton)
        button.setVisible (true);

    for (auto& button : outputBankButton)
        button.setVisible (true);

    auto topHalf = area.removeFromTop (area.getHeight() / 2);
    sourceHeading.setVisible (sourcesOpen);
    emptySources.setVisible (sourcesOpen && model.getNumSources() == 0);
    sourceList.setVisible (sourcesOpen && model.getNumSources() > 0);
    const bool showChannel = ! sourcesOpen && topEditor == 0;
    const bool showBus = ! sourcesOpen && topEditor == 1;
    const bool showMatrix = ! sourcesOpen && topEditor == 2;
    const bool showSpare = ! sourcesOpen && topEditor == 3;
    channelEditor->setVisible (showChannel);
    groupEditor->setVisible (showBus);
    matrixEditor->setVisible (showMatrix);
    spareTitle.setVisible (showSpare);
    fxEditor->setVisible (false);

    if (sourcesOpen)
    {
        auto pageArea = topHalf.reduced (16, 8);
        sourceHeading.setBounds (pageArea.removeFromTop (32));
        emptySources.setBounds (pageArea.removeFromTop (40));
        sourceList.setBounds (pageArea);
    }
    else if (showBus)
    {
        groupEditor->setBounds (topHalf.reduced (8, 6));
    }
    else if (showMatrix)
    {
        matrixEditor->setBounds (topHalf.reduced (8, 6));
    }
    else if (showSpare)
    {
        const bool effect = spareKind == 2;
        spareTitle.setVisible (! effect);
        fxEditor->setVisible (effect);

        if (effect)
            fxEditor->setBounds (topHalf.reduced (8, 6));
        else
            spareTitle.setBounds (topHalf.reduced (24, 24));
    }
    else
    {
        channelEditor->setBounds (topHalf.reduced (8, 6));
    }

    status.setBounds (area.removeFromBottom (18).reduced (10, 0));
    auto inputBanks = area.removeFromLeft (92);
    const int outputStrips = outputPage == 4 ? 1 : outputPage == 3 ? 7 : 8;
    const int outputStripWidth = outputPage == 4 ? 148 : 88;
    const int outputWidth = juce::jmin (outputStrips * outputStripWidth, juce::jmax (outputStrips * 72, (area.getWidth() - 64 - 108) / 2));
    auto rightSide = area.removeFromRight (64 + outputWidth + 108);
    auto deck = rightSide.removeFromTop (100).reduced (6, 4);
    auto playbackRow = deck.removeFromTop (28);
    playButton.setBounds (playbackRow.removeFromLeft (32));
    playbackRow.removeFromLeft (4);
    restartButton.setBounds (playbackRow.removeFromLeft (32));
    playbackRow.removeFromLeft (8);
    playbackHeading.setBounds (playbackRow.removeFromLeft (78));
    timeLabel.setBounds (playbackRow.removeFromRight (juce::jmin (118, playbackRow.getWidth())));
    deck.removeFromTop (2);
    timeline.setBounds (deck.removeFromTop (18));
    deck.removeFromTop (6);
    auto recordRow = deck.removeFromTop (28);
    recordButton.setBounds (recordRow.removeFromLeft (32));
    recordRow.removeFromLeft (4);
    recordChoose.setBounds (recordRow.removeFromLeft (76));
    recordRow.removeFromLeft (6);
    recordTime.setBounds (recordRow.removeFromRight (juce::jmin (56, recordRow.getWidth())));
    recordSummary.setBounds (recordRow);
    auto muteColumn = rightSide.removeFromRight (64);
    auto outputChannels = rightSide.removeFromRight (outputWidth).reduced (2, 4);
    auto outputBanks = rightSide.removeFromRight (108);
    const bool showMain = outputPage == 4;
    const bool showCenter = outputPage == 3;
    mainFader.setVisible (showMain);
    mainMeter.setVisible (showMain);
    mainScale.setVisible (showMain);
    mainMute.setVisible (showMain);
    mainLabel.setVisible (showMain);
    mainSelect.setVisible (showMain);
    muteHeading.setVisible (true);
    centerFader.setVisible (showCenter);
    centerScale.setVisible (showCenter);
    centerMute.setVisible (showCenter);
    centerStereo.setVisible (showCenter);
    centerLabel.setVisible (showCenter);
    centerSelect.setVisible (showCenter);
    outputRow->setVisible (! showMain);

    for (auto& button : muteGroupButton)
        button.setVisible (true);

    const int inputPages = inputPageCount();
    auto widthSlot = inputBanks.removeFromBottom (36);
    widthButton.setBounds (widthSlot.reduced (4, 4));
    widthButton.setButtonText (inputWidth == 16 ? "16" : "8");
    const int inputHeight = juce::jmax (1, inputBanks.getHeight() / inputPages);
    const char* wideNames[] = { "1-16", "17-32", "Aux FX", "Effects" };
    const char* narrowNames[] = { "1-8", "9-16", "17-24", "25-32", "Aux", "Effects" };

    for (int i = 0; i < 6; ++i)
    {
        const bool shown = i < inputPages;
        inputBankButton[i].setVisible (shown);

        if (! shown)
            continue;

        inputBankButton[i].setButtonText (inputWidth == 16 ? wideNames[i] : narrowNames[i]);
        const bool effectsButton = (inputWidth == 16 && i == 3) || (inputWidth == 8 && i == 5);
        inputBankButton[i].setTooltip (effectsButton
            ? "FX returns, then buses 13 to 16: Vintage Room, Hall, Stereo Delay, and Stereo Chorus."
            : juce::String());
        inputBankButton[i].setBounds (inputBanks.removeFromTop (inputHeight).reduced (4, 3));
        const bool on = dcaFilter < 0 && inputPage == i;
        inputBankButton[i].setColour (juce::TextButton::buttonColourId, on ? juce::Colour (0xff3d6f8f) : juce::Colour (0xff232830));
    }

    sendsButton.setBounds (outputBanks.removeFromTop (44).reduced (4, 4));
    const int outputHeight = juce::jmax (1, outputBanks.getHeight() / 5);

    for (int i = 0; i < 5; ++i)
    {
        outputBankButton[i].setBounds (outputBanks.removeFromTop (outputHeight).reduced (4, 3));
        const bool on = outputPage == i;
        outputBankButton[i].setColour (juce::TextButton::buttonColourId, on ? juce::Colour (0xff3d6f8f) : juce::Colour (0xff232830));
    }

    viewport.setBounds (area.reduced (2, 4));
    const int stripHeight = std::max (0, viewport.getHeight() - viewport.getScrollBarThickness());

    if (viewport.getViewedComponent() != faderBay.get())
        viewport.setViewedComponent (faderBay.get(), false);

    faderBay->setSize (faderBay->widthForBank(), stripHeight);

    muteHeading.setBounds (muteColumn.removeFromTop (18));

    for (int group = 0; group < numMuteGroups; ++group)
        muteGroupButton[group].setBounds (muteColumn.removeFromTop (muteColumn.getHeight() / std::max (1, numMuteGroups - group)).reduced (4, 2));

    if (showMain)
    {
        auto buttons = outputChannels.removeFromBottom (26);
        mainSelect.setBounds (buttons.removeFromLeft (44).reduced (2, 0));
        mainMute.setBounds (buttons.reduced (4, 0));
        mainLabel.setBounds (outputChannels.removeFromTop (18));
        mainScale.setBounds (outputChannels.removeFromLeft (28));
        mainMeter.setBounds (outputChannels.removeFromRight (10).reduced (0, 6));
        mainFader.setBounds (outputChannels.reduced (1, 0));
    }
    else if (showCenter)
    {
        auto centerColumn = outputChannels.removeFromRight (juce::jmax (72, outputChannels.getWidth() / 7));
        outputRow->setBounds (outputChannels);
        centerLabel.setBounds (centerColumn.removeFromTop (18));
        auto buttons = centerColumn.removeFromBottom (48);
        centerSelect.setBounds (buttons.removeFromTop (22).reduced (4, 1));
        auto lower = buttons;
        centerMute.setBounds (lower.removeFromLeft (lower.getWidth() / 2).reduced (2, 1));
        centerStereo.setBounds (lower.reduced (2, 1));
        centerScale.setBounds (centerColumn.removeFromLeft (24));
        centerFader.setBounds (centerColumn.reduced (1, 0));
    }
    else
    {
        outputRow->setBounds (outputChannels);
    }
}

int MainComponent::inputPageCount() const
{
    return inputWidth == 16 ? 4 : 6;
}

bool MainComponent::effectsPage() const
{
    if (dcaFilter >= 0)
        return false;

    return (inputWidth == 16 && inputPage == 3) || (inputWidth == 8 && inputPage == 5);
}

void MainComponent::syncEffectBuses()
{
    if (outputPage != 2 || outputRow == nullptr || faderBay == nullptr)
        return;

    faderBay->showBuses (*outputRow, 8, effectsPage() ? 4 : 8);
}

void MainComponent::showInputPage (int page)
{
    dcaFilter = -1;
    inputPage = juce::jlimit (0, inputPageCount() - 1, page);

    if (inputWidth == 16)
    {
        if (inputPage == 0)
            faderBay->showChannelRange (0, 16);
        else if (inputPage == 1)
            faderBay->showChannelRange (16, 16);
        else if (inputPage == 2)
            faderBay->showReturns (true, true, true);
        else
            faderBay->showEffects();
    }
    else if (inputPage < 4)
    {
        faderBay->showChannelRange (inputPage * 8, 8);
    }
    else if (inputPage == 4)
    {
        faderBay->showReturns (true, true, false);
    }
    else
    {
        faderBay->showEffects();
    }

    syncEffectBuses();
    applySendMode();

    const bool channelPage = inputWidth == 16 ? inputPage < 2 : inputPage < 4;

    if (channelPage)
    {
        const int first = inputWidth == 16 ? inputPage * 16 : inputPage * 8;
        const int last = first + inputWidth - 1;

        if (selectedChannel < first || selectedChannel > last)
            selectedChannel = first;

        selectChannel (selectedChannel);
    }
    else if (effectsPage())
    {
        selectSpare (2, 0);
    }
    else
    {
        selectSpare (0, 0);
    }

    resized();
}

void MainComponent::showOutputPage (int page)
{
    outputPage = juce::jlimit (0, 4, page);

    if (outputPage == 0)
        faderBay->showDcas (*outputRow);
    else if (outputPage == 1)
        faderBay->showBuses (*outputRow, 0, 8);
    else if (outputPage == 2)
        faderBay->showBuses (*outputRow, 8, effectsPage() ? 4 : 8);
    else if (outputPage == 3)
        faderBay->showMatrices (*outputRow);
    else
        faderBay->clearOutputs (*outputRow);

    resized();
}

void MainComponent::applySendMode()
{
    faderBay->setSendBus (sendsButton.getToggleState() ? armedBus : -1);
    faderBay->setHighlightBus (armedBus);
}

void MainComponent::selectSpare (int kind, int index)
{
    const bool changed = topEditor != 3 || sourcesOpen || routingOpen;
    topEditor = 3;
    spareKind = kind;
    spareIndex = index;
    closeTopPages();
    faderBay->setSelectedSpare (kind, index);

    juce::String name;

    if (kind == 0)
        name = model.getAuxName (index);
    else if (kind == 1)
        name = model.getUsbName (index);
    else if (kind == 2)
        name = model.getFxReturnName (index);
    else
        name = model.getDcaName (index);

    if (kind == 2)
    {
        fxEditor->setReturn (index);
        const int slot = index / 2;

        if (slot < 4)
        {
            faderBay->highlightBusStrip (12 + slot);
            armedBus = 12 + slot;
            applySendMode();
        }
    }
    else if (kind == 0)
        spareTitle.setText (name + " is on the fader bank. Aux playback is not mixed yet. Stereo and Mono are stored for when it is.", juce::dontSendNotification);
    else if (kind == 1)
        spareTitle.setText (name + " is on the fader bank. USB playback is not mixed yet. Stereo and Mono are stored for when it is.", juce::dontSendNotification);
    else
        spareTitle.setText (name + " is on the fader bank.", juce::dontSendNotification);

    if (changed)
        resized();
}

void MainComponent::selectBus (int busIndex)
{
    if (effectsPage() && busIndex >= 12)
    {
        selectSpare (2, (busIndex - 12) * 2);
        return;
    }

    const bool changed = topEditor != 1 || sourcesOpen || routingOpen;
    topEditor = 1;
    closeTopPages();
    selectedBus = juce::jlimit (0, numBuses - 1, busIndex);
    armedBus = selectedBus;
    const int busPage = selectedBus < 8 ? 1 : 2;

    if (outputPage != busPage)
        showOutputPage (busPage);

    faderBay->setSelectedBus (selectedBus);
    applySendMode();
    groupEditor->showStrip ("/bus/" + juce::String (selectedBus + 1).paddedLeft ('0', 2),
                            model.getBusName (selectedBus));

    if (changed || outputPage == busPage)
        resized();
}

void MainComponent::selectMatrix (int matrixIndex)
{
    const bool changed = topEditor != 2 || sourcesOpen || routingOpen;
    topEditor = 2;
    closeTopPages();
    selectedMatrix = juce::jlimit (0, numMatrices - 1, matrixIndex);

    if (outputPage != 3)
        showOutputPage (3);

    faderBay->setSelectedMatrix (selectedMatrix);
    matrixEditor->setMatrix (selectedMatrix);

    if (changed)
        resized();
}

void MainComponent::selectChannel (int channelIndex)
{
    const bool changed = topEditor != 0 || sourcesOpen || routingOpen;
    topEditor = 0;
    selectedChannel = juce::jlimit (0, numChannels - 1, channelIndex);
    closeTopPages();

    if (dcaFilter < 0)
    {
        const bool channelPage = inputWidth == 16 ? inputPage < 2 : inputPage < 4;

        if (channelPage)
        {
            const int needed = inputWidth == 16 ? (selectedChannel >= 16 ? 1 : 0) : selectedChannel / 8;

            if (inputPage != needed)
            {
                inputPage = needed;
                faderBay->showChannelRange (inputWidth == 16 ? needed * 16 : needed * 8, inputWidth);
                applySendMode();
            }
        }
    }

    faderBay->setSelectedChannel (selectedChannel);
    channelEditor->setChannel (selectedChannel);
    const int x = faderBay->xFor (faderBay->channelStrip (selectedChannel));
    const auto view = viewport.getViewArea();

    if (x < view.getX() || x + FaderBay::stripWidth > view.getRight())
        viewport.setViewPosition (std::max (0, x - FaderBay::stripWidth), 0);

    if (changed)
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
    faderBay->refresh();

    for (int group = 0; group < numMuteGroups; ++group)
    {
        if (muteGroupButton[group].isMouseButtonDown())
            continue;

        int enabled = 0;
        model.getInt ("/config/mute/" + juce::String (group + 1).paddedLeft ('0', 2), enabled);
        muteGroupButton[group].setToggleState (enabled != 0, juce::dontSendNotification);
    }
    channelEditor->refresh();
    groupEditor->refresh();
    matrixEditor->refresh();

    if (routingOpen && routingBoard != nullptr)
        routingBoard->refresh();

    if (fxEditor != nullptr && fxEditor->isVisible())
        fxEditor->refresh();
}

void MainComponent::closeTopPages()
{
    sourcesOpen = false;
    routingOpen = false;
    sourcesButton.setToggleState (false, juce::dontSendNotification);
    routingButton.setToggleState (false, juce::dontSendNotification);
}

class RecordChoices : public juce::Component
{
public:
    RecordChoices(ConsoleModel& model, MixRecorder& recorderToUse, std::function<void()> changed)
        : recorder(recorderToUse),
          onChange(std::move(changed))
    {
        hint.setText("Each ticked output is saved as its own WAV, after the fader.", juce::dontSendNotification);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff8b98a3));
        hint.setFont(juce::Font(juce::FontOptions(12.0f)));
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        auto add = [this](const juce::String& name, int target)
        {
            auto button = std::make_unique<juce::ToggleButton>(name);
            button->setToggleState(recorder.targetEnabled(target), juce::dontSendNotification);
            button->setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd7e2ea));
            button->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffe15b5b));
            button->onClick = [this, target, raw = button.get()]
            {
                recorder.setTarget(target, raw->getToggleState());

                if (onChange != nullptr)
                    onChange();
            };
            list.addAndMakeVisible(*button);
            toggles.push_back(std::move(button));
        };

        add("Main", recordMain);
        add("Center", recordCenter);

        for (int bus = 0; bus < numBuses; ++bus)
            add(model.getBusName(bus), recordBus(bus));

        for (int matrix = 0; matrix < numMatrices; ++matrix)
            add(model.getMatrixName(matrix), recordMatrix(matrix));

        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        setSize(280, 420);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1f24));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 6);
        hint.setBounds(area.removeFromTop(36));
        viewport.setBounds(area);
        const int rowHeight = 24;
        list.setSize(std::max(40, viewport.getMaximumVisibleWidth()), (int) toggles.size() * rowHeight);
        auto rows = list.getLocalBounds();

        for (auto& toggle : toggles)
            toggle->setBounds(rows.removeFromTop(rowHeight));
    }

private:
    MixRecorder& recorder;
    std::function<void()> onChange;
    juce::Label hint;
    juce::Component list;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
};

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
    recordButton.setRecording (engine.recorder().isRecording());
    recordChoose.setEnabled (! engine.recorder().isRecording());
    recordSummary.setText (recordingSummary(), juce::dontSendNotification);
    recordTime.setText (formatTime (engine.recorder().secondsRecorded()), juce::dontSendNotification);
}

void MainComponent::showRecordChoices()
{
    if (engine.recorder().isRecording())
        return;

    auto choices = std::make_unique<RecordChoices> (model, engine.recorder(), [this]
    {
        recordSummary.setText (recordingSummary(), juce::dontSendNotification);
    });
    juce::CallOutBox::launchAsynchronously (std::move (choices), recordChoose.getScreenBounds(), nullptr);
}

void MainComponent::refreshRecordNames()
{
    auto& recorder = engine.recorder();
    recorder.setTargetName (recordMain, "Main");
    recorder.setTargetName (recordCenter, "Center");

    for (int bus = 0; bus < numBuses; ++bus)
        recorder.setTargetName (recordBus (bus), model.getBusName (bus));

    for (int matrix = 0; matrix < numMatrices; ++matrix)
        recorder.setTargetName (recordMatrix (matrix), model.getMatrixName (matrix));
}

juce::String MainComponent::recordingSummary() const
{
    const auto& recorder = engine.recorder();
    juce::StringArray names;

    if (recorder.targetEnabled (recordMain))
        names.add ("Main");

    if (recorder.targetEnabled (recordCenter))
        names.add ("Center");

    for (int bus = 0; bus < numBuses; ++bus)
        if (recorder.targetEnabled (recordBus (bus)))
            names.add (model.getBusName (bus));

    for (int matrix = 0; matrix < numMatrices; ++matrix)
        if (recorder.targetEnabled (recordMatrix (matrix)))
            names.add (model.getMatrixName (matrix));

    if (names.isEmpty())
        return "Choose the mix";

    if (names.size() == 1)
        return names[0];

    return names[0] + " + " + juce::String (names.size() - 1);
}

void MainComponent::beginRecording()
{
    if (engine.recorder().isRecording())
    {
        engine.recorder().stop();
        syncTransport();
        return;
    }

    if (engine.recorder().enabledCount() == 0)
    {
        showRecordChoices();
        return;
    }

    refreshRecordNames();
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the recording",
                                                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    constexpr auto browserFlags = juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectDirectories;
    chooser->launchAsync (browserFlags, [this] (const juce::FileChooser& box)
    {
        const auto folder = box.getResult();

        if (! folder.isDirectory() || engine.recorder().isRecording())
            return;

        refreshRecordNames();
        engine.recorder().start (folder);
        syncTransport();
    });
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

    faderBay->refresh();
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

    if (engine.recorder().isRecording())
        parts.add ("Recording " + recordingSummary());

    const auto recordError = engine.recorder().lastError();

    if (recordError.isNotEmpty())
        parts.add (recordError);

    if (engine.recorder().overflowed())
        parts.add ("The recording dropped audio");

    return parts.joinIntoString ("   ");
}

} // namespace vc
