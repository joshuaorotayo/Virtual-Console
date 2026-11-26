#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleTypes.h"

#include <array>
#include <vector>

namespace vc
{

// Send effects for slots 1–4. These are equivalent algorithms, not a copy of
// another mixer's DSP. Slots 5–8 stay empty until insert effects are processed.
constexpr int fxParPrimary = 0;
constexpr int fxParSecondary = 1;
constexpr int fxParSize = 2;
constexpr int fxParDamp = 3;

enum class FxAlgo : int
{
    empty = 0,
    vintageRoom = 1,
    hall = 2,
    stereoDelay = 3,
    stereoChorus = 4,
    stereoFlanger = 5,
    stereoPhaser = 6,
    tremoloPanner = 7
};

const char* fxAlgoName(int type) noexcept;
void storeFxDefaults(FxSlot& slot, int type) noexcept;

class FxRack
{
public:
    void prepare(double newSampleRate);
    void process(int slot, const FxSlot& settings, float inL, float inR, float& outL, float& outR) noexcept;

private:
    struct Line
    {
        std::vector<float> buffer;
        int write = 0;

        void assign(int samples);
        float read(int delay) const noexcept;
        void writeSample(float sample) noexcept;
    };

    struct Voice
    {
        std::array<Line, 8> combL;
        std::array<Line, 8> combR;
        std::array<float, 8> combFilterL {};
        std::array<float, 8> combFilterR {};
        std::array<Line, 4> allpassL;
        std::array<Line, 4> allpassR;
        Line preL;
        Line preR;
        Line delayL;
        Line delayR;
        float delayFilterL = 0.0f;
        float delayFilterR = 0.0f;
        float chorusPhase = 0.0f;
        std::array<float, 4> phaserL {};
        std::array<float, 4> phaserR {};
        float phaserHoldL = 0.0f;
        float phaserHoldR = 0.0f;

        void prepare(double sampleRate);
        void process(const FxSlot& settings, double sampleRate, float inL, float inR, float& outL, float& outR) noexcept;
        void processFlanger(const FxSlot& settings, double sampleRate, float inL, float inR, float& outL, float& outR) noexcept;
        void processPhaser(const FxSlot& settings, double sampleRate, float inL, float inR, float& outL, float& outR) noexcept;
        void processTremolo(const FxSlot& settings, double sampleRate, float inL, float inR, float& outL, float& outR) noexcept;
    };

    double sampleRate = 48000.0;
    std::array<Voice, 4> voices;
};

} // namespace vc
