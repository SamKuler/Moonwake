#pragma once

#include <juce_core/juce_core.h>

struct TrackItem
{
    TrackItem() = default;

    TrackItem (juce::File fileToUse, juce::String titleToUse, double durationSecondsToUse)
        : file (std::move (fileToUse)),
          title (std::move (titleToUse)),
          durationSeconds (durationSecondsToUse)
    {
    }

    juce::File file;
    juce::String id { juce::Uuid().toString() };
    juce::String title;
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    int channelCount = 0;
    bool available = true;
    juce::String reservedEffectChainState;
};
