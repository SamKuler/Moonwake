#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class ChannelRouter
{
public:
    static void route (const juce::AudioBuffer<float>& source,
                       const juce::AudioChannelSet& sourceLayout,
                       juce::AudioBuffer<float>& destination,
                       const juce::AudioChannelSet& destinationLayout,
                       int destinationStartSample,
                       int numSamples);
};
