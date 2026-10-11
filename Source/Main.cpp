#include <juce_gui_extra/juce_gui_extra.h>

#include "app/ProductIdentity.h"
#include "ui/PlayerWindow.h"

#if JUCE_WINDOWS
 #include "app/WindowsApplicationIdentity.h"
#endif

class MoonwakeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return MoonwakeIdentity::productName; }
    const juce::String getApplicationVersion() override { return MoonwakeIdentity::version; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
       #if JUCE_WINDOWS
        const auto identity = WindowsApplicationIdentity::initialise();
       #endif
        mainWindow = std::make_unique<PlayerWindow> (getApplicationName());
        mainWindow->importCommandLine (commandLine);
       #if JUCE_WINDOWS
        if (identity.failed())
        {
            juce::Logger::writeToLog (identity.getErrorMessage());
            mainWindow->showStatus (identity.getErrorMessage(), true);
        }
       #endif
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            return;
        mainWindow->setVisible (true);
        mainWindow->toFront (true);
        mainWindow->importCommandLine (commandLine);
    }

private:
    std::unique_ptr<PlayerWindow> mainWindow;
};

START_JUCE_APPLICATION (MoonwakeApplication)
