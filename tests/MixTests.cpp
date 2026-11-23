#include "engine/MixEngine.h"
#include "model/FaderLaw.h"

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
        }

        beginTest ("one buffer feeds two independent strips");
        {
            ConsoleModel model;
            MixEngine engine (model);
            engine.prepareToPlay (64, 48000.0);
            const int source = engine.adoptMemorySource ({ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f }, 48000.0, "steps");
            expectEquals (source, 0);
            model.setChannelSource (0, source);
            model.setChannelSource (1, source);
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
            model.setChannelSource (0, first);
            model.setChannelSource (1, second);
            expect (model.setFloat ("/ch/01/mix/pan", 1.0f));
            expect (model.setFloat ("/ch/02/mix/pan", 0.0f));
            engine.play();
            engine.removeSource (first);
            expectEquals (model.getNumSources(), 1);
            expectEquals (model.getSourceName (0), juce::String ("second"));
            expectEquals (model.getChannelSource (0), -1);
            expectEquals (model.getChannelSource (1), 0);
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
            model.setChannelSource (0, source);
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
            model.setChannelSource (0, source);
            model.setChannelSource (1, source);
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

                model.setChannelSource (0, source);
                model.setChannelSource (1, source);
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
    }
};

void registerVirtualConsoleTests()
{
    static MixEngineTest mixEngineTest;
}

} // namespace vc
