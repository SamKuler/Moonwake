#include <juce_core/juce_core.h>

#include "playlist/PlaylistModel.h"

namespace
{
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
