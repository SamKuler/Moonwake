#include "PluginScanProcess.h"

PluginScanProcessResult PluginScanProcess::run (
    const juce::StringArray& command,
    int timeoutMilliseconds,
    CancellationPredicate shouldCancel)
{
    PluginScanProcessResult result;
    if (command.isEmpty() || command[0].isEmpty())
    {
        result.diagnostic = "The plug-in scanner command is empty.";
        return result;
    }

    juce::ChildProcess process;
    if (! process.start (command, 0))
    {
        result.diagnostic = "Could not launch plug-in scanner: " + command[0];
        return result;
    }

    const auto timeout = juce::jmax (1, timeoutMilliseconds);
    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                        + static_cast<double> (timeout);

    while (process.isRunning())
    {
        if (shouldCancel && shouldCancel())
        {
            static_cast<void> (process.kill());
            static_cast<void> (process.waitForProcessToFinish (1000));
            result.outcome = PluginScanProcessOutcome::cancelled;
            result.diagnostic = "Plug-in scanner was cancelled.";
            return result;
        }

        if (juce::Time::getMillisecondCounterHiRes() >= deadline)
        {
            static_cast<void> (process.kill());
            static_cast<void> (process.waitForProcessToFinish (1000));
            result.outcome = PluginScanProcessOutcome::timedOut;
            result.diagnostic = "Plug-in scanner exceeded its time limit.";
            return result;
        }

        juce::Thread::sleep (5);
    }

    result.exitCode = process.getExitCode();
    if (result.exitCode == 0)
    {
        result.outcome = PluginScanProcessOutcome::completed;
        return result;
    }

    result.outcome = PluginScanProcessOutcome::failed;
    result.diagnostic = "Plug-in scanner exited with code "
                      + juce::String (static_cast<int> (result.exitCode)) + ".";
    if (result.output.isNotEmpty())
        result.diagnostic += " " + result.output.trim();
    return result;
}
