#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include <atomic>
#include <array>

namespace vc
{

constexpr int numChannels = 32;
constexpr int numBuses = 16;
constexpr int numMatrices = 6;
constexpr int numFxReturns = 8;
constexpr int numAuxIns = 6;
constexpr int numFxSlots = 8;
constexpr int numDcas = 8;
constexpr int numMuteGroups = 6;
constexpr int numEqBands = 6;
constexpr int numChannelEqBands = 4;
constexpr int numFxParams = 64;
constexpr int maxSources = 64;

enum class ListenMode
{
    automatic = 0,
    main = 1,
    solo = 2
};

struct EqBand
{
    std::atomic<int> type { 2 };
    std::atomic<float> freq { 0.5f };
    std::atomic<float> gain { 0.5f };
    std::atomic<float> q { 0.5f };
    std::atomic<int> on { 1 };
};

struct Dynamics
{
    std::atomic<int> on { 0 };
    std::atomic<float> threshold { 0.75f };
    std::atomic<float> ratio { 0.5f };
    std::atomic<float> attack { 0.2f };
    std::atomic<float> hold { 0.2f };
    std::atomic<float> release { 0.5f };
    std::atomic<float> range { 0.5f };
    std::atomic<float> knee { 0.2f };
    std::atomic<float> makeup { 0.5f };
};

struct MixPoint
{
    std::atomic<float> fader { 0.75f };
    std::atomic<float> pan { 0.5f };
    std::atomic<int> on { 1 };
    EqBand eq[numEqBands] {};
    Dynamics comp {};
};

struct Channel : MixPoint
{
    std::atomic<int> solo { 0 };
    std::atomic<float> trim { 0.5f };
    std::atomic<int> invert { 0 };
    std::atomic<int> source { -1 };
    std::atomic<int> mainAssign { 1 };
    std::atomic<float> hpf { 0.2f };
    std::atomic<int> hpfOn { 0 };
    std::atomic<int> delayOn { 0 };
    std::atomic<float> delayTime { 0.0f };
    Dynamics gate {};
    float sendLevel[numBuses] {};
    int sendPre[numBuses] {};
    int dcaMask = 0;
    int muteMask = 0;
    int color = 0;
};

struct FxSlot
{
    int type = 0;
    std::atomic<int> on { 0 };
    float par[numFxParams] {};

    FxSlot()
    {
        for (float& value : par)
            value = 0.5f;
    }
};

struct Stagebox
{
    int inputs = 0;
    int outputs = 0;
    int kind = 0;
};

struct RoutingBanks
{
    int local = 32;
    int aux = 8;
    int card = 32;
};

} // namespace vc
