#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ConsoleTypes.h"

#include <juce_core/juce_core.h>

namespace vc
{

// Console state addressed like an X32 (/ch/01/mix/fader). Audio fields are atomics
// so the mixer thread can read them without locking. Names and the /virt source
// bindings stay on the message thread. /virt is kept beside the console
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

    bool assignFloat(const juce::String& path, float value);
    bool assignInt(const juce::String& path, int value);
    void mirrorFloat(const juce::String& path, float value);
    void mirrorInt(const juce::String& path, int value);
    void duplicateProcessing(int fromIndex, int toIndex, bool includePan);

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

    bool isChannelLinked(int channelIndex) const noexcept;
    void setChannelLinked(int channelIndex, bool linked);
    void copyChannel(int channelIndex);
    void pasteChannel(int channelIndex);
    bool hasChannelClipboard() const noexcept { return clipboardReady; }

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
    std::atomic<int> channelLink[16] {};
    bool suppressMirror = false;
    bool clipboardReady = false;

    struct ProcessingSnapshot
    {
        float trim = 0.5f;
        float fader = 0.75f;
        float pan = 0.5f;
        float hpf = 0.2f;
        float delayTime = 0.0f;
        int invert = 0;
        int on = 1;
        int mainAssign = 1;
        int hpfOn = 0;
        int delayOn = 0;
        int eqType[numChannelEqBands] {};
        float eqFreq[numChannelEqBands] {};
        float eqGain[numChannelEqBands] {};
        float eqQ[numChannelEqBands] {};
        int eqOn[numChannelEqBands] {};
        int gateOn = 0;
        float gateThr = 0.75f;
        float gateRange = 0.5f;
        float gateAttack = 0.2f;
        float gateHold = 0.2f;
        float gateRelease = 0.5f;
        int compOn = 0;
        float compThr = 0.75f;
        float compRatio = 0.5f;
        float compAttack = 0.2f;
        float compHold = 0.2f;
        float compRelease = 0.5f;
        float compKnee = 0.2f;
        float compMakeup = 0.5f;
    };

    ProcessingSnapshot clipboard;

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
