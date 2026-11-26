#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleModel.h"
#include "engine/SharedSource.h"
#include "engine/ChannelDsp.h"
#include "engine/FxProcessor.h"
#include "engine/MixRecorder.h"

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vc
{

// Real-time mix of the console's channel strips, buses, and solo.
// Targets are captured at the start of each block, then smoothed per sample.
class MixEngine : public juce::AudioSource
{
public:
    explicit MixEngine(ConsoleModel& modelToUse);

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& buffer) override;

    void openSource(int index, const juce::File& file);
    void removeSource(int index);
    int adoptMemorySource(std::vector<float> mono, double sourceRate, const juce::String& name);
    void process(float* left, float* right, int numSamples) noexcept;
    void snapSmoothers() noexcept;

    void play() noexcept;
    void pause() noexcept;
    void restart() noexcept;
    void seek(double seconds) noexcept;
    bool isPlaying() const noexcept { return playing.load(std::memory_order_acquire); }
    double getPosition() const noexcept;
    double getDuration() const noexcept;

    float getChannelMeter(int index) const noexcept;
    float getMainMeter() const noexcept;
    int bufferedSamples(int index) const noexcept;
    int sourceStatus(int index) const noexcept;
    juce::String sourceError(int index) const;
    double getSampleRate() const noexcept { return sampleRate; }
    MixRecorder& recorder() noexcept { return mixRecorder; }
    const MixRecorder& recorder() const noexcept { return mixRecorder; }

private:
    struct StripSmooth
    {
        float curL = 0.0f;
        float curR = 0.0f;
        float curPflL = 0.0f;
        float curPflR = 0.0f;
        float curPost = 0.0f;
        float curMono = 0.0f;
    };

    void ensureCapacity(int samples);
    void applySeek() noexcept;
    juce::int64 durationSamples() const noexcept;
    void processChunk(float* left, float* right, int numSamples, juce::int64 startSample, bool running) noexcept;
    void channelTargets(int index, float& pflL, float& pflR, float& outL, float& outR) const noexcept;
    void returnGains(int index, float& left, float& right, float& post, float& mono) const noexcept;
    float delayOutput(int index, float sample) noexcept;

    ConsoleModel& model;
    juce::SpinLock sourceLock;
    std::array<SharedSource, maxSources> sources;
    std::array<ChannelDsp, numChannels> channelDsp;
    std::array<GroupDsp, numBuses> busDsp;
    std::array<GroupDsp, numMatrices> matrixDsp;
    GroupDsp mainDsp;
    GroupDsp centerDsp;
    FxRack fxRack;
    std::array<float, numFxReturns> returnSmoothL {};
    std::array<float, numFxReturns> returnSmoothR {};
    std::array<float, numFxReturns> returnPostSmooth {};
    std::array<float, numFxReturns> returnMonoSmooth {};
    std::array<std::array<float, numBuses>, numFxReturns> returnSendSmooth {};
    std::vector<float> processed;
    std::array<StripSmooth, numChannels> smooth {};
    std::array<std::array<float, numBuses>, numChannels> sendSmooth {};
    std::array<float, numBuses> busSmoothL {};
    std::array<float, numBuses> busSmoothR {};
    std::array<float, numBuses> busPostSmooth {};
    std::array<float, numBuses> busMonoSmooth {};
    std::array<float, numMatrices> matrixSmooth {};
    std::array<std::array<float, numMatrices>, numBuses> busMatrixSmooth {};
    std::array<float, numMatrices> mainStereoMatrixSmooth {};
    std::array<float, numMatrices> mainMonoMatrixSmooth {};
    float mainMonoGain = 0.0f;
    std::array<std::atomic<float>, numChannels> channelMeters {};
    std::atomic<float> mainMeter { 0.0f };
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> playhead { 0 };
    std::atomic<juce::int64> pendingSeek { 0 };
    std::atomic<int> seekGeneration { 0 };
    int appliedSeek = 0;
    float mainGain = 0.0f;
    float listenFade = 0.0f;
    float smoothCoeff = 1.0f;
    double sampleRate = 48000.0;
    int scratchCapacity = 0;
    std::vector<float> scratch;
    std::vector<float> mainBufL;
    std::vector<float> mainBufR;
    std::vector<float> soloBufL;
    std::vector<float> soloBufR;
    std::array<std::vector<float>, numLocalOutputs> outputDelay;
    std::array<int, numLocalOutputs> outputWrite {};
    MixRecorder mixRecorder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEngine)
};

} // namespace vc
