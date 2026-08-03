#include "skin/SkinToolCommands.h"

#include <juce_events/juce_events.h>

#include <cstdio>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index)
        arguments.add (juce::String::fromUTF8 (argv[index]));

    const auto result = SkinTool::run (arguments);
    if (result.standardOutput.isNotEmpty())
        std::fputs (result.standardOutput.toRawUTF8(), stdout);
    if (result.standardError.isNotEmpty())
        std::fputs (result.standardError.toRawUTF8(), stderr);
    return result.exitCode;
}
