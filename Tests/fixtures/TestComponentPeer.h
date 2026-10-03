#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Deliver JUCE pointer events without depending on foreground macOS windows.
class TestComponentPeer final : public juce::ComponentPeer
{
public:
    TestComponentPeer (juce::Component& owner, int flags)
        : juce::ComponentPeer (owner, flags), bounds (owner.getBounds()) {}
    void* getNativeHandle() const override { return nullptr; }
    void setVisible (bool value) override { visible = value; }
    void setTitle (const juce::String&) override {}
    void setBounds (const juce::Rectangle<int>& value, bool) override { bounds = value; }
    juce::Rectangle<int> getBounds() const override { return bounds; }
    juce::Point<float> localToGlobal (juce::Point<float> point) override { return point + bounds.getPosition().toFloat(); }
    juce::Point<float> globalToLocal (juce::Point<float> point) override { return point - bounds.getPosition().toFloat(); }
    void setMinimised (bool) override {}
    bool isMinimised() const override { return false; }
    bool isShowing() const override { return visible; }
    void setFullScreen (bool) override {}
    bool isFullScreen() const override { return false; }
    void setIcon (const juce::Image&) override {}
    bool contains (juce::Point<int> point, bool) const override { return bounds.withPosition (0, 0).contains (point); }
    OptionalBorderSize getFrameSizeIfPresent() const override { return OptionalBorderSize (juce::BorderSize<int>()); }
    juce::BorderSize<int> getFrameSize() const override { return {}; }
    bool setAlwaysOnTop (bool) override { return true; }
    void toFront (bool) override {}
    void toBehind (juce::ComponentPeer*) override {}
    bool isFocused() const override { return true; }
    void grabFocus() override {}
    void repaint (const juce::Rectangle<int>&) override {}
    void performAnyPendingRepaintsNow() override {}
    void setAlpha (float) override {}
    juce::StringArray getAvailableRenderingEngines() override { return {}; }
    void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}
private:
    juce::Rectangle<int> bounds;
    bool visible = false;
};


class TestUiRoot final : public juce::Component
{
    juce::ComponentPeer* createNewPeer (int flags, void*) override
    {
        return new TestComponentPeer (*this, flags);
    }
};
