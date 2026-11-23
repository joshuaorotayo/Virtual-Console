#include "ui/ChannelEditor.h"

#include "dsp/FilterDesign.h"
#include "model/DspLaw.h"

#include <array>
#include <cmath>

namespace vc
{
namespace
{
juce::Colour panel() { return juce::Colour(0xff1c2128); }
juce::Colour text() { return juce::Colour(0xffe7edf3); }
juce::Colour dim() { return juce::Colour(0xff9aa3ad); }
juce::Colour accent() { return juce::Colour(0xff3d6f8f); }
juce::Colour engaged() { return juce::Colour(0xff8a6a32); }

juce::String channelPath(int index, const juce::String& leaf)
{
    return "/ch/" + juce::String(index + 1).paddedLeft('0', 2) + "/" + leaf;
}

juce::Colour channelColour(int index)
{
    return juce::Colour::fromHSV(std::fmod(index * 0.17f, 1.0f), 0.55f, 0.78f, 1.0f);
}

juce::String hertzText(float hertz)
{
    if (hertz >= 1000.0f)
        return juce::String(hertz / 1000.0f, 2) + " kHz";

    return juce::String(juce::roundToInt(hertz)) + " Hz";
}

juce::String decibelText(float decibels)
{
    return juce::String(decibels, 1) + " dB";
}

juce::String millisecondsText(float milliseconds)
{
    if (milliseconds >= 1000.0f)
        return juce::String(milliseconds / 1000.0f, 2) + " s";

    return juce::String(milliseconds, milliseconds < 10.0f ? 2 : 1) + " ms";
}

class UnitSlider : public juce::Component
{
public:
    UnitSlider(const juce::String& titleText,
               std::function<float()> readValue,
               std::function<void(float)> writeValue,
               std::function<juce::String(float)> formatValue)
        : read(std::move(readValue)),
          write(std::move(writeValue)),
          format(std::move(formatValue))
    {
        title.setText(titleText, juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);
        title.setFont(juce::Font(juce::FontOptions(11.0f)));
        title.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(title);

        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setScrollWheelEnabled(false);
        slider.textFromValueFunction = [this](double value) { return format((float) value); };
        slider.onValueChange = [this]
        {
            if (write != nullptr)
                write((float) slider.getValue());
        };
        addAndMakeVisible(slider);
    }

    void refresh()
    {
        if (read == nullptr || slider.isMouseButtonDown() || textBoxFocused())
            return;

        slider.setValue(read(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(16));
        slider.setBounds(area);
    }

private:
    bool textBoxFocused() const
    {
        for (auto* child : slider.getChildren())
            if (child->hasKeyboardFocus(true))
                return true;

        return false;
    }

    std::function<float()> read;
    std::function<void(float)> write;
    std::function<juce::String(float)> format;
    juce::Label title;
    juce::Slider slider;
};

juce::Colour eqBandColour(int band)
{
    static const juce::uint32 colours[] = { 0xffe0b040, 0xff3dce7a, 0xff7eb6d6, 0xffd27ad4, 0xffe07a7a };
    return juce::Colour(colours[band % 5]);
}

class ControlBox : public juce::Component
{
public:
    void setTitle(const juce::String& newTitle, juce::Colour outline)
    {
        title = newTitle;
        colour = outline;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff232830));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(colour);
        g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(title, getLocalBounds().reduced(8, 3).removeFromTop(14), juce::Justification::centredLeft, true);
    }

private:
    juce::String title;
    juce::Colour colour { 0xff3c4450 };
};

class FlagButton : public juce::TextButton
{
public:
    FlagButton(const juce::String& label, const juce::String& pathToUse, ConsoleModel& modelToUse, int& indexToUse)
        : juce::TextButton(label),
          path(pathToUse),
          model(modelToUse),
          index(indexToUse)
    {
        setClickingTogglesState(true);
        setColour(juce::TextButton::buttonOnColourId, accent());
        onClick = [this] { model.setInt(channelPath(index, path), getToggleState() ? 1 : 0); };
    }

    void refresh()
    {
        if (isMouseButtonDown())
            return;

        int enabled = 0;
        model.getInt(channelPath(index, path), enabled);
        setToggleState(enabled != 0, juce::dontSendNotification);
    }

    bool isOn() const { return getToggleState(); }

private:
    juce::String path;
    ConsoleModel& model;
    int& index;
};

class ChoiceBox : public juce::Component
{
public:
    ChoiceBox(const juce::String& titleText,
              const juce::String& pathToUse,
              ConsoleModel& modelToUse,
              int& indexToUse,
              const juce::StringArray& items)
        : path(pathToUse),
          model(modelToUse),
          index(indexToUse)
    {
        title.setText(titleText, juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centredLeft);
        title.setFont(juce::Font(juce::FontOptions(11.0f)));
        title.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(title);
        box.addItemList(items, 1);
        box.setScrollWheelEnabled(false);
        box.onChange = [this]
        {
            if (box.getSelectedId() > 0)
                model.setInt(channelPath(index, path), box.getSelectedId() - 1);
        };
        addAndMakeVisible(box);
    }

    void refresh()
    {
        if (box.isPopupActive())
            return;

        int value = 0;
        model.getInt(channelPath(index, path), value);
        box.setSelectedId(juce::jlimit(0, box.getNumItems() - 1, value) + 1, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(14));
        box.setBounds(area.removeFromTop(24).reduced(1, 0));
    }

private:
    juce::String path;
    ConsoleModel& model;
    int& index;
    juce::Label title;
    juce::ComboBox box;
};

class DynamicsGraph : public juce::Component
{
public:
    enum class Kind { gate, compressor };

    DynamicsGraph(ConsoleModel& modelToUse, Kind graphKind)
        : model(modelToUse), kind(graphKind)
    {
    }

    void setChannel(int channelIndex)
    {
        index = channelIndex;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff15191e));
        g.fillRoundedRectangle(bounds, 6.0f);

        auto plot = bounds.reduced(8.0f, 6.0f);
        auto timing = plot.removeFromBottom(42.0f);
        plot.removeFromBottom(8.0f);
        plot.removeFromLeft(34.0f);

        if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f)
            return;

        g.setColour(juce::Colour(0xff2a313a));

        for (float db : { 20.0f, 10.0f, 0.0f, -10.0f, -20.0f, -40.0f, -60.0f, -80.0f })
        {
            const float y = yFor(db, plot);
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
            g.setColour(dim());
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(juce::String(juce::roundToInt(db)), 2, juce::roundToInt(y) - 6, 30, 12, juce::Justification::centredRight, false);
            g.setColour(juce::Colour(0xff2a313a));
        }

        const float threshold = thresholdDb();
        const float range = rangeDb();
        g.setColour(juce::Colour(0xffe07a7a).withAlpha(0.9f));
        const float thresholdX = xFor(threshold, plot);
        g.drawVerticalLine(juce::roundToInt(thresholdX), plot.getY(), plot.getBottom());

        juce::Path curve;
        bool started = false;

        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const float input = dbForX(x, plot);
            const float output = std::clamp(outputDb(input, threshold, range), -80.0f, 12.0f);
            const float y = yFor(output, plot);

            if (! started)
            {
                curve.startNewSubPath(x, y);
                started = true;
            }
            else
            {
                curve.lineTo(x, y);
            }
        }

        g.setColour(juce::Colour(0xfff4f7fb));
        g.strokePath(curve, juce::PathStrokeType(2.0f));
        g.setColour(juce::Colour(0xffe07a7a));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(decibelText(threshold), juce::roundToInt(thresholdX) + 4, juce::roundToInt(plot.getY()) + 2, 70, 14, juce::Justification::left, false);

        paintTiming(g, timing);
    }

private:
    static float yFor(float db, juce::Rectangle<float> plot) noexcept
    {
        constexpr float top = 24.0f;
        constexpr float bottom = -80.0f;
        const float clamped = std::clamp(db, bottom, top);
        return plot.getY() + (top - clamped) / (top - bottom) * plot.getHeight();
    }

    static float xFor(float db, juce::Rectangle<float> plot) noexcept
    {
        const float clamped = std::clamp(db, -80.0f, 0.0f);
        return plot.getX() + (clamped + 80.0f) / 80.0f * plot.getWidth();
    }

    static float dbForX(float x, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::clamp((x - plot.getX()) / plot.getWidth(), 0.0f, 1.0f);
        return -80.0f + t * 80.0f;
    }

    float readUnit(const char* leaf, float fallback) const
    {
        float value = fallback;
        const auto root = kind == Kind::gate ? juce::String("gate/") : juce::String("dyn/");
        model.getFloat(channelPath(index, root + leaf), value);
        return value;
    }

    int readInt(const char* leaf) const
    {
        int value = 0;
        const auto root = kind == Kind::gate ? juce::String("gate/") : juce::String("dyn/");
        model.getInt(channelPath(index, root + leaf), value);
        return value;
    }

    float thresholdDb() const
    {
        return kind == Kind::gate ? gateThresholdDb(readUnit("thr", 0.75f)) : compThresholdDb(readUnit("thr", 0.75f));
    }

    float rangeDb() const
    {
        return kind == Kind::gate ? gateRangeDb(readUnit("range", 0.5f)) : 0.0f;
    }

    float outputDb(float input, float threshold, float depth) const
    {
        if (kind == Kind::gate)
            return gateOutputDb(input, readInt("mode"), threshold, depth);

        return compressorOutputDb(input,
                                   readInt("mode"),
                                   threshold,
                                   compRatio(readUnit("ratio", 0.5f)),
                                   compKneeDb(readUnit("knee", 0.2f)),
                                   compMakeupDb(readUnit("mgain", 0.5f)));
    }

    void paintTiming(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float attack = dynamicsMilliseconds(readUnit("attack", 0.2f), 0.1f, 120.0f);
        const float hold = dynamicsMilliseconds(readUnit("hold", 0.2f), 0.1f, 2000.0f);
        const float release = dynamicsMilliseconds(readUnit("release", 0.5f), 5.0f, 4000.0f);
        const float total = std::max(1.0f, attack + hold + release);
        auto bar = area.removeFromTop(16.0f);
        const float attackW = bar.getWidth() * attack / total;
        const float holdW = bar.getWidth() * hold / total;
        g.setColour(juce::Colour(0xff3d8f5a));
        g.fillRect(bar.removeFromLeft(attackW).reduced(1.0f, 2.0f));
        g.setColour(juce::Colour(0xff3d6f8f));
        g.fillRect(bar.removeFromLeft(holdW).reduced(1.0f, 2.0f));
        g.setColour(juce::Colour(0xffe07a7a));
        g.fillRect(bar.reduced(1.0f, 2.0f));
        g.setColour(dim());
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("Attack " + millisecondsText(attack) + "    Hold " + millisecondsText(hold) + "    Release " + millisecondsText(release),
                   area,
                   juce::Justification::centredLeft,
                   false);
    }

    ConsoleModel& model;
    Kind kind;
    int index = 0;
};

class EqGraph : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit EqGraph(ConsoleModel& modelToUse)
        : model(modelToUse)
    {
        setTooltip("Drag a band to set frequency and gain. Scroll over a band to change its Q.");
    }

    void setChannel(int channelIndex)
    {
        index = channelIndex;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff15191e));
        g.fillRoundedRectangle(bounds, 6.0f);

        auto plot = bounds.reduced(8.0f, 6.0f);
        plot.removeFromLeft(36.0f);
        plot.removeFromBottom(18.0f);

        if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f)
            return;

        g.setColour(juce::Colour(0xff2a313a));

        for (float db : { 15.0f, 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -15.0f, -20.0f })
        {
            const float y = yForDb(db, plot);
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }

        for (float hz : { 30.0f, 40.0f, 60.0f, 80.0f, 150.0f, 300.0f, 400.0f, 700.0f, 1500.0f, 3000.0f, 4000.0f, 7000.0f, 15000.0f })
        {
            const float x = xForHz(hz, plot);
            g.drawVerticalLine(juce::roundToInt(x), plot.getBottom() - 5.0f, plot.getBottom());
        }

        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForHz(hz, plot);
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        g.setColour(juce::Colour(0xffd7dbe2).withAlpha(0.35f));
        g.drawHorizontalLine(juce::roundToInt(yForDb(0.0f, plot)), plot.getX(), plot.getRight());

        g.setColour(dim());
        g.setFont(juce::Font(juce::FontOptions(10.0f)));

        for (float db : { 15.0f, 10.0f, 5.0f, 0.0f, -5.0f, -10.0f, -15.0f, -20.0f })
        {
            const auto label = juce::String(juce::roundToInt(db));
            g.drawText(label, 2, juce::roundToInt(yForDb(db, plot)) - 7, 30, 14, juce::Justification::centredRight, false);
        }

        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForHz(hz, plot);
            const auto label = hz >= 1000.0f ? juce::String(juce::roundToInt(hz / 1000.0f)) + "k" : juce::String(juce::roundToInt(hz));
            g.drawText(label, juce::roundToInt(x) - 16, juce::roundToInt(plot.getBottom()) + 1, 32, 14, juce::Justification::centred, false);
        }

        const auto hpf = readHpf();
        std::array<Band, numChannelEqBands> bands {};
        int eqEnabled = 1;
        model.getInt(channelPath(index, "eq/on"), eqEnabled);

        for (int band = 0; band < numChannelEqBands; ++band)
            bands[(size_t) band] = readBand(band);

        if (eqEnabled == 0)
        {
            for (auto& bandState : bands)
                bandState.contributes = false;

            g.setColour(juce::Colour(0xffe07a7a));
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("EQ off", plot.reduced(6.0f).removeFromTop(16.0f), juce::Justification::topRight, false);
        }

        auto drawCurve = [&](auto response, juce::Colour colour, float thickness)
        {
            juce::Path path;
            bool started = false;

            for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            {
                const float db = response(hzForX(x, plot));
                const float y = yForDb(db, plot);

                if (! started)
                {
                    path.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    path.lineTo(x, y);
                }
            }

            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(thickness));
            return path;
        };

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            const auto& bandState = bands[(size_t) band];

            if (! bandState.contributes)
                continue;

            drawCurve([&](float hz) { return biquadMagnitudeDb(bandState.coeffs, hz, sampleRate); },
                      bandColour(band).withAlpha(band == selected ? 0.95f : 0.55f),
                      band == selected ? 2.0f : 1.25f);
        }

        if (hpf.enabled)
            drawCurve([&](float hz) { return biquadMagnitudeDb(hpf.coeffs, hz, sampleRate); },
                      juce::Colour(0xfff2f5f8).withAlpha(0.45f),
                      1.25f);

        const auto sum = drawCurve([&](float hz)
                                    {
                                        float db = hpf.enabled ? biquadMagnitudeDb(hpf.coeffs, hz, sampleRate) : 0.0f;

                                        for (const auto& bandState : bands)
                                            if (bandState.contributes)
                                                db += biquadMagnitudeDb(bandState.coeffs, hz, sampleRate);

                                        return db;
                                    },
                                    juce::Colour(0xfff4f7fb),
                                    2.2f);
        juce::ignoreUnused(sum);

        drawHandle(g, plot, xForHz(hpf.frequency, plot), yForDb(0.0f, plot), juce::Colour(0xfff2f5f8), hpf.enabled, selected == hpfId, "H");

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            const auto& bandState = bands[(size_t) band];
            const float gain = bandState.usesGain ? bandState.gain : 0.0f;
            drawHandle(g,
                       plot,
                       xForHz(bandState.frequency, plot),
                       yForDb(gain, plot),
                       bandColour(band),
                       bandState.enabled,
                       selected == band,
                       juce::String(band + 1));
        }

        if (selected == hpfId)
            drawReadout(g, plot, xForHz(hpf.frequency, plot), yForDb(0.0f, plot), "HPF  " + hertzText(hpf.frequency));
        else if (selected >= 0 && selected < numChannelEqBands)
        {
            const auto& bandState = bands[(size_t) selected];
            juce::String readout = "Band " + juce::String(selected + 1) + "  " + hertzText(bandState.frequency);

            if (bandState.usesGain)
                readout << "  " << decibelText(bandState.gain);

            if (bandState.usesQ)
                readout << "  Q " << juce::String(bandState.q, 2);

            drawReadout(g, plot, xForHz(bandState.frequency, plot), yForDb(bandState.usesGain ? bandState.gain : 0.0f, plot), readout);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const int hit = hitTest(event.position);
        setMouseCursor(hit == noneId ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        selected = hitTest(event.position);

        if (selected == noneId)
        {
            repaint();
            return;
        }

        if (selected == hpfId)
            model.setInt(channelPath(index, "preamp/hpon"), 1);
        else
            model.setInt(channelPath(index, "eq/" + juce::String(selected + 1) + "/on"), 1);

        applyDrag(event.position);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (selected != noneId)
            applyDrag(event.position);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const int hit = hitTest(event.position);

        if (hit >= 0 && hit < numChannelEqBands)
        {
            model.setFloat(channelPath(index, "eq/" + juce::String(hit + 1) + "/g"), 0.5f);
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const int hit = selected != noneId && isMouseButtonDown() ? selected : hitTest(event.position);

        if (hit < 0 || hit >= numChannelEqBands)
            return;

        const auto band = readBand(hit);

        if (! band.usesQ)
            return;

        const float next = eqQUnit(band.q) - wheel.deltaY * 0.08f;
        model.setFloat(channelPath(index, "eq/" + juce::String(hit + 1) + "/q"), clamp01(next));
        selected = hit;
        repaint();
    }

private:
    static constexpr int noneId = -2;
    static constexpr int hpfId = -1;
    static constexpr double sampleRate = 48000.0;
    static constexpr float dbTop = 18.0f;
    static constexpr float dbBottom = -24.0f;

    struct Band
    {
        bool enabled = false;
        bool contributes = false;
        bool usesGain = true;
        bool usesQ = false;
        int type = 2;
        float frequency = 1000.0f;
        float gain = 0.0f;
        float q = 1.0f;
        BiquadCoeffs coeffs {};
    };

    struct Hpf
    {
        bool enabled = false;
        float frequency = 80.0f;
        BiquadCoeffs coeffs {};
    };

    static juce::Colour bandColour(int band)
    {
        return eqBandColour(band);
    }

    static float yForDb(float db, juce::Rectangle<float> plot) noexcept
    {
        const float clamped = std::clamp(db, dbBottom, dbTop);
        return plot.getY() + (dbTop - clamped) / (dbTop - dbBottom) * plot.getHeight();
    }

    static float dbForY(float y, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::clamp((y - plot.getY()) / plot.getHeight(), 0.0f, 1.0f);
        return dbTop - t * (dbTop - dbBottom);
    }

    static float xForHz(float hz, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::log(std::max(hz, 20.0f) / 20.0f) / std::log(1000.0f);
        return plot.getX() + std::clamp(t, 0.0f, 1.0f) * plot.getWidth();
    }

    static float hzForX(float x, juce::Rectangle<float> plot) noexcept
    {
        const float t = std::clamp((x - plot.getX()) / plot.getWidth(), 0.0f, 1.0f);
        return 20.0f * std::pow(1000.0f, t);
    }

    juce::Rectangle<float> plotArea() const
    {
        auto plot = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
        plot.removeFromLeft(36.0f);
        plot.removeFromBottom(18.0f);
        return plot;
    }

    Hpf readHpf() const
    {
        Hpf hpf;
        int enabled = 0;
        float unit = 0.2f;
        model.getInt(channelPath(index, "preamp/hpon"), enabled);
        model.getFloat(channelPath(index, "preamp/hpf"), unit);
        hpf.enabled = enabled != 0;
        hpf.frequency = hpfFrequencyHz(unit);
        hpf.coeffs = designHighPass(hpf.frequency, sampleRate);
        return hpf;
    }

    Band readBand(int band) const
    {
        Band state;
        const auto prefix = "eq/" + juce::String(band + 1) + "/";
        int enabled = 1;
        int type = 2;
        float frequency = 0.5f;
        float gain = 0.5f;
        float q = 0.5f;
        model.getInt(channelPath(index, prefix + "on"), enabled);
        model.getInt(channelPath(index, prefix + "t"), type);
        model.getFloat(channelPath(index, prefix + "f"), frequency);
        model.getFloat(channelPath(index, prefix + "g"), gain);
        model.getFloat(channelPath(index, prefix + "q"), q);
        state.enabled = enabled != 0;
        state.type = type;
        state.frequency = eqFrequencyHz(frequency);
        state.gain = eqGainDb(gain);
        state.q = eqQ(q);
        state.usesGain = type != 0 && type != 5;
        state.usesQ = type == 2 || type == 3;
        const bool flat = std::abs(state.gain) < 0.05f && state.usesGain;
        state.contributes = state.enabled && ! flat;

        if (state.contributes)
        {
            if (type == 0)
                state.coeffs = designHighPass(state.frequency, sampleRate);
            else if (type == 1)
                state.coeffs = designShelf(state.frequency, state.gain, false, sampleRate);
            else if (type == 4)
                state.coeffs = designShelf(state.frequency, state.gain, true, sampleRate);
            else if (type == 5)
                state.coeffs = designLowPass(state.frequency, sampleRate);
            else
                state.coeffs = designPeak(state.frequency, state.q, state.gain, sampleRate);
        }

        return state;
    }

    int hitTest(juce::Point<float> position) const
    {
        const auto plot = plotArea();
        int best = noneId;
        float bestDistance = 16.0f;
        const auto hpf = readHpf();
        const float hpfDistance = position.getDistanceFrom({ xForHz(hpf.frequency, plot), yForDb(0.0f, plot) });

        if (hpfDistance < bestDistance)
        {
            best = hpfId;
            bestDistance = hpfDistance;
        }

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            const auto state = readBand(band);
            const float gain = state.usesGain ? state.gain : 0.0f;
            const float distance = position.getDistanceFrom({ xForHz(state.frequency, plot), yForDb(gain, plot) });

            if (distance < bestDistance)
            {
                best = band;
                bestDistance = distance;
            }
        }

        return best;
    }

    void applyDrag(juce::Point<float> position)
    {
        const auto plot = plotArea();

        if (selected == hpfId)
        {
            model.setFloat(channelPath(index, "preamp/hpf"), hpfFrequencyUnit(std::min(400.0f, hzForX(position.x, plot))));
        }
        else if (selected >= 0 && selected < numChannelEqBands)
        {
            const auto prefix = "eq/" + juce::String(selected + 1) + "/";
            const auto state = readBand(selected);
            model.setFloat(channelPath(index, prefix + "f"), eqFrequencyUnit(hzForX(position.x, plot)));

            if (state.usesGain)
                model.setFloat(channelPath(index, prefix + "g"), eqGainUnit(dbForY(position.y, plot)));
        }

        repaint();
    }

    static void drawHandle(juce::Graphics& g,
                           juce::Rectangle<float> plot,
                           float x,
                           float y,
                           juce::Colour colour,
                           bool enabled,
                           bool active,
                           const juce::String& label)
    {
        juce::ignoreUnused(plot);
        const float radius = active ? 9.0f : 7.0f;
        g.setColour(enabled ? colour : colour.withAlpha(0.35f));
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff15191e));
        g.drawEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f, active ? 2.0f : 1.0f);
        g.setColour(juce::Colour(0xff15191e));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(label, juce::roundToInt(x - radius), juce::roundToInt(y - radius), juce::roundToInt(radius * 2.0f), juce::roundToInt(radius * 2.0f), juce::Justification::centred, false);
    }

    static void drawReadout(juce::Graphics& g, juce::Rectangle<float> plot, float x, float y, const juce::String& readout)
    {
        const int width = 180;
        int left = juce::roundToInt(x + 12.0f);

        if (left + width > plot.getRight())
            left = juce::roundToInt(x - 12.0f - width);

        g.setColour(juce::Colour(0xff101317).withAlpha(0.85f));
        g.fillRoundedRectangle((float) left, y - 18.0f, (float) width, 16.0f, 3.0f);
        g.setColour(text());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(readout, left + 4, juce::roundToInt(y - 18.0f), width - 8, 16, juce::Justification::centredLeft, false);
    }

    ConsoleModel& model;
    int index = 0;
    int selected = noneId;
};
} // namespace

class ChannelEditor::Surface : public juce::Component
{
public:
    enum class Page
    {
        channel,
        eq,
        dyn,
        sends
    };

    Surface(ChannelEditor& ownerToUse, ConsoleModel& modelToUse)
        : owner(ownerToUse),
          model(modelToUse),
          eqGraph(modelToUse),
          gateGraph(modelToUse, DynamicsGraph::Kind::gate),
          compGraph(modelToUse, DynamicsGraph::Kind::compressor)
    {
        auto addSlider = [this](const juce::String& title,
                                const juce::String& path,
                                std::function<juce::String(float)> format)
        {
            auto slider = std::make_unique<UnitSlider>(
                title,
                [this, path]
                {
                    float value = 0.0f;
                    model.getFloat(channelPath(index, path), value);
                    return value;
                },
                [this, path](float value) { model.setFloat(channelPath(index, path), value); },
                std::move(format));
            addAndMakeVisible(*slider);
            sliders.push_back(std::move(slider));
            return sliders.back().get();
        };

        auto addFlag = [this](const juce::String& label, const juce::String& path)
        {
            auto button = std::make_unique<FlagButton>(label, path, model, index);
            addAndMakeVisible(*button);
            flags.push_back(std::move(button));
            return flags.back().get();
        };

        auto addChoice = [this](const juce::String& title, const juce::String& path, const juce::StringArray& items)
        {
            auto choice = std::make_unique<ChoiceBox>(title, path, model, index, items);
            addAndMakeVisible(*choice);
            choices.push_back(std::move(choice));
            return choices.back().get();
        };

        indexLabel.setJustificationType(juce::Justification::centredLeft);
        indexLabel.setFont(juce::Font(juce::FontOptions(16.0f)));
        indexLabel.setColour(juce::Label::textColourId, text());
        addAndMakeVisible(indexLabel);

        name.setJustificationType(juce::Justification::centredLeft);
        name.setEditable(false, true, false);
        name.setFont(juce::Font(juce::FontOptions(16.0f)));
        name.setColour(juce::Label::textColourId, text());
        name.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        name.onTextChange = [this]
        {
            model.setChannelName(index, name.getText());
            model.setString(channelPath(index, "config/name"), name.getText());
        };
        addAndMakeVisible(name);

        sourceReadout.setJustificationType(juce::Justification::centredLeft);
        sourceReadout.setFont(juce::Font(juce::FontOptions(13.0f)));
        sourceReadout.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(sourceReadout);

        for (auto* button : { &previousButton, &nextButton, &linkButton, &copyButton, &pasteButton })
        {
            button->setColour(juce::TextButton::buttonOnColourId, accent());
            addAndMakeVisible(*button);
        }

        previousButton.onClick = [this] { changeChannel(index - 1); };
        nextButton.onClick = [this] { changeChannel(index + 1); };
        linkButton.setClickingTogglesState(true);
        linkButton.setTooltip("Link with the odd/even partner. Pan stays independent.");
        linkButton.onClick = [this]
        {
            model.setChannelLinked(index, linkButton.getToggleState());
            refresh();
        };
        copyButton.onClick = [this]
        {
            model.copyChannel(index);
            pasteButton.setEnabled(model.hasChannelClipboard());
        };
        pasteButton.setEnabled(false);
        pasteButton.onClick = [this]
        {
            model.pasteChannel(index);
            refresh();
        };

        homeTab.onClick = [this] { show(Page::channel); };
        eqTab.onClick = [this] { show(Page::eq); };
        dynTab.onClick = [this] { show(Page::dyn); };
        sendsTab.onClick = [this] { show(Page::sends); };

        for (auto* tab : { &homeTab, &eqTab, &dynTab, &sendsTab })
            addAndMakeVisible(*tab);

        trim = addSlider("Trim", "preamp/trim", [](float value) { return decibelText(trimToDb(value)); });
        polarity = addFlag("Polarity", "preamp/invert");
        hpfOn = addFlag("HPF", "preamp/hpon");
        eqOn = addFlag("EQ", "eq/on");
        hpfFreq = addSlider("Frequency", "preamp/hpf", [](float value) { return hertzText(hpfFrequencyHz(value)); });

        sourceLabel.setText("Input", juce::dontSendNotification);
        sourceLabel.setColour(juce::Label::textColourId, dim());
        addAndMakeVisible(sourceLabel);
        sourceBox.setTextWhenNothingSelected("None");
        sourceBox.setScrollWheelEnabled(false);
        sourceBox.addItem("None", 1);

        for (int input = 0; input < numRoutableInputs; ++input)
            sourceBox.addItem(ConsoleModel::inputName(input), input + 2);

        sourceBox.onChange = [this]
        {
            const int id = sourceBox.getSelectedId();
            model.setChannelSource(index, id <= 1 ? -1 : id - 2);
            refresh();
        };
        addAndMakeVisible(sourceBox);
        mainAssign = addFlag("Stereo", "mix/st");
        monoAssign = addFlag("Mono", "mix/mono");
        monoLevel = addSlider("M Level", "mix/mlevel", [](float value) { return decibelText(faderToDb(value)); });
        delayOn = addFlag("Delay", "delay/on");
        delayTime = addSlider("Time", "delay/time", [](float value) { return millisecondsText(delaySeconds(value) * 1000.0f); });

        gateOn = addFlag("Gate", "gate/on");
        gateMode = addChoice("Mode", "gate/mode", juce::StringArray { "Gate", "Exp 2", "Exp 3", "Exp 4", "Duck" });
        gateThr = addSlider("Threshold", "gate/thr", [](float value) { return decibelText(gateThresholdDb(value)); });
        gateRange = addSlider("Range", "gate/range", [](float value) { return decibelText(gateRangeDb(value)); });
        gateAttack = addSlider("Attack", "gate/attack", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 120.0f)); });
        gateHold = addSlider("Hold", "gate/hold", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 2000.0f)); });
        gateRelease = addSlider("Release", "gate/release", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 5.0f, 4000.0f)); });

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            const auto prefix = "eq/" + juce::String(band + 1) + "/";
            auto& row = eq[band];
            row.on = addFlag(juce::String(band + 1), prefix + "on");
            row.type.addItemList(juce::StringArray { "LCut", "LShv", "PEQ", "VEQ", "HShv", "HCut" }, 1);
            row.type.onChange = [this, band]
            {
                if (eq[band].type.getSelectedId() > 0)
                    model.setInt(channelPath(index, "eq/" + juce::String(band + 1) + "/t"), eq[band].type.getSelectedId() - 1);
            };
            addAndMakeVisible(row.type);
            row.freq = addSlider("Freq", prefix + "f", [](float value) { return hertzText(eqFrequencyHz(value)); });
            row.gain = addSlider("Gain", prefix + "g", [](float value) { return decibelText(eqGainDb(value)); });
            row.q = addSlider("Q", prefix + "q", [](float value) { return juce::String(eqQ(value), 2); });
        }

        compOn = addFlag("Comp", "dyn/on");
        compMode = addChoice("Mode", "dyn/mode", juce::StringArray { "Comp", "Exp" });
        compDetect = addChoice("Detect", "dyn/det", juce::StringArray { "Peak", "RMS" });
        compEnvelope = addChoice("Envelope", "dyn/env", juce::StringArray { "Linear", "Log" });
        compPosition = addChoice("Position", "dyn/pos", juce::StringArray { "Post EQ", "Pre EQ" });
        compAuto = addFlag("Auto", "dyn/auto");
        compFilter = addFlag("Filter", "dyn/filter/on");
        compFilterType = addChoice("Filter", "dyn/filter/type", juce::StringArray { "LC6", "LC12", "HC6", "HC12", "Q 1", "Q 2", "Q 3", "Q 5", "Q 10" });
        compThr = addSlider("Threshold", "dyn/thr", [](float value) { return decibelText(vc::compThresholdDb(value)); });
        compRatio = addSlider("Ratio", "dyn/ratio", [](float value) { return juce::String(vc::compRatio(value), 1) + ":1"; });
        compKnee = addSlider("Knee", "dyn/knee", [](float value) { return decibelText(compKneeDb(value)); });
        compMakeup = addSlider("Makeup", "dyn/mgain", [](float value) { return decibelText(vc::compMakeupDb(value)); });
        compMix = addSlider("Mix", "dyn/mix", [](float value) { return juce::String(juce::roundToInt(clamp01(value) * 100.0f)) + "%"; });
        compAttack = addSlider("Attack", "dyn/attack", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 120.0f)); });
        compHold = addSlider("Hold", "dyn/hold", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 0.1f, 2000.0f)); });
        compRelease = addSlider("Release", "dyn/release", [](float value) { return millisecondsText(dynamicsMilliseconds(value, 5.0f, 4000.0f)); });
        compFilterFreq = addSlider("Key freq", "dyn/filter/f", [](float value) { return hertzText(eqFrequencyHz(value)); });

        auto addBox = [this](ControlBox& box, const juce::String& title, juce::Colour colour)
        {
            box.setTitle(title, colour);
            addAndMakeVisible(box);
            box.toBack();
        };
        addBox(preampBox, "Preamp", juce::Colour(0xff7eb6d6));
        addBox(configBox, "Config", juce::Colour(0xffd7dbe2));
        addBox(delayBox, "Delay", juce::Colour(0xffe0b040));
        addBox(gateBox, "Gate", juce::Colour(0xffe07a7a));
        addBox(hpfBox, "HPF", juce::Colour(0xfff2f5f8));

        for (int band = 0; band < numChannelEqBands; ++band)
            addBox(bandBox[band], "Band " + juce::String(band + 1), eqBandColour(band));

        for (int bus = 0; bus < numBuses; ++bus)
        {
            const auto number = juce::String(bus + 1).paddedLeft('0', 2);
            sendLevel[bus] = addSlider("Level", "mix/" + number + "/level", [](float value) { return decibelText(faderToDb(value)); });
            sendPre[bus] = addFlag("Pre", "mix/" + number + "/tap");
            addBox(sendBox[bus], "Bus " + juce::String(bus + 1), juce::Colour(0xff7eb6d6));
        }

        addAndMakeVisible(eqGraph);
        addAndMakeVisible(gateGraph);
        addAndMakeVisible(compGraph);
        show(Page::channel);
        refresh();
    }

    int getChannel() const noexcept { return index; }

    void setChannel(int channelIndex)
    {
        index = juce::jlimit(0, numChannels - 1, channelIndex);
        eqGraph.setChannel(index);
        gateGraph.setChannel(index);
        compGraph.setChannel(index);
        refresh();
    }

    void setSourceNames(const juce::StringArray& names)
    {
        sourceNames = names;
        refresh();
    }

    void refresh()
    {
        indexLabel.setText(juce::String(index + 1).paddedLeft('0', 2), juce::dontSendNotification);

        if (! name.isBeingEdited())
            name.setText(model.getChannelName(index), juce::dontSendNotification);

        const int route = model.getChannelSource(index);
        const int patched = model.getInputSource(route);
        juce::String readout = model.inputName(route);

        if (patched >= 0 && patched < sourceNames.size())
            readout += "  ·  " + sourceNames[patched];
        else if (route >= 0)
            readout += "  ·  no source";

        sourceReadout.setText(readout, juce::dontSendNotification);

        if (! sourceBox.isPopupActive())
            sourceBox.setSelectedId(route < 0 ? 1 : route + 2, juce::dontSendNotification);

        linkButton.setToggleState(model.isChannelLinked(index), juce::dontSendNotification);
        pasteButton.setEnabled(model.hasChannelClipboard());

        for (auto& slider : sliders)
            slider->refresh();

        for (auto& flag : flags)
            flag->refresh();

        for (auto& choice : choices)
            choice->refresh();

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            if (eq[band].type.isPopupActive())
                continue;

            int type = 2;
            model.getInt(channelPath(index, "eq/" + juce::String(band + 1) + "/t"), type);
            eq[band].type.setSelectedId(juce::jlimit(0, 5, type) + 1, juce::dontSendNotification);
        }

        styleTabs();
        eqGraph.setChannel(index);
        gateGraph.setChannel(index);
        compGraph.setChannel(index);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel());
        g.setColour(channelColour(index));
        g.fillRect(0, 0, 6, getHeight());
        g.setColour(juce::Colour(0xff2c333b));
        g.fillRect(0, 76, getWidth(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromLeft(10);
        auto head = area.removeFromTop(40).reduced(8, 6);
        previousButton.setBounds(head.removeFromLeft(32));
        head.removeFromLeft(4);
        nextButton.setBounds(head.removeFromLeft(32));
        head.removeFromLeft(8);
        indexLabel.setBounds(head.removeFromLeft(36));
        name.setBounds(head.removeFromLeft(juce::jmin(220, head.getWidth() / 3)));
        sourceReadout.setBounds(head.removeFromLeft(juce::jmin(220, head.getWidth() / 2)).reduced(8, 0));
        pasteButton.setBounds(head.removeFromRight(72));
        head.removeFromRight(6);
        copyButton.setBounds(head.removeFromRight(72));
        head.removeFromRight(6);
        linkButton.setBounds(head.removeFromRight(72));

        auto tabs = area.removeFromTop(36).reduced(8, 4);
        for (auto* tab : { &homeTab, &eqTab, &dynTab, &sendsTab })
        {
            tab->setBounds(tabs.removeFromLeft(92));
            tabs.removeFromLeft(6);
        }

        auto body = area.reduced(12, 8);
        const int knobW = 92;

        auto takeKnob = [knobW](juce::Rectangle<int>& row, UnitSlider* slider)
        {
            if (slider != nullptr)
                slider->setBounds(row.removeFromLeft(knobW).reduced(4, 0));
        };
        auto takeButton = [](juce::Rectangle<int>& row, juce::Component* button, int width, int height)
        {
            if (button != nullptr)
                button->setBounds(row.removeFromLeft(width).withSizeKeepingCentre(width - 8, height));
        };

        if (page == Page::channel)
        {
            const int preampW = juce::jmin(250, body.getWidth() / 4);
            const int delayW = juce::jmin(230, body.getWidth() / 4);
            auto preampArea = body.removeFromLeft(preampW).reduced(3);
            auto delayArea = body.removeFromRight(delayW).reduced(3);
            auto configArea = body.reduced(3);
            preampBox.setBounds(preampArea);
            configBox.setBounds(configArea);
            delayBox.setBounds(delayArea);
            preampArea = preampArea.reduced(8, 6);
            preampArea.removeFromTop(16);
            configArea = configArea.reduced(8, 6);
            configArea.removeFromTop(16);
            delayArea = delayArea.reduced(8, 6);
            delayArea.removeFromTop(16);
            takeKnob(preampArea, trim);
            takeButton(preampArea, polarity, 88, 28);
            sourceLabel.setBounds(configArea.removeFromTop(16));
            auto configLine = configArea.removeFromTop(30);
            sourceBox.setBounds(configLine.removeFromLeft(juce::jmin(220, juce::jmax(80, configLine.getWidth() - 84))));
            auto assignLine = configArea.removeFromTop(30);
            takeButton(assignLine, mainAssign, 76, 28);
            takeButton(assignLine, monoAssign, 76, 28);
            takeKnob(configArea, monoLevel);
            takeButton(delayArea, delayOn, 76, 28);
            takeKnob(delayArea, delayTime);
        }
        else if (page == Page::eq)
        {
            auto controls = body.removeFromBottom(juce::jmin(148, juce::jmax(118, body.getHeight() / 3)));
            body.removeFromBottom(4);
            auto eqSwitch = body.removeFromTop(30);
            eqOn->setBounds(eqSwitch.removeFromLeft(72).reduced(0, 2));
            eqGraph.setBounds(body);
            auto hpfArea = controls.removeFromLeft(juce::jmin(168, controls.getWidth() / (numChannelEqBands + 1))).reduced(3);
            hpfBox.setBounds(hpfArea);
            hpfArea = hpfArea.reduced(6, 4);
            hpfArea.removeFromTop(16);
            takeButton(hpfArea, hpfOn, 64, 28);
            takeKnob(hpfArea, hpfFreq);
            const int bandWidth = juce::jmax(120, controls.getWidth() / juce::jmax(1, numChannelEqBands));

            for (int band = 0; band < numChannelEqBands; ++band)
            {
                auto column = controls.removeFromLeft(juce::jmin(bandWidth, controls.getWidth())).reduced(3);
                bandBox[band].setBounds(column);
                column = column.reduced(6, 4);
                column.removeFromTop(16);
                auto header = column.removeFromTop(26);
                eq[band].on->setBounds(header.removeFromLeft(36).reduced(1, 1));
                eq[band].type.setBounds(header.reduced(2, 1));
                const int eqKnob = juce::jmax(52, column.getWidth() / 3);
                eq[band].freq->setBounds(column.removeFromLeft(eqKnob).reduced(1, 0));
                eq[band].gain->setBounds(column.removeFromLeft(eqKnob).reduced(1, 0));
                eq[band].q->setBounds(column.removeFromLeft(eqKnob).reduced(1, 0));
            }
        }
        else if (page == Page::dyn)
        {
            auto gateRow = body.removeFromTop(body.getHeight() / 2);
            body.removeFromTop(4);
            auto placeDynamics = [&](juce::Rectangle<int> row, DynamicsGraph& graph, juce::Component* button, ChoiceBox* mode)
            {
                auto controls = row.removeFromRight(juce::jmax(280, row.getWidth() / 2));
                graph.setBounds(row.reduced(2));
                auto buttons = controls.removeFromTop(42);
                takeButton(buttons, button, 64, 26);

                if (mode != nullptr)
                    mode->setBounds(buttons.removeFromLeft(110).reduced(2, 0));

                return controls;
            };

            auto gateControls = placeDynamics(gateRow, gateGraph, gateOn, gateMode);
            const int gateKnob = juce::jmax(64, gateControls.getWidth() / 5);
            takeKnob(gateControls, gateThr);
            juce::ignoreUnused(gateKnob);
            takeKnob(gateControls, gateRange);
            takeKnob(gateControls, gateAttack);
            takeKnob(gateControls, gateHold);
            takeKnob(gateControls, gateRelease);

            auto compButtons = body.removeFromTop(42);
            takeButton(compButtons, compOn, 64, 26);
            auto choiceWidth = juce::jmax(78, compButtons.getWidth() / 7);
            auto takeChoice = [&](ChoiceBox* choice)
            {
                if (choice != nullptr)
                    choice->setBounds(compButtons.removeFromLeft(choiceWidth).reduced(2, 0));
            };
            takeChoice(compMode);
            takeChoice(compDetect);
            takeChoice(compEnvelope);
            takeChoice(compPosition);
            takeButton(compButtons, compAuto, 64, 26);
            takeButton(compButtons, compFilter, 64, 26);
            takeChoice(compFilterType);
            compGraph.setBounds(body.removeFromLeft(juce::jmax(220, body.getWidth() / 3)).reduced(2));
            const int compKnob = juce::jmax(58, body.getWidth() / 9);
            auto compTop = body.removeFromTop(body.getHeight() / 2);
            auto takeComp = [&](juce::Rectangle<int>& row, UnitSlider* slider)
            {
                if (slider != nullptr)
                    slider->setBounds(row.removeFromLeft(compKnob).reduced(1, 0));
            };
            takeComp(compTop, compThr);
            takeComp(compTop, compRatio);
            takeComp(compTop, compKnee);
            takeComp(compTop, compMakeup);
            takeComp(compTop, compMix);
            takeComp(compTop, compAttack);
            takeComp(compTop, compHold);
            takeComp(compTop, compRelease);
            takeComp(body, compFilterFreq);
        }
        else if (page == Page::sends)
        {
            const int columns = 8;
            const int rows = 2;
            const int cellW = body.getWidth() / columns;
            const int cellH = juce::jmax(96, body.getHeight() / rows);

            for (int bus = 0; bus < numBuses; ++bus)
            {
                const int column = bus % columns;
                const int row = bus / columns;
                auto cell = juce::Rectangle<int>(body.getX() + column * cellW, body.getY() + row * cellH, cellW, cellH).reduced(3);
                sendBox[bus].setBounds(cell);
                cell = cell.reduced(6, 4);
                cell.removeFromTop(16);
                sendPre[bus]->setBounds(cell.removeFromRight(52).removeFromTop(26));
                sendLevel[bus]->setBounds(cell);
            }
        }
    }

private:
    void changeChannel(int channelIndex)
    {
        const int wrapped = (channelIndex % numChannels + numChannels) % numChannels;

        if (owner.onChannelChanged != nullptr)
            owner.onChannelChanged(wrapped);
        else
            setChannel(wrapped);
    }

    void show(Page next)
    {
        page = next;
        const bool channel = page == Page::channel;
        const bool equaliser = page == Page::eq;
        const bool dyn = page == Page::dyn;
        const bool sends = page == Page::sends;

        trim->setVisible(channel);
        polarity->setVisible(channel);
        preampBox.setVisible(channel);
        configBox.setVisible(channel);
        delayBox.setVisible(channel);
        gateBox.setVisible(false);
        hpfOn->setVisible(equaliser);
        eqOn->setVisible(equaliser);
        hpfFreq->setVisible(equaliser);
        hpfBox.setVisible(equaliser);
        sourceLabel.setVisible(channel);
        sourceBox.setVisible(channel);
        mainAssign->setVisible(channel);
        monoAssign->setVisible(channel);
        monoLevel->setVisible(channel);
        delayOn->setVisible(channel);
        delayTime->setVisible(channel);
        gateOn->setVisible(dyn);
        gateMode->setVisible(dyn);
        gateThr->setVisible(dyn);
        gateRange->setVisible(dyn);
        gateAttack->setVisible(dyn);
        gateHold->setVisible(dyn);
        gateRelease->setVisible(dyn);
        gateGraph.setVisible(dyn);
        eqGraph.setVisible(equaliser);
        compGraph.setVisible(dyn);
        compOn->setVisible(dyn);
        compMode->setVisible(dyn);
        compDetect->setVisible(dyn);
        compEnvelope->setVisible(dyn);
        compPosition->setVisible(dyn);
        compAuto->setVisible(dyn);
        compFilter->setVisible(dyn);
        compFilterType->setVisible(dyn);
        compThr->setVisible(dyn);
        compRatio->setVisible(dyn);
        compKnee->setVisible(dyn);
        compMakeup->setVisible(dyn);
        compMix->setVisible(dyn);
        compAttack->setVisible(dyn);
        compHold->setVisible(dyn);
        compRelease->setVisible(dyn);
        compFilterFreq->setVisible(dyn);

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            bandBox[band].setVisible(equaliser);
            eq[band].on->setVisible(equaliser);
            eq[band].type.setVisible(equaliser);
            eq[band].freq->setVisible(equaliser);
            eq[band].gain->setVisible(equaliser);
            eq[band].q->setVisible(equaliser);
        }

        for (int bus = 0; bus < numBuses; ++bus)
        {
            sendBox[bus].setVisible(sends);
            sendLevel[bus]->setVisible(sends);
            sendPre[bus]->setVisible(sends);
        }

        styleTabs();
        resized();
    }

    void styleTabs()
    {
        auto style = [](juce::TextButton& button, bool active, bool lit)
        {
            button.setColour(juce::TextButton::buttonColourId, active ? accent() : lit ? engaged() : juce::Colour(0xff2e343d));
            button.setColour(juce::TextButton::textColourOffId, text());
        };

        style(homeTab, page == Page::channel, gateOn != nullptr && gateOn->isOn());
        style(eqTab, page == Page::eq, eqOn != nullptr && eqOn->isOn());
        style(dynTab, page == Page::dyn, (compOn != nullptr && compOn->isOn()) || (gateOn != nullptr && gateOn->isOn()));
        style(sendsTab, page == Page::sends, false);
    }

    struct EqBand
    {
        FlagButton* on = nullptr;
        juce::ComboBox type;
        UnitSlider* freq = nullptr;
        UnitSlider* gain = nullptr;
        UnitSlider* q = nullptr;
    };

    ChannelEditor& owner;
    ConsoleModel& model;
    int index = 0;
    Page page = Page::channel;
    juce::StringArray sourceNames;

    juce::TextButton previousButton { "<" };
    juce::TextButton nextButton { ">" };
    juce::Label indexLabel;
    juce::Label name;
    juce::Label sourceReadout;
    juce::TextButton linkButton { "Link" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton pasteButton { "Paste" };
    juce::TextButton homeTab { "Channel" };
    juce::TextButton eqTab { "EQ" };
    juce::TextButton dynTab { "Dyn" };
    juce::TextButton sendsTab { "Sends" };

    UnitSlider* trim = nullptr;
    FlagButton* polarity = nullptr;
    FlagButton* hpfOn = nullptr;
    FlagButton* eqOn = nullptr;
    UnitSlider* hpfFreq = nullptr;
    juce::Label sourceLabel;
    juce::ComboBox sourceBox;
    FlagButton* mainAssign = nullptr;
    FlagButton* monoAssign = nullptr;
    UnitSlider* monoLevel = nullptr;
    FlagButton* delayOn = nullptr;
    UnitSlider* delayTime = nullptr;
    FlagButton* gateOn = nullptr;
    ChoiceBox* gateMode = nullptr;
    UnitSlider* gateThr = nullptr;
    UnitSlider* gateRange = nullptr;
    UnitSlider* gateAttack = nullptr;
    UnitSlider* gateHold = nullptr;
    UnitSlider* gateRelease = nullptr;
    EqBand eq[numChannelEqBands];
    FlagButton* compOn = nullptr;
    ChoiceBox* compMode = nullptr;
    ChoiceBox* compDetect = nullptr;
    ChoiceBox* compEnvelope = nullptr;
    ChoiceBox* compPosition = nullptr;
    FlagButton* compAuto = nullptr;
    FlagButton* compFilter = nullptr;
    ChoiceBox* compFilterType = nullptr;
    UnitSlider* compThr = nullptr;
    UnitSlider* compRatio = nullptr;
    UnitSlider* compKnee = nullptr;
    UnitSlider* compMakeup = nullptr;
    UnitSlider* compMix = nullptr;
    UnitSlider* compAttack = nullptr;
    UnitSlider* compHold = nullptr;
    UnitSlider* compRelease = nullptr;
    UnitSlider* compFilterFreq = nullptr;
    ControlBox preampBox;
    ControlBox configBox;
    ControlBox delayBox;
    ControlBox gateBox;
    ControlBox hpfBox;
    ControlBox bandBox[numChannelEqBands];
    ControlBox sendBox[numBuses];
    UnitSlider* sendLevel[numBuses] {};
    FlagButton* sendPre[numBuses] {};
    EqGraph eqGraph;
    DynamicsGraph gateGraph;
    DynamicsGraph compGraph;
    std::vector<std::unique_ptr<ChoiceBox>> choices;
    std::vector<std::unique_ptr<UnitSlider>> sliders;
    std::vector<std::unique_ptr<FlagButton>> flags;
};

ChannelEditor::ChannelEditor(ConsoleModel& modelToUse)
    : model(modelToUse),
      surface(std::make_unique<Surface>(*this, model))
{
    addAndMakeVisible(*surface);
}

ChannelEditor::~ChannelEditor() = default;

void ChannelEditor::setChannel(int channelIndex)
{
    surface->setChannel(channelIndex);
}

int ChannelEditor::getChannel() const noexcept
{
    return surface->getChannel();
}

void ChannelEditor::refresh()
{
    surface->refresh();
}

void ChannelEditor::setSourceNames(const juce::StringArray& names)
{
    surface->setSourceNames(names);
}

void ChannelEditor::resized()
{
    surface->setBounds(getLocalBounds());
}

} // namespace vc
