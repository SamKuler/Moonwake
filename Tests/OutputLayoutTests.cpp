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

        beginTest ("every supported layout has a stable unique identifier and exact channel count");
        {
            struct Expected { OutputLayout layout; const char* id; int channels; };
            const Expected expected[] {
                { OutputLayout::mono, "mono", 1 }, { OutputLayout::stereo, "stereo", 2 },
                { OutputLayout::twoOne, "2.1", 3 }, { OutputLayout::lcr, "3.0", 3 },
                { OutputLayout::quad, "quad", 4 }, { OutputLayout::fiveZero, "5.0", 5 },
                { OutputLayout::fiveOne, "5.1", 6 }, { OutputLayout::sixOne, "6.1", 7 },
                { OutputLayout::sevenZero, "7.0", 7 }, { OutputLayout::sevenOne, "7.1", 8 },
                { OutputLayout::fiveOneTwo, "5.1.2", 8 }, { OutputLayout::fiveOneFour, "5.1.4", 10 },
                { OutputLayout::sevenOneTwo, "7.1.2", 10 }, { OutputLayout::sevenOneFour, "7.1.4", 12 },
                { OutputLayout::sevenOneSix, "7.1.6", 14 }, { OutputLayout::nineOneFour, "9.1.4", 14 },
                { OutputLayout::nineOneSix, "9.1.6", 16 }
            };
            expectEquals (static_cast<int> (outputLayouts().size()), 17);
            expectEquals (maximumOutputChannels, 16);
            juce::StringArray ids;
            for (const auto& item : expected)
            {
                const auto id = toString (item.layout);
                expectEquals (id, juce::String (item.id));
                expect (! ids.contains (id), "Layout identifiers must be unique.");
                ids.add (id);
                expect (outputLayoutFromString (id) == item.layout);
                expectEquals (getChannelCount (item.layout), item.channels, id);
                expect (! toAudioChannelSet (item.layout).isDiscreteLayout(), id);
            }
            expect (outputLayoutFromString ("unsupported-layout") == OutputLayout::stereo);
        }

        beginTest ("large layouts distinguish side heights and wide speakers");
        {
            using Set = juce::AudioChannelSet;
            expect (toAudioChannelSet (OutputLayout::sevenOneFour) == Set::create7point1point4());
            expect (toAudioChannelSet (OutputLayout::sevenOneSix) == Set::create7point1point6());
            expect (toAudioChannelSet (OutputLayout::nineOneFour) == Set::create9point1point4());
            expect (toAudioChannelSet (OutputLayout::nineOneSix) == Set::create9point1point6());
            expect (toAudioChannelSet (OutputLayout::sevenOneSix) != toAudioChannelSet (OutputLayout::nineOneFour));
            const auto largest = toAudioChannelSet (OutputLayout::nineOneSix);
            for (const auto role : { Set::wideLeft, Set::wideRight, Set::topFrontLeft,
                                     Set::topFrontRight, Set::topSideLeft, Set::topSideRight,
                                     Set::topRearLeft, Set::topRearRight })
                expect (largest.getChannelIndexForType (role) >= 0);
        }
    }
};

OutputLayoutTests outputLayoutTests;
} // namespace
