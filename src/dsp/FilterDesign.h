#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include <cmath>

namespace vc
{

struct BiquadCoeffs
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

inline float designFrequency(float frequency, double sampleRate) noexcept
{
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    return std::clamp(frequency, 10.0f, (float) rate * 0.45f);
}

inline BiquadCoeffs designHighPass(float frequency, double sampleRate) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    const float w0 = 2.0f * pi * designFrequency(frequency, rate) / (float) rate;
    const float cosine = std::cos(w0);
    const float sine = std::sin(w0);
    const float alpha = sine * 0.70710678118f;
    const float a0 = 1.0f + alpha;
    return { (1.0f + cosine) * 0.5f / a0,
             -(1.0f + cosine) / a0,
             (1.0f + cosine) * 0.5f / a0,
             -2.0f * cosine / a0,
             (1.0f - alpha) / a0 };
}

inline BiquadCoeffs designLowPass(float frequency, double sampleRate) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    const float w0 = 2.0f * pi * designFrequency(frequency, rate) / (float) rate;
    const float cosine = std::cos(w0);
    const float sine = std::sin(w0);
    const float alpha = sine * 0.70710678118f;
    const float a0 = 1.0f + alpha;
    return { (1.0f - cosine) * 0.5f / a0,
             (1.0f - cosine) / a0,
             (1.0f - cosine) * 0.5f / a0,
             -2.0f * cosine / a0,
             (1.0f - alpha) / a0 };
}

inline BiquadCoeffs designPeak(float frequency, float q, float gainDb, double sampleRate) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    const float amplitude = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * pi * designFrequency(frequency, rate) / (float) rate;
    const float cosine = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * std::max(q, 0.05f));
    const float a0 = 1.0f + alpha / amplitude;
    return { (1.0f + alpha * amplitude) / a0,
             (-2.0f * cosine) / a0,
             (1.0f - alpha * amplitude) / a0,
             (-2.0f * cosine) / a0,
             (1.0f - alpha / amplitude) / a0 };
}

inline BiquadCoeffs designShelf(float frequency, float gainDb, bool high, double sampleRate) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    const float amplitude = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * pi * designFrequency(frequency, rate) / (float) rate;
    const float cosine = std::cos(w0);
    const float sine = std::sin(w0);
    const float alpha = sine * 0.5f * std::sqrt(2.0f);
    const float twoRoot = 2.0f * std::sqrt(amplitude) * alpha;
    const float a0 = high ? (amplitude + 1.0f) - (amplitude - 1.0f) * cosine + twoRoot
                          : (amplitude + 1.0f) + (amplitude - 1.0f) * cosine + twoRoot;
    const float b0 = high ? amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine + twoRoot)
                          : amplitude * ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine + twoRoot);
    const float b1 = high ? -2.0f * amplitude * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosine)
                          : 2.0f * amplitude * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosine);
    const float b2 = high ? amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine - twoRoot)
                          : amplitude * ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine - twoRoot);
    const float a1 = high ? 2.0f * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosine)
                          : -2.0f * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosine);
    const float a2 = high ? (amplitude + 1.0f) - (amplitude - 1.0f) * cosine - twoRoot
                          : (amplitude + 1.0f) + (amplitude - 1.0f) * cosine - twoRoot;
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline float biquadMagnitudeDb(const BiquadCoeffs& coeffs, float frequency, double sampleRate) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const double rate = sampleRate > 1.0 ? sampleRate : 48000.0;
    const float w = 2.0f * pi * designFrequency(frequency, rate) / (float) rate;
    const float cosine = std::cos(w);
    const float sine = std::sin(w);
    const float cosine2 = std::cos(2.0f * w);
    const float sine2 = std::sin(2.0f * w);
    const float numeratorReal = coeffs.b0 + coeffs.b1 * cosine + coeffs.b2 * cosine2;
    const float numeratorImag = coeffs.b1 * sine + coeffs.b2 * sine2;
    const float denominatorReal = 1.0f + coeffs.a1 * cosine + coeffs.a2 * cosine2;
    const float denominatorImag = coeffs.a1 * sine + coeffs.a2 * sine2;
    const float numerator = numeratorReal * numeratorReal + numeratorImag * numeratorImag;
    const float denominator = std::max(1.0e-12f, denominatorReal * denominatorReal + denominatorImag * denominatorImag);
    return 20.0f * std::log10(std::sqrt(numerator / denominator));
}

} // namespace vc
