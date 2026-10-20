#include "PluginEditorHost.h"

#if JUCE_MAC
#include "PluginEditorNativeClip.h"
#endif

class PluginEditorHost::EditorWindow final : public juce::DocumentWindow
{
public:
    explicit EditorWindow (PluginEditorHost& host)
        : DocumentWindow (host.processor.getName(), juce::Colours::black, closeButton), owner (host)
    {
        setUsingNativeTitleBar (true);
        setResizable (owner.editor->isResizable(), false);
    }
    void closeButtonPressed() override { owner.dock(); }
private:
    PluginEditorHost& owner;
};

PluginEditorHost::PluginEditorHost (juce::AudioProcessor& value) : processor (value)
{
    editor.reset (processor.createEditorIfNeeded());
    if (editor == nullptr)
        editor = std::make_unique<juce::GenericAudioProcessorEditor> (processor);
    // Preserve the plug-in's own drawing when it becomes a rack child.
    editor->setLookAndFeel (&editor->getLookAndFeel());
    editor->addComponentListener (this);
   #if JUCE_MAC
    nativeContent = createNativePluginEditorContent (*editor);
   #endif
    viewport.setViewedComponent (&dockedComponent(), false);
    viewport.setWantsKeyboardFocus (false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    juce::Desktop::getInstance().addFocusChangeListener (this);
}

PluginEditorHost::~PluginEditorHost()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    editor->removeComponentListener (this);
    window.reset();
    viewport.setViewedComponent (nullptr, false);
   #if JUCE_MAC
    nativeContent.reset();
   #endif
    processor.editorBeingDeleted (editor.get());
}

void PluginEditorHost::resized() { viewport.setBounds (getLocalBounds()); }

juce::Component& PluginEditorHost::dockedComponent() const
{
   #if JUCE_MAC
    return *nativeContent;
   #else
    return *editor;
   #endif
}

int PluginEditorHost::requiredHeight (int width) const
{
    return floating ? 0 : editor->getHeight()
        + (editor->getWidth() > width ? viewport.getScrollBarThickness() : 0);
}

void PluginEditorHost::dock()
{
    if (window != nullptr)
    {
        window->clearContentComponent();
        window->setVisible (false);
    }
    floating = false;
    viewport.setViewedComponent (&dockedComponent(), false);
    if (onPlacementChanged) onPlacementChanged();
}

void PluginEditorHost::showWindow()
{
    if (! floating)
    {
        viewport.setViewedComponent (nullptr, false);
        if (window == nullptr) window = std::make_unique<EditorWindow> (*this);
        floating = true;
        window->setContentNonOwned (editor.get(), true);
        window->centreWithSize (window->getWidth(), window->getHeight());
    }
    window->setVisible (true);
    window->toFront (true);
    if (onPlacementChanged) onPlacementChanged();
}

void PluginEditorHost::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (wasResized && onSizeChanged) onSizeChanged();
}

void PluginEditorHost::globalFocusChanged (juce::Component* focused)
{
    if (floating || focused == nullptr || ! editor->isParentOf (focused)) return;
    const auto control = editor->getLocalArea (focused, focused->getLocalBounds());
    const auto view = viewport.getViewArea();
    if (control.getX() < view.getX()) viewport.setViewPosition (control.getX(), 0);
    else if (control.getRight() > view.getRight())
        viewport.setViewPosition (control.getRight() - view.getWidth(), 0);
    if (onEditorFocusChanged) onEditorFocusChanged (*focused);
}
