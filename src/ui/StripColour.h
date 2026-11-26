#pragma once

#include "model/ConsoleModel.h"

#include <algorithm>

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

constexpr int numStripColours = 8;

inline juce::Colour stripPalette(int index) noexcept
{
    static const juce::uint32 colours[] = {
        0xfff08a24, 0xffe23b3b, 0xff3dce4a, 0xffffe14a,
        0xff3b82f6, 0xffa855f7, 0xff7af4f8, 0xfff7f8fa
    };
    return juce::Colour(colours[index & 7]);
}

inline int stripColourIndex(int stored) noexcept
{
    return std::clamp(stored, 0, 15) & 7;
}

inline bool stripColourOutlined(int stored) noexcept
{
    return stored >= 8;
}

inline juce::Colour stripColourFrom(int stored) noexcept
{
    return stripPalette(stripColourIndex(stored));
}

inline juce::Colour stripVisible(juce::Colour colour) noexcept
{
    return colour.getPerceivedBrightness() < 0.15f ? juce::Colour(0xff9aa3ad) : colour;
}

inline juce::Colour stripIconInk(int stored) noexcept
{
    if (stripColourOutlined(stored))
        return stripVisible(stripColourFrom(stored));

    const int index = stripColourIndex(stored);
    return index == 3 || index == 6 || index == 7 ? juce::Colour(0xff14181d) : juce::Colours::white;
}

inline juce::Colour stripNameColour(int stored) noexcept
{
    return stripColourOutlined(stored) ? stripVisible(stripColourFrom(stored)) : juce::Colour(0xffe8edf2);
}

inline void paintColourIcon(juce::Graphics& g, juce::Rectangle<float> bounds, int stored)
{
    const auto colour = stripColourFrom(stored);
    const bool outlined = stripColourOutlined(stored);
    const auto edge = outlined || colour.getPerceivedBrightness() < 0.15f ? stripVisible(colour) : colour;
    g.setColour(outlined ? juce::Colour(0xff101418) : colour);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(bounds.reduced(0.6f), 4.0f, outlined ? 2.0f : 1.0f);
}

class ColourIcon : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;
    std::function<void()> onDoubleClick;

    void setStored(int next)
    {
        stored = std::clamp(next, 0, 15);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        paintColourIcon(g, getLocalBounds().toFloat().reduced(1.0f), stored);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.getNumberOfClicks() > 1)
        {
            if (onDoubleClick != nullptr)
                onDoubleClick();
        }
        else if (onClick != nullptr)
            onClick();
    }

private:
    int stored = 0;
};

class StripColourPicker : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    std::function<void(int)> onPick;
    int selected = 0;

    StripColourPicker()
    {
        setSize(columns * cell + 8, 2 * cell + 8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1c2128));

        for (int slot = 0; slot < numStripColours * 2; ++slot)
        {
            auto bounds = cellBounds(slot).toFloat().reduced(4.0f);
            paintColourIcon(g, bounds.reduced(2.0f), slot);
            if (slot == selected)
            {
                g.setColour(juce::Colour(0xffe8edf2));
                g.drawRoundedRectangle(bounds, 5.0f, 1.4f);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const int slot = slotAt(event.getPosition());

        if (slot < 0 || onPick == nullptr)
            return;

        selected = slot;
        onPick(slot);

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        static const char* names[] = { "Black", "Red", "Green", "Yellow", "Bright blue", "Purple", "Light blue", "White" };
        const int slot = slotAt(event.getPosition());

        if (slot < 0)
            setTooltip({});
        else
            setTooltip(juce::String(names[slot & 7]) + (slot >= 8 ? " outline" : " filled"));
    }

private:
    static constexpr int columns = 8;
    static constexpr int cell = 36;

    juce::Rectangle<int> cellBounds(int slot) const
    {
        return { 4 + (slot % columns) * cell, 4 + (slot / columns) * cell, cell, cell };
    }

    int slotAt(juce::Point<int> position) const
    {
        const int col = (position.x - 4) / cell;
        const int row = (position.y - 4) / cell;

        if (col < 0 || col >= columns || row < 0 || row > 1)
            return -1;

        return row * columns + col;
    }
};

inline void openStripColourPicker(juce::Component& anchor, ConsoleModel& model, const juce::String& path, std::function<void()> changed)
{
    auto picker = std::make_unique<StripColourPicker>();
    int current = 0;
    model.getInt(path, current);
    picker->selected = std::clamp(current, 0, 15);
    picker->onPick = [&model, path, changed](int colour)
    {
        model.setInt(path, colour);

        if (changed != nullptr)
            changed();
    };
    juce::CallOutBox::launchAsynchronously(std::move(picker), anchor.getScreenBounds(), nullptr);
}

} // namespace vc
