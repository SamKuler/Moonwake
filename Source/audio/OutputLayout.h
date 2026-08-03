#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

enum class OutputLayout
{
    stereo,
    fiveOne,
    fiveOneTwo,
    sevenOneTwo
};

juce::String toString (OutputLayout layout);
OutputLayout outputLayoutFromString (const juce::String& value);
juce::AudioChannelSet toAudioChannelSet (OutputLayout layout);
int getChannelCount (OutputLayout layout);
