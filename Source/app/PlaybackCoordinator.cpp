#include "PlaybackCoordinator.h"

PlaybackAction PlaybackCoordinator::togglePlay (const juce::String& selectedTrackId)
{
    if (selectedTrackId.isEmpty())
        return {};

    if (loadedTrackId != selectedTrackId)
        return loadTrack (selectedTrackId, true);

    if (intent == TransportIntent::playing)
    {
        intent = TransportIntent::paused;
        return { PlaybackActionType::pause, loadedTrackId };
    }

    intent = TransportIntent::playing;
    return { PlaybackActionType::play, loadedTrackId };
}

PlaybackAction PlaybackCoordinator::selectionChanged (const juce::String& selectedTrackId)
{
    if (selectedTrackId.isEmpty() || selectedTrackId == loadedTrackId)
        return {};

    return loadTrack (selectedTrackId, intent == TransportIntent::playing);
}

PlaybackAction PlaybackCoordinator::moveToTrack (const juce::String& selectedTrackId, bool forcePlay)
{
    if (selectedTrackId.isEmpty())
        return {};

    const auto shouldPlay = forcePlay || intent == TransportIntent::playing;

    if (selectedTrackId == loadedTrackId)
    {
        if (shouldPlay && intent != TransportIntent::playing)
        {
            intent = TransportIntent::playing;
            return { PlaybackActionType::play, loadedTrackId };
        }

        return {};
    }

    return loadTrack (selectedTrackId, shouldPlay);
}

PlaybackAction PlaybackCoordinator::preparePausedTrack (
    const juce::String& trackId)
{
    if (trackId.isEmpty())
        return {};

    return loadTrack (trackId, false);
}

PlaybackAction PlaybackCoordinator::previous (const juce::String& previousTrackId,
                                                double currentPositionSeconds)
{
    if (loadedTrackId.isNotEmpty() && currentPositionSeconds >= 3.0)
        return { PlaybackActionType::seekToStart, loadedTrackId };

    if (previousTrackId.isNotEmpty())
        return moveToTrack (previousTrackId, false);

    if (loadedTrackId.isNotEmpty())
        return { PlaybackActionType::seekToStart, loadedTrackId };

    return {};
}

PlaybackAction PlaybackCoordinator::playbackFinished (const juce::String& nextTrackId)
{
    if (nextTrackId.isNotEmpty())
        return loadTrack (nextTrackId, true);

    intent = TransportIntent::stopped;
    loadedTrackId.clear();
    return { PlaybackActionType::stop, {} };
}

void PlaybackCoordinator::loadFailed()
{
    intent = TransportIntent::stopped;
    loadedTrackId.clear();
}

TransportIntent PlaybackCoordinator::getIntent() const
{
    return intent;
}

const juce::String& PlaybackCoordinator::getLoadedTrackId() const
{
    return loadedTrackId;
}

PlaybackAction PlaybackCoordinator::loadTrack (const juce::String& trackId, bool shouldPlay)
{
    loadedTrackId = trackId;
    intent = shouldPlay ? TransportIntent::playing : TransportIntent::paused;
    return { shouldPlay ? PlaybackActionType::loadAndPlay : PlaybackActionType::load, trackId };
}
