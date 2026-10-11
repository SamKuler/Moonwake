#include "fixtures/FileAlias.h"
#include <juce_core/juce_core.h>

#include "playlist/PlaylistModel.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

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

std::vector<juce::String> occurrenceIds (const PlaylistModel& playlist)
{
    std::vector<juce::String> result;
    for (const auto& track : playlist.getTracks()) result.push_back (track.id);
    return result;
}

TrackItem occurrence (const juce::String& id)
{
    TrackItem track { juce::File ("/music").getChildFile (id + ".wav"), id, 1.0 };
    track.id = id;
    return track;
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
        beginTest ("play-next priority reaches both shuffle plans and manual navigation in input order");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleSeed (11);
            playlist.setShuffleEnabled (true);
            playlist.selectTrack (0);
            const auto a = playlist.getSelectedTrack()->id;
            playlist.insertTracks (1, { occurrence ("next-1"), occurrence ("next-2") });
            expect (playlist.prioritiseNextTracks ({ "next-1", "next-2" }));
            const auto plan = playlist.continuousPlaybackPlan();
            expectEquals (plan.trackIds[1], juce::String ("next-1"));
            expectEquals (plan.trackIds[2], juce::String ("next-2"));
            expectEquals (static_cast<int> (std::count (plan.trackIds.begin(), plan.trackIds.end(), "next-1")), 1);
            auto manual = playlist;
            expect (manual.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (manual.getSelectedTrack()->id, juce::String ("next-1"));
            expect (manual.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (manual.getSelectedTrack()->id, juce::String ("next-2"));
            expect (manual.advanceToPreviousTrack());
            expectEquals (manual.getSelectedTrack()->id, juce::String ("next-1"));
            expect (manual.advanceToPreviousTrack());
            expectEquals (manual.getSelectedTrack()->id, a);
            auto rendered = playlist;
            expect (rendered.recordPlayback ("next-1"));
            expectEquals (rendered.continuousPlaybackPlan().trackIds[1], juce::String ("next-2"));
            playlist.setRepeatMode (RepeatMode::one);
            expect (playlist.continuousPlaybackPlan().trackIds == std::vector<juce::String> { a });
            expectEquals (playlist.navigationCandidates (PlaylistNavigation::manualNext).front(), juce::String ("next-1"));
        }

        beginTest ("play-next priority survives toggling shuffle without duplicate playback");
        {
            PlaylistModel playlist;
            playlist.addTrack (occurrence ("a"));
            playlist.insertTracks (1, { occurrence ("b"), occurrence ("c") });
            expect (playlist.prioritiseNextTracks ({ "b", "c" }));
            playlist.setShuffleEnabled (true);
            expect (playlist.continuousPlaybackPlan().trackIds == std::vector<juce::String> { "a", "b", "c" });
            playlist.setShuffleEnabled (false);
            playlist.setShuffleEnabled (true);
            expect (playlist.continuousPlaybackPlan().trackIds == std::vector<juce::String> { "a", "b", "c" });
        }

        for (const auto shuffle : { false, true })
        {
            beginTest (juce::String ("reordering pending play-next entries follows the edited queue / ")
                       + (shuffle ? "shuffle" : "sequential"));
            PlaylistModel playlist;
            playlist.addTrack (occurrence ("a"));
            playlist.setShuffleEnabled (shuffle);
            playlist.selectTrack (0);
            playlist.insertTracks (1, { occurrence ("b"), occurrence ("c") });
            expect (playlist.prioritiseNextTracks ({ "b", "c" }));
            expect (playlist.moveTracks ({ "c" }, 1));
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "a", "c", "b" });
            expect (playlist.continuousPlaybackPlan().trackIds == std::vector<juce::String> { "a", "c", "b" });
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (! playlist.advanceToNextTrack());
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
        }

        beginTest ("sequential play-next edits keep ordinary queue entries between priority tracks");
        {
            PlaylistModel playlist;
            playlist.insertTracks (0, { occurrence ("a"), occurrence ("d") });
            playlist.selectTrack (0);
            playlist.insertTracks (1, { occurrence ("b"), occurrence ("c") });
            expect (playlist.prioritiseNextTracks ({ "b", "c" }));
            expect (playlist.moveTracks ({ "b" }, playlist.size()));
            const std::vector<juce::String> order { "a", "c", "d", "b" };
            expect (occurrenceIds (playlist) == order);
            expect (playlist.continuousPlaybackPlan().trackIds == order);
            for (size_t index = 1; index < order.size(); ++index)
            {
                expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
                expectEquals (playlist.getSelectedTrack()->id, order[index]);
            }
            expect (! playlist.advanceToNextTrack());
        }

        beginTest ("previous and forward traversal keep partially consumed play-next order");
        {
            PlaylistModel playlist;
            playlist.addTrack (occurrence ("a"));
            playlist.setShuffleEnabled (true);
            playlist.selectTrack (0);
            playlist.insertTracks (1, { occurrence ("b"), occurrence ("c") });
            expect (playlist.prioritiseNextTracks ({ "b", "c" }));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("a"));
            expect (playlist.continuousPlaybackPlan().trackIds == std::vector<juce::String> { "a", "b", "c" });
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
        }

        beginTest ("new play-next batches preserve actual forward history after Previous");
        {
            PlaylistModel playlist;
            playlist.addTrack (occurrence ("a"));
            playlist.setShuffleEnabled (true);
            playlist.selectTrack (0);
            playlist.insertTracks (1, { occurrence ("b"), occurrence ("c") });
            expect (playlist.prioritiseNextTracks ({ "b", "c" }));
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expect (playlist.advanceToPreviousTrack());
            playlist.insertTracks (1, { occurrence ("x") });
            expect (playlist.prioritiseNextTracks ({ "x" }));
            const std::vector<juce::String> expected { "a", "x", "b", "c" };
            expect (playlist.continuousPlaybackPlan().trackIds == expected);
            auto rendered = playlist;
            for (size_t index = 1; index < expected.size(); ++index)
            {
                expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
                expectEquals (playlist.getSelectedTrack()->id, expected[index]);
                expect (rendered.recordPlayback (expected[index]));
                expect (rendered.continuousPlaybackPlan().trackIds == playlist.continuousPlaybackPlan().trackIds);
            }
            expect (! playlist.advanceToNextTrack());
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
        }

        beginTest ("queue replacement preserves pending shuffle appearances after traversing previous history");
        {
            for (int seed = 0; seed < 20; ++seed)
            {
                auto playlist = navigationPlaylist();
                playlist.setShuffleSeed (seed);
                playlist.setShuffleEnabled (true);
                playlist.setRepeatMode (RepeatMode::all);
                playlist.selectTrack (0);
                for (int i = 0; i < 7; ++i) playlist.advanceToNextTrack (PlaylistNavigation::manualNext);
                playlist.advanceToPreviousTrack();
                playlist.advanceToPreviousTrack();
                const auto expected = playlist.continuousPlaybackPlan();
                expect (playlist.replaceTracks (playlist.getTracks()));
                expect (playlist.continuousPlaybackPlan().trackIds == expected.trackIds, "seed " + juce::String (seed));
                expectEquals (playlist.continuousPlaybackPlan().repeatFrom, expected.repeatFrom);
            }
        }

        beginTest ("play-next consumes a short shuffle queue without rebuilding its finished cycle");
        {
            PlaylistModel playlist;
            playlist.addTrack (occurrence ("a"));
            playlist.setShuffleEnabled (true);
            playlist.selectTrack (0);
            playlist.insertTracks (1, { occurrence ("b"), occurrence ("c") });
            expect (playlist.prioritiseNextTracks ({ "b", "c" }));
            expect (playlist.recordPlayback ("b"));
            expect (playlist.recordPlayback ("c"));
            expectEquals (static_cast<int> (playlist.continuousPlaybackPlan().trackIds.size()), 1);
            expect (! playlist.hasNavigationCandidate (PlaylistNavigation::manualNext));
        }

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
            expect (createTestFileAlias (original, alias));

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

        beginTest ("duplicate files and library records remain independent queue occurrences");
        {
            PlaylistModel playlist;
            auto track = occurrence ("first");
            track.libraryId = "library-track";
            expect (playlist.insertTracks (0, { track, track, track }));
            expectEquals (playlist.size(), 3);
            std::unordered_set<std::string> unique;
            for (const auto& item : playlist.getTracks())
            {
                expect (item.id.isNotEmpty());
                expect (unique.insert (item.id.toStdString()).second);
                expectEquals (item.libraryId, track.libraryId);
                expect (item.file == track.file);
            }
            const auto ids = occurrenceIds (playlist);
            expect (playlist.selectTrack (1));
            expect (playlist.removeTracks ({ ids[0] }));
            expectEquals (playlist.getSelectedTrack()->id, ids[1]);
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, ids[2]);
            expect (playlist.advanceToPreviousTrack());
            expectEquals (playlist.getSelectedTrack()->id, ids[1]);
            playlist.addTrack (*playlist.getSelectedTrack());
            expect (playlist.getTrack (2)->id != ids[1]);
            expect (playlist.getTrack (2)->id != ids[2]);
            expect (playlist.containsFile (track.file));
        }

        beginTest ("batch insertion repairs IDs and keeps selected occurrence at every boundary");
        {
            for (int boundary = 0; boundary <= 4; ++boundary)
            {
                auto playlist = navigationPlaylist();
                expect (playlist.selectTrack (2));
                auto missingId = occurrence ("new");
                missingId.id.clear();
                expect (playlist.insertTracks (boundary, { occurrence ("a"), missingId }));
                expectEquals (playlist.size(), 6);
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
                expectEquals (playlist.getSelectedIndex(), boundary <= 2 ? 4 : 2);
                expect (playlist.getTrack (boundary)->id != "a");
                expect (playlist.getTrack (boundary + 1)->id.isNotEmpty());
                std::unordered_set<std::string> ids;
                for (const auto& track : playlist.getTracks())
                    expect (ids.insert (track.id.toStdString()).second);
            }
        }

        beginTest ("batch moves use pre-edit boundaries and retain source order");
        {
            auto playlist = navigationPlaylist();
            expect (playlist.selectTrack (2));
            expect (playlist.moveTracks ({ "d", "b", "b" }, 2));
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "a", "b", "d", "c" });
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            expect (playlist.moveTracks ({ "c", "a" }, 4));
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "b", "d", "a", "c" });
            expect (playlist.moveTracks ({ "c", "a" }, 2));
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "b", "d", "a", "c" });
            expect (playlist.moveTracks ({ "c", "a" }, 0));
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "a", "c", "b", "d" });
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
        }

        beginTest ("invalid batches leave all occurrences and navigation intact");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            expect (playlist.selectTrack (1));
            const auto ids = occurrenceIds (playlist);
            const auto plan = playlist.continuousPlaybackPlan().trackIds;
            expect (! playlist.insertTracks (-1, { occurrence ("x") }));
            expect (! playlist.insertTracks (5, { occurrence ("x") }));
            expect (! playlist.moveTracks ({ "b", "unknown" }, 0));
            expect (! playlist.moveTracks ({ "b" }, 5));
            expect (! playlist.removeTracks ({ "b", "unknown" }));
            expect (playlist.insertTracks (0, {}));
            expect (playlist.removeTracks ({}));
            expect (playlist.moveTracks ({}, 4));
            expect (occurrenceIds (playlist) == ids);
            expect (playlist.continuousPlaybackPlan().trackIds == plan);
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
        }

        beginTest ("batch removal selects the old successor and clears exhausted navigation");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            expect (playlist.selectTrack (1));
            expect (playlist.removeTracks ({ "b", "a", "a" }));
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "c", "d" });
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("d"));
            expect (playlist.removeTracks ({ "d" }));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            expect (playlist.removeTracks ({ "c" }));
            expectEquals (playlist.getSelectedIndex(), -1);
            expect (! playlist.advanceToNextTrack());
            expect (! playlist.advanceToPreviousTrack());
            expect (playlist.insertTracks (0, { occurrence ("x"), occurrence ("y") }));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("x"));
            expect (! playlist.advanceToPreviousTrack());
            expect (playlist.advanceToNextTrack());
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("y"));
            expect (! playlist.advanceToNextTrack());
        }

        beginTest ("shuffle edits retain played history without restarting the cycle");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            playlist.setShuffleSeed (18);
            expect (playlist.recordPlayback ("a"));
            expect (playlist.recordPlayback ("b"));
            expect (playlist.recordPlayback ("c"));
            expect (playlist.advanceToPreviousTrack());
            expect (playlist.insertTracks (1, { occurrence ("x"), occurrence ("y") }));
            expect (playlist.moveTracks ({ "y", "c" }, 0));
            expect (playlist.removeTracks ({ "a", "d" }));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
            expect (! playlist.advanceToPreviousTrack());
            expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
            std::vector<juce::String> remaining;
            while (playlist.advanceToNextTrack()) remaining.push_back (playlist.getSelectedTrack()->id);
            std::sort (remaining.begin(), remaining.end());
            expect (remaining == std::vector<juce::String> { "x", "y" });
        }

        beginTest ("reordering retains exact shuffle sequence and forward-history occurrences");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            playlist.setShuffleSeed (721);
            for (const auto* id : { "a", "b", "a", "c", "a" })
                expect (playlist.recordPlayback (id));
            for (int step = 0; step < 4; ++step) expect (playlist.advanceToPreviousTrack());
            auto untouched = playlist;
            expect (playlist.moveTracks ({ "a", "c" }, 4));
            auto sorted = playlist.getTracks();
            std::reverse (sorted.begin(), sorted.end());
            expect (playlist.replaceTracks (std::move (sorted)));
            expectEquals (playlist.getSelectedTrack()->id, untouched.getSelectedTrack()->id);
            expect (playlist.continuousPlaybackPlan().trackIds == untouched.continuousPlaybackPlan().trackIds);
            for (const auto mode : { RepeatMode::off, RepeatMode::one, RepeatMode::all })
            {
                if (mode == RepeatMode::all)
                    expect (playlist.replaceTracks (untouched.getTracks()));
                playlist.setRepeatMode (mode);
                untouched.setRepeatMode (mode);
                for (int step = 0; step < 8; ++step)
                {
                    expect (playlist.advanceToNextTrack() == untouched.advanceToNextTrack());
                    expectEquals (playlist.getSelectedTrack()->id, untouched.getSelectedTrack()->id);
                }
            }
        }

        beginTest ("undo restores records without rewinding playback after it advances");
        {
            for (const auto shuffle : { false, true })
            {
                auto playlist = navigationPlaylist();
                playlist.setShuffleEnabled (shuffle);
                expect (playlist.recordPlayback ("a"));
                expect (playlist.recordPlayback ("b"));
                const auto saved = playlist.getTracks();
                expect (playlist.removeTracks ({ "a" }));
                expect (playlist.recordPlayback ("c"));
                expect (playlist.replaceTracks (saved));
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
                expect (playlist.advanceToPreviousTrack());
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
                expect (playlist.advanceToNextTrack (PlaylistNavigation::manualNext));
                expectEquals (playlist.getSelectedTrack()->id, juce::String ("c"));
                if (shuffle)
                {
                    auto remaining = playlist.navigationCandidates (PlaylistNavigation::manualNext);
                    std::sort (remaining.begin(), remaining.end());
                    expect (remaining == std::vector<juce::String> { "a", "d" });
                    expect (playlist.advanceToPreviousTrack());
                    expectEquals (playlist.getSelectedTrack()->id, juce::String ("b"));
                    expect (! playlist.advanceToPreviousTrack());
                }
            }
        }

        beginTest ("replacement repairs duplicate IDs and adds newly available shuffle occurrences");
        {
            auto playlist = navigationPlaylist();
            playlist.setShuffleEnabled (true);
            expect (playlist.recordPlayback ("a"));
            expect (playlist.setTrackAvailable ("c", false));
            expect (playlist.recordPlayback ("b"));
            expect (playlist.recordPlayback ("d"));
            expect (! playlist.advanceToNextTrack());
            auto replaced = playlist.getTracks();
            replaced[2].available = true;
            replaced.push_back (replaced[3]);
            replaced.push_back (occurrence ("x"));
            replaced.back().id.clear();
            expect (playlist.replaceTracks (std::move (replaced)));
            expectEquals (playlist.getSelectedTrack()->id, juce::String ("d"));
            std::unordered_set<std::string> ids;
            for (const auto& track : playlist.getTracks())
            {
                expect (track.id.isNotEmpty());
                expect (ids.insert (track.id.toStdString()).second);
            }
            expectEquals (static_cast<int> (playlist.navigationCandidates (PlaylistNavigation::manualNext).size()), 3);
            expect (playlist.replaceTracks ({}));
            expectEquals (playlist.getSelectedIndex(), -1);
            expect (! playlist.advanceToPreviousTrack());
        }

        beginTest ("reorder and retained replacement reuse file identities while relocation refreshes them");
        {
            int resolutions = 0;
            PlaylistModel playlist ([&] (const juce::File& file)
            {
                ++resolutions;
                return file.getFullPathName();
            });
            expect (playlist.insertTracks (0, { occurrence ("a"), occurrence ("b"), occurrence ("c") }));
            expectEquals (resolutions, 3);
            expect (playlist.moveTracks ({ "a", "b" }, 3));
            auto sorted = playlist.getTracks();
            std::reverse (sorted.begin(), sorted.end());
            expect (playlist.replaceTracks (sorted));
            expect (playlist.removeTracks ({ "a" }));
            expectEquals (resolutions, 3);
            auto relocated = playlist.getTracks();
            relocated[0].file = juce::File ("/music/relocated.wav");
            expect (playlist.replaceTracks (relocated));
            expectEquals (resolutions, 4);
            expect (playlist.containsFile (juce::File ("/music/relocated.wav")));
            expect (! playlist.containsFile (sorted[0].file));
        }

        beginTest ("batch edits preserve duplicate file identity and path reference counts");
        {
            PlaylistModel playlist ([] (const juce::File& file)
            {
                return file.getFileName() == "alias.wav" ? juce::File ("/music/a.wav").getFullPathName()
                                                         : file.getFullPathName();
            });
            auto alias = occurrence ("alias");
            auto duplicate = occurrence ("a");
            duplicate.id = "second-a";
            expect (playlist.insertTracks (0, { occurrence ("a"), alias, duplicate, occurrence ("b") }));
            expect (playlist.moveTracks ({ "alias", "second-a" }, 0));
            expect (playlist.removeTracks ({ "a", "alias" }));
            expect (playlist.containsFile (juce::File ("/music/a.wav")));
            expect (playlist.containsFile (juce::File ("/music/alias.wav")));
            expect (playlist.removeTracks ({ "second-a" }));
            expect (! playlist.containsFile (juce::File ("/music/a.wav")));
            expect (! playlist.containsFile (juce::File ("/music/alias.wav")));
            expect (playlist.containsFile (juce::File ("/music/b.wav")));
            expect (playlist.replaceTracks ({}));
            expect (! playlist.containsFile (juce::File ("/music/b.wav")));
        }

        beginTest ("identity resolution failure leaves insertion and replacement uncommitted");
        {
            PlaylistModel playlist ([] (const juce::File& file) -> juce::String
            {
                if (file.getFileName() == "failure.wav") throw std::runtime_error ("unavailable identity");
                return file.getFullPathName();
            });
            expect (playlist.insertTracks (0, { occurrence ("a"), occurrence ("b") }));
            playlist.setShuffleEnabled (true);
            expect (playlist.recordPlayback ("a"));
            const auto plan = playlist.continuousPlaybackPlan().trackIds;
            bool insertionFailed = false, replacementFailed = false;
            try { playlist.insertTracks (1, { occurrence ("c"), occurrence ("failure") }); }
            catch (const std::runtime_error&) { insertionFailed = true; }
            try { playlist.replaceTracks ({ occurrence ("c"), occurrence ("failure") }); }
            catch (const std::runtime_error&) { replacementFailed = true; }
            expect (insertionFailed && replacementFailed);
            expect (occurrenceIds (playlist) == std::vector<juce::String> { "a", "b" });
            expect (playlist.continuousPlaybackPlan().trackIds == plan);
            expect (playlist.containsFile (juce::File ("/music/a.wav")));
            expect (! playlist.containsFile (juce::File ("/music/c.wav")));
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
