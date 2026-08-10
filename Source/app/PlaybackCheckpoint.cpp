#include "PlaybackCheckpoint.h"

#include <cmath>

namespace
{
double sanitizedPosition (double positionSeconds)
{
    return std::isfinite (positionSeconds)
               ? juce::jmax (0.0, positionSeconds)
               : 0.0;
}
} // namespace

bool PlaybackCheckpoint::observe (const juce::String& trackId,
                                  double positionSeconds,
                                  bool isPlaying)
{
    positionSeconds = sanitizedPosition (positionSeconds);

    if (trackId.isEmpty())
    {
        reset ({}, 0.0);
        return false;
    }

    if (trackId != checkpointedTrackId)
    {
        reset (trackId, positionSeconds);
        return true;
    }

    if (! isPlaying
        || std::abs (positionSeconds - checkpointedPositionSeconds)
               < intervalSeconds)
        return false;

    checkpointedPositionSeconds = positionSeconds;
    return true;
}

void PlaybackCheckpoint::reset (const juce::String& trackId,
                                double positionSeconds)
{
    checkpointedTrackId = trackId;
    checkpointedPositionSeconds = sanitizedPosition (positionSeconds);
}

std::optional<double> PlaybackCheckpoint::positionForTrack (
    const juce::String& trackId) const
{
    if (trackId.isEmpty() || trackId != checkpointedTrackId)
        return std::nullopt;

    return checkpointedPositionSeconds;
}
