#include "LibraryRestore.h"

#include "audio/TrackMetadataReader.h"
#include "library/ArtworkStore.h"

#include <utility>

bool prepareLibraryRestore (PlaylistModel& playlist,
                            const juce::File& artworkCacheRoot,
                            const std::atomic_bool* cancelled,
                            juce::StringArray& warnings)
{
    const auto isCancelled = [cancelled]
    {
        return cancelled != nullptr && cancelled->load();
    };
    const auto selectedTrackId = playlist.getSelectedTrack() != nullptr
                                   ? playlist.getSelectedTrack()->id
                                   : juce::String{};
    PlaylistModel prepared;
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

    for (const auto& saved : playlist.getTracks())
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
        prepared.addTrack (std::move (restored));
    }

    if (isCancelled())
        return false;

    prepared.setRepeatMode (playlist.getRepeatMode());
    if (selectedTrackId.isNotEmpty())
        prepared.selectTrack (prepared.indexOfId (selectedTrackId));
    prepared.setShuffleEnabled (playlist.isShuffleEnabled());
    playlist = std::move (prepared);

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
