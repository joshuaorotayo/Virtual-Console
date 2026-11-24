#pragma once

#include "model/ConsoleModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

// DCA circles in two rows of four, and mute-group squares in two rows of three.
class GroupPad : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit GroupPad(ConsoleModel& modelToUse)
        : model(modelToUse)
    {
    }

    void setRoot(const juce::String& newRoot)
    {
        root = newRoot;
        refresh();
    }

    void refresh()
    {
        if (root.isEmpty())
            return;

        for (int group = 0; group < numDcas; ++group)
        {
            int assigned = 0;
            model.getInt(root + "/grp/dca/" + juce::String(group + 1).paddedLeft('0', 2), assigned);
            dcaOn[(size_t) group] = assigned != 0;
        }

        for (int group = 0; group < numMuteGroups; ++group)
        {
            int assigned = 0;
            model.getInt(root + "/grp/mute/" + juce::String(group + 1).paddedLeft('0', 2), assigned);
            muteOn[(size_t) group] = assigned != 0;
        }

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff12161b));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);

        g.setColour(juce::Colour(0xffe7edf3));
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("DCA Groups", dcaTitle, juce::Justification::centredLeft);
        g.drawText("Mute Groups", muteTitle, juce::Justification::centredLeft);

        for (int group = 0; group < numDcas; ++group)
        {
            const auto bounds = dcaBounds[(size_t) group];
            const bool on = dcaOn[(size_t) group];
            g.setColour(on ? juce::Colour(0xff3dce4a) : juce::Colour(0xff3a4048));
            g.fillEllipse(bounds);
            g.setColour(on ? juce::Colour(0xff102014) : juce::Colour(0xffd7dde4));
            g.setFont(juce::Font(juce::FontOptions(juce::jmax(11.0f, bounds.getHeight() * 0.42f))));
            g.drawText(juce::String(group + 1), bounds, juce::Justification::centred);
        }

        for (int group = 0; group < numMuteGroups; ++group)
        {
            const auto bounds = muteBounds[(size_t) group];
            const bool on = muteOn[(size_t) group];
            g.setColour(juce::Colour(0xff3a4048));
            g.fillRect(bounds);
            g.setColour(on ? juce::Colour(0xffe23a3a) : juce::Colour(0xff2a3036));
            g.drawRect(bounds, on ? 2.0f : 1.0f);
            g.setColour(juce::Colour(0xffe7edf3));
            g.setFont(juce::Font(juce::FontOptions(juce::jmax(11.0f, bounds.getHeight() * 0.42f))));
            g.drawText(juce::String(group + 1), bounds, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().toFloat().reduced(8.0f, 6.0f);
        const float titleH = juce::jmin(20.0f, area.getHeight() * 0.12f);
        const float grids = juce::jmax(0.0f, area.getHeight() - titleH * 2.0f);
        dcaTitle = area.removeFromTop(titleH);
        auto dcaGrid = area.removeFromTop(grids * 0.56f);
        muteTitle = area.removeFromTop(titleH);
        auto muteGrid = area;

        placeGrid(dcaGrid, dcaBounds, numDcas, 4, true);
        placeGrid(muteGrid, muteBounds, numMuteGroups, 3, false);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        int group = -1;

        if (hit(dcaBounds, numDcas, event.position, group))
        {
            dcaOn[(size_t) group] = ! dcaOn[(size_t) group];
            model.setInt(root + "/grp/dca/" + juce::String(group + 1).paddedLeft('0', 2), dcaOn[(size_t) group] ? 1 : 0);
            repaint();
            return;
        }

        if (hit(muteBounds, numMuteGroups, event.position, group))
        {
            muteOn[(size_t) group] = ! muteOn[(size_t) group];
            model.setInt(root + "/grp/mute/" + juce::String(group + 1).paddedLeft('0', 2), muteOn[(size_t) group] ? 1 : 0);
            repaint();
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        int group = -1;

        if (hit(dcaBounds, numDcas, event.position, group))
            setTooltip("Assign to DCA " + juce::String(group + 1));
        else if (hit(muteBounds, numMuteGroups, event.position, group))
            setTooltip("Assign to mute group " + juce::String(group + 1));
        else
            setTooltip({});
    }

private:
    static void placeGrid(juce::Rectangle<float> grid, juce::Rectangle<float>* bounds, int count, int columns, bool circle)
    {
        const int rows = (count + columns - 1) / columns;
        const float cellW = grid.getWidth() / (float) columns;
        const float cellH = rows > 0 ? grid.getHeight() / (float) rows : 0.0f;
        const float size = juce::jmax(12.0f, juce::jmin(cellW, cellH) - (circle ? 10.0f : 12.0f));

        for (int index = 0; index < count; ++index)
        {
            const int column = index % columns;
            const int row = index / columns;
            auto cell = juce::Rectangle<float>(grid.getX() + column * cellW, grid.getY() + row * cellH, cellW, cellH);
            bounds[index] = cell.withSizeKeepingCentre(size, circle ? size : size * 0.86f);
        }
    }

    static bool hit(const juce::Rectangle<float>* bounds, int count, juce::Point<float> position, int& index)
    {
        for (int i = 0; i < count; ++i)
        {
            if (bounds[i].contains(position))
            {
                index = i;
                return true;
            }
        }

        return false;
    }

    ConsoleModel& model;
    juce::String root;
    bool dcaOn[numDcas] {};
    bool muteOn[numMuteGroups] {};
    juce::Rectangle<float> dcaTitle;
    juce::Rectangle<float> muteTitle;
    juce::Rectangle<float> dcaBounds[numDcas];
    juce::Rectangle<float> muteBounds[numMuteGroups];
};

} // namespace vc
