#pragma once

#include <juce_core/juce_core.h>

struct PluginFormatPreferences
{
    bool vst3 = true;
   #if JUCE_MAC
    bool audioUnit = true;
   #else
    bool audioUnit = false;
   #endif

    [[nodiscard]] bool allows (const juce::String& format) const
    {
        return (format == "VST3" && vst3) || (format == "AudioUnit" && audioUnit);
    }
    bool operator== (const PluginFormatPreferences&) const = default;
};
