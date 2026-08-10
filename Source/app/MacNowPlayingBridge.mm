#include "NowPlayingBridge.h"

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

#include <cmath>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace
{
NSString* toNSString (const juce::String& value)
{
    return [NSString stringWithUTF8String:value.toRawUTF8()];
}

class CommandState
{
public:
    void setHandler (NowPlayingBridge::CommandHandler nextHandler)
    {
        const std::lock_guard lock (mutex);
        handler = std::move (nextHandler);
    }

    bool dispatch (RemoteTransportCommand command)
    {
        NowPlayingBridge::CommandHandler callback;
        {
            const std::lock_guard lock (mutex);
            callback = handler;
        }

        if (! callback)
            return false;

        callback (command);
        return true;
    }

private:
    std::mutex mutex;
    NowPlayingBridge::CommandHandler handler;
};

struct Registration
{
    MPRemoteCommand* __strong command = nil;
    id __strong token = nil;
};

class MacNowPlayingBridge final : public NowPlayingBridge
{
public:
    MacNowPlayingBridge()
        : commandState (std::make_shared<CommandState>())
    {
        @autoreleasepool
        {
            auto* center = [MPRemoteCommandCenter sharedCommandCenter];
            addCommand (center.playCommand,
                        RemoteTransportCommandType::play);
            addCommand (center.pauseCommand,
                        RemoteTransportCommandType::pause);
            addCommand (center.togglePlayPauseCommand,
                        RemoteTransportCommandType::togglePlayPause);
            addCommand (center.nextTrackCommand,
                        RemoteTransportCommandType::next);
            addCommand (center.previousTrackCommand,
                        RemoteTransportCommandType::previous);
            addPositionCommand (center.changePlaybackPositionCommand);

            center.stopCommand.enabled = NO;
            center.skipForwardCommand.enabled = NO;
            center.skipBackwardCommand.enabled = NO;
            center.seekForwardCommand.enabled = NO;
            center.seekBackwardCommand.enabled = NO;
            center.changePlaybackRateCommand.enabled = NO;
        }
    }

    ~MacNowPlayingBridge() override
    {
        setCommandHandler ({});

        @autoreleasepool
        {
            for (const auto& registration : registrations)
                if (registration.command != nil
                    && registration.token != nil)
                    [registration.command removeTarget:registration.token];
            registrations.clear();
            clear();
        }
    }

    void setCommandHandler (CommandHandler handler) override
    {
        commandState->setHandler (std::move (handler));
    }

    void publish (const NowPlayingSnapshot& snapshot) override
    {
        @autoreleasepool
        {
            if (! snapshot.hasTrack)
            {
                clear();
                return;
            }

            NSMutableDictionary<NSString*, id>* info =
                [NSMutableDictionary dictionary];
            addString (info, MPMediaItemPropertyTitle, snapshot.title);
            addString (info, MPMediaItemPropertyArtist, snapshot.artist);
            addString (info, MPMediaItemPropertyAlbumTitle, snapshot.album);
            addString (info, MPNowPlayingInfoPropertyExternalContentIdentifier,
                       snapshot.trackId);
            info[MPMediaItemPropertyPlaybackDuration] =
                @(snapshot.durationSeconds);
            info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
                @(snapshot.elapsedSeconds);
            info[MPNowPlayingInfoPropertyPlaybackRate] =
                @(snapshot.playing ? 1.0 : 0.0);
            info[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @1.0;
            info[MPNowPlayingInfoPropertyMediaType] =
                @(MPNowPlayingInfoMediaTypeAudio);
            if (snapshot.queueIndex >= 0)
                info[MPNowPlayingInfoPropertyPlaybackQueueIndex] =
                    @(snapshot.queueIndex);
            if (snapshot.queueCount > 0)
                info[MPNowPlayingInfoPropertyPlaybackQueueCount] =
                    @(snapshot.queueCount);

            if (auto* artwork = createArtwork (snapshot.artworkPath))
                info[MPMediaItemPropertyArtwork] = artwork;

            auto* nowPlayingCenter =
                [MPNowPlayingInfoCenter defaultCenter];
            nowPlayingCenter.nowPlayingInfo = info;
            nowPlayingCenter.playbackState = snapshot.playing
                ? MPNowPlayingPlaybackStatePlaying
                : MPNowPlayingPlaybackStatePaused;
            updateCommandAvailability (snapshot);
        }
    }

    void clear() override
    {
        @autoreleasepool
        {
            auto* commandCenter =
                [MPRemoteCommandCenter sharedCommandCenter];
            commandCenter.playCommand.enabled = NO;
            commandCenter.pauseCommand.enabled = NO;
            commandCenter.togglePlayPauseCommand.enabled = NO;
            commandCenter.nextTrackCommand.enabled = NO;
            commandCenter.previousTrackCommand.enabled = NO;
            commandCenter.changePlaybackPositionCommand.enabled = NO;

            auto* nowPlayingCenter =
                [MPNowPlayingInfoCenter defaultCenter];
            nowPlayingCenter.nowPlayingInfo = nil;
            nowPlayingCenter.playbackState =
                MPNowPlayingPlaybackStateStopped;
        }
    }

private:
    std::shared_ptr<CommandState> commandState;
    std::vector<Registration> registrations;

    void addCommand (MPRemoteCommand* command,
                     RemoteTransportCommandType type)
    {
        const std::weak_ptr<CommandState> weakState = commandState;
        id token = [command addTargetWithHandler:
            ^MPRemoteCommandHandlerStatus (MPRemoteCommandEvent*)
            {
                if (auto state = weakState.lock())
                    return state->dispatch ({ type, 0.0 })
                               ? MPRemoteCommandHandlerStatusSuccess
                               : MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
                return MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
            }];
        registrations.push_back ({ command, token });
    }

    void addPositionCommand (MPChangePlaybackPositionCommand* command)
    {
        const std::weak_ptr<CommandState> weakState = commandState;
        id token = [command addTargetWithHandler:
            ^MPRemoteCommandHandlerStatus (MPRemoteCommandEvent* event)
            {
                if (! [event isKindOfClass:
                        [MPChangePlaybackPositionCommandEvent class]])
                    return MPRemoteCommandHandlerStatusCommandFailed;

                const auto position =
                    static_cast<MPChangePlaybackPositionCommandEvent*> (event)
                        .positionTime;
                if (! std::isfinite (position))
                    return MPRemoteCommandHandlerStatusCommandFailed;

                if (auto state = weakState.lock())
                    return state->dispatch ({
                               RemoteTransportCommandType::seekToSeconds,
                               position })
                               ? MPRemoteCommandHandlerStatusSuccess
                               : MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
                return MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
            }];
        registrations.push_back ({ command, token });
    }

    static void addString (NSMutableDictionary<NSString*, id>* info,
                           NSString* key,
                           const juce::String& value)
    {
        if (value.isNotEmpty())
            if (auto* text = toNSString (value))
                info[key] = text;
    }

    static MPMediaItemArtwork* createArtwork (
        const juce::String& artworkPath)
    {
        if (artworkPath.isEmpty())
            return nil;
        auto* path = toNSString (artworkPath);
        auto* image = path != nil
                          ? [[NSImage alloc] initWithContentsOfFile:path]
                          : nil;
        if (image == nil || image.size.width <= 0.0
            || image.size.height <= 0.0)
            return nil;

        return [[MPMediaItemArtwork alloc]
            initWithBoundsSize:image.size
            requestHandler:^NSImage* (NSSize) { return image; }];
    }

    static void updateCommandAvailability (
        const NowPlayingSnapshot& snapshot)
    {
        auto* center = [MPRemoteCommandCenter sharedCommandCenter];
        center.playCommand.enabled = snapshot.canPlay;
        center.pauseCommand.enabled = snapshot.canPlay;
        center.togglePlayPauseCommand.enabled = snapshot.canPlay;
        center.nextTrackCommand.enabled = snapshot.canGoNext;
        center.previousTrackCommand.enabled = snapshot.canGoPrevious;
        center.changePlaybackPositionCommand.enabled = snapshot.canSeek;
    }
};
} // namespace

std::unique_ptr<NowPlayingBridge> createSystemNowPlayingBridge()
{
    return std::make_unique<MacNowPlayingBridge>();
}
