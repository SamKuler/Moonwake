#include "ui/SkinEffectChainViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class SkinEffectChainViewportTests final : public juce::UnitTest
{
public:
    SkinEffectChainViewportTests() : UnitTest ("SkinEffectChainViewport", "skin") {}

    void runTest() override
    {
        beginTest ("revealing effect rows and nested controls uses content coordinates and preserves horizontal scroll");
        SkinEffectChainViewport viewport;
        juce::Component content, row;
        juce::TextButton button { "Editor" };
        viewport.setBounds (0, 0, 200, 120);
        viewport.setViewedComponent (&content, false);
        content.setSize (400, 800);
        content.addAndMakeVisible (row);
        row.setBounds (0, 600, 380, 64);
        row.addAndMakeVisible (button);
        button.setBounds (12, 16, 80, 32);
        viewport.setViewPosition (30, 0);
        viewport.reveal (row);
        expect (viewport.getViewArea().contains (row.getBounds().withX (viewport.getViewPositionX()).withWidth (1)));
        expectEquals (viewport.getViewPositionX(), 30);
        const auto afterRow = viewport.getViewPosition();
        viewport.reveal (button);
        expect (viewport.getViewPosition() == afterRow, "Controls within a visible row must not move the viewport.");
        viewport.setViewPosition (30, 700);
        viewport.reveal (button);
        expectEquals (viewport.getViewPositionY(), 616);
        expectEquals (viewport.getViewPositionX(), 30);
        viewport.reveal (row);
        expectEquals (viewport.getViewPositionY(), 600);
        juce::Component unrelated;
        viewport.reveal (unrelated);
        expectEquals (viewport.getViewPositionY(), 600);
        viewport.setViewedComponent (nullptr, false);
        viewport.reveal (row);
    }
};

SkinEffectChainViewportTests skinEffectChainViewportTests;
}
