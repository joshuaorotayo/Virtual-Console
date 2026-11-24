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
constexpr int numUsbReturns = 2;
constexpr int numFxSlots = 8;
constexpr int numDcas = 8;
constexpr int numMuteGroups = 6;
constexpr int numEqBands = 6;
constexpr int numChannelEqBands = 5;
constexpr int numFxParams = 64;
constexpr int maxSources = 64;
constexpr int numLocalInputs = 32;
constexpr int numLocalOutputs = 16;
constexpr int numAuxOutputs = 6;
constexpr int numRoutableInputs = numLocalInputs + numAuxIns;
constexpr int numAes50Channels = 48;
constexpr int aes50AInputBase = numRoutableInputs;
constexpr int aes50BInputBase = aes50AInputBase + numAes50Channels;
constexpr int numCardChannels = 32;
constexpr int cardInputBase = aes50BInputBase + numAes50Channels;
constexpr int numUserInputs = 32;
constexpr int userInputBase = cardInputBase + numCardChannels;
constexpr int numPatchInputs = userInputBase + numUserInputs;
constexpr int numP16Outputs = 16;
constexpr int numAesEbuOutputs = 2;
constexpr int numUserOutputPatches = 48;
constexpr int connectorOut1Block = 20;
constexpr int connectorOut9Block = 21;

constexpr int outputOff = 0;
constexpr int outputMainL = 1;
constexpr int outputMainR = 2;
constexpr int outputMainC = 3;
constexpr int outputBusBase = 4;
constexpr int outputMatrixBase = outputBusBase + numBuses * 2;
constexpr int outputDirectBase = outputMatrixBase + numMatrices * 2;
constexpr int outputMonitorL = outputDirectBase + numChannels;
constexpr int outputMonitorR = outputMonitorL + 1;
constexpr int outputBusMonoBase = outputMonitorR + 1;
constexpr int outputMatrixMonoBase = outputBusMonoBase + numBuses;
constexpr int numOutputSources = outputMatrixMonoBase + numMatrices;

inline bool outputIsDirect (int source) noexcept
{
    return source >= outputDirectBase && source < outputDirectBase + numChannels;
}

inline int outputSourceForBus (int bus, bool right) noexcept
{
    return outputBusBase + bus * 2 + (right ? 1 : 0);
}

inline int outputSourceForMatrix (int matrix, bool right) noexcept
{
    return outputMatrixBase + matrix * 2 + (right ? 1 : 0);
}

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
    std::atomic<int> mode { 0 };
    std::atomic<int> detect { 0 };
    std::atomic<int> envelope { 0 };
    std::atomic<int> position { 0 };
    std::atomic<int> autoTime { 0 };
    std::atomic<int> filterOn { 0 };
    std::atomic<int> filterType { 0 };
    std::atomic<float> filterFreq { 0.5f };
    std::atomic<float> threshold { 0.75f };
    std::atomic<float> ratio { 0.5f };
    std::atomic<float> attack { 0.2f };
    std::atomic<float> hold { 0.2f };
    std::atomic<float> release { 0.5f };
    std::atomic<float> range { 0.5f };
    std::atomic<float> knee { 0.2f };
    std::atomic<float> makeup { 0.5f };
    std::atomic<float> mix { 1.0f };
};

struct MixPoint
{
    std::atomic<float> fader { 0.75f };
    std::atomic<float> pan { 0.5f };
    std::atomic<int> on { 1 };
    std::atomic<int> eqMaster { 1 };
    EqBand eq[numEqBands] {};
    Dynamics comp {};
};

struct ReturnStrip : MixPoint
{
    std::atomic<int> toMain { 0 };
    std::atomic<int> toMono { 0 };
    std::atomic<float> monoLevel { 0.75f };
    std::atomic<float> sendLevel[numBuses] {};
    int color = 6;
};

struct Bus : MixPoint
{
    std::atomic<int> toMain { 0 };
    std::atomic<int> toMono { 0 };
    std::atomic<float> monoLevel { 0.75f };
    std::atomic<float> matrixLevel[numMatrices] {};
    std::atomic<int> matrixPre[numMatrices] {};
    std::atomic<int> dcaMask { 0 };
    std::atomic<int> muteMask { 0 };
    int color = 0;
};

struct Matrix : MixPoint
{
    std::atomic<int> toMain { 0 };
    std::atomic<int> toMono { 0 };
    Dynamics gate {};
    std::atomic<int> insertPost { 0 };
    std::atomic<int> dcaMask { 0 };
    std::atomic<int> muteMask { 0 };
    int color = 0;
};

struct Channel : MixPoint
{
    std::atomic<int> solo { 0 };
    std::atomic<float> trim { 0.5f };
    std::atomic<int> invert { 0 };
    std::atomic<int> phantom { 0 };
    std::atomic<int> source { -1 };
    std::atomic<int> mainAssign { 1 };
    std::atomic<int> monoAssign { 0 };
    std::atomic<float> monoLevel { 0.75f };
    std::atomic<float> hpf { 0.2f };
    std::atomic<int> hpfOn { 0 };
    std::atomic<int> eqOn { 1 };
    std::atomic<int> delayOn { 0 };
    std::atomic<float> delayTime { 0.0f };
    Dynamics gate {};
    std::atomic<float> sendLevel[numBuses] {};
    std::atomic<int> sendPre[numBuses] {};
    std::atomic<int> dcaMask { 0 };
    std::atomic<int> muteMask { 0 };
    int color = 0;
    int icon = -1;
};

struct FxSlot
{
    std::atomic<int> type { 0 };
    std::atomic<int> on { 0 };
    std::atomic<float> par[numFxParams];

    FxSlot()
    {
        for (auto& value : par)
            value.store(0.5f, std::memory_order_relaxed);
    }
};

struct Stagebox
{
    int inputs = 0;
    int outputs = 0;
    int kind = 0;
};

struct LocalOutput
{
    std::atomic<int> source { outputOff };
    std::atomic<int> tap { 3 | 8 };
    std::atomic<int> phase { 0 };
    std::atomic<float> delayMs { 0.0f };
};

struct RoutingBanks
{
    int local = 32;
    int aux = 8;
    int card = 32;
};

} // namespace vc
