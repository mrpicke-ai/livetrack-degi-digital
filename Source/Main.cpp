#include <juce_gui_extra/juce_gui_extra.h>
#include "Audio/Engine.h"
#include "UI/MainComponent.h"

namespace ldd
{
class LivetrackApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Livetrack Degi Digital"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        engine = std::make_unique<Engine>();
        engine->initialise();
        mainWindow = std::make_unique<MainWindow> (*engine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        engine->shutdown();
        engine = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (Engine& e)
            : DocumentWindow ("Livetrack Degi Digital - DEGISOFT",
                              theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (e);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1024, 640, 10000, 10000);
            centreWithSize (1360, 860);
            setVisible (true);
            content->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        JUCE_DECLARE_NON_COPYABLE (MainWindow)
    };

    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION (ldd::LivetrackApplication)
