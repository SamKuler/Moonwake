#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// Empty selects the complete output bus. Explicit entries map, in order,
// to the mono or stereo plug-in's input and output channels.
using EffectScope = std::vector<juce::AudioChannelSet::ChannelType>;

inline bool validateEffectScope (const EffectScope& scope,
                                  const juce::AudioChannelSet& outputLayout,
                                  juce::String& error)
{
    error.clear();
    if (scope.size() > 2)
    {
        error = "Choose one channel, a channel pair, or the complete output bus.";
        return false;
    }
    if (scope.size() == 2 && scope[0] == scope[1])
    {
        error = "An effect channel pair must contain two different channels.";
        return false;
    }
    for (const auto channel : scope)
        if (channel == juce::AudioChannelSet::unknown
            || outputLayout.getChannelIndexForType (channel) < 0)
        {
            const auto label = juce::AudioChannelSet::getAbbreviatedChannelTypeName (channel);
            error = "Effect channel " + (label.isNotEmpty() ? label : juce::String (static_cast<int> (channel)))
                + " is not present in the " + outputLayout.getDescription() + " output layout.";
            return false;
        }
    return true;
}
