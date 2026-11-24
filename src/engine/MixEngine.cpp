// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MixEngine.h"

#include "model/FaderLaw.h"

#include <array>
#include <cmath>
#include <algorithm>

namespace vc
{
namespace
{
struct GroupControl
{
    std::array<float, numDcas> level {};
    std::array<char, numDcas> muted {};
    std::array<char, numDcas> solo {};
    int muteGroups = 0;
};

struct GroupResult
{
    float level = 1.0f;
    bool muted = false;
    bool solo = false;
};

GroupControl readGroupControl(const ConsoleModel& model)
{
    GroupControl control;

    for (int group = 0; group < numDcas; ++group)
    {
        const auto& dca = model.dca(group);
        control.level[(size_t) group] = faderToGain(dca.fader.load(std::memory_order_relaxed));
        control.muted[(size_t) group] = dca.on.load(std::memory_order_relaxed) == 0 ? 1 : 0;
        control.solo[(size_t) group] = model.dcaSoloState(group).load(std::memory_order_relaxed) != 0 ? 1 : 0;
    }

    for (int group = 0; group < numMuteGroups; ++group)
        if (model.muteGroupState(group).load(std::memory_order_relaxed) != 0)
            control.muteGroups |= 1 << group;

    return control;
}

GroupResult groupFor(int dcaMask, int muteMask, const GroupControl& control)
{
    GroupResult result;
    result.muted = (muteMask & control.muteGroups) != 0;

    for (int group = 0; group < numDcas; ++group)
    {
        if ((dcaMask & (1 << group)) == 0)
            continue;

        result.level *= control.level[(size_t) group];
        result.muted = result.muted || control.muted[(size_t) group] != 0;
        result.solo = result.solo || control.solo[(size_t) group] != 0;
    }

    return result;
}
} // namespace

MixEngine::MixEngine(ConsoleModel& modelToUse)
    : model(modelToUse)
{
}

void MixEngine::ensureCapacity(int samples)
{
    if (samples <= scratchCapacity)
        return;

    scratchCapacity = samples;
    scratch.assign((size_t) maxSources * (size_t) scratchCapacity, 0.0f);
    processed.assign((size_t) numChannels * (size_t) scratchCapacity, 0.0f);
    mainBufL.assign((size_t) scratchCapacity, 0.0f);
    mainBufR.assign((size_t) scratchCapacity, 0.0f);
    soloBufL.assign((size_t) scratchCapacity, 0.0f);
    soloBufR.assign((size_t) scratchCapacity, 0.0f);

    for (auto& strip : channelDsp)
        strip.ensureCapacity(scratchCapacity);
}

void MixEngine::prepareToPlay(int samplesPerBlockExpected, double newSampleRate)
{
    const juce::SpinLock::ScopedLockType lock(sourceLock);
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    smoothCoeff = 1.0f - std::exp(-1.0f / (float) (sampleRate * 0.02));
    ensureCapacity(std::max(samplesPerBlockExpected, 512));

    for (auto& strip : channelDsp)
        strip.prepare(sampleRate);

    for (auto& group : busDsp)
        group.prepare(sampleRate);

    for (auto& group : matrixDsp)
        group.prepare(sampleRate);

    mainDsp.prepare(sampleRate);
    centerDsp.prepare(sampleRate);

    for (int i = 0; i < model.getNumSources(); ++i)
        sources[(size_t) i].prepare(sampleRate);
}

void MixEngine::releaseResources()
{
}

void MixEngine::openSource(int index, const juce::File& file)
{
    const juce::SpinLock::ScopedLockType lock(sourceLock);

    if (index < 0 || index >= maxSources)
        return;

    sources[(size_t) index].openFile(file, sampleRate);
}

void MixEngine::removeSource(int index)
{
    const juce::SpinLock::ScopedLockType lock(sourceLock);
    const int count = model.getNumSources();

    if (index < 0 || index >= count)
        return;

    for (int i = index; i < count - 1; ++i)
        sources[(size_t) i].takeFrom(sources[(size_t) i + 1]);

    sources[(size_t) (count - 1)].close();
    model.removeSource(index);
}

int MixEngine::adoptMemorySource(std::vector<float> mono, double sourceRate, const juce::String& name)
{
    const juce::SpinLock::ScopedLockType lock(sourceLock);
    const int index = model.addSource(name, {});

    if (index < 0)
        return -1;

    sources[(size_t) index].setMemory(std::move(mono), sourceRate, sampleRate);
    return index;
}

void MixEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& buffer)
{
    if (buffer.buffer == nullptr || buffer.numSamples <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;
    auto* output = buffer.buffer;
    const int channels = output->getNumChannels();

    if (channels <= 0)
        return;

    if (channels == 1)
    {
        process(output->getWritePointer(0, buffer.startSample), nullptr, buffer.numSamples);
        return;
    }

    process(output->getWritePointer(0, buffer.startSample),
            output->getWritePointer(1, buffer.startSample),
            buffer.numSamples);

    for (int channel = 2; channel < channels; ++channel)
        output->clear(channel, buffer.startSample, buffer.numSamples);
}

void MixEngine::process(float* left, float* right, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    if (left == nullptr || numSamples <= 0)
        return;

    if (! sourceLock.tryEnter())
    {
        std::fill_n(left, numSamples, 0.0f);

        if (right != nullptr)
            std::fill_n(right, numSamples, 0.0f);

        return;
    }

    struct Unlock
    {
        juce::SpinLock& lock;
        ~Unlock() { lock.exit(); }
    } unlock { sourceLock };

    if (scratchCapacity <= 0)
    {
        std::fill_n(left, numSamples, 0.0f);

        if (right != nullptr)
            std::fill_n(right, numSamples, 0.0f);

        return;
    }

    applySeek();
    bool running = playing.load(std::memory_order_relaxed);
    int offset = 0;

    while (offset < numSamples)
    {
        const int count = std::min(numSamples - offset, scratchCapacity);
        const auto start = playhead.load(std::memory_order_relaxed);
        const int sourceCount = model.getNumSources();

        for (int source = 0; source < sourceCount && source < maxSources; ++source)
            sources[(size_t) source].hint(start);

        processChunk(left + offset, right == nullptr ? nullptr : right + offset, count, start, running);
        offset += count;

        if (! running)
            continue;

        const auto duration = durationSamples();

        if (duration <= 0)
            continue;

        auto next = start + (juce::int64) count;

        if (next >= duration)
        {
            next = duration;
            running = false;
            playing.store(false, std::memory_order_release);
        }

        playhead.store(next, std::memory_order_relaxed);
    }
}

void MixEngine::applySeek() noexcept
{
    const int generation = seekGeneration.load(std::memory_order_acquire);

    if (generation == appliedSeek)
        return;

    auto target = pendingSeek.load(std::memory_order_relaxed);
    const auto duration = durationSamples();

    if (target < 0)
        target = 0;

    if (duration > 0 && target > duration)
        target = duration;

    playhead.store(target, std::memory_order_relaxed);
    appliedSeek = generation;
}

juce::int64 MixEngine::durationSamples() const noexcept
{
    juce::int64 longest = 0;
    const int count = model.getNumSources();

    for (int i = 0; i < count && i < maxSources; ++i)
        longest = std::max(longest, sources[(size_t) i].lengthSamples());

    return longest;
}

void MixEngine::play() noexcept
{
    const auto duration = durationSamples();

    if (duration > 0 && playhead.load(std::memory_order_relaxed) >= duration)
    {
        restart();
        return;
    }

    playing.store(true, std::memory_order_release);
}

void MixEngine::pause() noexcept
{
    playing.store(false, std::memory_order_release);
}

void MixEngine::restart() noexcept
{
    pendingSeek.store(0, std::memory_order_relaxed);
    seekGeneration.fetch_add(1, std::memory_order_release);
    playing.store(true, std::memory_order_release);
}

void MixEngine::seek(double seconds) noexcept
{
    if (seconds < 0.0)
        seconds = 0.0;

    const auto samples = (juce::int64) std::llround(seconds * sampleRate);
    pendingSeek.store(samples, std::memory_order_relaxed);
    seekGeneration.fetch_add(1, std::memory_order_release);
}

double MixEngine::getPosition() const noexcept
{
    if (sampleRate <= 0.0)
        return 0.0;

    return (double) playhead.load(std::memory_order_relaxed) / sampleRate;
}

double MixEngine::getDuration() const noexcept
{
    if (sampleRate <= 0.0)
        return 0.0;

    return (double) durationSamples() / sampleRate;
}

void MixEngine::channelTargets(int index, float& pflL, float& pflR, float& outL, float& outR) const noexcept
{
    const auto& strip = model.channel(index);
    float panL = 0.0f;
    float panR = 0.0f;
    panGains(strip.pan.load(std::memory_order_relaxed), panL, panR);

    const float fader = faderToGain(strip.fader.load(std::memory_order_relaxed));
    const float enabled = strip.on.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f;

    pflL = panL;
    pflR = panR;
    outL = panL * fader * enabled;
    outR = panR * fader * enabled;
}

void MixEngine::snapSmoothers() noexcept
{
    const auto groups = readGroupControl(model);
    bool anySolo = false;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto& strip = model.channel(channel);
        const auto group = groupFor(strip.dcaMask.load(std::memory_order_relaxed),
                                    strip.muteMask.load(std::memory_order_relaxed),
                                    groups);
        float pflL = 0.0f, pflR = 0.0f, outL = 0.0f, outR = 0.0f;
        channelTargets(channel, pflL, pflR, outL, outR);
        const float scale = group.muted ? 0.0f : group.level;
        auto& state = smooth[(size_t) channel];
        state.curL = outL * scale;
        state.curR = outR * scale;
        state.curPflL = pflL;
        state.curPflR = pflR;
        state.curPost = faderToGain(strip.fader.load(std::memory_order_relaxed))
                      * (strip.on.load(std::memory_order_relaxed) != 0 ? scale : 0.0f);
        state.curMono = faderToGain(strip.monoLevel.load(std::memory_order_relaxed))
                      * (strip.monoAssign.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f);
        anySolo = anySolo || strip.solo.load(std::memory_order_relaxed) != 0 || group.solo;

        for (int bus = 0; bus < numBuses; ++bus)
            sendSmooth[(size_t) channel][(size_t) bus] = faderToGain(strip.sendLevel[bus].load(std::memory_order_relaxed));
    }

    for (int bus = 0; bus < numBuses; ++bus)
    {
        const auto& strip = model.bus(bus);
        const auto group = groupFor(strip.dcaMask.load(std::memory_order_relaxed),
                                    strip.muteMask.load(std::memory_order_relaxed),
                                    groups);
        const float pan = strip.pan.load(std::memory_order_relaxed);
        const float balanceL = pan <= 0.5f ? 1.0f : std::clamp((1.0f - pan) * 2.0f, 0.0f, 1.0f);
        const float balanceR = pan >= 0.5f ? 1.0f : std::clamp(pan * 2.0f, 0.0f, 1.0f);
        const float scale = group.muted ? 0.0f : group.level;
        const float gain = faderToGain(strip.fader.load(std::memory_order_relaxed))
                         * (strip.on.load(std::memory_order_relaxed) != 0 ? scale : 0.0f);
        busSmoothL[(size_t) bus] = balanceL * gain;
        busSmoothR[(size_t) bus] = balanceR * gain;
        busPostSmooth[(size_t) bus] = gain;
        busMonoSmooth[(size_t) bus] = faderToGain(strip.monoLevel.load(std::memory_order_relaxed))
                                    * (strip.toMono.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f);

        for (int matrix = 0; matrix < numMatrices; ++matrix)
            busMatrixSmooth[(size_t) bus][(size_t) matrix] = faderToGain(strip.matrixLevel[matrix].load(std::memory_order_relaxed));
    }

    for (int matrix = 0; matrix < numMatrices; ++matrix)
    {
        const auto& strip = model.matrix(matrix);
        const auto group = groupFor(strip.dcaMask.load(std::memory_order_relaxed),
                                    strip.muteMask.load(std::memory_order_relaxed),
                                    groups);
        const float scale = group.muted ? 0.0f : group.level;
        matrixSmooth[(size_t) matrix] = faderToGain(strip.fader.load(std::memory_order_relaxed))
                                      * (strip.on.load(std::memory_order_relaxed) != 0 ? scale : 0.0f);
        mainStereoMatrixSmooth[(size_t) matrix] = faderToGain(model.mainMatrixLevel(false, matrix).load(std::memory_order_relaxed));
        mainMonoMatrixSmooth[(size_t) matrix] = faderToGain(model.mainMatrixLevel(true, matrix).load(std::memory_order_relaxed));
    }

    const float mainFader = faderToGain(model.mainStereo().fader.load(std::memory_order_relaxed));
    const float mainOn = model.mainStereo().on.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f;
    mainGain = mainFader * mainOn;
    const float monoFader = faderToGain(model.mainCenter().fader.load(std::memory_order_relaxed));
    const float monoOn = model.mainCenter().on.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f;
    mainMonoGain = monoFader * monoOn;

    const auto listen = model.getListenMode();
    const bool wantSolo = listen == ListenMode::solo || (listen == ListenMode::automatic && anySolo);
    listenFade = wantSolo ? 1.0f : 0.0f;
}

void MixEngine::processChunk(float* left, float* right, int numSamples, juce::int64 startSample, bool running) noexcept
{
    if (scratchCapacity < numSamples || left == nullptr)
        return;

    std::array<int, numChannels> sourceOf {};
    std::array<char, maxSources> used {};

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const int route = model.channel(channel).source.load(std::memory_order_relaxed);
        const int source = model.getInputSource(route);
        const bool active = source >= 0 && source < maxSources && sources[(size_t) source].isActive();
        sourceOf[(size_t) channel] = active ? source : -1;

        if (active)
            used[(size_t) source] = 1;
    }

    for (int source = 0; source < maxSources; ++source)
    {
        if (used[(size_t) source] == 0)
            continue;

        auto* dest = scratch.data() + (size_t) source * (size_t) scratchCapacity;

        if (running)
            sources[(size_t) source].readAt(dest, startSample, numSamples);
        else
        {
            sources[(size_t) source].hint(startSample);
            std::fill_n(dest, numSamples, 0.0f);
        }
    }

    std::fill_n(processed.begin(), (size_t) numChannels * (size_t) scratchCapacity, 0.0f);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (channel + 1 < numChannels && model.isChannelLinked(channel) && (channel % 2) == 0)
        {
            const int partner = channel + 1;
            const int sourceA = sourceOf[(size_t) channel];
            const int sourceB = sourceOf[(size_t) partner];
            const float* inputA = sourceA >= 0 ? scratch.data() + (size_t) sourceA * (size_t) scratchCapacity : nullptr;
            const float* inputB = sourceB >= 0 ? scratch.data() + (size_t) sourceB * (size_t) scratchCapacity : nullptr;
            channelDsp[(size_t) channel].processLinked(channelDsp[(size_t) partner],
                                                       inputA,
                                                       inputB,
                                                       processed.data() + (size_t) channel * (size_t) scratchCapacity,
                                                       processed.data() + (size_t) partner * (size_t) scratchCapacity,
                                                       numSamples,
                                                       model.channel(channel));
            ++channel;
            continue;
        }

        const int source = sourceOf[(size_t) channel];

        if (source < 0)
            continue;

        channelDsp[(size_t) channel].process(scratch.data() + (size_t) source * (size_t) scratchCapacity,
                                             processed.data() + (size_t) channel * (size_t) scratchCapacity,
                                             numSamples,
                                             model.channel(channel));
    }

    std::fill_n(mainBufL.begin(), numSamples, 0.0f);
    std::fill_n(mainBufR.begin(), numSamples, 0.0f);
    std::fill_n(soloBufL.begin(), numSamples, 0.0f);
    std::fill_n(soloBufR.begin(), numSamples, 0.0f);

    std::array<float, numChannels> targetL {};
    std::array<float, numChannels> targetR {};
    std::array<float, numChannels> targetPflL {};
    std::array<float, numChannels> targetPflR {};
    std::array<char, numChannels> soloed {};
    std::array<char, numChannels> toMain {};
    std::array<char, numChannels> audible {};
    std::array<float, numChannels> postTarget {};
    std::array<float, numChannels> monoTarget {};
    std::array<std::array<float, numBuses>, numChannels> sendTarget {};
    std::array<std::array<char, numBuses>, numChannels> sendTap {};
    std::array<float, numBuses> busTargetL {};
    std::array<float, numBuses> busTargetR {};
    std::array<float, numBuses> busPostTarget {};
    std::array<float, numBuses> busMonoTarget {};
    std::array<std::array<float, numMatrices>, numBuses> busMatrixTarget {};
    std::array<std::array<char, numMatrices>, numBuses> busMatrixPre {};
    std::array<char, numBuses> busToMain {};
    std::array<char, numChannels> dcaSoloed {};
    std::array<char, numBuses> busDcaSolo {};
    bool anySolo = false;
    const auto groups = readGroupControl(model);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto& strip = model.channel(channel);
        const auto group = groupFor(strip.dcaMask.load(std::memory_order_relaxed),
                                    strip.muteMask.load(std::memory_order_relaxed),
                                    groups);
        const float scale = group.muted ? 0.0f : group.level;
        channelTargets(channel, targetPflL[(size_t) channel], targetPflR[(size_t) channel],
                       targetL[(size_t) channel], targetR[(size_t) channel]);
        targetL[(size_t) channel] *= scale;
        targetR[(size_t) channel] *= scale;
        soloed[(size_t) channel] = strip.solo.load(std::memory_order_relaxed) != 0 ? 1 : 0;
        dcaSoloed[(size_t) channel] = group.solo ? 1 : 0;
        toMain[(size_t) channel] = strip.mainAssign.load(std::memory_order_relaxed) != 0 ? 1 : 0;
        audible[(size_t) channel] = strip.on.load(std::memory_order_relaxed) != 0 && ! group.muted ? 1 : 0;
        postTarget[(size_t) channel] = faderToGain(strip.fader.load(std::memory_order_relaxed)) * audible[(size_t) channel] * group.level;
        monoTarget[(size_t) channel] = faderToGain(strip.monoLevel.load(std::memory_order_relaxed))
                                     * (strip.monoAssign.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f);
        anySolo = anySolo || soloed[(size_t) channel] != 0 || dcaSoloed[(size_t) channel] != 0;

        for (int bus = 0; bus < numBuses; ++bus)
        {
            sendTarget[(size_t) channel][(size_t) bus] = faderToGain(strip.sendLevel[bus].load(std::memory_order_relaxed));
            sendTap[(size_t) channel][(size_t) bus] = (char) std::clamp(strip.sendPre[bus].load(std::memory_order_relaxed), 0, 4);
        }
    }

    for (int bus = 0; bus < numBuses; ++bus)
    {
        const auto& strip = model.bus(bus);
        const auto group = groupFor(strip.dcaMask.load(std::memory_order_relaxed),
                                    strip.muteMask.load(std::memory_order_relaxed),
                                    groups);
        const float scale = group.muted ? 0.0f : group.level;
        const float pan = strip.pan.load(std::memory_order_relaxed);
        const float balanceL = pan <= 0.5f ? 1.0f : std::clamp((1.0f - pan) * 2.0f, 0.0f, 1.0f);
        const float balanceR = pan >= 0.5f ? 1.0f : std::clamp(pan * 2.0f, 0.0f, 1.0f);
        const float gain = faderToGain(strip.fader.load(std::memory_order_relaxed))
                         * (strip.on.load(std::memory_order_relaxed) != 0 ? scale : 0.0f);
        busDcaSolo[(size_t) bus] = group.solo ? 1 : 0;
        anySolo = anySolo || group.solo;
        busTargetL[(size_t) bus] = balanceL * gain;
        busTargetR[(size_t) bus] = balanceR * gain;
        busPostTarget[(size_t) bus] = gain;
        busMonoTarget[(size_t) bus] = faderToGain(strip.monoLevel.load(std::memory_order_relaxed))
                                    * (strip.toMono.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f);
        busToMain[(size_t) bus] = strip.toMain.load(std::memory_order_relaxed) != 0 ? 1 : 0;

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            busMatrixTarget[(size_t) bus][(size_t) matrix] = faderToGain(strip.matrixLevel[matrix].load(std::memory_order_relaxed));
            busMatrixPre[(size_t) bus][(size_t) matrix] = strip.matrixPre[matrix].load(std::memory_order_relaxed) != 0 ? 1 : 0;
        }
    }

    std::array<float, numMatrices> matrixTarget {};
    std::array<char, numMatrices> matrixInsertPost {};
    std::array<float, numMatrices> mainStereoMatrixTarget {};
    std::array<float, numMatrices> mainMonoMatrixTarget {};
    const int speakerSource = std::clamp(model.getSpeakerSource(), 0, numBuses);

    for (int matrix = 0; matrix < numMatrices; ++matrix)
    {
        const auto& strip = model.matrix(matrix);
        const auto group = groupFor(strip.dcaMask.load(std::memory_order_relaxed),
                                    strip.muteMask.load(std::memory_order_relaxed),
                                    groups);
        const float scale = group.muted ? 0.0f : group.level;
        matrixTarget[(size_t) matrix] = faderToGain(strip.fader.load(std::memory_order_relaxed))
                                      * (strip.on.load(std::memory_order_relaxed) != 0 ? scale : 0.0f);
        matrixInsertPost[(size_t) matrix] = (char) strip.insertPost.load(std::memory_order_relaxed);
        mainStereoMatrixTarget[(size_t) matrix] = faderToGain(model.mainMatrixLevel(false, matrix).load(std::memory_order_relaxed));
        mainMonoMatrixTarget[(size_t) matrix] = faderToGain(model.mainMatrixLevel(true, matrix).load(std::memory_order_relaxed));
    }

    const auto listen = model.getListenMode();
    const bool wantSolo = listen == ListenMode::solo || (listen == ListenMode::automatic && anySolo);
    const float listenTarget = wantSolo ? 1.0f : 0.0f;
    const float mainTarget = faderToGain(model.mainStereo().fader.load(std::memory_order_relaxed))
                           * (model.mainStereo().on.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f);

    for (int bus = 0; bus < numBuses; ++bus)
        busDsp[(size_t) bus].configure(model.bus(bus));

    for (int matrix = 0; matrix < numMatrices; ++matrix)
    {
        matrixDsp[(size_t) matrix].configure(model.matrix(matrix));
        matrixDsp[(size_t) matrix].configureGate(model.matrix(matrix).gate);
    }

    mainDsp.configure(model.mainStereo());
    centerDsp.configure(model.mainCenter());
    const bool foldCenter = model.centerFeedsStereo();

    std::array<float, numChannels> peaks {};

    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<float, numBuses> busSumL {};
        std::array<float, numBuses> busSumR {};
        float monoSum = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& state = smooth[(size_t) channel];
            state.curL += (targetL[(size_t) channel] - state.curL) * smoothCoeff;
            state.curR += (targetR[(size_t) channel] - state.curR) * smoothCoeff;
            state.curPflL += (targetPflL[(size_t) channel] - state.curPflL) * smoothCoeff;
            state.curPflR += (targetPflR[(size_t) channel] - state.curPflR) * smoothCoeff;
            state.curPost += (postTarget[(size_t) channel] - state.curPost) * smoothCoeff;
            state.curMono += (monoTarget[(size_t) channel] - state.curMono) * smoothCoeff;

            const int source = sourceOf[(size_t) channel];

            if (source < 0)
                continue;

            const float input = processed[(size_t) channel * (size_t) scratchCapacity + (size_t) sample];
            const float leftSample = input * state.curL;
            const float rightSample = input * state.curR;
            monoSum += input * state.curPost * state.curMono;
            peaks[(size_t) channel] = std::max(peaks[(size_t) channel], std::max(std::abs(leftSample), std::abs(rightSample)));

            if (toMain[(size_t) channel] != 0)
            {
                mainBufL[(size_t) sample] += leftSample;
                mainBufR[(size_t) sample] += rightSample;
            }

            if (soloed[(size_t) channel] != 0)
            {
                soloBufL[(size_t) sample] += input * state.curPflL;
                soloBufR[(size_t) sample] += input * state.curPflR;
            }
            else if (dcaSoloed[(size_t) channel] != 0)
            {
                soloBufL[(size_t) sample] += leftSample;
                soloBufR[(size_t) sample] += rightSample;
            }

            for (int bus = 0; bus < numBuses; ++bus)
            {
                auto& gain = sendSmooth[(size_t) channel][(size_t) bus];
                const float target = sendTarget[(size_t) channel][(size_t) bus];
                gain += (target - gain) * smoothCoeff;

                const int tap = sendTap[(size_t) channel][(size_t) bus];
                const bool subgroup = tap == 4;

                if (! subgroup && gain <= 1.0e-6f)
                    continue;

                const float send = subgroup ? 1.0f : gain;
                const float mute = audible[(size_t) channel] != 0 ? 1.0f : 0.0f;

                if (tap == 1 || tap == 2 || tap == 3)
                {
                    const float tapped = tap == 2 ? channelDsp[(size_t) channel].preEqSample(sample)
                                       : tap == 3 ? channelDsp[(size_t) channel].postEqSample(sample)
                                                  : input;
                    busSumL[(size_t) bus] += tapped * state.curPflL * mute * send;
                    busSumR[(size_t) bus] += tapped * state.curPflR * mute * send;
                }
                else
                {
                    busSumL[(size_t) bus] += leftSample * send;
                    busSumR[(size_t) bus] += rightSample * send;
                }
            }
        }

        for (int bus = 0; bus < numBuses; ++bus)
        {
            busDsp[(size_t) bus].process(busSumL[(size_t) bus], busSumR[(size_t) bus]);
            busSmoothL[(size_t) bus] += (busTargetL[(size_t) bus] - busSmoothL[(size_t) bus]) * smoothCoeff;
            busSmoothR[(size_t) bus] += (busTargetR[(size_t) bus] - busSmoothR[(size_t) bus]) * smoothCoeff;
            busPostSmooth[(size_t) bus] += (busPostTarget[(size_t) bus] - busPostSmooth[(size_t) bus]) * smoothCoeff;
            busMonoSmooth[(size_t) bus] += (busMonoTarget[(size_t) bus] - busMonoSmooth[(size_t) bus]) * smoothCoeff;

            if (busToMain[(size_t) bus] != 0)
            {
                mainBufL[(size_t) sample] += busSumL[(size_t) bus] * busSmoothL[(size_t) bus];
                mainBufR[(size_t) sample] += busSumR[(size_t) bus] * busSmoothR[(size_t) bus];
            }

            const float busMono = 0.5f * (busSumL[(size_t) bus] + busSumR[(size_t) bus]);
            monoSum += busMono * busPostSmooth[(size_t) bus] * busMonoSmooth[(size_t) bus];

            if (busDcaSolo[(size_t) bus] != 0)
            {
                soloBufL[(size_t) sample] += busSumL[(size_t) bus] * busSmoothL[(size_t) bus];
                soloBufR[(size_t) sample] += busSumR[(size_t) bus] * busSmoothR[(size_t) bus];
            }
        }

        mainGain += (mainTarget - mainGain) * smoothCoeff;
        const float mainMonoTarget = faderToGain(model.mainCenter().fader.load(std::memory_order_relaxed))
                                   * (model.mainCenter().on.load(std::memory_order_relaxed) != 0 ? 1.0f : 0.0f);
        mainMonoGain += (mainMonoTarget - mainMonoGain) * smoothCoeff;

        constexpr float centerGain = 0.70710678f;
        mainDsp.process(mainBufL[(size_t) sample], mainBufR[(size_t) sample]);
        float centerSample = monoSum;
        float centerPair = monoSum;
        centerDsp.process(centerSample, centerPair);
        const float lrL = mainBufL[(size_t) sample] * mainGain;
        const float lrR = mainBufR[(size_t) sample] * mainGain;
        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            matrixSmooth[(size_t) matrix] += (matrixTarget[(size_t) matrix] - matrixSmooth[(size_t) matrix]) * smoothCoeff;
            mainStereoMatrixSmooth[(size_t) matrix] += (mainStereoMatrixTarget[(size_t) matrix] - mainStereoMatrixSmooth[(size_t) matrix]) * smoothCoeff;
            mainMonoMatrixSmooth[(size_t) matrix] += (mainMonoMatrixTarget[(size_t) matrix] - mainMonoMatrixSmooth[(size_t) matrix]) * smoothCoeff;

            for (int bus = 0; bus < numBuses; ++bus)
            {
                auto& send = busMatrixSmooth[(size_t) bus][(size_t) matrix];
                const float target = busMatrixTarget[(size_t) bus][(size_t) matrix];
                send += (target - send) * smoothCoeff;
            }
        }

        const float center = foldCenter ? centerSample * mainMonoGain * centerGain : 0.0f;
        float programL = lrL + center;
        float programR = lrR + center;

        for (int matrix = 0; matrix < numMatrices; ++matrix)
        {
            float matrixL = 0.0f;
            float matrixR = 0.0f;

            for (int bus = 0; bus < numBuses; ++bus)
            {
                const float send = busMatrixSmooth[(size_t) bus][(size_t) matrix];
                const bool pre = busMatrixPre[(size_t) bus][(size_t) matrix] != 0;
                matrixL += (pre ? busSumL[(size_t) bus] : busSumL[(size_t) bus] * busSmoothL[(size_t) bus]) * send;
                matrixR += (pre ? busSumR[(size_t) bus] : busSumR[(size_t) bus] * busSmoothR[(size_t) bus]) * send;
            }

            matrixL += lrL * mainStereoMatrixSmooth[(size_t) matrix];
            matrixR += lrR * mainStereoMatrixSmooth[(size_t) matrix];
            matrixL += centerSample * mainMonoGain * mainMonoMatrixSmooth[(size_t) matrix];
            matrixR += centerSample * mainMonoGain * mainMonoMatrixSmooth[(size_t) matrix];

            if (matrixInsertPost[(size_t) matrix] == 0)
                matrixDsp[(size_t) matrix].process(matrixL, matrixR);

            matrixL *= matrixSmooth[(size_t) matrix];
            matrixR *= matrixSmooth[(size_t) matrix];

            if (matrixInsertPost[(size_t) matrix] != 0)
                matrixDsp[(size_t) matrix].process(matrixL, matrixR);
            juce::ignoreUnused(matrixL, matrixR);
        }

        if (speakerSource > 0)
        {
            const int bus = speakerSource - 1;
            programL = busSumL[(size_t) bus] * busSmoothL[(size_t) bus];
            programR = busSumR[(size_t) bus] * busSmoothR[(size_t) bus];
        }

        listenFade += (listenTarget - listenFade) * smoothCoeff;

        const float fade = listenFade;
        const float mixedL = programL * (1.0f - fade) + soloBufL[(size_t) sample] * fade;
        const float mixedR = programR * (1.0f - fade) + soloBufR[(size_t) sample] * fade;
        left[sample] = mixedL;

        if (right != nullptr)
            right[sample] = mixedR;
        else
            left[sample] = 0.5f * (mixedL + mixedR);
    }

    float outputPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        outputPeak = std::max(outputPeak, std::abs(left[sample]));

        if (right != nullptr)
            outputPeak = std::max(outputPeak, std::abs(right[sample]));
    }

    const float decay = std::exp(-(float) numSamples / (float) (sampleRate * 0.3));

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const float previous = channelMeters[(size_t) channel].load(std::memory_order_relaxed);
        channelMeters[(size_t) channel].store(std::max(peaks[(size_t) channel], previous * decay),
                                              std::memory_order_relaxed);
    }

    const float previousMain = mainMeter.load(std::memory_order_relaxed);
    mainMeter.store(std::max(outputPeak, previousMain * decay), std::memory_order_relaxed);
}

float MixEngine::getChannelMeter(int index) const noexcept
{
    if (index < 0 || index >= numChannels)
        return 0.0f;

    return channelMeters[(size_t) index].load(std::memory_order_relaxed);
}

float MixEngine::getMainMeter() const noexcept
{
    return mainMeter.load(std::memory_order_relaxed);
}

int MixEngine::bufferedSamples(int index) const noexcept
{
    if (index < 0 || index >= maxSources)
        return 0;

    return sources[(size_t) index].available();
}

int MixEngine::sourceStatus(int index) const noexcept
{
    if (index < 0 || index >= maxSources)
        return 0;

    return sources[(size_t) index].statusCode();
}

juce::String MixEngine::sourceError(int index) const
{
    if (index < 0 || index >= maxSources)
        return {};

    return sources[(size_t) index].errorMessage();
}

} // namespace vc
