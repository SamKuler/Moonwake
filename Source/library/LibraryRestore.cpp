#include "LibraryRestore.h"

#include "audio/TrackMetadataReader.h"
#include "library/ArtworkStore.h"

#include <utility>

namespace
{
bool prepareTracks (std::vector<TrackItem>& tracks,
                    const juce::File& artworkCacheRoot,
                    const std::atomic_bool* cancelled,
                    juce::StringArray& warnings)
{
    const auto isCancelled = [cancelled]
    {
        return cancelled != nullptr && cancelled->load();
    };
    std::vector<TrackItem> prepared;
    TrackMetadataReader reader;
    ArtworkStore artwork (artworkCacheRoot);
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
        if (isCancelled())
            return false;

        auto restored = saved;
        const auto needsArtwork = restored.artworkCacheKey.isEmpty()
                               || ! artwork.isValid (restored.artworkCacheKey);
        if (needsArtwork && restored.artworkCacheKey.isNotEmpty())
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
            restored.durationSeconds = metadata.track.durationSeconds;
            restored.sampleRate = metadata.track.sampleRate;
            restored.channelCount = metadata.track.channelCount;
            restored.bitDepth = metadata.track.bitDepth;
            restored.formatName = metadata.track.formatName;
            const auto updateText = [] (juce::String& destination,
                                        const juce::String& refreshed)
            {
                if (refreshed.isNotEmpty()) destination = refreshed;
            };
            updateText (restored.title, metadata.track.title);
            updateText (restored.artist, metadata.track.artist);
            updateText (restored.album, metadata.track.album);
            updateText (restored.albumArtist, metadata.track.albumArtist);
            updateText (restored.genre, metadata.track.genre);
            if (metadata.track.year > 0) restored.year = metadata.track.year;
            if (metadata.track.trackNumber > 0)
                restored.trackNumber = metadata.track.trackNumber;
            if (metadata.track.discNumber > 0)
                restored.discNumber = metadata.track.discNumber;
            if (needsArtwork && metadata.artworkData.getSize() > 0)
            {
                juce::String error;
                restored.artworkCacheKey = artwork.store (
                    restored.id, metadata.artworkData, error);
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

bool prepareLibraryRestore (MusicLibrary& library, PlaylistModel& playlist,
                            const juce::File& artworkCacheRoot,
                            const std::atomic_bool* cancelled,
                            juce::StringArray& warnings)
{
    auto tracks = library.getTracks();
    if (! prepareTracks (tracks, artworkCacheRoot, cancelled, warnings)) return false;
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
    return true;
}
