#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleTypes.h"

#include <array>
#include <vector>

namespace vc
{

// Send effects for slots 1–4, and graphic EQ for inserts. These are equivalent
// algorithms, not a copy of another mixer's DSP. Slots 5–8 are insert only.
constexpr int fxParPrimary = 0;
constexpr int fxParSecondary = 1;
constexpr int fxParSize = 2;
constexpr int fxParDamp = 3;
constexpr int graphicEqBands = 31;

enum class FxAlgo : int
{
    empty = 0,
    vintageRoom = 1,
    hall = 2,
    stereoDelay = 3,
    stereoChorus = 4,
    stereoFlanger = 5,
    stereoPhaser = 6,
    tremoloPanner = 7,
    graphicEq = 8,
    dualGraphicEq = 9
};

inline bool fxIsGraphicEq(int type) noexcept
{
    return type == (int) FxAlgo::graphicEq || type == (int) FxAlgo::dualGraphicEq;
}

const char* fxAlgoName(int type) noexcept;
void storeFxDefaults(FxSlot& slot, int type) noexcept;
float graphicEqFrequency(int band) noexcept;
float graphicEqGainDb(float unit) noexcept;

class FxRack
{
public:
    void prepare(double newSampleRate);
    void process(int slot, const FxSlot& settings, float inL, float inR, float& outL, float& outR) noexcept;
    void prepareInsert(int slot, const FxSlot& settings) noexcept;
    void processInsert(int slot, float& left, float& right) noexcept;

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

    struct GraphicBand
    {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;
        bool active = false;

        void setUnity() noexcept;
        void setNormalized(float nb0, float nb1, float nb2, float na1, float na2) noexcept;
        float tick(float sample) noexcept;
    };

    struct GraphicVoice
    {
        std::array<GraphicBand, graphicEqBands> left;
        std::array<GraphicBand, graphicEqBands> right;

        void reset() noexcept;
        void process(float& leftSample, float& rightSample) noexcept;
    };

    double sampleRate = 48000.0;
    std::array<Voice, 4> voices;
    std::array<GraphicVoice, numFxSlots> graphic;
};

} // namespace vc
