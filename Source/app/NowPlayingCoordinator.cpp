#include "NowPlayingCoordinator.h"

#include <cmath>

namespace
{
double finiteNonNegative (double value)
{
    return std::isfinite (value) ? juce::jmax (0.0, value) : 0.0;
}

bool samePresentation (const NowPlayingSnapshot& left,
                       const NowPlayingSnapshot& right)
{
    return left.trackId == right.trackId
        && left.title == right.title
        && left.artist == right.artist
        && left.album == right.album
        && left.artworkPath == right.artworkPath
        && left.queueIndex == right.queueIndex
        && left.queueCount == right.queueCount
        && juce::approximatelyEqual (left.durationSeconds,
                                     right.durationSeconds)
        && left.hasTrack == right.hasTrack
        && left.playing == right.playing
        && left.canPlay == right.canPlay
        && left.canGoNext == right.canGoNext
        && left.canGoPrevious == right.canGoPrevious
        && left.canSeek == right.canSeek;
}
} // namespace

NowPlayingCoordinator::NowPlayingCoordinator (
    std::unique_ptr<NowPlayingBridge> bridgeToUse)
    : bridge (std::move (bridgeToUse))
{
    jassert (bridge != nullptr);
}

void NowPlayingCoordinator::setCommandHandler (
    NowPlayingBridge::CommandHandler handler)
{
    if (bridge != nullptr)
        bridge->setCommandHandler (std::move (handler));
}

void NowPlayingCoordinator::update (const PlayerViewState& state,
                                    double monotonicSeconds,
                                    bool acceptDeferredCommands)
{
    if (bridge == nullptr)
        return;

    const auto snapshot = project (state, acceptDeferredCommands);
    monotonicSeconds = finiteNonNegative (monotonicSeconds);
    if (! shouldPublish (snapshot, monotonicSeconds))
        return;

    bridge->publish (snapshot);
    lastPublished = snapshot;
    lastPublishedAt = monotonicSeconds;
}

void NowPlayingCoordinator::clear()
{
    if (bridge != nullptr)
        bridge->clear();
    lastPublished.reset();
    lastPublishedAt = 0.0;
}

NowPlayingSnapshot NowPlayingCoordinator::project (const PlayerViewState& state,
                                                   bool acceptDeferredCommands)
{
    NowPlayingSnapshot result;
    result.trackId = state.selectedTrackId;
    result.title = state.selectedTrackTitle;
    result.artist = state.selectedTrackArtist;
    result.album = state.selectedTrackAlbum;
    result.artworkPath = state.selectedTrackArtworkPath;
    result.queueIndex = state.selectedIndex;
    result.queueCount = juce::jmax (0, state.playlistSize);
    result.hasTrack = state.selectedIndex >= 0 && state.selectedTrackId.isNotEmpty();
    result.playing = result.hasTrack && state.isPlaying;
    result.canPlay = acceptDeferredCommands || state.canPlay;
    result.canGoPrevious = acceptDeferredCommands || state.canGoPrevious;
    result.canGoNext = acceptDeferredCommands || state.canGoNext;
    result.durationSeconds = finiteNonNegative (juce::jmax (
        state.lengthSeconds, state.selectedTrackDurationSeconds));
    result.elapsedSeconds = finiteNonNegative (state.positionSeconds);
    if (result.durationSeconds > 0.0)
        result.elapsedSeconds = juce::jmin (result.elapsedSeconds,
                                            result.durationSeconds);
    result.canSeek = result.hasTrack && state.selectedTrackAvailable
        && result.durationSeconds > 0.0;
    return result;
}

bool NowPlayingCoordinator::shouldPublish (
    const NowPlayingSnapshot& snapshot,
    double monotonicSeconds) const
{
    if (! lastPublished.has_value())
        return true;

    if (! samePresentation (*lastPublished, snapshot))
        return true;

    if (! snapshot.hasTrack)
        return false;

    if (! snapshot.playing)
        return std::abs (snapshot.elapsedSeconds
                         - lastPublished->elapsedSeconds) >= 0.25;

    const auto elapsedSincePublication = juce::jmax (
        0.0, monotonicSeconds - lastPublishedAt);
    const auto systemPosition = lastPublished->elapsedSeconds
                              + elapsedSincePublication;
    return std::abs (snapshot.elapsedSeconds - systemPosition) >= 1.0;
}
