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

            expectEquals (static_cast<int> (tree.getProperty ("schemaVersion")), 7);
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
