#pragma once

#include <juce_core/juce_core.h>

[[nodiscard]] juce::String trackFileIdentity (const juce::File& file);

struct TrackFileStamp
{
    juce::String identity;
    juce::int64 size = 0;
    juce::int64 modifiedAtMs = 0;
    bool operator== (const TrackFileStamp&) const = default;
};

[[nodiscard]] TrackFileStamp trackFileStamp (const juce::File& file);
