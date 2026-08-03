#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "audio/ChannelRouter.h"

namespace
{
void fillChannel (juce::AudioBuffer<float>& buffer, int channel, float value)
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        buffer.setSample (channel, sample, value);
}

class ChannelRouterTests final : public juce::UnitTest
{
public:
    ChannelRouterTests()
        : UnitTest ("ChannelRouter", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("stereo maps only to surround front left and right");
        {
            const auto sourceLayout = juce::AudioChannelSet::stereo();
            const auto destinationLayout = juce::AudioChannelSet::create5point1point2();
            juce::AudioBuffer<float> source (sourceLayout.size(), 8);
            juce::AudioBuffer<float> destination (destinationLayout.size(), 8);
            fillChannel (source, 0, 1.0f);
            fillChannel (source, 1, 2.0f);
            destination.clear();

            ChannelRouter::route (source, sourceLayout, destination, destinationLayout, 0, 8);

            expectWithinAbsoluteError (destination.getSample (
                                           destinationLayout.getChannelIndexForType (juce::AudioChannelSet::left), 0),
                                       1.0f,
                                       0.0001f);
            expectWithinAbsoluteError (destination.getSample (
                                           destinationLayout.getChannelIndexForType (juce::AudioChannelSet::right), 0),
                                       2.0f,
                                       0.0001f);
            for (int channel = 2; channel < destination.getNumChannels(); ++channel)
                expectWithinAbsoluteError (destination.getSample (channel, 0), 0.0f, 0.0001f);
        }

        beginTest ("mono uses centre when available and equal-power stereo otherwise");
        {
            const auto mono = juce::AudioChannelSet::mono();
            juce::AudioBuffer<float> source (1, 4);
            fillChannel (source, 0, 1.0f);

            const auto surround = juce::AudioChannelSet::create5point1();
            juce::AudioBuffer<float> surroundDestination (surround.size(), 4);
            ChannelRouter::route (source, mono, surroundDestination, surround, 0, 4);
            expectWithinAbsoluteError (surroundDestination.getSample (
                                           surround.getChannelIndexForType (juce::AudioChannelSet::centre), 0),
                                       1.0f,
                                       0.0001f);

            const auto stereo = juce::AudioChannelSet::stereo();
            juce::AudioBuffer<float> stereoDestination (2, 4);
            ChannelRouter::route (source, mono, stereoDestination, stereo, 0, 4);
            constexpr auto equalPower = 0.70710678f;
            expectWithinAbsoluteError (stereoDestination.getSample (0, 0), equalPower, 0.0001f);
            expectWithinAbsoluteError (stereoDestination.getSample (1, 0), equalPower, 0.0001f);
        }

        beginTest ("native surround channels retain their speaker roles");
        {
            const auto sourceLayout = juce::AudioChannelSet::create5point1();
            const auto destinationLayout = juce::AudioChannelSet::create7point1point2();
            juce::AudioBuffer<float> source (sourceLayout.size(), 4);
            juce::AudioBuffer<float> destination (destinationLayout.size(), 4);
            for (int channel = 0; channel < source.getNumChannels(); ++channel)
                fillChannel (source, channel, static_cast<float> (channel + 1));

            ChannelRouter::route (source, sourceLayout, destination, destinationLayout, 0, 4);

            for (int channel = 0; channel < sourceLayout.size(); ++channel)
            {
                const auto type = sourceLayout.getTypeOfChannel (channel);
                auto destinationChannel = destinationLayout.getChannelIndexForType (type);
                if (destinationChannel < 0 && type == juce::AudioChannelSet::leftSurround)
                    destinationChannel = destinationLayout.getChannelIndexForType (
                        juce::AudioChannelSet::leftSurroundSide);
                if (destinationChannel < 0 && type == juce::AudioChannelSet::rightSurround)
                    destinationChannel = destinationLayout.getChannelIndexForType (
                        juce::AudioChannelSet::rightSurroundSide);
                expect (destinationChannel >= 0);
                if (destinationChannel >= 0)
                    expectWithinAbsoluteError (destination.getSample (destinationChannel, 0),
                                               static_cast<float> (channel + 1),
                                               0.0001f);
            }
        }

        beginTest ("5.1 downmix reaches stereo without clipping or dropping dialogue");
        {
            const auto sourceLayout = juce::AudioChannelSet::create5point1();
            const auto destinationLayout = juce::AudioChannelSet::stereo();
            juce::AudioBuffer<float> source (sourceLayout.size(), 4);
            juce::AudioBuffer<float> destination (destinationLayout.size(), 4);

            for (int sourceChannel = 0; sourceChannel < sourceLayout.size(); ++sourceChannel)
            {
                source.clear();
                destination.clear();
                fillChannel (source, sourceChannel, 1.0f);
                ChannelRouter::route (source, sourceLayout, destination, destinationLayout, 0, 4);
                expect (destination.getMagnitude (0, destination.getNumSamples()) > 0.0f,
                        "Source channel " + juce::String (sourceChannel) + " was dropped");
            }

            for (int sourceChannel = 0; sourceChannel < sourceLayout.size(); ++sourceChannel)
                fillChannel (source, sourceChannel, 1.0f);
            destination.clear();
            ChannelRouter::route (source, sourceLayout, destination, destinationLayout, 0, 4);
            for (int channel = 0; channel < destination.getNumChannels(); ++channel)
                expect (destination.getMagnitude (channel, 0, 4) <= 1.0001f);
        }

        for (const auto& destinationLayout : { juce::AudioChannelSet::create5point1point2(),
                                               juce::AudioChannelSet::create5point1() })
        {
            beginTest ("7.1.2 folds every channel into " + destinationLayout.getDescription());
            const auto sourceLayout = juce::AudioChannelSet::create7point1point2();
            juce::AudioBuffer<float> source (sourceLayout.size(), 4);
            juce::AudioBuffer<float> destination (destinationLayout.size(), 4);

            for (int sourceChannel = 0; sourceChannel < sourceLayout.size(); ++sourceChannel)
            {
                source.clear();
                destination.clear();
                fillChannel (source, sourceChannel, 1.0f);
                ChannelRouter::route (source, sourceLayout, destination, destinationLayout, 0, 4);
                expect (destination.getMagnitude (0, destination.getNumSamples()) > 0.0f,
                        "Source channel " + juce::String (sourceChannel) + " was dropped");
            }

            for (int sourceChannel = 0; sourceChannel < sourceLayout.size(); ++sourceChannel)
                fillChannel (source, sourceChannel, 1.0f);
            destination.clear();
            ChannelRouter::route (source, sourceLayout, destination, destinationLayout, 0, 4);
            for (int channel = 0; channel < destination.getNumChannels(); ++channel)
                expect (destination.getMagnitude (channel, 0, 4) <= 1.0001f);
        }
    }
};

ChannelRouterTests channelRouterTests;
} // namespace
