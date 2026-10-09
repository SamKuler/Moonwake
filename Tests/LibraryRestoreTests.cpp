#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

#include "library/ArtworkStore.h"
#include "library/LibraryRestore.h"

namespace
{
struct RestoreDirectory
{
    RestoreDirectory()
        : root (juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                    .getNonexistentChildFile ("moonwake-library-restore", {}))
    {
        static_cast<void> (root.createDirectory());
    }
    ~RestoreDirectory() { static_cast<void> (root.deleteRecursively()); }
    juce::File root;
};

bool writeRestoreWave (const juce::File& file)
{
    auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (
        output, juce::AudioFormatWriterOptions{}
                    .withSampleRate (48000.0)
                    .withNumChannels (2)
                    .withBitsPerSample (16)
                    .withMetadata (juce::WavAudioFormat::riffInfoTitle, "Fresh title"));
    if (writer == nullptr) return false;
    juce::AudioBuffer<float> samples (2, 4800);
    samples.clear();
    return writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples());
}

juce::MemoryBlock restoreArtwork()
{
    juce::Image image (juce::Image::ARGB, 8, 8, true);
    juce::Graphics (image).fillAll (juce::Colours::cornflowerblue);
    juce::MemoryOutputStream output;
    juce::PNGImageFormat png;
    static_cast<void> (png.writeImageToStream (image, output));
    return output.getMemoryBlock();
}

class LibraryRestoreTests final : public juce::UnitTest
{
public:
    LibraryRestoreTests() : UnitTest ("LibraryRestore", "library") {}

    void runTest() override
    {
        beginTest ("startup refresh preserves moved file identity when a different recording occupies its old path");
        {
            RestoreDirectory directory;
            const auto original = directory.root.getChildFile ("original.wav");
            const auto moved = directory.root.getChildFile ("moved.wav");
            expect (writeRestoreWave (original));
            MusicLibrary library;
            TrackItem saved { original, "Original recording", 1.0 };
            saved.fileSizeBytes = original.getSize();
            saved.fileModifiedAtMs = original.getLastModificationTime().toMilliseconds();
            const auto id = library.upsertTrack (saved);
            const auto identity = library.getTrack (id)->fileIdentity;
            const auto savedTracks = library.getTracks();
            expect (original.moveFileTo (moved));
            expect (writeRestoreWave (original));
            expect (original.setLastModificationTime (juce::Time (saved.fileModifiedAtMs + 5000)));
            expect (library.restore (savedTracks, {}, {}));
            PlaylistModel queue;
            juce::StringArray warnings;
            expect (prepareLibraryRestore (library, queue, directory.root.getChildFile ("cache"), nullptr, warnings,
                                            { { "managed", directory.root, true } }));
            expectEquals (library.getTrack (id)->fileIdentity, identity);
            expect (library.findTrackByFile (moved) == library.getTrack (id));
            expectEquals (library.getTrack (id)->title, saved.title);
            expectEquals (library.getTrack (id)->fileSizeBytes, saved.fileSizeBytes);
            expectEquals (library.getTrack (id)->fileModifiedAtMs, saved.fileModifiedAtMs);
            expectWithinAbsoluteError (library.getTrack (id)->durationSeconds, saved.durationSeconds, 0.0001);
            expect (! library.getTrack (id)->available);
            expect (warnings.joinIntoString (" ").contains ("moved or replaced"));
            expectEquals (library.upsertTrack ({ moved, "Original recovered", 1.0 }), id);
            expect (library.getTrack (id)->available);
            expect (library.restore (savedTracks, {}, {}));
            expect (prepareLibraryRestore (library, queue, directory.root.getChildFile ("cache"), nullptr, warnings,
                                            { { "paused", directory.root, false } }));
            expect (library.getTrack (id)->available);
            expectEquals (library.getTrack (id)->title, juce::String ("Fresh title"));
            expect (library.restore (savedTracks, {}, {}));
            expect (prepareLibraryRestore (library, queue, directory.root.getChildFile ("cache"), nullptr, warnings));
            expect (library.getTrack (id)->available);
        }

        beginTest ("unique library refresh synchronizes repeated queue occurrences without changing identities");
        {
            RestoreDirectory directory;
            const auto audio = directory.root.getChildFile ("fresh.wav");
            expect (writeRestoreWave (audio));
            MusicLibrary library;
            const auto readable = library.upsertTrack ({ audio, "Saved", 99.0 });
            const auto missing = library.upsertTrack ({ directory.root.getChildFile ("missing.wav"), "Missing", 40.0 });
            const auto list = library.createPlaylist ("Saved list");
            const auto entries = library.addToPlaylist (list, { readable, missing, readable });
            library.setFavourite (readable, true);
            PlaylistModel queue;
            int occurrence = 0;
            for (const auto& id : { readable, missing, readable, missing })
            {
                auto track = *library.getTrack (id);
                track.libraryId = id;
                track.id = "occurrence-" + juce::String (occurrence++);
                queue.addTrack (std::move (track));
            }
            queue.selectTrack (2);
            queue.setRepeatMode (RepeatMode::all);
            queue.setShuffleEnabled (true);
            juce::StringArray warnings;
            expect (prepareLibraryRestore (library, queue, directory.root.getChildFile ("cache"), nullptr, warnings));
            expectEquals (queue.size(), 4);
            expectEquals (queue.getSelectedTrack()->id, juce::String ("occurrence-2"));
            expectEquals (queue.getTrack (0)->libraryId, readable);
            expectEquals (queue.getTrack (2)->libraryId, readable);
            expectEquals (queue.getTrack (0)->title, juce::String ("Fresh title"));
            expectEquals (queue.getTrack (2)->title, library.getTrack (readable)->title);
            expectWithinAbsoluteError (queue.getTrack (2)->durationSeconds, 0.1, 0.0001);
            expect (! queue.getTrack (1)->available && ! queue.getTrack (3)->available);
            expect (warnings.joinIntoString (" ").startsWith ("1 saved track is unavailable."));
            expect (queue.isShuffleEnabled() && queue.getRepeatMode() == RepeatMode::all);
            expect (library.getPlaylist (list)->entries == entries);
            expect (library.isFavourite (readable));
            std::atomic_bool cancelled { true };
            expect (! prepareLibraryRestore (library, queue, directory.root.getChildFile ("cache"), &cancelled, warnings));
            expectEquals (queue.getSelectedTrack()->id, juce::String ("occurrence-2"));
            expect (library.getPlaylist (list)->entries == entries);
        }

        beginTest ("refreshes readable tracks and preserves missing track identity and metadata");
        {
            RestoreDirectory directory;
            const auto audio = directory.root.getChildFile ("work.wav");
            expect (writeRestoreWave (audio));
            TrackItem readable { audio, "Old title", 99.0 };
            readable.id = "readable-id";
            readable.artist = "Saved artist";
            readable.album = "Saved album";
            readable.addedAtMs = 1700000000123LL;
            TrackItem missing { directory.root.getChildFile ("missing.wav"),
                                "Missing title", 123.0 };
            missing.id = "missing-id";
            missing.artist = "Missing artist";
            missing.album = "Missing album";
            missing.channelCount = 6;
            missing.sampleRate = 96000.0;
            missing.fileIdentity = "posix:abc:def";
            missing.fileSizeBytes = 999;
            missing.fileModifiedAtMs = 1700000000999LL;
            PlaylistModel playlist;
            MusicLibrary library;
            readable.libraryId = library.upsertTrack (readable);
            missing.libraryId = library.upsertTrack (missing);
            playlist.addTrack (readable);
            playlist.addTrack (missing);
            expect (playlist.selectTrack (1));
            playlist.setRepeatMode (RepeatMode::all);
            playlist.setShuffleEnabled (true);
            juce::StringArray warnings;
            expect (prepareLibraryRestore (
                library, playlist, directory.root.getChildFile ("artwork"), nullptr, warnings));
            expectEquals (playlist.size(), 2);
            const auto* refreshed = playlist.getTrack (0);
            expectEquals (refreshed->id, readable.id);
            expectEquals (refreshed->title, juce::String ("Fresh title"));
            expectEquals (refreshed->artist, readable.artist);
            expectEquals (refreshed->album, readable.album);
            expectEquals (refreshed->addedAtMs, readable.addedAtMs);
            expectEquals (refreshed->fileSizeBytes, audio.getSize());
            expectEquals (refreshed->fileModifiedAtMs, audio.getLastModificationTime().toMilliseconds());
            expect (refreshed->fileIdentity.isNotEmpty());
            expectEquals (playlist.getTrack (1)->fileIdentity, missing.fileIdentity);
            expectEquals (playlist.getTrack (1)->fileSizeBytes, missing.fileSizeBytes);
            expectEquals (playlist.getTrack (1)->fileModifiedAtMs, missing.fileModifiedAtMs);
            expectWithinAbsoluteError (refreshed->durationSeconds, 0.1, 0.0001);
            expectWithinAbsoluteError (refreshed->sampleRate, 48000.0, 0.001);
            expectEquals (refreshed->channelCount, 2);
            expect (refreshed->available);
            const auto* unavailable = playlist.getSelectedTrack();
            expectEquals (unavailable->id, missing.id);
            expectEquals (unavailable->title, missing.title);
            expectEquals (unavailable->artist, missing.artist);
            expectEquals (unavailable->album, missing.album);
            expectWithinAbsoluteError (unavailable->durationSeconds, 123.0, 0.0001);
            expectEquals (unavailable->channelCount, 6);
            expect (! unavailable->available);
            expect (playlist.isShuffleEnabled());
            expect (playlist.getRepeatMode() == RepeatMode::all);
            expect (warnings.joinIntoString (" ").containsIgnoreCase ("unavailable"));
        }

        beginTest ("rebuilds corrupt artwork and reports cache write failures");
        {
            RestoreDirectory directory;
            const auto audio = directory.root.getChildFile ("work.wav");
            expect (writeRestoreWave (audio));
            const auto image = restoreArtwork();
            expect (directory.root.getChildFile ("cover.png").replaceWithData (
                image.getData(), image.getSize()));
            const auto cache = directory.root.getChildFile ("cache");
            expect (cache.createDirectory());
            ArtworkStore store (cache);
            expect (store.resolve ("track-id").replaceWithText ("corrupt"));
            TrackItem track { audio, "Saved", 0.0 };
            track.id = "track-id";
            track.artworkCacheKey = "track-id";
            PlaylistModel playlist;
            MusicLibrary library;
            track.libraryId = library.upsertTrack (track);
            playlist.addTrack (track);
            juce::StringArray warnings;
            expect (prepareLibraryRestore (library, playlist, cache, nullptr, warnings));
            expect (warnings.isEmpty(), warnings.joinIntoString (" "));
            expect (store.isValid (playlist.getTrack (0)->artworkCacheKey));

            const auto blockedCache = directory.root.getChildFile ("cache-file");
            expect (blockedCache.replaceWithText ("not a directory"));
            PlaylistModel blockedPlaylist;
            track.artworkCacheKey.clear();
            MusicLibrary blockedLibrary;
            track.libraryId = blockedLibrary.upsertTrack (track);
            blockedPlaylist.addTrack (track);
            warnings.clear();
            expect (prepareLibraryRestore (
                blockedLibrary, blockedPlaylist, blockedCache, nullptr, warnings));
            expect (blockedPlaylist.getTrack (0)->available);
            expect (blockedPlaylist.getTrack (0)->artworkCacheKey.isEmpty());
            expect (warnings.joinIntoString (" ").containsIgnoreCase ("artwork"));
        }

        beginTest ("cancelled preparation leaves the original playlist intact");
        {
            RestoreDirectory directory;
            PlaylistModel playlist;
            TrackItem track { directory.root.getChildFile ("missing.wav"), "Saved", 20.0 };
            track.id = "saved-id";
            MusicLibrary library;
            track.libraryId = library.upsertTrack (track);
            playlist.addTrack (track);
            std::atomic_bool cancelled { true };
            juce::StringArray warnings;
            expect (! prepareLibraryRestore (
                library, playlist, directory.root.getChildFile ("cache"), &cancelled, warnings));
            expectEquals (playlist.getTrack (0)->id, track.id);
            expect (playlist.getTrack (0)->available);
            expect (warnings.isEmpty());
        }
    }
};

LibraryRestoreTests libraryRestoreTests;
} // namespace
