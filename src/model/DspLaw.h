#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/FaderLaw.h"

#include <cmath>

namespace vc
{

// Published X32-style ranges. The 0..1 float is the value stored in the console tree.
inline float logRange(float unit, float minimum, float maximum) noexcept
{
    return minimum * std::pow(maximum / minimum, clamp01(unit));
}

inline float eqFrequencyHz(float unit) noexcept
{
    return logRange(unit, 20.0f, 20000.0f);
}

inline float eqGainDb(float unit) noexcept
{
    return clamp01(unit) * 30.0f - 15.0f;
}

inline float eqQ(float unit) noexcept
{
    return logRange(unit, 10.0f, 0.3f);
}

inline float unitFromLogRange(float value, float minimum, float maximum) noexcept
{
    const float safe = std::max(value, 1.0e-6f);
    const float span = std::log(maximum / minimum);

    if (std::abs(span) < 1.0e-6f)
        return 0.0f;

    return clamp01(std::log(safe / minimum) / span);
}

inline float eqFrequencyUnit(float hertz) noexcept
{
    return unitFromLogRange(hertz, 20.0f, 20000.0f);
}

inline float hpfFrequencyUnit(float hertz) noexcept
{
    return unitFromLogRange(hertz, 20.0f, 400.0f);
}

inline float eqGainUnit(float decibels) noexcept
{
    return clamp01((decibels + 15.0f) / 30.0f);
}

inline float eqQUnit(float q) noexcept
{
    return unitFromLogRange(q, 10.0f, 0.3f);
}

inline float hpfFrequencyHz(float unit) noexcept
{
    return logRange(unit, 20.0f, 400.0f);
}

inline float delaySeconds(float unit) noexcept
{
    return logRange(unit, 0.0003f, 0.5f);
}

inline float gateThresholdDb(float unit) noexcept
{
    return clamp01(unit) * 80.0f - 80.0f;
}

inline float gateRangeDb(float unit) noexcept
{
    return 3.0f + clamp01(unit) * 57.0f;
}

inline float compThresholdDb(float unit) noexcept
{
    return clamp01(unit) * 60.0f - 60.0f;
}

inline float compRatio(float unit) noexcept
{
    return logRange(unit, 1.1f, 100.0f);
}

inline float compKneeDb(float unit) noexcept
{
    return clamp01(unit) * 5.0f;
}

inline float compMakeupDb(float unit) noexcept
{
    return clamp01(unit) * 24.0f;
}

inline float dynamicsMilliseconds(float unit, float minimum, float maximum) noexcept
{
    return logRange(unit, minimum, maximum);
}

inline float timeCoefficient(float milliseconds, double sampleRate) noexcept
{
    const float seconds = std::max(milliseconds, 0.05f) * 0.001f;
    const float samples = std::max(1.0f, (float) sampleRate * seconds);
    return 1.0f - std::exp(-1.0f / samples);
}

// Gain change in dB for a downward compressor. Negative means attenuation.
inline float compressorReductionDb(float overDb, float ratio, float kneeDb) noexcept
{
    const float slope = 1.0f / std::max(ratio, 1.01f) - 1.0f;

    if (kneeDb <= 0.05f)
        return overDb > 0.0f ? overDb * slope : 0.0f;

    if (overDb > kneeDb * 0.5f)
        return overDb * slope;

    if (overDb > -kneeDb * 0.5f)
    {
        const float soft = overDb + kneeDb * 0.5f;
        return slope * soft * soft / (2.0f * kneeDb);
    }

    return 0.0f;
}

// Output level after compression or downward expansion, including makeup.
inline float compressorOutputDb(float inputDb, int mode, float thresholdDb, float ratio, float kneeDb, float makeupDb) noexcept
{
    if (mode == 0)
        return inputDb + compressorReductionDb(inputDb - thresholdDb, ratio, kneeDb) + makeupDb;

    const float under = thresholdDb - inputDb;
    const float slope = std::max(ratio, 1.01f) - 1.0f;
    float reduction = 0.0f;

    if (kneeDb <= 0.05f)
        reduction = under > 0.0f ? -under * slope : 0.0f;
    else if (under > kneeDb * 0.5f)
        reduction = -under * slope;
    else if (under > -kneeDb * 0.5f)
    {
        const float soft = under + kneeDb * 0.5f;
        reduction = -slope * soft * soft / (2.0f * kneeDb);
    }

    return inputDb + reduction + makeupDb;
}

// Output level for the gate transfer. mode 0 gate, 1..3 expanders, 4 ducker.
inline float gateOutputDb(float inputDb, int mode, float thresholdDb, float rangeDb) noexcept
{
    const float depth = std::max(0.0f, rangeDb);

    if (mode == 4)
        return inputDb >= thresholdDb ? inputDb - depth : inputDb;

    if (inputDb >= thresholdDb || mode == 0)
        return inputDb >= thresholdDb ? inputDb : inputDb - depth;

    const float ratio = mode == 1 ? 2.0f : mode == 2 ? 3.0f : 4.0f;
    const float extra = std::min(depth, (thresholdDb - inputDb) * (ratio - 1.0f));
    return inputDb - extra;
}

} // namespace vc
