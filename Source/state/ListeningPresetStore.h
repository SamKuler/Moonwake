#pragma once

#include "ListeningPresets.h"
#include <variant>

using ImportedListeningPreset = std::variant<EffectChainPreset, ListeningPreset>;

class ListeningPresetStore
{
public:
    static constexpr juce::int64 maximumFileBytes = 64 * 1024 * 1024;
    static juce::File backupFileFor (const juce::File&);
    static bool writeAtomically (const ListeningPresetLibrary&, const juce::File&, juce::String& error);
    // A recovered backup returns true with a recovery notice in error.
    static bool readFromFile (const juce::File&, ListeningPresetLibrary&, juce::String& error);
    static bool exportPreset (const EffectChainPreset&, const juce::File&, juce::String& error);
    static bool exportPreset (const ListeningPreset&, const juce::File&, juce::String& error);
    static bool importPreset (const juce::File&, ImportedListeningPreset&, juce::String& error);
};
