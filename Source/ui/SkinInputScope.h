#pragma once

#include "JuceSkinAdapter.h"

// Tracks input before the focused control consumes it; controls keep their native key handling.
class SkinInputScope final : private juce::MouseListener,
                             private juce::KeyListener,
                             private juce::FocusChangeListener
{
public:
    explicit SkinInputScope (juce::Component& rootToUse) : root (rootToUse)
    {
        root.getProperties().set (JuceSkinAdapter::focusVisibleProperty, true);
        root.addMouseListener (this, true);
        root.addKeyListener (this);
        juce::Desktop::getInstance().addFocusChangeListener (this);
        watchFocusedComponent();
    }

    ~SkinInputScope() override
    {
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        if (focused != nullptr) focused->removeKeyListener (this);
        root.removeKeyListener (this);
        root.removeMouseListener (this);
    }

private:
    void setKeyboardInput (bool value)
    {
        if (static_cast<bool> (root.getProperties()[JuceSkinAdapter::focusVisibleProperty]) == value) return;
        root.getProperties().set (JuceSkinAdapter::focusVisibleProperty, value);
        root.repaint();
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        setKeyboardInput (false);
        watchFocusedComponent();
    }

    void mouseUp (const juce::MouseEvent&) override { watchFocusedComponent(); }

    bool keyPressed (const juce::KeyPress&, juce::Component*) override
    {
        setKeyboardInput (true);
        return false;
    }

    void globalFocusChanged (juce::Component*) override { watchFocusedComponent(); }

    void watchFocusedComponent()
    {
        auto* next = juce::Component::getCurrentlyFocusedComponent();
        if (next == &root || ! root.isParentOf (next)) next = nullptr;
        if (focused == next) return;
        if (focused != nullptr) focused->removeKeyListener (this);
        focused = next;
        if (focused != nullptr) focused->addKeyListener (this);
    }

    juce::Component& root;
    juce::Component::SafePointer<juce::Component> focused;

    JUCE_DECLARE_NON_COPYABLE (SkinInputScope)
};
