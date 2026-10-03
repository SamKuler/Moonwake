#pragma once

#include "MusicLibrary.h"
#include "playlist/PlaylistModel.h"

struct LibraryCollection
{
    juce::String id;
    juce::String name;
    juce::String section;
    bool operator== (const LibraryCollection&) const = default;
};

[[nodiscard]] std::vector<LibraryCollection> libraryCollections (const MusicLibrary&);
// Projected IDs identify list occurrences; libraryId identifies the shared recording.
[[nodiscard]] std::vector<TrackItem> libraryTracks (
    const MusicLibrary&, const PlaylistModel& queue, const juce::String& collectionId);
[[nodiscard]] juce::String libraryAlbumId (const TrackItem&);

struct LibraryCollectionSummary
{
    juce::String id, name, subtitle;
    int trackCount = 0;
    double durationSeconds = 0.0;
    juce::int64 createdAtMs = 0;
    juce::StringArray artworkKeys;
    bool operator== (const LibraryCollectionSummary& other) const
    {
        return id == other.id && name == other.name && subtitle == other.subtitle
            && trackCount == other.trackCount && juce::exactlyEqual (durationSeconds, other.durationSeconds)
            && createdAtMs == other.createdAtMs && artworkKeys == other.artworkKeys;
    }
};

[[nodiscard]] std::vector<LibraryCollectionSummary> libraryCollectionSummaries (
    const MusicLibrary&, const juce::String& category);
[[nodiscard]] LibraryCollectionSummary libraryCollectionSummary (
    const MusicLibrary&, const PlaylistModel&, const juce::String& collectionId);
