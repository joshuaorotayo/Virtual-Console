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

} // namespace vc
