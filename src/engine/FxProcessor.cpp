// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/FxProcessor.h"

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
} // namespace

const char* fxAlgoName(int type) noexcept
{
    switch (type)
    {
        case (int) FxAlgo::vintageRoom: return "Vintage Room";
        case (int) FxAlgo::hall: return "Hall";
        case (int) FxAlgo::stereoDelay: return "Stereo Delay";
        case (int) FxAlgo::stereoChorus: return "Stereo Chorus";
        default: return "Empty";
    }
}

void storeFxDefaults(FxSlot& slot, int type) noexcept
{
    const int algo = std::clamp(type, 0, (int) FxAlgo::stereoChorus);
    slot.type.store(algo, std::memory_order_relaxed);

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
}

void FxRack::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& voice : voices)
        voice.prepare(sampleRate);
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

} // namespace vc
