#pragma once

#include <juce_core/juce_core.h>

struct PluginFormatPreferences
{
    bool vst3 = true;
    bool audioUnit = true;

    [[nodiscard]] bool allows (const juce::String& format) const
    {
        return (format == "VST3" && vst3) || (format == "AudioUnit" && audioUnit);
    }
    bool operator== (const PluginFormatPreferences&) const = default;
};
