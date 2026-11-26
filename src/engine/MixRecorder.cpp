// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MixRecorder.h"

#include <algorithm>
#include <cmath>

namespace vc
{
namespace
{
juce::String safeRecordingName(juce::String name)
{
    name = name.trim();
    juce::String clean;

    for (auto character : name)
    {
        const bool illegal = character < 32
                          || character == '\\' || character == '/' || character == ':'
                          || character == '*' || character == '?' || character == '"'
                          || character == '<' || character == '>' || character == '|';
        clean += illegal ? ' ' : character;
    }

    clean = clean.trim();
    return clean.isEmpty() ? juce::String("Mix") : clean;
}
} // namespace

MixRecorder::MixRecorder()
{
    armed.fill(false);
    armed[(size_t) recordMain] = true;
    names[(size_t) recordMain] = "Main";
    names[(size_t) recordCenter] = "Center";

    for (int bus = 0; bus < numBuses; ++bus)
        names[(size_t) recordBus(bus)] = "Bus " + juce::String(bus + 1).paddedLeft('0', 2);

    for (int matrix = 0; matrix < numMatrices; ++matrix)
        names[(size_t) recordMatrix(matrix)] = "Matrix " + juce::String(matrix + 1).paddedLeft('0', 2);
}

MixRecorder::~MixRecorder()
{
    stop();
}

void MixRecorder::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    if (isRecording())
        return;

    const int frames = std::max(8192, (int) std::lround(rate * 2.0));

    if (frames == capacity && (int) ring.size() == frames * recordFrameChannels)
        return;

    capacity = frames;
    fifo.setTotalSize(frames);
    ring.assign((size_t) frames * (size_t) recordFrameChannels, 0.0f);
}

void MixRecorder::setTarget(int target, bool enabled)
{
    if (isRecording() || target < 0 || target >= numRecordTargets)
        return;

    armed[(size_t) target] = enabled;
}

void MixRecorder::setTargetName(int target, const juce::String& name)
{
    if (target < 0 || target >= numRecordTargets)
        return;

    const auto clean = safeRecordingName(name);

    if (clean.isNotEmpty())
        names[(size_t) target] = clean;
}

bool MixRecorder::targetEnabled(int target) const noexcept
{
    if (target < 0 || target >= numRecordTargets)
        return false;

    return armed[(size_t) target];
}

int MixRecorder::enabledCount() const noexcept
{
    int count = 0;

    for (bool on : armed)
        if (on)
            ++count;

    return count;
}

juce::File MixRecorder::uniqueWav(const juce::File& directory, const juce::String& title) const
{
    const auto base = safeRecordingName(title);
    auto file = directory.getChildFile(base + ".wav");
    int suffix = 2;

    while (file.existsAsFile())
        file = directory.getChildFile(base + " " + juce::String(suffix++) + ".wav");

    return file;
}

void MixRecorder::setError(const juce::String& message)
{
    const juce::ScopedLock lock(errorLock);
    error = message;
}

juce::String MixRecorder::lastError() const
{
    const juce::ScopedLock lock(errorLock);
    return error;
}

double MixRecorder::secondsRecorded() const noexcept
{
    const double sampleRate = rate > 0.0 ? rate : 48000.0;
    return (double) samplesWritten.load(std::memory_order_relaxed) / sampleRate;
}

bool MixRecorder::start(const juce::File& directory)
{
    if (isRecording())
        return false;

    if (writer.joinable())
        writer.join();

    if (! directory.isDirectory())
    {
        setError("Choose a folder for the recording");
        return false;
    }

    if (capacity == 0)
        prepare(rate);

    std::vector<Track> opened;

    for (int target = 0; target < numRecordTargets; ++target)
    {
        if (! armed[(size_t) target])
            continue;

        Track track;
        track.target = target;
        track.channels = recordChannels(target);
        track.file = uniqueWav(directory, names[(size_t) target]);
        auto stream = std::unique_ptr<juce::OutputStream>(track.file.createOutputStream());

        if (stream == nullptr)
        {
            setError("Could not create " + track.file.getFileName());

            for (auto& earlier : opened)
            {
                earlier.writer.reset();
                earlier.file.deleteFile();
            }

            return false;
        }

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(rate)
                                 .withNumChannels(track.channels)
                                 .withBitsPerSample(24);
        track.writer = wav.createWriterFor(stream, options);

        if (track.writer == nullptr)
        {
            stream.reset();
            track.file.deleteFile();
            setError("Could not write " + track.file.getFileName());

            for (auto& earlier : opened)
            {
                earlier.writer.reset();
                earlier.file.deleteFile();
            }

            return false;
        }

        opened.push_back(std::move(track));
    }

    if (opened.empty())
    {
        setError("Choose what to record");
        return false;
    }

    tracks = std::move(opened);
    fifo.reset();
    samplesWritten.store(0, std::memory_order_relaxed);
    overflow.store(false, std::memory_order_relaxed);
    setError({});
    writerRun.store(true, std::memory_order_release);
    recording.store(true, std::memory_order_release);

    try
    {
        writer = std::thread([this] { writerLoop(); });
    }
    catch (...)
    {
        recording.store(false, std::memory_order_release);
        writerRun.store(false, std::memory_order_release);

        for (auto& track : tracks)
        {
            track.writer.reset();
            track.file.deleteFile();
        }

        tracks.clear();
        setError("Could not start recording");
        return false;
    }

    return true;
}

void MixRecorder::stop()
{
    recording.store(false, std::memory_order_release);
    writerRun.store(false, std::memory_order_release);
    dataReady.signal();

    if (writer.joinable())
        writer.join();

    tracks.clear();
}

void MixRecorder::writerLoop()
{
    constexpr int chunk = 2048;
    std::vector<float> block((size_t) chunk * (size_t) recordFrameChannels);
    std::vector<std::array<std::vector<float>, 2>> scratch(tracks.size());

    for (size_t index = 0; index < tracks.size(); ++index)
        for (int channel = 0; channel < tracks[index].channels; ++channel)
            scratch[index][(size_t) channel].assign((size_t) chunk, 0.0f);

    juce::int64 sinceFlush = 0;

    while (writerRun.load(std::memory_order_acquire) || fifo.getNumReady() > 0)
    {
        const int ready = fifo.getNumReady();

        if (ready <= 0)
        {
            if (! writerRun.load(std::memory_order_acquire))
                break;

            dataReady.wait(30);
            continue;
        }

        const int count = std::min(ready, chunk);
        int stored = 0;

        {
            const auto scope = fifo.read(count);
            auto take = [&](int start, int size)
            {
                if (size <= 0)
                    return;

                const float* source = ring.data() + (size_t) start * (size_t) recordFrameChannels;
                std::copy(source, source + (size_t) size * (size_t) recordFrameChannels, block.begin() + (size_t) stored * (size_t) recordFrameChannels);
                stored += size;
            };
            take(scope.startIndex1, scope.blockSize1);
            take(scope.startIndex2, scope.blockSize2);
        }

        if (stored <= 0)
            continue;

        bool failed = false;

        for (size_t index = 0; index < tracks.size(); ++index)
        {
            auto& track = tracks[index];
            const int offset = recordFrameOffset(track.target);

            for (int frame = 0; frame < stored; ++frame)
            {
                const float* sample = block.data() + (size_t) frame * (size_t) recordFrameChannels + (size_t) offset;
                scratch[index][0][(size_t) frame] = sample[0];

                if (track.channels > 1)
                    scratch[index][1][(size_t) frame] = sample[1];
            }

            const float* channels[2] = {
                scratch[index][0].data(),
                track.channels > 1 ? scratch[index][1].data() : nullptr
            };

            if (track.writer == nullptr || ! track.writer->writeFromFloatArrays(channels, track.channels, stored))
                failed = true;
        }

        if (failed)
            setError("The recording could not be written");

        samplesWritten.fetch_add(stored, std::memory_order_relaxed);
        sinceFlush += stored;

        if (sinceFlush >= (juce::int64) std::max(1.0, rate))
        {
            for (auto& track : tracks)
                if (track.writer != nullptr)
                    track.writer->flush();

            sinceFlush = 0;
        }
    }

    for (auto& track : tracks)
        track.writer.reset();
}

void MixRecorder::push(float mainL, float mainR, float center,
                       const float* busL, const float* busR,
                       const float* matrixL, const float* matrixR) noexcept
{
    if (! recording.load(std::memory_order_acquire))
        return;

    const auto scope = fifo.write(1);

    if (scope.blockSize1 < 1)
    {
        overflow.store(true, std::memory_order_relaxed);
        return;
    }

    float* dest = ring.data() + (size_t) scope.startIndex1 * (size_t) recordFrameChannels;
    dest[0] = mainL;
    dest[1] = mainR;
    dest[2] = center;

    for (int bus = 0; bus < numBuses; ++bus)
    {
        float* side = dest + recordFrameOffset(recordBus(bus));
        side[0] = busL != nullptr ? busL[bus] : 0.0f;
        side[1] = busR != nullptr ? busR[bus] : 0.0f;
    }

    for (int matrix = 0; matrix < numMatrices; ++matrix)
    {
        float* side = dest + recordFrameOffset(recordMatrix(matrix));
        side[0] = matrixL != nullptr ? matrixL[matrix] : 0.0f;
        side[1] = matrixR != nullptr ? matrixR[matrix] : 0.0f;
    }
}

void MixRecorder::endBlock() noexcept
{
    if (recording.load(std::memory_order_relaxed))
        dataReady.signal();
}

} // namespace vc
