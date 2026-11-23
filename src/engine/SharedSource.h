#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include <atomic>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace vc
{

// One decoded file, shared by every channel that selects it.
// The audio thread only reads. A background thread fills a cache from disk,
// resampling to the device rate. Playback position belongs to the mix engine.
// Tests can publish an in-memory buffer instead.
class SharedSource : private juce::Thread
{
public:
    SharedSource();
    ~SharedSource() override;

    void openFile(const juce::File& file, double playbackRate);
    void setMemory(std::vector<float> mono, double sourceRate, double playbackRate);
    void prepare(double playbackRate);
    void close();
    void takeFrom(SharedSource& other);

    void readAt(float* dest, juce::int64 startSample, int numSamples) noexcept;
    void hint(juce::int64 startSample) noexcept;
    int available() const noexcept;
    bool isActive() const noexcept;
    juce::int64 lengthSamples() const noexcept;
    double lengthSeconds() const noexcept;
    int statusCode() const noexcept { return status.load(std::memory_order_acquire); }
    juce::String errorMessage() const;

private:
    void run() override;
    void stopStreaming();
    float sampleAt(juce::AudioFormatReader& reader, double position);
    void loadWindow(juce::AudioFormatReader& reader, juce::int64 start);

    juce::File file;
    std::atomic<double> deviceRate { 48000.0 };
    std::atomic<bool> streaming { false };
    std::atomic<int> status { 0 };
    juce::CriticalSection errorLock;
    juce::String error;

    std::vector<float> memoryOwner;
    std::atomic<const float*> memory { nullptr };
    std::atomic<int> memoryLength { 0 };
    std::atomic<double> memoryRate { 48000.0 };
    std::atomic<juce::int64> lengthInDeviceSamples { 0 };

    static constexpr int cacheSize = 131072;
    std::vector<float> cache;
    std::atomic<juce::int64> validBegin { 0 };
    std::atomic<juce::int64> validEnd { 0 };
    std::atomic<juce::int64> demand { 0 };

    std::vector<float> window;
    juce::int64 windowStart = -1;
    int windowCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharedSource)
};

} // namespace vc
