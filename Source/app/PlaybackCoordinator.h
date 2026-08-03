#pragma once

#include <juce_core/juce_core.h>

enum class TransportIntent
{
    stopped,
    paused,
    playing
};

enum class PlaybackActionType
{
    none,
    load,
    loadAndPlay,
    play,
    pause,
    seekToStart,
    stop
};

struct PlaybackAction
{
    PlaybackActionType type = PlaybackActionType::none;
    juce::String trackId;
};

class PlaybackCoordinator
{
public:
    [[nodiscard]] PlaybackAction togglePlay (const juce::String& selectedTrackId);
    [[nodiscard]] PlaybackAction selectionChanged (const juce::String& selectedTrackId);
    [[nodiscard]] PlaybackAction moveToTrack (const juce::String& selectedTrackId, bool forcePlay);
    [[nodiscard]] PlaybackAction previous (const juce::String& previousTrackId, double currentPositionSeconds);
    [[nodiscard]] PlaybackAction playbackFinished (const juce::String& nextTrackId);

    void loadFailed();

    [[nodiscard]] TransportIntent getIntent() const;
    [[nodiscard]] const juce::String& getLoadedTrackId() const;

private:
    TransportIntent intent = TransportIntent::stopped;
    juce::String loadedTrackId;

    [[nodiscard]] PlaybackAction loadTrack (const juce::String& trackId, bool shouldPlay);
};
