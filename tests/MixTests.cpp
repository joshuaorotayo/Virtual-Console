#include "engine/MixEngine.h"
#include "model/DspLaw.h"
#include "model/FaderLaw.h"

#include <initializer_list>

#include <juce_audio_formats/juce_audio_formats.h>

namespace vc
{
namespace
{
void expectSamples (juce::UnitTest& test, const std::vector<float>& actual, const std::vector<float>& expected, const juce::String& label)
{
    test.expectEquals ((int) actual.size(), (int) expected.size(), label + " size");

    for (size_t i = 0; i < std::min (actual.size(), expected.size()); ++i)
        test.expectWithinAbsoluteError (actual[i], expected[i], 0.001f, label + " [" + juce::String ((int) i) + "]");
}

struct StereoBlock
{
    std::vector<float> left;
    std::vector<float> right;
};

StereoBlock renderBlock (MixEngine& engine, int samples)
{
    StereoBlock block;
    block.left.assign ((size_t) samples, 0.0f);
    block.right.assign ((size_t) samples, 0.0f);
    engine.process (block.left.data(), block.right.data(), samples);
    return block;
}

void routeSource (ConsoleModel& model, int sourceIndex, int inputIndex, std::initializer_list<int> channels)
{
    model.setInputSource (inputIndex, sourceIndex);

    for (int channel : channels)
        model.setChannelSource (channel, inputIndex);
}
} // namespace

class MixEngineTest : public juce::UnitTest
{
public:
    MixEngineTest() : juce::UnitTest ("Mix engine", "VirtualConsole") {}

    void runTest() override
    {
        beginTest ("fader and pan law");
        expectWithinAbsoluteError (faderToDb (0.75f), 0.0f, 1.0e-4f);
        expectWithinAbsoluteError (faderToDb (1.0f), 10.0f, 1.0e-4f);
        expectWithinAbsoluteError (faderToDb (0.5f), -10.0f, 1.0e-4f);
        expectWithinAbsoluteError (faderToDb (0.25f), -30.0f, 1.0e-4f);
        expectWithinAbsoluteError (faderToDb (0.0625f), -60.0f, 1.0e-4f);
        expectWithinAbsoluteError (faderToGain (0.75f), 1.0f, 1.0e-4f);
        expectWithinAbsoluteError (faderToGain (0.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (trimToDb (0.5f), 0.0f, 1.0e-4f);
        expectWithinAbsoluteError (trimToDb (1.0f), 18.0f, 1.0e-4f);
        expectWithinAbsoluteError (trimToDb (0.0f), -18.0f, 1.0e-4f);

        float left = 0.0f;
        float right = 0.0f;
        panGains (0.5f, left, right);
        expectWithinAbsoluteError (left, 0.70710678f, 1.0e-5f);
        expectWithinAbsoluteError (right, left, 1.0e-6f);
        expectWithinAbsoluteError (left * left + right * right, 1.0f, 1.0e-5f);
        panGains (0.0f, left, right);
        expectWithinAbsoluteError (left, 1.0f, 1.0e-5f);
        expectWithinAbsoluteError (right, 0.0f, 1.0e-5f);
        panGains (1.0f, left, right);
        expectWithinAbsoluteError (left, 0.0f, 1.0e-5f);
        expectWithinAbsoluteError (right, 1.0f, 1.0e-5f);

        beginTest ("console paths stay independent");
        {
            ConsoleModel model;
            expect (model.setFloat ("/ch/01/mix/fader", 0.33f));
            expect (model.setFloat ("/ch/02/eq/4/g", 0.66f));
            float fader = 0.0f;
            float eq = 0.0f;
            float otherEq = 0.0f;
            expect (model.getFloat ("/ch/01/mix/fader", fader));
            expect (model.getFloat ("/ch/02/eq/4/g", eq));
            expect (model.getFloat ("/ch/01/eq/4/g", otherEq));
            expectWithinAbsoluteError (fader, 0.33f, 1.0e-5f);
            expectWithinAbsoluteError (eq, 0.66f, 1.0e-5f);
            expectWithinAbsoluteError (otherEq, 0.5f, 1.0e-5f);
            expect (! model.setFloat ("/missing", 0.2f));

            expect (model.setInt ("/-stat/solosw/05", 1));
            int solo = 0;
            int quiet = 1;
            expect (model.getInt ("/-stat/solosw/05", solo));
            expect (model.getInt ("/-stat/solosw/04", quiet));
            expectEquals (solo, 1);
            expectEquals (quiet, 0);

            expect (model.setString ("/config/aes50/A/box", "DL32"));
            expect (model.setString ("/config/aes50/B/box", "SD16"));
            int inputs = 0;
            int outputs = 0;
            juce::String boxName;
            expect (model.getInt ("/config/aes50/A/inputs", inputs));
            expect (model.getInt ("/config/aes50/A/outputs", outputs));
            expect (model.getString ("/config/aes50/A/box", boxName));
            expectEquals (inputs, 32);
            expectEquals (outputs, 16);
            expectEquals (boxName, juce::String ("DL32"));
            expect (model.getString ("/config/aes50/B/box", boxName));
            expectEquals (boxName, juce::String ("SD16"));
            expect (! model.setString ("/config/aes50/A/box", "not-a-box"));
            expect (model.getString ("/config/aes50/A/box", boxName));
            expectEquals (boxName, juce::String ("DL32"));
            expect (model.setFloat ("/ch/01/mix/01/level", 0.4f));
            expect (model.setInt ("/ch/01/mix/01/tap", 1));
            expect (model.setInt ("/bus/01/mix/st", 1));
            float send = 0.0f;
            int tap = 0;
            int toMain = 0;
            expect (model.getFloat ("/ch/01/mix/01/level", send));
            expect (model.getInt ("/ch/01/mix/01/tap", tap));
            expect (model.getInt ("/bus/01/mix/st", toMain));
            expectWithinAbsoluteError (send, 0.4f, 1.0e-5f);
            expectEquals (tap, 1);
            expectEquals (toMain, 1);
            expectEquals (model.addSource ("vocal", "C:/audio/vocal.wav"), 0);
            expect (model.addSource ("again", "C:/audio/vocal.wav") < 0);
            expectEquals (model.addSource ("room", "C:/audio/room.wav"), 1);
            model.setInputSource (0, 0);
            model.setInputSource (numLocalInputs, 1);
            expectEquals (model.getInputSource (0), 0);
            expectEquals (model.inputForSource (1), numLocalInputs);
            expectEquals (model.inputName (0), juce::String ("Local 1"));
            expectEquals (model.inputName (numLocalInputs), juce::String ("Aux 1"));
            expectEquals (model.getChannelSource (0), 0);
        }

        beginTest ("one buffer feeds two independent strips");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (64, 48000.0);
            const int source = engine.adoptMemorySource ({ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f }, 48000.0, "steps");
            expectEquals (source, 0);
            routeSource (model, source, 0, { 0, 1 });
            expect (model.setFloat ("/ch/01/mix/pan", 0.0f));
            expect (model.setFloat ("/ch/02/mix/pan", 1.0f));
            expect (model.setFloat ("/ch/01/mix/fader", 0.75f));
            expect (model.setFloat ("/ch/02/mix/fader", 0.75f));
            engine.play();
            engine.snapSmoothers();

            const auto shared = renderBlock (engine, 4);
            expectSamples (*this, shared.left, { 1.0f, 2.0f, 3.0f, 4.0f }, "shared left");
            expectSamples (*this, shared.right, { 1.0f, 2.0f, 3.0f, 4.0f }, "shared right");

            expect (model.setFloat ("/ch/02/mix/fader", 0.0f));
            engine.snapSmoothers();
            const auto changed = renderBlock (engine, 4);
            expectSamples (*this, changed.left, { 5.0f, 6.0f, 7.0f, 8.0f }, "left after channel 2 change");
            expectSamples (*this, changed.right, { 0.0f, 0.0f, 0.0f, 0.0f }, "right follows channel 2");
        }

        beginTest ("removing a source keeps the others");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (32, 48000.0);
            const int first = engine.adoptMemorySource (std::vector<float> (16, 1.0f), 48000.0, "first");
            const int second = engine.adoptMemorySource (std::vector<float> (16, 0.5f), 48000.0, "second");
            model.setInputSource (0, first);
            model.setInputSource (1, second);
            model.setChannelSource (0, 0);
            model.setChannelSource (1, 1);
            expect (model.setFloat ("/ch/01/mix/pan", 1.0f));
            expect (model.setFloat ("/ch/02/mix/pan", 0.0f));
            engine.play();
            engine.removeSource (first);
            expectEquals (model.getNumSources(), 1);
            expectEquals (model.getSourceName (0), juce::String ("second"));
            expectEquals (model.getChannelSource (0), 0);
            expectEquals (model.getChannelSource (1), 1);
            expectEquals (model.getInputSource (0), -1);
            expectEquals (model.getInputSource (1), 0);
            engine.snapSmoothers();
            const auto kept = renderBlock (engine, 4);
            expectSamples (*this, kept.left, std::vector<float> (4, 0.5f), "kept source");
            expectSamples (*this, kept.right, std::vector<float> (4, 0.0f), "removed source");
        }

        beginTest ("transport pauses, seeks, and stops at the end");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (32, 48000.0);
            const int source = engine.adoptMemorySource ({ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f }, 48000.0, "counted");
            routeSource (model, source, 0, { 0 });
            expect (model.setFloat ("/ch/01/mix/pan", 0.0f));
            engine.snapSmoothers();

            auto silent = renderBlock (engine, 4);
            expectSamples (*this, silent.left, { 0.0f, 0.0f, 0.0f, 0.0f }, "stopped until play");

            engine.play();
            engine.snapSmoothers();
            auto started = renderBlock (engine, 4);
            expectSamples (*this, started.left, { 1.0f, 2.0f, 3.0f, 4.0f }, "play from the start");

            engine.pause();
            auto held = renderBlock (engine, 4);
            expectSamples (*this, held.left, { 0.0f, 0.0f, 0.0f, 0.0f }, "pause is silent");
            expect (! engine.isPlaying());

            engine.seek (4.0 / 48000.0);
            engine.play();
            engine.snapSmoothers();
            auto jumped = renderBlock (engine, 4);
            expectSamples (*this, jumped.left, { 5.0f, 6.0f, 7.0f, 8.0f }, "seek continues from the new point");
            expect (! engine.isPlaying(), "playback stops at the end");

            engine.play();
            engine.snapSmoothers();
            auto again = renderBlock (engine, 4);
            expectSamples (*this, again.left, { 1.0f, 2.0f, 3.0f, 4.0f }, "play at the end starts again");
        }

        beginTest ("listen switches main and solo while audio runs");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (32, 48000.0);
            const int source = engine.adoptMemorySource (std::vector<float> (64, 1.0f), 48000.0, "ones");
            routeSource (model, source, 0, { 0, 1 });
            expect (model.setFloat ("/ch/01/mix/pan", 0.0f));
            expect (model.setFloat ("/ch/02/mix/pan", 1.0f));
            expect (model.setFloat ("/ch/01/mix/fader", 0.75f));
            expect (model.setFloat ("/ch/02/mix/fader", 0.0f));
            model.setListenMode (ListenMode::main);
            engine.play();
            engine.snapSmoothers();

            auto block = renderBlock (engine, 8);
            expectSamples (*this, block.left, std::vector<float> (8, 1.0f), "main left");
            expectSamples (*this, block.right, std::vector<float> (8, 0.0f), "main right silent");

            expect (model.setFloat ("/ch/02/mix/fader", 0.75f));
            engine.snapSmoothers();
            block = renderBlock (engine, 8);
            expectSamples (*this, block.left, std::vector<float> (8, 1.0f), "left unchanged");
            expectSamples (*this, block.right, std::vector<float> (8, 1.0f), "right opens");

            expect (model.setInt ("/ch/02/mix/on", 0));
            engine.snapSmoothers();
            block = renderBlock (engine, 8);
            expectSamples (*this, block.left, std::vector<float> (8, 1.0f), "mute leaves channel 1");
            expectSamples (*this, block.right, std::vector<float> (8, 0.0f), "mute channel 2");

            expect (model.setInt ("/ch/02/mix/on", 1));
            expect (model.setFloat ("/ch/02/mix/fader", 0.0f));
            expect (model.setInt ("/-stat/solosw/02", 1));
            model.setListenMode (ListenMode::solo);
            engine.snapSmoothers();
            block = renderBlock (engine, 8);
            expectSamples (*this, block.left, std::vector<float> (8, 0.0f), "solo left");
            expectSamples (*this, block.right, std::vector<float> (8, 1.0f), "pre-fader solo ignores the fader");

            model.setListenMode (ListenMode::main);
            engine.snapSmoothers();
            block = renderBlock (engine, 8);
            expectSamples (*this, block.left, std::vector<float> (8, 1.0f), "forced main left");
            expectSamples (*this, block.right, std::vector<float> (8, 0.0f), "forced main ignores solo");

            expect (model.setFloat ("/main/st/mix/fader", 0.0f));
            model.setListenMode (ListenMode::automatic);
            expect (model.setInt ("/-stat/solosw/02", 0));
            engine.snapSmoothers();
            block = renderBlock (engine, 8);
            expectSamples (*this, block.left, std::vector<float> (8, 0.0f), "main fader closes the mix");
        }

        beginTest ("a file is read once for every channel");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (64, 48000.0);

            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("virtual-console-shared.wav");
            file.deleteFile();
            {
                auto stream = std::make_unique<juce::FileOutputStream> (file);
                expect (stream->openedOk(), "wav stream");
                juce::WavAudioFormat wav;
                juce::StringPairArray meta;
                auto* rawStream = stream.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (rawStream,
                                                                                      48000.0,
                                                                                      juce::AudioChannelSet::mono(),
                                                                                      32,
                                                                                      meta,
                                                                                      0));

                if (writer == nullptr)
                    delete rawStream;

                expect (writer != nullptr, "wav writer");

                if (writer != nullptr)
                {
                    juce::AudioBuffer<float> audio (1, 8);

                    for (int i = 0; i < 8; ++i)
                        audio.setSample (0, i, (float) (i + 1));

                    expect (writer->writeFromAudioSampleBuffer (audio, 0, 8));
                }
            }

            if (file.existsAsFile())
            {
                const int source = model.addSource ("file", file.getFullPathName());
                engine.openSource (source, file);
                const auto started = juce::Time::getMillisecondCounter();

                while (engine.bufferedSamples (source) < 8 || engine.sourceStatus (source) != 1)
                {
                    if (engine.sourceStatus (source) == 2 || juce::Time::getMillisecondCounter() - started > 3000)
                    {
                        expect (false, "file did not buffer: " + engine.sourceError (source));
                        break;
                    }

                    juce::Thread::sleep (10);
                }

                model.setInputSource (0, source);
                model.setChannelSource (0, 0);
                model.setChannelSource (1, 0);
                expect (model.setFloat ("/ch/01/mix/pan", 0.0f));
                expect (model.setFloat ("/ch/02/mix/pan", 1.0f));
                engine.play();
                engine.snapSmoothers();
                const auto fileBlock = renderBlock (engine, 4);
                expectSamples (*this, fileBlock.left, { 1.0f, 2.0f, 3.0f, 4.0f }, "file left");
                expectSamples (*this, fileBlock.right, { 1.0f, 2.0f, 3.0f, 4.0f }, "file right");
            }

            file.deleteFile();
        }

        beginTest ("a bus send reaches the main mix");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (32, 48000.0);
            const int source = engine.adoptMemorySource (std::vector<float> (256, 1.0f), 48000.0, "bus");
            routeSource (model, source, 0, { 0 });
            expect (model.setFloat ("/ch/01/mix/pan", 0.0f));
            expect (model.setInt ("/ch/01/mix/st", 0));
            expect (model.setFloat ("/ch/01/mix/01/level", 0.75f));
            expect (model.setInt ("/bus/01/mix/st", 1));
            engine.play();
            engine.snapSmoothers();
            auto block = renderBlock (engine, 32);
            expectWithinAbsoluteError (block.left[8], 1.0f, 0.02f);
            expectWithinAbsoluteError (block.right[8], 0.0f, 0.02f);

            expect (model.setFloat ("/ch/01/mix/fader", 0.0f));
            engine.snapSmoothers();
            block = renderBlock (engine, 32);
            expectWithinAbsoluteError (block.left[8], 0.0f, 0.02f);

            expect (model.setInt ("/ch/01/mix/01/tap", 1));
            engine.snapSmoothers();
            block = renderBlock (engine, 32);
            expectWithinAbsoluteError (block.left[8], 1.0f, 0.02f);
            expectWithinAbsoluteError (block.right[8], 0.0f, 0.02f);
        }
    }
};

class ChannelDspTest : public juce::UnitTest
{
public:
    ChannelDspTest() : juce::UnitTest ("Channel DSP", "VirtualConsole") {}

    void runTest() override
    {
        beginTest ("equaliser, filter, dynamics, delay, link and copy");

        const float eqFreq = std::log (1000.0f / 20.0f) / std::log (20000.0f / 20.0f);
        auto boosted = renderTone (4800, 1000.0f, [&] (ConsoleModel& model)
        {
            model.setFloat ("/ch/01/eq/3/f", eqFreq);
            model.setFloat ("/ch/01/eq/3/g", 1.0f);
            model.setFloat ("/ch/01/eq/3/q", 0.5f);
        });
        expectWithinAbsoluteError (tailPeak (boosted), std::pow (10.0f, 15.0f / 20.0f), 0.6f, "1 kHz peak boost");

        auto filtered = renderTone (4000, 100.0f, [] (ConsoleModel& model)
        {
            model.setInt ("/ch/01/preamp/hpon", 1);
            model.setFloat ("/ch/01/preamp/hpf", 1.0f);
        });
        expectLessThan (tailPeak (filtered), 0.2f, "high pass attenuates 100 Hz");

        auto closed = renderConstant (8000, 0.01f, [] (ConsoleModel& model)
        {
            model.setInt ("/ch/01/gate/on", 1);
            model.setFloat ("/ch/01/gate/thr", 0.75f);
            model.setFloat ("/ch/01/gate/range", 1.0f);
            model.setFloat ("/ch/01/gate/attack", 0.0f);
            model.setFloat ("/ch/01/gate/release", 0.0f);
        });
        expectLessThan (std::abs (closed.back()), 0.002f, "gate closes a quiet signal");

        auto open = renderConstant (2000, 1.0f, [] (ConsoleModel& model)
        {
            model.setInt ("/ch/01/gate/on", 1);
            model.setFloat ("/ch/01/gate/thr", 0.75f);
            model.setFloat ("/ch/01/gate/range", 1.0f);
            model.setFloat ("/ch/01/gate/attack", 0.0f);
        });
        expectWithinAbsoluteError (open.back(), 1.0f, 0.05f, "gate stays open on a full-scale signal");

        const float ratio = std::log ((4.0f / 1.1f)) / std::log (100.0f / 1.1f);
        auto compressed = renderConstant (4000, 1.0f, [ratio] (ConsoleModel& model)
        {
            model.setInt ("/ch/01/dyn/on", 1);
            model.setFloat ("/ch/01/dyn/thr", 0.9f);
            model.setFloat ("/ch/01/dyn/ratio", ratio);
            model.setFloat ("/ch/01/dyn/knee", 0.0f);
            model.setFloat ("/ch/01/dyn/mgain", 0.0f);
            model.setFloat ("/ch/01/dyn/attack", 0.0f);
        });
        expectWithinAbsoluteError (compressed.back(), std::pow (10.0f, -4.5f / 20.0f), 0.05f, "compressor settles near 4:1");

        const float delayUnit = std::log (0.01f / 0.0003f) / std::log (0.5f / 0.0003f);
        std::vector<float> impulse (2000, 0.0f);
        impulse[0] = 1.0f;
        auto delayed = renderSamples (impulse, [&] (ConsoleModel& model)
        {
            model.setInt ("/ch/01/delay/on", 1);
            model.setFloat ("/ch/01/delay/time", delayUnit);
        });
        float early = 0.0f;

        for (int i = 0; i < 400; ++i)
            early = std::max (early, std::abs (delayed[(size_t) i]));

        int peakIndex = 0;

        for (int i = 1; i < (int) delayed.size(); ++i)
            if (std::abs (delayed[(size_t) i]) > std::abs (delayed[(size_t) peakIndex]))
                peakIndex = i;

        const int expectedDelay = (int) std::lround (delaySeconds (delayUnit) * 48000.0f);
        expectLessThan (early, 0.001f, "delay is silent before the impulse returns");
        expectWithinAbsoluteError ((float) peakIndex, (float) expectedDelay, 2.0f, "delay lands near 10 ms");

        ConsoleModel linked;
        linked.setFloat ("/ch/01/eq/3/g", 0.9f);
        linked.setFloat ("/ch/01/mix/pan", 0.1f);
        linked.setFloat ("/ch/02/mix/pan", 0.8f);
        linked.setChannelLinked (0, true);
        float partnerGain = 0.0f;
        float partnerPan = 0.0f;
        expect (linked.getFloat ("/ch/02/eq/3/g", partnerGain));
        expect (linked.getFloat ("/ch/02/mix/pan", partnerPan));
        expectWithinAbsoluteError (partnerGain, 0.9f, 1.0e-4f, "link copies equaliser gain");
        expectWithinAbsoluteError (partnerPan, 0.8f, 1.0e-4f, "link leaves pan independent");
        linked.setFloat ("/ch/01/mix/fader", 0.2f);
        float mirroredFader = 0.0f;
        expect (linked.getFloat ("/ch/02/mix/fader", mirroredFader));
        expectWithinAbsoluteError (mirroredFader, 0.2f, 1.0e-4f, "link keeps faders together");

        ConsoleModel clipboard;
        clipboard.addSource ("keep", "C:/keep.wav");
        clipboard.setChannelSource (1, 0);
        clipboard.setFloat ("/ch/01/mix/fader", 0.22f);
        clipboard.setFloat ("/ch/01/mix/pan", 0.15f);
        clipboard.copyChannel (0);
        clipboard.pasteChannel (1);
        float pasted = 0.0f;
        expect (clipboard.getFloat ("/ch/02/mix/fader", pasted));
        expectWithinAbsoluteError (pasted, 0.22f, 1.0e-4f, "paste copies the fader");
        expectEquals (clipboard.getChannelSource (1), 0, "paste leaves the source assignment");
    }

private:
    template <typename Setup>
    std::vector<float> renderSamples (const std::vector<float>& input, Setup setup)
    {
        ConsoleModel model;
        MixEngine engine (model);
        engine.prepareToPlay (128, 48000.0);
        const int source = engine.adoptMemorySource (input, 48000.0, "dsp");
        routeSource (model, source, 0, { 0 });
        model.setFloat ("/ch/01/mix/pan", 0.0f);
        setup (model);
        engine.play();
        engine.snapSmoothers();
        std::vector<float> output (input.size(), 0.0f);

        for (size_t offset = 0; offset < output.size(); offset += 128)
        {
            const int count = (int) std::min<size_t> (128, output.size() - offset);
            auto block = renderBlock (engine, count);
            std::copy (block.left.begin(), block.left.end(), output.begin() + (std::ptrdiff_t) offset);
        }

        return output;
    }

    template <typename Setup>
    std::vector<float> renderTone (int samples, float hertz, Setup setup)
    {
        std::vector<float> tone ((size_t) samples);

        for (int i = 0; i < samples; ++i)
            tone[(size_t) i] = std::sin (2.0f * 3.14159265f * hertz * (float) i / 48000.0f);

        return renderSamples (tone, setup);
    }

    template <typename Setup>
    std::vector<float> renderConstant (int samples, float value, Setup setup)
    {
        return renderSamples (std::vector<float> ((size_t) samples, value), setup);
    }

    static float tailPeak (const std::vector<float>& samples)
    {
        const int begin = std::max (0, (int) samples.size() - 480);
        float peak = 0.0f;

        for (int i = begin; i < (int) samples.size(); ++i)
            peak = std::max (peak, std::abs (samples[(size_t) i]));

        return peak;
    }
};

void registerVirtualConsoleTests()
{
    static MixEngineTest mixEngineTest;
    static ChannelDspTest channelDspTest;
}

} // namespace vc
