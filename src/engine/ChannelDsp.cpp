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

void ChannelDsp::ensureCapacity(int samples)
{
    if (samples <= (int) preEq.size())
        return;

    preEq.assign((size_t) samples, 0.0f);
    postEq.assign((size_t) samples, 0.0f);
}

float ChannelDsp::preEqSample(int sample) const noexcept
{
    if (sample < 0 || sample >= (int) preEq.size())
        return 0.0f;

    return preEq[(size_t) sample];
}

float ChannelDsp::postEqSample(int sample) const noexcept
{
    if (sample < 0 || sample >= (int) postEq.size())
        return 0.0f;

    return postEq[(size_t) sample];
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

    eqEnabled = channel.eqOn.load(std::memory_order_relaxed) != 0;

    gateEnabled = channel.gate.on.load(std::memory_order_relaxed) != 0;
    gateMode = std::clamp(channel.gate.mode.load(std::memory_order_relaxed), 0, 4);
    gateThresholdDb = vc::gateThresholdDb(loadUnit(channel.gate.threshold));
    gateRangeDb = vc::gateRangeDb(loadUnit(channel.gate.range));
    gateThreshold = dbToGain(gateThresholdDb);
    gateFloor = dbToGain(-gateRangeDb);
    gateAttack = timeCoefficient(dynamicsMilliseconds(loadUnit(channel.gate.attack), 0.1f, 120.0f), sampleRate);
    gateRelease = timeCoefficient(dynamicsMilliseconds(loadUnit(channel.gate.release), 5.0f, 4000.0f), sampleRate);
    gateHoldSamples = (int) std::lround(dynamicsMilliseconds(loadUnit(channel.gate.hold), 0.1f, 2000.0f) * 0.001 * sampleRate);

    compEnabled = channel.comp.on.load(std::memory_order_relaxed) != 0;
    compMode = channel.comp.mode.load(std::memory_order_relaxed) != 0 ? 1 : 0;
    compPre = channel.comp.position.load(std::memory_order_relaxed) != 0;
    compRms = channel.comp.detect.load(std::memory_order_relaxed) != 0;
    compLog = channel.comp.envelope.load(std::memory_order_relaxed) != 0;
    compThresholdDb = vc::compThresholdDb(loadUnit(channel.comp.threshold));
    compRatio = vc::compRatio(loadUnit(channel.comp.ratio));
    compKnee = compKneeDb(loadUnit(channel.comp.knee));
    compMakeupDb = vc::compMakeupDb(loadUnit(channel.comp.makeup));
    compMix = clamp01(loadUnit(channel.comp.mix));
    const bool automatic = channel.comp.autoTime.load(std::memory_order_relaxed) != 0;
    const float attackMs = automatic ? 10.0f : dynamicsMilliseconds(loadUnit(channel.comp.attack), 0.1f, 120.0f);
    const float releaseMs = automatic ? 120.0f : dynamicsMilliseconds(loadUnit(channel.comp.release), 5.0f, 4000.0f);
    compAttack = timeCoefficient(attackMs, sampleRate);
    compRelease = timeCoefficient(releaseMs, sampleRate);
    compHoldSamples = (int) std::lround(dynamicsMilliseconds(loadUnit(channel.comp.hold), 0.1f, 2000.0f) * 0.001 * sampleRate);
    compFilterEnabled = channel.comp.filterOn.load(std::memory_order_relaxed) != 0;
    configureKeyFilter(channel.comp, compFilterKind, compFilterPole, compKeyFilter);
}

void ChannelDsp::configureKeyFilter(const Dynamics& dynamics, int& kind, float& pole, Biquad& filter) noexcept
{
    const float frequency = eqFrequencyHz(loadUnit(dynamics.filterFreq));
    const int type = std::clamp(dynamics.filterType.load(std::memory_order_relaxed), 0, 8);
    const float coefficient = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * frequency / (float) std::max(sampleRate, 1.0));

    if (type == 0)
    {
        kind = 0;
        pole = coefficient;
        return;
    }

    if (type == 2)
    {
        kind = 1;
        pole = coefficient;
        return;
    }

    kind = 2;

    if (type == 1)
        setHighPass(filter, frequency, sampleRate);
    else if (type == 3)
        setLowPass(filter, frequency, sampleRate);
    else
    {
        const float q = type == 4 ? 1.0f : type == 5 ? 2.0f : type == 6 ? 3.0f : type == 7 ? 5.0f : 10.0f;
        setPeak(filter, frequency, q, 12.0f, sampleRate);
    }
}

float ChannelDsp::filterKey(float sample, bool enabled, int kind, float pole, float& state, Biquad& filter) noexcept
{
    if (! enabled)
        return sample;

    if (kind == 0)
    {
        state += pole * (sample - state);
        return sample - state;
    }

    if (kind == 1)
    {
        state += pole * (sample - state);
        return state;
    }

    return filter.tick(sample);
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
    if (! eqEnabled)
        return sample;

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

    const bool open = gateEnvelope >= gateThreshold;
    float target = open ? 1.0f : gateFloor;

    if (gateMode == 4)
        target = open ? gateFloor : 1.0f;
    else if (gateMode != 0)
    {
        const float levelDb = 20.0f * std::log10(gateEnvelope + 1.0e-8f);
        target = dbToGain(gateOutputDb(levelDb, gateMode, gateThresholdDb, gateRangeDb) - levelDb);
    }

    if (target >= gateLevel)
    {
        gateHoldLeft = gateHoldSamples;
        gateLevel += (target - gateLevel) * gateAttack;
    }
    else if (gateHoldLeft > 0)
    {
        --gateHoldLeft;
    }
    else
    {
        gateLevel += (target - gateLevel) * gateRelease;
    }

    return gateLevel;
}

float ChannelDsp::compressorGain(float key) noexcept
{
    if (! compEnabled)
        return 1.0f;

    const float keyed = filterKey(key, compFilterEnabled, compFilterKind, compFilterPole, compFilterState, compKeyFilter);
    float magnitude = std::abs(keyed);

    if (compRms)
    {
        compPower += (magnitude * magnitude - compPower) * compAttack;
        magnitude = std::sqrt(std::max(compPower, 0.0f));
    }

    if (compLog)
    {
        const float decibels = 20.0f * std::log10(magnitude + 1.0e-8f);
        const float follow = decibels > compEnvDb ? compAttack : compRelease;
        compEnvDb += (decibels - compEnvDb) * follow;
        magnitude = std::pow(10.0f, compEnvDb / 20.0f);
    }
    else
    {
        const float follow = magnitude > compEnvelope ? compAttack : compRelease;
        compEnvelope += (magnitude - compEnvelope) * follow;
        magnitude = compEnvelope;
    }

    const float levelDb = 20.0f * std::log10(magnitude + 1.0e-8f);
    const float outputDb = compressorOutputDb(levelDb, compMode, compThresholdDb, compRatio, compKnee, compMakeupDb);
    const float target = dbToGain(outputDb - levelDb);

    if (target < compLevel)
    {
        compHoldLeft = compHoldSamples;
        compLevel += (target - compLevel) * compAttack;
    }
    else if (compHoldLeft > 0)
    {
        --compHoldLeft;
    }
    else
    {
        compLevel += (target - compLevel) * compRelease;
    }

    return (1.0f - compMix) + compLevel * compMix;
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
        const float beforeEq = compPre ? gated * compressorGain(gated) : gated;
        const float afterEq = shape(beforeEq);
        output[i] = compPre ? afterEq : afterEq * compressorGain(afterEq);

        if (i < (int) preEq.size())
        {
            preEq[(size_t) i] = beforeEq;
            postEq[(size_t) i] = afterEq;
        }
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
        const float gatedA = frontA * gate;
        const float gatedB = frontB * gate;
        const float preGain = compPre ? compressorGain(std::max(std::abs(gatedA), std::abs(gatedB))) : 1.0f;
        const float beforeA = gatedA * preGain;
        const float beforeB = gatedB * preGain;
        const float afterA = shape(beforeA);
        const float afterB = partner.shape(beforeB);
        const float compressed = compPre ? 1.0f : compressorGain(std::max(std::abs(afterA), std::abs(afterB)));

        if (i < (int) preEq.size())
        {
            preEq[(size_t) i] = beforeA;
            postEq[(size_t) i] = afterA;
        }

        if (i < (int) partner.preEq.size())
        {
            partner.preEq[(size_t) i] = beforeB;
            partner.postEq[(size_t) i] = afterB;
        }

        if (outputA != nullptr)
            outputA[i] = afterA * compressed;

        if (outputB != nullptr)
            outputB[i] = afterB * compressed;
    }
}

void GroupDsp::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    for (auto& band : eqL)
        band.reset();

    for (auto& band : eqR)
        band.reset();

    compEnvelope = 0.0f;
    compEnvDb = -80.0f;
    compPower = 0.0f;
    compLevel = 1.0f;
    compHoldLeft = 0;
}

void GroupDsp::configureGate(const Dynamics& gate) noexcept
{
    gateEnabled = gate.on.load(std::memory_order_relaxed) != 0;
    gateMode = std::clamp(gate.mode.load(std::memory_order_relaxed), 0, 4);
    gateThresholdDb = vc::gateThresholdDb(loadUnit(gate.threshold));
    gateRangeDb = vc::gateRangeDb(loadUnit(gate.range));
    gateThreshold = dbToGain(gateThresholdDb);
    gateFloor = dbToGain(-gateRangeDb);
    gateAttack = timeCoefficient(dynamicsMilliseconds(loadUnit(gate.attack), 0.1f, 120.0f), sampleRate);
    gateRelease = timeCoefficient(dynamicsMilliseconds(loadUnit(gate.release), 5.0f, 4000.0f), sampleRate);
    gateHoldSamples = (int) std::lround(dynamicsMilliseconds(loadUnit(gate.hold), 0.1f, 2000.0f) * 0.001 * sampleRate);
}

float GroupDsp::gateGain(float key) noexcept
{
    if (! gateEnabled)
        return 1.0f;

    const float magnitude = std::abs(key);
    const float follow = magnitude > gateEnvelope ? gateAttack : gateRelease;
    gateEnvelope += (magnitude - gateEnvelope) * follow;
    const bool open = gateEnvelope >= gateThreshold;
    float target = open ? 1.0f : gateFloor;

    if (gateMode == 4)
        target = open ? gateFloor : 1.0f;
    else if (gateMode != 0)
    {
        const float levelDb = 20.0f * std::log10(gateEnvelope + 1.0e-8f);
        target = dbToGain(gateOutputDb(levelDb, gateMode, gateThresholdDb, gateRangeDb) - levelDb);
    }

    if (target >= gateLevel)
    {
        gateHoldLeft = gateHoldSamples;
        gateLevel += (target - gateLevel) * gateAttack;
    }
    else if (gateHoldLeft > 0)
    {
        --gateHoldLeft;
    }
    else
    {
        gateLevel += (target - gateLevel) * gateRelease;
    }

    return std::clamp(gateLevel, 0.0f, 4.0f);
}

void GroupDsp::configure(const MixPoint& point) noexcept
{
    gateEnabled = false;
    eqEnabled = point.eqMaster.load(std::memory_order_relaxed) != 0;

    for (int band = 0; band < numEqBands; ++band)
    {
        const auto& params = point.eq[band];
        const float gain = eqGainDb(loadUnit(params.gain));
        const int type = params.type.load(std::memory_order_relaxed);
        const bool enabled = params.on.load(std::memory_order_relaxed) != 0;
        const bool flat = std::abs(gain) < 0.05f && type != 0 && type != 5;

        if (! enabled || flat)
        {
            eqL[band].setUnity();
            eqR[band].setUnity();
            continue;
        }

        const float frequency = std::clamp(eqFrequencyHz(loadUnit(params.freq)), 20.0f, (float) sampleRate * 0.45f);
        const float q = eqQ(loadUnit(params.q));

        auto design = [&](ChannelDsp::Biquad& filter)
        {
            if (type == 0)
                setHighPass(filter, frequency, sampleRate);
            else if (type == 1)
                setShelf(filter, frequency, gain, false, sampleRate);
            else if (type == 4)
                setShelf(filter, frequency, gain, true, sampleRate);
            else if (type == 5)
                setLowPass(filter, frequency, sampleRate);
            else
                setPeak(filter, frequency, q, gain, sampleRate);
        };

        design(eqL[band]);
        design(eqR[band]);
    }

    const auto& comp = point.comp;
    compEnabled = comp.on.load(std::memory_order_relaxed) != 0;
    compMode = comp.mode.load(std::memory_order_relaxed) != 0 ? 1 : 0;
    compPre = comp.position.load(std::memory_order_relaxed) != 0;
    compRms = comp.detect.load(std::memory_order_relaxed) != 0;
    compLog = comp.envelope.load(std::memory_order_relaxed) != 0;
    compThresholdDb = vc::compThresholdDb(loadUnit(comp.threshold));
    compRatio = vc::compRatio(loadUnit(comp.ratio));
    compKnee = compKneeDb(loadUnit(comp.knee));
    compMakeupDb = vc::compMakeupDb(loadUnit(comp.makeup));
    compMix = clamp01(loadUnit(comp.mix));
    const bool automatic = comp.autoTime.load(std::memory_order_relaxed) != 0;
    const float attackMs = automatic ? 10.0f : dynamicsMilliseconds(loadUnit(comp.attack), 0.1f, 120.0f);
    const float releaseMs = automatic ? 120.0f : dynamicsMilliseconds(loadUnit(comp.release), 5.0f, 4000.0f);
    compAttack = timeCoefficient(attackMs, sampleRate);
    compRelease = timeCoefficient(releaseMs, sampleRate);
    compHoldSamples = (int) std::lround(dynamicsMilliseconds(loadUnit(comp.hold), 0.1f, 2000.0f) * 0.001 * sampleRate);
}

float GroupDsp::compressorGain(float key) noexcept
{
    if (! compEnabled)
        return 1.0f;

    float magnitude = std::abs(key);

    if (compRms)
    {
        compPower += (magnitude * magnitude - compPower) * compAttack;
        magnitude = std::sqrt(std::max(compPower, 0.0f));
    }

    if (compLog)
    {
        const float decibels = 20.0f * std::log10(magnitude + 1.0e-8f);
        const float follow = decibels > compEnvDb ? compAttack : compRelease;
        compEnvDb += (decibels - compEnvDb) * follow;
        magnitude = std::pow(10.0f, compEnvDb / 20.0f);
    }
    else
    {
        const float follow = magnitude > compEnvelope ? compAttack : compRelease;
        compEnvelope += (magnitude - compEnvelope) * follow;
        magnitude = compEnvelope;
    }

    const float levelDb = 20.0f * std::log10(magnitude + 1.0e-8f);
    const float outputDb = compressorOutputDb(levelDb, compMode, compThresholdDb, compRatio, compKnee, compMakeupDb);
    const float target = dbToGain(outputDb - levelDb);

    if (target < compLevel)
    {
        compHoldLeft = compHoldSamples;
        compLevel += (target - compLevel) * compAttack;
    }
    else if (compHoldLeft > 0)
    {
        --compHoldLeft;
    }
    else
    {
        compLevel += (target - compLevel) * compRelease;
    }

    return (1.0f - compMix) + compLevel * compMix;
}

void GroupDsp::process(float& left, float& right) noexcept
{
    auto shape = [](ChannelDsp::Biquad* bands, bool enabled, float sample)
    {
        if (! enabled)
            return sample;

        for (int band = 0; band < numEqBands; ++band)
            sample = bands[band].tick(sample);

        return sample;
    };

    if (gateEnabled)
    {
        const float gain = gateGain(std::max(std::abs(left), std::abs(right)));
        left *= gain;
        right *= gain;
    }

    if (compPre)
    {
        const float gain = compressorGain(std::max(std::abs(left), std::abs(right)));
        left *= gain;
        right *= gain;
    }

    left = shape(eqL, eqEnabled, left);
    right = shape(eqR, eqEnabled, right);

    if (! compPre)
    {
        const float gain = compressorGain(std::max(std::abs(left), std::abs(right)));
        left *= gain;
        right *= gain;
    }
}

} // namespace vc
