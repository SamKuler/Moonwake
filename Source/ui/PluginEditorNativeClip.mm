#include "PluginEditorNativeClip.h"

#import <AppKit/AppKit.h>

@interface MoonwakePluginClipView : NSClipView
@end

@implementation MoonwakePluginClipView
- (BOOL)isFlipped { return YES; }
@end

namespace
{
// A child JUCE peer keeps every native plug-in subview inside one AppKit clip,
// including AU views delivered asynchronously after the editor is constructed.
class NativeEditorContent final : public juce::Component,
                                  private juce::ComponentMovementWatcher,
                                  private juce::KeyListener
{
public:
    explicit NativeEditorContent (juce::AudioProcessorEditor& value)
        : ComponentMovementWatcher (this), editor (value), clip ([[MoonwakePluginClipView alloc] init])
    {
        [clip setDrawsBackground:NO];
        if (@available (macOS 14.0, *)) [clip setClipsToBounds:YES];
        setWantsKeyboardFocus (true);
        setName ("Plug-in editor");
        juce::AccessibilityHandler::setNativeChildForComponent (*this, clip);
        editor.addComponentListener (this);
        editor.addKeyListener (this);
        setSize (editor.getWidth(), editor.getHeight());
    }

    ~NativeEditorContent() override
    {
        editor.removeComponentListener (this);
        editor.removeKeyListener (this);
        juce::AccessibilityHandler::setNativeChildForComponent (*this, nullptr);
        detach();
        [clip release];
    }

private:
    using juce::Component::keyPressed;
    void focusGainedWithDirection (FocusChangeType, FocusChangeDirection direction) override
    {
        if (leavingEditor) return;
        auto traverser = editor.createKeyboardFocusTraverser();
        const auto controls = traverser->getAllComponents (&editor);
        if (! controls.empty())
            (direction == FocusChangeDirection::backward ? controls.back() : controls.front())->grabKeyboardFocus();
        else if (auto* peer = editor.getPeer())
            [[clip window] makeFirstResponder:static_cast<NSView*> (peer->getNativeHandle())];
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (! attached || ! key.isKeyCode (juce::KeyPress::tabKey)) return false;
        auto traverser = editor.createKeyboardFocusTraverser();
        const auto controls = traverser->getAllComponents (&editor);
        auto* current = juce::Component::getCurrentlyFocusedComponent();
        const auto forward = ! key.getModifiers().isShiftDown();
        if (! controls.empty() && current != (forward ? controls.back() : controls.front())) return false;
        const juce::ScopedValueSetter<bool> leaving (leavingEditor, true);
        grabKeyboardFocus();
        moveKeyboardFocusToSibling (forward);
        return true;
    }

    void detach()
    {
        if (attached) editor.removeFromDesktop();
        attached = false;
        [clip removeFromSuperview];
    }

    void update()
    {
        auto* peer = getPeer();
        if (peer == nullptr)
        {
            detach();
            return;
        }
        auto* peerView = static_cast<NSView*> (peer->getNativeHandle());
        if ([clip superview] != peerView)
        {
            detach();
            [peerView addSubview:clip];
        }
        auto visible = getLocalBounds();
        for (auto* parent = getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
            visible = visible.getIntersection (getLocalArea (parent, parent->getLocalBounds()));
        const auto area = peer->getAreaCoveredBy (*this);
        visible = visible.translated (area.getX(), area.getY());
        const auto frame = NSMakeRect (visible.getX(), visible.getY(), visible.getWidth(), visible.getHeight());
        [clip setFrame:frame];
        [clip setBoundsOrigin:frame.origin];
        [clip setHidden:! isShowing() || visible.isEmpty()];
        if (! attached)
        {
            editor.addToDesktop (0, clip);
            attached = true;
        }
        editor.setBounds (area);
        editor.setVisible (isShowing());
    }

    void componentMovedOrResized (bool, bool) override { update(); }
    void componentPeerChanged() override { update(); }
    void componentVisibilityChanged() override { update(); }
    void componentMovedOrResized (juce::Component& component, bool moved, bool resized) override
    {
        if (&component == &editor)
        {
            if (resized) setSize (editor.getWidth(), editor.getHeight());
            return;
        }
        ComponentMovementWatcher::componentMovedOrResized (component, moved, resized);
        // Ancestor resizing changes the clip even when the editor's own frame stays fixed.
        update();
    }

    juce::AudioProcessorEditor& editor;
    MoonwakePluginClipView* clip;
    bool attached = false;
    bool leavingEditor = false;
};
}

std::unique_ptr<juce::Component> createNativePluginEditorContent (juce::AudioProcessorEditor& editor)
{
    return std::make_unique<NativeEditorContent> (editor);
}
