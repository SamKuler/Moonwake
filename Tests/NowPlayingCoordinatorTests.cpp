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
        current = snapshot;
    }

    void clear() override
    {
        ++clearCount;
        current.reset();
    }

    void setNativeWindowHandle (void* handle) override
    {
        windowHandles.push_back (handle);
    }

    juce::String getLastError() const override { return error; }
    double getTimelineRefreshIntervalSeconds() const noexcept override { return refreshIntervalSeconds; }

    bool emit (RemoteTransportCommand command)
    {
        if (! current || ! handler) return false;
        const auto enabled = [&]
        {
            switch (command.type)
            {
                case RemoteTransportCommandType::play:
                case RemoteTransportCommandType::pause:
                case RemoteTransportCommandType::togglePlayPause: return current->canPlay;
                case RemoteTransportCommandType::next: return current->canGoNext;
                case RemoteTransportCommandType::previous: return current->canGoPrevious;
                case RemoteTransportCommandType::seekToSeconds: return current->canSeek;
            }
            return false;
        }();
        if (! enabled) return false;
        handler (command);
        return true;
    }

    CommandHandler handler;
    std::vector<NowPlayingSnapshot> publications;
    std::optional<NowPlayingSnapshot> current;
    int clearCount = 0;
    std::vector<void*> windowHandles;
    juce::String error;
    double refreshIntervalSeconds = 0.0;
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
    state.canPlay = true;
    state.canGoNext = true;
    state.canGoPrevious = true;
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
       #if ! JUCE_MAC
        beginTest ("system bridge factory supports publication before a native window exists");
        {
            auto bridge = createSystemNowPlayingBridge();
            expect (bridge != nullptr);
            if (bridge)
            {
                int remoteCommands = 0;
                NowPlayingCoordinator coordinator (std::move (bridge));
                coordinator.setCommandHandler ([&] (const auto&) { ++remoteCommands; });
                auto state = selectedState();
                coordinator.update (state, 10.0);
                state.isPlaying = true;
                coordinator.update (state, 11.0);
                coordinator.clear();
                coordinator.update (state, 12.0);
                coordinator.setCommandHandler ({});
                expectEquals (remoteCommands, 0);
            }
        }
       #endif

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

        beginTest ("unavailable selection retains playback retry and available navigation commands");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            std::vector<RemoteTransportCommandType> received;
            coordinator.setCommandHandler ([&received] (const auto& command)
            {
                received.push_back (command.type);
            });
            auto state = selectedState();
            state.selectedTrackAvailable = false;
            state.canGoPrevious = false;
            coordinator.update (state, 1.0);

            expect (fake->emit ({ RemoteTransportCommandType::play }));
            expect (fake->emit ({ RemoteTransportCommandType::next }));
            expect (! fake->emit ({ RemoteTransportCommandType::previous }));
            expect (! fake->emit ({ RemoteTransportCommandType::seekToSeconds, 3.0 }));
            expectEquals (static_cast<int> (received.size()), 2);

            state.canGoNext = false;
            coordinator.update (state, 1.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            expect (! fake->emit ({ RemoteTransportCommandType::next }));
            expect (fake->emit ({ RemoteTransportCommandType::play }));
        }

        beginTest ("restore accepts deferred transport commands without publishing a track");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            std::vector<RemoteTransportCommandType> queued;
            coordinator.setCommandHandler ([&queued] (const auto& command)
            {
                queued.push_back (command.type);
            });
            coordinator.update ({}, 1.0, true);
            const auto& loading = fake->publications.back();
            expect (! loading.hasTrack);
            expect (loading.trackId.isEmpty());
            expect (loading.title.isEmpty());
            expectEquals (loading.queueCount, 0);
            for (const auto command : { RemoteTransportCommandType::play,
                                        RemoteTransportCommandType::pause,
                                        RemoteTransportCommandType::togglePlayPause,
                                        RemoteTransportCommandType::next,
                                        RemoteTransportCommandType::previous })
                expect (fake->emit ({ command }));
            expectEquals (static_cast<int> (queued.size()), 5);
            expect (! fake->emit ({ RemoteTransportCommandType::seekToSeconds, 3.0 }));

            coordinator.update ({}, 1.0, false);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            for (const auto command : { RemoteTransportCommandType::play,
                                        RemoteTransportCommandType::pause,
                                        RemoteTransportCommandType::togglePlayPause,
                                        RemoteTransportCommandType::next,
                                        RemoteTransportCommandType::previous })
                expect (! fake->emit ({ command }));
            expectEquals (static_cast<int> (queued.size()), 5);
            expect (! fake->publications.back().hasTrack);
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

            // Backends such as macOS retain extrapolation even beyond the
            // Windows refresh interval, unless the position actually drifts.
            expectEquals (fake->NowPlayingBridge::getTimelineRefreshIntervalSeconds(), 0.0);
            state.positionSeconds = 22.5;
            coordinator.update (state, 110.0);
            expectEquals (static_cast<int> (fake->publications.size()), 1);

            state.positionSeconds = 40.0;
            coordinator.update (state, 111.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            expectWithinAbsoluteError (fake->publications.back().elapsedSeconds,
                                       40.0, 0.001);
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

            coordinator.update (selectedState(), 1.0);
            expect (fake->emit ({ RemoteTransportCommandType::seekToSeconds, 42.0 }));
            expect (received.type == RemoteTransportCommandType::seekToSeconds);
            expectWithinAbsoluteError (received.positionSeconds, 42.0, 0.001);

            coordinator.clear();
            expect (! fake->emit ({ RemoteTransportCommandType::play }));
            coordinator.update (selectedState(), 2.0);
            expectEquals (fake->clearCount, 1);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
        }

        beginTest ("refreshes the playing timeline at the interval requested by the bridge");
        for (const auto interval : { 2.0, 5.0 })
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            fake->refreshIntervalSeconds = interval;
            NowPlayingCoordinator coordinator (std::move (bridge));
            auto state = selectedState();
            state.isPlaying = true;
            coordinator.update (state, 100.0);
            state.positionSeconds += interval - 0.1;
            coordinator.update (state, 100.0 + interval - 0.1);
            expectEquals (static_cast<int> (fake->publications.size()), 1);
            state.positionSeconds += 0.1;
            coordinator.update (state, 100.0 + interval);
            expectEquals (static_cast<int> (fake->publications.size()), 2);

            state.positionSeconds += interval;
            coordinator.update (state, 100.0 + 2.0 * interval);
            expectEquals (static_cast<int> (fake->publications.size()), 3);
        }

        beginTest ("periodic refresh does not republish unchanged paused or empty sessions");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            fake->refreshIntervalSeconds = 5.0;
            NowPlayingCoordinator coordinator (std::move (bridge));
            auto state = selectedState();
            coordinator.update (state, 100.0);
            coordinator.update (state, 110.0);
            expectEquals (static_cast<int> (fake->publications.size()), 1);
            state.positionSeconds += 1.0;
            coordinator.update (state, 111.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            coordinator.update ({}, 112.0);
            coordinator.update ({}, 122.0);
            expectEquals (static_cast<int> (fake->publications.size()), 3);
        }

        beginTest ("republishes paused metadata after attaching or replacing the native window");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            int firstWindow = 0, secondWindow = 0;
            const auto state = selectedState();
            coordinator.update (state, 1.0);
            coordinator.setNativeWindowHandle (&firstWindow);
            coordinator.update (state, 2.0);
            coordinator.setNativeWindowHandle (&firstWindow);
            coordinator.update (state, 3.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            expectEquals (static_cast<int> (fake->windowHandles.size()), 1);
            coordinator.setNativeWindowHandle (&secondWindow);
            coordinator.update (state, 4.0);
            expectEquals (static_cast<int> (fake->publications.size()), 3);
            coordinator.setNativeWindowHandle (nullptr);
            expect (fake->windowHandles.back() == nullptr);
        }

        beginTest ("exposes media errors and retries paused publication at a bounded rate");
        {
            auto bridge = std::make_unique<FakeNowPlayingBridge>();
            auto* fake = bridge.get();
            NowPlayingCoordinator coordinator (std::move (bridge));
            fake->error = "System media service unavailable";
            const auto state = selectedState();
            coordinator.update (state, 10.0);
            expectEquals (coordinator.getLastError(), fake->error);
            coordinator.update (state, 14.0);
            expectEquals (static_cast<int> (fake->publications.size()), 1);
            coordinator.update (state, 15.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            fake->error.clear();
            coordinator.update (state, 20.0);
            expectEquals (static_cast<int> (fake->publications.size()), 2);
            expect (coordinator.getLastError().isEmpty());
        }
    }
};

NowPlayingCoordinatorTests nowPlayingCoordinatorTests;
} // namespace
