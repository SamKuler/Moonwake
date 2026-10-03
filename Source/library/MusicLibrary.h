#pragma once

#include "playlist/TrackItem.h"

#include <vector>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct PlaylistEntry
{
    juce::String id { juce::Uuid().toString() };
    juce::String trackId;

    bool operator== (const PlaylistEntry&) const = default;
};

struct SavedPlaylist
{
    juce::String id { juce::Uuid().toString() };
    juce::String name;
    std::vector<PlaylistEntry> entries;
    // Unix epoch milliseconds; zero means the saved time is unknown.
    juce::int64 createdAtMs = 0;
    juce::int64 updatedAtMs = 0;
};

class MusicLibrary
{
public:
    [[nodiscard]] const std::vector<TrackItem>& getTracks() const noexcept;
    [[nodiscard]] const std::vector<SavedPlaylist>& getPlaylists() const noexcept;
    [[nodiscard]] const TrackItem* getTrack (const juce::String& id) const;
    [[nodiscard]] const TrackItem* findTrackByFile (const juce::File&) const;
    [[nodiscard]] const SavedPlaylist* getPlaylist (const juce::String& id) const;
    juce::String upsertTrack (TrackItem);
    bool replaceTrack (const juce::String& id, TrackItem);

    juce::String createPlaylist (juce::String name);
    bool renamePlaylist (const juce::String& id, juce::String name);
    juce::String duplicatePlaylist (const juce::String& id, juce::String name);
    bool removePlaylist (const juce::String& id);
    bool movePlaylist (const juce::String& id, int toIndex);
    std::vector<PlaylistEntry> addToPlaylist (
        const juce::String& playlistId, const std::vector<juce::String>& trackIds,
        int insertIndex = -1);
    // Unknown entry IDs reject the entire edit; repeated request IDs identify one entry.
    bool removeEntries (const juce::String& playlistId,
                        const std::vector<juce::String>& entryIds);
    // The destination is a gap in the original list, before any entries are removed.
    bool moveEntries (const juce::String& playlistId,
                      const std::vector<juce::String>& entryIds, int insertBeforeIndex);
    bool replacePlaylistEntries (const juce::String& playlistId,
                                 std::vector<PlaylistEntry> entries);
    bool setFavourite (const juce::String& trackId, bool favourite);
    [[nodiscard]] bool isFavourite (const juce::String& trackId) const;
    [[nodiscard]] const std::vector<juce::String>& getFavouriteTrackIds() const noexcept;

    // Restores the complete graph atomically; duplicate IDs and dangling references fail.
    bool restore (std::vector<TrackItem>, std::vector<SavedPlaylist>,
                  std::vector<juce::String> favouriteTrackIds);

private:
    std::vector<TrackItem> tracks;
    std::vector<SavedPlaylist> playlists;
    std::vector<juce::String> favourites;
    std::unordered_set<std::string> favouriteIds;
    std::vector<juce::String> fileIdentities;
    std::unordered_map<std::string, size_t> trackIndices;
    // Restored records can share a physical file; the first record remains the alias match.
    std::unordered_map<std::string, std::set<size_t>> filePathIndices, fileIdentityIndices;

    void indexTrack (size_t index);
    bool replaceTrackAt (size_t index, TrackItem, juce::String fileIdentity);
    SavedPlaylist* editablePlaylist (const juce::String& id);
    bool validEntries (const std::vector<PlaylistEntry>&) const;
};
