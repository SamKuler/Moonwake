#pragma once

#include "OutputLayout.h"

#include <algorithm>
#include <cmath>
#include <vector>

struct OutputChannelMix
{
    juce::AudioChannelSet::ChannelType channel = juce::AudioChannelSet::unknown;
    float gain = 1.0f;
    bool muted = false;
    bool solo = false;

    bool operator== (const OutputChannelMix& other) const
    {
        return channel == other.channel && juce::exactlyEqual (gain, other.gain)
            && muted == other.muted && solo == other.solo;
    }
};

inline bool isValidOutputChannelMix (const OutputChannelMix& mix)
{
    if (! std::isfinite (mix.gain) || mix.gain < 0.0f || mix.gain > 2.0f
        || mix.channel == juce::AudioChannelSet::unknown)
        return false;
    for (const auto& layout : outputLayouts())
        if (layout.channels.getChannelIndexForType (mix.channel) >= 0)
            return true;
    return false;
}

inline std::vector<OutputChannelMix> defaultOutputChannelMix()
{
    std::vector<OutputChannelMix> result;
    for (const auto& layout : outputLayouts())
        for (const auto channel : layout.channels.getChannelTypes())
            if (std::none_of (result.begin(), result.end(), [channel] (const auto& mix)
                { return mix.channel == channel; }))
                result.push_back ({ channel });
    return result;
}
