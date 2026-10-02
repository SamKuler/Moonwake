#include <juce_core/juce_core.h>

#include "playlist/PlaylistModel.h"

#include <algorithm>

namespace
{
PlaylistModel navigationPlaylist()
{
    PlaylistModel playlist;
    for (const auto* id : { "a", "b", "c", "d" })
    {
        TrackItem track { juce::File ("/music").getChildFile (
                             juce::String (id) + ".wav"), id, 1.0 };
        track.id = id;
        playlist.addTrack (std::move (track));
    }
    return playlist;
}

class PlaylistModelTests final : public juce::UnitTest
{
public:
    PlaylistModelTests()
        : UnitTest ("PlaylistModel", "playlist")
    {
    }

    void runTest() override
    {
        beginTest ("adds tracks and selects the first track");
        {
            PlaylistModel playlist;

            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });

            expectEquals (playlist.size(), 2);
            expectEquals (playlist.getSelectedIndex(), 0);
            expectEquals (playlist.getSelectedTrack()->title, juce::String ("First"));
        }

        beginTest ("selects tracks by valid index");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });

            expect (playlist.selectTrack (1));
            expectEquals (playlist.getSelectedIndex(), 1);
            expectEquals (playlist.getSelectedTrack()->title, juce::String ("Second"));
            expect (! playlist.selectTrack (4));
            expectEquals (playlist.getSelectedIndex(), 1);
        }

        beginTest ("removing selected tracks keeps selection valid");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.addTrack ({ juce::File ("/music/third.wav"), "Third", 21.0 });
            playlist.selectTrack (1);

            expect (playlist.removeTrack (1));
            expectEquals (playlist.size(), 2);
            expectEquals (playlist.getSelectedIndex(), 1);
            expectEquals (playlist.getSelectedTrack()->title, juce::String ("Third"));

            expect (playlist.removeTrack (1));
            expectEquals (playlist.getSelectedIndex(), 0);
            expectEquals (playlist.getSelectedTrack()->title, juce::String ("First"));

            expect (playlist.removeTrack (0));
            expectEquals (playlist.size(), 0);
            expectEquals (playlist.getSelectedIndex(), -1);
            expect (playlist.getSelectedTrack() == nullptr);
        }

        beginTest ("advances normally until the end when repeat is off");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });

            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), 1);
            expect (! playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), 1);
        }

        beginTest ("repeat all wraps to the first track");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.selectTrack (1);
            playlist.setRepeatMode (RepeatMode::all);

            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), 0);
        }

        beginTest ("repeat one keeps the selected track");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.selectTrack (1);
            playlist.setRepeatMode (RepeatMode::one);

            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), 1);
        }

        beginTest ("shuffle avoids immediate repeats when possible");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.addTrack ({ juce::File ("/music/third.wav"), "Third", 21.0 });
            playlist.setShuffleSeed (1234);
            playlist.setShuffleEnabled (true);

            const auto before = playlist.getSelectedIndex();
            expect (playlist.advanceToNextTrack());
            expect (playlist.getSelectedIndex() != before);
        }

        beginTest ("shuffle with repeat off visits one cycle and then stops");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.addTrack ({ juce::File ("/music/third.wav"), "Third", 21.0 });
            playlist.setShuffleSeed (1234);
            playlist.setShuffleEnabled (true);

            juce::StringArray visited { playlist.getSelectedTrack()->id };
            expect (playlist.advanceToNextTrack());
            visited.addIfNotAlreadyThere (playlist.getSelectedTrack()->id);
            expect (playlist.advanceToNextTrack());
            visited.addIfNotAlreadyThere (playlist.getSelectedTrack()->id);
            expectEquals (visited.size(), 3);
            const auto finalId = playlist.getSelectedTrack()->id;
            expect (! playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, finalId);
        }

        beginTest ("shuffle repeat all begins a new cycle while a single repeat-off track stops");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.setShuffleEnabled (true);
            playlist.setRepeatMode (RepeatMode::all);
            const auto firstId = playlist.getSelectedTrack()->id;
            expect (playlist.advanceToNextTrack());
            expect (playlist.getSelectedTrack()->id != firstId);
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, firstId);

            PlaylistModel single;
            single.addTrack ({ juce::File ("/music/only.wav"), "Only", 8.0 });
            single.setShuffleEnabled (true);
            expect (! single.advanceToNextTrack());
        }

        beginTest ("manual next ignores repeat one while natural completion repeats");
        {
            auto playlist = navigationPlaylist();
            playlist.setRepeatMode (RepeatMode::one);
            expect (playlist.selectTrack (0));
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (playlist.selectTrack (3));
            expect (! playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
        }

        beginTest ("shuffle previous and next follow committed playback history");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleSeed (27);
            playlist.setShuffleEnabled (true);
            expect (playlist.selectTrack (0));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            const auto second = playlist.getSelectedTrack()->id;
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            const auto third = playlist.getSelectedTrack()->id;
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, second);
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, second);
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, third);
        }

        beginTest ("sequential previous follows queue order after an explicit selection");
        {
            auto playlist = navigationPlaylist();
            expect (playlist.selectTrack (2));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            expect (playlist.setTrackAvailable ("b", false));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            playlist.setRepeatMode (RepeatMode::all);
            expect (! playlist.advanceToPreviousTrack());
        }

        beginTest ("manual selection replaces forward history and stable IDs survive edits");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            expect (playlist.selectTrack (0));
            expect (playlist.recordPlayback ("b"));
            expect (playlist.recordPlayback ("c"));
            expect (playlist.advanceToPreviousTrack());
            expect (playlist.selectTrack (3));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (playlist.moveTrack (playlist.indexOfId ("d"), 0));
            expect (playlist.removeTrack (playlist.indexOfId ("a")));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("d"));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (! playlist.advanceToPreviousTrack());
        }

        beginTest ("candidate inspection and preload copies leave navigation unchanged");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleSeed (321);
            playlist.setShuffleEnabled (true);
            playlist.setRepeatMode (RepeatMode::all);
            expect (playlist.selectTrack (0));
            const auto candidates = playlist.navigationCandidates (
                PlaylistNavigation::naturalCompletion);
            expect (candidates == playlist.navigationCandidates (
                PlaylistNavigation::naturalCompletion));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            expect (! playlist.advanceToPreviousTrack());

            auto plan = playlist;
            std::vector<juce::String> sequence;
            for (int transition = 0; transition < 12; ++transition)
            {
                expect (plan.advanceToNextTrack());
                sequence.push_back (plan.getSelectedTrack()->id);
            }
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            for (const auto& id : sequence)
            {
                const auto next = playlist.navigationCandidates (
                    PlaylistNavigation::naturalCompletion);
                expect (! next.empty());
                if (! next.empty()) expectEquals (next.front(), id);
                expect (playlist.commitPlayback (id));
                expectEquals (playlist.getSelectedTrack()->id, id);
            }
            expect (playlist.navigationCandidates (PlaylistNavigation::naturalCompletion)
                    == plan.navigationCandidates (PlaylistNavigation::naturalCompletion));
        }

        beginTest ("continuous plans preserve repeated forward-history occurrences");
        {
            for (const auto mode : { RepeatMode::off, RepeatMode::all })
            {
                auto playlist = navigationPlaylist();
                playlist.setShuffleSeed (27);
                playlist.setShuffleEnabled (true);
                playlist.setRepeatMode (mode);
                for (const auto* id : { "a", "b", "a", "c", "a" })
                    expect (playlist.selectTrack (playlist.indexOfId (id)));
                for (int step = 0; step < 4; ++step)
                    expect (playlist.advanceToPreviousTrack());
                const auto candidates = playlist.navigationCandidates (PlaylistNavigation::manualNext);
                const auto plan = playlist.continuousPlaybackPlan();
                const std::vector<juce::String> prefix { "a", "b", "a", "c", "a" };
                expect (plan.trackIds.size() >= prefix.size());
                if (plan.trackIds.size() >= prefix.size())
                    expect (std::equal (prefix.begin(), prefix.end(), plan.trackIds.begin()));
                expect (candidates == playlist.navigationCandidates (PlaylistNavigation::manualNext));
                expect (plan.trackIds == playlist.continuousPlaybackPlan().trackIds);
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
                if (mode == RepeatMode::off)
                    expectEquals (plan.repeatFrom, -1);
                else
                {
                    expect (plan.repeatFrom >= static_cast<int> (prefix.size()));
                    expectEquals (static_cast<int> (plan.trackIds.size()) - plan.repeatFrom, 4);
                }
                for (size_t index = 1; index < prefix.size(); ++index)
                    expect (playlist.recordPlayback (prefix[index]));
                expect (playlist.advanceToPreviousTrack());
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            }
        }

        beginTest ("unavailable history entries preserve adjacent occurrences of the same track");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            for (const auto* id : { "a", "b", "a", "c", "a" })
                expect (playlist.selectTrack (playlist.indexOfId (id)));
            for (int step = 0; step < 4; ++step)
                expect (playlist.advanceToPreviousTrack());
            expect (playlist.setTrackAvailable ("b", false));
            expect (playlist.setTrackAvailable ("c", false));
            const auto plan = playlist.continuousPlaybackPlan();
            expect (plan.trackIds == std::vector<juce::String> { "a", "a", "a", "d" });
            expect (playlist.recordPlayback ("a"));
            expect (playlist.recordPlayback ("a"));
            expect (playlist.setTrackAvailable ("b", true));
            expect (playlist.setTrackAvailable ("c", true));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
        }

        beginTest ("continuous repeat plans isolate the playable loop from the initial queue tail");
        {
            auto playlist = navigationPlaylist();
            expect (playlist.selectTrack (2));
            auto plan = playlist.continuousPlaybackPlan();
            expect (plan.trackIds == std::vector<juce::String> { "c", "d" });
            expectEquals (plan.repeatFrom, -1);
            playlist.setRepeatMode (RepeatMode::all);
            plan = playlist.continuousPlaybackPlan();
            expect (plan.trackIds == std::vector<juce::String> { "c", "d", "a", "b", "c", "d" });
            expectEquals (plan.repeatFrom, 2);
            playlist.setRepeatMode (RepeatMode::one);
            plan = playlist.continuousPlaybackPlan();
            expect (plan.trackIds == std::vector<juce::String> { "c" });
            expectEquals (plan.repeatFrom, 0);
            expect (playlist.setTrackAvailable ("c", false));
            plan = playlist.continuousPlaybackPlan();
            expect (plan.trackIds == std::vector<juce::String> { "c", "d" });
            expectEquals (plan.repeatFrom, 1);
            for (const auto& track : playlist.getTracks())
                expect (playlist.setTrackAvailable (track.id, false));
            for (const auto mode : { RepeatMode::off, RepeatMode::one, RepeatMode::all })
            {
                playlist.setRepeatMode (mode);
                plan = playlist.continuousPlaybackPlan();
                expect (plan.trackIds == std::vector<juce::String> { "c" });
                expectEquals (plan.repeatFrom, -1);
            }
            playlist.clear();
            expect (playlist.continuousPlaybackPlan().trackIds.empty());
        }

        beginTest ("continuous shuffle loops contain one complete available cycle");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleSeed (39);
            playlist.setShuffleEnabled (true);
            playlist.setRepeatMode (RepeatMode::all);
            expect (playlist.selectTrack (0));
            expect (playlist.setTrackAvailable ("c", false));
            const auto candidates = playlist.navigationCandidates (PlaylistNavigation::manualNext);
            const auto plan = playlist.continuousPlaybackPlan();
            expect (playlist.navigationCandidates (PlaylistNavigation::manualNext) == candidates);
            expectEquals (plan.repeatFrom, 3);
            expectEquals (static_cast<int> (plan.trackIds.size()), 6);
            if (plan.trackIds.size() == 6)
            {
                auto cycle = std::vector<juce::String> (plan.trackIds.begin() + 3, plan.trackIds.end());
                std::sort (cycle.begin(), cycle.end());
                expect (cycle == std::vector<juce::String> { "a", "b", "d" });
                expect (plan.trackIds[2] != plan.trackIds[3]);
                expect (plan.trackIds.back() != plan.trackIds[3]);
                for (int index = 1; index < 3; ++index)
                {
                    expect (playlist.advanceToNextTrack());
                    expectEquals (playlist.getSelectedTrack()->id, plan.trackIds[static_cast<size_t> (index)]);
                }
            }
        }

        beginTest ("navigation capabilities match bounded candidates without consuming state");
        {
            for (const auto shuffle : { false, true })
                for (const auto mode : { RepeatMode::off, RepeatMode::one, RepeatMode::all })
                {
                    auto playlist = navigationPlaylist();
                    playlist.setShuffleEnabled (shuffle);
                    playlist.setShuffleSeed (8);
                    playlist.setRepeatMode (mode);
                    expect (playlist.selectTrack (0));
                    expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
                    expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
                    expect (playlist.advanceToPreviousTrack());
                    for (const auto kind : { PlaylistNavigation::manualNext,
                                             PlaylistNavigation::naturalCompletion,
                                             PlaylistNavigation::previous })
                    {
                        const auto before = playlist.navigationCandidates (kind);
                        expect (playlist.hasNavigationCandidate (kind) == ! before.empty());
                        expect (playlist.navigationCandidates (kind) == before);
                    }
                    for (const auto& track : playlist.getTracks())
                        expect (playlist.setTrackAvailable (track.id, false));
                    for (const auto kind : { PlaylistNavigation::manualNext,
                                             PlaylistNavigation::naturalCompletion,
                                             PlaylistNavigation::previous })
                        expect (! playlist.hasNavigationCandidate (kind));
                }
            PlaylistModel empty;
            expect (! empty.hasNavigationCandidate (PlaylistNavigation::manualNext));
        }

        beginTest ("actual playback records fixed shuffle cycles without consuming other tracks");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleSeed (73);
            playlist.setShuffleEnabled (true);
            playlist.setRepeatMode (RepeatMode::all);
            expect (playlist.recordPlayback ("a"));
            for (int cycle = 0; cycle < 2; ++cycle)
                for (const auto* id : { "c", "b", "d", "a" })
                {
                    expect (playlist.recordPlayback (id));
                    expectEquals (playlist.getSelectedTrack()->id, juce::String (id));
                }
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("d"));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (playlist.recordPlayback ("d"));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));

            auto unplayed = navigationPlaylist();
            unplayed.setShuffleSeed (8);
            unplayed.setShuffleEnabled (true);
            expect (unplayed.recordPlayback ("a"));
            const auto proposed = unplayed.navigationCandidates (PlaylistNavigation::manualNext);
            expectEquals (static_cast<int> (proposed.size()), 3);
            if (proposed.size() == 3)
            {
                expect (unplayed.recordPlayback (proposed.back()));
                const auto remaining = unplayed.navigationCandidates (PlaylistNavigation::manualNext);
                expectEquals (static_cast<int> (remaining.size()), 2);
                for (size_t index = 0; index < 2; ++index)
                    expect (std::find (remaining.begin(), remaining.end(), proposed[index])
                            != remaining.end());
            }
        }

        beginTest ("failed candidates remain bounded and never enter playback history");
        {
            for (const auto shuffle : { false, true })
            {
                auto playlist = navigationPlaylist();
                playlist.setShuffleSeed (17);
                playlist.setShuffleEnabled (shuffle);
                playlist.setRepeatMode (RepeatMode::all);
                expect (playlist.selectTrack (0));
                const auto candidates = playlist.navigationCandidates (
                    PlaylistNavigation::manualNext);
                expectEquals (static_cast<int> (candidates.size()), playlist.size());
                expect (candidates.size() >= 2);
                if (candidates.size() >= 2)
                {
                    expect (playlist.setTrackAvailable (candidates.front(), false));
                    expect (playlist.commitPlayback (
                        candidates[1], PlaylistNavigation::manualNext));
                    expect (playlist.advanceToPreviousTrack());
                    expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
                }
                for (const auto& track : playlist.getTracks())
                    expect (playlist.setTrackAvailable (track.id, false));
                expect (playlist.navigationCandidates (PlaylistNavigation::manualNext).empty());
                expect (playlist.navigationCandidates (PlaylistNavigation::naturalCompletion).empty());
                expect (! playlist.advanceToNextTrack());
                expect (playlist.setTrackAvailable ("c", true));
                expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            }
        }

        beginTest ("repeat one does not duplicate history and unavailable entries are skipped");
        {
            auto playlist = navigationPlaylist();
            expect (playlist.selectTrack (0));
            expect (playlist.advanceToNextTrack());
            playlist.setRepeatMode (RepeatMode::one);
            for (int repetition = 0; repetition < 20; ++repetition)
                expect (playlist.advanceToNextTrack());
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            expect (playlist.setTrackAvailable ("b", false));
            expect (playlist.setTrackAvailable ("a", false));
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            expect (! playlist.advanceToPreviousTrack());
        }

        beginTest ("empty playlists do not advance");
        {
            PlaylistModel playlist;
            expect (! playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), -1);
        }

        beginTest ("advance skips unavailable restored tracks and preserves selection at the end");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            TrackItem missing { juce::File ("/music/missing.wav"), "Missing", 18.0 };
            missing.available = false;
            playlist.addTrack (std::move (missing));
            playlist.addTrack ({ juce::File ("/music/third.wav"), "Third", 21.0 });

            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->title, juce::String ("Third"));
            expect (! playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->title, juce::String ("Third"));
        }

        beginTest ("repeat all and shuffle choose only available tracks");
        {
            PlaylistModel playlist;
            TrackItem missing { juce::File ("/music/missing.wav"), "Missing", 18.0 };
            missing.available = false;
            playlist.addTrack (std::move (missing));
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });
            playlist.selectTrack (1);
            playlist.setRepeatMode (RepeatMode::all);
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), 1);

            playlist.setShuffleEnabled (true);
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedIndex(), 1);
        }

        beginTest ("moving tracks preserves selected track identity");
        {
            PlaylistModel playlist;
            TrackItem first { juce::File ("/music/first.wav"), "First", 12.5 };
            TrackItem second { juce::File ("/music/second.wav"), "Second", 18.0 };
            TrackItem third { juce::File ("/music/third.wav"), "Third", 21.0 };
            first.id = "first-id";
            second.id = "second-id";
            third.id = "third-id";
            playlist.addTrack (std::move (first));
            playlist.addTrack (std::move (second));
            playlist.addTrack (std::move (third));
            playlist.selectTrack (1);

            expect (playlist.moveTrack (1, 0));
            expectEquals (playlist.getSelectedIndex(), 0);
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("second-id"));

            expect (playlist.moveTrack (0, 2));
            expectEquals (playlist.getSelectedIndex(), 2);
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("second-id"));
            expect (! playlist.moveTrack (-1, 0));
            expect (! playlist.moveTrack (0, 8));
        }

        beginTest ("queue finds stable IDs and duplicate file paths");
        {
            PlaylistModel playlist;
            TrackItem track { juce::File ("/music/track.wav"), "Track", 2.0 };
            track.id = "stable-track-id";
            playlist.addTrack (std::move (track));

            expectEquals (playlist.indexOfId ("stable-track-id"), 0);
            expectEquals (playlist.indexOfId ("missing-id"), -1);
            expect (playlist.containsFile (juce::File ("/music/track.wav")));
            expect (! playlist.containsFile (juce::File ("/music/other.wav")));
        }

        beginTest ("file identity index deduplicates aliases and is cleaned on remove and clear");
        {
           #if ! JUCE_WINDOWS
            const auto temporaryDirectory = juce::File::getCurrentWorkingDirectory()
                .getNonexistentChildFile ("moonwake-playlist-identity", {}, true);
            const auto directoryResult = temporaryDirectory.createDirectory();
            expect (directoryResult.wasOk(),
                    temporaryDirectory.getFullPathName() + ": "
                        + directoryResult.getErrorMessage());
            const auto original = temporaryDirectory.getChildFile ("original.wav");
            const auto alias = temporaryDirectory.getChildFile ("alias.wav");
            expect (original.replaceWithText ("audio fixture"),
                    original.getFullPathName());
            expect (original.createSymbolicLink (alias, true));

            PlaylistModel playlist;
            playlist.addTrack ({ original, "Original", 1.0 });
            expect (playlist.containsFile (alias));

            playlist.addTrack ({ alias, "Alias", 1.0 });
            expect (playlist.removeTrack (0));
            expect (playlist.containsFile (original));
            expect (playlist.removeTrack (0));
            expect (! playlist.containsFile (original));

            playlist.addTrack ({ original, "Restored", 1.0 });
            auto restored = playlist;
            expect (restored.containsFile (alias));
            restored.clear();
            expect (! restored.containsFile (original));

            expect (temporaryDirectory.deleteRecursively());
           #endif
        }

        beginTest ("same path remains indexed when the file identity changes");
        {
            juce::String currentIdentity = "posix:1:1";
            PlaylistModel playlist ([&currentIdentity] (const juce::File&)
            {
                return currentIdentity;
            });

            const juce::File path ("/music/replaceable.wav");
            playlist.addTrack ({ path, "Original", 1.0 });
            currentIdentity = "posix:1:2";
            expect (playlist.containsFile (path));

            playlist.addTrack ({ path, "Replacement", 1.0 });
            expect (playlist.removeTrack (0));
            expect (playlist.containsFile (path));
            expect (playlist.removeTrack (0));
            expect (! playlist.containsFile (path));
        }

        beginTest ("file identity work stays linear for a large queue");
        {
            int identityResolutionCount = 0;
            PlaylistModel playlist ([&identityResolutionCount] (const juce::File& file)
            {
                ++identityResolutionCount;
                return "test:" + file.getFullPathName();
            });

            constexpr int trackCount = 512;
            for (int index = 0; index < trackCount; ++index)
                playlist.addTrack ({ juce::File ("/music").getChildFile (
                                         "track-" + juce::String (index) + ".wav"),
                                     "Track", 1.0 });
            expectEquals (identityResolutionCount, trackCount);

            for (int index = 0; index < trackCount; ++index)
                expect (playlist.containsFile (juce::File ("/music").getChildFile (
                    "track-" + juce::String (index) + ".wav")));
            expectEquals (identityResolutionCount, trackCount * 2);
        }

        beginTest ("clear resets queue and selection");
        {
            PlaylistModel playlist;
            playlist.addTrack ({ juce::File ("/music/first.wav"), "First", 12.5 });
            playlist.addTrack ({ juce::File ("/music/second.wav"), "Second", 18.0 });

            playlist.clear();
            expect (playlist.isEmpty());
            expectEquals (playlist.getSelectedIndex(), -1);
            expect (playlist.getSelectedTrack() == nullptr);
        }
    }
};

PlaylistModelTests playlistModelTests;
} // namespace
