// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SharedSource.h"

#include <cmath>
#include <algorithm>

#include <juce_audio_formats/juce_audio_formats.h>

namespace vc
{

SharedSource::SharedSource()
    : juce::Thread("vc-source")
{
    cache.assign((size_t) cacheSize, 0.0f);
}

SharedSource::~SharedSource()
{
    stopStreaming();
}

void SharedSource::stopStreaming()
{
    streaming.store(false, std::memory_order_release);
    signalThreadShouldExit();
    stopThread(4000);
}

void SharedSource::openFile(const juce::File& newFile, double playbackRate)
{
    stopStreaming();
    file = newFile;
    deviceRate.store(playbackRate, std::memory_order_relaxed);
    memory.store(nullptr, std::memory_order_release);
    memoryOwner.clear();
    {
        const juce::ScopedLock lock(errorLock);
        error.clear();
    }
    status.store(0, std::memory_order_relaxed);
    validBegin.store(0, std::memory_order_relaxed);
    validEnd.store(0, std::memory_order_relaxed);
    demand.store(0, std::memory_order_relaxed);
    lengthInDeviceSamples.store(0, std::memory_order_relaxed);
    windowStart = -1;
    if (cache.size() != (size_t) cacheSize)
        cache.assign((size_t) cacheSize, 0.0f);
    startThread();
}

void SharedSource::setMemory(std::vector<float> mono, double sourceRate, double playbackRate)
{
    stopStreaming();
    file = juce::File();
    memoryOwner = std::move(mono);
    memoryLength.store((int) memoryOwner.size(), std::memory_order_relaxed);
    memoryRate.store(sourceRate, std::memory_order_relaxed);
    deviceRate.store(playbackRate > 0.0 ? playbackRate : 48000.0, std::memory_order_relaxed);
    error.clear();
    status.store(memoryOwner.empty() ? 2 : 1, std::memory_order_relaxed);
    const double rate = deviceRate.load(std::memory_order_relaxed);
    const double srcRate = memoryRate.load(std::memory_order_relaxed);
    const auto deviceLength = (rate > 0.0 && srcRate > 0.0)
                                  ? (juce::int64) std::llround((double) memoryOwner.size() * rate / srcRate)
                                  : (juce::int64) memoryOwner.size();
    lengthInDeviceSamples.store(deviceLength, std::memory_order_relaxed);
    memory.store(memoryOwner.empty() ? nullptr : memoryOwner.data(), std::memory_order_release);
}

void SharedSource::close()
{
    stopStreaming();
    memory.store(nullptr, std::memory_order_release);
    memoryLength.store(0, std::memory_order_relaxed);
    memoryOwner.clear();
    file = juce::File();
    window.clear();
    windowStart = -1;
    windowCount = 0;
    validBegin.store(0, std::memory_order_relaxed);
    validEnd.store(0, std::memory_order_relaxed);
    lengthInDeviceSamples.store(0, std::memory_order_relaxed);
    status.store(0, std::memory_order_release);

    const juce::ScopedLock lock(errorLock);
    error.clear();
}

void SharedSource::takeFrom(SharedSource& other)
{
    if (this == &other)
        return;

    stopStreaming();
    other.stopStreaming();

    memory.store(nullptr, std::memory_order_release);
    other.memory.store(nullptr, std::memory_order_release);

    file = other.file;
    other.file = juce::File();
    memoryOwner = std::move(other.memoryOwner);
    memoryLength.store((int) memoryOwner.size(), std::memory_order_relaxed);
    other.memoryLength.store(0, std::memory_order_relaxed);
    memoryRate.store(other.memoryRate.load(std::memory_order_relaxed), std::memory_order_relaxed);
    deviceRate.store(other.deviceRate.load(std::memory_order_relaxed), std::memory_order_relaxed);
    lengthInDeviceSamples.store(other.lengthInDeviceSamples.load(std::memory_order_relaxed), std::memory_order_relaxed);
    other.lengthInDeviceSamples.store(0, std::memory_order_relaxed);
    window = std::move(other.window);
    windowStart = other.windowStart;
    windowCount = other.windowCount;
    other.windowStart = -1;
    other.windowCount = 0;
    validBegin.store(other.validBegin.load(std::memory_order_relaxed), std::memory_order_relaxed);
    validEnd.store(other.validEnd.load(std::memory_order_relaxed), std::memory_order_relaxed);
    other.validBegin.store(0, std::memory_order_relaxed);
    other.validEnd.store(0, std::memory_order_relaxed);
    cache.swap(other.cache);
    if (cache.size() != (size_t) cacheSize)
        cache.assign((size_t) cacheSize, 0.0f);

    const int otherStatus = other.status.load(std::memory_order_relaxed);
    juce::String otherError;
    {
        const juce::ScopedLock lock(other.errorLock);
        otherError = other.error;
        other.error.clear();
    }
    {
        const juce::ScopedLock lock(errorLock);
        error = otherError;
    }
    other.status.store(0, std::memory_order_relaxed);

    if (! memoryOwner.empty())
    {
        status.store(otherStatus == 0 ? 1 : otherStatus, std::memory_order_release);
        memory.store(memoryOwner.data(), std::memory_order_release);
        return;
    }

    if (file.getFullPathName().isNotEmpty())
    {
        const auto path = file;
        const double rate = deviceRate.load(std::memory_order_relaxed);
        openFile(path, rate);
        return;
    }

    status.store(0, std::memory_order_release);
}

void SharedSource::prepare(double playbackRate)
{
    const double previous = deviceRate.load(std::memory_order_relaxed);
    deviceRate.store(playbackRate, std::memory_order_relaxed);

    if (memory.load(std::memory_order_acquire) != nullptr)
        return;

    if (file.existsAsFile() && std::abs(previous - playbackRate) > 0.5)
        openFile(file, playbackRate);
}

bool SharedSource::isActive() const noexcept
{
    return memory.load(std::memory_order_acquire) != nullptr
        || streaming.load(std::memory_order_acquire);
}

int SharedSource::available() const noexcept
{
    if (memory.load(std::memory_order_acquire) != nullptr)
        return memoryLength.load(std::memory_order_relaxed);

    const auto begin = validBegin.load(std::memory_order_acquire);
    const auto end = validEnd.load(std::memory_order_acquire);
    const auto at = demand.load(std::memory_order_relaxed);

    if (at < begin || at >= end)
        return 0;

    return (int) std::min<juce::int64>(end - at, 100000000);
}

juce::int64 SharedSource::lengthSamples() const noexcept
{
    if (memory.load(std::memory_order_acquire) != nullptr)
    {
        const double rate = deviceRate.load(std::memory_order_relaxed);
        const double srcRate = memoryRate.load(std::memory_order_relaxed);
        const int count = memoryLength.load(std::memory_order_relaxed);

        if (rate <= 0.0 || srcRate <= 0.0)
            return count;

        return (juce::int64) std::llround((double) count * rate / srcRate);
    }

    return lengthInDeviceSamples.load(std::memory_order_acquire);
}

double SharedSource::lengthSeconds() const noexcept
{
    const double rate = deviceRate.load(std::memory_order_relaxed);
    const auto samples = lengthSamples();
    return rate > 0.0 ? (double) samples / rate : 0.0;
}

void SharedSource::hint(juce::int64 startSample) noexcept
{
    if (startSample < 0)
        startSample = 0;

    demand.store(startSample, std::memory_order_relaxed);
}

juce::String SharedSource::errorMessage() const
{
    if (status.load(std::memory_order_acquire) != 2)
        return {};

    const juce::ScopedLock lock(errorLock);
    return error;
}

void SharedSource::loadWindow(juce::AudioFormatReader& reader, juce::int64 start)
{
    constexpr int windowTarget = 4096;
    const auto length = reader.lengthInSamples;

    window.assign((size_t) windowTarget + 2, 0.0f);
    windowStart = start;
    windowCount = 0;

    if (length <= 0 || start >= length)
        return;

    juce::AudioBuffer<float> block(2, windowTarget + 1);
    const int filled = (int) std::min<juce::int64>(windowTarget + 1, length - start);
    reader.read(&block, 0, filled, start, true, true);

    for (int i = 0; i < filled; ++i)
    {
        const float left = block.getSample(0, i);
        const float right = block.getNumChannels() > 1 ? block.getSample(1, i) : left;
        window[(size_t) i] = 0.5f * (left + right);
    }

    windowCount = filled;
}

float SharedSource::sampleAt(juce::AudioFormatReader& reader, double position)
{
    const auto length = reader.lengthInSamples;

    if (length <= 0 || position < 0.0 || position >= (double) length)
        return 0.0f;

    const auto index = (juce::int64) std::floor(position);

    if (windowStart < 0 || index < windowStart || index + 1 >= windowStart + windowCount)
        loadWindow(reader, index);

    const int local = (int) (index - windowStart);

    if (local < 0 || local >= windowCount)
        return 0.0f;

    const float fraction = (float) (position - (double) index);
    const float a = window[(size_t) local];
    const float b = local + 1 < windowCount ? window[(size_t) (local + 1)] : 0.0f;
    return a + (b - a) * fraction;
}

void SharedSource::run()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        const juce::ScopedLock lock(errorLock);
        error = "Could not read " + file.getFileName();
        status.store(2, std::memory_order_release);
        return;
    }

    const double fileRate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const double playbackRate = std::max(1.0, deviceRate.load(std::memory_order_relaxed));
    const auto deviceLength = (juce::int64) std::llround((double) reader->lengthInSamples * playbackRate / fileRate);
    lengthInDeviceSamples.store(std::max<juce::int64>(deviceLength, 0), std::memory_order_release);
    windowStart = -1;
    validBegin.store(0, std::memory_order_relaxed);
    validEnd.store(0, std::memory_order_relaxed);
    status.store(1, std::memory_order_release);
    streaming.store(true, std::memory_order_release);

    while (! threadShouldExit())
    {
        const auto want = std::max<juce::int64>(0, demand.load(std::memory_order_relaxed));
        auto begin = validBegin.load(std::memory_order_relaxed);
        auto end = validEnd.load(std::memory_order_relaxed);
        const auto limit = lengthInDeviceSamples.load(std::memory_order_relaxed);

        if (want < begin || want > end)
        {
            begin = want;

            if (limit > 0 && begin > limit)
                begin = limit;

            validEnd.store(begin, std::memory_order_relaxed);
            validBegin.store(begin, std::memory_order_release);
            end = begin;
            windowStart = -1;
        }

        auto target = want + (cacheSize - 4096);

        if (limit >= 0 && target > limit)
            target = limit;

        if (end < target && cache.size() == (size_t) cacheSize)
        {
            const int count = (int) std::min<juce::int64>(target - end, 1024);
            const auto newEnd = end + count;
            const auto newBegin = std::max(begin, newEnd - (juce::int64) cacheSize);

            if (newBegin != begin)
                validBegin.store(newBegin, std::memory_order_release);

            for (int i = 0; i < count; ++i)
            {
                const auto devicePos = end + i;
                const double filePos = (double) devicePos * fileRate / playbackRate;
                cache[(size_t) devicePos & (size_t) (cacheSize - 1)] = sampleAt(*reader, filePos);
            }

            validEnd.store(newEnd, std::memory_order_release);
            continue;
        }

        wait(2);
    }
}

void SharedSource::readAt(float* dest, juce::int64 startSample, int numSamples) noexcept
{
    if (dest == nullptr || numSamples <= 0)
        return;

    if (startSample < 0)
        startSample = 0;

    demand.store(startSample, std::memory_order_relaxed);
    const float* samples = memory.load(std::memory_order_acquire);

    if (samples != nullptr)
    {
        const int length = memoryLength.load(std::memory_order_relaxed);
        const double rate = deviceRate.load(std::memory_order_relaxed);
        const double step = rate > 0.0 ? memoryRate.load(std::memory_order_relaxed) / rate : 1.0;
        double position = (double) startSample * step;

        if (length <= 0)
        {
            std::fill(dest, dest + numSamples, 0.0f);
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (position < 0.0 || position >= (double) length)
            {
                dest[i] = 0.0f;
            }
            else
            {
                const int i0 = (int) position;
                const int i1 = i0 + 1;
                const float fraction = (float) (position - (double) i0);
                const float a = samples[i0];
                const float b = i1 < length ? samples[i1] : 0.0f;
                dest[i] = a + (b - a) * fraction;
            }

            position += step;
        }

        return;
    }

    if (! streaming.load(std::memory_order_acquire) || cache.size() != (size_t) cacheSize)
    {
        std::fill(dest, dest + numSamples, 0.0f);
        return;
    }

    const auto begin = validBegin.load(std::memory_order_acquire);
    const auto end = validEnd.load(std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto position = startSample + (juce::int64) i;
        dest[i] = (position >= begin && position < end)
                      ? cache[(size_t) position & (size_t) (cacheSize - 1)]
                      : 0.0f;
    }
}

} // namespace vc
