#include "ui/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace vc
{

class VirtualConsoleApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Virtual Console"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override { mainWindow = nullptr; }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : DocumentWindow ("Virtual Console " + juce::String (JUCE_APPLICATION_VERSION_STRING),
                              juce::Colour (0xff14181d),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (960, 640, 4000, 2200);
            centreWithSize (1280, 800);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace vc

START_JUCE_APPLICATION (vc::VirtualConsoleApplication)
