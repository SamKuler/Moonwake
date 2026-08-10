#include <juce_core/juce_core.h>

#include "app/PlaybackCoordinator.h"

namespace
{
class PlaybackCoordinatorTests final : public juce::UnitTest
{
public:
    PlaybackCoordinatorTests()
        : UnitTest ("PlaybackCoordinator", "app")
    {
    }

    void runTest() override
    {
        beginTest ("pause and resume do not reload the current track");
        {
            PlaybackCoordinator coordinator;

            expect (coordinator.togglePlay ("track-2").type == PlaybackActionType::loadAndPlay);
            expect (coordinator.getIntent() == TransportIntent::playing);
            expectEquals (coordinator.getLoadedTrackId(), juce::String ("track-2"));

            expect (coordinator.togglePlay ("track-2").type == PlaybackActionType::pause);
            expect (coordinator.getIntent() == TransportIntent::paused);

            expect (coordinator.togglePlay ("track-2").type == PlaybackActionType::play);
            expect (coordinator.getIntent() == TransportIntent::playing);
        }

        beginTest ("selecting the loaded track is a no-op");
        {
            PlaybackCoordinator coordinator;
            static_cast<void> (coordinator.togglePlay ("track-1"));

            const auto action = coordinator.selectionChanged ("track-1");
            expect (action.type == PlaybackActionType::none);
            expectEquals (coordinator.getLoadedTrackId(), juce::String ("track-1"));
        }

        beginTest ("selection changes preserve playing or paused intent");
        {
            PlaybackCoordinator playing;
            static_cast<void> (playing.togglePlay ("track-1"));
            const auto playingAction = playing.selectionChanged ("track-2");
            expect (playingAction.type == PlaybackActionType::loadAndPlay);
            expectEquals (playingAction.trackId, juce::String ("track-2"));

            PlaybackCoordinator paused;
            static_cast<void> (paused.togglePlay ("track-1"));
            static_cast<void> (paused.togglePlay ("track-1"));
            const auto pausedAction = paused.selectionChanged ("track-2");
            expect (pausedAction.type == PlaybackActionType::load);
            expect (paused.getIntent() == TransportIntent::paused);
        }

        beginTest ("previous restarts after three seconds and otherwise changes track");
        {
            PlaybackCoordinator coordinator;
            static_cast<void> (coordinator.togglePlay ("track-2"));

            expect (coordinator.previous ("track-1", 3.0).type == PlaybackActionType::seekToStart);

            const auto previousAction = coordinator.previous ("track-1", 2.9);
            expect (previousAction.type == PlaybackActionType::loadAndPlay);
            expectEquals (previousAction.trackId, juce::String ("track-1"));
        }

        beginTest ("playback finish loads the next track or stops cleanly");
        {
            PlaybackCoordinator coordinator;
            static_cast<void> (coordinator.togglePlay ("track-1"));

            const auto nextAction = coordinator.playbackFinished ("track-2");
            expect (nextAction.type == PlaybackActionType::loadAndPlay);
            expectEquals (nextAction.trackId, juce::String ("track-2"));

            const auto stopAction = coordinator.playbackFinished ({});
            expect (stopAction.type == PlaybackActionType::stop);
            expect (coordinator.getIntent() == TransportIntent::stopped);
            expect (coordinator.getLoadedTrackId().isEmpty());
        }

        beginTest ("restored tracks are always prepared paused");
        {
            PlaybackCoordinator coordinator;
            static_cast<void> (coordinator.togglePlay ("old-track"));
            expect (coordinator.getIntent() == TransportIntent::playing);

            const auto action = coordinator.preparePausedTrack ("saved-track");
            expect (action.type == PlaybackActionType::load);
            expectEquals (action.trackId, juce::String ("saved-track"));
            expect (coordinator.getIntent() == TransportIntent::paused);
            expectEquals (coordinator.getLoadedTrackId(),
                          juce::String ("saved-track"));
        }

        beginTest ("invalid selections and failed loads leave a safe stopped state");
        {
            PlaybackCoordinator coordinator;
            expect (coordinator.togglePlay ({}).type == PlaybackActionType::none);
            expect (coordinator.selectionChanged ({}).type == PlaybackActionType::none);

            static_cast<void> (coordinator.togglePlay ("track-1"));
            coordinator.loadFailed();
            expect (coordinator.getIntent() == TransportIntent::stopped);
            expect (coordinator.getLoadedTrackId().isEmpty());
        }
    }
};

PlaybackCoordinatorTests playbackCoordinatorTests;
} // namespace
