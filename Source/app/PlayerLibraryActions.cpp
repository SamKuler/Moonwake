#include "PlayerController.h"

#include <algorithm>
#include <unordered_set>

namespace
{
juce::String playlistId (const juce::String& collection)
{
    return collection.startsWith ("playlist:") ? collection.substring (9) : juce::String {};
}

template <typename Entry>
bool containsEveryEntry (const std::vector<Entry>& entries,
                         const std::vector<juce::String>& requested)
{
    std::unordered_set<std::string> entryIds;
    entryIds.reserve (entries.size());
    for (const auto& entry : entries) entryIds.insert (entry.id.toStdString());
    return std::all_of (requested.begin(), requested.end(),
                       [&] (const auto& id) { return entryIds.contains (id.toStdString()); });
}

bool sameOrder (const std::vector<TrackItem>& a, const std::vector<TrackItem>& b)
{
    return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(),
        [] (const auto& x, const auto& y) { return x.id == y.id; });
}
}

void PlayerController::setLibraryBrowseState (juce::String collectionId, juce::String query)
{
    if (collectionId.isEmpty()) collectionId = "all";
    if (libraryCollectionId == collectionId && librarySearchQuery == query) return;
    libraryCollectionId = std::move (collectionId);
    librarySearchQuery = std::move (query);
    ++stateRevision;
}

juce::String PlayerController::storePreparedLibraryTrack (
    TrackMetadataResult& prepared, juce::StringArray& warnings, juce::StringArray& artworkCandidates)
{
    auto& track = prepared.track;
    artworkCandidates.add (track.artworkCacheKey);
    if (const auto* existing = library.findTrackByFile (track.file))
    {
        artworkCandidates.add (existing->artworkCacheKey);
        // Failed reads retain recorded metadata while updating file availability.
        if (! prepared.ok)
        {
            const auto id = existing->id;
            if (existing->available)
            {
                auto unavailable = *existing;
                unavailable.available = false;
                library.replaceTrack (id, std::move (unavailable));
            }
            return id;
        }
        if (sameLoudnessSource (track, *existing) && existing->loudness.measured)
            track.loudness = existing->loudness;
        track.id = existing->id;
    }
    if (prepared.artworkData.getSize() > 0)
    {
        juce::String error;
        const auto key = artworkStore.store (juce::Uuid().toString(), prepared.artworkData, error);
        if (key.isNotEmpty()) track.artworkCacheKey = key;
        else if (error.isNotEmpty()) warnings.add (track.file.getFileName() + " (" + error + ")");
    }
    if (! prepared.ok) track.available = false;
    return library.upsertTrack (std::move (track));
}

void PlayerController::discardUnreferencedArtwork (const juce::StringArray& candidates)
{
    std::unordered_set<std::string> retained;
    for (const auto& track : library.getTracks()) retained.insert (track.artworkCacheKey.toStdString());
    for (const auto& track : playlist.getTracks()) retained.insert (track.artworkCacheKey.toStdString());
    for (const auto& saved : library.getPlaylists()) retained.insert (saved.artworkCacheKey.toStdString());
    for (const auto& key : candidates)
        if (key.isNotEmpty() && ! retained.contains (key.toStdString()))
        {
            if (savedArtworkReferences.contains (key.toStdString())) deferredArtworkRemoval.addIfNotAlreadyThere (key);
            else static_cast<void> (artworkStore.remove (key));
        }
}

juce::StringArray PlayerController::retainArtworkForStateSave (const PlayerAppState& state)
{
    juce::StringArray references;
    std::unordered_set<std::string> unique;
    const auto retain = [&] (const juce::String& key)
    {
        if (key.isNotEmpty() && unique.insert (key.toStdString()).second) references.add (key);
    };
    for (const auto& track : state.library.getTracks()) retain (track.artworkCacheKey);
    for (const auto& track : state.playlist.getTracks()) retain (track.artworkCacheKey);
    for (const auto& saved : state.library.getPlaylists()) retain (saved.artworkCacheKey);
    savedArtworkReferences.insert (unique.begin(), unique.end());
    return references;
}

void PlayerController::acknowledgeSavedArtwork (juce::StringArray references)
{
    // State writes are serialized; only a successful replacement releases the preceding snapshot's assets.
    savedArtworkReferences.clear();
    for (const auto& key : references) savedArtworkReferences.insert (key.toStdString());
    auto retired = std::exchange (deferredArtworkRemoval, {});
    discardUnreferencedArtwork (retired);
}

void PlayerController::refreshQueueMetadata()
{
    auto tracks = playlist.getTracks();
    for (auto& occurrence : tracks)
        if (const auto* canonical = library.getTrack (occurrence.libraryId))
        {
            const auto id = occurrence.id;
            occurrence = *canonical;
            occurrence.id = id;
            occurrence.libraryId = canonical->id;
        }
    playlist.replaceTracks (std::move (tracks));
    refreshNormalizationGains();
}

void PlayerController::setLibraryTrackAvailable (const juce::String& occurrenceId, bool available)
{
    const auto* occurrence = playlist.getTrack (playlist.indexOfId (occurrenceId));
    if (occurrence == nullptr) return;
    const auto canonicalId = occurrence->libraryId;
    if (const auto* canonical = library.getTrack (canonicalId))
    {
        if (canonical->available != available)
        {
            auto updated = *canonical;
            updated.available = available;
            library.replaceTrack (canonicalId, std::move (updated));
        }
        const auto& current = playlist.getTracks();
        std::vector<TrackItem> updated;
        for (size_t index = 0; index < current.size(); ++index)
            if (current[index].libraryId == canonicalId && current[index].available != available)
            {
                if (updated.empty()) updated = current;
                updated[index].available = available;
            }
        if (! updated.empty()) playlist.replaceTracks (std::move (updated));
    }
    else playlist.setTrackAvailable (occurrenceId, available);
}

juce::String PlayerController::addPreparedTracks (std::vector<TrackMetadataResult> prepared)
{
    juce::StringArray skipped, warnings, artworkCandidates;
    std::vector<TrackItem> inserted;
    std::unordered_set<std::string> insertedIds;
    for (auto& metadata : prepared)
    {
        const auto file = metadata.track.file;
        artworkCandidates.add (metadata.track.artworkCacheKey);
        if (! metadata.ok || ! metadata.track.available)
        {
            skipped.add (file.getFileName() + " (" + (metadata.error.isNotEmpty() ? metadata.error : "unreadable") + ")");
            continue;
        }
        const auto* existing = library.findTrackByFile (file);
        if (playlist.containsFile (file)
            || (existing != nullptr && insertedIds.contains (existing->id.toStdString())))
        {
            skipped.add (file.getFileName() + " (already in queue)");
            continue;
        }
        const auto id = storePreparedLibraryTrack (metadata, warnings, artworkCandidates);
        insertedIds.insert (id.toStdString());
        auto occurrence = *library.getTrack (id);
        occurrence.id = juce::Uuid().toString();
        occurrence.libraryId = id;
        inserted.push_back (std::move (occurrence));
    }
    const auto count = static_cast<int> (inserted.size());
    if (count > 0)
    {
        synchronizePlayback();
        refreshQueueMetadata();
        for (auto& occurrence : inserted)
        {
            const auto id = occurrence.id, libraryId = occurrence.libraryId;
            occurrence = *library.getTrack (libraryId);
            occurrence.id = id;
            occurrence.libraryId = libraryId;
        }
        playlist.insertTracks (playlist.size(), std::move (inserted));
        invalidateCollectionUndo ("queue");
        ++stateRevision;
        refreshUpcomingPlayback();
    }
    discardUnreferencedArtwork (artworkCandidates);
    lastStatusMessage = "Added " + juce::String (count) + (count == 1 ? " track." : " tracks.");
    if (! skipped.isEmpty()) lastStatusMessage += " Skipped: " + skipped.joinIntoString (", ");
    if (! warnings.isEmpty()) lastStatusMessage += " Artwork warnings: " + warnings.joinIntoString (", ");
    return lastStatusMessage;
}

juce::String PlayerController::importPreparedTracks (std::vector<TrackMetadataResult> prepared,
                                                    const juce::String& target)
{
    synchronizePlayback();
    std::vector<juce::String> ids;
    juce::StringArray skipped, warnings, artworkCandidates;
    for (auto& metadata : prepared)
    {
        artworkCandidates.add (metadata.track.artworkCacheKey);
        if (! metadata.ok || ! metadata.track.available)
        {
            skipped.add (metadata.track.file.getFileName() + " ("
                         + (metadata.error.isNotEmpty() ? metadata.error : "unreadable") + ")");
            continue;
        }
        ids.push_back (storePreparedLibraryTrack (metadata, warnings, artworkCandidates));
    }
    juce::String destinationWarning;
    if (! ids.empty())
    {
        refreshQueueMetadata();
        ++stateRevision;
        if (target == "queue")
        {
            if (! enqueueLibraryTracks (ids, false)) destinationWarning = " " + lastStatusMessage;
        }
        else if (target == "favourites") setTrackFavourites (ids, true);
        else if (target.startsWith ("playlist:"))
        {
            const auto* destination = library.getPlaylist (playlistId (target));
            if (destination != nullptr && destination->smartRules)
                destinationWarning = " The smart playlist will include songs that match its rules.";
            else if (! addTracksToSavedPlaylist (playlistId (target), ids))
                destinationWarning = " The destination playlist no longer exists; tracks were retained in the library.";
        }
        else if (target != "all")
            destinationWarning = " Tracks were retained in the library; this collection is not an import destination.";
        if (target != "queue" && ! refreshUpcomingPlayback()) destinationWarning += " " + lastStatusMessage;
    }
    discardUnreferencedArtwork (artworkCandidates);
    lastStatusMessage = "Imported " + juce::String (static_cast<int> (ids.size())) + " tracks." + destinationWarning;
    if (! skipped.isEmpty()) lastStatusMessage += " Skipped: " + skipped.joinIntoString (", ");
    if (! warnings.isEmpty()) lastStatusMessage += " Artwork warnings: " + warnings.joinIntoString (", ");
    return lastStatusMessage;
}

juce::String PlayerController::importPreparedPlaylist (juce::String name,
                                                       std::vector<TrackMetadataResult> prepared)
{
    if (name.trim().isEmpty())
    {
        juce::StringArray artworkCandidates;
        for (const auto& metadata : prepared) artworkCandidates.add (metadata.track.artworkCacheKey);
        discardUnreferencedArtwork (artworkCandidates);
        lastStatusMessage = "Enter a playlist name.";
        return {};
    }
    synchronizePlayback();
    juce::StringArray warnings, artworkCandidates;
    std::vector<juce::String> ids;
    for (auto& metadata : prepared) ids.push_back (storePreparedLibraryTrack (metadata, warnings, artworkCandidates));
    const auto id = library.createPlaylist (std::move (name));
    library.addToPlaylist (id, ids);
    refreshQueueMetadata();
    ++stateRevision;
    discardUnreferencedArtwork (artworkCandidates);
    const auto playbackWarning = refreshUpcomingPlayback() ? juce::String {} : " " + lastStatusMessage;
    lastStatusMessage = "Imported playlist with " + juce::String (static_cast<int> (ids.size())) + " entries." + playbackWarning;
    if (! warnings.isEmpty()) lastStatusMessage += " Artwork warnings: " + warnings.joinIntoString (", ");
    return id;
}

juce::String PlayerController::createSavedPlaylist (juce::String name)
{
    const auto id = library.createPlaylist (std::move (name));
    if (id.isNotEmpty()) ++stateRevision;
    lastStatusMessage = id.isNotEmpty() ? "Playlist created." : "Enter a playlist name.";
    return id;
}

juce::String PlayerController::createSmartPlaylist (juce::String name, const SmartPlaylistRules& rules)
{
    const auto validation = validateSmartPlaylistRules (rules);
    if (validation.failed()) { lastStatusMessage = validation.getErrorMessage(); return {}; }
    const auto id = library.createSmartPlaylist (std::move (name), rules);
    if (id.isNotEmpty()) ++stateRevision;
    lastStatusMessage = id.isNotEmpty() ? "Smart playlist created." : "Enter a playlist name.";
    return id;
}

bool PlayerController::updateSmartPlaylist (const juce::String& id, juce::String name,
                                            const SmartPlaylistRules& rules)
{
    name = name.trim();
    const auto validation = validateSmartPlaylistRules (rules);
    const auto* saved = library.getPlaylist (id);
    if (saved == nullptr || ! saved->smartRules)
    { lastStatusMessage = "The smart playlist no longer exists."; return false; }
    if (name.isEmpty()) { lastStatusMessage = "Enter a playlist name."; return false; }
    if (validation.failed()) { lastStatusMessage = validation.getErrorMessage(); return false; }
    if (saved->name != name || *saved->smartRules != rules)
    {
        if (saved->name != name) library.renamePlaylist (id, std::move (name));
        library.setSmartPlaylistRules (id, rules);
        invalidateCollectionUndo ("playlist:" + id);
        ++stateRevision;
    }
    lastStatusMessage = "Smart playlist updated.";
    return true;
}

juce::String PlayerController::materializeSmartPlaylist (const juce::String& id)
{
    const auto* saved = library.getPlaylist (id);
    const auto created = saved != nullptr ? library.materializeSmartPlaylist (id, saved->name + " copy") : juce::String {};
    if (created.isNotEmpty()) ++stateRevision;
    lastStatusMessage = created.isNotEmpty() ? "Saved a regular playlist." : "Choose a smart playlist to copy.";
    return created;
}

void PlayerController::choosePlaylistArtwork (const juce::String& id, juce::File imageFile,
                                               std::function<void(bool, const juce::String&)> callback)
{
    if (library.getPlaylist (id) == nullptr)
    {
        if (callback) callback (false, "Playlist no longer exists.");
        return;
    }
    const auto request = ++nextArtworkRequest;
    artworkRequests[id] = request;
    playlistArtworkJobs.addJob ([this, alive = lifetimeFlag, store = artworkStore, id, request,
                                image = std::move (imageFile), completion = std::move (callback)]
    {
        juce::String error, key;
        juce::MemoryBlock bytes;
        constexpr auto maxBytes = 20 * 1024 * 1024;
        if (auto stream = image.createInputStream())
        {
            stream->readIntoMemoryBlock (bytes, maxBytes + 1);
            key = store.store (juce::Uuid().toString(), bytes, error);
        }
        else error = "Could not read the cover image.";
        juce::MessageManager::callAsync ([this, alive, store, id, request, key, error, completion]
        {
            if (! alive->load()) { if (key.isNotEmpty()) static_cast<void> (store.remove (key)); return; }
            const auto found = artworkRequests.find (id);
            if (found == artworkRequests.end() || found->second != request)
            {
                if (key.isNotEmpty()) discardUnreferencedArtwork ({ key });
                return;
            }
            artworkRequests.erase (found);
            const auto* saved = library.getPlaylist (id);
            const auto previous = saved != nullptr ? saved->artworkCacheKey : juce::String {};
            const auto changed = key.isNotEmpty() && library.setPlaylistArtwork (id, key);
            discardUnreferencedArtwork ({ previous, key });
            if (changed) ++stateRevision;
            lastStatusMessage = changed ? "Playlist cover updated." : saved == nullptr ? "Playlist no longer exists."
                : error.isNotEmpty() ? error : "Choose a nonempty PNG or JPEG image.";
            if (completion) completion (changed, lastStatusMessage);
        });
    });
}

bool PlayerController::resetPlaylistArtwork (const juce::String& id)
{
    artworkRequests.erase (id);
    const auto* saved = library.getPlaylist (id);
    if (saved == nullptr) return false;
    const auto previous = saved->artworkCacheKey;
    const auto changed = library.setPlaylistArtwork (id, {});
    discardUnreferencedArtwork ({ previous });
    if (changed) ++stateRevision;
    lastStatusMessage = "Playlist cover reset.";
    return changed;
}

bool PlayerController::renameSavedPlaylist (const juce::String& id, juce::String name)
{
    if (! library.renamePlaylist (id, std::move (name)))
    {
        lastStatusMessage = "Choose an existing playlist and a different, nonempty name.";
        return false;
    }
    ++stateRevision;
    lastStatusMessage = "Playlist renamed.";
    return true;
}

juce::String PlayerController::duplicateSavedPlaylist (const juce::String& id)
{
    const auto* source = library.getPlaylist (id);
    if (source == nullptr) { lastStatusMessage = "Playlist no longer exists."; return {}; }
    const auto copy = library.duplicatePlaylist (id, source->name + " copy");
    ++stateRevision;
    lastStatusMessage = "Playlist duplicated.";
    return copy;
}

bool PlayerController::deleteSavedPlaylist (const juce::String& id)
{
    const auto* saved = library.getPlaylist (id);
    const auto previousArtwork = saved != nullptr ? saved->artworkCacheKey : juce::String {};
    if (! library.removePlaylist (id)) { lastStatusMessage = "Playlist no longer exists."; return false; }
    artworkRequests.erase (id);
    discardUnreferencedArtwork ({ previousArtwork });
    invalidateCollectionUndo ("playlist:" + id);
    if (libraryCollectionId == "playlist:" + id) libraryCollectionId = "all";
    ++stateRevision;
    lastStatusMessage = "Playlist deleted.";
    return true;
}

bool PlayerController::moveSavedPlaylist (const juce::String& id, int toIndex)
{
    if (! library.movePlaylist (id, toIndex)) return false;
    ++stateRevision;
    lastStatusMessage = "Playlists reordered.";
    return true;
}

bool PlayerController::addTracksToSavedPlaylist (const juce::String& id,
                                                const std::vector<juce::String>& ids)
{
    if (const auto* saved = library.getPlaylist (id); saved != nullptr && saved->smartRules)
    {
        lastStatusMessage = "Smart playlists select songs using their rules.";
        return false;
    }
    if (ids.empty() || library.addToPlaylist (id, ids).empty())
    {
        lastStatusMessage = "Choose tracks and an existing playlist.";
        return false;
    }
    invalidateCollectionUndo ("playlist:" + id);
    ++stateRevision;
    lastStatusMessage = "Tracks added to playlist.";
    return true;
}

bool PlayerController::setTrackFavourites (const std::vector<juce::String>& ids, bool favourite)
{
    if (ids.empty() || std::any_of (ids.begin(), ids.end(), [&] (const auto& id) { return library.getTrack (id) == nullptr; }))
    {
        lastStatusMessage = "Choose tracks from the library.";
        return false;
    }
    bool changed = false;
    for (const auto& id : ids) changed = library.setFavourite (id, favourite) || changed;
    if (changed) { invalidateCollectionUndo ("favourites"); ++stateRevision; }
    lastStatusMessage = favourite ? "Tracks added to favourites." : "Tracks removed from favourites.";
    return true;
}

bool PlayerController::makeQueueOccurrences (const std::vector<juce::String>& ids,
                                             std::vector<TrackItem>& occurrences)
{
    if (ids.empty()) { lastStatusMessage = "Choose tracks from the library."; return false; }
    for (const auto& id : ids)
    {
        const auto* track = library.getTrack (id);
        if (track == nullptr) { lastStatusMessage = "A selected track no longer exists."; return false; }
        auto occurrence = *track;
        occurrence.id = juce::Uuid().toString();
        occurrence.libraryId = id;
        occurrences.push_back (std::move (occurrence));
    }
    return true;
}

bool PlayerController::playLibraryTracks (const std::vector<juce::String>& ids, int startIndex)
{
    std::vector<TrackItem> occurrences;
    if (! makeQueueOccurrences (ids, occurrences)) return false;
    if (startIndex < 0 || startIndex >= static_cast<int> (occurrences.size()))
    {
        lastStatusMessage = "Choose a track to play.";
        return false;
    }
    const auto id = occurrences[static_cast<size_t> (startIndex)].id;
    synchronizePlayback();
    playlist.replaceTracks (std::move (occurrences));
    playlist.selectTrack (startIndex);
    invalidateCollectionUndo ("queue");
    ++stateRevision;
    if (loadTrackById (id, true)) return true;
    return navigatePlayback (PlaylistNavigation::manualNext, true);
}

bool PlayerController::enqueueLibraryTracks (const std::vector<juce::String>& ids, bool playNext)
{
    std::vector<TrackItem> occurrences;
    if (! makeQueueOccurrences (ids, occurrences)) return false;
    synchronizePlayback();
    const auto index = playNext && playlist.getSelectedIndex() >= 0 ? playlist.getSelectedIndex() + 1 : playlist.size();
    std::vector<juce::String> nextIds;
    if (playNext && ! playlist.isEmpty())
        for (const auto& occurrence : occurrences) nextIds.push_back (occurrence.id);
    playlist.insertTracks (index, std::move (occurrences));
    if (! nextIds.empty()) playlist.prioritiseNextTracks (nextIds);
    invalidateCollectionUndo ("queue");
    ++stateRevision;
    const auto prepared = refreshUpcomingPlayback();
    if (prepared) lastStatusMessage = playNext ? "Tracks added to play next." : "Tracks appended to queue.";
    return prepared;
}

juce::String PlayerController::saveQueueAsPlaylist (juce::String name)
{
    if (playlist.isEmpty()) { lastStatusMessage = "The queue is empty."; return {}; }
    if (name.trim().isEmpty()) { lastStatusMessage = "Enter a playlist name."; return {}; }
    const auto id = library.createPlaylist (std::move (name));
    std::vector<juce::String> ids;
    for (const auto& track : playlist.getTracks()) ids.push_back (track.libraryId);
    library.addToPlaylist (id, ids);
    ++stateRevision;
    lastStatusMessage = "Queue saved as a playlist.";
    return id;
}

PlayerController::CollectionEdit PlayerController::captureCollectionEdit (const juce::String& collection) const
{
    CollectionEdit edit;
    edit.collectionId = collection;
    if (collection == "queue")
    {
        edit.queue = playlist.getTracks();
        edit.pendingNextTracks = playlist.capturePendingNextTracks();
    }
    else if (collection == "favourites") edit.favourites = library.getFavouriteTrackIds();
    else if (const auto* saved = library.getPlaylist (playlistId (collection))) edit.entries = saved->entries;
    return edit;
}

void PlayerController::recordCollectionEdit (CollectionEdit edit)
{
    constexpr size_t maximumUndoEdits = 32;
    if (collectionUndo.size() == maximumUndoEdits) collectionUndo.erase (collectionUndo.begin());
    collectionUndo.push_back (std::move (edit));
}

void PlayerController::invalidateCollectionUndo (const juce::String& collection)
{
    std::erase_if (collectionUndo, [&] (const auto& edit) { return edit.collectionId == collection; });
}

bool PlayerController::finishQueueEdit()
{
    const auto loaded = playbackCoordinator.getLoadedTrackId();
    if (playlist.isEmpty() || (loaded.isNotEmpty() && playlist.indexOfId (loaded) < 0))
    {
        audioEngine.unload();
        playbackCoordinator.loadFailed();
        playbackCheckpoint.reset ({}, 0.0);
        playbackPlan = {};
        playbackTrackIds.clear();
        failedPlaybackItems.clear();
        playbackGeneration = playbackSequence = 0;
        upcomingPlaybackDirty = false;
    }
    return refreshUpcomingPlayback();
}

bool PlayerController::removeUpcomingQueueEntries (const std::vector<juce::String>& ids)
{
    const AudioEngine::ScopedPlaybackEdit edit (audioEngine);
    synchronizePlayback();
    auto removable = ids;
    std::erase (removable, playbackCoordinator.getLoadedTrackId());
    if (removable.empty()) { lastStatusMessage = "The current track stays in the play queue."; return false; }
    return removeCollectionEntries ("queue", removable);
}

bool PlayerController::canMoveUpcomingQueueEntry (const juce::String& id, int direction) const
{
    if ((direction != -1 && direction != 1) || playlist.isShuffleEnabled()
        || playlist.getRepeatMode() != RepeatMode::off) return false;
    const auto visible = getPlaybackQueue();
    const auto found = std::find_if (visible.begin(), visible.end(), [&] (const auto& item) { return item.track.id == id; });
    if (found == visible.end() || found->current) return false;
    const auto target = static_cast<int> (found - visible.begin()) + direction;
    return juce::isPositiveAndBelow (target, visible.size()) && ! visible[static_cast<size_t> (target)].current;
}

bool PlayerController::moveUpcomingQueueEntry (const juce::String& id, int direction)
{
    const AudioEngine::ScopedPlaybackEdit edit (audioEngine);
    synchronizePlayback();
    if ((direction != -1 && direction != 1) || playlist.isShuffleEnabled()
        || playlist.getRepeatMode() != RepeatMode::off) return false;
    const auto visible = getPlaybackQueue();
    const auto found = std::find_if (visible.begin(), visible.end(), [&] (const auto& item) { return item.track.id == id; });
    if (found == visible.end() || found->current) return false;
    const auto target = static_cast<int> (found - visible.begin()) + direction;
    if (! juce::isPositiveAndBelow (target, visible.size()) || visible[static_cast<size_t> (target)].current) return false;
    const auto source = playlist.indexOfId (id);
    const auto destination = playlist.indexOfId (visible[static_cast<size_t> (target)].track.id);
    return moveTrack (source, destination);
}

bool PlayerController::removeCollectionEntries (const juce::String& collection,
                                                const std::vector<juce::String>& ids)
{
    if (ids.empty()) { lastStatusMessage = "Choose entries to remove."; return false; }
    if (collection == "queue") synchronizePlayback();
    auto undo = captureCollectionEdit (collection);
    bool changed = false;
    if (collection == "queue")
    {
        const auto loaded = playbackCoordinator.getLoadedTrackId();
        if (loaded.isNotEmpty() && std::find (ids.begin(), ids.end(), loaded) != ids.end())
        {
            audioEngine.pause();
            const AudioEngine::ScopedPlaybackEdit edit (audioEngine);
            synchronizePlayback (audioEngine.getPlaybackProgress(), true, nullptr);
            changed = playlist.removeTracks (ids);
        }
        else changed = playlist.removeTracks (ids);
    }
    else if (collection == "favourites")
    {
        if (std::any_of (ids.begin(), ids.end(), [&] (const auto& id) { return ! library.isFavourite (id); })) return false;
        for (const auto& id : ids) changed = library.setFavourite (id, false) || changed;
    }
    else if (const auto* saved = library.getPlaylist (playlistId (collection)))
    {
        if (! containsEveryEntry (saved->entries, ids)) return false;
        changed = library.removeEntries (saved->id, ids);
    }
    if (! changed) { lastStatusMessage = "These entries cannot be removed from this collection."; return false; }
    recordCollectionEdit (std::move (undo));
    ++stateRevision;
    if (collection == "queue" && ! finishQueueEdit()) return false;
    lastStatusMessage = "Entries removed.";
    return true;
}

bool PlayerController::moveCollectionEntries (const juce::String& collection,
                                              const std::vector<juce::String>& ids, int beforeIndex)
{
    if (ids.empty()) { lastStatusMessage = "Choose entries to move."; return false; }
    const auto* saved = library.getPlaylist (playlistId (collection));
    const auto valid = collection == "queue"
        ? beforeIndex >= 0 && beforeIndex <= playlist.size() && containsEveryEntry (playlist.getTracks(), ids)
        : saved != nullptr && beforeIndex >= 0 && beforeIndex <= static_cast<int> (saved->entries.size())
            && containsEveryEntry (saved->entries, ids);
    if (! valid) { lastStatusMessage = "Choose existing entries and a valid destination in a playlist or queue."; return false; }
    if (collection == "queue") synchronizePlayback();
    auto undo = captureCollectionEdit (collection);
    bool changed = false;
    if (collection == "queue")
        changed = playlist.moveTracks (ids, beforeIndex) && ! sameOrder (undo.queue, playlist.getTracks());
    else changed = library.moveEntries (saved->id, ids, beforeIndex);
    if (! changed) { lastStatusMessage = "Collection order unchanged."; return true; }
    recordCollectionEdit (std::move (undo));
    ++stateRevision;
    if (collection == "queue" && ! finishQueueEdit()) return false;
    lastStatusMessage = "Entries reordered.";
    return true;
}

bool PlayerController::sortCollectionEntries (const juce::String& collection,
                                              const juce::String& key, bool descending)
{
    if (key != "title" && key != "artist" && key != "album" && key != "duration")
    {
        lastStatusMessage = "Choose title, artist, album, or duration to sort.";
        return false;
    }
    const auto* saved = library.getPlaylist (playlistId (collection));
    if (saved != nullptr && saved->smartRules)
    {
        lastStatusMessage = "Smart playlist entries are selected by their rules.";
        return false;
    }
    if (collection != "queue" && saved == nullptr)
    {
        lastStatusMessage = "Choose an existing playlist or queue to sort.";
        return false;
    }
    const auto less = [&] (const TrackItem& a, const TrackItem& b)
    {
        const auto comparison = key == "duration" ? (a.durationSeconds < b.durationSeconds ? -1 : a.durationSeconds > b.durationSeconds ? 1 : 0)
            : (key == "artist" ? a.artist : key == "album" ? a.album : a.title)
                .compareIgnoreCase (key == "artist" ? b.artist : key == "album" ? b.album : b.title);
        return descending ? comparison > 0 : comparison < 0;
    };
    if (collection == "queue") synchronizePlayback();
    auto undo = captureCollectionEdit (collection);
    bool changed = false;
    if (collection == "queue")
    {
        auto sorted = playlist.getTracks();
        std::stable_sort (sorted.begin(), sorted.end(), less);
        changed = ! sameOrder (sorted, playlist.getTracks()) && playlist.replaceTracks (std::move (sorted));
    }
    else
    {
        auto sorted = saved->entries;
        std::stable_sort (sorted.begin(), sorted.end(), [&] (const auto& a, const auto& b)
            { return less (*library.getTrack (a.trackId), *library.getTrack (b.trackId)); });
        changed = library.replacePlaylistEntries (saved->id, std::move (sorted));
    }
    if (! changed) { lastStatusMessage = "Collection order unchanged."; return true; }
    recordCollectionEdit (std::move (undo));
    ++stateRevision;
    if (collection == "queue" && ! finishQueueEdit()) return false;
    lastStatusMessage = "Entries sorted.";
    return true;
}

bool PlayerController::canUndoCollectionEdit (const juce::String& collection) const
{
    return std::any_of (collectionUndo.begin(), collectionUndo.end(),
                        [&] (const auto& edit) { return edit.collectionId == collection; });
}

bool PlayerController::undoCollectionEdit (const juce::String& collection)
{
    const auto found = std::find_if (collectionUndo.rbegin(), collectionUndo.rend(),
                                   [&] (const auto& edit) { return edit.collectionId == collection; });
    if (found == collectionUndo.rend()) { lastStatusMessage = "No collection edit to undo."; return false; }
    auto edit = *found;
    collectionUndo.erase (std::next (found).base());
    if (collection == "queue")
    {
        synchronizePlayback();
        for (auto& track : edit.queue)
            if (const auto* canonical = library.getTrack (track.libraryId))
            {
                const auto id = track.id;
                track = *canonical;
                track.id = id;
                track.libraryId = canonical->id;
            }
        playlist.replaceTracks (std::move (edit.queue));
        playlist.restorePendingNextTracks (edit.pendingNextTracks);
    }
    else if (collection == "favourites")
    {
        const auto current = library.getFavouriteTrackIds();
        for (const auto& id : current) library.setFavourite (id, false);
        for (const auto& id : edit.favourites) library.setFavourite (id, true);
    }
    else if (! library.replacePlaylistEntries (playlistId (collection), std::move (edit.entries))) return false;
    ++stateRevision;
    if (collection == "queue" && ! finishQueueEdit()) return false;
    lastStatusMessage = "Collection edit undone.";
    return true;
}

bool PlayerController::relocateLibraryTrack (const juce::String& id, TrackMetadataResult replacement)
{
    juce::StringArray artworkCandidates { replacement.track.artworkCacheKey };
    const auto* existing = library.getTrack (id);
    if (existing != nullptr) artworkCandidates.add (existing->artworkCacheKey);
    if (existing == nullptr || ! replacement.ok || ! replacement.track.available)
    {
        lastStatusMessage = replacement.error.isNotEmpty() ? replacement.error : "Choose a readable audio file for this track.";
        discardUnreferencedArtwork (artworkCandidates);
        return false;
    }
    if (const auto* collision = library.findTrackByFile (replacement.track.file); collision != nullptr && collision->id != id)
    {
        lastStatusMessage = "That file already belongs to another library track.";
        discardUnreferencedArtwork (artworkCandidates);
        return false;
    }
    replacement.track.id = id;
    if (replacement.track.artworkCacheKey.isEmpty()) replacement.track.artworkCacheKey = existing->artworkCacheKey;
    juce::StringArray warnings;
    if (replacement.artworkData.getSize() > 0)
    {
        juce::String error;
        const auto key = artworkStore.store (juce::Uuid().toString(), replacement.artworkData, error);
        if (key.isNotEmpty()) replacement.track.artworkCacheKey = key;
        else if (error.isNotEmpty()) warnings.add (error);
    }
    synchronizePlayback();
    library.replaceTrack (id, std::move (replacement.track));
    refreshQueueMetadata();
    discardUnreferencedArtwork (artworkCandidates);
    ++stateRevision;
    if (! refreshUpcomingPlayback()) return false;
    lastStatusMessage = "Track location updated.";
    if (! warnings.isEmpty()) lastStatusMessage += " Artwork warnings: " + warnings.joinIntoString (", ");
    return true;
}
