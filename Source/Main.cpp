#include <juce_gui_extra/juce_gui_extra.h>

#include "app/ProductIdentity.h"
#include "ui/MainComponent.h"

class MoonwakeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return MoonwakeIdentity::productName; }
    const juce::String getApplicationVersion() override { return MoonwakeIdentity::version; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
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
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (std::move (name),
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (1040, 700, 1800, 1200);
            centreWithSize (1240, 800);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        void importCommandLine (juce::String commandLine)
        {
            if (content == nullptr || commandLine.isEmpty())
                return;
            juce::Array<juce::File> files;
            for (auto token : juce::StringArray::fromTokens (commandLine, true))
            {
                const juce::File file (token.trim().unquoted());
                if (file.exists())
                    files.addIfNotAlreadyThere (file);
            }
            if (! files.isEmpty())
                content->importFilesAndFolders (files);
        }

    private:
        MainComponent* content = nullptr;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MoonwakeApplication)
