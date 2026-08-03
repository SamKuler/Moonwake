#include "PluginScannerWorker.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::StringArray arguments;
    for (int index = 1; index < argc; ++index)
        arguments.add (juce::String::fromUTF8 (argv[index]));
    return runPluginScannerWorker (arguments);
}
