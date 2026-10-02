#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <span>

inline constexpr int maximumOutputChannels = 16;

enum class OutputLayout
{
    stereo,
    fiveOne,
    fiveOneTwo,
    sevenOneTwo,
    mono,
    twoOne,
    quad,
    lcr,
    fiveZero,
    sixOne,
    sevenZero,
    sevenOne,
    fiveOneFour,
    sevenOneFour,
    sevenOneSix,
    nineOneFour,
    nineOneSix
};

struct OutputLayoutDescription
{
    OutputLayout layout;
    const char* id;
    const char* name;
    juce::AudioChannelSet channels;
};

std::span<const OutputLayoutDescription> outputLayouts();
juce::String toString (OutputLayout layout);
juce::String outputLayoutDisplayName (OutputLayout layout);
OutputLayout outputLayoutFromString (const juce::String& value);
juce::AudioChannelSet toAudioChannelSet (OutputLayout layout);
int getChannelCount (OutputLayout layout);
