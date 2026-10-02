#include "PluginScannerWorker.h"

#include <juce_gui_basics/juce_gui_basics.h>

class PluginScannerApplication final : public juce::JUCEApplication,
                                       private juce::Thread
{
public:
    PluginScannerApplication() : juce::Thread ("Moonwake plug-in scan")
    {
    }

    const juce::String getApplicationName() override { return "MoonwakePluginScanner"; }
    const juce::String getApplicationVersion() override
    {
        return juce::File::getSpecialLocation (
            juce::File::currentApplicationFile).getVersion();
    }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        arguments = getCommandLineParameterArray();
        if (! startThread())
        {
            exitCode = 8;
            quit();
        }
    }

    void shutdown() override
    {
        static_cast<void> (waitForThreadToExit (-1));
        setApplicationReturnValue (exitCode);
    }

private:
    void run() override
    {
        exitCode = runPluginScannerWorker (arguments);
        quit();
    }

    juce::StringArray arguments;
    int exitCode = 0;
};

START_JUCE_APPLICATION (PluginScannerApplication)
