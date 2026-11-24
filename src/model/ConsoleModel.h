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
    MixPoint& mainCenter() noexcept { return mainMono; }
    const MixPoint& mainCenter() const noexcept { return mainMono; }
    Bus& bus(int index) noexcept { return buses[(size_t) index]; }
    const Bus& bus(int index) const noexcept { return buses[(size_t) index]; }
    MixPoint& dca(int index) noexcept { return dcas[(size_t) index]; }
    const MixPoint& dca(int index) const noexcept { return dcas[(size_t) index]; }
    std::atomic<int>& dcaSoloState(int index) noexcept { return dcaSolo[(size_t) index]; }
    const std::atomic<int>& dcaSoloState(int index) const noexcept { return dcaSolo[(size_t) index]; }
    std::atomic<int>& muteGroupState(int index) noexcept { return muteGroups[(size_t) index]; }
    const std::atomic<int>& muteGroupState(int index) const noexcept { return muteGroups[(size_t) index]; }
    Matrix& matrix(int index) noexcept { return matrices[(size_t) index]; }
    const Matrix& matrix(int index) const noexcept { return matrices[(size_t) index]; }
    std::atomic<float>& mainMatrixLevel(bool monoBus, int matrix) noexcept;
    const std::atomic<float>& mainMatrixLevel(bool monoBus, int matrix) const noexcept;
    std::atomic<int>& mainMatrixPre(bool monoBus, int matrix) noexcept;

    void setListenMode(ListenMode mode) noexcept;
    ListenMode getListenMode() const noexcept;

    // 0 plays main LR. 1..16 plays that mix bus on the output device.
    void setSpeakerSource(int source) noexcept;
    int getSpeakerSource() const noexcept;
    void setCenterToStereo(bool enabled) noexcept;
    bool centerFeedsStereo() const noexcept;

    int addSource(const juce::String& name, const juce::String& filePath);
    void removeSource(int index);
    int getNumSources() const noexcept { return sourceCount; }
    int getSourceId(int index) const;
    int findSourceByFile(const juce::String& filePath) const;
    juce::String getSourceName(int index) const;
    juce::String getSourceFile(int index) const;

    void setChannelSource(int channelIndex, int inputIndex) noexcept;
    int getChannelSource(int channelIndex) const noexcept;
    void setInputSource(int inputIndex, int sourceIndex) noexcept;
    int getInputSource(int inputIndex) const noexcept;
    int inputForSource(int sourceIndex) const noexcept;
    int firstFreeInput() const noexcept;
    static juce::String inputName(int inputIndex);

    juce::String getChannelName(int index) const;
    void setChannelName(int index, const juce::String& name);
    juce::String getBusName(int index) const;
    void setBusName(int index, const juce::String& name);

    bool isChannelLinked(int channelIndex) const noexcept;
    void setChannelLinked(int channelIndex, bool linked);
    void copyChannel(int channelIndex);
    void pasteChannel(int channelIndex);
    bool hasChannelClipboard() const noexcept { return clipboardReady; }

    const Stagebox& aes50a() const noexcept { return aes50A; }
    const Stagebox& aes50b() const noexcept { return aes50B; }

private:
    std::array<Channel, numChannels> channels;
    std::array<Bus, numBuses> buses;
    std::array<Matrix, numMatrices> matrices;
    std::array<ReturnStrip, numFxReturns> fxReturns;
    std::array<ReturnStrip, numAuxIns> auxIns;
    std::array<ReturnStrip, numUsbReturns> usbReturns;
    std::array<MixPoint, numDcas> dcas;
    std::array<std::atomic<int>, numDcas> dcaSolo {};
    MixPoint mainSt;
    MixPoint mainMono;
    std::atomic<float> mainStereoMatrix[numMatrices] {};
    std::atomic<int> mainStereoMatrixPre[numMatrices] {};
    std::atomic<float> mainMonoMatrixLevel[numMatrices] {};
    std::atomic<int> mainMonoMatrixPre[numMatrices] {};
    std::array<FxSlot, numFxSlots> fx;
    std::array<std::atomic<int>, numMuteGroups> muteGroups;
    std::atomic<int> listenMode { (int) ListenMode::automatic };
    std::atomic<int> speakerSource { 0 };
    std::atomic<int> centerToStereo { 0 };
    int soloMode = 0;
    std::atomic<int> channelLink[16] {};
    std::atomic<int> inputSource[numRoutableInputs];
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
        int phantom = 0;
        int on = 1;
        int mainAssign = 1;
        int monoAssign = 0;
        float monoLevel = 0.75f;
        int hpfOn = 0;
        int eqMaster = 1;
        int delayOn = 0;
        int eqType[numChannelEqBands] {};
        float eqFreq[numChannelEqBands] {};
        float eqGain[numChannelEqBands] {};
        float eqQ[numChannelEqBands] {};
        int eqOn[numChannelEqBands] {};
        int gateOn = 0;
        int gateMode = 0;
        float gateThr = 0.75f;
        float gateRange = 0.5f;
        float gateAttack = 0.2f;
        float gateHold = 0.2f;
        float gateRelease = 0.5f;
        int compOn = 0;
        int compMode = 0;
        int compDetect = 0;
        int compEnvelope = 0;
        int compPosition = 0;
        int compAuto = 0;
        int compFilterOn = 0;
        int compFilterType = 0;
        float compFilterFreq = 0.5f;
        float compMix = 1.0f;
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
