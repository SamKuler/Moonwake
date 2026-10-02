#include "OutputLayout.h"

#include <array>

std::span<const OutputLayoutDescription> outputLayouts()
{
    using Set = juce::AudioChannelSet;
    static const std::array values {
        OutputLayoutDescription { OutputLayout::mono, "mono", "Mono", Set::mono() },
        OutputLayoutDescription { OutputLayout::stereo, "stereo", "Stereo", Set::stereo() },
        OutputLayoutDescription { OutputLayout::twoOne, "2.1", "2.1", Set::channelSetWithChannels ({ Set::left, Set::right, Set::LFE }) },
        OutputLayoutDescription { OutputLayout::lcr, "3.0", "LCR", Set::createLCR() },
        OutputLayoutDescription { OutputLayout::quad, "quad", "Quad", Set::quadraphonic() },
        OutputLayoutDescription { OutputLayout::fiveZero, "5.0", "5.0", Set::create5point0() },
        OutputLayoutDescription { OutputLayout::fiveOne, "5.1", "5.1", Set::create5point1() },
        OutputLayoutDescription { OutputLayout::sixOne, "6.1", "6.1", Set::create6point1() },
        OutputLayoutDescription { OutputLayout::sevenZero, "7.0", "7.0", Set::create7point0() },
        OutputLayoutDescription { OutputLayout::sevenOne, "7.1", "7.1", Set::create7point1() },
        OutputLayoutDescription { OutputLayout::fiveOneTwo, "5.1.2", "5.1.2", Set::create5point1point2() },
        OutputLayoutDescription { OutputLayout::fiveOneFour, "5.1.4", "5.1.4", Set::create5point1point4() },
        OutputLayoutDescription { OutputLayout::sevenOneTwo, "7.1.2", "7.1.2", Set::create7point1point2() },
        OutputLayoutDescription { OutputLayout::sevenOneFour, "7.1.4", "7.1.4", Set::create7point1point4() },
        OutputLayoutDescription { OutputLayout::sevenOneSix, "7.1.6", "7.1.6", Set::create7point1point6() },
        OutputLayoutDescription { OutputLayout::nineOneFour, "9.1.4", "9.1.4", Set::create9point1point4() },
        OutputLayoutDescription { OutputLayout::nineOneSix, "9.1.6", "9.1.6", Set::create9point1point6() }
    };
    return values;
}

juce::String toString (OutputLayout layout)
{
    for (const auto& value : outputLayouts())
        if (value.layout == layout) return value.id;
    return "stereo";
}

juce::String outputLayoutDisplayName (OutputLayout layout)
{
    for (const auto& value : outputLayouts())
        if (value.layout == layout) return value.name;
    return "Stereo";
}

OutputLayout outputLayoutFromString (const juce::String& value)
{
    for (const auto& description : outputLayouts())
        if (value == description.id) return description.layout;
    return OutputLayout::stereo;
}

juce::AudioChannelSet toAudioChannelSet (OutputLayout layout)
{
    for (const auto& value : outputLayouts())
        if (value.layout == layout) return value.channels;
    return juce::AudioChannelSet::stereo();
}

int getChannelCount (OutputLayout layout)
{
    return toAudioChannelSet (layout).size();
}
