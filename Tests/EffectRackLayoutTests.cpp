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
        beginTest ("Drop y maps to the nearest bounded effect row");
        {
            expectEquals (calculateEffectDropIndex (-20, 44, 8, 3), 0);
            expectEquals (calculateEffectDropIndex (0, 44, 8, 3), 0);
            expectEquals (calculateEffectDropIndex (51, 44, 8, 3), 0);
            expectEquals (calculateEffectDropIndex (52, 44, 8, 3), 1);
            expectEquals (calculateEffectDropIndex (104, 44, 8, 3), 2);
            expectEquals (calculateEffectDropIndex (999, 44, 8, 3), 2);
            expectEquals (calculateEffectDropIndex (40, 44, 8, 0), -1);
        }
    }
};

EffectRackLayoutTests effectRackLayoutTests;
