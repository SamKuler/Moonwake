#include <juce_core/juce_core.h>

#include "app/NowPlayingCoordinator.h"

namespace
{
class FakeNowPlayingBridge final : public NowPlayingBridge
{
public:
    void setCommandHandler (CommandHandler nextHandler) override
    {
        handler = std::move (nextHandler);
    }

    void publish (const NowPlayingSnapshot& snapshot) override
    {
        publications.push_back (snapshot);
    }

    void clear() override { ++clearCount; }

    void emit (RemoteTransportCommand command)
    {
        if (handler)
            handler (command);
    }

    CommandHandler handler;
    std::vector<NowPlayingSnapshot> publications;
    int clearCount = 0;
};

PlayerViewState selectedState()
{
    PlayerViewState state;
    state.selectedIndex = 1;
    state.playlistSize = 3;
    state.selectedTrackId = "track-2";
    state.selectedTrackTitle = "Forest Light";
    state.selectedTrackArtist = "Sam Kuler";
    state.selectedTrackAlbum = "Natural Works";
    state.selectedTrackArtworkPath = "/artwork/forest.png";
    state.selectedTrackAvailable = true;
    state.positionSeconds = 12.5;
    state.lengthSeconds = 0.0;
    state.selectedTrackDurationSeconds = 180.0;
    return state;
}

class NowPlayingCoordinatorTests final : public juce::UnitTest
{
public:
    NowPlayingCoordinatorTests()
        : UnitTest ("NowPlayingCoordinator", "app") {}

    void runTest() override
    {
        beginTest ("projects selected-track metadata and command capabilities");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));

            coordinator.update (selectedState(), 10.0);

            expectEquals (static_cast<int> (fake->publications.size()), 1);
            const auto& snapshot = fake->publications.front();
            expect (snapshot.hasTrack);
            expectEquals (snapshot.trackId, juce::String ("track-2"));
            expectEquals (snapshot.title, juce::String ("Forest Light"));
            expectEquals (snapshot.artist, juce::String ("Sam Kuler"));
            expectEquals (snapshot.album, juce::String ("Natural Works"));
            expectEquals (snapshot.artworkPath,
                          juce::String ("/artwork/forest.png"));
            expectEquals (snapshot.queueIndex, 1);
            expectEquals (snapshot.queueCount, 3);
            expect (snapshot.canPlay);
            expect (snapshot.canGoPrevious);
            expect (snapshot.canGoNext);
            expect (snapshot.canSeek);
            expect (! snapshot.playing);
            expectWithinAbsoluteError (snapshot.elapsedSeconds, 12.5, 0.001);
            expectWithinAbsoluteError (snapshot.durationSeconds, 180.0, 0.001);
        }

        beginTest ("suppresses duplicates and publishes paused position changes");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            auto state = selectedState();

            coordinator.update (state, 20.0);
            coordinator.update (state, 21.0);
            state.positionSeconds += 4.0;
            coordinator.update (state, 22.0);

            expectEquals (static_cast<int> (fake->publications.size()), 2);
            expectWithinAbsoluteError (fake->publications.back().elapsedSeconds,
                                       16.5, 0.001);
        }

        beginTest ("uses system extrapolation while playing and repairs drift");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            auto state = selectedState();
            state.isPlaying = true;

            coordinator.update (state, 100.0);
            state.positionSeconds = 13.5;
            coordinator.update (state, 101.0);
            expectEquals (static_cast<int> (fake->publications.size()), 1);

            state.positionSeconds = 20.0;
            coordinator.update (state, 102.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            expectWithinAbsoluteError (fake->publications.back().elapsedSeconds,
                                       20.0, 0.001);
        }

        beginTest ("forwards typed remote commands and resets publication state");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            RemoteTransportCommand received;
            coordinator.setCommandHandler ([&received] (const auto& command)
            {
                received = command;
            });

            fake->emit ({ RemoteTransportCommandType::seekToSeconds, 42.0 });
            expect (received.type == RemoteTransportCommandType::seekToSeconds);
            expectWithinAbsoluteError (received.positionSeconds, 42.0, 0.001);

            coordinator.update (selectedState(), 1.0);
            coordinator.clear();
            coordinator.update (selectedState(), 2.0);
            expectEquals (fake->clearCount, 1);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
        }
    }
};

NowPlayingCoordinatorTests nowPlayingCoordinatorTests;
} // namespace
