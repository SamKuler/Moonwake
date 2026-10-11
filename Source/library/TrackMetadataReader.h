#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "playlist/TrackItem.h"

struct TrackMetadataResult
{
    bool ok = false;
    TrackItem track;
    juce::MemoryBlock artworkData;
    juce::String error;
};

class TrackMetadataReader
{
public:
    TrackMetadataReader();

    [[nodiscard]] TrackMetadataResult read (const juce::File& file,
                                            bool includeArtwork = true) const;
    static void applyMetadata (TrackItem&, const juce::StringPairArray&);

private:
    mutable juce::AudioFormatManager formatManager;
};
