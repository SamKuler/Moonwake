#pragma once

#include <juce_core/juce_core.h>

struct MusicDirectory
{
    juce::String id { juce::Uuid().toString() };
    juce::File folder;
    bool automatic = true;

    bool operator== (const MusicDirectory&) const = default;
};
