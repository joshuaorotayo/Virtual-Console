#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleTypes.h"

#include <vector>

namespace vc
{

// One channel strip: delay, trim, high-pass, gate, four-band EQ, compressor.
// State lives here. Parameters are read from the console at each block.
class ChannelDsp
{
public:
    struct Biquad
    {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        void reset() noexcept;
        void setUnity() noexcept;
        void setNormalized(float nb0, float nb1, float nb2, float na1, float na2) noexcept;
        float tick(float sample) noexcept;
    };

    void prepare(double newSampleRate);
    void process(const float* input, float* output, int numSamples, const Channel& channel) noexcept;
    void processLinked(ChannelDsp& partner,
                       const float* inputA,
                       const float* inputB,
                       float* outputA,
                       float* outputB,
                       int numSamples,
                       const Channel& leader) noexcept;

private:
    void configure(const Channel& channel) noexcept;
    float delayTrimAndHpf(float sample) noexcept;
    float shape(float sample) noexcept;
    float gateGain(float key) noexcept;
    float compressorGain(float key) noexcept;

    double sampleRate = 48000.0;
    std::vector<float> delay;
    int delayWrite = 0;
    float delaySamples = 0.0f;
    bool delayEnabled = false;
    float trimGain = 1.0f;
    bool hpfEnabled = false;
    Biquad hpf;
    Biquad eq[numChannelEqBands];
    bool gateEnabled = false;
    float gateThreshold = 0.1f;
    float gateFloor = 0.001f;
    float gateAttack = 1.0f;
    float gateRelease = 1.0f;
    int gateHoldSamples = 0;
    int gateHoldLeft = 0;
    float gateEnvelope = 0.0f;
    float gateLevel = 1.0f;
    bool compEnabled = false;
    float compThresholdDb = 0.0f;
    float compRatio = 1.0f;
    float compKnee = 0.0f;
    float compMakeup = 1.0f;
    float compAttack = 1.0f;
    float compRelease = 1.0f;
    float compEnvelope = 0.0f;
    float compLevel = 1.0f;
};

} // namespace vc
