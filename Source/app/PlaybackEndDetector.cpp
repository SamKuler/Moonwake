#include "PlaybackEndDetector.h"

bool PlaybackEndDetector::update (bool isPlaying, double positionSeconds, double lengthSeconds)
{
    const auto reachedEnd = lengthSeconds > 0.0 && positionSeconds >= lengthSeconds - 0.05;
    const auto shouldAdvance = wasPlaying && ! isPlaying && reachedEnd;

    wasPlaying = isPlaying;
    return shouldAdvance;
}

void PlaybackEndDetector::reset()
{
    wasPlaying = false;
}
