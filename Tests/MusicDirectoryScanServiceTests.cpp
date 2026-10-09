#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "library/MusicDirectoryScanService.h"
#include "ArtistMetadataFixtures.h"

#include <algorithm>
#include <filesystem>

namespace
{
bool dispatchScanUntil (const std::function<bool()>& complete)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! complete() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return complete();
}

bool writeScanWav (const juce::File& file, const juce::String& title)
{
    if (file.existsAsFile() && ! file.deleteFile()) return false;
    auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (
        output, juce::AudioFormatWriterOptions{}
            .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16)
            .withMetadata (juce::WavAudioFormat::riffInfoTitle, title));
    if (writer == nullptr) return false;
    juce::AudioBuffer<float> samples (2, 480);
    samples.clear();
    return writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples());
}

bool writeScanArtwork (const juce::File& file)
{
    juce::Image image (juce::Image::RGB, 2, 2, true);
    juce::FileOutputStream output (file);
    juce::PNGImageFormat png;
    return output.openedOk() && png.writeImageToStream (image, output);
}

struct ScanFixture
{
    ScanFixture()
        : root (juce::File (MOONWAKE_SOURCE_DIR).getChildFile ("build-directory-scan-tests")
                    .getChildFile (juce::Uuid().toString()))
    {
        static_cast<void> (root.createDirectory());
    }
    ~ScanFixture() { static_cast<void> (root.deleteRecursively()); }
    juce::File root;
};

class MusicDirectoryScanServiceTests final : public juce::UnitTest
{
public:
    MusicDirectoryScanServiceTests() : UnitTest ("MusicDirectoryScanService", "library") {}

    MusicDirectoryScanResult scan (MusicDirectoryScanService& service,
                                    std::vector<MusicDirectory> directories,
                                    std::vector<TrackItem> known = {})
    {
        MusicDirectoryScanResult result;
        bool completed = false;
        expect (service.start (std::move (directories), std::move (known), {},
            [&] (MusicDirectoryScanResult prepared)
            {
                expect (juce::MessageManager::getInstance()->isThisTheMessageThread());
                result = std::move (prepared);
                completed = true;
            }));
        expect (dispatchScanUntil ([&] { return completed; }));
        return result;
    }

    void runTest() override
    {
        beginTest ("unchanged legacy artist metadata refreshes once and preserves library references");
        {
            ScanFixture fixture;
            const auto file = fixture.root.getChildFile ("collaboration.mp3");
            expect (artistMetadataFixtures::writeMp3 (file, 3, 1, { "First/Second" }));
            TrackMetadataReader reader;
            auto known = reader.read (file, false).track;
            known.id = "saved-recording";
            known.addedAtMs = 1234;
            known.artists.clear();
            known.artist = "First/Second";
            MusicDirectoryScanService service;
            const MusicDirectory directory { "music", fixture.root, true };
            const auto upgraded = scan (service, { directory }, { known });
            expectEquals (upgraded.metadataReads, 1);
            expectEquals (static_cast<int> (upgraded.updates.size()), 1);
            if (! upgraded.updates.empty())
            {
                const auto& updated = upgraded.updates.front().track;
                expectEquals (updated.id, known.id);
                expectEquals (updated.addedAtMs, known.addedAtMs);
                expect (updated.artists == juce::StringArray { "First", "Second" });
                const auto unchanged = scan (service, { directory }, { updated });
                expectEquals (unchanged.metadataReads, 0);
                expect (unchanged.updates.empty());
            }
        }

        beginTest ("Unicode directory scans preserve exact paths and skip unchanged metadata");
        {
            ScanFixture fixture;
            const auto music = fixture.root.getChildFile (juce::String::fromUTF8 (u8"中文音乐"));
            const auto album = music.getChildFile (juce::String::fromUTF8 (u8"专辑 🎵"));
            const auto audio = album.getChildFile (juce::String::fromUTF8 (u8"月光 夜曲.wav"));
            expect (album.createDirectory());
            expect (writeScanWav (audio, "Unicode recording"));
            MusicDirectoryScanService service;
            const MusicDirectory directory { "unicode-directory", music, true };
            const auto first = scan (service, { directory });
            expect (first.errors.isEmpty());
            expectEquals (first.metadataReads, 1);
            expectEquals (static_cast<int> (first.updates.size()), 1);
            if (! first.updates.empty())
            {
                const auto known = first.updates.front().track;
                expect (known.file == audio);
                const auto unchanged = scan (service, { directory }, { known });
                expectEquals (unchanged.metadataReads, 0);
                expect (unchanged.updates.empty());
                expect (unchanged.missingTrackIds.isEmpty());
                const auto moved = album.getChildFile (juce::String::fromUTF8 (u8"重命名 夜曲.wav"));
                expect (audio.moveFileTo (moved));
                const auto renamed = scan (service, { directory }, { known });
                expectEquals (renamed.metadataReads, 0);
                expectEquals (static_cast<int> (renamed.updates.size()), 1);
                if (! renamed.updates.empty())
                {
                    expectEquals (renamed.updates.front().track.id, known.id);
                    expect (renamed.updates.front().track.file == moved);
                }
                expect (renamed.missingTrackIds.isEmpty());
            }
        }

        beginTest ("unchanged scans skip metadata and changed files retain their library identity");
        {
            ScanFixture fixture;
            const auto audio = fixture.root.getChildFile ("song.wav");
            expect (writeScanWav (audio, "First title"));
            MusicDirectoryScanService service;
            const MusicDirectory directory { "music", fixture.root, true };
            const auto first = scan (service, { directory });
            expect (! first.cancelled);
            expectEquals (first.metadataReads, 1);
            expectEquals (static_cast<int> (first.updates.size()), 1);
            expectEquals (static_cast<int> (first.roots.size()), 1);
            if (first.updates.empty()) return;
            auto known = first.updates.front().track;
            expectEquals (known.title, juce::String ("First title"));
            known.id = "retained-library-id";
            known.libraryId = known.id;
            known.addedAtMs = 1234;
            const auto unchanged = scan (service, { directory }, { known });
            expectEquals (unchanged.metadataReads, 0);
            expectEquals (unchanged.unchangedFiles, 1);
            expect (unchanged.updates.empty());
            expect (unchanged.missingTrackIds.isEmpty());
            expect (writeScanWav (audio, "Changed title with a different size"));
            const auto changed = scan (service, { directory }, { known });
            expectEquals (changed.metadataReads, 1);
            expectEquals (static_cast<int> (changed.updates.size()), 1);
            if (! changed.updates.empty())
            {
                expectEquals (changed.updates.front().track.id, known.id);
                expectEquals (changed.updates.front().track.libraryId, known.libraryId);
                expectEquals (changed.updates.front().track.addedAtMs, known.addedAtMs);
                expectEquals (changed.updates.front().track.title,
                              juce::String ("Changed title with a different size"));
            }
        }

        beginTest ("same-filesystem moves across roots preserve IDs without reading unchanged metadata");
        {
            ScanFixture fixture;
            const auto firstRoot = fixture.root.getChildFile ("First");
            const auto secondRoot = fixture.root.getChildFile ("Second");
            expect (firstRoot.createDirectory());
            expect (secondRoot.createDirectory());
            const auto original = firstRoot.getChildFile ("original.wav");
            const auto moved = secondRoot.getChildFile ("moved.wav");
            expect (writeScanWav (original, "Stable recording"));
            MusicDirectoryScanService service;
            const std::vector<MusicDirectory> roots {
                { "first", firstRoot, true }, { "second", secondRoot, false }
            };
            const auto initial = scan (service, roots);
            expectEquals (static_cast<int> (initial.updates.size()), 1);
            if (! initial.updates.empty())
            {
                const auto known = initial.updates.front().track;
                expect (original.moveFileTo (moved));
                MusicDirectoryScanService restartedService;
                const auto result = scan (restartedService, roots, { known });
                expectEquals (result.metadataReads, 0);
                expectEquals (static_cast<int> (result.updates.size()), 1);
                expect (result.missingTrackIds.isEmpty());
                if (! result.updates.empty())
                {
                    expectEquals (result.updates.front().track.id, known.id);
                    expectEquals (result.updates.front().track.title, known.title);
                    expect (result.updates.front().track.file == moved);
                }
                expectEquals (static_cast<int> (result.roots.size()), 2);
                for (const auto& root : result.roots)
                    expect (root.status == MusicDirectoryScanRootReport::Status::complete);
            }
        }

        beginTest ("overlapping and repeated roots share metadata work and retain every root report");
        {
            ScanFixture fixture;
            const auto nested = fixture.root.getChildFile ("Album");
            expect (nested.createDirectory());
            expect (writeScanWav (nested.getChildFile ("song.wav"), "One song"));
            MusicDirectoryScanService service;
            const auto result = scan (service, {
                { "outer", fixture.root, true }, { "inner", nested, true },
                { "repeated", fixture.root, false }
            });
            expectEquals (result.discoveredFiles, 1);
            expectEquals (result.metadataReads, 1);
            expectEquals (static_cast<int> (result.updates.size()), 1);
            expectEquals (static_cast<int> (result.roots.size()), 3);
            for (const auto& root : result.roots)
            {
                expect (root.status == MusicDirectoryScanRootReport::Status::complete);
                expectEquals (root.discoveredFiles, 1);
            }
        }

        beginTest ("a moved file and a replacement at its old path remain separate library tracks");
        {
            ScanFixture fixture;
            const auto original = fixture.root.getChildFile ("a-original.wav");
            const auto moved = fixture.root.getChildFile ("z-moved.wav");
            expect (writeScanWav (original, "Original recording"));
            MusicDirectoryScanService service;
            const MusicDirectory directory { "music", fixture.root, true };
            const auto initial = scan (service, { directory });
            if (! initial.updates.empty())
            {
                const auto known = initial.updates.front().track;
                expect (original.moveFileTo (moved));
                expect (writeScanWav (original, "Replacement recording"));
                const auto result = scan (service, { directory }, { known });
                expectEquals (result.metadataReads, 1);
                expectEquals (static_cast<int> (result.updates.size()), 2);
                if (result.updates.size() == 2)
                {
                    expect (result.updates[0].track.file == original);
                    expect (result.updates[0].track.id != known.id);
                    expect (result.updates[1].track.file == moved);
                    expectEquals (result.updates[1].track.id, known.id);
                }
                expect (result.missingTrackIds.isEmpty());
            }
        }

        beginTest ("incomplete discovery cannot replace the identity of an unseen moved file");
        {
            ScanFixture fixture;
            const auto original = fixture.root.getChildFile ("Original.wav");
            const auto nested = fixture.root.getChildFile ("NotEnumerated");
            const auto moved = nested.getChildFile ("Moved.wav");
            expect (writeScanWav (original, "Original recording"));
            MusicDirectoryScanService service;
            const MusicDirectory directory { "music", fixture.root, true };
            const auto initial = scan (service, { directory });
            expectEquals (static_cast<int> (initial.updates.size()), 1);
            if (! initial.updates.empty())
            {
                const auto known = initial.updates.front().track;
                expect (nested.createDirectory());
                expect (original.moveFileTo (moved));
                expect (writeScanWav (original, "Replacement recording"));
                MusicDirectoryScanService limited ({}, { 100, 100, 0 });
                const auto partial = scan (limited, { directory }, { known });
                expect (partial.missingTrackIds.isEmpty());
                expect (partial.updates.empty(), "An incomplete traversal cannot assign the original ID to a replacement file.");
                expect (! partial.errors.isEmpty());
                const auto completed = scan (service, { directory }, { known });
                expectEquals (completed.metadataReads, 1);
                expectEquals (static_cast<int> (completed.updates.size()), 2);
                const auto found = std::find_if (completed.updates.begin(), completed.updates.end(),
                    [&] (const auto& update) { return update.track.id == known.id; });
                expect (found != completed.updates.end());
                if (found != completed.updates.end()) expect (found->track.file == moved);
            }
        }

        beginTest ("historical aliases retain exact paths and share a single metadata refresh");
        {
            ScanFixture fixture;
            const auto target = fixture.root.getChildFile ("a-target.wav");
            const auto alias = fixture.root.getChildFile ("b-alias.wav");
            expect (writeScanWav (target, "Shared recording"));
            MusicDirectoryScanService service;
            const MusicDirectory directory { "music", fixture.root, true };
            const auto initial = scan (service, { directory });
            expectEquals (static_cast<int> (initial.updates.size()), 1);
            if (! initial.updates.empty())
            {
                auto first = initial.updates.front().track;
                first.id = "original-record";
                auto second = first;
                second.id = "historical-alias";
                second.file = alias;
                expect (target.createSymbolicLink (alias, true));
                const auto unchanged = scan (service, { directory }, { second, first });
                expectEquals (unchanged.metadataReads, 0);
                expect (unchanged.updates.empty(), "Existing alias paths must not be relocated to the representative file.");
                expect (unchanged.missingTrackIds.isEmpty(), "Existing aliases must not be reported missing.");
                expect (target.setLastModificationTime (juce::Time (first.fileModifiedAtMs + 2000)));
                const auto refreshed = scan (service, { directory }, { second, first });
                expectEquals (refreshed.metadataReads, 1);
                expectEquals (static_cast<int> (refreshed.updates.size()), 2);
                expect (refreshed.missingTrackIds.isEmpty());
                for (const auto& update : refreshed.updates)
                {
                    expect (update.ok);
                    expectEquals (update.track.title, juce::String ("Shared recording"));
                    expect (update.track.file == (update.track.id == first.id ? target : alias));
                }
            }
        }

        beginTest ("missing tracks require a complete root and offline roots preserve availability");
        {
            ScanFixture fixture;
            const auto music = fixture.root.getChildFile ("Music");
            expect (music.createDirectory());
            const auto audio = music.getChildFile ("song.wav");
            expect (writeScanWav (audio, "Song"));
            MusicDirectoryScanService service;
            const MusicDirectory directory { "music", music, true };
            const auto initial = scan (service, { directory });
            if (! initial.updates.empty())
            {
                const auto known = initial.updates.front().track;
                expect (audio.deleteFile());
                const auto missing = scan (service, { directory }, { known });
                expectEquals (missing.missingTrackIds.size(), 1);
                expect (missing.missingTrackIds.contains (known.id));
                expect (missing.updates.empty());
                expect (music.deleteRecursively());
                const auto offline = scan (service, { directory }, { known });
                expect (offline.missingTrackIds.isEmpty());
                expectEquals (static_cast<int> (offline.roots.size()), 1);
                if (! offline.roots.empty())
                    expect (offline.roots.front().status == MusicDirectoryScanRootReport::Status::offline);
                expect (! offline.errors.isEmpty());
            }
        }

        beginTest ("truncated traversal cannot mark unseen tracks missing");
        {
            ScanFixture fixture;
            expect (writeScanWav (fixture.root.getChildFile ("one.wav"), "One"));
            expect (writeScanWav (fixture.root.getChildFile ("two.wav"), "Two"));
            TrackItem previouslyPresent (fixture.root.getChildFile ("removed.wav"), "Removed", 1.0);
            previouslyPresent.id = "retained-missing-track";
            MusicDirectoryScanService service ({}, { 1, 100, 10 });
            const auto result = scan (service, { { "music", fixture.root, true } }, { previouslyPresent });
            expect (result.missingTrackIds.isEmpty());
            expect (! result.errors.isEmpty());
            expectEquals (static_cast<int> (result.roots.size()), 1);
            if (! result.roots.empty())
                expect (result.roots.front().status == MusicDirectoryScanRootReport::Status::incomplete);
        }

        beginTest ("unreadable new files create no tracks and failed refreshes retain known metadata");
        {
            ScanFixture fixture;
            const auto audio = fixture.root.getChildFile ("known.wav");
            expect (writeScanWav (audio, "Remembered title"));
            const MusicDirectory directory { "music", fixture.root, true };
            MusicDirectoryScanService service;
            const auto initial = scan (service, { directory });
            if (! initial.updates.empty())
            {
                auto known = initial.updates.front().track;
                known.artworkCacheKey = "existing-cover";
                expect (audio.replaceWithText ("corrupt audio"));
                expect (fixture.root.getChildFile ("new.wav").replaceWithText ("also corrupt"));
                const auto result = scan (service, { directory }, { known });
                expectEquals (result.metadataReads, 2);
                expectEquals (static_cast<int> (result.updates.size()), 1);
                expect (result.missingTrackIds.isEmpty());
                if (! result.updates.empty())
                {
                    const auto& update = result.updates.front();
                    expect (! update.ok);
                    expect (! update.track.available);
                    expectEquals (update.track.id, known.id);
                    expectEquals (update.track.title, known.title);
                    expectEquals (update.track.artworkCacheKey, known.artworkCacheKey);
                    expect (update.error.isNotEmpty());
                }
            }
        }

        beginTest ("inaccessible subdirectories invalidate removal inference");
        {
            ScanFixture fixture;
            const auto denied = fixture.root.getChildFile ("Denied");
            expect (denied.createDirectory());
            TrackItem known (denied.getChildFile ("missing.wav"), "Previously present", 1.0);
            const auto nativePath = std::filesystem::path (denied.getFullPathName().toStdString());
            std::error_code error;
            const auto permissions = std::filesystem::status (nativePath, error).permissions();
            expect (! error);
            std::filesystem::permissions (nativePath, std::filesystem::perms::none, error);
            expect (! error);
            MusicDirectoryScanService service;
            const auto result = scan (service, { { "music", fixture.root, true } }, { known });
            std::filesystem::permissions (nativePath, permissions, error);
            expect (! error);
            expect (result.missingTrackIds.isEmpty());
            expect (! result.errors.isEmpty());
            if (! result.roots.empty())
                expect (result.roots.front().status == MusicDirectoryScanRootReport::Status::incomplete);
        }

        beginTest ("busy state includes queued completion and cancellation discards only newly prepared artwork");
        {
            ScanFixture fixture;
            const auto music = fixture.root.getChildFile ("Music");
            const auto cache = fixture.root.getChildFile ("Artwork");
            expect (music.createDirectory());
            expect (cache.createDirectory());
            expect (writeScanWav (music.getChildFile ("song.wav"), "Song"));
            expect (writeScanArtwork (music.getChildFile ("cover.png")));
            const auto existing = cache.getChildFile ("existing-key.png");
            expect (existing.replaceWithText ("existing artwork"));
            MusicDirectoryScanService service (cache);
            const std::vector<MusicDirectory> directories { { "music", music, true } };
            bool completed = false;
            expect (service.start (directories, {}, {}, [&] (MusicDirectoryScanResult result)
            {
                expect (service.isRunning());
                expect (! service.start (directories, {}, {}, [] (MusicDirectoryScanResult) {}));
                expect (result.cancelled);
                expect (result.updates.empty());
                expect (result.missingTrackIds.isEmpty());
                expect (result.preparedArtworkKeys.isEmpty());
                completed = true;
            }));
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
            auto stagedFiles = cache.findChildFiles (juce::File::findFiles, false, "*.png");
            while (stagedFiles.size() < 2 && juce::Time::getMillisecondCounterHiRes() < deadline)
            {
                juce::Thread::sleep (1);
                stagedFiles = cache.findChildFiles (juce::File::findFiles, false, "*.png");
            }
            // Assert the observed staging snapshot; the worker can rename its temporary image concurrently.
            expectEquals (stagedFiles.size(), 2);
            expect (! completed);
            expect (service.isRunning());
            service.cancel();
            expect (dispatchScanUntil ([&] { return completed; }));
            expect (! service.isRunning());
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 1);
            expectEquals (existing.loadFileAsString(), juce::String ("existing artwork"));

            auto accepted = scan (service, directories);
            expectEquals (static_cast<int> (accepted.updates.size()), 1);
            expectEquals (accepted.preparedArtworkKeys.size(), 1);
            MusicDirectoryScanService::discardPreparedArtwork (accepted, cache);
            expect (accepted.preparedArtworkKeys.isEmpty());
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 1);
        }

        beginTest ("destruction joins the worker and clears staged artwork before message dispatch");
        {
            ScanFixture fixture;
            const auto music = fixture.root.getChildFile ("Music");
            const auto cache = fixture.root.getChildFile ("Artwork");
            expect (music.createDirectory());
            expect (writeScanWav (music.getChildFile ("song.wav"), "Song"));
            expect (writeScanArtwork (music.getChildFile ("cover.png")));
            bool completed = false;
            {
                MusicDirectoryScanService service (cache);
                expect (service.start ({ { "music", music, true } }, {}, {},
                    [&] (MusicDirectoryScanResult result)
                    {
                        expect (result.cancelled);
                        expect (result.updates.empty());
                        completed = true;
                    }));
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
                while (cache.findChildFiles (juce::File::findFiles, false, "*.png").isEmpty()
                       && juce::Time::getMillisecondCounterHiRes() < deadline)
                    juce::Thread::sleep (1);
                expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 1);
            }
            expect (cache.findChildFiles (juce::File::findFiles, false, "*.png").isEmpty());
            expect (dispatchScanUntil ([&] { return completed; }));
        }
    }
};

MusicDirectoryScanServiceTests musicDirectoryScanServiceTests;
} // namespace
