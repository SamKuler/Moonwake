#include <juce_core/juce_core.h>

#include "plugins/EffectChainState.h"

namespace
{
class EffectChainStateTests final : public juce::UnitTest
{
public:
    EffectChainStateTests()
        : UnitTest ("EffectChainState", "plugins")
    {
    }

    void runTest() override
    {
        beginTest ("adds plugin slots with stable IDs");
        {
            EffectChainState chain;

            const auto eqId = chain.addSlot ("EQ", "vst3:eq");
            const auto compressorId = chain.addSlot ("Compressor", "vst3:compressor");

            expectEquals (chain.size(), 2);
            expect (eqId != compressorId);
            expectEquals (chain.getSlot (0)->id, eqId);
            expectEquals (chain.getSlot (1)->pluginName, juce::String ("Compressor"));
        }

        beginTest ("toggles bypass by slot ID");
        {
            EffectChainState chain;
            const auto id = chain.addSlot ("EQ", "vst3:eq");

            expect (chain.setBypassed (id, true));
            expect (chain.getSlotById (id)->bypassed);
            expect (chain.setBypassed (id, false));
            expect (! chain.getSlotById (id)->bypassed);
            expect (! chain.setBypassed (999, true));
        }

        beginTest ("moves slots without changing IDs");
        {
            EffectChainState chain;
            const EffectScope scope { juce::AudioChannelSet::centre };
            const auto first = chain.addSlot ("EQ", "vst3:eq", "VST3", scope);
            const auto second = chain.addSlot ("Delay", "vst3:delay");
            const auto third = chain.addSlot ("Limiter", "vst3:limiter");

            expect (chain.moveSlot (2, 0));
            expectEquals (chain.getSlot (0)->id, third);
            expectEquals (chain.getSlot (1)->id, first);
            expect (chain.getSlot (1)->scope == scope);
            expectEquals (chain.getSlot (2)->id, second);
            expect (! chain.moveSlot (5, 0));
        }

        beginTest ("removes slots by ID");
        {
            EffectChainState chain;
            const auto first = chain.addSlot ("EQ", "vst3:eq");
            const auto second = chain.addSlot ("Delay", "vst3:delay");

            expect (chain.removeSlot (first));
            expectEquals (chain.size(), 1);
            expectEquals (chain.getSlot (0)->id, second);
            expect (chain.getSlotById (first) == nullptr);
            expect (! chain.removeSlot (first));
        }

        beginTest ("restores stable slot IDs and advances the allocator");
        {
            EffectChainState chain;
            const auto restored = chain.addSlotWithId (42, "EQ", "AudioUnit", "au:eq");
            const auto added = chain.addSlot ("Delay", "vst3:delay", "VST3");

            expectEquals (restored, 42);
            expectEquals (added, 43);
            expectEquals (chain.getSlot (0)->pluginFormat, juce::String ("AudioUnit"));
            expect (! chain.addSlotWithId (42, "Duplicate", "VST3", "duplicate"));
        }

        beginTest ("retains unavailable slots and diagnostic text");
        {
            EffectChainState chain;
            const auto id = chain.addSlot ("Missing EQ", "missing:eq", "VST3");

            expect (chain.setAvailability (id, false, "Plug-in is not installed"));
            expect (! chain.getSlotById (id)->available);
            expectEquals (chain.getSlotById (id)->errorMessage, juce::String ("Plug-in is not installed"));
        }
    }
};

EffectChainStateTests effectChainStateTests;
} // namespace
