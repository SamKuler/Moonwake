#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

#include "library/ArtworkStore.h"
#include "library/LibraryRestore.h"

namespace
{
struct RestoreDirectory
{
    RestoreDirectory()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
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
        beginTest ("refreshes readable tracks and preserves missing track identity and metadata");
        {
            RestoreDirectory directory;
            const auto audio = directory.root.getChildFile ("work.wav");
            expect (writeRestoreWave (audio));
            TrackItem readable { audio, "Old title", 99.0 };
            readable.id = "readable-id";
            readable.artist = "Saved artist";
            readable.album = "Saved album";
            TrackItem missing { directory.root.getChildFile ("missing.wav"),
                                "Missing title", 123.0 };
            missing.id = "missing-id";
            missing.artist = "Missing artist";
            missing.album = "Missing album";
            missing.channelCount = 6;
            missing.sampleRate = 96000.0;
            PlaylistModel playlist;
            playlist.addTrack (readable);
            playlist.addTrack (missing);
            expect (playlist.selectTrack (1));
            playlist.setRepeatMode (RepeatMode::all);
            playlist.setShuffleEnabled (true);
            juce::StringArray warnings;
            expect (prepareLibraryRestore (
                playlist, directory.root.getChildFile ("artwork"), nullptr, warnings));
            expectEquals (playlist.size(), 2);
            const auto* refreshed = playlist.getTrack (0);
            expectEquals (refreshed->id, readable.id);
            expectEquals (refreshed->title, juce::String ("Fresh title"));
            expectEquals (refreshed->artist, readable.artist);
            expectEquals (refreshed->album, readable.album);
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
            playlist.addTrack (track);
            juce::StringArray warnings;
            expect (prepareLibraryRestore (playlist, cache, nullptr, warnings));
            expect (warnings.isEmpty(), warnings.joinIntoString (" "));
            expect (store.isValid (playlist.getTrack (0)->artworkCacheKey));

            const auto blockedCache = directory.root.getChildFile ("cache-file");
            expect (blockedCache.replaceWithText ("not a directory"));
            PlaylistModel blockedPlaylist;
            track.artworkCacheKey.clear();
            blockedPlaylist.addTrack (track);
            warnings.clear();
            expect (prepareLibraryRestore (
                blockedPlaylist, blockedCache, nullptr, warnings));
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
            playlist.addTrack (track);
            std::atomic_bool cancelled { true };
            juce::StringArray warnings;
            expect (! prepareLibraryRestore (
                playlist, directory.root.getChildFile ("cache"), &cancelled, warnings));
            expectEquals (playlist.getTrack (0)->id, track.id);
            expect (playlist.getTrack (0)->available);
            expect (warnings.isEmpty());
        }
    }
};

LibraryRestoreTests libraryRestoreTests;
} // namespace
