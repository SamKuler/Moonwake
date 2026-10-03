#include "MusicLibrary.h"

#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <set>
#include <utility>

namespace
{
std::string pathIndexKey (const juce::File& file)
{
    const auto path = file.getFullPathName();
    return (juce::File::areFileNamesCaseSensitive() ? path : path.toUpperCase()).toStdString();
}

void removeFileIndex (auto& index, const std::string& key, size_t trackIndex)
{
    const auto found = index.find (key);
    found->second.erase (trackIndex);
    if (found->second.empty()) index.erase (found);
}
}

const std::vector<TrackItem>& MusicLibrary::getTracks() const noexcept { return tracks; }
const std::vector<SavedPlaylist>& MusicLibrary::getPlaylists() const noexcept { return playlists; }
const std::vector<juce::String>& MusicLibrary::getFavouriteTrackIds() const noexcept { return favourites; }

const TrackItem* MusicLibrary::getTrack (const juce::String& id) const
{
    const auto found = trackIndices.find (id.toStdString());
    return found != trackIndices.end() ? &tracks[found->second] : nullptr;
}

const TrackItem* MusicLibrary::findTrackByFile (const juce::File& file) const
{
    const auto path = filePathIndices.find (pathIndexKey (file));
    if (path != filePathIndices.end()) return &tracks[*path->second.begin()];
    const auto identity = fileIdentityIndices.find (trackFileIdentity (file).toStdString());
    return identity != fileIdentityIndices.end() ? &tracks[*identity->second.begin()] : nullptr;
}

const SavedPlaylist* MusicLibrary::getPlaylist (const juce::String& id) const
{
    const auto found = std::find_if (playlists.begin(), playlists.end(),
                                    [&] (const auto& playlist) { return playlist.id == id; });
    return found != playlists.end() ? &*found : nullptr;
}

SavedPlaylist* MusicLibrary::editablePlaylist (const juce::String& id)
{
    return const_cast<SavedPlaylist*> (std::as_const (*this).getPlaylist (id));
}

juce::String MusicLibrary::upsertTrack (TrackItem track)
{
    auto identity = trackFileIdentity (track.file);
    const auto path = filePathIndices.find (pathIndexKey (track.file));
    const auto alias = fileIdentityIndices.find (identity.toStdString());
    if (path != filePathIndices.end() || alias != fileIdentityIndices.end())
    {
        const auto index = path != filePathIndices.end() ? *path->second.begin() : *alias->second.begin();
        const auto id = tracks[index].id;
        static_cast<void> (replaceTrackAt (index, std::move (track), std::move (identity)));
        return id;
    }
    while (track.id.isEmpty() || getTrack (track.id) != nullptr)
        track.id = juce::Uuid().toString();
    track.libraryId.clear();
    fileIdentities.push_back (std::move (identity));
    tracks.push_back (std::move (track));
    indexTrack (tracks.size() - 1);
    return tracks.back().id;
}

bool MusicLibrary::replaceTrack (const juce::String& id, TrackItem replacement)
{
    const auto existing = trackIndices.find (id.toStdString());
    if (existing == trackIndices.end()) return false;
    auto identity = trackFileIdentity (replacement.file);
    return replaceTrackAt (existing->second, std::move (replacement), std::move (identity));
}

void MusicLibrary::indexTrack (size_t index)
{
    trackIndices.emplace (tracks[index].id.toStdString(), index);
    filePathIndices[pathIndexKey (tracks[index].file)].insert (index);
    fileIdentityIndices[fileIdentities[index].toStdString()].insert (index);
}

bool MusicLibrary::replaceTrackAt (size_t index, TrackItem replacement, juce::String identity)
{
    const auto oldPath = pathIndexKey (tracks[index].file);
    const auto newPath = pathIndexKey (replacement.file);
    const auto oldIdentity = fileIdentities[index].toStdString();
    const auto newIdentity = identity.toStdString();
    if (oldPath != newPath)
    {
        if (filePathIndices.contains (newPath)) return false;
        if (newIdentity != oldIdentity && fileIdentityIndices.contains (newIdentity)) return false;
    }
    if (oldPath != newPath)
    {
        filePathIndices[newPath].insert (index);
        removeFileIndex (filePathIndices, oldPath, index);
    }
    if (oldIdentity != newIdentity)
    {
        fileIdentityIndices[newIdentity].insert (index);
        removeFileIndex (fileIdentityIndices, oldIdentity, index);
    }
    replacement.id = tracks[index].id;
    replacement.libraryId.clear();
    fileIdentities[index] = std::move (identity);
    tracks[index] = std::move (replacement);
    return true;
}

juce::String MusicLibrary::createPlaylist (juce::String name)
{
    name = name.trim();
    if (name.isEmpty()) return {};
    SavedPlaylist playlist;
    playlist.name = std::move (name);
    playlist.createdAtMs = playlist.updatedAtMs = juce::Time::currentTimeMillis();
    playlists.push_back (std::move (playlist));
    return playlists.back().id;
}

bool MusicLibrary::renamePlaylist (const juce::String& id, juce::String name)
{
    auto* playlist = editablePlaylist (id);
    name = name.trim();
    if (playlist == nullptr || name.isEmpty() || playlist->name == name) return false;
    playlist->name = std::move (name);
    playlist->updatedAtMs = juce::Time::currentTimeMillis();
    return true;
}

juce::String MusicLibrary::duplicatePlaylist (const juce::String& id, juce::String name)
{
    const auto* source = getPlaylist (id);
    if (source == nullptr || name.trim().isEmpty()) return {};
    auto duplicate = *source;
    duplicate.id = juce::Uuid().toString();
    duplicate.name = name.trim();
    duplicate.createdAtMs = duplicate.updatedAtMs = juce::Time::currentTimeMillis();
    for (auto& entry : duplicate.entries) entry.id = juce::Uuid().toString();
    playlists.push_back (std::move (duplicate));
    return playlists.back().id;
}

bool MusicLibrary::removePlaylist (const juce::String& id)
{
    const auto oldSize = playlists.size();
    std::erase_if (playlists, [&] (const auto& playlist) { return playlist.id == id; });
    return playlists.size() != oldSize;
}

bool MusicLibrary::movePlaylist (const juce::String& id, int toIndex)
{
    const auto* playlist = getPlaylist (id);
    if (playlist == nullptr || toIndex < 0 || toIndex >= static_cast<int> (playlists.size())) return false;
    const auto index = playlist - playlists.data();
    if (index == toIndex) return false;
    auto moved = std::move (playlists[static_cast<size_t> (index)]);
    moved.updatedAtMs = juce::Time::currentTimeMillis();
    playlists.erase (playlists.begin() + index);
    playlists.insert (playlists.begin() + toIndex, std::move (moved));
    return true;
}

std::vector<PlaylistEntry> MusicLibrary::addToPlaylist (
    const juce::String& playlistId, const std::vector<juce::String>& trackIds, int insertIndex)
{
    auto* playlist = editablePlaylist (playlistId);
    if (playlist == nullptr || trackIds.empty() || std::any_of (trackIds.begin(), trackIds.end(),
        [&] (const auto& id) { return getTrack (id) == nullptr; })) return {};
    std::vector<PlaylistEntry> added;
    for (const auto& id : trackIds) added.push_back ({ juce::Uuid().toString(), id });
    const auto index = insertIndex < 0 ? static_cast<int> (playlist->entries.size())
                                      : juce::jmin (insertIndex, static_cast<int> (playlist->entries.size()));
    playlist->entries.insert (playlist->entries.begin() + index, added.begin(), added.end());
    playlist->updatedAtMs = juce::Time::currentTimeMillis();
    return added;
}

bool MusicLibrary::removeEntries (const juce::String& playlistId,
                                  const std::vector<juce::String>& entryIds)
{
    auto* playlist = editablePlaylist (playlistId);
    if (playlist == nullptr || entryIds.empty()) return false;
    std::unordered_set<std::string> requested;
    requested.reserve (entryIds.size());
    for (const auto& id : entryIds) requested.insert (id.toStdString());
    std::vector<PlaylistEntry> remaining;
    remaining.reserve (playlist->entries.size());
    size_t removed = 0;
    for (const auto& entry : playlist->entries)
        if (requested.contains (entry.id.toStdString())) ++removed;
        else remaining.push_back (entry);
    if (removed != requested.size()) return false;
    playlist->entries = std::move (remaining);
    playlist->updatedAtMs = juce::Time::currentTimeMillis();
    return true;
}

bool MusicLibrary::moveEntries (const juce::String& playlistId,
                                const std::vector<juce::String>& entryIds, int insertBeforeIndex)
{
    auto* playlist = editablePlaylist (playlistId);
    if (playlist == nullptr || entryIds.empty() || insertBeforeIndex < 0
        || insertBeforeIndex > static_cast<int> (playlist->entries.size())) return false;
    std::unordered_set<std::string> requested;
    requested.reserve (entryIds.size());
    for (const auto& id : entryIds) requested.insert (id.toStdString());
    std::vector<PlaylistEntry> moved, remaining;
    moved.reserve (requested.size());
    remaining.reserve (playlist->entries.size());
    int destination = 0;
    for (size_t index = 0; index < playlist->entries.size(); ++index)
    {
        const auto& entry = playlist->entries[index];
        if (requested.contains (entry.id.toStdString())) moved.push_back (entry);
        else
        {
            if (static_cast<int> (index) < insertBeforeIndex) ++destination;
            remaining.push_back (entry);
        }
    }
    if (moved.size() != requested.size()) return false;
    remaining.insert (remaining.begin() + destination, moved.begin(), moved.end());
    if (remaining == playlist->entries) return false;
    playlist->entries = std::move (remaining);
    playlist->updatedAtMs = juce::Time::currentTimeMillis();
    return true;
}

bool MusicLibrary::validEntries (const std::vector<PlaylistEntry>& entries) const
{
    std::set<juce::String> ids;
    for (const auto& entry : entries)
        if (entry.id.isEmpty() || ! ids.insert (entry.id).second || getTrack (entry.trackId) == nullptr)
            return false;
    return true;
}

bool MusicLibrary::replacePlaylistEntries (const juce::String& playlistId,
                                           std::vector<PlaylistEntry> entries)
{
    auto* playlist = editablePlaylist (playlistId);
    if (playlist == nullptr || ! validEntries (entries) || playlist->entries == entries) return false;
    playlist->entries = std::move (entries);
    playlist->updatedAtMs = juce::Time::currentTimeMillis();
    return true;
}

bool MusicLibrary::setFavourite (const juce::String& trackId, bool favourite)
{
    if (getTrack (trackId) == nullptr || isFavourite (trackId) == favourite) return false;
    if (favourite)
    {
        favourites.push_back (trackId);
        favouriteIds.insert (trackId.toStdString());
    }
    else
    {
        std::erase (favourites, trackId);
        favouriteIds.erase (trackId.toStdString());
    }
    return true;
}

bool MusicLibrary::isFavourite (const juce::String& trackId) const
{
    return favouriteIds.contains (trackId.toStdString());
}

bool MusicLibrary::restore (std::vector<TrackItem> restoredTracks,
                            std::vector<SavedPlaylist> restoredPlaylists,
                            std::vector<juce::String> restoredFavourites)
{
    MusicLibrary restored;
    std::set<juce::String> trackIds, playlistIds;
    for (auto& track : restoredTracks)
    {
        if (track.id.isEmpty() || ! trackIds.insert (track.id).second) return false;
        track.libraryId.clear();
        restored.fileIdentities.push_back (trackFileIdentity (track.file));
        restored.tracks.push_back (std::move (track));
        restored.indexTrack (restored.tracks.size() - 1);
    }
    for (const auto& playlist : restoredPlaylists)
        if (playlist.id.isEmpty() || playlist.name.trim().isEmpty()
            || ! playlistIds.insert (playlist.id).second || ! restored.validEntries (playlist.entries))
            return false;
    for (const auto& id : restoredFavourites)
        if (restored.getTrack (id) == nullptr || ! restored.favouriteIds.insert (id.toStdString()).second) return false;
    restored.playlists = std::move (restoredPlaylists);
    restored.favourites = std::move (restoredFavourites);
    *this = std::move (restored);
    return true;
}
