#pragma once

class PlaybackEndDetector
{
public:
    [[nodiscard]] bool update (bool isPlaying, double positionSeconds, double lengthSeconds);
    void reset();

private:
    bool wasPlaying = false;
};
