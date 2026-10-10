#include <juce_gui_extra/juce_gui_extra.h>

#include "app/ProductIdentity.h"
#include "ui/PlayerWindow.h"

class MoonwakeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return MoonwakeIdentity::productName; }
    const juce::String getApplicationVersion() override { return MoonwakeIdentity::version; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<PlayerWindow> (getApplicationName());
        mainWindow->importCommandLine (commandLine);
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
