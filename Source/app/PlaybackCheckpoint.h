#pragma once

#include <juce_core/juce_core.h>

#include <optional>

class PlaybackCheckpoint
{
public:
    [[nodiscard]] bool observe (const juce::String& trackId,
                                double positionSeconds,
                                bool isPlaying);
    void reset (const juce::String& trackId, double positionSeconds);
    [[nodiscard]] std::optional<double> positionForTrack (
        const juce::String& trackId) const;

private:
    static constexpr double intervalSeconds = 15.0;

    juce::String checkpointedTrackId;
    double checkpointedPositionSeconds = 0.0;
};
