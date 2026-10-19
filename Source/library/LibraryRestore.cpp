#include "LibraryRestore.h"

#include "library/TrackMetadataReader.h"
#include "library/ArtworkStore.h"
#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <utility>

namespace
{
bool prepareTracks (std::vector<TrackItem>& tracks,
                    const juce::File& artworkCacheRoot,
                    const std::atomic_bool* cancelled,
                    juce::StringArray& warnings,
                    const std::vector<MusicDirectory>& managedDirectories,
                    juce::StringArray& preparedArtworkKeys,
                    LibraryRestoreProgress* progress)
{
    const auto isCancelled = [cancelled]
    {
        return cancelled != nullptr && cancelled->load();
    };
    std::vector<TrackItem> prepared;
    TrackMetadataReader reader;
    ArtworkStore artwork (artworkCacheRoot);
    ArtworkStore::EncodingCache artworkEncoding;
    int unavailableCount = 0;
    int artworkWarningCount = 0;
    juce::String firstUnavailableError;
    juce::String firstArtworkError;
    const auto recordArtworkWarning = [&] (const juce::String& message)
    {
        if (artworkWarningCount++ == 0)
            firstArtworkError = message;
    };

    for (const auto& saved : tracks)
    {
        if (progress != nullptr) progress->completed.store (prepared.size());
        if (isCancelled())
            return false;

        auto restored = saved;
        const auto currentIdentity = trackFileIdentity (saved.file);
        const auto hasPhysicalIdentity = saved.fileIdentity.startsWith ("posix:")
                                      || saved.fileIdentity.startsWith ("windows:");
        const auto automaticallyManaged = std::any_of (managedDirectories.begin(), managedDirectories.end(),
            [&] (const auto& directory) { return directory.automatic && saved.file.isAChildOf (directory.folder); });
        if (hasPhysicalIdentity && automaticallyManaged)
        {
            if (! currentIdentity.startsWith ("path:") && currentIdentity != saved.fileIdentity)
            {
                restored.available = false;
                if (unavailableCount++ == 0)
                    firstUnavailableError = "Audio file moved or replaced: " + saved.file.getFullPathName();
                prepared.push_back (std::move (restored));
                continue;
            }
        }
        const auto sourceChanged = (saved.fileSizeBytes > 0 && saved.fileSizeBytes != saved.file.getSize())
                                || (saved.fileModifiedAtMs > 0 && saved.fileModifiedAtMs != saved.file.getLastModificationTime().toMilliseconds())
                                || (hasPhysicalIdentity && currentIdentity != saved.fileIdentity);
        const auto invalidArtwork = restored.artworkCacheKey.isNotEmpty()
                                 && ! artwork.isValid (restored.artworkCacheKey);
        const auto needsArtwork = sourceChanged || restored.artworkCacheKey.isEmpty() || invalidArtwork;
        if (invalidArtwork)
        {
            if (! artwork.remove (restored.artworkCacheKey))
                recordArtworkWarning ("Could not remove invalid artwork for "
                                      + restored.file.getFileName() + ".");
            restored.artworkCacheKey.clear();
        }
        if (isCancelled())
            return false;

        const auto metadata = reader.read (saved.file, needsArtwork);
        if (isCancelled())
            return false;

        if (metadata.ok)
        {
            if (sourceChanged)
            {
                restored = metadata.track;
                restored.id = saved.id;
                restored.libraryId = saved.libraryId;
                restored.addedAtMs = saved.addedAtMs;
                restored.reservedEffectChainState = saved.reservedEffectChainState;
            }
            else
            {
                if (! restored.loudness.measured) restored.loudness = metadata.track.loudness;
                restored.durationSeconds = metadata.track.durationSeconds;
                restored.sampleRate = metadata.track.sampleRate;
                restored.channelCount = metadata.track.channelCount;
                restored.bitDepth = metadata.track.bitDepth;
                restored.formatName = metadata.track.formatName;
                restored.fileSizeBytes = metadata.track.fileSizeBytes;
                restored.fileModifiedAtMs = metadata.track.fileModifiedAtMs;
                restored.fileIdentity = metadata.track.fileIdentity;
                const auto updateText = [] (juce::String& destination,
                                            const juce::String& refreshed)
                {
                    if (refreshed.isNotEmpty()) destination = refreshed;
                };
                updateText (restored.title, metadata.track.title);
                if (metadata.track.artist.isNotEmpty() || ! metadata.track.artists.isEmpty())
                {
                    restored.artist = metadata.track.artist;
                    restored.artists = metadata.track.artists;
                }
                updateText (restored.album, metadata.track.album);
                updateText (restored.albumArtist, metadata.track.albumArtist);
                updateText (restored.genre, metadata.track.genre);
                if (metadata.track.year > 0) restored.year = metadata.track.year;
                if (metadata.track.trackNumber > 0)
                    restored.trackNumber = metadata.track.trackNumber;
                if (metadata.track.discNumber > 0)
                    restored.discNumber = metadata.track.discNumber;
            }
            if (needsArtwork && metadata.artworkData.getSize() > 0)
            {
                juce::String error;
                restored.artworkCacheKey = artwork.store (
                    metadata.track.id, metadata.artworkData, error, &artworkEncoding);
                if (restored.artworkCacheKey.isNotEmpty())
                    preparedArtworkKeys.add (restored.artworkCacheKey);
                if (error.isNotEmpty())
                    recordArtworkWarning (restored.file.getFileName()
                                          + " (" + error + ")");
            }
            restored.available = true;
        }
        else
        {
            restored.available = false;
            if (unavailableCount++ == 0)
                firstUnavailableError = metadata.error;
        }
        prepared.push_back (std::move (restored));
    }

    if (isCancelled())
        return false;

    if (progress != nullptr) progress->completed.store (prepared.size());
    tracks = std::move (prepared);

    if (unavailableCount > 0)
        warnings.add (juce::String (unavailableCount)
                      + (unavailableCount == 1 ? " saved track is unavailable. "
                                               : " saved tracks are unavailable. ")
                      + firstUnavailableError);
    if (artworkWarningCount > 0)
        warnings.add (juce::String (artworkWarningCount)
                      + (artworkWarningCount == 1 ? " artwork warning. "
                                                 : " artwork warnings. ")
                      + firstArtworkError);
    return true;
}
} // namespace

PreparedRestoreArtwork::~PreparedRestoreArtwork()
{
    const ArtworkStore artwork (cacheRoot);
    for (const auto& key : keys) static_cast<void> (artwork.remove (key));
}

bool prepareLibraryRestore (MusicLibrary& library, PlaylistModel& playlist,
                            const juce::File& artworkCacheRoot,
                            const std::atomic_bool* cancelled,
                            juce::StringArray& warnings,
                            const std::vector<MusicDirectory>& managedDirectories,
                            PreparedRestoreArtwork* preparedArtwork,
                            LibraryRestoreProgress* progress)
{
    PreparedRestoreArtwork stagedArtwork (artworkCacheRoot);
    auto tracks = library.getTracks();
    if (progress != nullptr)
    {
        progress->completed.store (0);
        progress->total.store (tracks.size());
    }
    if (! prepareTracks (tracks, artworkCacheRoot, cancelled, warnings, managedDirectories, stagedArtwork.keys, progress)) return false;
    auto savedPlaylists = library.getPlaylists();
    const ArtworkStore artwork (artworkCacheRoot);
    for (auto& saved : savedPlaylists)
        if (saved.artworkCacheKey.isNotEmpty() && ! artwork.isValid (saved.artworkCacheKey))
        {
            warnings.add ("Playlist cover unavailable: " + saved.name + ". Using automatic artwork.");
            saved.artworkCacheKey.clear();
        }
    MusicLibrary prepared;
    if (! prepared.restore (std::move (tracks), std::move (savedPlaylists), library.getFavouriteTrackIds()))
        return false;
    auto queue = playlist.getTracks();
    for (auto& occurrence : queue)
    {
        const auto* record = prepared.getTrack (occurrence.libraryId);
        if (record == nullptr)
        {
            warnings.add ("A queue entry references a missing library record.");
            return false;
        }
        const auto occurrenceId = occurrence.id;
        occurrence = *record;
        occurrence.libraryId = record->id;
        occurrence.id = occurrenceId;
    }
    if (cancelled != nullptr && cancelled->load()) return false;
    library = std::move (prepared);
    playlist.replaceTracks (std::move (queue));
    if (preparedArtwork != nullptr) preparedArtwork->keys.addArray (stagedArtwork.keys);
    stagedArtwork.keys.clear();
    return true;
}
