#pragma once

#include "library/SmartPlaylistRules.h"
#include "ui/JuceSkinAdapter.h"

#include <functional>
#include <memory>

class SmartPlaylistEditor final : public juce::Component,
                                  private juce::ComponentListener,
                                  private juce::AsyncUpdater
{
public:
    using PreviewCount = std::function<int (const SmartPlaylistRules&)>;
    using Save = std::function<juce::Result (const juce::String&, const SmartPlaylistRules&)>;
    explicit SmartPlaylistEditor (JuceSkinAdapter&);
    ~SmartPlaylistEditor() override;
    void show (juce::Component& parent, juce::String name, SmartPlaylistRules,
               PreviewCount, Save);
    void close();
    void refreshPreview();
    std::function<void()> onClosed;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class RuleList;
    void updatePreview();
    void saveDraft();
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void handleAsyncUpdate() override;
    JuceSkinAdapter& adapter;
    juce::Component::SafePointer<juce::Component> host, returnFocus;
    std::unique_ptr<RuleList> rules;
    juce::Label heading, nameLabel, preview;
    juce::TextEditor name;
    juce::ComboBox match;
    juce::TextButton add { "Add rule" }, save { "Save playlist" }, cancel { "Cancel" };
    SmartPlaylistRules draft;
    PreviewCount previewCount;
    Save saveCallback;
    std::optional<size_t> focusRuleAfterRebuild;
};
