#include <juce_core/juce_core.h>

#include "app/PlaybackEndDetector.h"

class PlaybackEndDetectorTests final : public juce::UnitTest
{
public:
    PlaybackEndDetectorTests()
        : UnitTest ("PlaybackEndDetector", "app")
    {
    }

    void runTest() override
    {
        beginTest ("triggers only when active playback reaches the end");
        {
            PlaybackEndDetector detector;

            expect (! detector.update (false, 0.0, 10.0));
            expect (! detector.update (true, 5.0, 10.0));
            expect (! detector.update (true, 9.8, 10.0));
            expect (detector.update (false, 10.0, 10.0));
            expect (! detector.update (false, 10.0, 10.0));
        }

        beginTest ("does not treat pause or unknown length as completion");
        {
            PlaybackEndDetector detector;

            expect (! detector.update (true, 2.0, 10.0));
            expect (! detector.update (false, 2.0, 10.0));

            expect (! detector.update (true, 0.0, 0.0));
            expect (! detector.update (false, 0.0, 0.0));
        }
    }
};

PlaybackEndDetectorTests playbackEndDetectorTests;
