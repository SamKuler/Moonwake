#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

enum class RemoteTransportCommandType
{
    play,
    pause,
    togglePlayPause,
    next,
    previous,
    seekToSeconds
};

struct RemoteTransportCommand
{
    RemoteTransportCommandType type = RemoteTransportCommandType::togglePlayPause;
    double positionSeconds = 0.0;
};

struct NowPlayingSnapshot
{
    juce::String trackId;
    juce::String title;
    juce::String artist;
    juce::String album;
    juce::String artworkPath;
    int queueIndex = -1;
    int queueCount = 0;
    double elapsedSeconds = 0.0;
    double durationSeconds = 0.0;
    bool hasTrack = false;
    bool playing = false;
    bool canPlay = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
    bool canSeek = false;
};

class NowPlayingBridge
{
public:
    using CommandHandler = std::function<void (const RemoteTransportCommand&)>;

    virtual ~NowPlayingBridge() = default;
    virtual void setCommandHandler (CommandHandler) = 0;
    virtual void publish (const NowPlayingSnapshot&) = 0;
    virtual void clear() = 0;
};

[[nodiscard]] std::unique_ptr<NowPlayingBridge> createSystemNowPlayingBridge();
