#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "state/AppStateStore.h"

namespace
{
class AppStateStoreTests final : public juce::UnitTest
{
public:
    AppStateStoreTests()
        : UnitTest ("AppStateStore", "state")
    {
    }

    void runTest() override
    {
        beginTest ("schema 11 migration keeps queue occurrences and creates one referenced library");
        {
            juce::ValueTree root ("MoonwakeState");
            root.setProperty ("schemaVersion", 11, nullptr);
            root.setProperty ("playbackPositionSeconds", 14.5, nullptr);
            juce::ValueTree queue ("Playlist");
            queue.setProperty ("selectedTrackId", "second-occurrence", nullptr);
            queue.setProperty ("selectedIndex", 0, nullptr);
            queue.setProperty ("shuffle", true, nullptr);
            queue.setProperty ("repeat", "all", nullptr);
            for (const auto& id : { "first-occurrence", "second-occurrence", "missing-occurrence" })
            {
                juce::ValueTree track ("Track");
                track.setProperty ("id", id, nullptr);
                track.setProperty ("path", juce::String::fromUTF8 (juce::String (id) == "missing-occurrence"
                    ? "/missing/消失.wav" : "/music/重复.wav"), nullptr);
                track.setProperty ("title", juce::String::fromUTF8 ("月光"), nullptr);
                track.setProperty ("artist", juce::String::fromUTF8 ("演奏者"), nullptr);
                track.setProperty ("available", juce::String (id) != "missing-occurrence", nullptr);
                queue.appendChild (track, nullptr);
            }
            root.appendChild (queue, nullptr);
            const auto restored = AppStateStore::fromValueTree (root);
            expectEquals (restored.playlist.size(), 3);
            expectEquals (static_cast<int> (restored.library.getTracks().size()), 2);
            expectEquals (restored.playlist.getTrack (0)->id, juce::String ("first-occurrence"));
            expectEquals (restored.playlist.getSelectedTrack()->id, juce::String ("second-occurrence"));
            expectEquals (restored.playlist.getTrack (0)->libraryId, restored.playlist.getTrack (1)->libraryId);
            expect (! restored.playlist.getTrack (2)->available);
            expect (restored.playlist.isShuffleEnabled());
            expect (restored.playlist.getRepeatMode() == RepeatMode::all);
            expectWithinAbsoluteError (restored.playbackPositionSeconds, 14.5, 0.001);
            expectEquals (static_cast<int> (restored.library.getPlaylists().size()), 1);
            const auto& imported = restored.library.getPlaylists()[0];
            expectEquals (imported.name, juce::String ("Imported queue"));
            expectEquals (imported.createdAtMs, juce::int64 { 0 });
            expectEquals (imported.updatedAtMs, juce::int64 { 0 });
            expectEquals (static_cast<int> (imported.entries.size()), 3);
            expect (imported.entries[0].id != imported.entries[1].id);
            expectEquals (imported.entries[0].trackId, restored.playlist.getTrack (0)->libraryId);
            expectEquals (imported.entries[2].trackId, restored.playlist.getTrack (2)->libraryId);
            const auto roundTrip = AppStateStore::fromValueTree (AppStateStore::toValueTree (restored));
            expectEquals (static_cast<int> (roundTrip.library.getPlaylists().size()), 1);
            expectEquals (roundTrip.library.getPlaylists()[0].id, imported.id);
            expectEquals (roundTrip.library.getPlaylists()[0].createdAtMs, juce::int64 { 0 });
            expectEquals (roundTrip.library.getPlaylists()[0].updatedAtMs, juce::int64 { 0 });
        }

        beginTest ("schema 12 stores metadata once and restores Unicode playlists favourites and browsing");
        {
            PlayerAppState state;
            TrackItem track { juce::File (juce::String::fromUTF8 ("/missing/夜曲 🎵.flac")),
                              juce::String::fromUTF8 ("夜曲"), 42.0 };
            track.id = "library-track";
            track.artist = juce::String::fromUTF8 ("演奏者");
            track.album = juce::String::fromUTF8 ("月光");
            track.artworkCacheKey = "cover-reference";
            track.reservedEffectChainState = "reserved-state";
            track.available = false;
            const auto id = state.library.upsertTrack (track);
            const auto playlist = state.library.createPlaylist (juce::String::fromUTF8 ("晚间 🎶"));
            const auto entries = state.library.addToPlaylist (playlist, { id, id });
            expect (state.library.setFavourite (id, true));
            state.libraryCollectionId = "playlist:" + playlist;
            state.librarySearchQuery = juce::String::fromUTF8 ("月光 演奏者");
            for (const auto& occurrence : { "queue-a", "queue-b" })
            {
                auto queued = track;
                queued.id = occurrence;
                queued.libraryId = id;
                queued.title = "stale queue metadata";
                state.playlist.addTrack (queued);
            }
            state.playlist.selectTrack (1);
            const auto tree = AppStateStore::toValueTree (state);
            expectEquals (tree.getChildWithName ("MusicLibrary").getChildWithName ("Tracks").getNumChildren(), 1);
            expect (! tree.getChildWithName ("Playlist").getChild (0).hasProperty ("title"));
            const auto restored = AppStateStore::fromValueTree (juce::ValueTree::fromXml (*tree.createXml()));
            expectEquals (static_cast<int> (restored.library.getTracks().size()), 1);
            expect (restored.library.getPlaylist (playlist)->entries == entries);
            expect (restored.library.isFavourite (id));
            expectEquals (restored.libraryCollectionId, state.libraryCollectionId);
            expectEquals (restored.librarySearchQuery, state.librarySearchQuery);
            expectEquals (restored.playlist.getSelectedTrack()->id, juce::String ("queue-b"));
            expectEquals (restored.playlist.getTrack (0)->libraryId, id);
            expectEquals (restored.playlist.getTrack (0)->title, track.title);
            expectEquals (restored.playlist.getTrack (1)->reservedEffectChainState, track.reservedEffectChainState);
            expect (! restored.playlist.getTrack (0)->available);
            expect (restored.library.getTrack (id)->file == track.file);
        }

        beginTest ("playlist timestamps round-trip exact Unix milliseconds and unknown zeros through XML");
        {
            PlayerAppState state;
            TrackItem track { juce::File ("/music/timestamp.wav"), "Timestamp fixture", 12.0 };
            track.id = "timestamp-track";
            SavedPlaylist known;
            known.id = "known-playlist-time";
            known.name = "Known creation time";
            known.entries = { { "known-entry-one", track.id }, { "known-entry-two", track.id } };
            known.createdAtMs = 1704164645123LL;
            known.updatedAtMs = 1750000000789LL;
            SavedPlaylist unknown;
            unknown.id = "unknown-playlist-time";
            unknown.name = "Unknown creation time";
            expect (state.library.restore ({ track }, { known, unknown }, {}));
            const auto tree = AppStateStore::toValueTree (state);
            const auto saved = tree.getChildWithName ("MusicLibrary").getChildWithName ("SavedPlaylists");
            expectEquals (saved.getNumChildren(), 2);
            expectEquals (static_cast<juce::int64> (saved.getChild (0).getProperty ("createdAtMs")), known.createdAtMs);
            expectEquals (static_cast<juce::int64> (saved.getChild (0).getProperty ("updatedAtMs")), known.updatedAtMs);
            expectEquals (static_cast<juce::int64> (saved.getChild (1).getProperty ("createdAtMs")), juce::int64 { 0 });
            expectEquals (static_cast<juce::int64> (saved.getChild (1).getProperty ("updatedAtMs")), juce::int64 { 0 });
            const auto restored = AppStateStore::fromValueTree (juce::ValueTree::fromXml (*tree.createXml()));
            const auto* knownRestored = restored.library.getPlaylist (known.id);
            const auto* unknownRestored = restored.library.getPlaylist (unknown.id);
            expect (knownRestored != nullptr && unknownRestored != nullptr);
            if (knownRestored != nullptr && unknownRestored != nullptr)
            {
                expectEquals (knownRestored->createdAtMs, known.createdAtMs);
                expectEquals (knownRestored->updatedAtMs, known.updatedAtMs);
                expect (knownRestored->entries == known.entries);
                expectEquals (unknownRestored->createdAtMs, juce::int64 { 0 });
                expectEquals (unknownRestored->updatedAtMs, juce::int64 { 0 });
                const auto rewritten = AppStateStore::fromValueTree (AppStateStore::toValueTree (restored));
                expectEquals (rewritten.library.getPlaylist (known.id)->createdAtMs, known.createdAtMs);
                expectEquals (rewritten.library.getPlaylist (known.id)->updatedAtMs, known.updatedAtMs);
                expectEquals (rewritten.library.getPlaylist (unknown.id)->createdAtMs, juce::int64 { 0 });
            }
        }

        beginTest ("schema 12 playlists without timestamp properties stay unknown after restore and rewrite");
        {
            PlayerAppState state;
            const auto id = state.library.createPlaylist ("Existing playlist without dates");
            auto tree = AppStateStore::toValueTree (state);
            auto saved = tree.getChildWithName ("MusicLibrary").getChildWithName ("SavedPlaylists").getChild (0);
            saved.removeProperty ("createdAtMs", nullptr);
            saved.removeProperty ("updatedAtMs", nullptr);
            const auto restored = AppStateStore::fromValueTree (juce::ValueTree::fromXml (*tree.createXml()));
            const auto* playlist = restored.library.getPlaylist (id);
            expect (playlist != nullptr);
            if (playlist != nullptr)
            {
                expectEquals (playlist->createdAtMs, juce::int64 { 0 });
                expectEquals (playlist->updatedAtMs, juce::int64 { 0 });
                const auto rewritten = AppStateStore::toValueTree (restored).getChildWithName ("MusicLibrary")
                    .getChildWithName ("SavedPlaylists").getChild (0);
                expectEquals (static_cast<juce::int64> (rewritten.getProperty ("createdAtMs")), juce::int64 { 0 });
                expectEquals (static_cast<juce::int64> (rewritten.getProperty ("updatedAtMs")), juce::int64 { 0 });
            }
        }

        beginTest ("invalid library references preserve the saved file and the caller state");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                .getNonexistentChildFile ("state-library-protection", {});
            expect (directory.createDirectory());
            const auto file = directory.getChildFile ("state.xml");
            PlayerAppState saved;
            const auto id = saved.library.upsertTrack ({ juce::File ("/music/one.wav"), "One", 1.0 });
            const auto list = saved.library.createPlaylist ("Saved");
            saved.library.addToPlaylist (list, { id });
            auto invalid = AppStateStore::toValueTree (saved);
            invalid.getChildWithName ("MusicLibrary").getChildWithName ("SavedPlaylists")
                .getChild (0).getChild (0).setProperty ("trackId", "dangling", nullptr);
            for (const auto& version : { "12", "11", "invalid" })
            {
                invalid.setProperty ("schemaVersion", version, nullptr);
                expect (invalid.createXml()->writeTo (file));
                const auto original = file.loadFileAsString();
                PlayerAppState destination;
                destination.libraryCollectionId = "favourites";
                juce::String error;
                expect (! AppStateStore::readFromFile (file, destination, error));
                expect (error.containsIgnoreCase ("references"), error);
                expectEquals (destination.libraryCollectionId, juce::String ("favourites"));
                expect (! AppStateStore::writeAtomically (saved, file, error));
                expectEquals (file.loadFileAsString(), original);
            }
            expect (directory.deleteRecursively());
        }

        beginTest ("plug-in format preferences persist and older sessions enable both formats");
        {
            PlayerAppState state;
            for (const auto preferences : { PluginFormatPreferences { true, false },
                                           PluginFormatPreferences { false, true },
                                           PluginFormatPreferences { false, false } })
            {
                state.pluginFormats = preferences;
                expect (AppStateStore::fromValueTree (AppStateStore::toValueTree (state)).pluginFormats == preferences);
            }
            juce::ValueTree legacy ("MoonwakeState");
            legacy.setProperty ("schemaVersion", 9, nullptr);
            expect (AppStateStore::fromValueTree (legacy).pluginFormats == PluginFormatPreferences {});
        }
        beginTest ("round-trips playback, playlist, layout, and effect-chain state");
        {
            PlayerAppState state;
            TrackItem first { juce::File ("/music/first.wav"), "First", 12.5 };
            first.id = "first-id";
            first.artist = "Artist";
            first.album = "Album";
            first.albumArtist = "Album Artist";
            first.genre = "Ambient";
            first.year = 2026;
            first.trackNumber = 3;
            first.discNumber = 2;
            first.sampleRate = 48000.0;
            first.channelCount = 2;
            first.bitDepth = 24;
            first.formatName = "WAV";
            first.artworkCacheKey = "first-id";
            state.playlist.addTrack (std::move (first));

            TrackItem second { juce::File ("/music/second.wav"), "Second", 18.0 };
            second.id = "second-id";
            second.sampleRate = 96000.0;
            second.channelCount = 6;
            second.available = false;
            state.playlist.addTrack (std::move (second));
            state.playlist.selectTrack (1);
            state.playlist.setShuffleEnabled (true);
            state.playlist.setRepeatMode (RepeatMode::all);
            state.volume = 0.65;
            state.playbackPositionSeconds = 37.25;
            state.outputLayout = OutputLayout::fiveOneTwo;
            state.audioDevice = { "Aggregate Output", 96000.0, 128 };
            state.customVst3Directories = { "/plugins/Custom VST3", "/offline/Plug-ins" };
            state.audioDevice.outputRoutes.push_back ({ "device-uid", OutputLayout::stereo, { 7, -1 } });
            state.workspaceMode = WorkspaceMode::studio;
            state.activeSkinId = "com.example.skin";
            state.activeSkinVersion = "2.3.4";
            state.developerSkinDirectory = "/skins/development";

            const auto eqId = state.effectChain.addSlot ("EQ", "vst3:eq", "VST3");
            state.effectChain.addSlotWithId (91, "Delay", "AudioUnit", "au:delay");
            state.effectChain.setBypassed (eqId, true);
            state.effectChain.setAvailability (91, false, "Not installed");
            const uint8_t pluginBytes[] { 0, 1, 2, 127, 255 };
            state.effectChain.getSlotById (eqId)->serializedState.append (pluginBytes, sizeof (pluginBytes));

            const auto tree = AppStateStore::toValueTree (state);
            const auto restored = AppStateStore::fromValueTree (tree);

            expectEquals (static_cast<int> (tree.getProperty ("schemaVersion")), 12);
            expect (restored.customVst3Directories == state.customVst3Directories);
            expectEquals (static_cast<int> (restored.audioDevice.outputRoutes.size()), 1);
            if (! restored.audioDevice.outputRoutes.empty())
            {
                expectEquals (restored.audioDevice.outputRoutes[0].deviceIdentifier, juce::String ("device-uid"));
                expect (restored.audioDevice.outputRoutes[0].channels == std::vector<int> { 7, -1 });
            }
            expectEquals (restored.playlist.size(), 2);
            expectEquals (restored.playlist.getSelectedIndex(), 1);
            expectEquals (restored.playlist.getTrack (0)->file.getFullPathName(),
                          juce::String ("/music/first.wav"));
            expectEquals (restored.playlist.getTrack (1)->title, juce::String ("Second"));
            expectEquals (restored.playlist.getTrack (0)->id, juce::String ("first-id"));
            expectEquals (restored.playlist.getTrack (0)->artist, juce::String ("Artist"));
            expectEquals (restored.playlist.getTrack (0)->album, juce::String ("Album"));
            expectEquals (restored.playlist.getTrack (0)->albumArtist,
                          juce::String ("Album Artist"));
            expectEquals (restored.playlist.getTrack (0)->genre, juce::String ("Ambient"));
            expectEquals (restored.playlist.getTrack (0)->year, 2026);
            expectEquals (restored.playlist.getTrack (0)->trackNumber, 3);
            expectEquals (restored.playlist.getTrack (0)->discNumber, 2);
            expectWithinAbsoluteError (restored.playlist.getTrack (0)->sampleRate, 48000.0, 0.001);
            expectEquals (restored.playlist.getTrack (0)->channelCount, 2);
            expectEquals (restored.playlist.getTrack (0)->bitDepth, 24);
            expectEquals (restored.playlist.getTrack (0)->formatName, juce::String ("WAV"));
            expectEquals (restored.playlist.getTrack (0)->artworkCacheKey,
                          juce::String ("first-id"));
            expectEquals (restored.playlist.getTrack (1)->id, juce::String ("second-id"));
            expect (! restored.playlist.getTrack (1)->available);
            expect (restored.playlist.isShuffleEnabled());
            expect (restored.playlist.getRepeatMode() == RepeatMode::all);
            expectWithinAbsoluteError (restored.volume, 0.65, 0.0001);
            expectWithinAbsoluteError (restored.playbackPositionSeconds,
                                       37.25, 0.0001);
            expect (restored.outputLayout == OutputLayout::fiveOneTwo);
            expectEquals (restored.audioDevice.outputDeviceName,
                          juce::String ("Aggregate Output"));
            expectWithinAbsoluteError (restored.audioDevice.sampleRate, 96000.0, 0.01);
            expectEquals (restored.audioDevice.bufferSize, 128);
            expect (restored.workspaceMode == WorkspaceMode::studio);
            expectEquals (restored.activeSkinId, juce::String ("com.example.skin"));
            expectEquals (restored.activeSkinVersion, juce::String ("2.3.4"));
            expectEquals (restored.developerSkinDirectory,
                          juce::String ("/skins/development"));

            expectEquals (restored.effectChain.size(), 2);
            expectEquals (restored.effectChain.getSlot (0)->id, eqId);
            expectEquals (restored.effectChain.getSlot (0)->pluginIdentifier, juce::String ("vst3:eq"));
            expectEquals (restored.effectChain.getSlot (0)->pluginFormat, juce::String ("VST3"));
            expect (restored.effectChain.getSlot (0)->bypassed);
            expect (restored.effectChain.getSlot (0)->serializedState == state.effectChain.getSlot (0)->serializedState);
            expectEquals (restored.effectChain.getSlot (1)->id, 91);
            expectEquals (restored.effectChain.getSlot (1)->pluginName, juce::String ("Delay"));
            expect (! restored.effectChain.getSlot (1)->available);
            expectEquals (restored.effectChain.getSlot (1)->errorMessage, juce::String ("Not installed"));
        }

        beginTest ("channel mixes persist absent speakers and old states restore neutral gains");
        {
            PlayerAppState state;
            for (auto& mix : state.outputChannelMix)
            {
                if (mix.channel == juce::AudioChannelSet::left)
                    mix = { mix.channel, 0.5f, true, false };
                if (mix.channel == juce::AudioChannelSet::topSideLeft)
                    mix = { mix.channel, 2.0f, false, true };
            }
            auto tree = AppStateStore::toValueTree (state);
            const auto restored = AppStateStore::fromValueTree (juce::ValueTree::fromXml (*tree.createXml()));
            expect (restored.outputChannelMix == state.outputChannelMix);
            expect (restored.outputLayout == OutputLayout::stereo);
            auto mixTree = tree.getChildWithName ("OutputChannelMix");
            for (auto channel : mixTree)
                if (static_cast<int> (channel.getProperty ("type")) == static_cast<int> (juce::AudioChannelSet::left))
                    channel.setProperty ("gain", -1.0f, nullptr);
            auto unknown = juce::ValueTree ("Channel");
            unknown.setProperty ("type", 999, nullptr);
            unknown.setProperty ("solo", true, nullptr);
            mixTree.appendChild (unknown, nullptr);
            const auto sanitized = AppStateStore::fromValueTree (tree);
            expectEquals (sanitized.outputChannelMix.size(), defaultOutputChannelMix().size());
            for (const auto& mix : sanitized.outputChannelMix)
            {
                expect (isValidOutputChannelMix (mix));
                if (mix.channel == juce::AudioChannelSet::left)
                    expect (mix == OutputChannelMix { mix.channel });
            }
            tree.removeChild (mixTree, nullptr);
            tree.setProperty ("schemaVersion", 8, nullptr);
            expect (AppStateStore::fromValueTree (tree).outputChannelMix == defaultOutputChannelMix());
        }

        beginTest ("effect scopes round-trip stable channel identities and preserve invalid slots for diagnosis");
        {
            PlayerAppState state;
            state.outputLayout = OutputLayout::sevenOneTwo;
            const EffectScope pair { juce::AudioChannelSet::rightSurroundSide, juce::AudioChannelSet::leftSurroundSide };
            const auto pairId = state.effectChain.addSlot ("Stereo EQ", "stereo-eq", "VST3", pair);
            state.effectChain.addSlot ("Mono EQ", "mono-eq", "AudioUnit", { juce::AudioChannelSet::LFE });
            state.effectChain.addSlot ("Bus", "full-bus", "VST3");
            state.effectChain.setBypassed (pairId, true);
            state.effectChain.getSlotById (pairId)->serializedState.append ("state", 5);
            auto tree = AppStateStore::toValueTree (state);
            const auto xml = tree.createXml();
            const auto restored = AppStateStore::fromValueTree (juce::ValueTree::fromXml (*xml));
            expect (restored.effectChain.getSlot (0)->scope == pair);
            expect (restored.effectChain.getSlot (1)->scope == EffectScope { juce::AudioChannelSet::LFE });
            expect (restored.effectChain.getSlot (2)->scope.empty());
            expect (restored.effectChain.getSlot (0)->bypassed);
            expect (restored.effectChain.getSlot (0)->serializedState == state.effectChain.getSlot (0)->serializedState);
            auto slots = tree.getChildWithName ("EffectChain");
            auto scopeTree = slots.getChild (0).getChildWithName ("Scope");
            scopeTree.getChild (0).setProperty ("type", 999, nullptr);
            const auto invalid = AppStateStore::fromValueTree (tree);
            expectEquals (invalid.effectChain.size(), 3);
            expect (! invalid.effectChain.getSlot (0)->available);
            expect (invalid.effectChain.getSlot (0)->errorMessage.isNotEmpty());
            expectEquals (static_cast<int> (invalid.effectChain.getSlot (0)->scope[0]), 999);
            expect (invalid.effectChain.getSlot (0)->bypassed);
            tree.setProperty ("schemaVersion", 8, nullptr);
            for (auto slot : slots)
                slot.removeAllChildren (nullptr);
            const auto legacy = AppStateStore::fromValueTree (tree);
            for (const auto& slot : legacy.effectChain.getSlots())
                expect (slot.scope.empty(), "legacy effects retain full-bus processing");
        }

        beginTest ("restored shuffle cycles retain the selected track and visit every available alternative");
        {
            PlayerAppState state;
            for (int index = 0; index < 64; ++index)
            {
                TrackItem track { juce::File ("/music/restored-" + juce::String (index) + ".wav"),
                                  "Track " + juce::String (index), 30.0 };
                track.id = "restored-" + juce::String (index);
                track.available = index % 7 != 0;
                state.playlist.addTrack (std::move (track));
            }
            expect (state.playlist.selectTrack (17));
            state.playlist.setShuffleEnabled (true);
            state.playlist.setRepeatMode (RepeatMode::off);

            auto restored = AppStateStore::fromValueTree (AppStateStore::toValueTree (state));
            expectEquals (restored.playlist.getSelectedIndex(), 17);
            expectEquals (restored.playlist.getSelectedTrack()->id, juce::String ("restored-17"));
            expect (restored.playlist.isShuffleEnabled());
            expect (restored.playlist.getRepeatMode() == RepeatMode::off);

            juce::StringArray visited;
            while (restored.playlist.advanceToNextTrack())
            {
                const auto* track = restored.playlist.getSelectedTrack();
                expect (track != nullptr && track->available);
                if (track == nullptr) break;
                expect (track->id != "restored-17");
                expect (! visited.contains (track->id));
                visited.add (track->id);
                if (visited.size() > state.playlist.size()) break;
            }
            expectEquals (visited.size(), 53);
            expect (! restored.playlist.advanceToNextTrack());
        }

        beginTest ("loads schema 5 trees without IDs or stream metadata");
        {
            juce::ValueTree root ("MoonwakeState");
            root.setProperty ("schemaVersion", 5, nullptr);
            juce::ValueTree playlist ("Playlist");
            playlist.setProperty ("selectedIndex", 0, nullptr);
            juce::ValueTree track ("Track");
            track.setProperty ("path", "/music/legacy.wav", nullptr);
            track.setProperty ("title", "Legacy", nullptr);
            track.setProperty ("duration", 3.5, nullptr);
            playlist.appendChild (track, nullptr);
            root.appendChild (playlist, nullptr);

            const auto restored = AppStateStore::fromValueTree (root);
            expectEquals (restored.playlist.size(), 1);
            expectEquals (restored.playlist.getSelectedIndex(), 0);
            expect (restored.playlist.getTrack (0)->id.isNotEmpty());
            expectWithinAbsoluteError (restored.playlist.getTrack (0)->sampleRate, 0.0, 0.001);
            expectEquals (restored.playlist.getTrack (0)->channelCount, 0);
            expect (restored.playlist.getTrack (0)->artist.isEmpty());
            expectEquals (restored.playlist.getTrack (0)->year, 0);
            expectEquals (restored.playlist.getTrack (0)->bitDepth, 0);
            expect (restored.playlist.getTrack (0)->artworkCacheKey.isEmpty());
            expect (restored.workspaceMode == WorkspaceMode::listening);
            expectEquals (restored.activeSkinId, juce::String ("builtin.default"));
            expect (restored.activeSkinVersion.isEmpty());
            expect (restored.developerSkinDirectory.isEmpty());
            expectWithinAbsoluteError (restored.playbackPositionSeconds,
                                       0.0, 0.0001);
        }

        beginTest ("loads schema 6 trees without a playback checkpoint");
        {
            juce::ValueTree root ("MoonwakeState");
            root.setProperty ("schemaVersion", 6, nullptr);
            root.appendChild (juce::ValueTree ("Playlist"), nullptr);

            const auto restored = AppStateStore::fromValueTree (root);
            expectWithinAbsoluteError (restored.playbackPositionSeconds,
                                       0.0, 0.0001);
        }

        beginTest ("sanitizes invalid playback checkpoints");
        {
            juce::ValueTree root ("MoonwakeState");
            root.setProperty ("schemaVersion", 7, nullptr);
            root.setProperty ("playbackPositionSeconds", -12.0, nullptr);
            root.appendChild (juce::ValueTree ("Playlist"), nullptr);

            const auto restored = AppStateStore::fromValueTree (root);
            expectWithinAbsoluteError (restored.playbackPositionSeconds,
                                       0.0, 0.0001);
        }

        beginTest ("rejects state written by a newer schema");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("state-tests");
            expect (directory.createDirectory());
            const auto stateFile = directory.getChildFile (
                "future-schema.xml");
            expect (stateFile.replaceWithText (
                "<MoonwakeState schemaVersion=\"999\"><Playlist/>"
                "<EffectChain/></MoonwakeState>"));

            PlayerAppState restored;
            juce::String error;
            expect (! AppStateStore::readFromFile (
                stateFile, restored, error));
            expect (error.containsIgnoreCase ("newer"), error);
            expect (stateFile.deleteFile());
        }

        beginTest ("failed restores cannot be overwritten by a subsequent save");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                .getChildFile ("build").getNonexistentChildFile (
                    "moonwake-state-preservation", {});
            expect (directory.createDirectory());
            const auto stateFile = directory.getChildFile ("state.xml");
            const juce::StringArray unreadableStates {
                "<MoonwakeState schemaVersion=\"999\"><Playlist/>"
                "<EffectChain/></MoonwakeState>",
                "<MoonwakeState><broken>",
                "<OtherApplication/>"
            };
            for (const auto& contents : unreadableStates)
            {
                expect (stateFile.replaceWithText (contents));
                const auto original = stateFile.loadFileAsString();
                PlayerAppState restored;
                juce::String error;
                expect (! AppStateStore::readFromFile (stateFile, restored, error));
                expect (! AppStateStore::writeAtomically (restored, stateFile, error));
                expect (error.isNotEmpty());
                expectEquals (stateFile.loadFileAsString(), original);
            }
            expect (directory.deleteRecursively());
        }

        beginTest ("writes and reads state atomically");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("state-tests");
            expect (directory.createDirectory());
            const auto stateFile = directory.getChildFile ("state.xml");
            static_cast<void> (stateFile.deleteFile());

            PlayerAppState state;
            TrackItem track { juce::File ("/music/atomic.wav"), "Atomic", 5.0 };
            track.id = "atomic-id";
            state.playlist.addTrack (std::move (track));

            juce::String error;
            expect (AppStateStore::writeAtomically (state, stateFile, error), error);
            expect (stateFile.existsAsFile());

            PlayerAppState restored;
            error.clear();
            expect (AppStateStore::readFromFile (stateFile, restored, error), error);
            expectEquals (restored.playlist.getTrack (0)->id, juce::String ("atomic-id"));
            state.volume = 0.25;
            expect (AppStateStore::writeAtomically (state, stateFile, error), error);
            expect (AppStateStore::readFromFile (stateFile, restored, error), error);
            expectWithinAbsoluteError (restored.volume, 0.25, 0.0001);
            expect (stateFile.deleteFile());
        }

        beginTest ("malformed XML returns an actionable error");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("state-tests");
            expect (directory.createDirectory());
            const auto stateFile = directory.getChildFile ("malformed.xml");
            expect (stateFile.replaceWithText ("<MoonwakeState><broken>"));

            PlayerAppState restored;
            juce::String error;
            expect (! AppStateStore::readFromFile (stateFile, restored, error));
            expect (error.isNotEmpty());
            expect (stateFile.deleteFile());
        }
    }
};

AppStateStoreTests appStateStoreTests;
} // namespace
