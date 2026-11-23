#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>

namespace vc
{

inline float clamp01(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

// X32 fader float. 0.75 is 0 dB and 1.0 is +10 dB. Zero is fully off.
inline float faderToDb(float fader) noexcept
{
    const float f = clamp01(fader);

    if (f <= 0.0f)
        return -144.0f;
    if (f >= 0.5f)
        return f * 40.0f - 30.0f;
    if (f >= 0.25f)
        return f * 80.0f - 50.0f;
    if (f >= 0.0625f)
        return f * 160.0f - 70.0f;

    return f * 480.0f - 90.0f;
}

inline float dbToFader(float db) noexcept
{
    float fader = 0.0f;

    if (db <= -90.0f)
        fader = 0.0f;
    else if (db < -60.0f)
        fader = (db + 90.0f) / 480.0f;
    else if (db < -30.0f)
        fader = (db + 70.0f) / 160.0f;
    else if (db < -10.0f)
        fader = (db + 50.0f) / 80.0f;
    else
        fader = (db + 30.0f) / 40.0f;

    return clamp01(fader);
}

inline float dbToGain(float db) noexcept
{
    if (db <= -120.0f)
        return 0.0f;

    return std::pow(10.0f, db * 0.05f);
}

inline float faderToGain(float fader) noexcept
{
    return dbToGain(faderToDb(fader));
}

// Digital trim float 0..1 maps linearly across -18..+18 dB.
inline float trimToDb(float trim) noexcept
{
    return clamp01(trim) * 36.0f - 18.0f;
}

inline float trimToGain(float trim) noexcept
{
    return dbToGain(trimToDb(trim));
}

// Equal-power pan. 0 is hard left, 0.5 is centre, 1 is hard right.
inline void panGains(float pan, float& left, float& right) noexcept
{
    const float angle = clamp01(pan) * 1.5707963267948966f;
    left = std::cos(angle);
    right = std::sin(angle);
}

} // namespace vc
