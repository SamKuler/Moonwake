#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Owns one editor while its viewport and floating window borrow the component.
class PluginEditorHost final : public juce::Component,
                               private juce::ComponentListener,
                               private juce::FocusChangeListener
{
public:
    explicit PluginEditorHost (juce::AudioProcessor&);
    ~PluginEditorHost() override;
    void resized() override;
    void dock();
    void showWindow();
    [[nodiscard]] bool isFloating() const noexcept { return floating; }
    [[nodiscard]] int requiredHeight (int width) const;
    std::function<void()> onPlacementChanged;
    std::function<void()> onSizeChanged;
    std::function<void (juce::Component&)> onEditorFocusChanged;

private:
    class EditorWindow;
    void globalFocusChanged (juce::Component*) override;
    juce::Component& dockedComponent() const;
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    juce::AudioProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
   #if JUCE_MAC
    std::unique_ptr<juce::Component> nativeContent;
   #endif
    juce::Viewport viewport;
    std::unique_ptr<EditorWindow> window;
    bool floating = false;
};
