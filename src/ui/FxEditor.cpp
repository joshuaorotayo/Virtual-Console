// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/FxEditor.h"

#include "engine/FxProcessor.h"

#include <algorithm>
#include <cmath>

namespace vc
{
namespace
{
juce::String slotPath(int slot)
{
    return "/fx/" + juce::String(slot + 1).paddedLeft('0', 2);
}

const char* paramName(int type, int index)
{
    const bool delay = type == (int) FxAlgo::stereoDelay;
    const bool chorus = type == (int) FxAlgo::stereoChorus;
    const bool flanger = type == (int) FxAlgo::stereoFlanger;
    const bool phaser = type == (int) FxAlgo::stereoPhaser;
    const bool tremolo = type == (int) FxAlgo::tremoloPanner;
    static const char* room[] = { "Decay", "Predelay", "Size", "Damping" };
    static const char* delayNames[] = { "Time", "Feedback", "Right", "Damping" };
    static const char* chorusNames[] = { "Rate", "Depth", "Width", "Tone" };
    static const char* flangerNames[] = { "Rate", "Depth", "Feedback", "Manual" };
    static const char* phaserNames[] = { "Rate", "Depth", "Feedback", "Spread" };
    static const char* tremoloNames[] = { "Rate", "Depth", "Phase", "Pan" };

    if (delay)
        return delayNames[index];

    if (chorus)
        return chorusNames[index];

    if (flanger)
        return flangerNames[index];

    if (phaser)
        return phaserNames[index];

    if (tremolo)
        return tremoloNames[index];

    return room[index];
}

juce::String readoutFor(int type, int index, float value)
{
    const auto percent = [value] { return juce::String(juce::roundToInt(std::clamp(value, 0.0f, 1.0f) * 100.0f)); };
    const bool room = type == (int) FxAlgo::vintageRoom || type == (int) FxAlgo::hall;
    const bool delay = type == (int) FxAlgo::stereoDelay;
    const bool modulated = type == (int) FxAlgo::stereoChorus || type == (int) FxAlgo::stereoFlanger
                        || type == (int) FxAlgo::stereoPhaser || type == (int) FxAlgo::tremoloPanner;

    if (room && index == 0)
        return juce::String(0.2 + value * 7.8, value > 0.85f ? 0 : 1) + " s";

    if (room && index == 1)
        return juce::String(juce::roundToInt(value * 200.0f)) + " ms";

    if (delay && index == 0)
        return juce::String(juce::roundToInt(value * 1000.0f)) + " ms";

    if (delay && index == 1)
        return juce::String(juce::roundToInt(value * 90.0f)) + "%";

    if (delay && index == 2)
        return juce::String(juce::roundToInt((0.5f + value) * 100.0f)) + "%";

    if (modulated && index == 0)
        return juce::String(0.15 + value * 4.5, 2) + " Hz";

    if ((type == (int) FxAlgo::stereoChorus || type == (int) FxAlgo::stereoFlanger) && index == 1)
        return juce::String(1.0 + value * 7.0, 1) + " ms";

    return percent();
}

juce::Colour ink() { return juce::Colour(0xffd7e2ea); }
juce::Colour dim() { return juce::Colour(0xff8b98a3); }
juce::Colour leftColour() { return juce::Colour(0xff3ee0d0); }
juce::Colour rightColour() { return juce::Colour(0xffe0b040); }

juce::Rectangle<float> plotBounds(juce::Rectangle<float> area)
{
    return area.reduced(16.0f, 18.0f).withTrimmedBottom(8.0f);
}

void paintFrame(juce::Graphics& g, juce::Rectangle<float> area, juce::Rectangle<float> plot, const juce::String& caption)
{
    g.setColour(juce::Colour(0xff101418));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour(0xff2a323a));
    g.drawRoundedRectangle(area.reduced(0.5f), 8.0f, 1.0f);

    g.setColour(juce::Colour(0xff243038));

    for (int line = 1; line < 4; ++line)
    {
        const float y = plot.getY() + plot.getHeight() * (float) line / 4.0f;
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
    }

    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(caption, area.reduced(12.0f, 6.0f).removeFromTop(16.0f).toNearestInt(), juce::Justification::topLeft, false);
}

void paintReverb(juce::Graphics& g, juce::Rectangle<float> area, int type, const float* par)
{
    auto plot = plotBounds(area);
    const bool hall = type == (int) FxAlgo::hall;
    paintFrame(g, area, plot, hall ? "Hall tail" : "Room tail");

    const float decay = par[0];
    const float predelay = par[1];
    const float size = par[2];
    const float damp = par[3];
    const float left = plot.getX();
    const float right = plot.getRight();
    const float top = plot.getY() + 8.0f;
    const float bottom = plot.getBottom();
    const float width = std::max(1.0f, right - left);
    const float start = left + predelay * 0.28f * width;
    const float fall = (hall ? 0.45f : 0.7f) + (1.0f - decay) * (hall ? 4.2f : 5.4f);
    const int reflections = 5 + (int) std::round(size * (hall ? 14.0f : 8.0f));
    const float spread = (0.08f + size * (hall ? 0.28f : 0.16f)) * width;

    g.setColour(juce::Colour(0xff3a4650));
    g.drawVerticalLine(juce::roundToInt(start), top, bottom);

    for (int i = 0; i < reflections; ++i)
    {
        const float along = (float) (i + 1) / (float) (reflections + 1);
        const float x = start + along * spread;
        const float height = (0.55f - along * 0.35f) * (0.65f + 0.35f * std::sin(along * 17.0f));

        if (x >= right - 4.0f)
            break;

        g.setColour(leftColour().withAlpha(0.55f));
        g.drawVerticalLine(juce::roundToInt(x), bottom - height * (bottom - top), bottom);
    }

    juce::Path tail;
    juce::Path highs;
    const float tailStart = std::min(right - 8.0f, start + spread * 0.35f);
    tail.startNewSubPath(tailStart, bottom);
    const int steps = std::max(8, (int) (right - tailStart));

    for (int step = 0; step <= steps; ++step)
    {
        const float t = (float) step / (float) steps;
        const float body = std::exp(-t * fall);
        const float air = std::exp(-t * (fall + 1.2f + damp * 5.0f));
        const float x = tailStart + t * (right - tailStart);
        tail.lineTo(x, bottom - body * (bottom - top));
        const float highY = bottom - air * (bottom - top) * 0.85f;

        if (step == 0)
            highs.startNewSubPath(x, highY);
        else
            highs.lineTo(x, highY);
    }

    tail.lineTo(right, bottom);
    tail.closeSubPath();
    g.setColour(leftColour().withAlpha(0.22f));
    g.fillPath(tail);
    g.setColour(leftColour());
    g.strokePath(tail, juce::PathStrokeType(2.0f));
    g.setColour(rightColour().withAlpha(0.9f));
    g.strokePath(highs, juce::PathStrokeType(1.4f));
    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("Predelay", juce::roundToInt(start) + 4, juce::roundToInt(top), 70, 14, juce::Justification::left, false);
    g.drawText("HF", juce::roundToInt(right) - 28, juce::roundToInt(top), 24, 14, juce::Justification::right, false);
}

void paintDelay(juce::Graphics& g, juce::Rectangle<float> area, const float* par)
{
    auto plot = plotBounds(area);
    paintFrame(g, area, plot, "Delay taps");
    const float time = std::max(0.04f, par[0]);
    const float feedback = par[1] * 0.9f;
    const float rightFactor = 0.5f + par[2];
    const float damp = par[3];
    const float left = plot.getX();
    const float right = plot.getRight();
    const float bottom = plot.getBottom();
    const float top = plot.getY() + 10.0f;
    const float width = std::max(1.0f, right - left);
    const float span = (0.12f + time * 0.16f) * width;
    const int count = 1 + (int) std::round(feedback * 8.0f);

    auto spike = [&](float x, float height, juce::Colour colour)
    {
        if (x < left + 2.0f || x > right - 2.0f || height <= 0.02f)
            return;

        g.setColour(colour);
        const float y = bottom - height * (bottom - top);
        g.fillRect(x - 2.0f, y, 4.0f, bottom - y);
        g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    };

    for (int tap = 0; tap < count; ++tap)
    {
        float level = std::pow(std::max(feedback, 0.05f), (float) tap);
        level *= 1.0f - damp * 0.75f * (float) tap / (float) count;
        const float step = (float) (tap + 1);
        spike(left + step * span * rightFactor, level * 0.92f, rightColour());
        spike(left + step * span, level, leftColour());
    }

    g.setColour(leftColour());
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("L", plot.toNearestInt().removeFromTop(16).removeFromRight(36), juce::Justification::centredRight, false);
    g.setColour(rightColour());
    g.drawText("R", plot.toNearestInt().removeFromTop(16).translated(0, 14).removeFromRight(36), juce::Justification::centredRight, false);
}

void paintWave(juce::Graphics& g, juce::Rectangle<float> plot, float cycles, float depth, float rightPhase, float centreShift, float feedback)
{
    const float mid = plot.getCentreY() + centreShift * plot.getHeight() * 0.2f;
    const float amp = (0.08f + depth * 0.38f) * plot.getHeight();
    juce::Path leftPath;
    juce::Path rightPath;
    const int steps = std::max(16, (int) plot.getWidth());

    for (int step = 0; step <= steps; ++step)
    {
        const float t = (float) step / (float) steps;
        const float x = plot.getX() + t * plot.getWidth();
        const float leftY = mid - std::sin(t * cycles * 6.2831853f) * amp;
        const float rightY = mid - std::sin((t + rightPhase) * cycles * 6.2831853f) * amp * (0.85f + feedback * 0.3f);

        if (step == 0)
        {
            leftPath.startNewSubPath(x, leftY);
            rightPath.startNewSubPath(x, rightY);
        }
        else
        {
            leftPath.lineTo(x, leftY);
            rightPath.lineTo(x, rightY);
        }
    }

    g.setColour(juce::Colour(0xff3a4650));
    g.drawHorizontalLine(juce::roundToInt(mid), plot.getX(), plot.getRight());
    g.setColour(rightColour());
    g.strokePath(rightPath, juce::PathStrokeType(1.6f));
    g.setColour(leftColour());
    g.strokePath(leftPath, juce::PathStrokeType(2.0f));
}

void paintModulation(juce::Graphics& g, juce::Rectangle<float> area, int type, const float* par)
{
    auto plot = plotBounds(area);
    const bool flanger = type == (int) FxAlgo::stereoFlanger;
    const bool phaser = type == (int) FxAlgo::stereoPhaser;
    paintFrame(g, area, plot, flanger ? "Flanger sweep" : phaser ? "Phaser notches" : "Chorus sweep");

    if (phaser)
    {
        const float spread = 0.05f + par[3] * 0.12f;
        const float depth = par[1];
        const float feedback = par[2];
        juce::Path response;
        juce::Path ghost;
        const int steps = std::max(24, (int) plot.getWidth());

        for (int step = 0; step <= steps; ++step)
        {
            const float t = (float) step / (float) steps;
            float shape = 1.0f;
            float ghostShape = 1.0f;

            for (int notch = 0; notch < 4; ++notch)
            {
                const float centre = 0.18f + (float) notch * spread + 0.08f;
                const float distance = std::abs(t - centre);
                const float dip = std::exp(-distance * distance / 0.0016f);
                shape -= dip * (0.55f + feedback * 0.4f);
                ghostShape -= std::exp(-std::pow(t - (centre + depth * 0.06f), 2.0f) / 0.0016f) * 0.45f;
            }

            const float x = plot.getX() + t * plot.getWidth();
            const float y = plot.getBottom() - std::clamp(shape, 0.05f, 1.0f) * plot.getHeight() * 0.85f;
            const float ghostY = plot.getBottom() - std::clamp(ghostShape, 0.05f, 1.0f) * plot.getHeight() * 0.85f;

            if (step == 0)
            {
                response.startNewSubPath(x, y);
                ghost.startNewSubPath(x, ghostY);
            }
            else
            {
                response.lineTo(x, y);
                ghost.lineTo(x, ghostY);
            }
        }

        g.setColour(rightColour().withAlpha(0.7f));
        g.strokePath(ghost, juce::PathStrokeType(1.3f));
        g.setColour(leftColour());
        g.strokePath(response, juce::PathStrokeType(2.0f));
        g.setColour(dim());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("100", juce::roundToInt(plot.getX()), juce::roundToInt(plot.getBottom()) + 1, 36, 14, juce::Justification::left, false);
        g.drawText("10k", juce::roundToInt(plot.getRight()) - 28, juce::roundToInt(plot.getBottom()) + 1, 28, 14, juce::Justification::right, false);
        return;
    }

    const float rate = par[0];
    const float depth = par[1];
    const float feedback = flanger ? std::abs(par[2] - 0.5f) * 2.0f : 0.0f;
    const float manual = flanger ? par[3] - 0.5f : 0.0f;
    paintWave(g, plot, 0.7f + rate * 2.4f, depth, flanger ? 0.5f : 0.25f, manual, feedback);
}

void paintTremolo(juce::Graphics& g, juce::Rectangle<float> area, const float* par)
{
    auto plot = plotBounds(area);
    const bool panner = par[3] >= 0.5f;
    paintFrame(g, area, plot, panner ? "Auto pan" : "Tremolo");
    const float rate = par[0];
    const float depth = par[1];
    const float phase = par[2];
    const float floor = 1.0f - depth;
    juce::Path leftPath;
    juce::Path rightPath;
    const int steps = std::max(16, (int) plot.getWidth());

    for (int step = 0; step <= steps; ++step)
    {
        const float t = (float) step / (float) steps;
        const float x = plot.getX() + t * plot.getWidth();
        const float leftWave = 0.5f + 0.5f * std::sin(t * (0.8f + rate * 2.6f) * 6.2831853f);
        const float rightWave = 0.5f + 0.5f * std::sin((t + phase * 0.5f + (panner ? 0.5f : 0.0f)) * (0.8f + rate * 2.6f) * 6.2831853f);
        const float leftGain = floor + (1.0f - floor) * leftWave;
        const float rightGain = floor + (1.0f - floor) * rightWave;
        const float leftY = plot.getBottom() - leftGain * plot.getHeight() * 0.82f;
        const float rightY = plot.getBottom() - rightGain * plot.getHeight() * 0.82f;

        if (step == 0)
        {
            leftPath.startNewSubPath(x, leftY);
            rightPath.startNewSubPath(x, rightY);
        }
        else
        {
            leftPath.lineTo(x, leftY);
            rightPath.lineTo(x, rightY);
        }
    }

    if (panner)
    {
        g.setColour(rightColour());
        g.strokePath(rightPath, juce::PathStrokeType(1.6f));
    }

    g.setColour(leftColour());
    g.strokePath(leftPath, juce::PathStrokeType(2.0f));
}

juce::String hertzLabel(float frequency)
{
    if (frequency >= 1000.0f)
        return juce::String(frequency / 1000.0f, frequency >= 10000.0f ? 0 : 1) + "k";

    return juce::String(juce::roundToInt(frequency));
}
} // namespace

class FxEditor::Picture : public juce::Component
{
public:
    int type = 0;
    std::array<float, 64> values {};
    int drag = -1;
    std::function<void(int, float)> onBand;

    void setParam(int index, float value)
    {
        if (index >= 0 && index < (int) values.size())
            values[(size_t) index] = value;

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced(2.0f);

        if (fxIsGraphicEq(type))
        {
            paintGraphic(g, area);
            return;
        }

        const float par[4] = { values[0], values[1], values[2], values[3] };

        if (type == (int) FxAlgo::vintageRoom || type == (int) FxAlgo::hall)
            paintReverb(g, area, type, par);
        else if (type == (int) FxAlgo::stereoDelay)
            paintDelay(g, area, par);
        else if (type == (int) FxAlgo::stereoChorus || type == (int) FxAlgo::stereoFlanger || type == (int) FxAlgo::stereoPhaser)
            paintModulation(g, area, type, par);
        else if (type == (int) FxAlgo::tremoloPanner)
            paintTremolo(g, area, par);
        else
        {
            paintFrame(g, area, plotBounds(area), "Empty");
            g.setColour(dim());
            g.setFont(juce::Font(juce::FontOptions(16.0f)));
            g.drawText("No effect in this slot", getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void paintGraphic(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const bool dual = type == (int) FxAlgo::dualGraphicEq;
        auto plot = plotBounds(area);
        paintFrame(g, area, plot, dual ? "Dual graphic EQ" : "Graphic EQ");
        g.setColour(juce::Colour(0xff3a4650));
        g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());
        const float width = plot.getWidth() / (float) graphicEqBands;

        for (int band = 0; band < graphicEqBands; ++band)
        {
            const float x = plot.getX() + width * (float) band;
            auto bar = [&](float gain, juce::Colour colour, float inset)
            {
                const float y = plot.getBottom() - std::clamp(gain, 0.0f, 1.0f) * plot.getHeight();
                g.setColour(colour);
                g.fillRect(x + inset, std::min(y, plot.getCentreY()), std::max(2.0f, width * (dual ? 0.42f : 0.72f)), std::abs(y - plot.getCentreY()));
            };

            if (dual)
            {
                bar(values[(size_t) (graphicEqBands + band)], rightColour(), width * 0.48f);
                bar(values[(size_t) band], leftColour(), 1.0f);
            }
            else
            {
                bar(values[(size_t) band], leftColour(), 1.0f);
            }

            if (band == 0 || band == 7 || band == 17 || band == 27 || band == 30)
            {
                g.setColour(dim());
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(hertzLabel(graphicEqFrequency(band)),
                           juce::roundToInt(x) - 8,
                           juce::roundToInt(plot.getBottom()) + 1,
                           juce::roundToInt(width) + 16,
                           12,
                           juce::Justification::centred,
                           false);
            }
        }
    }

    int bandAt(juce::Point<int> position) const
    {
        auto plot = plotBounds(getLocalBounds().toFloat()).toNearestInt();

        if (! plot.contains(position))
            return -1;

        const int band = (position.x - plot.getX()) * graphicEqBands / std::max(1, plot.getWidth());
        return juce::jlimit(0, graphicEqBands - 1, band);
    }

    void applyBand(const juce::MouseEvent& event)
    {
        if (! fxIsGraphicEq(type) || drag < 0)
            return;

        auto plot = plotBounds(getLocalBounds().toFloat());
        const float value = juce::jlimit(0.0f, 1.0f, 1.0f - ((float) event.y - plot.getY()) / std::max(1.0f, plot.getHeight()));
        values[(size_t) drag] = value;

        if (onBand != nullptr)
            onBand(drag, value);

        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        drag = bandAt(event.getPosition());
        applyBand(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        applyBand(event);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        drag = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const int band = bandAt(event.getPosition());

        if (band < 0 || onBand == nullptr)
            return;

        values[(size_t) band] = 0.5f;
        onBand(band, 0.5f);
        repaint();
    }
};

FxEditor::FxEditor(ConsoleModel& modelToUse)
    : model(modelToUse)
{
    title.setFont(juce::Font(juce::FontOptions(18.0f)));
    title.setColour(juce::Label::textColourId, ink());
    title.setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff1c2128));
    title.setEditable(false, true, false);
    title.onTextChange = [this] { model.setFxReturnName(fxReturn, title.getText()); };
    addAndMakeVisible(title);
    sendLabel.setColour(juce::Label::textColourId, dim());
    addAndMakeVisible(sendLabel);

    for (int type = 0; type <= (int) FxAlgo::dualGraphicEq; ++type)
        typeBox.addItem(fxAlgoName(type), type + 1);

    typeBox.onChange = [this]
    {
        if (! applying && typeBox.getSelectedId() > 0)
            applyType(typeBox.getSelectedId() - 1);
    };
    addAndMakeVisible(typeBox);

    enabled.setClickingTogglesState(true);
    enabled.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3d6f8f));
    enabled.onClick = [this]
    {
        if (! applying)
            model.setInt(slotPath(slot) + "/on", enabled.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible(enabled);

    picture = std::make_unique<Picture>();
    picture->onBand = [this](int band, float value)
    {
        if (applying)
            return;

        model.setFloat(slotPath(slot) + "/par/" + juce::String(band + 1).paddedLeft('0', 2), value);
    };
    addAndMakeVisible(*picture);

    for (int i = 0; i < 4; ++i)
    {
        paramLabels[(size_t) i].setColour(juce::Label::textColourId, dim());
        paramLabels[(size_t) i].setJustificationType(juce::Justification::centred);
        paramLabels[(size_t) i].setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(paramLabels[(size_t) i]);
        readouts[(size_t) i].setColour(juce::Label::textColourId, ink());
        readouts[(size_t) i].setJustificationType(juce::Justification::centred);
        readouts[(size_t) i].setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(readouts[(size_t) i]);
        auto& slider = params[(size_t) i];
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setScrollWheelEnabled(false);
        slider.setDoubleClickReturnValue(true, 0.5);
        const int index = i;
        slider.onValueChange = [this, index]
        {
            const float value = (float) params[(size_t) index].getValue();
            picture->setParam(index, value);
            readouts[(size_t) index].setText(readoutFor(typeBox.getSelectedId() - 1, index, value), juce::dontSendNotification);

            if (! applying)
                model.setFloat(slotPath(slot) + "/par/" + juce::String(index + 1).paddedLeft('0', 2), value);
        };
        addAndMakeVisible(slider);
    }

    setReturn(0);
}

FxEditor::~FxEditor() = default;

void FxEditor::setReturn(int returnIndex)
{
    slot = juce::jlimit(0, 3, returnIndex / 2);
    fxReturn = juce::jlimit(0, numFxReturns - 1, returnIndex);
    title.setText(model.getFxReturnName(fxReturn), juce::dontSendNotification);
    refresh();
}

void FxEditor::applyType(int type)
{
    model.setFxType(slot, type);
    refresh();
}

void FxEditor::showParameters(int type)
{
    const bool chorus = type == (int) FxAlgo::stereoChorus;
    const bool graphic = fxIsGraphicEq(type);

    for (int i = 0; i < 4; ++i)
    {
        const bool visible = ! graphic && (! chorus || i < 2);
        paramLabels[(size_t) i].setVisible(visible);
        readouts[(size_t) i].setVisible(visible);
        params[(size_t) i].setVisible(visible);
        paramLabels[(size_t) i].setText(paramName(type, i), juce::dontSendNotification);
    }
}

void FxEditor::pushPicture()
{
    int type = 0;
    model.getInt(slotPath(slot) + "/type", type);
    picture->type = type;
    const int count = fxIsGraphicEq(type) ? (type == (int) FxAlgo::dualGraphicEq ? 62 : graphicEqBands) : 4;

    for (int i = 0; i < count; ++i)
    {
        if (i < 4 && params[(size_t) i].isMouseButtonDown())
            continue;

        if (i == picture->drag)
            continue;

        float value = 0.5f;
        model.getFloat(slotPath(slot) + "/par/" + juce::String(i + 1).paddedLeft('0', 2), value);
        picture->values[(size_t) i] = value;
    }

    picture->repaint();
}

void FxEditor::refresh()
{
    if (! title.isBeingEdited())
        title.setText(model.getFxReturnName(fxReturn), juce::dontSendNotification);

    int type = 0;
    model.getInt(slotPath(slot) + "/type", type);

    if (fxIsGraphicEq(type))
        sendLabel.setText("Drag a band. Double-click it to flatten that band.", juce::dontSendNotification);
    else
        sendLabel.setText(model.getBusName(12 + slot) + " sends into this effect. The picture follows the faders.",
                          juce::dontSendNotification);

    applying = true;

    if (! typeBox.isPopupActive())
        typeBox.setSelectedId(type + 1, juce::dontSendNotification);

    int on = 0;
    model.getInt(slotPath(slot) + "/on", on);
    enabled.setToggleState(on != 0, juce::dontSendNotification);
    showParameters(type);

    for (int i = 0; i < 4; ++i)
    {
        if (params[(size_t) i].isMouseButtonDown())
            continue;

        float value = 0.5f;
        model.getFloat(slotPath(slot) + "/par/" + juce::String(i + 1).paddedLeft('0', 2), value);
        params[(size_t) i].setValue(value, juce::dontSendNotification);
        readouts[(size_t) i].setText(readoutFor(type, i, value), juce::dontSendNotification);
    }

    pushPicture();
    applying = false;
    resized();
}

void FxEditor::resized()
{
    auto area = getLocalBounds().reduced(16, 10);
    auto header = area.removeFromTop(32);
    title.setBounds(header.removeFromLeft(140));
    enabled.setBounds(header.removeFromRight(64).reduced(0, 4));
    typeBox.setBounds(header.removeFromLeft(220).reduced(8, 4));
    sendLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    int shown = 0;

    for (int i = 0; i < 4; ++i)
        if (params[(size_t) i].isVisible())
            ++shown;

    auto controls = shown == 0 ? juce::Rectangle<int>() : area.removeFromRight(shown * 84);
    picture->setBounds(area.reduced(0, 2));

    for (int i = 0; i < 4; ++i)
    {
        if (! params[(size_t) i].isVisible())
            continue;

        auto column = controls.removeFromLeft(84).reduced(6, 0);
        readouts[(size_t) i].setBounds(column.removeFromTop(18));
        paramLabels[(size_t) i].setBounds(column.removeFromBottom(32));
        params[(size_t) i].setBounds(column.reduced(10, 2));
    }
}

} // namespace vc
