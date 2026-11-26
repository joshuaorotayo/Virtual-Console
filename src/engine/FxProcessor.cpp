// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/FxProcessor.h"

#include "dsp/FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace vc
{
namespace
{
constexpr int roomCombs[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
constexpr int roomAllpass[4] = { 225, 341, 441, 556 };
constexpr int stereoSpread = 23;

int scaled(int samples, double sampleRate, float size)
{
    const float rate = (float) sampleRate / 44100.0f;
    return std::max(1, (int) std::lround((float) samples * rate * size));
}

float param(const FxSlot& slot, int index) noexcept
{
    return slot.par[index].load(std::memory_order_relaxed);
}

constexpr float twoPi = 6.2831853f;

float wrapPhase(float phase) noexcept
{
    if (phase >= 1.0f)
        phase -= 1.0f;

    return phase;
}

float sineUnit(float phase) noexcept
{
    return 0.5f + 0.5f * std::sin(phase * twoPi);
}

float allpass(float input, float& state, float coefficient) noexcept
{
    const float output = state - coefficient * input;
    state = input + coefficient * output;
    return output;
}
} // namespace

const char* fxAlgoName(int type) noexcept
{
    switch (type)
    {
        case (int) FxAlgo::vintageRoom: return "Vintage Room";
        case (int) FxAlgo::hall: return "Hall";
        case (int) FxAlgo::stereoDelay: return "Stereo Delay";
        case (int) FxAlgo::stereoChorus: return "Stereo Chorus";
        case (int) FxAlgo::stereoFlanger: return "Stereo Flanger";
        case (int) FxAlgo::stereoPhaser: return "Stereo Phaser";
        case (int) FxAlgo::tremoloPanner: return "Tremolo / Panner";
        case (int) FxAlgo::graphicEq: return "Graphic EQ";
        case (int) FxAlgo::dualGraphicEq: return "Dual Graphic EQ";
        default: return "Empty";
    }
}

float graphicEqFrequency(int band) noexcept
{
    static constexpr float frequencies[graphicEqBands] = {
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
        200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f,
        2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f,
        20000.0f
    };

    return frequencies[(size_t) std::clamp(band, 0, graphicEqBands - 1)];
}

float graphicEqGainDb(float unit) noexcept
{
    return (std::clamp(unit, 0.0f, 1.0f) - 0.5f) * 30.0f;
}

void storeFxDefaults(FxSlot& slot, int type) noexcept
{
    const int algo = std::clamp(type, 0, (int) FxAlgo::dualGraphicEq);
    slot.type.store(algo, std::memory_order_relaxed);

    if (fxIsGraphicEq(algo))
    {
        for (auto& value : slot.par)
            value.store(0.5f, std::memory_order_relaxed);

        return;
    }

    float primary = 0.5f;
    float secondary = 0.5f;
    float size = 0.5f;
    float damp = 0.4f;

    switch (algo)
    {
        case (int) FxAlgo::vintageRoom:
            primary = 0.42f;
            secondary = 0.08f;
            size = 0.48f;
            damp = 0.42f;
            break;
        case (int) FxAlgo::hall:
            primary = 0.68f;
            secondary = 0.12f;
            size = 0.82f;
            damp = 0.36f;
            break;
        case (int) FxAlgo::stereoDelay:
            primary = 0.38f;
            secondary = 0.35f;
            size = 0.5f;
            damp = 0.25f;
            break;
        case (int) FxAlgo::stereoChorus:
            primary = 0.28f;
            secondary = 0.45f;
            size = 0.5f;
            damp = 0.2f;
            break;
        case (int) FxAlgo::stereoFlanger:
            primary = 0.22f;
            secondary = 0.55f;
            size = 0.5f;
            damp = 0.35f;
            break;
        case (int) FxAlgo::stereoPhaser:
            primary = 0.24f;
            secondary = 0.7f;
            size = 0.28f;
            damp = 0.45f;
            break;
        case (int) FxAlgo::tremoloPanner:
            primary = 0.32f;
            secondary = 0.6f;
            size = 0.2f;
            damp = 0.0f;
            break;
        default:
            break;
    }

    slot.par[fxParPrimary].store(primary, std::memory_order_relaxed);
    slot.par[fxParSecondary].store(secondary, std::memory_order_relaxed);
    slot.par[fxParSize].store(size, std::memory_order_relaxed);
    slot.par[fxParDamp].store(damp, std::memory_order_relaxed);
}

void FxRack::Line::assign(int samples)
{
    buffer.assign((size_t) std::max(2, samples), 0.0f);
    write = 0;
}

float FxRack::Line::read(int delay) const noexcept
{
    const int size = (int) buffer.size();

    if (size == 0)
        return 0.0f;

    int index = write - std::clamp(delay, 0, size - 1);

    if (index < 0)
        index += size;

    return buffer[(size_t) index];
}

void FxRack::Line::writeSample(float sample) noexcept
{
    if (buffer.empty())
        return;

    buffer[(size_t) write] = sample;
    write = (write + 1) % (int) buffer.size();
}

void FxRack::Voice::prepare(double sampleRate)
{
    const float longest = 1.9f;

    for (int i = 0; i < 8; ++i)
    {
        const int samples = scaled(roomCombs[i], sampleRate, longest) + stereoSpread + 8;
        combL[(size_t) i].assign(samples);
        combR[(size_t) i].assign(samples);
        combFilterL[(size_t) i] = 0.0f;
        combFilterR[(size_t) i] = 0.0f;
    }

    for (int i = 0; i < 4; ++i)
    {
        const int samples = scaled(roomAllpass[i], sampleRate, longest) + 8;
        allpassL[(size_t) i].assign(samples);
        allpassR[(size_t) i].assign(samples);
    }

    preL.assign((int) std::lround(sampleRate * 0.25) + 2);
    preR.assign((int) std::lround(sampleRate * 0.25) + 2);
    delayL.assign((int) std::lround(sampleRate * 1.5) + 2);
    delayR.assign((int) std::lround(sampleRate * 1.5) + 2);
    delayFilterL = 0.0f;
    delayFilterR = 0.0f;
    chorusPhase = 0.0f;
    phaserL.fill(0.0f);
    phaserR.fill(0.0f);
    phaserHoldL = 0.0f;
    phaserHoldR = 0.0f;
}

void FxRack::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& voice : voices)
        voice.prepare(sampleRate);

    for (auto& voice : graphic)
        voice.reset();
}

void FxRack::GraphicBand::setUnity() noexcept
{
    b0 = 1.0f;
    b1 = 0.0f;
    b2 = 0.0f;
    a1 = 0.0f;
    a2 = 0.0f;
    active = false;
}

void FxRack::GraphicBand::setNormalized(float nb0, float nb1, float nb2, float na1, float na2) noexcept
{
    b0 = nb0;
    b1 = nb1;
    b2 = nb2;
    a1 = na1;
    a2 = na2;
    active = true;
}

float FxRack::GraphicBand::tick(float sample) noexcept
{
    if (! active)
        return sample;

    const float output = b0 * sample + z1;
    z1 = b1 * sample - a1 * output + z2;
    z2 = b2 * sample - a2 * output;
    return output;
}

void FxRack::GraphicVoice::reset() noexcept
{
    for (auto& band : left)
    {
        band.setUnity();
        band.z1 = 0.0f;
        band.z2 = 0.0f;
    }

    for (auto& band : right)
    {
        band.setUnity();
        band.z1 = 0.0f;
        band.z2 = 0.0f;
    }
}

void FxRack::GraphicVoice::process(float& leftSample, float& rightSample) noexcept
{
    for (auto& band : left)
        leftSample = band.tick(leftSample);

    for (auto& band : right)
        rightSample = band.tick(rightSample);
}

void FxRack::prepareInsert(int slot, const FxSlot& settings) noexcept
{
    if (slot < 0 || slot >= numFxSlots)
        return;

    const int type = settings.type.load(std::memory_order_relaxed);
    auto& voice = graphic[(size_t) slot];
    const bool dual = type == (int) FxAlgo::dualGraphicEq;
    constexpr float bandQ = 4.32f;

    for (int band = 0; band < graphicEqBands; ++band)
    {
        const float leftDb = graphicEqGainDb(param(settings, band));
        const float rightDb = graphicEqGainDb(param(settings, dual ? graphicEqBands + band : band));
        const float frequency = graphicEqFrequency(band);
        auto load = [&](GraphicBand& filter, float gainDb)
        {
            if (std::abs(gainDb) < 0.2f)
            {
                filter.setUnity();
                return;
            }

            const auto coeffs = designPeak(frequency, bandQ, gainDb, sampleRate);
            filter.setNormalized(coeffs.b0, coeffs.b1, coeffs.b2, coeffs.a1, coeffs.a2);
        };
        load(voice.left[(size_t) band], leftDb);
        load(voice.right[(size_t) band], rightDb);
    }
}

void FxRack::processInsert(int slot, float& left, float& right) noexcept
{
    if (slot < 0 || slot >= numFxSlots)
        return;

    graphic[(size_t) slot].process(left, right);
}

void FxRack::process(int slot, const FxSlot& settings, float inL, float inR, float& outL, float& outR) noexcept
{
    outL = 0.0f;
    outR = 0.0f;

    if (slot < 0 || slot >= (int) voices.size() || settings.on.load(std::memory_order_relaxed) == 0)
        return;

    voices[(size_t) slot].process(settings, sampleRate, inL, inR, outL, outR);
}

void FxRack::Voice::process(const FxSlot& settings, double rate, float inL, float inR, float& outL, float& outR) noexcept
{
    const int type = settings.type.load(std::memory_order_relaxed);

    if (type == (int) FxAlgo::stereoDelay)
    {
        const int leftDelay = (int) std::lround(param(settings, fxParPrimary) * rate);
        const float rightFactor = 0.5f + param(settings, fxParSize);
        const int rightDelay = (int) std::lround((float) leftDelay * rightFactor);
        const float feedback = std::clamp(param(settings, fxParSecondary) * 0.9f, 0.0f, 0.9f);
        const float damp = std::clamp(param(settings, fxParDamp), 0.0f, 0.95f);

        if (leftDelay <= 0 && rightDelay <= 0)
        {
            outL = inL;
            outR = inR;
            return;
        }

        const float delayedL = delayL.read(std::max(1, leftDelay));
        const float delayedR = delayR.read(std::max(1, rightDelay));
        delayFilterL = delayedL * (1.0f - damp) + delayFilterL * damp;
        delayFilterR = delayedR * (1.0f - damp) + delayFilterR * damp;
        delayL.writeSample(inL + delayFilterL * feedback);
        delayR.writeSample(inR + delayFilterR * feedback);
        outL = delayedL;
        outR = delayedR;
        return;
    }

    if (type == (int) FxAlgo::stereoChorus)
    {
        const float speed = 0.15f + param(settings, fxParPrimary) * 4.5f;
        const float depthMs = 1.0f + param(settings, fxParSecondary) * 7.0f;
        chorusPhase += speed / (float) rate;

        if (chorusPhase >= 1.0f)
            chorusPhase -= 1.0f;

        const float leftWave = 0.5f + 0.5f * std::sin(chorusPhase * 6.2831853f);
        const float rightWave = 0.5f + 0.5f * std::sin((chorusPhase + 0.25f) * 6.2831853f);
        const int base = std::max(1, (int) std::lround(0.008 * rate));
        const int leftTap = base + (int) std::lround(leftWave * depthMs * 0.001 * rate);
        const int rightTap = base + (int) std::lround(rightWave * depthMs * 0.001 * rate);
        delayL.writeSample(inL);
        delayR.writeSample(inR);
        outL = delayL.read(leftTap);
        outR = delayR.read(rightTap);
        return;
    }

    if (type == (int) FxAlgo::stereoFlanger)
    {
        processFlanger(settings, rate, inL, inR, outL, outR);
        return;
    }

    if (type == (int) FxAlgo::stereoPhaser)
    {
        processPhaser(settings, rate, inL, inR, outL, outR);
        return;
    }

    if (type == (int) FxAlgo::tremoloPanner)
    {
        processTremolo(settings, rate, inL, inR, outL, outR);
        return;
    }

    if (type != (int) FxAlgo::vintageRoom && type != (int) FxAlgo::hall)
        return;

    const float size = type == (int) FxAlgo::hall ? 1.15f + param(settings, fxParSize) * 0.6f
                                                  : 0.72f + param(settings, fxParSize) * 0.45f;
    const float feedback = std::clamp(0.28f + param(settings, fxParPrimary) * 0.64f, 0.0f, 0.93f);
    const float damp = std::clamp(param(settings, fxParDamp) * 0.85f, 0.0f, 0.85f);
    const int preDelay = (int) std::lround(param(settings, fxParSecondary) * 0.2 * rate);
    preL.writeSample(inL);
    preR.writeSample(inR);
    const float roomL = preL.read(preDelay);
    const float roomR = preR.read(preDelay);
    float sumL = 0.0f;
    float sumR = 0.0f;

    for (int i = 0; i < 8; ++i)
    {
        const int length = scaled(roomCombs[i], rate, size);
        auto tick = [&](Line& line, float& filter, float input, int extra)
        {
            const float delayed = line.read(length + extra);
            filter = delayed * (1.0f - damp) + filter * damp;
            line.writeSample(input + filter * feedback);
            return delayed;
        };
        sumL += tick(combL[(size_t) i], combFilterL[(size_t) i], roomL, 0);
        sumR += tick(combR[(size_t) i], combFilterR[(size_t) i], roomR, stereoSpread);
    }

    sumL *= 0.125f;
    sumR *= 0.125f;

    for (int i = 0; i < 4; ++i)
    {
        const int length = scaled(roomAllpass[i], rate, size);
        auto diffuse = [&](Line& line, float input)
        {
            const float buffered = line.read(length);
            const float output = buffered - input;
            line.writeSample(input + buffered * 0.5f);
            return output;
        };
        sumL = diffuse(allpassL[(size_t) i], sumL);
        sumR = diffuse(allpassR[(size_t) i], sumR);
    }

    outL = sumL;
    outR = sumR;
}

void FxRack::Voice::processFlanger(const FxSlot& settings, double rate, float inL, float inR, float& outL, float& outR) noexcept
{
    const float speed = 0.05f + param(settings, fxParPrimary) * 5.0f;
    const float depthMs = param(settings, fxParSecondary) * 4.0f;
    const float feedback = std::clamp((param(settings, fxParSize) - 0.5f) * 1.5f, -0.75f, 0.75f);
    const float manualMs = 0.3f + param(settings, fxParDamp) * 6.0f;
    chorusPhase = wrapPhase(chorusPhase + speed / (float) rate);

    const float leftWave = sineUnit(chorusPhase);
    const float rightWave = sineUnit(chorusPhase + 0.5f);
    const int leftTap = std::max(1, (int) std::lround((manualMs + leftWave * depthMs) * 0.001 * rate));
    const int rightTap = std::max(1, (int) std::lround((manualMs + rightWave * depthMs) * 0.001 * rate));
    const float delayedL = delayL.read(leftTap);
    const float delayedR = delayR.read(rightTap);
    delayL.writeSample(inL + delayedL * feedback);
    delayR.writeSample(inR + delayedR * feedback);
    outL = delayedL;
    outR = delayedR;
}

void FxRack::Voice::processPhaser(const FxSlot& settings, double rate, float inL, float inR, float& outL, float& outR) noexcept
{
    const float speed = 0.05f + param(settings, fxParPrimary) * 4.0f;
    const float depth = std::clamp(param(settings, fxParSecondary), 0.0f, 1.0f);
    const float feedback = std::clamp(param(settings, fxParSize) * 0.6f, 0.0f, 0.6f);
    const float spread = std::clamp(param(settings, fxParDamp), 0.0f, 1.0f);
    chorusPhase = wrapPhase(chorusPhase + speed / (float) rate);

    auto coefficient = [&](float wave)
    {
        const float sweep = (1.0f - depth) * 0.5f + depth * wave;
        return std::clamp(0.08f + spread * 0.5f + sweep * 0.34f, 0.05f, 0.92f);
    };

    auto sweep = [&](float input, float coefficientValue, std::array<float, 4>& state, float& held)
    {
        float sample = input + held * feedback;

        for (float& stage : state)
            sample = allpass(sample, stage, coefficientValue);

        held = sample;
        return sample;
    };

    outL = sweep(inL, coefficient(sineUnit(chorusPhase)), phaserL, phaserHoldL);
    outR = sweep(inR, coefficient(sineUnit(chorusPhase + 0.5f)), phaserR, phaserHoldR);
}

void FxRack::Voice::processTremolo(const FxSlot& settings, double rate, float inL, float inR, float& outL, float& outR) noexcept
{
    const float speed = 0.1f + param(settings, fxParPrimary) * 10.0f;
    const float depth = std::clamp(param(settings, fxParSecondary), 0.0f, 1.0f);
    const float offset = std::clamp(param(settings, fxParSize), 0.0f, 1.0f) * 0.5f;
    const bool pan = param(settings, fxParDamp) >= 0.5f;
    chorusPhase = wrapPhase(chorusPhase + speed / (float) rate);

    const float leftWave = sineUnit(chorusPhase);
    const float rightWave = sineUnit(chorusPhase + (pan ? 0.5f : offset));
    const float leftGain = (1.0f - depth) + depth * leftWave;
    const float rightGain = (1.0f - depth) + depth * rightWave;
    outL = inL * leftGain;
    outR = inR * rightGain;
}

} // namespace vc
