#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "audio/OutputLayout.h"

namespace
{
class OutputLayoutTests final : public juce::UnitTest
{
public:
    OutputLayoutTests()
        : UnitTest ("OutputLayout", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("maps player layouts to exact JUCE speaker sets");
        {
            expect (toAudioChannelSet (OutputLayout::stereo) == juce::AudioChannelSet::stereo());
            expect (toAudioChannelSet (OutputLayout::fiveOne) == juce::AudioChannelSet::create5point1());
            expect (toAudioChannelSet (OutputLayout::fiveOneTwo) == juce::AudioChannelSet::create5point1point2());
            expect (toAudioChannelSet (OutputLayout::sevenOneTwo) == juce::AudioChannelSet::create7point1point2());
            expectEquals (getChannelCount (OutputLayout::stereo), 2);
            expectEquals (getChannelCount (OutputLayout::fiveOne), 6);
            expectEquals (getChannelCount (OutputLayout::fiveOneTwo), 8);
            expectEquals (getChannelCount (OutputLayout::sevenOneTwo), 10);
        }
    }
};

OutputLayoutTests outputLayoutTests;
} // namespace
