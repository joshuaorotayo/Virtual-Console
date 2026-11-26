// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/InsertEditor.h"

#include "engine/FxProcessor.h"

namespace vc
{
namespace
{
juce::String hertzLabel(float frequency)
{
    if (frequency >= 1000.0f)
        return juce::String(frequency / 1000.0f, frequency >= 10000.0f ? 0 : (frequency >= 2000.0f ? 1 : 2)) + " kHz";

    return juce::String(frequency, frequency < 100.0f ? 1 : 0) + " Hz";
}

juce::String slotLeaf(int slot)
{
    return "/fx/" + juce::String(slot + 1).paddedLeft('0', 2);
}
} // namespace

struct InsertEditor::Bands : public juce::Component
{
    std::function<void(int, float)> onBand;
    std::array<float, graphicEqBands> values {};
    int drag = -1;

    Bands()
    {
        values.fill(0.5f);
    }

    void setBand(int index, float value)
    {
        if (index < 0 || index >= graphicEqBands || index == drag)
            return;

        values[(size_t) index] = value;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto plot = getLocalBounds().toFloat();
        plot.removeFromLeft(36.0f);
        plot.removeFromBottom(18.0f);
        g.setColour(juce::Colour(0xff232830));
        g.fillRoundedRectangle(plot, 4.0f);

        const float centre = plot.getCentreY();
        g.setColour(juce::Colour(0xff3c4450));
        g.drawHorizontalLine((int) centre, plot.getX(), plot.getRight());
        g.setColour(juce::Colour(0xff98a2ad));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("+15", 0, (int) plot.getY(), 34, 14, juce::Justification::centredRight);
        g.drawText("0", 0, (int) centre - 7, 34, 14, juce::Justification::centredRight);
        g.drawText("-15", 0, (int) plot.getBottom() - 14, 34, 14, juce::Justification::centredRight);

        const float width = plot.getWidth() / (float) graphicEqBands;

        for (int band = 0; band < graphicEqBands; ++band)
        {
            const float x = plot.getX() + width * (float) band;
            const float gain = values[(size_t) band];
            const float y = plot.getBottom() - gain * plot.getHeight();
            const auto bar = juce::Rectangle<float>(x + 1.0f, std::min(y, centre), std::max(2.0f, width - 2.0f), std::abs(y - centre));
            g.setColour(gain >= 0.5f ? juce::Colour(0xff7eb6d6) : juce::Colour(0xffd28a4a));
            g.fillRect(bar);

            if (band == 0 || band == 7 || band == 17 || band == 27 || band == 30)
            {
                g.setColour(juce::Colour(0xff98a2ad));
                g.drawText(hertzLabel(graphicEqFrequency(band)),
                           (int) x - 8,
                           getHeight() - 16,
                           (int) width + 16,
                           14,
                           juce::Justification::centred);
            }
        }
    }

    int bandAt(const juce::MouseEvent& event) const
    {
        auto plot = getLocalBounds();
        plot.removeFromLeft(36);
        plot.removeFromBottom(18);

        if (! plot.contains(event.getPosition()))
            return -1;

        const int band = (event.x - plot.getX()) * graphicEqBands / std::max(1, plot.getWidth());
        return juce::jlimit(0, graphicEqBands - 1, band);
    }

    void apply(const juce::MouseEvent& event)
    {
        if (drag < 0)
            return;

        auto plot = getLocalBounds();
        plot.removeFromLeft(36);
        plot.removeFromBottom(18);
        const float span = (float) std::max(1, plot.getHeight());
        const float value = juce::jlimit(0.0f, 1.0f, 1.0f - ((float) event.y - (float) plot.getY()) / span);
        values[(size_t) drag] = value;

        if (onBand != nullptr)
            onBand(drag, value);

        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        drag = bandAt(event);
        apply(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        apply(event);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        drag = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const int band = bandAt(event);

        if (band < 0 || onBand == nullptr)
            return;

        values[(size_t) band] = 0.5f;
        onBand(band, 0.5f);
        repaint();
    }
};

InsertEditor::InsertEditor(ConsoleModel& modelToUse)
    : model(modelToUse)
{
    slotBox.addItem("Off", 1);

    for (int slotIndex = 0; slotIndex < numFxSlots; ++slotIndex)
        slotBox.addItem("FX " + juce::String(slotIndex + 1), slotIndex + 2);

    slotBox.onChange = [this]
    {
        if (! applying && slotBox.getSelectedId() > 0)
            applySlot(slotBox.getSelectedId() - 2);
    };
    slotBox.setTooltip("Insert one FX slot on this strip. Each slot can feed one strip.");
    addAndMakeVisible(slotBox);

    position.setClickingTogglesState(true);
    position.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
    position.setTooltip("Pre EQ is before the equaliser. Post is after the compressor and before the fader.");
    position.onClick = [this]
    {
        if (! applying && root.isNotEmpty())
        {
            model.setInt(root + "/fxinsert/pos", position.getToggleState() ? 1 : 0);
            position.setButtonText(position.getToggleState() ? "Post" : "Pre EQ");
        }
    };
    addAndMakeVisible(position);

    for (int type = 0; type <= (int) FxAlgo::dualGraphicEq; ++type)
        typeBox.addItem(fxAlgoName(type), type + 1);

    typeBox.onChange = [this]
    {
        if (! applying && slot >= 0 && typeBox.getSelectedId() > 0)
        {
            model.setFxType(slot, typeBox.getSelectedId() - 1);
            refresh();
        }
    };
    addAndMakeVisible(typeBox);

    enabled.setClickingTogglesState(true);
    enabled.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d8f5a));
    enabled.onClick = [this]
    {
        if (! applying && slot >= 0)
            model.setInt(slotLeaf(slot) + "/on", enabled.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible(enabled);

    side.setClickingTogglesState(true);
    side.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
    side.setTooltip("Dual Graphic EQ keeps a separate curve for each side. A mono strip uses the left curve.");
    side.onClick = [this]
    {
        editingRight = side.getToggleState();
        side.setButtonText(editingRight ? "Right" : "Left");
        refresh();
    };
    addAndMakeVisible(side);

    flat.onClick = [this]
    {
        if (slot < 0)
            return;

        for (int band = 0; band < graphicEqBands; ++band)
            model.setFloat(slotLeaf(slot) + "/par/" + juce::String(paramIndex(band) + 1).paddedLeft('0', 2), 0.5f);

        refresh();
    };
    addAndMakeVisible(flat);

    hint.setColour(juce::Label::textColourId, juce::Colour(0xff98a2ad));
    hint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(hint);

    bands = std::make_unique<Bands>();
    bands->onBand = [this](int band, float value)
    {
        if (applying || slot < 0)
            return;

        model.setFloat(slotLeaf(slot) + "/par/" + juce::String(paramIndex(band) + 1).paddedLeft('0', 2), value);
        hint.setText(hertzLabel(graphicEqFrequency(band)) + "   " + juce::String(graphicEqGainDb(value), 1) + " dB",
                     juce::dontSendNotification);
    };
    addAndMakeVisible(*bands);
}

InsertEditor::~InsertEditor() = default;

void InsertEditor::setRoot(const juce::String& newRoot)
{
    root = newRoot;
    editingRight = false;
    side.setToggleState(false, juce::dontSendNotification);
    side.setButtonText("Left");
    refresh();
}

void InsertEditor::applySlot(int newSlot)
{
    if (root.isEmpty())
        return;

    model.setInt(root + "/fxinsert/slot", newSlot);
    refresh();
}

int InsertEditor::paramIndex(int band) const noexcept
{
    return band + (editingRight ? graphicEqBands : 0);
}

void InsertEditor::showBands(int type)
{
    const bool graphic = fxIsGraphicEq(type);
    const bool dual = type == (int) FxAlgo::dualGraphicEq;
    bands->setVisible(slot >= 0 && graphic);
    side.setVisible(slot >= 0 && dual);
    flat.setVisible(slot >= 0 && graphic);
    typeBox.setVisible(slot >= 0);
    enabled.setVisible(slot >= 0);

    if (slot < 0)
        hint.setText("Choose an FX slot to insert it on this strip. Slots 5 to 8 are insert only.", juce::dontSendNotification);
    else if (! graphic)
        hint.setText("Graphic EQ and Dual Graphic EQ run in the insert. The other effects stay on their send returns.", juce::dontSendNotification);
    else if (hint.getText().isEmpty())
        hint.setText("Drag a band. Double-click it to flatten that band.", juce::dontSendNotification);
}

void InsertEditor::refresh()
{
    applying = true;
    int chosen = -1;

    if (root.isNotEmpty())
        model.getInt(root + "/fxinsert/slot", chosen);

    slot = chosen >= 0 && chosen < numFxSlots ? chosen : -1;

    if (! slotBox.isPopupActive())
        slotBox.setSelectedId(slot + 2, juce::dontSendNotification);

    int post = 0;

    if (root.isNotEmpty())
        model.getInt(root + "/fxinsert/pos", post);

    if (! position.isMouseButtonDown())
    {
        position.setToggleState(post != 0, juce::dontSendNotification);
        position.setButtonText(post != 0 ? "Post" : "Pre EQ");
    }

    int type = 0;
    int on = 0;

    if (slot >= 0)
    {
        model.getInt(slotLeaf(slot) + "/type", type);
        model.getInt(slotLeaf(slot) + "/on", on);
    }

    if (! typeBox.isPopupActive())
        typeBox.setSelectedId(type + 1, juce::dontSendNotification);
    enabled.setToggleState(on != 0, juce::dontSendNotification);
    showBands(type);

    if (slot >= 0 && fxIsGraphicEq(type))
    {
        for (int band = 0; band < graphicEqBands; ++band)
        {
            float value = 0.5f;
            model.getFloat(slotLeaf(slot) + "/par/" + juce::String(paramIndex(band) + 1).paddedLeft('0', 2), value);
            bands->setBand(band, value);
        }

        bands->repaint();
    }

    applying = false;
}

void InsertEditor::resized()
{
    auto area = getLocalBounds().reduced(8, 6);
    auto header = area.removeFromTop(32);
    slotBox.setBounds(header.removeFromLeft(110).reduced(0, 4));
    header.removeFromLeft(6);
    position.setBounds(header.removeFromLeft(78).reduced(0, 4));
    header.removeFromLeft(6);
    typeBox.setBounds(header.removeFromLeft(180).reduced(0, 4));
    header.removeFromLeft(6);
    enabled.setBounds(header.removeFromLeft(56).reduced(0, 4));
    header.removeFromLeft(6);
    side.setBounds(header.removeFromLeft(64).reduced(0, 4));
    flat.setBounds(header.removeFromRight(64).reduced(0, 4));
    hint.setBounds(area.removeFromTop(22));
    bands->setBounds(area);
}

} // namespace vc
