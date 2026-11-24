#pragma once

#include "model/ConsoleModel.h"
#include "ui/StripColour.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

constexpr int numChannelIcons = 36;

inline const char* channelIconName(int index)
{
    static const char* names[] = {
        "Kick", "Snare", "Tom", "Hats", "Cymbal", "Kit",
        "Congas", "Shaker", "Acoustic", "Electric", "Bass guitar", "Piano",
        "Organ", "Synth", "Keys", "Trumpet", "Sax", "Flute",
        "Violin", "Mic", "Radio mic", "Headset", "Vocal", "Duo",
        "Choir", "Laptop", "FX", "Speaker", "Headphones", "Direct box",
        "Amp", "Mixer", "Left", "Right", "Talkback", "Click"
    };

    if (index < 0 || index >= numChannelIcons)
        return "No icon";

    return names[index];
}

inline void drawChannelIcon(juce::Graphics& g, juce::Rectangle<float> area, int index, juce::Colour ink = juce::Colours::white)
{
    if (index < 0 || index >= numChannelIcons || area.getWidth() < 4.0f)
        return;

    const auto r = area.reduced(area.getWidth() * 0.08f);
    const float stroke = juce::jmax(1.15f, r.getWidth() * 0.07f);
    g.setColour(ink);

    auto X = [&](float u) { return r.getX() + u * r.getWidth(); };
    auto Y = [&](float v) { return r.getY() + v * r.getHeight(); };
    auto line = [&](float x1, float y1, float x2, float y2)
    {
        g.drawLine(X(x1), Y(y1), X(x2), Y(y2), stroke);
    };
    auto ellipse = [&](float x, float y, float w, float h)
    {
        g.drawEllipse(X(x), Y(y), w * r.getWidth(), h * r.getHeight(), stroke);
    };
    auto fillEllipse = [&](float x, float y, float w, float h)
    {
        g.fillEllipse(X(x), Y(y), w * r.getWidth(), h * r.getHeight());
    };
    auto box = [&](float x, float y, float w, float h)
    {
        g.drawRoundedRectangle(X(x), Y(y), w * r.getWidth(), h * r.getHeight(), 1.5f, stroke);
    };
    auto text = [&](const juce::String& label)
    {
        g.setFont(juce::Font(juce::FontOptions(juce::jmax(8.0f, r.getHeight() * 0.62f))).boldened());
        g.drawText(label, r, juce::Justification::centred, false);
    };

    switch (index)
    {
        case 0: ellipse(0.16f, 0.16f, 0.68f, 0.68f); fillEllipse(0.40f, 0.40f, 0.20f, 0.20f); break;
        case 1: ellipse(0.12f, 0.28f, 0.76f, 0.44f); line(0.22f, 0.50f, 0.78f, 0.50f); line(0.50f, 0.32f, 0.50f, 0.68f); break;
        case 2: ellipse(0.22f, 0.18f, 0.56f, 0.56f); line(0.28f, 0.30f, 0.72f, 0.30f); line(0.50f, 0.74f, 0.50f, 0.92f); break;
        case 3: ellipse(0.18f, 0.22f, 0.64f, 0.22f); ellipse(0.18f, 0.48f, 0.64f, 0.22f); line(0.50f, 0.70f, 0.50f, 0.92f); break;
        case 4: ellipse(0.10f, 0.34f, 0.80f, 0.22f); line(0.50f, 0.56f, 0.50f, 0.94f); break;
        case 5: ellipse(0.08f, 0.40f, 0.28f, 0.28f); ellipse(0.36f, 0.28f, 0.28f, 0.28f); ellipse(0.62f, 0.46f, 0.26f, 0.26f); break;
        case 6: ellipse(0.16f, 0.12f, 0.28f, 0.70f); ellipse(0.54f, 0.20f, 0.26f, 0.62f); break;
        case 7: ellipse(0.38f, 0.08f, 0.24f, 0.84f); line(0.42f, 0.28f, 0.58f, 0.28f); line(0.42f, 0.48f, 0.58f, 0.48f); break;
        case 8: ellipse(0.10f, 0.28f, 0.48f, 0.56f); fillEllipse(0.26f, 0.46f, 0.12f, 0.16f); line(0.50f, 0.40f, 0.86f, 0.08f); line(0.58f, 0.34f, 0.70f, 0.22f); break;
        case 9: ellipse(0.08f, 0.40f, 0.42f, 0.34f); line(0.46f, 0.48f, 0.90f, 0.10f); line(0.16f, 0.74f, 0.16f, 0.92f); line(0.40f, 0.74f, 0.40f, 0.92f); break;
        case 10: ellipse(0.08f, 0.42f, 0.36f, 0.30f); line(0.40f, 0.52f, 0.92f, 0.12f); line(0.18f, 0.72f, 0.18f, 0.94f); break;
        case 11: box(0.08f, 0.28f, 0.84f, 0.44f); line(0.28f, 0.28f, 0.28f, 0.72f); line(0.50f, 0.28f, 0.50f, 0.72f); line(0.72f, 0.28f, 0.72f, 0.72f); break;
        case 12: box(0.12f, 0.16f, 0.76f, 0.18f); box(0.12f, 0.42f, 0.76f, 0.18f); box(0.12f, 0.68f, 0.76f, 0.16f); break;
        case 13: box(0.10f, 0.22f, 0.80f, 0.56f); fillEllipse(0.22f, 0.36f, 0.14f, 0.14f); fillEllipse(0.44f, 0.36f, 0.14f, 0.14f); fillEllipse(0.66f, 0.36f, 0.14f, 0.14f); break;
        case 14: box(0.06f, 0.32f, 0.88f, 0.36f); line(0.24f, 0.32f, 0.24f, 0.68f); line(0.42f, 0.32f, 0.42f, 0.68f); line(0.60f, 0.32f, 0.60f, 0.68f); line(0.78f, 0.32f, 0.78f, 0.68f); break;
        case 15: line(0.06f, 0.58f, 0.62f, 0.40f); ellipse(0.58f, 0.28f, 0.30f, 0.36f); line(0.20f, 0.62f, 0.20f, 0.78f); line(0.36f, 0.54f, 0.36f, 0.70f); break;
        case 16:
        {
            juce::Path path;
            path.startNewSubPath(X(0.70f), Y(0.12f));
            path.quadraticTo(X(0.20f), Y(0.20f), X(0.28f), Y(0.55f));
            path.quadraticTo(X(0.36f), Y(0.92f), X(0.78f), Y(0.78f));
            g.strokePath(path, juce::PathStrokeType(stroke));
            ellipse(0.62f, 0.62f, 0.28f, 0.26f);
            break;
        }
        case 17: line(0.06f, 0.62f, 0.92f, 0.28f); fillEllipse(0.30f, 0.46f, 0.08f, 0.08f); fillEllipse(0.48f, 0.38f, 0.08f, 0.08f); fillEllipse(0.66f, 0.30f, 0.08f, 0.08f); break;
        case 18: ellipse(0.16f, 0.28f, 0.46f, 0.58f); line(0.52f, 0.40f, 0.86f, 0.08f); line(0.28f, 0.48f, 0.48f, 0.62f); break;
        case 19: ellipse(0.30f, 0.06f, 0.40f, 0.28f); line(0.38f, 0.16f, 0.62f, 0.16f); line(0.38f, 0.24f, 0.62f, 0.24f); line(0.50f, 0.34f, 0.50f, 0.78f); ellipse(0.40f, 0.76f, 0.20f, 0.16f); break;
        case 20: ellipse(0.34f, 0.08f, 0.32f, 0.24f); line(0.50f, 0.32f, 0.50f, 0.72f); line(0.62f, 0.18f, 0.86f, 0.10f); line(0.66f, 0.28f, 0.88f, 0.28f); break;
        case 21: g.drawEllipse(X(0.16f), Y(0.10f), r.getWidth() * 0.68f, r.getHeight() * 0.48f, stroke); ellipse(0.10f, 0.40f, 0.22f, 0.32f); ellipse(0.68f, 0.40f, 0.22f, 0.32f); line(0.79f, 0.70f, 0.92f, 0.88f); break;
        case 22: ellipse(0.32f, 0.06f, 0.36f, 0.36f); g.strokePath([&]{ juce::Path p; p.startNewSubPath(X(0.16f), Y(0.96f)); p.quadraticTo(X(0.50f), Y(0.48f), X(0.84f), Y(0.96f)); return p; }(), juce::PathStrokeType(stroke)); break;
        case 23: ellipse(0.12f, 0.10f, 0.30f, 0.30f); ellipse(0.56f, 0.10f, 0.30f, 0.30f); line(0.08f, 0.90f, 0.42f, 0.52f); line(0.42f, 0.52f, 0.58f, 0.90f); line(0.58f, 0.90f, 0.92f, 0.52f); break;
        case 24: ellipse(0.06f, 0.14f, 0.24f, 0.24f); ellipse(0.38f, 0.08f, 0.24f, 0.24f); ellipse(0.70f, 0.14f, 0.24f, 0.24f); line(0.08f, 0.92f, 0.28f, 0.50f); line(0.38f, 0.92f, 0.50f, 0.46f); line(0.62f, 0.92f, 0.74f, 0.50f); break;
        case 25: box(0.12f, 0.10f, 0.76f, 0.50f); line(0.06f, 0.78f, 0.94f, 0.78f); line(0.12f, 0.60f, 0.06f, 0.78f); line(0.88f, 0.60f, 0.94f, 0.78f); break;
        case 26: text("FX"); break;
        case 27: box(0.16f, 0.12f, 0.68f, 0.76f); ellipse(0.30f, 0.28f, 0.40f, 0.40f); fillEllipse(0.42f, 0.40f, 0.16f, 0.16f); break;
        case 28: g.drawEllipse(X(0.18f), Y(0.08f), r.getWidth() * 0.64f, r.getHeight() * 0.46f, stroke); ellipse(0.08f, 0.38f, 0.24f, 0.36f); ellipse(0.68f, 0.38f, 0.24f, 0.36f); break;
        case 29: box(0.18f, 0.22f, 0.64f, 0.56f); fillEllipse(0.28f, 0.40f, 0.14f, 0.14f); fillEllipse(0.56f, 0.40f, 0.14f, 0.14f); break;
        case 30: box(0.14f, 0.16f, 0.72f, 0.70f); ellipse(0.32f, 0.32f, 0.36f, 0.36f); line(0.28f, 0.16f, 0.28f, 0.08f); line(0.72f, 0.16f, 0.72f, 0.08f); break;
        case 31: line(0.22f, 0.16f, 0.22f, 0.84f); line(0.50f, 0.16f, 0.50f, 0.84f); line(0.78f, 0.16f, 0.78f, 0.84f); fillEllipse(0.14f, 0.30f, 0.16f, 0.12f); fillEllipse(0.42f, 0.52f, 0.16f, 0.12f); fillEllipse(0.70f, 0.38f, 0.16f, 0.12f); break;
        case 32: text("L"); break;
        case 33: text("R"); break;
        case 34: ellipse(0.12f, 0.12f, 0.76f, 0.76f); fillEllipse(0.30f, 0.34f, 0.10f, 0.10f); fillEllipse(0.60f, 0.34f, 0.10f, 0.10f); g.strokePath([&]{ juce::Path p; p.startNewSubPath(X(0.32f), Y(0.58f)); p.quadraticTo(X(0.50f), Y(0.78f), X(0.68f), Y(0.58f)); return p; }(), juce::PathStrokeType(stroke)); break;
        case 35: box(0.22f, 0.08f, 0.56f, 0.56f); line(0.50f, 0.64f, 0.50f, 0.94f); line(0.34f, 0.80f, 0.66f, 0.80f); break;
        default: break;
    }
}

class ChannelIconButton : public juce::Button,
                          private juce::Timer
{
public:
    std::function<void()> onDoubleClick;

    ChannelIconButton()
        : juce::Button("icon")
    {
        setTooltip("Click to choose an icon. Double-click to choose a colour");
    }

    void setIcon(int next)
    {
        icon = next;
        repaint();
    }

    void setStripColour(int stored)
    {
        colour = std::clamp(stored, 0, 15);
        repaint();
    }

    int getIcon() const noexcept { return icon; }

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        paintColourIcon(g, bounds, colour);

        if (icon >= 0)
            drawChannelIcon(g, bounds.reduced(2.0f), icon, stripIconInk(colour));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.getNumberOfClicks() > 1)
        {
            stopTimer();
            doubled = true;
            setState(buttonNormal);
            return;
        }

        doubled = false;
        juce::Button::mouseDown(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        setState(buttonNormal);

        if (! doubled && event.getNumberOfClicks() == 1 && getLocalBounds().contains(event.getPosition()))
            startTimer(250);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        stopTimer();
        doubled = true;

        if (onDoubleClick != nullptr)
            onDoubleClick();
    }

private:
    void timerCallback() override
    {
        stopTimer();

        if (! doubled)
            triggerClick();
    }

    int icon = -1;
    int colour = 0;
    bool doubled = false;
};

class ChannelIconPicker : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    std::function<void(int)> onPick;
    int selected = -1;

    ChannelIconPicker()
    {
        const int rows = (numChannelIcons + 1 + columns - 1) / columns;
        setSize(columns * cell + 8, rows * cell + 8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1c2128));

        for (int slot = 0; slot < numChannelIcons + 1; ++slot)
        {
            const int icon = slot == 0 ? -1 : slot - 1;
            auto bounds = cellBounds(slot).toFloat().reduced(3.0f);
            const bool active = icon == selected;
            g.setColour(active ? juce::Colour(0xff24343c) : juce::Colour(0xff14181d));
            g.fillRoundedRectangle(bounds, 4.0f);
            g.setColour(active ? juce::Colour(0xff7eb6d6) : juce::Colour(0xff3c4450));
            g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

            if (icon >= 0)
                drawChannelIcon(g, bounds.reduced(4.0f), icon);
            else
            {
                g.setColour(juce::Colour(0xff9aa3ad));
                g.drawLine(bounds.getX() + 8.0f, bounds.getY() + 8.0f, bounds.getRight() - 8.0f, bounds.getBottom() - 8.0f, 1.4f);
                g.drawLine(bounds.getRight() - 8.0f, bounds.getY() + 8.0f, bounds.getX() + 8.0f, bounds.getBottom() - 8.0f, 1.4f);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const int slot = slotAt(event.getPosition());
        setTooltip(slot < 0 ? juce::String() : channelIconName(slot == 0 ? -1 : slot - 1));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const int slot = slotAt(event.getPosition());

        if (slot < 0 || onPick == nullptr)
            return;

        onPick(slot == 0 ? -1 : slot - 1);

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

private:
    static constexpr int columns = 8;
    static constexpr int cell = 42;

    juce::Rectangle<int> cellBounds(int slot) const
    {
        return { 4 + (slot % columns) * cell, 4 + (slot / columns) * cell, cell, cell };
    }

    int slotAt(juce::Point<int> position) const
    {
        const int col = (position.x - 4) / cell;
        const int row = (position.y - 4) / cell;

        if (col < 0 || col >= columns || row < 0)
            return -1;

        const int slot = row * columns + col;
        return slot <= numChannelIcons ? slot : -1;
    }
};

inline void openChannelIconPicker(juce::Component& anchor, ConsoleModel& model, int channel, std::function<void()> changed)
{
    auto picker = std::make_unique<ChannelIconPicker>();
    int current = -1;
    model.getInt("/ch/" + juce::String(channel + 1).paddedLeft('0', 2) + "/config/icon", current);
    picker->selected = current;
    picker->onPick = [&model, channel, changed](int icon)
    {
        model.setInt("/ch/" + juce::String(channel + 1).paddedLeft('0', 2) + "/config/icon", icon);

        if (changed != nullptr)
            changed();
    };
    juce::CallOutBox::launchAsynchronously(std::move(picker), anchor.getScreenBounds(), nullptr);
}

} // namespace vc
