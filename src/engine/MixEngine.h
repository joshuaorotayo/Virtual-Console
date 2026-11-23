#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleModel.h"
#include "engine/SharedSource.h"
#include "engine/ChannelDsp.h"

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

private:
    struct StripSmooth
    {
        float curL = 0.0f;
        float curR = 0.0f;
        float curPflL = 0.0f;
        float curPflR = 0.0f;
    };

    void ensureCapacity(int samples);
    void applySeek() noexcept;
    juce::int64 durationSamples() const noexcept;
    void processChunk(float* left, float* right, int numSamples, juce::int64 startSample, bool running) noexcept;
    void channelTargets(int index, float& pflL, float& pflR, float& outL, float& outR) const noexcept;

    ConsoleModel& model;
    juce::SpinLock sourceLock;
    std::array<SharedSource, maxSources> sources;
    std::array<ChannelDsp, numChannels> channelDsp;
    std::vector<float> processed;
    std::array<StripSmooth, numChannels> smooth {};
    std::array<std::array<float, numBuses>, numChannels> sendSmooth {};
    std::array<float, numBuses> busSmoothL {};
    std::array<float, numBuses> busSmoothR {};
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixEngine)
};

} // namespace vc
