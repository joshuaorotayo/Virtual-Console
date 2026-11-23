// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ConsoleModel.h"

#include <algorithm>

namespace vc
{
namespace
{

juce::StringArray tokensOf(const juce::String& path)
{
    auto text = path.trim();

    if (text.startsWithChar('/'))
        text = text.substring(1);

    return juce::StringArray::fromTokens(text, "/", "");
}

int indexOf(const juce::String& text, int count)
{
    if (text.isEmpty() || ! text.containsOnly("0123456789"))
        return -1;

    const int value = text.getIntValue();

    if (value < 1 || value > count)
        return -1;

    return value - 1;
}

float clampUnit(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

bool applyStagebox(Stagebox& box, const juce::String& name)
{
    struct Spec
    {
        const char* name;
        int inputs;
        int outputs;
        int kind;
    };

    static constexpr Spec table[] {
        { "none", 0, 0, 0 },
        { "S16", 16, 8, 1 },
        { "S32", 32, 16, 2 },
        { "SD8", 8, 8, 3 },
        { "SD16", 16, 8, 4 },
        { "DL16", 16, 8, 5 },
        { "DL32", 32, 16, 6 },
    };

    for (const auto& spec : table)
    {
        if (name.equalsIgnoreCase(spec.name))
        {
            box.inputs = spec.inputs;
            box.outputs = spec.outputs;
            box.kind = spec.kind;
            return true;
        }
    }

    return false;
}

juce::String stageboxName(const Stagebox& box)
{
    struct Spec
    {
        const char* name;
        int kind;
    };

    static constexpr Spec table[] {
        { "none", 0 },
        { "S16", 1 },
        { "S32", 2 },
        { "SD8", 3 },
        { "SD16", 4 },
        { "DL16", 5 },
        { "DL32", 6 },
    };

    for (const auto& spec : table)
        if (box.kind == spec.kind)
            return spec.name;

    return "none";
}

bool setEqFloat(EqBand* bands, int bandCount, const juce::StringArray& tokens, int index, float value)
{
    if (index + 2 >= tokens.size() || tokens[index] != "eq")
        return false;

    const int band = indexOf(tokens[index + 1], bandCount);

    if (band < 0)
        return false;

    const auto leaf = tokens[index + 2];
    auto& eq = bands[band];

    if (leaf == "f")
        eq.freq = clampUnit(value);
    else if (leaf == "g")
        eq.gain = clampUnit(value);
    else if (leaf == "q")
        eq.q = clampUnit(value);
    else
        return false;

    return true;
}

bool getEqFloat(const EqBand* bands, int bandCount, const juce::StringArray& tokens, int index, float& value)
{
    if (index + 2 >= tokens.size() || tokens[index] != "eq")
        return false;

    const int band = indexOf(tokens[index + 1], bandCount);

    if (band < 0)
        return false;

    const auto leaf = tokens[index + 2];
    const auto& eq = bands[band];

    if (leaf == "f")
        value = eq.freq.load(std::memory_order_relaxed);
    else if (leaf == "g")
        value = eq.gain.load(std::memory_order_relaxed);
    else if (leaf == "q")
        value = eq.q.load(std::memory_order_relaxed);
    else
        return false;

    return true;
}

bool setEqInt(EqBand* bands, int bandCount, const juce::StringArray& tokens, int index, int value)
{
    if (index + 2 >= tokens.size() || tokens[index] != "eq")
        return false;

    const int band = indexOf(tokens[index + 1], bandCount);

    if (band < 0)
        return false;

    const auto leaf = tokens[index + 2];
    auto& eq = bands[band];

    if (leaf == "t")
        eq.type = value;
    else if (leaf == "on")
        eq.on = value != 0 ? 1 : 0;
    else
        return false;

    return true;
}

bool getEqInt(const EqBand* bands, int bandCount, const juce::StringArray& tokens, int index, int& value)
{
    if (index + 2 >= tokens.size() || tokens[index] != "eq")
        return false;

    const int band = indexOf(tokens[index + 1], bandCount);

    if (band < 0)
        return false;

    const auto leaf = tokens[index + 2];
    const auto& eq = bands[band];

    if (leaf == "t")
        value = eq.type.load(std::memory_order_relaxed);
    else if (leaf == "on")
        value = eq.on.load(std::memory_order_relaxed);
    else
        return false;

    return true;
}

bool setDynamicsFloat(Dynamics& dyn, const juce::String& leaf, float value)
{
    const float unit = clampUnit(value);

    if (leaf == "thr")
        dyn.threshold = unit;
    else if (leaf == "ratio")
        dyn.ratio = unit;
    else if (leaf == "attack")
        dyn.attack = unit;
    else if (leaf == "hold")
        dyn.hold = unit;
    else if (leaf == "release")
        dyn.release = unit;
    else if (leaf == "range")
        dyn.range = unit;
    else if (leaf == "knee")
        dyn.knee = unit;
    else if (leaf == "mgain")
        dyn.makeup = unit;
    else if (leaf == "mix")
        dyn.mix = unit;
    else
        return false;

    return true;
}

bool getDynamicsFloat(const Dynamics& dyn, const juce::String& leaf, float& value)
{
    if (leaf == "thr")
        value = dyn.threshold.load(std::memory_order_relaxed);
    else if (leaf == "ratio")
        value = dyn.ratio.load(std::memory_order_relaxed);
    else if (leaf == "attack")
        value = dyn.attack.load(std::memory_order_relaxed);
    else if (leaf == "hold")
        value = dyn.hold.load(std::memory_order_relaxed);
    else if (leaf == "release")
        value = dyn.release.load(std::memory_order_relaxed);
    else if (leaf == "range")
        value = dyn.range.load(std::memory_order_relaxed);
    else if (leaf == "knee")
        value = dyn.knee.load(std::memory_order_relaxed);
    else if (leaf == "mgain")
        value = dyn.makeup.load(std::memory_order_relaxed);
    else if (leaf == "mix")
        value = dyn.mix.load(std::memory_order_relaxed);
    else
        return false;

    return true;
}

bool setMixFloat(MixPoint& mix, const juce::StringArray& tokens, int index, float value, int eqBands)
{
    if (index >= tokens.size())
        return false;

    if (tokens[index] == "mix" && index + 1 < tokens.size() && ! tokens[index + 1].containsOnly("0123456789"))
    {
        const auto leaf = tokens[index + 1];

        if (leaf == "fader")
        {
            mix.fader.store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        if (leaf == "pan")
        {
            mix.pan.store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        return false;
    }

    if (tokens[index] == "eq")
        return setEqFloat(mix.eq, eqBands, tokens, index, value);

    if (tokens[index] == "dyn" && index + 1 < tokens.size())
        return setDynamicsFloat(mix.comp, tokens[index + 1], value);

    return false;
}

bool getMixFloat(const MixPoint& mix, const juce::StringArray& tokens, int index, float& value, int eqBands)
{
    if (index >= tokens.size())
        return false;

    if (tokens[index] == "mix" && index + 1 < tokens.size() && ! tokens[index + 1].containsOnly("0123456789"))
    {
        const auto leaf = tokens[index + 1];

        if (leaf == "fader")
        {
            value = mix.fader.load(std::memory_order_relaxed);
            return true;
        }

        if (leaf == "pan")
        {
            value = mix.pan.load(std::memory_order_relaxed);
            return true;
        }

        return false;
    }

    if (tokens[index] == "eq")
        return getEqFloat(mix.eq, eqBands, tokens, index, value);

    if (tokens[index] == "dyn" && index + 1 < tokens.size())
        return getDynamicsFloat(mix.comp, tokens[index + 1], value);

    return false;
}

bool setMixOn(MixPoint& mix, const juce::StringArray& tokens, int index, int value)
{
    if (index + 1 < tokens.size() && tokens[index] == "mix" && tokens[index + 1] == "on")
    {
        mix.on.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
        return true;
    }

    if (tokens[index] == "dyn" && index + 1 < tokens.size() && tokens[index + 1] == "on")
    {
        mix.comp.on = value != 0 ? 1 : 0;
        return true;
    }

    if (tokens[index] == "eq")
        return setEqInt(mix.eq, numEqBands, tokens, index, value);

    return false;
}

bool getMixOn(const MixPoint& mix, const juce::StringArray& tokens, int index, int& value)
{
    if (index + 1 < tokens.size() && tokens[index] == "mix" && tokens[index + 1] == "on")
    {
        value = mix.on.load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[index] == "dyn" && index + 1 < tokens.size() && tokens[index + 1] == "on")
    {
        value = mix.comp.on.load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[index] == "eq")
        return getEqInt(mix.eq, numEqBands, tokens, index, value);

    return false;
}

} // namespace

ConsoleModel::ConsoleModel()
{
    static constexpr float channelFreqs[] { 0.18f, 0.34f, 0.50f, 0.66f, 0.84f };
    static constexpr int channelTypes[] { 1, 2, 2, 2, 4 };

    for (int i = 0; i < numChannels; ++i)
    {
        channelNames[i] = "Ch " + juce::String(i + 1);
        channels[(size_t) i].source.store(i, std::memory_order_relaxed);

        for (int band = 0; band < numChannelEqBands; ++band)
        {
            channels[(size_t) i].eq[band].freq = channelFreqs[band];
            channels[(size_t) i].eq[band].type = channelTypes[band];
        }
    }

    for (auto& patch : inputSource)
        patch.store(-1, std::memory_order_relaxed);

    for (int i = 0; i < numBuses; ++i)
        busNames[i] = "Bus " + juce::String(i + 1);

    for (int i = 0; i < numMatrices; ++i)
        matrixNames[i] = "Mtx " + juce::String(i + 1);

    for (int i = 0; i < numFxReturns; ++i)
        fxReturnNames[i] = "Fx " + juce::String(i + 1);

    for (int i = 0; i < numAuxIns; ++i)
        auxNames[i] = "Aux " + juce::String(i + 1);

    for (int i = 0; i < numDcas; ++i)
        dcaNames[i] = "DCA " + juce::String(i + 1);

    for (auto& group : muteGroups)
        group.store(0, std::memory_order_relaxed);
}

void ConsoleModel::setListenMode(ListenMode mode) noexcept
{
    listenMode.store((int) mode, std::memory_order_relaxed);
}

ListenMode ConsoleModel::getListenMode() const noexcept
{
    return (ListenMode) listenMode.load(std::memory_order_relaxed);
}

void ConsoleModel::setSpeakerSource(int source) noexcept
{
    speakerSource.store(std::clamp(source, 0, numBuses), std::memory_order_relaxed);
}

int ConsoleModel::getSpeakerSource() const noexcept
{
    return speakerSource.load(std::memory_order_relaxed);
}

std::atomic<float>& ConsoleModel::mainMatrixLevel(bool monoBus, int matrix) noexcept
{
    return monoBus ? mainMonoMatrixLevel[(size_t) matrix] : mainStereoMatrix[(size_t) matrix];
}

const std::atomic<float>& ConsoleModel::mainMatrixLevel(bool monoBus, int matrix) const noexcept
{
    return monoBus ? mainMonoMatrixLevel[(size_t) matrix] : mainStereoMatrix[(size_t) matrix];
}

std::atomic<int>& ConsoleModel::mainMatrixPre(bool monoBus, int matrix) noexcept
{
    return monoBus ? mainMonoMatrixPre[(size_t) matrix] : mainStereoMatrixPre[(size_t) matrix];
}

int ConsoleModel::addSource(const juce::String& name, const juce::String& filePath)
{
    if (sourceCount >= maxSources || findSourceByFile(filePath) >= 0)
        return -1;

    const int index = sourceCount;
    sources[(size_t) index].name = name;
    sources[(size_t) index].file = filePath;
    sources[(size_t) index].id = nextSourceId++;
    ++sourceCount;
    return index;
}

void ConsoleModel::removeSource(int index)
{
    if (index < 0 || index >= sourceCount)
        return;

    for (int i = index; i < sourceCount - 1; ++i)
        sources[(size_t) i] = std::move(sources[(size_t) i + 1]);

    sources[(size_t) (sourceCount - 1)] = {};
    --sourceCount;

    for (auto& patch : inputSource)
    {
        int assigned = patch.load(std::memory_order_relaxed);

        if (assigned == index)
            assigned = -1;
        else if (assigned > index)
            --assigned;

        patch.store(assigned, std::memory_order_relaxed);
    }
}

int ConsoleModel::findSourceByFile(const juce::String& filePath) const
{
    if (filePath.isEmpty())
        return -1;

    for (int i = 0; i < sourceCount; ++i)
        if (sources[(size_t) i].file.equalsIgnoreCase(filePath))
            return i;

    return -1;
}

int ConsoleModel::getSourceId(int index) const
{
    if (index < 0 || index >= sourceCount)
        return 0;

    return sources[(size_t) index].id;
}

juce::String ConsoleModel::getSourceName(int index) const
{
    if (index < 0 || index >= sourceCount)
        return {};

    return sources[(size_t) index].name;
}

juce::String ConsoleModel::getSourceFile(int index) const
{
    if (index < 0 || index >= sourceCount)
        return {};

    return sources[(size_t) index].file;
}

void ConsoleModel::setChannelSource(int channelIndex, int inputIndex) noexcept
{
    if (channelIndex < 0 || channelIndex >= numChannels)
        return;

    if (inputIndex < -1 || inputIndex >= numRoutableInputs)
        inputIndex = -1;

    channels[(size_t) channelIndex].source.store(inputIndex, std::memory_order_relaxed);
}

int ConsoleModel::getChannelSource(int channelIndex) const noexcept
{
    if (channelIndex < 0 || channelIndex >= numChannels)
        return -1;

    return channels[(size_t) channelIndex].source.load(std::memory_order_relaxed);
}

void ConsoleModel::setInputSource(int inputIndex, int sourceIndex) noexcept
{
    if (inputIndex < 0 || inputIndex >= numRoutableInputs)
        return;

    if (sourceIndex < -1 || sourceIndex >= sourceCount)
        sourceIndex = -1;

    if (sourceIndex >= 0)
    {
        for (int i = 0; i < numRoutableInputs; ++i)
            if (i != inputIndex && inputSource[(size_t) i].load(std::memory_order_relaxed) == sourceIndex)
                inputSource[(size_t) i].store(-1, std::memory_order_relaxed);
    }

    inputSource[(size_t) inputIndex].store(sourceIndex, std::memory_order_relaxed);
}

int ConsoleModel::getInputSource(int inputIndex) const noexcept
{
    if (inputIndex < 0 || inputIndex >= numRoutableInputs)
        return -1;

    return inputSource[(size_t) inputIndex].load(std::memory_order_relaxed);
}

int ConsoleModel::inputForSource(int sourceIndex) const noexcept
{
    if (sourceIndex < 0)
        return -1;

    for (int i = 0; i < numRoutableInputs; ++i)
        if (inputSource[(size_t) i].load(std::memory_order_relaxed) == sourceIndex)
            return i;

    return -1;
}

int ConsoleModel::firstFreeInput() const noexcept
{
    for (int i = 0; i < numRoutableInputs; ++i)
        if (inputSource[(size_t) i].load(std::memory_order_relaxed) < 0)
            return i;

    return -1;
}

juce::String ConsoleModel::inputName(int inputIndex)
{
    if (inputIndex >= 0 && inputIndex < numLocalInputs)
        return "Local " + juce::String(inputIndex + 1);

    if (inputIndex >= numLocalInputs && inputIndex < numRoutableInputs)
        return "Aux " + juce::String(inputIndex - numLocalInputs + 1);

    return "None";
}

juce::String ConsoleModel::getChannelName(int index) const
{
    if (index < 0 || index >= numChannels)
        return {};

    return channelNames[index];
}

void ConsoleModel::setChannelName(int index, const juce::String& name)
{
    if (index < 0 || index >= numChannels)
        return;

    channelNames[index] = name;
}

bool ConsoleModel::setFloat(const juce::String& path, float value)
{
    const bool changed = assignFloat(path, value);

    if (changed)
        mirrorFloat(path, value);

    return changed;
}

bool ConsoleModel::assignFloat(const juce::String& path, float value)
{
    const auto tokens = tokensOf(path);

    if (tokens.size() < 2)
        return false;

    if (tokens[0] == "ch")
    {
        const int index = indexOf(tokens[1], numChannels);

        if (index < 0)
            return false;

        auto& strip = channels[(size_t) index];

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "trim")
        {
            strip.trim.store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "hpf")
        {
            strip.hpf = clampUnit(value);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "delay" && tokens[3] == "time")
        {
            strip.delayTime = clampUnit(value);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "dyn" && tokens[3] == "filter" && tokens[4] == "f")
        {
            strip.comp.filterFreq = clampUnit(value);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "gate")
            return setDynamicsFloat(strip.gate, tokens[3], value);

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mlevel")
        {
            strip.monoLevel.store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "level")
        {
            const int bus = indexOf(tokens[3], numBuses);

            if (bus < 0)
                return false;

            strip.sendLevel[bus].store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        return setMixFloat(strip, tokens, 2, value, numChannelEqBands);
    }

    if (tokens[0] == "bus")
    {
        const int index = indexOf(tokens[1], numBuses);

        if (index < 0)
            return false;

        auto& strip = buses[(size_t) index];

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mlevel")
        {
            strip.monoLevel.store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "level")
        {
            const int matrix = indexOf(tokens[3], numMatrices);

            if (matrix < 0)
                return false;

            strip.matrixLevel[matrix].store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        return setMixFloat(strip, tokens, 2, value, numEqBands);
    }

    if (tokens[0] == "mtx")
    {
        const int index = indexOf(tokens[1], numMatrices);
        return index >= 0 && setMixFloat(matrices[(size_t) index], tokens, 2, value, numEqBands);
    }

    if (tokens[0] == "fxrtn")
    {
        const int index = indexOf(tokens[1], numFxReturns);
        return index >= 0 && setMixFloat(fxReturns[(size_t) index], tokens, 2, value, numChannelEqBands);
    }

    if (tokens[0] == "auxin")
    {
        const int index = indexOf(tokens[1], numAuxIns);
        return index >= 0 && setMixFloat(auxIns[(size_t) index], tokens, 2, value, numChannelEqBands);
    }

    if (tokens[0] == "dca")
    {
        const int index = indexOf(tokens[1], numDcas);

        if (index < 0 || tokens.size() < 3 || tokens[2] != "fader")
            return false;

        dcas[(size_t) index].fader.store(clampUnit(value), std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "main" && tokens.size() >= 4 && tokens[2] == "mix")
    {
        if (tokens[1] != "m" && tokens[1] != "st")
            return false;

        const bool monoBus = tokens[1] == "m";

        if (tokens.size() >= 5 && tokens[3].containsOnly("0123456789") && tokens[4] == "level")
        {
            const int matrix = indexOf(tokens[3], numMatrices);

            if (matrix < 0)
                return false;

            mainMatrixLevel(monoBus, matrix).store(clampUnit(value), std::memory_order_relaxed);
            return true;
        }

        auto& mix = monoBus ? mainMono : mainSt;
        return setMixFloat(mix, tokens, 2, value, numEqBands);
    }

    if (tokens[0] == "fx" && tokens.size() >= 4 && tokens[2] == "par")
    {
        const int slot = indexOf(tokens[1], numFxSlots);
        const int param = indexOf(tokens[3], numFxParams);

        if (slot < 0 || param < 0)
            return false;

        fx[(size_t) slot].par[param] = clampUnit(value);
        return true;
    }

    return false;
}

bool ConsoleModel::getFloat(const juce::String& path, float& value) const
{
    const auto tokens = tokensOf(path);

    if (tokens.size() < 2)
        return false;

    if (tokens[0] == "ch")
    {
        const int index = indexOf(tokens[1], numChannels);

        if (index < 0)
            return false;

        const auto& strip = channels[(size_t) index];

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "trim")
        {
            value = strip.trim.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "hpf")
        {
            value = strip.hpf.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "delay" && tokens[3] == "time")
        {
            value = strip.delayTime.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "dyn" && tokens[3] == "filter" && tokens[4] == "f")
        {
            value = strip.comp.filterFreq.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "gate")
            return getDynamicsFloat(strip.gate, tokens[3], value);

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mlevel")
        {
            value = strip.monoLevel.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "level")
        {
            const int bus = indexOf(tokens[3], numBuses);

            if (bus < 0)
                return false;

            value = strip.sendLevel[bus].load(std::memory_order_relaxed);
            return true;
        }

        return getMixFloat(strip, tokens, 2, value, numChannelEqBands);
    }

    if (tokens[0] == "bus")
    {
        const int index = indexOf(tokens[1], numBuses);

        if (index < 0)
            return false;

        const auto& strip = buses[(size_t) index];

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mlevel")
        {
            value = strip.monoLevel.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "level")
        {
            const int matrix = indexOf(tokens[3], numMatrices);

            if (matrix < 0)
                return false;

            value = strip.matrixLevel[matrix].load(std::memory_order_relaxed);
            return true;
        }

        return getMixFloat(strip, tokens, 2, value, numEqBands);
    }

    if (tokens[0] == "mtx")
    {
        const int index = indexOf(tokens[1], numMatrices);
        return index >= 0 && getMixFloat(matrices[(size_t) index], tokens, 2, value, numEqBands);
    }

    if (tokens[0] == "fxrtn")
    {
        const int index = indexOf(tokens[1], numFxReturns);
        return index >= 0 && getMixFloat(fxReturns[(size_t) index], tokens, 2, value, numChannelEqBands);
    }

    if (tokens[0] == "auxin")
    {
        const int index = indexOf(tokens[1], numAuxIns);
        return index >= 0 && getMixFloat(auxIns[(size_t) index], tokens, 2, value, numChannelEqBands);
    }

    if (tokens[0] == "dca")
    {
        const int index = indexOf(tokens[1], numDcas);

        if (index < 0 || tokens.size() < 3 || tokens[2] != "fader")
            return false;

        value = dcas[(size_t) index].fader.load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "main" && tokens.size() >= 3)
    {
        if (tokens[1] != "m" && tokens[1] != "st")
            return false;

        const bool monoBus = tokens[1] == "m";

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "level")
        {
            const int matrix = indexOf(tokens[3], numMatrices);

            if (matrix < 0)
                return false;

            value = mainMatrixLevel(monoBus, matrix).load(std::memory_order_relaxed);
            return true;
        }

        const auto& mix = monoBus ? mainMono : mainSt;
        return getMixFloat(mix, tokens, 2, value, numEqBands);
    }

    if (tokens[0] == "fx" && tokens.size() >= 4 && tokens[2] == "par")
    {
        const int slot = indexOf(tokens[1], numFxSlots);
        const int param = indexOf(tokens[3], numFxParams);

        if (slot < 0 || param < 0)
            return false;

        value = fx[(size_t) slot].par[param];
        return true;
    }

    return false;
}

bool ConsoleModel::setInt(const juce::String& path, int value)
{
    const bool changed = assignInt(path, value);

    if (changed)
        mirrorInt(path, value);

    return changed;
}

bool ConsoleModel::assignInt(const juce::String& path, int value)
{
    const auto tokens = tokensOf(path);

    if (tokens.size() < 2)
        return false;

    if (tokens[0] == "-stat" && tokens.size() >= 3 && tokens[1] == "solosw")
    {
        const int index = indexOf(tokens[2], numChannels);

        if (index < 0)
            return false;

        channels[(size_t) index].solo.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "virt" && tokens.size() >= 4 && tokens[1] == "ch" && tokens[3] == "source")
    {
        const int index = indexOf(tokens[2], numChannels);

        if (index < 0)
            return false;

        setChannelSource(index, value <= 0 ? -1 : value - 1);
        return true;
    }

    if (tokens[0] == "virt" && tokens.size() >= 2 && tokens[1] == "listen")
    {
        const int mode = std::clamp(value, 0, 2);
        listenMode.store(mode, std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 3 && tokens[1] == "solo" && tokens[2] == "mode")
    {
        soloMode = value != 0 ? 1 : 0;
        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 3 && tokens[1] == "chlink")
    {
        const int pair = indexOf(tokens[2], 16);

        if (pair < 0)
            return false;

        channelLink[(size_t) pair].store(value != 0 ? 1 : 0, std::memory_order_relaxed);

        if (value != 0)
            duplicateProcessing(pair * 2, pair * 2 + 1, false);

        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 3 && tokens[1] == "mute")
    {
        const int index = indexOf(tokens[2], numMuteGroups);

        if (index < 0)
            return false;

        muteGroups[(size_t) index].store(value != 0 ? 1 : 0, std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "ch")
    {
        const int index = indexOf(tokens[1], numChannels);

        if (index < 0)
            return false;

        auto& strip = channels[(size_t) index];

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "invert")
        {
            strip.invert.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "hpon")
        {
            strip.hpfOn = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "delay" && tokens[3] == "on")
        {
            strip.delayOn = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "gate" && tokens[3] == "on")
        {
            strip.gate.on = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "gate" && tokens[3] == "mode")
        {
            strip.gate.mode = std::clamp(value, 0, 4);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "eq" && tokens[3] == "on")
        {
            strip.eqOn = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "mode")
        {
            strip.comp.mode = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "det")
        {
            strip.comp.detect = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "env")
        {
            strip.comp.envelope = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "pos")
        {
            strip.comp.position = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "auto")
        {
            strip.comp.autoTime = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "dyn" && tokens[3] == "filter" && tokens[4] == "on")
        {
            strip.comp.filterOn = value != 0 ? 1 : 0;
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "dyn" && tokens[3] == "filter" && tokens[4] == "type")
        {
            strip.comp.filterType = std::clamp(value, 0, 8);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "st")
        {
            strip.mainAssign.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mono")
        {
            strip.monoAssign.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "tap")
        {
            const int bus = indexOf(tokens[3], numBuses);

            if (bus < 0)
                return false;

            strip.sendPre[bus].store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (setMixOn(strip, tokens, 2, value))
            return true;

        return setEqInt(strip.eq, numChannelEqBands, tokens, 2, value);
    }

    if (tokens[0] == "bus")
    {
        const int index = indexOf(tokens[1], numBuses);

        if (index < 0)
            return false;

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "st")
        {
            buses[(size_t) index].toMain.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mono")
        {
            buses[(size_t) index].toMono.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "tap")
        {
            const int matrix = indexOf(tokens[3], numMatrices);

            if (matrix < 0)
                return false;

            buses[(size_t) index].matrixPre[matrix].store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        return setMixOn(buses[(size_t) index], tokens, 2, value);
    }

    if (tokens[0] == "mtx")
    {
        const int index = indexOf(tokens[1], numMatrices);

        if (index < 0)
            return false;

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "st")
        {
            matrices[(size_t) index].toMain.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mono")
        {
            matrices[(size_t) index].toMono.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }

        return setMixOn(matrices[(size_t) index], tokens, 2, value);
    }

    if (tokens[0] == "main" && tokens.size() >= 3 && (tokens[1] == "st" || tokens[1] == "m"))
    {
        auto& mix = tokens[1] == "m" ? mainMono : mainSt;
        return setMixOn(mix, tokens, 2, value);
    }

    if (tokens[0] == "dca")
    {
        const int index = indexOf(tokens[1], numDcas);

        if (index < 0 || tokens.size() < 3 || tokens[2] != "on")
            return false;

        dcas[(size_t) index].on.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "fx" && tokens.size() >= 3)
    {
        const int slot = indexOf(tokens[1], numFxSlots);

        if (slot < 0)
            return false;

        if (tokens[2] == "type")
        {
            fx[(size_t) slot].type = value;
            return true;
        }

        if (tokens[2] == "on")
        {
            fx[(size_t) slot].on.store(value != 0 ? 1 : 0, std::memory_order_relaxed);
            return true;
        }
    }

    return false;
}

bool ConsoleModel::getInt(const juce::String& path, int& value) const
{
    const auto tokens = tokensOf(path);

    if (tokens.size() < 2)
        return false;

    if (tokens[0] == "-stat" && tokens.size() >= 3 && tokens[1] == "solosw")
    {
        const int index = indexOf(tokens[2], numChannels);

        if (index < 0)
            return false;

        value = channels[(size_t) index].solo.load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "virt" && tokens.size() >= 4 && tokens[1] == "ch" && tokens[3] == "source")
    {
        const int index = indexOf(tokens[2], numChannels);

        if (index < 0)
            return false;

        const int source = getChannelSource(index);
        value = source < 0 ? 0 : source + 1;
        return true;
    }

    if (tokens[0] == "virt" && tokens.size() >= 2 && tokens[1] == "listen")
    {
        value = listenMode.load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 3 && tokens[1] == "solo" && tokens[2] == "mode")
    {
        value = soloMode;
        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 3 && tokens[1] == "chlink")
    {
        const int pair = indexOf(tokens[2], 16);

        if (pair < 0)
            return false;

        value = channelLink[(size_t) pair].load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 3 && tokens[1] == "mute")
    {
        const int index = indexOf(tokens[2], numMuteGroups);

        if (index < 0)
            return false;

        value = muteGroups[(size_t) index].load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 4 && tokens[1] == "aes50")
    {
        const Stagebox* box = tokens[2] == "A" ? &aes50A : tokens[2] == "B" ? &aes50B : nullptr;

        if (box == nullptr)
            return false;

        if (tokens[3] == "inputs")
        {
            value = box->inputs;
            return true;
        }

        if (tokens[3] == "outputs")
        {
            value = box->outputs;
            return true;
        }
    }

    if (tokens[0] == "ch")
    {
        const int index = indexOf(tokens[1], numChannels);

        if (index < 0)
            return false;

        const auto& strip = channels[(size_t) index];

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "invert")
        {
            value = strip.invert.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "preamp" && tokens[3] == "hpon")
        {
            value = strip.hpfOn.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "delay" && tokens[3] == "on")
        {
            value = strip.delayOn.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "gate" && tokens[3] == "on")
        {
            value = strip.gate.on.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "gate" && tokens[3] == "mode")
        {
            value = strip.gate.mode.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "eq" && tokens[3] == "on")
        {
            value = strip.eqOn.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "mode")
        {
            value = strip.comp.mode.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "det")
        {
            value = strip.comp.detect.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "env")
        {
            value = strip.comp.envelope.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "pos")
        {
            value = strip.comp.position.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "dyn" && tokens[3] == "auto")
        {
            value = strip.comp.autoTime.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "dyn" && tokens[3] == "filter" && tokens[4] == "on")
        {
            value = strip.comp.filterOn.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "dyn" && tokens[3] == "filter" && tokens[4] == "type")
        {
            value = strip.comp.filterType.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "st")
        {
            value = strip.mainAssign.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mono")
        {
            value = strip.monoAssign.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "tap")
        {
            const int bus = indexOf(tokens[3], numBuses);

            if (bus < 0)
                return false;

            value = strip.sendPre[bus].load(std::memory_order_relaxed);
            return true;
        }

        if (getMixOn(strip, tokens, 2, value))
            return true;

        return getEqInt(strip.eq, numChannelEqBands, tokens, 2, value);
    }

    if (tokens[0] == "bus")
    {
        const int index = indexOf(tokens[1], numBuses);

        if (index < 0)
            return false;

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "st")
        {
            value = buses[(size_t) index].toMain.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mono")
        {
            value = buses[(size_t) index].toMono.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 5 && tokens[2] == "mix" && tokens[3].containsOnly("0123456789") && tokens[4] == "tap")
        {
            const int matrix = indexOf(tokens[3], numMatrices);

            if (matrix < 0)
                return false;

            value = buses[(size_t) index].matrixPre[matrix].load(std::memory_order_relaxed);
            return true;
        }

        return getMixOn(buses[(size_t) index], tokens, 2, value);
    }

    if (tokens[0] == "mtx")
    {
        const int index = indexOf(tokens[1], numMatrices);

        if (index < 0)
            return false;

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "st")
        {
            value = matrices[(size_t) index].toMain.load(std::memory_order_relaxed);
            return true;
        }

        if (tokens.size() >= 4 && tokens[2] == "mix" && tokens[3] == "mono")
        {
            value = matrices[(size_t) index].toMono.load(std::memory_order_relaxed);
            return true;
        }

        return getMixOn(matrices[(size_t) index], tokens, 2, value);
    }

    if (tokens[0] == "main" && tokens.size() >= 3 && (tokens[1] == "st" || tokens[1] == "m"))
    {
        const auto& mix = tokens[1] == "m" ? mainMono : mainSt;
        return getMixOn(mix, tokens, 2, value);
    }

    if (tokens[0] == "dca")
    {
        const int index = indexOf(tokens[1], numDcas);

        if (index < 0 || tokens.size() < 3 || tokens[2] != "on")
            return false;

        value = dcas[(size_t) index].on.load(std::memory_order_relaxed);
        return true;
    }

    if (tokens[0] == "fx" && tokens.size() >= 3)
    {
        const int slot = indexOf(tokens[1], numFxSlots);

        if (slot < 0)
            return false;

        if (tokens[2] == "type")
        {
            value = fx[(size_t) slot].type;
            return true;
        }

        if (tokens[2] == "on")
        {
            value = fx[(size_t) slot].on.load(std::memory_order_relaxed);
            return true;
        }
    }

    return false;
}

bool ConsoleModel::setString(const juce::String& path, const juce::String& value)
{
    const auto tokens = tokensOf(path);

    if (tokens.isEmpty())
        return false;

    if (tokens[0] == "ch" && tokens.size() >= 4 && tokens[2] == "config" && tokens[3] == "name")
    {
        const int index = indexOf(tokens[1], numChannels);

        if (index < 0)
            return false;

        channelNames[index] = value.substring(0, 12);
        return true;
    }

    if (tokens[0] == "virt" && tokens.size() >= 4 && tokens[1] == "source")
    {
        const int index = indexOf(tokens[2], sourceCount);

        if (index < 0)
            return false;

        if (tokens[3] == "name")
            sources[(size_t) index].name = value;
        else if (tokens[3] == "file")
            sources[(size_t) index].file = value;
        else
            return false;

        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 4 && tokens[1] == "aes50" && tokens[3] == "box")
    {
        if (tokens[2] == "A")
            return applyStagebox(aes50A, value);
        if (tokens[2] == "B")
            return applyStagebox(aes50B, value);
    }

    return false;
}

bool ConsoleModel::getString(const juce::String& path, juce::String& value) const
{
    const auto tokens = tokensOf(path);

    if (tokens.isEmpty())
        return false;

    if (tokens[0] == "ch" && tokens.size() >= 4 && tokens[2] == "config" && tokens[3] == "name")
    {
        const int index = indexOf(tokens[1], numChannels);

        if (index < 0)
            return false;

        value = channelNames[index];
        return true;
    }

    if (tokens[0] == "virt" && tokens.size() >= 4 && tokens[1] == "source")
    {
        const int index = indexOf(tokens[2], sourceCount);

        if (index < 0)
            return false;

        if (tokens[3] == "name")
            value = sources[(size_t) index].name;
        else if (tokens[3] == "file")
            value = sources[(size_t) index].file;
        else
            return false;

        return true;
    }

    if (tokens[0] == "config" && tokens.size() >= 4 && tokens[1] == "aes50" && tokens[3] == "box")
    {
        if (tokens[2] == "A")
        {
            value = stageboxName(aes50A);
            return true;
        }

        if (tokens[2] == "B")
        {
            value = stageboxName(aes50B);
            return true;
        }
    }

    return false;
}

namespace
{
bool mirrorsAcrossLink(const juce::StringArray& tokens)
{
    if (tokens.size() < 3 || tokens[0] != "ch")
        return false;

    if (tokens[2] == "mix" && tokens.size() >= 4 && tokens[3] == "pan")
        return false;

    if (tokens[2] == "mix" && tokens.size() >= 4 && tokens[3].containsOnly("0123456789"))
        return false;

    return tokens[2] == "mix" || tokens[2] == "preamp" || tokens[2] == "delay"
        || tokens[2] == "gate" || tokens[2] == "dyn" || tokens[2] == "eq";
}
} // namespace

bool ConsoleModel::isChannelLinked(int channelIndex) const noexcept
{
    if (channelIndex < 0 || channelIndex >= numChannels)
        return false;

    return channelLink[(size_t) (channelIndex / 2)].load(std::memory_order_relaxed) != 0;
}

void ConsoleModel::setChannelLinked(int channelIndex, bool linked)
{
    if (channelIndex < 0 || channelIndex >= numChannels)
        return;

    const int pair = channelIndex / 2;
    setInt("/config/chlink/" + juce::String(pair + 1).paddedLeft('0', 2), linked ? 1 : 0);
}

void ConsoleModel::duplicateProcessing(int fromIndex, int toIndex, bool includePan)
{
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= numChannels || toIndex >= numChannels || fromIndex == toIndex)
        return;

    const auto& from = channels[(size_t) fromIndex];
    auto& to = channels[(size_t) toIndex];
    to.trim.store(from.trim.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.invert.store(from.invert.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.fader.store(from.fader.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.on.store(from.on.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.mainAssign.store(from.mainAssign.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.monoAssign.store(from.monoAssign.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.monoLevel.store(from.monoLevel.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.hpf.store(from.hpf.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.hpfOn.store(from.hpfOn.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.eqOn.store(from.eqOn.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.delayOn.store(from.delayOn.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.delayTime.store(from.delayTime.load(std::memory_order_relaxed), std::memory_order_relaxed);

    if (includePan)
        to.pan.store(from.pan.load(std::memory_order_relaxed), std::memory_order_relaxed);

    to.gate.on.store(from.gate.on.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.gate.mode.store(from.gate.mode.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.gate.threshold.store(from.gate.threshold.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.gate.range.store(from.gate.range.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.gate.attack.store(from.gate.attack.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.gate.hold.store(from.gate.hold.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.gate.release.store(from.gate.release.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.on.store(from.comp.on.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.mode.store(from.comp.mode.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.detect.store(from.comp.detect.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.envelope.store(from.comp.envelope.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.position.store(from.comp.position.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.autoTime.store(from.comp.autoTime.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.filterOn.store(from.comp.filterOn.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.filterType.store(from.comp.filterType.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.filterFreq.store(from.comp.filterFreq.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.mix.store(from.comp.mix.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.threshold.store(from.comp.threshold.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.ratio.store(from.comp.ratio.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.attack.store(from.comp.attack.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.hold.store(from.comp.hold.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.release.store(from.comp.release.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.knee.store(from.comp.knee.load(std::memory_order_relaxed), std::memory_order_relaxed);
    to.comp.makeup.store(from.comp.makeup.load(std::memory_order_relaxed), std::memory_order_relaxed);

    for (int band = 0; band < numChannelEqBands; ++band)
    {
        to.eq[band].type.store(from.eq[band].type.load(std::memory_order_relaxed), std::memory_order_relaxed);
        to.eq[band].freq.store(from.eq[band].freq.load(std::memory_order_relaxed), std::memory_order_relaxed);
        to.eq[band].gain.store(from.eq[band].gain.load(std::memory_order_relaxed), std::memory_order_relaxed);
        to.eq[band].q.store(from.eq[band].q.load(std::memory_order_relaxed), std::memory_order_relaxed);
        to.eq[band].on.store(from.eq[band].on.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}

void ConsoleModel::mirrorFloat(const juce::String& path, float value)
{
    if (suppressMirror)
        return;

    const auto tokens = tokensOf(path);

    if (! mirrorsAcrossLink(tokens))
        return;

    const int index = indexOf(tokens[1], numChannels);

    if (index < 0 || ! isChannelLinked(index))
        return;

    auto partner = tokens;
    partner.set(1, juce::String((index ^ 1) + 1).paddedLeft('0', 2));
    suppressMirror = true;
    assignFloat("/" + partner.joinIntoString("/"), value);
    suppressMirror = false;
}

void ConsoleModel::mirrorInt(const juce::String& path, int value)
{
    if (suppressMirror)
        return;

    const auto tokens = tokensOf(path);

    if (! mirrorsAcrossLink(tokens))
        return;

    const int index = indexOf(tokens[1], numChannels);

    if (index < 0 || ! isChannelLinked(index))
        return;

    auto partner = tokens;
    partner.set(1, juce::String((index ^ 1) + 1).paddedLeft('0', 2));
    suppressMirror = true;
    assignInt("/" + partner.joinIntoString("/"), value);
    suppressMirror = false;
}

void ConsoleModel::copyChannel(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= numChannels)
        return;

    const auto& from = channels[(size_t) channelIndex];
    clipboard.trim = from.trim.load(std::memory_order_relaxed);
    clipboard.fader = from.fader.load(std::memory_order_relaxed);
    clipboard.pan = from.pan.load(std::memory_order_relaxed);
    clipboard.hpf = from.hpf.load(std::memory_order_relaxed);
    clipboard.delayTime = from.delayTime.load(std::memory_order_relaxed);
    clipboard.invert = from.invert.load(std::memory_order_relaxed);
    clipboard.on = from.on.load(std::memory_order_relaxed);
    clipboard.mainAssign = from.mainAssign.load(std::memory_order_relaxed);
    clipboard.monoAssign = from.monoAssign.load(std::memory_order_relaxed);
    clipboard.monoLevel = from.monoLevel.load(std::memory_order_relaxed);
    clipboard.hpfOn = from.hpfOn.load(std::memory_order_relaxed);
    clipboard.eqMaster = from.eqOn.load(std::memory_order_relaxed);
    clipboard.delayOn = from.delayOn.load(std::memory_order_relaxed);
    clipboard.gateOn = from.gate.on.load(std::memory_order_relaxed);
    clipboard.gateMode = from.gate.mode.load(std::memory_order_relaxed);
    clipboard.gateThr = from.gate.threshold.load(std::memory_order_relaxed);
    clipboard.gateRange = from.gate.range.load(std::memory_order_relaxed);
    clipboard.gateAttack = from.gate.attack.load(std::memory_order_relaxed);
    clipboard.gateHold = from.gate.hold.load(std::memory_order_relaxed);
    clipboard.gateRelease = from.gate.release.load(std::memory_order_relaxed);
    clipboard.compOn = from.comp.on.load(std::memory_order_relaxed);
    clipboard.compMode = from.comp.mode.load(std::memory_order_relaxed);
    clipboard.compDetect = from.comp.detect.load(std::memory_order_relaxed);
    clipboard.compEnvelope = from.comp.envelope.load(std::memory_order_relaxed);
    clipboard.compPosition = from.comp.position.load(std::memory_order_relaxed);
    clipboard.compAuto = from.comp.autoTime.load(std::memory_order_relaxed);
    clipboard.compFilterOn = from.comp.filterOn.load(std::memory_order_relaxed);
    clipboard.compFilterType = from.comp.filterType.load(std::memory_order_relaxed);
    clipboard.compFilterFreq = from.comp.filterFreq.load(std::memory_order_relaxed);
    clipboard.compMix = from.comp.mix.load(std::memory_order_relaxed);
    clipboard.compThr = from.comp.threshold.load(std::memory_order_relaxed);
    clipboard.compRatio = from.comp.ratio.load(std::memory_order_relaxed);
    clipboard.compAttack = from.comp.attack.load(std::memory_order_relaxed);
    clipboard.compHold = from.comp.hold.load(std::memory_order_relaxed);
    clipboard.compRelease = from.comp.release.load(std::memory_order_relaxed);
    clipboard.compKnee = from.comp.knee.load(std::memory_order_relaxed);
    clipboard.compMakeup = from.comp.makeup.load(std::memory_order_relaxed);

    for (int band = 0; band < numChannelEqBands; ++band)
    {
        clipboard.eqType[band] = from.eq[band].type.load(std::memory_order_relaxed);
        clipboard.eqFreq[band] = from.eq[band].freq.load(std::memory_order_relaxed);
        clipboard.eqGain[band] = from.eq[band].gain.load(std::memory_order_relaxed);
        clipboard.eqQ[band] = from.eq[band].q.load(std::memory_order_relaxed);
        clipboard.eqOn[band] = from.eq[band].on.load(std::memory_order_relaxed);
    }

    clipboardReady = true;
}

void ConsoleModel::pasteChannel(int channelIndex)
{
    if (! clipboardReady || channelIndex < 0 || channelIndex >= numChannels)
        return;

    auto& to = channels[(size_t) channelIndex];
    to.trim.store(clipboard.trim, std::memory_order_relaxed);
    to.fader.store(clipboard.fader, std::memory_order_relaxed);
    to.pan.store(clipboard.pan, std::memory_order_relaxed);
    to.hpf.store(clipboard.hpf, std::memory_order_relaxed);
    to.delayTime.store(clipboard.delayTime, std::memory_order_relaxed);
    to.invert.store(clipboard.invert, std::memory_order_relaxed);
    to.on.store(clipboard.on, std::memory_order_relaxed);
    to.mainAssign.store(clipboard.mainAssign, std::memory_order_relaxed);
    to.monoAssign.store(clipboard.monoAssign, std::memory_order_relaxed);
    to.monoLevel.store(clipboard.monoLevel, std::memory_order_relaxed);
    to.hpfOn.store(clipboard.hpfOn, std::memory_order_relaxed);
    to.eqOn.store(clipboard.eqMaster, std::memory_order_relaxed);
    to.delayOn.store(clipboard.delayOn, std::memory_order_relaxed);
    to.gate.on.store(clipboard.gateOn, std::memory_order_relaxed);
    to.gate.mode.store(clipboard.gateMode, std::memory_order_relaxed);
    to.gate.threshold.store(clipboard.gateThr, std::memory_order_relaxed);
    to.gate.range.store(clipboard.gateRange, std::memory_order_relaxed);
    to.gate.attack.store(clipboard.gateAttack, std::memory_order_relaxed);
    to.gate.hold.store(clipboard.gateHold, std::memory_order_relaxed);
    to.gate.release.store(clipboard.gateRelease, std::memory_order_relaxed);
    to.comp.on.store(clipboard.compOn, std::memory_order_relaxed);
    to.comp.mode.store(clipboard.compMode, std::memory_order_relaxed);
    to.comp.detect.store(clipboard.compDetect, std::memory_order_relaxed);
    to.comp.envelope.store(clipboard.compEnvelope, std::memory_order_relaxed);
    to.comp.position.store(clipboard.compPosition, std::memory_order_relaxed);
    to.comp.autoTime.store(clipboard.compAuto, std::memory_order_relaxed);
    to.comp.filterOn.store(clipboard.compFilterOn, std::memory_order_relaxed);
    to.comp.filterType.store(clipboard.compFilterType, std::memory_order_relaxed);
    to.comp.filterFreq.store(clipboard.compFilterFreq, std::memory_order_relaxed);
    to.comp.mix.store(clipboard.compMix, std::memory_order_relaxed);
    to.comp.threshold.store(clipboard.compThr, std::memory_order_relaxed);
    to.comp.ratio.store(clipboard.compRatio, std::memory_order_relaxed);
    to.comp.attack.store(clipboard.compAttack, std::memory_order_relaxed);
    to.comp.hold.store(clipboard.compHold, std::memory_order_relaxed);
    to.comp.release.store(clipboard.compRelease, std::memory_order_relaxed);
    to.comp.knee.store(clipboard.compKnee, std::memory_order_relaxed);
    to.comp.makeup.store(clipboard.compMakeup, std::memory_order_relaxed);

    for (int band = 0; band < numChannelEqBands; ++band)
    {
        to.eq[band].type.store(clipboard.eqType[band], std::memory_order_relaxed);
        to.eq[band].freq.store(clipboard.eqFreq[band], std::memory_order_relaxed);
        to.eq[band].gain.store(clipboard.eqGain[band], std::memory_order_relaxed);
        to.eq[band].q.store(clipboard.eqQ[band], std::memory_order_relaxed);
        to.eq[band].on.store(clipboard.eqOn[band], std::memory_order_relaxed);
    }

    if (isChannelLinked(channelIndex))
        duplicateProcessing(channelIndex, channelIndex ^ 1, true);
}

} // namespace vc
