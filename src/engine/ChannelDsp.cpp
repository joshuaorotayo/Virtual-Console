// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/ChannelDsp.h"

#include "dsp/FilterDesign.h"
#include "model/DspLaw.h"
#include "model/FaderLaw.h"

#include <algorithm>
#include <cmath>

namespace vc
{
namespace
{
float loadUnit(const std::atomic<float>& value) noexcept
{
    return value.load(std::memory_order_relaxed);
}

void applyCoeffs(ChannelDsp::Biquad& filter, const BiquadCoeffs& coeffs)
{
    filter.setNormalized(coeffs.b0, coeffs.b1, coeffs.b2, coeffs.a1, coeffs.a2);
}

void setHighPass(ChannelDsp::Biquad& filter, float frequency, double sampleRate)
{
    applyCoeffs(filter, designHighPass(frequency, sampleRate));
}

void setLowPass(ChannelDsp::Biquad& filter, float frequency, double sampleRate)
{
    applyCoeffs(filter, designLowPass(frequency, sampleRate));
}

void setPeak(ChannelDsp::Biquad& filter, float frequency, float q, float gainDb, double sampleRate)
{
    applyCoeffs(filter, designPeak(frequency, q, gainDb, sampleRate));
}

void setShelf(ChannelDsp::Biquad& filter, float frequency, float gainDb, bool high, double sampleRate)
{
    applyCoeffs(filter, designShelf(frequency, gainDb, high, sampleRate));
}

float readDelay(const std::vector<float>& line, int write, float samplesBack) noexcept
{
    const int size = (int) line.size();

    if (size <= 1)
        return 0.0f;

    float position = (float) write - samplesBack;

    while (position < 0.0f)
        position += (float) size;

    const int index = (int) position;
    const int next = index + 1 >= size ? 0 : index + 1;
    const float fraction = position - (float) index;
    return line[(size_t) index] * (1.0f - fraction) + line[(size_t) next] * fraction;
}
} // namespace

void ChannelDsp::Biquad::reset() noexcept
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void ChannelDsp::Biquad::setUnity() noexcept
{
    b0 = 1.0f;
    b1 = 0.0f;
    b2 = 0.0f;
    a1 = 0.0f;
    a2 = 0.0f;
}

void ChannelDsp::Biquad::setNormalized(float nb0, float nb1, float nb2, float na1, float na2) noexcept
{
    b0 = nb0;
    b1 = nb1;
    b2 = nb2;
    a1 = na1;
    a2 = na2;
}

float ChannelDsp::Biquad::tick(float sample) noexcept
{
    const float output = b0 * sample + z1;
    z1 = b1 * sample - a1 * output + z2;
    z2 = b2 * sample - a2 * output;
    return output;
}

void ChannelDsp::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    delay.assign((size_t) std::ceil(sampleRate * 0.5) + 8, 0.0f);
    delayWrite = 0;
    hpf.reset();

    for (auto& band : eq)
        band.reset();

    gateEnvelope = 0.0f;
    gateLevel = 1.0f;
    gateHoldLeft = 0;
    compEnvelope = 0.0f;
    compLevel = 1.0f;
}

void ChannelDsp::configure(const Channel& channel) noexcept
{
    delayEnabled = channel.delayOn.load(std::memory_order_relaxed) != 0;
    delaySamples = (float) (delaySeconds(loadUnit(channel.delayTime)) * sampleRate);
    const float polarity = channel.invert.load(std::memory_order_relaxed) != 0 ? -1.0f : 1.0f;
    trimGain = trimToGain(channel.trim.load(std::memory_order_relaxed)) * polarity;

    hpfEnabled = channel.hpfOn.load(std::memory_order_relaxed) != 0;

    if (hpfEnabled)
        setHighPass(hpf, hpfFrequencyHz(loadUnit(channel.hpf)), sampleRate);
    else
        hpf.setUnity();

    for (int band = 0; band < numChannelEqBands; ++band)
    {
        const auto& params = channel.eq[band];
        const float gain = eqGainDb(loadUnit(params.gain));
        const int type = params.type.load(std::memory_order_relaxed);
        const bool enabled = params.on.load(std::memory_order_relaxed) != 0;
        const bool flat = std::abs(gain) < 0.05f && type != 0 && type != 5;

        if (! enabled || flat)
        {
            eq[band].setUnity();
            continue;
        }

        const float frequency = std::clamp(eqFrequencyHz(loadUnit(params.freq)), 20.0f, (float) sampleRate * 0.45f);
        const float q = eqQ(loadUnit(params.q));

        if (type == 0)
            setHighPass(eq[band], frequency, sampleRate);
        else if (type == 1)
            setShelf(eq[band], frequency, gain, false, sampleRate);
        else if (type == 4)
            setShelf(eq[band], frequency, gain, true, sampleRate);
        else if (type == 5)
            setLowPass(eq[band], frequency, sampleRate);
        else
            setPeak(eq[band], frequency, q, gain, sampleRate);
    }

    gateEnabled = channel.gate.on.load(std::memory_order_relaxed) != 0;
    gateThreshold = dbToGain(gateThresholdDb(loadUnit(channel.gate.threshold)));
    gateFloor = dbToGain(-gateRangeDb(loadUnit(channel.gate.range)));
    gateAttack = timeCoefficient(dynamicsMilliseconds(loadUnit(channel.gate.attack), 0.1f, 120.0f), sampleRate);
    gateRelease = timeCoefficient(dynamicsMilliseconds(loadUnit(channel.gate.release), 5.0f, 4000.0f), sampleRate);
    gateHoldSamples = (int) std::lround(dynamicsMilliseconds(loadUnit(channel.gate.hold), 0.1f, 2000.0f) * 0.001 * sampleRate);

    compEnabled = channel.comp.on.load(std::memory_order_relaxed) != 0;
    compThresholdDb = vc::compThresholdDb(loadUnit(channel.comp.threshold));
    compRatio = vc::compRatio(loadUnit(channel.comp.ratio));
    compKnee = compKneeDb(loadUnit(channel.comp.knee));
    compMakeup = dbToGain(compMakeupDb(loadUnit(channel.comp.makeup)));
    compAttack = timeCoefficient(dynamicsMilliseconds(loadUnit(channel.comp.attack), 0.1f, 120.0f), sampleRate);
    compRelease = timeCoefficient(dynamicsMilliseconds(loadUnit(channel.comp.release), 5.0f, 4000.0f), sampleRate);
}

float ChannelDsp::delayTrimAndHpf(float sample) noexcept
{
    float delayed = sample;

    if (delayEnabled && delaySamples > 0.5f && ! delay.empty())
    {
        delayed = readDelay(delay, delayWrite, delaySamples);
        delay[(size_t) delayWrite] = sample;
        ++delayWrite;

        if (delayWrite >= (int) delay.size())
            delayWrite = 0;
    }

    const float trimmed = delayed * trimGain;
    return hpfEnabled ? hpf.tick(trimmed) : trimmed;
}

float ChannelDsp::shape(float sample) noexcept
{
    float shaped = sample;

    for (auto& band : eq)
        shaped = band.tick(shaped);

    return shaped;
}

float ChannelDsp::gateGain(float key) noexcept
{
    if (! gateEnabled)
        return 1.0f;

    const float magnitude = std::abs(key);
    const float follow = magnitude > gateEnvelope ? gateAttack : gateRelease;
    gateEnvelope += (magnitude - gateEnvelope) * follow;

    if (gateEnvelope >= gateThreshold)
    {
        gateHoldLeft = gateHoldSamples;
        gateLevel += (1.0f - gateLevel) * gateAttack;
    }
    else if (gateHoldLeft > 0)
    {
        --gateHoldLeft;
    }
    else
    {
        gateLevel += (gateFloor - gateLevel) * gateRelease;
    }

    return gateLevel;
}

float ChannelDsp::compressorGain(float key) noexcept
{
    if (! compEnabled)
        return 1.0f;

    const float magnitude = std::abs(key);
    const float follow = magnitude > compEnvelope ? compAttack : compRelease;
    compEnvelope += (magnitude - compEnvelope) * follow;
    const float levelDb = 20.0f * std::log10(compEnvelope + 1.0e-8f);
    const float over = levelDb - compThresholdDb;
    const float slope = 1.0f / std::max(compRatio, 1.01f) - 1.0f;
    float reduction = 0.0f;

    if (compKnee <= 0.05f)
    {
        if (over > 0.0f)
            reduction = over * slope;
    }
    else if (over > compKnee * 0.5f)
    {
        reduction = over * slope;
    }
    else if (over > -compKnee * 0.5f)
    {
        const float soft = over + compKnee * 0.5f;
        reduction = slope * soft * soft / (2.0f * compKnee);
    }

    const float target = dbToGain(reduction) * compMakeup;
    const float followGain = target < compLevel ? compAttack : compRelease;
    compLevel += (target - compLevel) * followGain;
    return compLevel;
}

void ChannelDsp::process(const float* input, float* output, int numSamples, const Channel& channel) noexcept
{
    if (output == nullptr || numSamples <= 0)
        return;

    configure(channel);

    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = input != nullptr ? input[i] : 0.0f;
        const float front = delayTrimAndHpf(sample);
        const float gated = front * gateGain(front);
        const float shaped = shape(gated);
        output[i] = shaped * compressorGain(shaped);
    }
}

void ChannelDsp::processLinked(ChannelDsp& partner,
                               const float* inputA,
                               const float* inputB,
                               float* outputA,
                               float* outputB,
                               int numSamples,
                               const Channel& leader) noexcept
{
    if (numSamples <= 0)
        return;

    configure(leader);
    partner.configure(leader);

    for (int i = 0; i < numSamples; ++i)
    {
        const float frontA = delayTrimAndHpf(inputA != nullptr ? inputA[i] : 0.0f);
        const float frontB = partner.delayTrimAndHpf(inputB != nullptr ? inputB[i] : 0.0f);
        const float gate = gateGain(std::max(std::abs(frontA), std::abs(frontB)));
        const float shapedA = shape(frontA * gate);
        const float shapedB = partner.shape(frontB * gate);
        const float compressed = compressorGain(std::max(std::abs(shapedA), std::abs(shapedB)));

        if (outputA != nullptr)
            outputA[i] = shapedA * compressed;

        if (outputB != nullptr)
            outputB[i] = shapedB * compressed;
    }
}

} // namespace vc
