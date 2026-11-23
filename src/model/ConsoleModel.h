#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ConsoleTypes.h"

#include <juce_core/juce_core.h>

namespace vc
{

// Console state addressed like an X32 (/ch/01/mix/fader). Audio fields are atomics
// so the mixer thread can read them without locking. Names, EQ, dynamics, and the
// /virt source bindings are message-thread state. /virt is kept beside the console
// tree so a later scene export can drop it.
class ConsoleModel
{
public:
    ConsoleModel();

    bool setFloat(const juce::String& path, float value);
    bool getFloat(const juce::String& path, float& value) const;
    bool setInt(const juce::String& path, int value);
    bool getInt(const juce::String& path, int& value) const;
    bool setString(const juce::String& path, const juce::String& value);
    bool getString(const juce::String& path, juce::String& value) const;

    Channel& channel(int index) noexcept { return channels[(size_t) index]; }
    const Channel& channel(int index) const noexcept { return channels[(size_t) index]; }

    MixPoint& mainStereo() noexcept { return mainSt; }
    const MixPoint& mainStereo() const noexcept { return mainSt; }

    void setListenMode(ListenMode mode) noexcept;
    ListenMode getListenMode() const noexcept;

    int addSource(const juce::String& name, const juce::String& filePath);
    void removeSource(int index);
    int getNumSources() const noexcept { return sourceCount; }
    int getSourceId(int index) const;
    juce::String getSourceName(int index) const;
    juce::String getSourceFile(int index) const;

    void setChannelSource(int channelIndex, int sourceIndex) noexcept;
    int getChannelSource(int channelIndex) const noexcept;

    juce::String getChannelName(int index) const;
    void setChannelName(int index, const juce::String& name);

    const Stagebox& aes50a() const noexcept { return aes50A; }
    const Stagebox& aes50b() const noexcept { return aes50B; }

private:
    std::array<Channel, numChannels> channels;
    std::array<MixPoint, numBuses> buses;
    std::array<MixPoint, numMatrices> matrices;
    std::array<MixPoint, numFxReturns> fxReturns;
    std::array<MixPoint, numAuxIns> auxIns;
    std::array<MixPoint, numDcas> dcas;
    MixPoint mainSt;
    MixPoint mainMono;
    std::array<FxSlot, numFxSlots> fx;
    std::array<std::atomic<int>, numMuteGroups> muteGroups;
    std::atomic<int> listenMode { (int) ListenMode::automatic };
    int soloMode = 0;

    juce::String channelNames[numChannels];
    juce::String busNames[numBuses];
    juce::String matrixNames[numMatrices];
    juce::String fxReturnNames[numFxReturns];
    juce::String auxNames[numAuxIns];
    juce::String dcaNames[numDcas];

    struct Source
    {
        juce::String name;
        juce::String file;
        int id = 0;
    };

    Source sources[(size_t) maxSources];
    int sourceCount = 0;
    int nextSourceId = 1;

    Stagebox aes50A;
    Stagebox aes50B;
    RoutingBanks banks;
};

} // namespace vc
