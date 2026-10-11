#include "fixtures/FileAlias.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "library/LibraryImportService.h"

namespace
{
bool dispatchImportUntil (const std::function<bool()>& complete)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! complete() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return complete();
}

bool writeImportWav (const juce::File& file, const juce::String& title)
{
    auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (
        output, juce::AudioFormatWriterOptions{}
            .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16)
            .withMetadata (juce::WavAudioFormat::riffInfoTitle, title));
    if (writer == nullptr)
        return false;
    juce::AudioBuffer<float> samples (2, 480);
    samples.clear();
    return writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples());
}

bool writeImportArtwork (const juce::File& file)
{
    juce::Image image (juce::Image::RGB, 2, 2, true);
    juce::FileOutputStream output (file);
    juce::PNGImageFormat format;
    return output.openedOk() && format.writeImageToStream (image, output);
}

juce::File importFixtureDirectory()
{
    return juce::File (MOONWAKE_SOURCE_DIR).getChildFile ("build-library-import-tests")
        .getChildFile (juce::Uuid().toString());
}

class LibraryImportServiceTests final : public juce::UnitTest
{
public:
    LibraryImportServiceTests()
        : UnitTest ("LibraryImportService", "library") {}

    void runTest() override
    {
        beginTest ("imports audio through Unicode directory and file names");
        {
            const auto root = importFixtureDirectory();
            const auto music = root.getChildFile (juce::String::fromUTF8 (u8"中文音乐"));
            const auto album = music.getChildFile (juce::String::fromUTF8 (u8"专辑 🎵"));
            const auto audio = album.getChildFile (juce::String::fromUTF8 (u8"月光 夜曲.wav"));
            expect (album.createDirectory());
            expect (writeImportWav (audio, "Unicode path"));
            const auto discovered = LibraryImportService::collectAudioFiles ({ music });
            expectEquals (discovered.size(), 1);
            if (! discovered.isEmpty()) expect (discovered[0] == audio);
            LibraryImportService service;
            bool completed = false;
            expect (service.start ({ music }, {}, [&] (LibraryImportResult result)
            {
                expect (! result.cancelled);
                expect (result.skippedPaths.isEmpty());
                expectEquals (static_cast<int> (result.tracks.size()), 1);
                if (! result.tracks.empty())
                {
                    expect (result.tracks.front().track.file == audio);
                    expectEquals (result.tracks.front().track.title, juce::String ("Unicode path"));
                }
                completed = true;
            }));
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (root.deleteRecursively());
        }

        beginTest ("discovers supported files recursively and deduplicates inputs");
        {
            const auto root = importFixtureDirectory();
            const auto nested = root.getChildFile ("Album");
            expect (nested.createDirectory());
            const auto first = root.getChildFile ("b.WAV");
            const auto second = nested.getChildFile ("a.flac");
            expect (first.replaceWithText ("fixture"));
            expect (second.replaceWithText ("fixture"));
            expect (nested.getChildFile ("notes.txt").replaceWithText ("ignore"));

            juce::Array<juce::File> inputs { root, first, nested };
            const auto files = LibraryImportService::collectAudioFiles (inputs);
            expectEquals (files.size(), 2);
            expectEquals (files[0].getFileName(), juce::String ("a.flac"));
            expectEquals (files[1].getFileName(), juce::String ("b.WAV"));
            expect (root.deleteRecursively());
        }

        beginTest ("deduplicates direct file aliases by file identity");
        {
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            const auto target = root.getChildFile ("target.wav");
            const auto alias = root.getChildFile ("alias.wav");
            expect (target.replaceWithText ("fixture"));
            expect (createTestFileAlias (target, alias));
            const auto files = LibraryImportService::collectAudioFiles (
                { target, alias });
            expectEquals (files.size(), 1);
            expect (root.deleteRecursively());
        }

        beginTest ("stops discovery at configured safety limits");
        {
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            expect (root.getChildFile ("one.wav").replaceWithText ("fixture"));
            expect (root.getChildFile ("two.wav").replaceWithText ("fixture"));
            juce::StringArray skipped;
            const auto files = LibraryImportService::collectAudioFiles (
                { root }, nullptr, &skipped, { 1, 100, 10 });
            expectEquals (files.size(), 1);
            expect (skipped.joinIntoString (" ").containsIgnoreCase ("limit"));
            expect (root.deleteRecursively());
        }

        beginTest ("reports missing roots and honours cancellation");
        {
            const auto missing = importFixtureDirectory();
            juce::StringArray skipped;
            juce::Array<juce::File> inputs { missing };
            const auto files = LibraryImportService::collectAudioFiles (
                inputs, nullptr, &skipped);
            expect (files.isEmpty());
            expectEquals (skipped.size(), 1);

            std::atomic_bool cancelled { true };
            skipped.clear();
            expect (LibraryImportService::collectAudioFiles (
                inputs, &cancelled, &skipped).isEmpty());
            expect (skipped.isEmpty());
        }

        beginTest ("stays busy until its queued completion has been consumed");
        {
            const auto missing = importFixtureDirectory().getChildFile ("missing.wav");
            const juce::Array<juce::File> inputs { missing };
            LibraryImportService service;
            bool firstCompleted = false;
            bool busyDuringCompletion = false;
            bool overlapRejected = false;
            bool secondStarted = false;
            bool secondCompleted = false;

            expect (service.start (
                inputs, {},
                [&] (LibraryImportResult)
                {
                    firstCompleted = true;
                    busyDuringCompletion = service.isRunning();
                    overlapRejected = ! service.start (
                        inputs, {}, [] (LibraryImportResult) {});
                    juce::MessageManager::callAsync ([&]
                    {
                        secondStarted = service.start (
                            inputs, {},
                            [&] (LibraryImportResult)
                            {
                                secondCompleted = true;
                            });
                    });
                }));

            const auto deadline = juce::Time::getMillisecondCounterHiRes()
                                + 2000.0;
            while (! secondCompleted
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

            expect (firstCompleted);
            expect (busyDuringCompletion);
            expect (overlapRejected);
            expect (secondStarted);
            expect (secondCompleted);
        }

        beginTest ("playlist import preserves BOM Unicode order duplicates and unavailable local entries");
        {
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            const auto first = root.getChildFile (juce::String::fromUTF8 ("夜曲.wav"));
            const auto second = root.getChildFile ("a.wav");
            const auto alias = root.getChildFile ("alias.wav");
            const auto missing = root.getChildFile (juce::String::fromUTF8 ("缺失.wav"));
            const auto unreadable = root.getChildFile ("broken.wav");
            expect (writeImportWav (first, "Embedded first"));
            expect (writeImportWav (second, "Embedded second"));
            expect (createTestFileAlias (first, alias));
            expect (unreadable.replaceWithText ("not audio"));
            expect (writeImportArtwork (root.getChildFile ("cover.png")));
            const auto cache = root.getChildFile ("artwork");
            expect (cache.createDirectory());
            const auto existingCache = cache.getChildFile ("existing-library-track.png");
            expect (existingCache.replaceWithText ("existing cache"));
            const auto playlist = root.getChildFile (juce::String::fromUTF8 ("晚间歌单.m3u8"));
            const auto text = juce::String::fromUTF8 ("\xef\xbb\xbf")
                + "#EXTM3U\r\n" + first.getFileName() + "\r\n"
                + juce::String::fromUTF8 ("#EXTINF:12.5,缺失曲目\r\n") + missing.getFileName() + "\r\n"
                + second.getFileName() + "\r\n" + alias.getFileName() + "\r\n"
                + "#EXTINF:-1,Second missing occurrence\r\n" + missing.getFileName() + "\r\n"
                + first.getFileName() + "\r\nhttps://example.invalid/audio\r\n"
                + "#EXTINF:42,Unreadable recording\r\n" + unreadable.getFileName() + "\r\n";
            expect (playlist.replaceWithData (text.toRawUTF8(), text.getNumBytesAsUTF8()));

            LibraryImportService service (cache);
            LibraryImportResult result;
            bool completed = false;
            bool progressOnMessageThread = true;
            int total = 0;
            expect (service.startPlaylist (playlist, [&] (const LibraryImportProgress& progress)
            {
                progressOnMessageThread = progressOnMessageThread
                    && juce::MessageManager::getInstance()->isThisTheMessageThread();
                total = juce::jmax (total, progress.total);
            }, [&] (LibraryImportResult prepared)
            {
                expect (juce::MessageManager::getInstance()->isThisTheMessageThread());
                result = std::move (prepared);
                completed = true;
            }));
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (progressOnMessageThread);
            expectEquals (total, 7);
            expect (result.playlistImport);
            expect (! result.cancelled);
            expect (result.error.isEmpty(), result.error);
            expectEquals (result.playlistName, juce::String::fromUTF8 ("晚间歌单"));
            expectEquals (static_cast<int> (result.tracks.size()), 7);
            expectEquals (result.skippedPaths.size(), 2);
            expect (result.playlistWarnings.joinIntoString (" ").contains ("Unsupported URL"));
            expect (result.artworkWarnings.isEmpty());
            if (result.tracks.size() == 7)
            {
                const juce::Array<juce::File> expected { first, missing, second, alias, missing, first, unreadable };
                for (int index = 0; index < expected.size(); ++index)
                    expect (result.tracks[static_cast<size_t> (index)].track.file == expected[index]);
                for (const auto index : { 0, 2, 3, 5 })
                {
                    const auto& prepared = result.tracks[static_cast<size_t> (index)];
                    expect (prepared.ok);
                    expect (prepared.track.available);
                    expect (prepared.artworkData.isEmpty());
                    expect (ArtworkStore (cache).isValid (prepared.track.artworkCacheKey));
                }
                for (const auto index : { 1, 4, 6 })
                {
                    const auto& prepared = result.tracks[static_cast<size_t> (index)];
                    expect (! prepared.ok);
                    expect (! prepared.track.available);
                    expect (prepared.error.isNotEmpty());
                    expect (prepared.track.id.isNotEmpty());
                }
                expectEquals (result.tracks[0].track.id, result.tracks[3].track.id);
                expectEquals (result.tracks[0].track.id, result.tracks[5].track.id);
                expectEquals (result.tracks[1].track.id, result.tracks[4].track.id);
                expectEquals (result.tracks[0].track.title, juce::String ("Embedded first"));
                expectEquals (result.tracks[1].track.title, juce::String::fromUTF8 ("缺失曲目"));
                expectEquals (result.tracks[4].track.title, juce::String ("Second missing occurrence"));
                expectEquals (result.tracks[6].track.title, juce::String ("Unreadable recording"));
                expectWithinAbsoluteError (result.tracks[1].track.durationSeconds, 12.5, 0.000001);
                expectWithinAbsoluteError (result.tracks[4].track.durationSeconds, -1.0, 0.000001);
                expectWithinAbsoluteError (result.tracks[6].track.durationSeconds, 42.0, 0.000001);
            }
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 3);
            expectEquals (existingCache.loadFileAsString(), juce::String ("existing cache"));

            completed = false;
            expect (service.start ({ first, first, second, missing, unreadable }, {},
                [&] (LibraryImportResult prepared)
                {
                    result = std::move (prepared);
                    completed = true;
                }));
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (! result.playlistImport);
            expect (result.playlistName.isEmpty());
            expect (result.playlistWarnings.isEmpty());
            expect (result.error.isEmpty());
            expectEquals (static_cast<int> (result.tracks.size()), 2);
            if (result.tracks.size() == 2)
            {
                expect (result.tracks[0].track.file != result.tracks[1].track.file);
                for (const auto& prepared : result.tracks)
                    expect (prepared.track.file == first || prepared.track.file == second);
            }
            expect (root.deleteRecursively());
        }

        beginTest ("playlist parse failure reports an error without any partially prepared tracks");
        {
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            const auto audio = root.getChildFile ("valid.wav");
            expect (writeImportWav (audio, "Valid"));
            const auto playlist = root.getChildFile ("streaming.m3u8");
            expect (playlist.replaceWithText ("valid.wav\n#EXT-X-TARGETDURATION:10\nsegment.ts\n"));
            LibraryImportService service (root.getChildFile ("artwork"));
            const auto import = [&] (const juce::File& source)
            {
                LibraryImportResult result;
                bool completed = false;
                expect (service.startPlaylist (source, {}, [&] (LibraryImportResult prepared)
                {
                    result = std::move (prepared);
                    completed = true;
                }));
                expect (dispatchImportUntil ([&] { return completed; }));
                expect (result.playlistImport);
                expect (! result.cancelled);
                expect (result.tracks.empty());
                return result;
            };
            expect (import (playlist).error.contains ("streaming"));
            const unsigned char invalid[] { 0xff, 0xfe, 0x41, 0x00 };
            expect (playlist.replaceWithData (invalid, sizeof (invalid)));
            expect (import (playlist).error.contains ("UTF-8"));
            expect (import (root.getChildFile ("missing.m3u8")).error.isNotEmpty());
            expect (playlist.replaceWithText ("#EXTM3U\n"));
            expect (import (playlist).error.isEmpty());
            expect (! service.startPlaylist ({}, {}, [] (LibraryImportResult) {}));
            expect (! service.startPlaylist (playlist, {}, {}));
            expect (root.deleteRecursively());
        }

        beginTest ("playlist cancellation discards all entries and only its newly prepared artwork");
        {
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            const auto audio = root.getChildFile ("valid.wav");
            expect (writeImportWav (audio, "Valid"));
            expect (writeImportArtwork (root.getChildFile ("cover.png")));
            const auto cache = root.getChildFile ("artwork");
            expect (cache.createDirectory());
            const auto existingCache = cache.getChildFile ("active-library-track.png");
            expect (existingCache.replaceWithText ("active artwork"));
            const auto playlist = root.getChildFile ("cancel.m3u8");
            expect (playlist.replaceWithText (juce::String::repeatedString ("valid.wav\n", 5000)));
            LibraryImportService service (cache);
            bool completed = false;
            bool requestedCancellation = false;
            expect (service.startPlaylist (playlist, [&] (const LibraryImportProgress& progress)
            {
                if (progress.stage == LibraryImportProgress::Stage::reading
                    && progress.processed == progress.total - 1)
                {
                    expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 2);
                    requestedCancellation = true;
                    service.cancel();
                }
            }, [&] (LibraryImportResult prepared)
            {
                expect (prepared.cancelled);
                expect (prepared.playlistImport);
                expect (prepared.tracks.empty());
                completed = true;
            }));
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (requestedCancellation);
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 1);
            expectEquals (existingCache.loadFileAsString(), juce::String ("active artwork"));

            completed = false;
            expect (service.startPlaylist (playlist, {}, [&] (LibraryImportResult prepared)
            {
                expect (prepared.cancelled);
                expect (prepared.tracks.empty());
                completed = true;
            }));
            service.cancel();
            expect (dispatchImportUntil ([&] { return completed; }));
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 1);
            expect (root.deleteRecursively());
        }

        for (const auto folderImport : { false, true })
        {
            beginTest (juce::String (folderImport ? "folder" : "audio file")
                + " cancellation before queued delivery discards tracks and newly prepared artwork");
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            const auto audio = root.getChildFile ("valid.wav");
            expect (writeImportWav (audio, "Valid"));
            expect (writeImportArtwork (root.getChildFile ("cover.png")));
            const auto cache = root.getChildFile ("artwork");
            expect (cache.createDirectory());
            const auto existingCache = cache.getChildFile ("active-library-track.png");
            expect (existingCache.replaceWithText ("active artwork"));

            LibraryImportService service (cache);
            bool completed = false;
            bool requestedCancellation = false;
            LibraryImportResult result;
            expect (service.start ({ folderImport ? root : audio },
                [&] (const LibraryImportProgress& progress)
                {
                    if (progress.stage == LibraryImportProgress::Stage::reading
                        && progress.processed == progress.total - 1)
                    {
                        expect (! completed);
                        expect (service.isRunning());
                        expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 2);
                        requestedCancellation = true;
                        service.cancel();
                    }
                }, [&] (LibraryImportResult prepared)
                {
                    result = std::move (prepared);
                    completed = true;
                }));

            // Leave queued progress and completion callbacks undispatched while metadata and artwork finish.
            const auto preparationDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
            while (cache.findChildFiles (juce::File::findFiles, false, "*.png").size() < 2
                   && juce::Time::getMillisecondCounterHiRes() < preparationDeadline)
                juce::Thread::sleep (1);
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 2);
            juce::Thread::sleep (20);
            expect (! completed);
            expect (service.isRunning());
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (requestedCancellation);
            expect (result.cancelled);
            expect (! result.playlistImport);
            expect (result.tracks.empty());
            expect (! service.isRunning());
            expectEquals (cache.findChildFiles (juce::File::findFiles, false, "*.png").size(), 1);
            expectEquals (existingCache.loadFileAsString(), juce::String ("active artwork"));

            completed = false;
            expect (service.start ({ audio }, {}, [&] (LibraryImportResult prepared)
            {
                result = std::move (prepared);
                completed = true;
            }));
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (! result.cancelled);
            expectEquals (static_cast<int> (result.tracks.size()), 1);
            if (! result.tracks.empty())
                expect (ArtworkStore (cache).isValid (result.tracks.front().track.artworkCacheKey));
            expectEquals (existingCache.loadFileAsString(), juce::String ("active artwork"));
            expect (root.deleteRecursively());
        }

        beginTest ("destroyed playlist importer cannot publish prepared tracks after joining its worker");
        {
            const auto root = importFixtureDirectory();
            expect (root.createDirectory());
            const auto playlist = root.getChildFile ("cancel.m3u8");
            expect (playlist.replaceWithText ("missing.wav\n"));
            bool completed = false;
            {
                LibraryImportService service;
                expect (service.startPlaylist (playlist, {}, [&] (LibraryImportResult prepared)
                {
                    expect (prepared.cancelled);
                    expect (prepared.tracks.empty());
                    completed = true;
                }));
            }
            expect (dispatchImportUntil ([&] { return completed; }));
            expect (root.deleteRecursively());
        }
    }
};

LibraryImportServiceTests libraryImportServiceTests;
} // namespace
