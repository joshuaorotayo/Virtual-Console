#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace vc
{
void registerVirtualConsoleTests();
}

int main (int, char**)
{
    vc::registerVirtualConsoleTests();
    juce::ScopedJuceInitialiser_GUI juce;
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0;
    const int results = runner.getNumResults();

    for (int i = 0; i < results; ++i)
    {
        const auto* result = runner.getResult (i);
        failures += result->failures;

        if (result->failures > 0)
        {
            std::printf ("%s\n", result->unitTestName.toRawUTF8());

            for (const auto& message : result->messages)
                std::printf ("  %s\n", message.toRawUTF8());
        }
    }

    if (results == 0)
        std::printf ("No tests ran\n");
    else if (failures == 0)
        std::printf ("All tests passed (%d)\n", results);
    else
        std::printf ("Tests failed: %d\n", failures);

    return failures == 0 && results > 0 ? 0 : 1;
}
