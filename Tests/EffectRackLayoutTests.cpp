#include <array>
#include <juce_core/juce_core.h>

#include "ui/EffectRackLayout.h"

class EffectRackLayoutTests final : public juce::UnitTest
{
public:
    EffectRackLayoutTests()
        : UnitTest ("EffectRackLayout", "ui")
    {
    }

    void runTest() override
    {
        beginTest ("expanded editors contribute their complete height to drop positions");
        const std::array<int, 3> heights { 364, 84, 52 };
        expectEquals (calculateEffectDropIndex (-20, heights, 8), 0);
        expectEquals (calculateEffectDropIndex (371, heights, 8), 0);
        expectEquals (calculateEffectDropIndex (372, heights, 8), 1);
        expectEquals (calculateEffectDropIndex (463, heights, 8), 1);
        expectEquals (calculateEffectDropIndex (464, heights, 8), 2);
        expectEquals (calculateEffectDropIndex (999, heights, 8), 2);
        expectEquals (calculateEffectDropIndex (40, {}, 8), -1);
    }
};

EffectRackLayoutTests effectRackLayoutTests;
