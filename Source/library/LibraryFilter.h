#pragma once

#include "playlist/PlaylistModel.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

class LibraryFilter
{
public:
    void setQuery (juce::String queryToUse);
    void rebuild (const PlaylistModel&);
    void rebuild (const std::vector<TrackItem>&);

    [[nodiscard]] const juce::String& query() const noexcept;
    [[nodiscard]] int visibleCount() const noexcept;
    [[nodiscard]] int playlistIndexForRow (int visibleRow) const noexcept;
    [[nodiscard]] int rowForPlaylistIndex (int playlistIndex) const noexcept;
    [[nodiscard]] int searchableTextBuildCountForTesting() const noexcept;

private:
    struct CachedSearchableText
    {
        [[nodiscard]] bool matches (const TrackItem&) const noexcept;
        void updateFrom (const TrackItem&);

        juce::File file;
        juce::String title;
        juce::String artist;
        juce::String album;
        juce::String albumArtist;
        juce::String genre;
        juce::String formatName;
        int year = 0;
        int trackNumber = 0;
        int channelCount = 0;
        double sampleRate = 0.0;
        juce::String text;
        bool valid = false;
        size_t lastSeenGeneration = 0;
    };

    struct StringHash
    {
        [[nodiscard]] size_t operator() (const juce::String& value) const noexcept
        {
            return static_cast<size_t> (value.hashCode64());
        }
    };

    [[nodiscard]] static juce::String normalise (juce::String);
    [[nodiscard]] static juce::String searchableText (const TrackItem&);
    [[nodiscard]] CachedSearchableText& cachedSearchableText (
        const TrackItem&, size_t generation);
    void removeUnusedCacheEntries (size_t generation);

    juce::String currentQuery;
    juce::StringArray queryTokens;
    std::vector<int> visiblePlaylistIndices;
    std::unordered_map<juce::String, CachedSearchableText, StringHash> searchCache;
    size_t cacheGeneration = 0;
    int searchableTextBuildCount = 0;
};
