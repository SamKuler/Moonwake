#include "PlayerController.h"

#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <unordered_map>

juce::String PlayerController::addMusicDirectory (const juce::File& folder)
{
    if (! folder.isDirectory())
    {
        lastStatusMessage = "Choose an available music folder.";
        return {};
    }
    const auto identity = trackFileIdentity (folder);
    for (const auto& directory : musicDirectories)
        if (directory.folder == folder || trackFileIdentity (directory.folder) == identity)
        {
            lastStatusMessage = "This music folder is already managed.";
            return directory.id;
        }
    MusicDirectory directory;
    directory.folder = folder;
    musicDirectories.push_back (directory);
    ++stateRevision;
    lastStatusMessage = "Music folder added.";
    return directory.id;
}

bool PlayerController::removeMusicDirectory (const juce::String& id)
{
    const auto removed = std::erase_if (musicDirectories, [&] (const auto& directory) { return directory.id == id; });
    if (removed == 0) return false;
    ++stateRevision;
    lastStatusMessage = "Folder monitoring stopped. Songs and playlists are kept.";
    return true;
}

bool PlayerController::setMusicDirectoryAutomatic (const juce::String& id, bool enabled)
{
    for (auto& directory : musicDirectories)
        if (directory.id == id)
        {
            if (directory.automatic != enabled)
            {
                directory.automatic = enabled;
                ++stateRevision;
            }
            lastStatusMessage = enabled ? "Automatic scanning enabled." : "Automatic scanning paused.";
            return true;
        }
    return false;
}

bool PlayerController::applyMusicDirectoryScan (MusicDirectoryScanResult result)
{
    if (result.cancelled)
    {
        MusicDirectoryScanService::discardPreparedArtwork (result, artworkStore.root());
        lastStatusMessage = "Music folder scan cancelled.";
        return false;
    }
    synchronizePlayback();
    auto nextTracks = library.getTracks();
    std::unordered_map<std::string, size_t> indices;
    for (size_t index = 0; index < nextTracks.size(); ++index)
        indices.emplace (nextTracks[index].id.toStdString(), index);
    juce::StringArray artworkCandidates = result.preparedArtworkKeys;
    bool changed = false;
    int updatedCount = 0, addedCount = 0, missingCount = 0;
    for (const auto& metadata : result.updates)
        if (const auto found = indices.find (metadata.track.id.toStdString()); found != indices.end())
        {
            auto& track = nextTracks[found->second];
            artworkCandidates.add (track.artworkCacheKey);
            auto replacement = metadata.track;
            replacement.addedAtMs = track.addedAtMs;
            replacement.libraryId.clear();
            if (replacement.artworkCacheKey.isEmpty()) replacement.artworkCacheKey = track.artworkCacheKey;
            track = std::move (replacement);
            ++updatedCount;
            changed = true;
        }
    for (const auto& id : result.missingTrackIds)
        if (const auto found = indices.find (id.toStdString()); found != indices.end())
            if (auto& track = nextTracks[found->second]; track.available)
            {
                track.available = false;
                ++missingCount;
                changed = true;
            }

    // Commit all existing paths together, including swaps, before resolving new files by path.
    if (changed && ! library.restore (std::move (nextTracks), library.getPlaylists(), library.getFavouriteTrackIds()))
    {
        discardUnreferencedArtwork (artworkCandidates);
        lastStatusMessage = "The scanned music could not be applied to the library.";
        return false;
    }
    juce::StringArray warnings = result.artworkWarnings;
    for (auto& metadata : result.updates)
        if (! indices.contains (metadata.track.id.toStdString()))
        {
            static_cast<void> (storePreparedLibraryTrack (metadata, warnings, artworkCandidates));
            ++addedCount;
            changed = true;
        }
    juce::String playbackWarning;
    if (changed)
    {
        refreshQueueMetadata();
        ++stateRevision;
        if (! refreshUpcomingPlayback()) playbackWarning = lastStatusMessage;
    }
    discardUnreferencedArtwork (artworkCandidates);
    lastStatusMessage = "Scan complete: " + juce::String (addedCount) + " added, "
        + juce::String (updatedCount) + " updated, " + juce::String (missingCount) + " missing.";
    warnings.addArray (result.errors);
    if (playbackWarning.isNotEmpty()) warnings.add (playbackWarning);
    if (! warnings.isEmpty()) lastStatusMessage += " " + warnings.joinIntoString (" ");
    return playbackWarning.isEmpty();
}
