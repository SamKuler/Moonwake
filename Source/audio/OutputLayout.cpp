#include "OutputLayout.h"

juce::String toString (OutputLayout layout)
{
    switch (layout)
    {
        case OutputLayout::stereo: return "stereo";
        case OutputLayout::fiveOne: return "5.1";
        case OutputLayout::fiveOneTwo: return "5.1.2";
        case OutputLayout::sevenOneTwo: return "7.1.2";
    }

    return "stereo";
}

OutputLayout outputLayoutFromString (const juce::String& value)
{
    if (value == "5.1")
        return OutputLayout::fiveOne;

    if (value == "5.1.2")
        return OutputLayout::fiveOneTwo;

    if (value == "7.1.2")
        return OutputLayout::sevenOneTwo;

    return OutputLayout::stereo;
}

juce::AudioChannelSet toAudioChannelSet (OutputLayout layout)
{
    switch (layout)
    {
        case OutputLayout::fiveOne: return juce::AudioChannelSet::create5point1();
        case OutputLayout::fiveOneTwo: return juce::AudioChannelSet::create5point1point2();
        case OutputLayout::sevenOneTwo: return juce::AudioChannelSet::create7point1point2();
        case OutputLayout::stereo: return juce::AudioChannelSet::stereo();
    }

    return juce::AudioChannelSet::stereo();
}

int getChannelCount (OutputLayout layout)
{
    return toAudioChannelSet (layout).size();
}
