#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "playlist/TrackItem.h"

struct TrackMetadataResult
{
    bool ok = false;
    TrackItem track;
    juce::String error;
};

class TrackMetadataReader
{
public:
    TrackMetadataReader();

    [[nodiscard]] TrackMetadataResult read (const juce::File& file) const;

private:
    mutable juce::AudioFormatManager formatManager;
};
