#pragma once

#include "skin/SkinComponentBindings.h"
#include <functional>
#include <optional>

class PresetControls final : private juce::KeyListener
{
public:
    struct Entry
    {
        juce::String id, name;
        bool operator== (const Entry&) const = default;
    };
    enum class Action { saveAs, update, rename, duplicate, remove, importFile, exportFile };
    struct EditingState
    {
        juce::String text;
        juce::Range<int> selection;
        bool focused = false;
    };

    explicit PresetControls (juce::String noun);
    ~PresetControls() override;
    void setState (std::vector<Entry>, juce::String selectedId, bool dirty = false);
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, const juce::String& prefix, SkinValidationReport&);
    void beginAction (Action);
    void revertToSaved();
    void cancelDraft();
    void cancelEditing() { cancelDraft(); }
    void setEnabled (bool);
    [[nodiscard]] std::optional<EditingState> captureEditingState() const;
    void restoreEditingState (const std::optional<EditingState>&);
    [[nodiscard]] bool isEditingName() const noexcept { return pendingAction.has_value(); }

    juce::ComboBox selector;
    juce::TextButton actions { "Actions" };
    std::function<juce::Result (const juce::String& id)> onApply;
    std::function<juce::Result (Action, const juce::String& id, const juce::String& name)> onAction;
    std::function<void (const juce::String&)> onError;

private:
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void showActions();
    void submitDraft();
    void refreshSelection();
    void prepareNameEditor();
    void report (const juce::Result&);
    [[nodiscard]] juce::String selectedName() const;
    juce::String noun, selectedId;
    std::vector<Entry> entries;
    std::optional<Action> pendingAction;
    bool dirty = false, changing = false;
    juce::Component::SafePointer<juce::ComboBox> lifetime;
};
