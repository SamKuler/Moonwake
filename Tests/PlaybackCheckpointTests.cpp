#include <juce_core/juce_core.h>

#include "app/PlaybackCheckpoint.h"

#include <limits>

namespace
{
class PlaybackCheckpointTests final : public juce::UnitTest
{
public:
    PlaybackCheckpointTests()
        : UnitTest ("PlaybackCheckpoint", "app") {}

    void runTest() override
    {
        beginTest ("checkpoints a playing track every fifteen seconds");
        {
            PlaybackCheckpoint checkpoint;
            checkpoint.reset ("track-a", 5.0);

            expect (! checkpoint.observe ("track-a", 19.9, true));
            expect (checkpoint.observe ("track-a", 20.0, true));
            expect (! checkpoint.observe ("track-a", 34.9, true));
            expect (checkpoint.observe ("track-a", 35.0, true));
        }

        beginTest ("paused playback does not create periodic revisions");
        {
            PlaybackCheckpoint checkpoint;
            checkpoint.reset ("track-a", 4.0);

            expect (! checkpoint.observe ("track-a", 40.0, false));
            expect (! checkpoint.observe ("track-a", 55.0, false));
        }

        beginTest ("track changes create a checkpoint and sanitize positions");
        {
            PlaybackCheckpoint checkpoint;
            checkpoint.reset ("track-a", 10.0);

            expect (checkpoint.observe ("track-b", 2.5, true));
            expect (! checkpoint.observe (
                "track-b", std::numeric_limits<double>::quiet_NaN(), true));
            expect (! checkpoint.observe ({}, 90.0, true));
        }

        beginTest ("retains a position only for its matching track");
        {
            PlaybackCheckpoint checkpoint;
            checkpoint.reset ("track-a", 27.5);

            const auto matching = checkpoint.positionForTrack ("track-a");
            expect (matching.has_value());
            if (matching.has_value())
                expectWithinAbsoluteError (*matching, 27.5, 0.001);
            expect (! checkpoint.positionForTrack ("track-b").has_value());
            expect (! checkpoint.positionForTrack ({}).has_value());

            checkpoint.reset ({}, 0.0);
            expect (! checkpoint.positionForTrack ("track-a").has_value());
        }
    }
};

PlaybackCheckpointTests playbackCheckpointTests;
} // namespace
