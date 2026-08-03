#pragma once

#include <juce_core/juce_core.h>

#include <functional>

enum class PluginScanProcessOutcome
{
    completed,
    launchFailed,
    timedOut,
    cancelled,
    failed
};

struct PluginScanProcessResult
{
    PluginScanProcessOutcome outcome = PluginScanProcessOutcome::launchFailed;
    juce::uint32 exitCode = 0;
    juce::String output;
    juce::String diagnostic;
};

class PluginScanProcess
{
public:
    using CancellationPredicate = std::function<bool()>;

    static PluginScanProcessResult run (
        const juce::StringArray& command,
        int timeoutMilliseconds,
        CancellationPredicate shouldCancel);
};
