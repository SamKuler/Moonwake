#include "app/PlayerController.h"
#include "library/LibraryProjection.h"
#include "state/StateSaveService.h"
#include "fixtures/LibraryAudioBackend.h"

#include <algorithm>
#include <cmath>

namespace
{
class PlayerLibraryTests final : public juce::UnitTest
{
public:
    PlayerLibraryTests() : UnitTest ("Player library", "app") {}

    void runTest() override
    {
        beginTest ("create library audio fixtures");
        const auto root = juce::File::getCurrentWorkingDirectory().getChildFile ("build/player-library-tests")
            .getNonexistentChildFile ("signals", {});
        expect (root.createDirectory());
        const auto write = [&] (const juce::String& name, float offset, int frames = 48000 * 4)
        {
            const auto file = root.getChildFile (name + ".wav");
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                .withSampleRate (48000).withNumChannels (2).withBitsPerSample (32)
                .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (writer != nullptr);
            if (writer)
            {
                juce::AudioBuffer<float> samples (2, frames);
                for (int channel = 0; channel < 2; ++channel)
                    for (int frame = 0; frame < samples.getNumSamples(); ++frame)
                        samples.setSample (channel, frame, offset + 0.03f * static_cast<float> (std::sin (frame * 0.037)));
                expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
            }
            return file;
        };
        const auto a = write ("A", 0.2f), b = write ("B", 0.4f), c = write ("C", 0.6f);
        TrackMetadataReader reader;
        const auto idFor = [] (const PlayerController& controller, const juce::File& file)
        {
            const auto* track = controller.getLibrary().findTrackByFile (file);
            return track != nullptr ? track->id : juce::String {};
        };
        const auto scanDirectory = [&] (const PlayerController& controller, const juce::File& folder)
        {
            MusicDirectoryScanService scanner;
            MusicDirectoryScanResult result;
            bool completed = false;
            expect (scanner.start ({ { "test-directory", folder, true } },
                controller.getLibrary().getTracks(), {}, [&] (MusicDirectoryScanResult prepared)
                {
                    result = std::move (prepared);
                    completed = true;
                }));
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
            while (! completed && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            expect (completed);
            expect (! result.cancelled);
            return result;
        };

        beginTest ("managed directories preserve records and incomplete or cancelled scans cannot remove them");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (a), reader.read (b) }, "all");
            const auto aid = idFor (controller, a);
            const auto saved = controller.createSavedPlaylist ("Retained songs");
            expect (controller.addTracksToSavedPlaylist (saved, { aid }));
            expect (controller.setTrackFavourites ({ aid }, true));
            const auto directory = controller.addMusicDirectory (root);
            expect (directory.isNotEmpty());
            expectEquals (controller.addMusicDirectory (root), directory);
            expectEquals (static_cast<int> (controller.getMusicDirectories().size()), 1);
            expect (controller.setMusicDirectoryAutomatic (directory, false));
            PlayerAppState snapshot;
            juce::String error;
            expect (controller.captureStateSnapshot (snapshot, error), error);
            expectEquals (static_cast<int> (snapshot.musicDirectories.size()), 1);
            if (! snapshot.musicDirectories.empty()) expect (! snapshot.musicDirectories.front().automatic);
            MusicDirectoryScanResult cancelled;
            cancelled.cancelled = true;
            cancelled.missingTrackIds.add (aid);
            expect (! controller.applyMusicDirectoryScan (std::move (cancelled)));
            expect (controller.getLibrary().getTrack (aid)->available);
            expect (controller.removeMusicDirectory (directory));
            expect (controller.getMusicDirectories().empty());
            expect (controller.getLibrary().isFavourite (aid));
            expectEquals (controller.getLibrary().getPlaylist (saved)->entries.front().trackId, aid);
            expectEquals (static_cast<int> (controller.getLibrary().getTracks().size()), 2);
        }

        beginTest ("scanned moves preserve references while a replacement at the old path receives a new identity");
        {
            const auto folder = root.getChildFile ("scan-move");
            expect (folder.createDirectory());
            const auto original = folder.getChildFile ("A.wav");
            const auto moved = folder.getChildFile ("Moved.wav");
            expect (a.copyFileTo (original));
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (original) }, "all");
            const auto id = idFor (controller, original);
            const auto addedAt = controller.getLibrary().getTrack (id)->addedAtMs;
            const auto saved = controller.createSavedPlaylist ("Moved recording");
            expect (controller.addTracksToSavedPlaylist (saved, { id, id }));
            expect (controller.setTrackFavourites ({ id }, true));
            expect (controller.enqueueLibraryTracks ({ id, id }, false));
            const auto queued = controller.getPlaylist().getTracks();
            expect (original.moveFileTo (moved));
            expect (b.copyFileTo (original));
            auto scanned = scanDirectory (controller, folder);
            expectEquals (scanned.metadataReads, 1);
            expectEquals (static_cast<int> (scanned.updates.size()), 2);
            expect (controller.applyMusicDirectoryScan (std::move (scanned)), controller.getLastStatusMessage());
            expectEquals (static_cast<int> (controller.getLibrary().getTracks().size()), 2);
            expectEquals (idFor (controller, moved), id);
            const auto replacementId = idFor (controller, original);
            expect (replacementId.isNotEmpty() && replacementId != id);
            expectEquals (controller.getLibrary().getTrack (id)->addedAtMs, addedAt);
            expect (controller.getLibrary().isFavourite (id));
            for (const auto& entry : controller.getLibrary().getPlaylist (saved)->entries)
                expectEquals (entry.trackId, id);
            expectEquals (controller.getPlaylist().size(), 2);
            for (int index = 0; index < controller.getPlaylist().size(); ++index)
            {
                const auto* occurrence = controller.getPlaylist().getTrack (index);
                expectEquals (occurrence->id, queued[static_cast<size_t> (index)].id);
                expectEquals (occurrence->libraryId, id);
                expect (occurrence->file == moved);
            }
        }

        beginTest ("scanned path swaps rebuild both indices together and preserve the loaded occurrence");
        {
            const auto folder = root.getChildFile ("scan-swap");
            expect (folder.createDirectory());
            const auto first = folder.getChildFile ("First.wav");
            const auto second = folder.getChildFile ("Second.wav");
            const auto temporary = folder.getChildFile ("Temporary.wav");
            expect (a.copyFileTo (first));
            expect (b.copyFileTo (second));
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (first), reader.read (second) }, "all");
            const auto firstId = idFor (controller, first), secondId = idFor (controller, second);
            const auto firstIdentity = controller.getLibrary().getTrack (firstId)->fileIdentity;
            const auto secondIdentity = controller.getLibrary().getTrack (secondId)->fileIdentity;
            const auto saved = controller.createSavedPlaylist ("Swapped paths");
            expect (controller.addTracksToSavedPlaylist (saved, { firstId, secondId }));
            expect (controller.setTrackFavourites ({ firstId }, true));
            expect (controller.playLibraryTracks ({ firstId, secondId }));
            const auto queued = controller.getPlaylist().getTracks();
            juce::AudioBuffer<float> output (2, 256);
            device->render (output);
            const auto position = controller.getCurrentPositionSeconds();
            const auto setups = device->setupChanges, attachments = device->callbackAttachments;
            expect (first.moveFileTo (temporary));
            expect (second.moveFileTo (first));
            expect (temporary.moveFileTo (second));
            auto scanned = scanDirectory (controller, folder);
            expectEquals (scanned.metadataReads, 0);
            expectEquals (static_cast<int> (scanned.updates.size()), 2);
            expect (controller.applyMusicDirectoryScan (std::move (scanned)), controller.getLastStatusMessage());
            expectEquals (idFor (controller, first), secondId);
            expectEquals (idFor (controller, second), firstId);
            expectEquals (controller.getLibrary().getTrack (firstId)->fileIdentity, firstIdentity);
            expectEquals (controller.getLibrary().getTrack (secondId)->fileIdentity, secondIdentity);
            expect (controller.getLibrary().isFavourite (firstId));
            const auto* playlist = controller.getLibrary().getPlaylist (saved);
            expectEquals (playlist->entries[0].trackId, firstId);
            expectEquals (playlist->entries[1].trackId, secondId);
            expectEquals (controller.getLoadedTrackId(), queued[0].id);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expectEquals (device->setupChanges, setups);
            expectEquals (device->callbackAttachments, attachments);
            expect (controller.isPlaying());
            device->render (output);
            expect (output.getSample (0, 255) > 0.1f && output.getSample (0, 255) < 0.3f);
        }

        beginTest ("cancelled scan application removes its staged cover and keeps library and queue ownership");
        {
            const auto artworkRoot = root.getChildFile ("cancelled-scan-artwork");
            ArtworkStore artwork (artworkRoot);
            PlayerController controller (std::make_unique<LibraryAudioBackend>(), {}, artworkRoot);
            juce::Image image (juce::Image::RGB, 2, 2, true);
            juce::MemoryOutputStream encoded;
            juce::PNGImageFormat png;
            expect (png.writeImageToStream (image, encoded));
            auto original = reader.read (a);
            original.artworkData = encoded.getMemoryBlock();
            controller.importPreparedTracks ({ original }, "all");
            const auto id = idFor (controller, a);
            const auto existingKey = controller.getLibrary().getTrack (id)->artworkCacheKey;
            expect (controller.enqueueLibraryTracks ({ id }, false));
            const auto occurrenceId = controller.getPlaylist().getTrack (0)->id;
            const auto revision = controller.getStateRevision();
            juce::String error;
            const auto stagedKey = artwork.store ("cancelled-scan", encoded.getMemoryBlock(), error);
            expect (stagedKey.isNotEmpty(), error);
            MusicDirectoryScanResult result;
            result.cancelled = true;
            auto candidate = reader.read (b);
            candidate.track.artworkCacheKey = stagedKey;
            result.updates.push_back (std::move (candidate));
            result.preparedArtworkKeys.add (stagedKey);
            result.missingTrackIds.add (id);
            expect (! controller.applyMusicDirectoryScan (std::move (result)));
            expectEquals (controller.getStateRevision(), revision);
            expectEquals (static_cast<int> (controller.getLibrary().getTracks().size()), 1);
            expect (controller.getLibrary().getTrack (id)->available);
            expectEquals (controller.getLibrary().getTrack (id)->artworkCacheKey, existingKey);
            expectEquals (controller.getPlaylist().size(), 1);
            expectEquals (controller.getPlaylist().getTrack (0)->id, occurrenceId);
            expect (artwork.isValid (existingKey));
            expect (! artwork.resolve (stagedKey).existsAsFile());
        }

        beginTest ("smart rule changes update results without replacing playback queue or position");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            SmartPlaylistRules rules;
            rules.rules.push_back ({ SmartPlaylistField::title, SmartPlaylistOperator::equals, "A" });
            const auto saved = controller.createSmartPlaylist ("First selection", rules);
            expect (saved.isNotEmpty());
            const auto firstResults = libraryTracks (controller.getLibrary(), controller.getPlaylist(), "playlist:" + saved);
            expectEquals (static_cast<int> (firstResults.size()), 1);
            if (! firstResults.empty()) expectEquals (firstResults.front().libraryId, aid);
            expect (controller.playLibraryTracks ({ aid, bid, cid }));
            juce::AudioBuffer<float> output (2, 256);
            device->render (output);
            const auto position = controller.getCurrentPositionSeconds();
            const auto queued = controller.getPlaylist().getTracks();
            const auto playback = controller.getPlaybackQueue();
            const auto loaded = controller.getLoadedTrackId();
            const auto setups = device->setupChanges, attachments = device->callbackAttachments;
            rules.rules.front().textValue = "B";
            expect (controller.updateSmartPlaylist (saved, "Second selection", rules));
            const auto updatedResults = libraryTracks (controller.getLibrary(), controller.getPlaylist(), "playlist:" + saved);
            expectEquals (static_cast<int> (updatedResults.size()), 1);
            if (! updatedResults.empty()) expectEquals (updatedResults.front().libraryId, bid);
            expectEquals (controller.getPlaylist().size(), static_cast<int> (queued.size()));
            const auto updatedPlayback = controller.getPlaybackQueue();
            expectEquals (static_cast<int> (updatedPlayback.size()), static_cast<int> (playback.size()));
            for (size_t index = 0; index < queued.size(); ++index)
                expectEquals (controller.getPlaylist().getTrack (static_cast<int> (index))->id, queued[index].id);
            for (size_t index = 0; index < std::min (playback.size(), updatedPlayback.size()); ++index)
                expectEquals (updatedPlayback[index].key, playback[index].key);
            expectEquals (controller.getLoadedTrackId(), loaded);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expectEquals (device->setupChanges, setups);
            expectEquals (device->callbackAttachments, attachments);
            expect (controller.isPlaying());
            device->render (output);
            expect (output.getSample (0, 255) > 0.1f && output.getSample (0, 255) < 0.3f);
        }

        beginTest ("unloaded shuffle preview preserves first-play history and protects newly current entries");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) });
            controller.setShuffleEnabled (true);
            const auto preview = controller.getPlaybackQueue();
            expect (controller.setPlaying (true));
            const auto loaded = controller.getPlaybackQueue();
            expectEquals (static_cast<int> (loaded.size()), static_cast<int> (preview.size()));
            for (size_t i = 0; i < std::min (loaded.size(), preview.size()); ++i)
                expectEquals (loaded[i].track.id, preview[i].track.id);
            expectEquals (static_cast<int> (std::count_if (loaded.begin(), loaded.end(), [] (const auto& row) { return row.current; })), 1);
            const auto first = controller.getLoadedTrackId();
            expect (controller.nextTrack());
            expect (controller.previousTrack());
            expectEquals (controller.getLoadedTrackId(), first);
            controller.setShuffleEnabled (false);
            expect (controller.playLibraryTracks ({ idFor (controller, a), idFor (controller, b), idFor (controller, c) }));
            const auto second = controller.getPlaylist().getTrack (1)->id;
            const auto third = controller.getPlaylist().getTrack (2)->id;
            juce::AudioBuffer<float> output (2, 256);
            for (int block = 0; block < 755; ++block)
            {
                device->render (output);
                if (block % 16 == 0) juce::Thread::sleep (1);
            }
            // UI has not polled the new boundary; the edit synchronizes inside its playback scope.
            expect (! controller.removeUpcomingQueueEntries ({ second }));
            expectEquals (controller.getLoadedTrackId(), second);
            expect (! controller.moveUpcomingQueueEntry (third, -1));
            expect (controller.isPlaying());
            expectEquals (controller.getPlaylist().size(), 3);
        }

        beginTest ("published shuffle order survives natural progress and manual Next");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            std::vector<TrackMetadataResult> imported;
            for (int i = 0; i < 7; ++i) imported.push_back (reader.read (write ("Queue-" + juce::String (i), 0.1f * static_cast<float> (i + 1), 4800)));
            controller.importPreparedTracks (std::move (imported), "all");
            std::vector<juce::String> ids;
            for (const auto& track : controller.getLibrary().getTracks()) ids.push_back (track.id);
            controller.setShuffleEnabled (true);
            expect (controller.playLibraryTracks (ids));
            const auto initial = controller.getPlaybackQueue();
            expectEquals (static_cast<int> (initial.size()), 7);
            expectEquals (static_cast<int> (std::count_if (initial.begin(), initial.end(), [] (const auto& row) { return row.current; })), 1);
            juce::AudioBuffer<float> output (2, 256);
            for (int block = 0; block < 21; ++block)
            {
                device->render (output);
                juce::Thread::sleep (1);
            }
            controller.synchronizePlayback();
            auto current = controller.getPlaybackQueue();
            expectEquals (static_cast<int> (current.size()), 6);
            if (current.size() == 6 && initial.size() == 7)
                for (size_t i = 0; i < current.size(); ++i)
                {
                    expectEquals (current[i].key, initial[i + 1].key);
                    expectEquals (current[i].track.id, initial[i + 1].track.id);
                }
            expect (controller.setPlaying (false));
            while (current.size() > 1)
            {
                const auto before = current;
                expect (controller.nextTrack());
                current = controller.getPlaybackQueue();
                expectEquals (static_cast<int> (current.size()), static_cast<int> (before.size()) - 1);
                if (current.size() + 1 != before.size()) break;
                for (size_t i = 0; i < current.size(); ++i)
                    expectEquals (current[i].track.id, before[i + 1].track.id);
            }
            expect (! controller.nextTrack());
            controller.setRepeatMode (RepeatMode::all);
            for (int i = 0; i < 16; ++i)
            {
                const auto before = controller.getPlaybackQueue();
                expect (before.size() > 1);
                expect (controller.nextTrack());
                const auto after = controller.getPlaybackQueue();
                expectEquals (static_cast<int> (std::count_if (after.begin(), after.end(), [] (const auto& row) { return row.current; })), 1);
                for (size_t j = 0; j + 1 < before.size() && j < after.size(); ++j)
                    expectEquals (after[j].track.id, before[j + 1].track.id);
            }
            controller.setRepeatMode (RepeatMode::one);
            const auto repeated = controller.getPlaybackQueue();
            expectEquals (static_cast<int> (repeated.size()), 1);
            expect (controller.nextTrack(), "Manual Next skips Repeat one.");
        }

        beginTest ("preparation failures invalidate the published queue before a transport refresh");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            const auto shortCurrent = write ("Before-corrupt", 0.2f, 4800);
            auto corrupt = reader.read (b);
            const auto broken = root.getChildFile ("corrupt-upcoming.wav");
            expect (broken.replaceWithText ("not an audio file"));
            corrupt.track.file = broken;
            controller.importPreparedTracks ({ reader.read (shortCurrent), corrupt, reader.read (c) }, "all");
            const auto aid = idFor (controller, shortCurrent), bid = idFor (controller, broken), cid = idFor (controller, c);
            expect (controller.playLibraryTracks ({ aid, bid, cid }));
            const auto before = controller.getPlaybackQueueVersion();
            juce::Thread::sleep (100);
            // Pause consumes preparation failures before the UI timer polls.
            expect (controller.setPlaying (false));
            const auto after = controller.getPlaybackQueueVersion();
            expect (before != after);
            const auto queue = controller.getPlaybackQueue();
            expect (std::none_of (queue.begin(), queue.end(), [&] (const auto& row) { return row.track.libraryId == bid; }));
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, aid);
        }

        beginTest ("visible upcoming neighbours move across unavailable source entries");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            auto missing = reader.read (root.getChildFile ("unavailable.wav"));
            const auto saved = controller.importPreparedPlaylist ("Gaps", { reader.read (a), reader.read (b), missing, reader.read (c) });
            std::vector<juce::String> ids;
            for (const auto& entry : controller.getLibrary().getPlaylist (saved)->entries) ids.push_back (entry.trackId);
            expect (controller.playLibraryTracks (ids));
            expect (controller.setPlaying (false));
            const auto before = controller.getPlaybackQueue();
            expectEquals (static_cast<int> (before.size()), 3);
            if (before.size() == 3)
            {
                expect (controller.moveUpcomingQueueEntry (before[1].track.id, 1));
                const auto after = controller.getPlaybackQueue();
                expectEquals (after[1].track.id, before[2].track.id);
                expectEquals (controller.getLoadedTrackId(), before[0].track.id);
            }
        }

        beginTest ("collection playback continues past a first unreadable selection");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            const auto removed = root.getChildFile ("removed-first.wav");
            expect (a.copyFileTo (removed));
            controller.importPreparedTracks ({ reader.read (removed), reader.read (b) }, "all");
            const auto first = idFor (controller, removed), second = idFor (controller, b);
            expect (removed.deleteFile());
            expect (controller.playLibraryTracks ({ first, second }));
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, second);
        }

        beginTest ("playlist covers persist, duplicate safely and reject corrupt replacements");
        {
            const auto artworkRoot = root.getChildFile ("playlist-covers");
            PlayerController controller (std::make_unique<LibraryAudioBackend>(), {}, artworkRoot);
            const auto id = controller.createSavedPlaylist ("Cover playlist");
            const auto imageFile = root.getChildFile ("cover.png");
            juce::Image image (juce::Image::RGB, 80, 50, true);
            image.clear (image.getBounds(), juce::Colours::coral);
            { auto stream = imageFile.createOutputStream(); juce::PNGImageFormat png; expect (png.writeImageToStream (image, *stream)); }
            const auto choose = [&] (const juce::File& file)
            {
                bool done = false, ok = false;
                controller.choosePlaylistArtwork (id, file, [&] (bool success, const auto&) { done = true; ok = success; });
                const auto end = juce::Time::getMillisecondCounterHiRes() + 5000;
                while (! done && juce::Time::getMillisecondCounterHiRes() < end)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                expect (done);
                return ok;
            };
            expect (choose (imageFile));
            const auto key = controller.getLibrary().getPlaylist (id)->artworkCacheKey;
            expect (key.isNotEmpty() && controller.resolveArtworkFile (key).existsAsFile());
            const auto copy = controller.duplicateSavedPlaylist (id);
            PlayerAppState state; juce::String error;
            expect (controller.captureStateSnapshot (state, error), error);
            const auto restored = AppStateStore::fromValueTree (AppStateStore::toValueTree (state));
            expectEquals (restored.library.getPlaylist (id)->artworkCacheKey, key);
            expect (imageFile.replaceWithText ("invalid image"));
            expect (! choose (imageFile));
            expectEquals (controller.getLibrary().getPlaylist (id)->artworkCacheKey, key);
            const auto stateFile = root.getChildFile ("saved-cover.xml");
            const auto pluginFile = root.getChildFile ("saved-cover-plugins.xml");
            PlayerAppState pending;
            expect (controller.captureStateSnapshot (pending, error));
            const auto retained = controller.retainArtworkForStateSave (pending);
            StateSaveService saver;
            bool saved = false;
            expect (saver.start (std::move (pending), stateFile, [&] (bool ok, juce::String message)
            {
                expect (ok, message);
                if (ok) controller.acknowledgeSavedArtwork (retained);
                saved = true;
            }));
            expect (controller.resetPlaylistArtwork (id));
            expect (controller.resolveArtworkFile (key).existsAsFile(), "A duplicate retains the shared cover.");
            expect (controller.deleteSavedPlaylist (copy));
            expect (controller.resolveArtworkFile (key).existsAsFile(), "The in-flight snapshot still owns its custom cover.");
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
            while (! saved && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            expect (saved);
            expect (controller.resolveArtworkFile (key).existsAsFile(), "The last durable state still owns its custom cover.");
            const auto blocked = root.getChildFile ("blocked-state-parent");
            expect (blocked.replaceWithText ("file"));
            expect (! controller.saveState (blocked.getChildFile ("state.xml"), pluginFile));
            expect (controller.resolveArtworkFile (key).existsAsFile(), "A failed save must not orphan the durable cover.");
            expect (controller.saveState (stateFile, pluginFile));
            expect (! controller.resolveArtworkFile (key).existsAsFile());
        }

        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                                   OutputLayout::sevenOneTwo, OutputLayout::nineOneSix })
        {
            beginTest ("library edits and scanned metadata preserve every audio sample / " + toString (layout));
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            auto referenceBackend = std::make_unique<LibraryAudioBackend>();
            auto* referenceDevice = referenceBackend.get();
            PlayerController controller (std::move (backend)), reference (std::move (referenceBackend));
            expect (controller.setOutputLayout (layout).isEmpty());
            expect (reference.setOutputLayout (layout).isEmpty());
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            reference.addFiles ({ a });
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            expect (controller.playLibraryTracks ({ aid }));
            expect (reference.setPlaying (true));
            const auto selected = controller.getPlaylist().getSelectedTrack()->id;
            const auto setupChanges = device->setupChanges, attachments = device->callbackAttachments;
            const auto effectsRevision = controller.getEffectChainRevision();
            const auto saved = controller.createSavedPlaylist ("Working playlist");
            expect (controller.addTracksToSavedPlaylist (saved, { cid, bid, bid }));
            juce::AudioBuffer<float> actual (getChannelCount (layout), 256), expected (getChannelCount (layout), 256);
            float maximumError = 0.0f;
            for (int block = 0; block < 32; ++block)
            {
                if (block == 8)
                {
                    const auto position = controller.getCurrentPositionSeconds();
                    controller.setLibraryBrowseState ("playlist:" + saved, "unrelated search");
                    expect (controller.renameSavedPlaylist (saved, "Edited playlist"));
                    expect (controller.sortCollectionEntries ("playlist:" + saved, "title"));
                    const auto entries = controller.getLibrary().getPlaylist (saved)->entries;
                    expect (controller.removeCollectionEntries ("playlist:" + saved, { entries.front().id }));
                    expect (controller.undoCollectionEdit ("playlist:" + saved));
                    expect (controller.setTrackFavourites ({ aid, bid }, true));
                    expect (controller.enqueueLibraryTracks ({ cid, bid }, true));
                    expect (controller.enqueueLibraryTracks ({ aid }, false));
                    expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
                    expectEquals (controller.getPlaylist().getSelectedTrack()->id, selected);
                    expectEquals (controller.getPlaylist().getTrack (1)->libraryId, cid);
                    expectEquals (controller.getPlaylist().getTrack (2)->libraryId, bid);
                    expectEquals (device->setupChanges, setupChanges);
                    expectEquals (device->callbackAttachments, attachments);
                    expectEquals (controller.getEffectChainRevision(), effectsRevision);
                    expect (controller.isPlaying());
                }
                if (block == 16)
                {
                    const auto position = controller.getCurrentPositionSeconds();
                    const auto queue = controller.getPlaylist().getTracks();
                    TrackMetadataResult refreshed;
                    refreshed.ok = true;
                    refreshed.track = *controller.getLibrary().getTrack (aid);
                    refreshed.track.title = "Scanned recording title";
                    MusicDirectoryScanResult scanned;
                    scanned.updates.push_back (std::move (refreshed));
                    expect (controller.applyMusicDirectoryScan (std::move (scanned)), controller.getLastStatusMessage());
                    expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
                    expectEquals (controller.getLoadedTrackId(), selected);
                    expectEquals (controller.getPlaylist().getSelectedTrack()->title,
                                  juce::String ("Scanned recording title"));
                    expectEquals (controller.getPlaylist().size(), static_cast<int> (queue.size()));
                    for (size_t index = 0; index < queue.size(); ++index)
                        expectEquals (controller.getPlaylist().getTrack (static_cast<int> (index))->id, queue[index].id);
                    expectEquals (device->setupChanges, setupChanges);
                    expectEquals (device->callbackAttachments, attachments);
                    expectEquals (controller.getEffectChainRevision(), effectsRevision);
                    expect (controller.isPlaying());
                }
                referenceDevice->render (expected);
                device->render (actual);
                for (int channel = 0; channel < actual.getNumChannels(); ++channel)
                    for (int frame = 0; frame < actual.getNumSamples(); ++frame)
                        maximumError = std::max (maximumError, std::abs (expected.getSample (channel, frame) - actual.getSample (channel, frame)));
                if (block % 8 == 0) juce::Thread::sleep (1);
            }
            expectWithinAbsoluteError (maximumError, 0.0f, 0.000001f);
        }

        beginTest ("duplicate queue entries remain independent and undo keeps advanced playback");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            expect (controller.playLibraryTracks ({ aid, bid, aid, cid }));
            const auto entries = controller.getPlaylist().getTracks();
            expect (entries[0].id != entries[2].id);
            expect (controller.removeCollectionEntries ("queue", { entries[2].id }));
            expectEquals (controller.getPlaylist().size(), 3);
            expect (controller.nextTrack());
            expectEquals (controller.getPlaylist().getSelectedTrack()->id, entries[1].id);
            juce::AudioBuffer<float> output (2, 256);
            device->render (output);
            const auto position = controller.getCurrentPositionSeconds();
            expect (controller.undoCollectionEdit ("queue"));
            expectEquals (controller.getPlaylist().size(), 4);
            expectEquals (controller.getPlaylist().getTrack (2)->id, entries[2].id);
            expectEquals (controller.getPlaylist().getSelectedTrack()->id, entries[1].id);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expect (controller.isPlaying());
            expect (controller.removeCollectionEntries ("queue", { entries[2].id }));
            expect (controller.enqueueLibraryTracks ({ aid }, false));
            expect (! controller.canUndoCollectionEdit ("queue"));
            const auto saved = controller.saveQueueAsPlaylist ("Queue copy");
            expectEquals (static_cast<int> (controller.getLibrary().getPlaylist (saved)->entries.size()), 4);
        }

        beginTest ("queue undo retains the audio boundary that advanced between UI polls");
        {
            const auto shortTrack = write ("Short", 0.2f, 4800);
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (shortTrack), reader.read (b), reader.read (c) }, "all");
            const auto shortId = idFor (controller, shortTrack), bid = idFor (controller, b), cid = idFor (controller, c);
            expect (controller.playLibraryTracks ({ shortId, bid, cid }));
            const auto third = controller.getPlaylist().getTrack (2)->id;
            expect (controller.removeCollectionEntries ("queue", { third }));
            juce::AudioBuffer<float> output (2, 256);
            for (int block = 0; block < 32; ++block)
            {
                device->render (output);
                if (block % 4 == 0) juce::Thread::sleep (1);
            }
            const auto position = controller.getCurrentPositionSeconds();
            expect (controller.undoCollectionEdit ("queue"));
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, bid);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expect (controller.isPlaying());
            expectEquals (controller.getPlaylist().getTrack (2)->id, third);
            expect (controller.sortCollectionEntries ("queue", "title", true));
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, bid);
            expect (controller.undoCollectionEdit ("queue"));
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
        }

        beginTest ("play next retains explicit order under shuffle and repeat preferences survive replacement");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            controller.setShuffleEnabled (true);
            controller.setRepeatMode (RepeatMode::all);
            expect (controller.playLibraryTracks ({ aid, bid }));
            expect (controller.getPlaylist().isShuffleEnabled());
            expect (controller.getPlaylist().getRepeatMode() == RepeatMode::all);
            expect (controller.enqueueLibraryTracks ({ cid, aid }, true));
            const auto plan = controller.getPlaylist().continuousPlaybackPlan();
            expectEquals (controller.getPlaylist().getTrack (controller.getPlaylist().indexOfId (plan.trackIds[1]))->libraryId, cid);
            expectEquals (controller.getPlaylist().getTrack (controller.getPlaylist().indexOfId (plan.trackIds[2]))->libraryId, aid);
            expect (controller.nextTrack());
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, cid);
            expect (controller.nextTrack());
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, aid);
        }

        for (const auto shuffle : { false, true })
        {
            beginTest (juce::String ("sorting and undoing pending play-next edits retain transport and playback order / ")
                       + (shuffle ? "shuffle" : "sequential"));
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            controller.setShuffleEnabled (shuffle);
            expect (controller.playLibraryTracks ({ aid }));
            expect (controller.enqueueLibraryTracks ({ cid, bid }, true));
            const auto original = controller.getPlaylist().getTracks();
            juce::AudioBuffer<float> output (2, 256);
            device->render (output);
            const auto position = controller.getCurrentPositionSeconds();
            expect (controller.sortCollectionEntries ("queue", "title"));
            const std::vector<juce::String> sorted { original[0].id, original[2].id, original[1].id };
            expect (controller.getPlaylist().continuousPlaybackPlan().trackIds == sorted);
            expect (controller.undoCollectionEdit ("queue"));
            const std::vector<juce::String> originalOrder { original[0].id, original[1].id, original[2].id };
            expect (controller.getPlaylist().continuousPlaybackPlan().trackIds == originalOrder);
            expect (controller.removeCollectionEntries ("queue", { original[1].id }));
            expect (controller.undoCollectionEdit ("queue"));
            expect (controller.getPlaylist().continuousPlaybackPlan().trackIds == originalOrder);
            expectEquals (controller.getLoadedTrackId(), original[0].id);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expect (controller.isPlaying());
            expect (controller.nextTrack());
            expectEquals (controller.getLoadedTrackId(), original[1].id);
            expect (controller.nextTrack());
            expectEquals (controller.getLoadedTrackId(), original[2].id);
            expect (! controller.nextTrack());
        }

        beginTest ("undo reinstates unplayed priorities without rescheduling consumed entries or rewinding playback");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            controller.setShuffleEnabled (true);
            expect (controller.playLibraryTracks ({ aid }));
            expect (controller.enqueueLibraryTracks ({ bid, cid }, true));
            const auto queued = controller.getPlaylist().getTracks();
            expect (controller.removeCollectionEntries ("queue", { queued[1].id }));
            expect (controller.nextTrack());
            expectEquals (controller.getLoadedTrackId(), queued[2].id);
            juce::AudioBuffer<float> output (2, 256);
            device->render (output);
            const auto position = controller.getCurrentPositionSeconds();
            expect (controller.undoCollectionEdit ("queue"));
            expectEquals (controller.getLoadedTrackId(), queued[2].id);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expect (controller.isPlaying());
            const auto pending = controller.getPlaylist().capturePendingNextTracks();
            expectEquals (static_cast<int> (pending.size()), 1);
            if (! pending.empty()) expectEquals (pending.front().id, queued[1].id);
            expect (controller.getPlaylist().continuousPlaybackPlan().trackIds
                    == std::vector<juce::String> { queued[2].id, queued[1].id });
            expect (controller.previousTrack());
            expectEquals (controller.getLoadedTrackId(), queued[0].id);
            expect (controller.nextTrack());
            expectEquals (controller.getLoadedTrackId(), queued[2].id);
            expect (controller.nextTrack());
            expectEquals (controller.getLoadedTrackId(), queued[1].id);
            expect (! controller.nextTrack());
        }

        for (const auto repeatedHistory : { false, true })
        {
            beginTest (juce::String ("undo preserves priority gaps inside actual forward history / ")
                       + (repeatedHistory ? "repeated occurrences" : "single cycle"));
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b), cid = idFor (controller, c);
            controller.setShuffleEnabled (true);
            if (repeatedHistory) controller.setRepeatMode (RepeatMode::all);
            expect (controller.playLibraryTracks ({ aid, cid }));
            expect (controller.nextTrack());
            if (repeatedHistory)
            {
                expect (controller.nextTrack());
                expect (controller.nextTrack());
                expect (controller.previousTrack());
            }
            expect (controller.enqueueLibraryTracks ({ bid }, true));
            const auto queued = controller.getPlaylist().getTracks();
            const auto pending = std::find_if (queued.begin(), queued.end(),
                [&] (const auto& track) { return track.libraryId == bid; })->id;
            expect (controller.previousTrack());
            const auto loaded = controller.getLoadedTrackId();
            const auto position = controller.getCurrentPositionSeconds();
            const auto expected = controller.getPlaylist().continuousPlaybackPlan();
            expect (controller.removeCollectionEntries ("queue", { pending }));
            expect (controller.undoCollectionEdit ("queue"));
            const auto restored = controller.getPlaylist().continuousPlaybackPlan();
            expect (restored.trackIds == expected.trackIds);
            expectEquals (restored.repeatFrom, expected.repeatFrom);
            expectEquals (controller.getLoadedTrackId(), loaded);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expect (controller.nextTrack());
            expectEquals (controller.getLoadedTrackId(), expected.trackIds[1]);
        }

        beginTest ("failed playlist reimports update availability while retaining recorded metadata in the same session");
        {
            const auto movedFile = root.getChildFile ("moved-during-session.wav");
            expect (a.copyFileTo (movedFile));
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            auto metadata = reader.read (movedFile);
            metadata.track.title = "Recorded title";
            metadata.track.artist = "Recorded artist";
            controller.importPreparedTracks ({ metadata }, "all");
            const auto id = idFor (controller, movedFile);
            expect (controller.enqueueLibraryTracks ({ id, id }, false));
            const auto occurrences = controller.getPlaylist().getTracks();
            expect (controller.setTrackFavourites ({ id }, true));
            expect (controller.getLibrary().getTrack (id)->available);
            expect (movedFile.deleteFile());
            auto missing = reader.read (movedFile);
            expect (! missing.ok);
            missing.track.title = "M3U fallback title";
            const auto imported = controller.importPreparedPlaylist ("Unavailable references", { missing, missing });
            expect (imported.isNotEmpty());
            const auto* retained = controller.getLibrary().getTrack (id);
            expect (! retained->available);
            expectEquals (retained->title, metadata.track.title);
            expectEquals (retained->artist, metadata.track.artist);
            expect (controller.getLibrary().isFavourite (id));
            for (size_t index = 0; index < occurrences.size(); ++index)
            {
                const auto* queued = controller.getPlaylist().getTrack (static_cast<int> (index));
                expect (! queued->available);
                expectEquals (queued->id, occurrences[index].id);
                expectEquals (queued->libraryId, id);
            }
            const auto* saved = controller.getLibrary().getPlaylist (imported);
            expectEquals (static_cast<int> (saved->entries.size()), 2);
            for (const auto& entry : saved->entries) expectEquals (entry.trackId, id);
        }

        beginTest ("captured import destinations survive browsing changes and deleted targets retain library records");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            const auto target = controller.createSavedPlaylist ("Target"), other = controller.createSavedPlaylist ("Other");
            controller.setLibraryBrowseState ("playlist:" + other, "B");
            controller.importPreparedTracks ({ reader.read (a), reader.read (a) }, "playlist:" + target);
            expectEquals (static_cast<int> (controller.getLibrary().getTracks().size()), 1);
            expectEquals (static_cast<int> (controller.getLibrary().getPlaylist (target)->entries.size()), 2);
            expect (controller.getLibrary().getPlaylist (other)->entries.empty());
            expect (controller.getPlaylist().isEmpty());
            const auto entries = controller.getLibrary().getPlaylist (target)->entries;
            expect (controller.removeCollectionEntries ("playlist:" + target, { entries[0].id }));
            controller.importPreparedTracks ({ reader.read (b) }, "playlist:" + target);
            expect (! controller.canUndoCollectionEdit ("playlist:" + target));
            expect (controller.deleteSavedPlaylist (target));
            const auto status = controller.importPreparedTracks ({ reader.read (c) }, "playlist:" + target);
            expect (status.containsIgnoreCase ("no longer exists"), status);
            expectEquals (static_cast<int> (controller.getLibrary().getTracks().size()), 3);
            expect (controller.getLibrary().getPlaylist (other)->entries.empty());
            expectEquals (controller.getLibraryCollectionId(), "playlist:" + other);
            expect (controller.getPlaylist().isEmpty());
        }

        beginTest ("playlist import preserves missing entries and duplicates, and snapshot copies library browsing state");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            auto missing = reader.read (root.getChildFile ("missing.wav"));
            missing.track.title = "Missing song";
            const auto saved = controller.importPreparedPlaylist ("Imported", { reader.read (a), missing, reader.read (a) });
            const auto entries = controller.getLibrary().getPlaylist (saved)->entries;
            expectEquals (static_cast<int> (entries.size()), 3);
            expectEquals (entries[0].trackId, entries[2].trackId);
            expect (entries[0].id != entries[2].id);
            expect (! controller.getLibrary().getTrack (entries[1].trackId)->available);
            controller.setTrackFavourites ({ entries[0].trackId }, true);
            controller.setLibraryBrowseState ("playlist:" + saved, juce::String::fromUTF8 ("歌曲"));
            expect (controller.playLibraryTracks ({ entries[0].trackId, entries[0].trackId }, 1));
            expect (controller.seekToSeconds (1.0));
            PlayerAppState snapshot;
            juce::String error;
            expect (controller.captureStateSnapshot (snapshot, error), error);
            expectEquals (snapshot.libraryCollectionId, "playlist:" + saved);
            expectEquals (snapshot.librarySearchQuery, juce::String::fromUTF8 ("歌曲"));
            expect (snapshot.library.isFavourite (entries[0].trackId));
            expect (snapshot.library.getPlaylist (saved)->entries == entries);
            PlayerController restored (std::make_unique<LibraryAudioBackend>());
            StateRestoreResult prepared;
            prepared.state = AppStateStore::fromValueTree (AppStateStore::toValueTree (snapshot));
            expect (restored.applyPreparedState (std::move (prepared)));
            expect (! restored.isPlaying());
            expectWithinAbsoluteError (restored.getCurrentPositionSeconds(), 1.0, 0.00001);
            expectEquals (restored.getLibraryCollectionId(), snapshot.libraryCollectionId);
            expectEquals (restored.getLibrarySearchQuery(), snapshot.librarySearchQuery);
            expectEquals (restored.getPlaylist().getSelectedTrack()->id, snapshot.playlist.getSelectedTrack()->id);
        }

        beginTest ("failed playlist reimports retain restored unavailable metadata and discard unused staged artwork");
        {
            const auto audioFile = root.getChildFile ("tagged-missing.wav");
            const auto artworkRoot = root.getChildFile ("retained-metadata-artwork");
            const auto stateFile = root.getChildFile ("retained-metadata-state.xml");
            expect (a.copyFileTo (audioFile));
            juce::Image image (juce::Image::RGB, 2, 2, true);
            image.clear (image.getBounds(), juce::Colours::blue);
            juce::MemoryOutputStream encoded;
            juce::PNGImageFormat png;
            expect (png.writeImageToStream (image, encoded));
            PlayerAppState snapshot;
            juce::String error, id, savedPlaylist;
            TrackItem known;
            {
                PlayerController original (std::make_unique<LibraryAudioBackend>(), PluginScannerConfiguration {}, artworkRoot);
                auto metadata = reader.read (audioFile);
                expect (metadata.ok, metadata.error);
                metadata.track.title = "Recorded title";
                metadata.track.artist = "Recorded artist";
                metadata.track.album = "Recorded album";
                metadata.track.albumArtist = "Recorded album artist";
                metadata.track.genre = "Recorded genre";
                metadata.track.year = 2024;
                metadata.track.trackNumber = 7;
                metadata.track.discNumber = 2;
                metadata.artworkData = encoded.getMemoryBlock();
                original.importPreparedTracks ({ metadata }, "all");
                id = idFor (original, audioFile);
                known = *original.getLibrary().getTrack (id);
                expect (known.artworkCacheKey.isNotEmpty());
                expect (original.enqueueLibraryTracks ({ id, id }, false));
                expect (original.setTrackFavourites ({ id }, true));
                savedPlaylist = original.saveQueueAsPlaylist ("Existing references");
                expect (original.captureStateSnapshot (snapshot, error), error);
            }
            expect (AppStateStore::writeAtomically (snapshot, stateFile, error), error);
            expect (audioFile.deleteFile());
            auto prepared = StateRestoreService::prepare (
                stateFile, root.getChildFile ("no-retained-metadata-plugins.xml"), artworkRoot);
            expect (prepared.state.has_value(), prepared.errorMessage);
            PlayerController restored (std::make_unique<LibraryAudioBackend>(), PluginScannerConfiguration {}, artworkRoot);
            expect (restored.applyPreparedState (std::move (prepared)));
            const auto* restoredTrack = restored.getLibrary().getTrack (id);
            expect (restoredTrack != nullptr);
            if (restoredTrack != nullptr)
            {
                expect (! restoredTrack->available);
                expectEquals (restoredTrack->artist, known.artist);
                ArtworkStore artwork (artworkRoot);
                const auto stagedKey = artwork.store (juce::Uuid().toString(), encoded.getMemoryBlock(), error);
                expect (stagedKey.isNotEmpty(), error);
                auto unresolved = reader.read (audioFile);
                expect (! unresolved.ok);
                unresolved.track.title = "Fallback playlist caption";
                unresolved.track.durationSeconds = 999.0;
                unresolved.track.artworkCacheKey = stagedKey;
                const auto imported = restored.importPreparedPlaylist ("Unresolved references", { unresolved, unresolved });
                expect (imported.isNotEmpty(), restored.getLastStatusMessage());
                const auto* retained = restored.getLibrary().getTrack (id);
                expect (retained != nullptr);
                if (retained != nullptr)
                {
                    expect (! retained->available);
                    expectEquals (retained->title, known.title);
                    expectEquals (retained->artist, known.artist);
                    expectEquals (retained->album, known.album);
                    expectEquals (retained->albumArtist, known.albumArtist);
                    expectEquals (retained->genre, known.genre);
                    expectEquals (retained->year, known.year);
                    expectEquals (retained->trackNumber, known.trackNumber);
                    expectEquals (retained->discNumber, known.discNumber);
                    expectWithinAbsoluteError (retained->durationSeconds, known.durationSeconds, 0.000001);
                    expectWithinAbsoluteError (retained->sampleRate, known.sampleRate, 0.000001);
                    expectEquals (retained->channelCount, known.channelCount);
                    expectEquals (retained->bitDepth, known.bitDepth);
                    expectEquals (retained->formatName, known.formatName);
                    expectEquals (retained->artworkCacheKey, known.artworkCacheKey);
                }
                const auto* importedPlaylist = restored.getLibrary().getPlaylist (imported);
                expect (importedPlaylist != nullptr);
                if (importedPlaylist != nullptr)
                {
                    expectEquals (static_cast<int> (importedPlaylist->entries.size()), 2);
                    for (const auto& entry : importedPlaylist->entries) expectEquals (entry.trackId, id);
                }
                expectEquals (static_cast<int> (restored.getLibrary().getTracks().size()), 1);
                expect (restored.getLibrary().isFavourite (id));
                expect (restored.getLibrary().getPlaylist (savedPlaylist)->entries == snapshot.library.getPlaylist (savedPlaylist)->entries);
                expectEquals (restored.getPlaylist().getSelectedTrack()->id, snapshot.playlist.getSelectedTrack()->id);
                expectEquals (restored.getPlaylist().size(), 2);
                for (int index = 0; index < restored.getPlaylist().size(); ++index)
                {
                    const auto* occurrence = restored.getPlaylist().getTrack (index);
                    expectEquals (occurrence->id, snapshot.playlist.getTrack (index)->id);
                    expectEquals (occurrence->artist, known.artist);
                    expectEquals (occurrence->artworkCacheKey, known.artworkCacheKey);
                }
                expect (artwork.resolve (known.artworkCacheKey).existsAsFile());
                expect (! artwork.resolve (stagedKey).existsAsFile());
                expect (a.copyFileTo (audioFile));
                auto refreshed = reader.read (audioFile);
                refreshed.track.artist = "Refreshed artist";
                restored.importPreparedTracks ({ refreshed }, "all");
                expect (restored.getLibrary().getTrack (id)->available);
                expectEquals (restored.getLibrary().getTrack (id)->artist, refreshed.track.artist);
                expectEquals (restored.getPlaylist().getTrack (0)->artist, refreshed.track.artist);
                expect (artwork.resolve (known.artworkCacheKey).existsAsFile());
            }
        }

        for (const auto importPlaylist : { false, true })
        {
            beginTest (juce::String ("restoring a queued file through ")
                       + (importPlaylist ? "playlist import" : "library import")
                       + " refreshes upcoming audio without restarting the current decoder");
            const auto shortTrack = write (importPlaylist ? "Playlist-recovery-short" : "Library-recovery-short", 0.2f, 4800);
            const auto recoveredFile = root.getChildFile (importPlaylist ? "playlist-recovered.wav" : "library-recovered.wav");
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            auto missing = reader.read (recoveredFile);
            expect (! missing.ok);
            const auto saved = controller.importPreparedPlaylist ("Waiting for file", { reader.read (shortTrack), missing });
            const auto entries = controller.getLibrary().getPlaylist (saved)->entries;
            const auto firstId = entries[0].trackId, recoveredId = entries[1].trackId;
            expect (controller.playLibraryTracks ({ firstId, recoveredId }));
            expect (! controller.getPlaylist().getTrack (1)->available);
            juce::AudioBuffer<float> output (2, 256);
            for (int block = 0; block < 4; ++block) device->render (output);
            const auto selected = controller.getPlaylist().getSelectedTrack()->id;
            const auto position = controller.getCurrentPositionSeconds();
            const auto setupChanges = device->setupChanges, attachments = device->callbackAttachments;
            expect (b.copyFileTo (recoveredFile));
            auto refreshed = reader.read (recoveredFile);
            expect (refreshed.ok, refreshed.error);
            if (importPlaylist)
                expect (controller.importPreparedPlaylist ("Recovered file", { refreshed }).isNotEmpty());
            else controller.importPreparedTracks ({ refreshed }, "all");
            expect (controller.getLibrary().getTrack (recoveredId)->available);
            expect (controller.getPlaylist().getTrack (1)->available);
            expectEquals (controller.getPlaylist().getSelectedTrack()->id, selected);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expectEquals (device->setupChanges, setupChanges);
            expectEquals (device->callbackAttachments, attachments);
            for (int block = 0; block < 40; ++block)
            {
                device->render (output);
                if (block % 4 == 0) juce::Thread::sleep (1);
            }
            controller.synchronizePlayback();
            expectEquals (controller.getPlaylist().getSelectedTrack()->libraryId, recoveredId);
            expect (controller.isPlaying());
            expect (controller.getCurrentPositionSeconds() > 0.0 && controller.getCurrentPositionSeconds() < 0.3);
            expect (output.getMagnitude (0, 0, output.getNumSamples()) > 0.25f,
                    "The restored second file must actually render after the first ends.");
        }

        beginTest ("availability changes cover a large duplicate queue without replacing occurrence identities");
        {
            const auto temporary = root.getChildFile ("many-availability.wav");
            expect (a.copyFileTo (temporary));
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (temporary) }, "all");
            const auto id = idFor (controller, temporary);
            const std::vector<juce::String> ids (4096, id);
            expect (controller.enqueueLibraryTracks (ids, false));
            const auto before = controller.getPlaylist().getTracks();
            expect (temporary.deleteFile());
            expect (! controller.setPlaying (true));
            expect (! controller.getLibrary().getTrack (id)->available);
            expectEquals (controller.getPlaylist().size(), static_cast<int> (before.size()));
            expect (std::none_of (controller.getPlaylist().getTracks().begin(), controller.getPlaylist().getTracks().end(),
                                 [] (const auto& occurrence) { return occurrence.available; }));
            expect (! controller.setPlaying (true));
            for (size_t index = 0; index < before.size(); ++index)
                expectEquals (controller.getPlaylist().getTrack (static_cast<int> (index))->id, before[index].id);
        }

        beginTest ("large saved-playlist edits validate the whole selection before changing membership");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (a) }, "all");
            const auto id = idFor (controller, a);
            const auto saved = controller.createSavedPlaylist ("Batch validation");
            expect (controller.addTracksToSavedPlaylist (saved, std::vector<juce::String> (4096, id)));
            const auto before = controller.getLibrary().getPlaylist (saved)->entries;
            std::vector<juce::String> selected;
            for (size_t index = 0; index < before.size(); index += 3) selected.push_back (before[index].id);
            auto invalid = selected;
            invalid.push_back ("unknown-entry");
            expect (! controller.moveCollectionEntries ("playlist:" + saved, invalid, static_cast<int> (before.size())));
            expect (controller.getLibrary().getPlaylist (saved)->entries == before);
            expect (! controller.removeCollectionEntries ("playlist:" + saved, invalid));
            expect (controller.getLibrary().getPlaylist (saved)->entries == before);
            expect (! controller.canUndoCollectionEdit ("playlist:" + saved));
            expect (controller.moveCollectionEntries ("playlist:" + saved, selected, static_cast<int> (before.size())));
            expect (controller.undoCollectionEdit ("playlist:" + saved));
            expect (controller.getLibrary().getPlaylist (saved)->entries == before);
            expect (controller.removeCollectionEntries ("playlist:" + saved, selected));
            expectEquals (static_cast<int> (controller.getLibrary().getPlaylist (saved)->entries.size()),
                          static_cast<int> (before.size() - selected.size()));
            expect (controller.undoCollectionEdit ("playlist:" + saved));
            expect (controller.getLibrary().getPlaylist (saved)->entries == before);
        }

        beginTest ("relocation updates every reference and undo metadata without replacing the loaded decoder");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.importPreparedTracks ({ reader.read (a), reader.read (b) }, "all");
            const auto aid = idFor (controller, a), bid = idFor (controller, b);
            const auto saved = controller.createSavedPlaylist ("Locations");
            controller.addTracksToSavedPlaylist (saved, { aid, aid });
            controller.setTrackFavourites ({ aid }, true);
            expect (controller.playLibraryTracks ({ aid, bid, aid }));
            const auto entries = controller.getPlaylist().getTracks();
            expect (controller.removeCollectionEntries ("queue", { entries[2].id }));
            juce::AudioBuffer<float> output (2, 256);
            device->render (output);
            const auto position = controller.getCurrentPositionSeconds();
            auto replacement = reader.read (c);
            replacement.track.title = "Relocated title";
            expect (controller.relocateLibraryTrack (aid, replacement));
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.000000001);
            expect (controller.undoCollectionEdit ("queue"));
            expectEquals (controller.getPlaylist().getTrack (2)->id, entries[2].id);
            expectEquals (controller.getPlaylist().getTrack (2)->title, juce::String ("Relocated title"));
            expect (controller.getPlaylist().getTrack (2)->file == c);
            expect (controller.getLibrary().isFavourite (aid));
            expectEquals (controller.getLibrary().getPlaylist (saved)->entries[0].trackId, aid);
            device->render (output);
            expect (output.getSample (0, 255) < 0.3f, "The current decoder must keep rendering A until explicit navigation.");
        }

        beginTest ("load failures and recovery persist canonical availability for every duplicate");
        {
            const auto temporary = root.getChildFile ("availability.wav");
            expect (a.copyFileTo (temporary));
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (temporary) }, "all");
            const auto id = idFor (controller, temporary);
            expect (controller.enqueueLibraryTracks ({ id, id }, false));
            expect (temporary.deleteFile());
            expect (! controller.setPlaying (true));
            expect (! controller.getLibrary().getTrack (id)->available);
            expect (! controller.getPlaylist().getTrack (0)->available);
            expect (! controller.getPlaylist().getTrack (1)->available);
            PlayerAppState failed;
            juce::String error;
            expect (controller.captureStateSnapshot (failed, error), error);
            expect (! AppStateStore::fromValueTree (AppStateStore::toValueTree (failed)).library.getTrack (id)->available);
            expect (a.copyFileTo (temporary));
            expect (controller.selectTrack (0));
            expect (controller.getLibrary().getTrack (id)->available);
            expect (controller.getPlaylist().getTrack (1)->available);
        }

        beginTest ("valid unchanged reorder and sort succeed without revisions or undo entries");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            controller.importPreparedTracks ({ reader.read (a), reader.read (b), reader.read (c) }, "all");
            const std::vector<juce::String> ids { idFor (controller, a), idFor (controller, b), idFor (controller, c) };
            expect (controller.enqueueLibraryTracks (ids, false));
            const auto saved = controller.createSavedPlaylist ("Already ordered");
            expect (controller.addTracksToSavedPlaylist (saved, ids));
            const auto empty = controller.createSavedPlaylist ("Empty ordering");
            const auto queueBefore = controller.getPlaylist().getTracks();
            const auto entriesBefore = controller.getLibrary().getPlaylist (saved)->entries;
            const auto updatedBefore = controller.getLibrary().getPlaylist (saved)->updatedAtMs;
            const auto revision = controller.getStateRevision();
            expect (controller.moveCollectionEntries ("queue", { queueBefore[1].id }, 1));
            expect (controller.moveCollectionEntries ("queue", { queueBefore[1].id }, 2));
            expect (controller.moveCollectionEntries ("playlist:" + saved, { entriesBefore[0].id, entriesBefore[1].id }, 1));
            expect (controller.sortCollectionEntries ("queue", "title"));
            expect (controller.sortCollectionEntries ("playlist:" + saved, "title"));
            expect (controller.sortCollectionEntries ("playlist:" + empty, "duration"));
            expectEquals (controller.getStateRevision(), revision);
            expect (! controller.canUndoCollectionEdit ("queue"));
            expect (! controller.canUndoCollectionEdit ("playlist:" + saved));
            expect (! controller.canUndoCollectionEdit ("playlist:" + empty));
            expectEquals (controller.getLibrary().getPlaylist (saved)->updatedAtMs, updatedBefore);
            expect (controller.getLibrary().getPlaylist (saved)->entries == entriesBefore);
            for (size_t index = 0; index < queueBefore.size(); ++index)
                expectEquals (controller.getPlaylist().getTrack (static_cast<int> (index))->id, queueBefore[index].id);
            expect (! controller.moveCollectionEntries ("queue", { queueBefore[0].id, "missing-entry" }, 0));
            expect (! controller.moveCollectionEntries ("queue", { queueBefore[0].id }, -1));
            expect (! controller.moveCollectionEntries ("queue", { queueBefore[0].id }, 4));
            expect (! controller.moveCollectionEntries ("playlist:" + saved, { "missing-entry" }, 0));
            expect (! controller.moveCollectionEntries ("playlist:" + saved, { entriesBefore[0].id }, -1));
            expect (! controller.moveCollectionEntries ("playlist:" + saved, { entriesBefore[0].id }, 4));
            expect (! controller.moveCollectionEntries ("playlist:missing", { entriesBefore[0].id }, 0));
            expect (! controller.sortCollectionEntries ("queue", "unsupported"));
            expect (! controller.sortCollectionEntries ("playlist:missing", "title"));
            expect (! controller.sortCollectionEntries ("all", "title"));
            expectEquals (controller.getStateRevision(), revision);
            expect (controller.sortCollectionEntries ("queue", "title", true));
            expect (controller.canUndoCollectionEdit ("queue"));
            const auto sortedRevision = controller.getStateRevision();
            expect (controller.sortCollectionEntries ("queue", "title", true));
            expectEquals (controller.getStateRevision(), sortedRevision);
            expect (controller.undoCollectionEdit ("queue"));
            expect (! controller.canUndoCollectionEdit ("queue"));
            for (size_t index = 0; index < queueBefore.size(); ++index)
                expectEquals (controller.getPlaylist().getTrack (static_cast<int> (index))->id, queueBefore[index].id);
        }

        beginTest ("raw metadata and relocation artwork replacements receive fresh cache identities");
        {
            const auto artworkRoot = root.getChildFile ("replacement-artwork");
            PlayerController controller (std::make_unique<LibraryAudioBackend>(), PluginScannerConfiguration {}, artworkRoot);
            ArtworkStore artwork (artworkRoot);
            const auto encodedImage = [&] (juce::Colour colour)
            {
                juce::Image image (juce::Image::RGB, 2, 2, true);
                image.clear (image.getBounds(), colour);
                juce::MemoryOutputStream encoded;
                juce::PNGImageFormat png;
                expect (png.writeImageToStream (image, encoded));
                return encoded.getMemoryBlock();
            };
            auto original = reader.read (a);
            original.artworkData = encodedImage (juce::Colours::red);
            controller.importPreparedTracks ({ original }, "all");
            const auto id = idFor (controller, a);
            expect (controller.enqueueLibraryTracks ({ id, id }, false));
            const auto queued = controller.getPlaylist().getTracks();
            const auto saved = controller.saveQueueAsPlaylist ("Artwork versions");
            const auto entries = controller.getLibrary().getPlaylist (saved)->entries;
            const auto firstKey = controller.getLibrary().getTrack (id)->artworkCacheKey;
            expect (firstKey.isNotEmpty() && artwork.resolve (firstKey).existsAsFile());
            auto refreshed = reader.read (a);
            refreshed.artworkData = encodedImage (juce::Colours::blue);
            controller.importPreparedTracks ({ refreshed }, "all");
            const auto secondKey = controller.getLibrary().getTrack (id)->artworkCacheKey;
            expect (secondKey.isNotEmpty() && secondKey != firstKey);
            expect (artwork.resolve (secondKey).existsAsFile());
            expect (! artwork.resolve (firstKey).existsAsFile());
            expectEquals (controller.getPlaylist().getTrack (0)->artworkCacheKey, secondKey);
            expectEquals (controller.getPlaylist().getTrack (1)->artworkCacheKey, secondKey);
            auto relocated = reader.read (b);
            relocated.artworkData = encodedImage (juce::Colours::green);
            expect (controller.relocateLibraryTrack (id, relocated));
            const auto thirdKey = controller.getLibrary().getTrack (id)->artworkCacheKey;
            expect (thirdKey.isNotEmpty() && thirdKey != secondKey && thirdKey != firstKey);
            expect (artwork.resolve (thirdKey).existsAsFile());
            expect (! artwork.resolve (secondKey).existsAsFile());
            expect (controller.getLibrary().getTrack (id)->file == b);
            expect (controller.getLibrary().getPlaylist (saved)->entries == entries);
            for (size_t index = 0; index < queued.size(); ++index)
            {
                const auto* occurrence = controller.getPlaylist().getTrack (static_cast<int> (index));
                expectEquals (occurrence->id, queued[index].id);
                expectEquals (occurrence->libraryId, id);
                expectEquals (occurrence->artworkCacheKey, thirdKey);
            }
        }

        beginTest ("queue removal keeps shared artwork and legacy imports still reject duplicate files");
        {
            const auto artworkRoot = root.getChildFile ("artwork");
            PlayerController controller (std::make_unique<LibraryAudioBackend>(), PluginScannerConfiguration {}, artworkRoot);
            auto metadata = reader.read (a);
            juce::Image image (juce::Image::RGB, 2, 2, true);
            image.clear (image.getBounds(), juce::Colours::red);
            juce::MemoryOutputStream encoded;
            juce::PNGImageFormat png;
            expect (png.writeImageToStream (image, encoded));
            metadata.artworkData = encoded.getMemoryBlock();
            controller.addPreparedTracks ({ metadata, reader.read (a) });
            expectEquals (controller.getPlaylist().size(), 1);
            const auto id = idFor (controller, a);
            const auto key = controller.getLibrary().getTrack (id)->artworkCacheKey;
            ArtworkStore artwork (artworkRoot);
            expect (key.isNotEmpty(), controller.getLastStatusMessage());
            expect (artwork.resolve (key).existsAsFile());
            const auto saved = controller.saveQueueAsPlaylist ("Artwork reference");
            controller.clearPlaylist();
            expect (controller.getPlaylist().isEmpty());
            expect (artwork.resolve (key).existsAsFile());
            expectEquals (controller.getLibrary().getTrack (id)->artworkCacheKey, key);
            expectEquals (controller.getLibrary().getPlaylist (saved)->entries[0].trackId, id);
            expect (controller.undoCollectionEdit ("queue"));
            expectEquals (controller.getPlaylist().getTrack (0)->artworkCacheKey, key);
            controller.addFiles ({ a });
            expectEquals (controller.getPlaylist().size(), 1);
            expect (artwork.resolve (key).existsAsFile());
            juce::String artworkError;
            const auto stagedKey = artwork.store (juce::Uuid().toString(), encoded.getMemoryBlock(), artworkError);
            expect (stagedKey.isNotEmpty(), artworkError);
            auto duplicate = reader.read (a);
            duplicate.track.artworkCacheKey = stagedKey;
            controller.addPreparedTracks ({ duplicate });
            expect (! artwork.resolve (stagedKey).existsAsFile());
            expect (artwork.resolve (key).existsAsFile());
            const auto replacementKey = artwork.store (juce::Uuid().toString(), encoded.getMemoryBlock(), artworkError);
            auto refreshed = reader.read (a);
            refreshed.track.artworkCacheKey = replacementKey;
            controller.importPreparedTracks ({ refreshed }, "all");
            expect (! artwork.resolve (key).existsAsFile());
            expect (artwork.resolve (replacementKey).existsAsFile());
            expectEquals (controller.getPlaylist().getTrack (0)->artworkCacheKey, replacementKey);
            expectEquals (controller.getLibrary().getTrack (id)->artworkCacheKey, replacementKey);
            expect (artwork.remove (replacementKey));
        }
        expect (root.deleteRecursively());
    }
};

PlayerLibraryTests playerLibraryTests;
}
