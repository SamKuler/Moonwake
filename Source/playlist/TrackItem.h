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
    juce::StringArray artists;
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
    // Unix epoch milliseconds; zero means the import time is unknown.
    juce::int64 addedAtMs = 0;
    juce::int64 fileSizeBytes = 0;
    juce::int64 fileModifiedAtMs = 0;
    juce::String fileIdentity;
    bool available = true;
    juce::String reservedEffectChainState;

    [[nodiscard]] juce::StringArray artistNames() const
    {
        juce::StringArray names;
        for (const auto& name : artists)
            if (const auto trimmed = name.trim(); trimmed.isNotEmpty())
                names.addIfNotAlreadyThere (trimmed);
        if (names.isEmpty() && artist.trim().isNotEmpty())
            names.add (artist.trim());
        return names;
    }
};
