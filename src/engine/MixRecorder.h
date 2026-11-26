#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleTypes.h"

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace vc
{

// Mix outputs that can be written while playback keeps running.
constexpr int recordMain = 0;
constexpr int recordCenter = 1;
constexpr int recordBusBase = 2;
constexpr int recordMatrixBase = recordBusBase + numBuses;
constexpr int numRecordTargets = recordMatrixBase + numMatrices;
constexpr int recordFrameChannels = 2 + 1 + numBuses * 2 + numMatrices * 2;

constexpr int recordBus(int index) noexcept
{
    return recordBusBase + index;
}

constexpr int recordMatrix(int index) noexcept
{
    return recordMatrixBase + index;
}

constexpr int recordChannels(int target) noexcept
{
    return target == recordCenter ? 1 : 2;
}

constexpr int recordFrameOffset(int target) noexcept
{
    if (target <= recordMain)
        return 0;

    if (target == recordCenter)
        return 2;

    if (target < recordMatrixBase)
        return 3 + (target - recordBusBase) * 2;

    return 3 + numBuses * 2 + (target - recordMatrixBase) * 2;
}

// Writes the chosen post-fader mix outputs to WAV files.
// The audio thread only copies into a ring. A background thread owns the files.
class MixRecorder
{
public:
    MixRecorder();
    ~MixRecorder();

    void prepare(double sampleRate);
    void setTarget(int target, bool enabled);
    void setTargetName(int target, const juce::String& name);
    bool targetEnabled(int target) const noexcept;
    int enabledCount() const noexcept;

    bool start(const juce::File& directory);
    void stop();

    bool isRecording() const noexcept { return recording.load(std::memory_order_acquire); }
    double secondsRecorded() const noexcept;
    bool overflowed() const noexcept { return overflow.load(std::memory_order_relaxed); }
    juce::String lastError() const;

    void push(float mainL, float mainR, float center,
              const float* busL, const float* busR,
              const float* matrixL, const float* matrixR) noexcept;
    void endBlock() noexcept;

private:
    struct Track
    {
        int target = 0;
        int channels = 0;
        juce::File file;
        std::unique_ptr<juce::AudioFormatWriter> writer;
    };

    void writerLoop();
    void setError(const juce::String& message);
    juce::File uniqueWav(const juce::File& directory, const juce::String& title) const;

    juce::WavAudioFormat wav;
    double rate = 48000.0;
    std::array<bool, numRecordTargets> armed {};
    std::array<juce::String, numRecordTargets> names;
    std::atomic<bool> recording { false };
    std::atomic<bool> writerRun { false };
    std::atomic<bool> overflow { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    juce::AbstractFifo fifo { 1024 };
    std::vector<float> ring;
    int capacity = 0;
    juce::WaitableEvent dataReady;
    std::thread writer;
    std::vector<Track> tracks;
    juce::CriticalSection errorLock;
    juce::String error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixRecorder)
};

} // namespace vc
