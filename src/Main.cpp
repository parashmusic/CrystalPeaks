#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class MiniMetersApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "CrystalPeaks"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }

    struct MainWindow : public juce::DocumentWindow
    {
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Colour (0xff050505), // Black background
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (false); // Custom JUCE title bar
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (900, 200);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MiniMetersApp)