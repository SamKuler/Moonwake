#pragma once

#include <juce_core/juce_core.h>

struct TrackItem
{
    TrackItem() = default;

    TrackItem (juce::File fileToUse, juce::String titleToUse, double durationSecondsToUse)
        : file (std::move (fileToUse)),
          title (std::move (titleToUse)),
          durationSeconds (durationSecondsToUse)
    {
    }

    juce::File file;
    juce::String id { juce::Uuid().toString() };
    juce::String libraryId;
    juce::String title;
    juce::String artist;
    juce::String album;
    juce::String albumArtist;
    juce::String genre;
    int year = 0;
    int trackNumber = 0;
    int discNumber = 0;
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    int channelCount = 0;
    int bitDepth = 0;
    juce::String formatName;
    juce::String artworkCacheKey;
    bool available = true;
    juce::String reservedEffectChainState;
};
