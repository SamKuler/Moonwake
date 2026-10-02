#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

struct PluginFilter
{
    juce::String search, type, manufacturer, format;

    [[nodiscard]] bool matches (const juce::PluginDescription&) const;
    [[nodiscard]] static juce::StringArray types (const juce::PluginDescription&);
    [[nodiscard]] static juce::String manufacturerName (const juce::PluginDescription&);
};
