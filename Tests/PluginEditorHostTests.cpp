#include "ui/PluginEditorHost.h"

#if JUCE_MAC
 #import <AppKit/AppKit.h>
#endif

namespace
{
class EditorProbe final : public juce::AudioProcessor
{
public:
    explicit EditorProbe (bool native = true, bool cocoa = false) : nativeEditor (native), cocoaEditor (cocoa)
    {
        gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f);
        addParameter (gain);
    }
    const juce::String getName() const override { return "Editor probe"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return nativeEditor; }
    juce::AudioProcessorEditor* createEditor() override
    {
        ++creations;
        if (! nativeEditor) return nullptr;
        struct NativeEditor final : juce::AudioProcessorEditor
        {
            NativeEditor (juce::AudioProcessor& owner, [[maybe_unused]] bool cocoa) : AudioProcessorEditor (owner)
            {
                addAndMakeVisible (firstControl);
                addAndMakeVisible (control);
                firstControl.setExplicitFocusOrder (1);
                control.setExplicitFocusOrder (2);
               #if JUCE_MAC
                if (cocoa)
                {
                    auto* view = [[NSButton alloc] initWithFrame:NSMakeRect (0, 0, 640, 320)];
                    nativeView.setView (view);
                    [view release];
                    addAndMakeVisible (nativeView);
                }
               #endif
            }
            void resized() override
            {
                control.setBounds (getWidth() - 80, getHeight() - 40, 72, 28);
                firstControl.setBounds (8, 8, 72, 28);
               #if JUCE_MAC
                nativeView.setBounds (getLocalBounds());
               #endif
            }
            juce::TextButton firstControl { "First parameter" }, control { "Last parameter" };
           #if JUCE_MAC
            juce::NSViewComponent nativeView;
           #endif
        };
        auto* editor = new NativeEditor (*this, cocoaEditor);
        editor->setSize (640, 320);
        return editor;
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    juce::AudioParameterFloat* gain = nullptr;
    int creations = 0;
private:
    bool nativeEditor;
    bool cocoaEditor;
};

class PluginEditorHostTests final : public juce::UnitTest
{
public:
    PluginEditorHostTests() : UnitTest ("PluginEditorHost", "ui") {}
    void runTest() override
    {
        beginTest ("dock and floating transitions keep one editor and live parameter values");
        EditorProbe processor;
        {
            PluginEditorHost host (processor);
            host.setBounds (0, 0, 400, host.requiredHeight (400));
            auto* editor = processor.getActiveEditor();
            expect (editor != nullptr);
            expect (host.requiredHeight (400) > 320, "Wide editors retain native size and gain a horizontal scrollbar");
            expectEquals (host.requiredHeight (800), 320);
            processor.gain->setValueNotifyingHost (0.7f);
            for (int pass = 0; pass < 3; ++pass)
            {
                host.showWindow();
                expect (host.isFloating());
                expectEquals (host.requiredHeight (400), 0);
                expect (processor.getActiveEditor() == editor);
                host.dock();
                expect (! host.isFloating());
                expect (processor.getActiveEditor() == editor);
            }
            expectEquals (processor.creations, 1);
            expectWithinAbsoluteError (processor.gain->get(), 0.7f, 0.0001f);
            if (editor != nullptr)
            {
                int notifications = 0;
                host.onSizeChanged = [&] { ++notifications; };
                editor->setSize (900, 500);
                expect (notifications > 0);
                expect (host.requiredHeight (400) > 500);
                host.onSizeChanged = {};
                juce::DocumentWindow container ("Editor focus test", juce::Colours::black, juce::DocumentWindow::closeButton);
                container.setUsingNativeTitleBar (true);
                container.setContentNonOwned (&host, true);
                container.setVisible (true);
                container.toFront (true);
                editor->getChildComponent (0)->grabKeyboardFocus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                auto* viewport = dynamic_cast<juce::Viewport*> (host.getChildComponent (0));
                expect (viewport != nullptr && viewport->getViewPositionX() == 0);
                editor->getChildComponent (0)->moveKeyboardFocusToSibling (true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                expect (viewport != nullptr && viewport->getViewPositionX() > 0,
                        "Keyboard focus must reveal controls beyond the horizontal viewport");
                container.clearContentComponent();
            }
            host.showWindow(); // Also exercise destruction while the editor is floating.
        }
        expect (processor.getActiveEditor() == nullptr);

       #if JUCE_MAC
        beginTest ("Tab enters and leaves docked editors in both directions");
        {
            EditorProbe tabProcessor;
            PluginEditorHost host (tabProcessor);
            juce::TextButton before ("Before rack"), after ("After rack");
            juce::Component frame;
            frame.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
            frame.setSize (700, 440);
            frame.addAndMakeVisible (before);
            frame.addAndMakeVisible (host);
            frame.addAndMakeVisible (after);
            before.setBounds (0, 0, 100, 30);
            host.setBounds (0, 40, 650, 320);
            after.setBounds (0, 380, 100, 30);
            before.setExplicitFocusOrder (1);
            host.setExplicitFocusOrder (2);
            after.setExplicitFocusOrder (3);
            juce::DocumentWindow container ("Editor Tab test", juce::Colours::black, juce::DocumentWindow::closeButton);
            container.setUsingNativeTitleBar (true);
            container.setContentNonOwned (&frame, true);
            container.setVisible (true);
            container.toFront (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            auto* editor = tabProcessor.getActiveEditor();
            auto* first = editor->getChildComponent (0);
            auto* last = editor->getChildComponent (1);
            before.grabKeyboardFocus();
            before.moveKeyboardFocusToSibling (true);
            expect (first->hasKeyboardFocus (false));
            editor->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey));
            expect (last->hasKeyboardFocus (false));
            editor->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey));
            expect (after.hasKeyboardFocus (false));
            after.moveKeyboardFocusToSibling (false);
            expect (last->hasKeyboardFocus (false));
            editor->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0));
            expect (first->hasKeyboardFocus (false));
            editor->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0));
            expect (before.hasKeyboardFocus (false));
            host.showWindow();
            first->grabKeyboardFocus();
            editor->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0));
            expect (last->hasKeyboardFocus (false), "Floating windows retain their own Tab traversal");
            host.dock();
            container.clearContentComponent();
        }

        beginTest ("native views obey both rack clipping axes while scrolling, resizing and changing peers");
        {
            EditorProbe cocoa (true, true);
            PluginEditorHost host (cocoa);
            juce::Viewport rack;
            juce::Component content;
            juce::DocumentWindow container ("Native clip test", juce::Colours::black, juce::DocumentWindow::closeButton);
            container.setUsingNativeTitleBar (true);
            content.setSize (420, 800);
            content.addAndMakeVisible (host);
            host.setBounds (20, 40, 360, host.requiredHeight (360));
            rack.setViewedComponent (&content, false);
            rack.setSize (420, 240);
            container.setContentNonOwned (&rack, true);
            container.setVisible (true);
            auto* editor = cocoa.getActiveEditor();
            auto* native = dynamic_cast<juce::NSViewComponent*> (editor->getChildComponent (2));
            expect (native != nullptr);
            auto checkClip = [&]
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                auto bounds = native->getLocalBounds();
                for (auto* parent = native->getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
                    bounds = bounds.getIntersection (native->getLocalArea (parent, parent->getLocalBounds()));
                if (! host.isFloating())
                    for (juce::Component* parent = &host; parent != nullptr; parent = parent->getParentComponent())
                        bounds = bounds.getIntersection (native->getLocalArea (parent, parent->getLocalBounds()));
                auto* view = static_cast<NSView*> (native->getView());
                const auto visible = NSIntersectionRect ([view visibleRect], [view bounds]);
                expectWithinAbsoluteError (visible.size.width, static_cast<double> (bounds.getWidth()), 1.0);
                expectWithinAbsoluteError (visible.size.height, static_cast<double> (bounds.getHeight()), 1.0);
                if (! bounds.isEmpty())
                {
                    const auto outside = NSMakePoint (bounds.getRight() + 10, bounds.getCentreY());
                    auto* peerView = [[view window] contentView];
                    expect ([peerView hitTest:[view convertPoint:outside toView:peerView]] != view,
                            "Clipped native controls must not capture clicks outside the viewport");
                }
            };
            checkClip();
            // AU editors may deliver or replace their native view after construction.
            native->setView (nullptr);
            auto* replacement = [[NSButton alloc] initWithFrame:NSMakeRect (0, 0, 640, 320)];
            native->setView (replacement);
            [replacement release];
            checkClip();
            rack.setViewPosition (0, 120);
            checkClip();
            rack.setSize (300, 180);
            checkClip();
            host.showWindow();
            checkClip();
            host.dock();
            checkClip();
            host.setVisible (false);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            expect ([static_cast<NSView*> (native->getView()) isHiddenOrHasHiddenAncestor]);
            container.clearContentComponent();
            rack.setViewedComponent (nullptr, false);
        }
       #endif

        beginTest ("processors without a native editor use JUCE parameter controls");
        EditorProbe generic (false);
        {
            PluginEditorHost host (generic);
            expect (host.requiredHeight (500) > 0);
            host.showWindow();
            host.dock();
            expectEquals (generic.creations, 1);
        }
    }
} pluginEditorHostTests;
} // namespace
