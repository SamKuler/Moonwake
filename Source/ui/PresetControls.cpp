#include "PresetControls.h"

PresetControls::PresetControls (juce::String nounToUse) : noun (std::move (nounToUse)), lifetime (&selector)
{
    selector.setTitle (noun);
    selector.setTextWhenNothingSelected ("Unsaved " + noun.toLowerCase());
    selector.setTextWhenNoChoicesAvailable ("No saved " + noun.toLowerCase() + "s");
    actions.setTitle (noun + " actions");
    selector.onChange = [this]
    {
        if (changing || ! selector.isEnabled()) return;
        if (pendingAction) return;
        const auto index = selector.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int> (entries.size())) return;
        const auto id = entries[static_cast<size_t> (index)].id;
        if (id == selectedId) { refreshSelection(); return; }
        const auto result = onApply ? onApply (id) : juce::Result::fail ("Preset application is unavailable.");
        report (result);
        refreshSelection();
    };
    actions.onClick = [this] { showActions(); };
    refreshSelection();
}

PresetControls::~PresetControls()
{
    selector.onChange = {};
    for (auto* child : selector.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            label->onEditorShow = {};
            if (auto* editor = label->getCurrentTextEditor()) editor->removeKeyListener (this);
        }
}

void PresetControls::setState (std::vector<Entry> next, juce::String selected, bool modified)
{
    if (entries == next && selectedId == selected && dirty == modified) return;
    entries = std::move (next);
    selectedId = std::move (selected);
    dirty = modified;
    if (! pendingAction) refreshSelection();
}

void PresetControls::setEnabled (bool enabled)
{
    selector.setEnabled (enabled);
    actions.setEnabled (enabled);
}

std::optional<PresetControls::EditingState> PresetControls::captureEditingState() const
{
    if (pendingAction)
        for (auto* child : selector.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                if (auto* editor = label->getCurrentTextEditor())
                    return EditingState { editor->getText(), editor->getHighlightedRegion(), editor->hasKeyboardFocus (false) };
    return std::nullopt;
}

void PresetControls::prepareNameEditor()
{
    for (auto* child : selector.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            label->onEditorShow = [this, label]
            {
                if (auto* editor = label->getCurrentTextEditor()) editor->addKeyListener (this);
            };
}

void PresetControls::restoreEditingState (const std::optional<EditingState>& state)
{
    if (! pendingAction) return;
    prepareNameEditor();
    if (! state) return;
    const juce::ScopedValueSetter<bool> guard (changing, true);
    selector.setText (state->text, juce::dontSendNotification);
    if (! state->focused || ! selector.isShowing()) return;
    selector.showEditor();
    for (auto* child : selector.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            if (auto* editor = label->getCurrentTextEditor()) editor->setHighlightedRegion (state->selection);
}

void PresetControls::addTo (juce::Component& owner)
{
    owner.addAndMakeVisible (selector);
    owner.addAndMakeVisible (actions);
}

void PresetControls::bind (SkinComponentBindings& bindings, const juce::String& prefix, SkinValidationReport& reportToUse)
{
    bindings.bind (prefix + ".select", selector, reportToUse);
    bindings.bind (prefix + ".actions", actions, reportToUse);
}

juce::String PresetControls::selectedName() const
{
    for (const auto& entry : entries) if (entry.id == selectedId) return entry.name;
    return {};
}

void PresetControls::refreshSelection()
{
    const juce::ScopedValueSetter<bool> guard (changing, true);
    selector.setEditableText (false);
    selector.clear (juce::dontSendNotification);
    int selected = 0;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        selector.addItem (entries[index].name, static_cast<int> (index + 1));
        if (entries[index].id == selectedId) selected = static_cast<int> (index + 1);
    }
    selector.setSelectedId (selected, juce::dontSendNotification);
    if (selected > 0 && dirty) selector.setText (selectedName() + " (modified)", juce::dontSendNotification);
    actions.setButtonText ("Actions");
    actions.setTooltip ("Save, manage, import or export " + noun.toLowerCase() + "s");
    selector.setTooltip (noun + (dirty ? " has unsaved changes" : ""));
}

void PresetControls::report (const juce::Result& result)
{
    if (result.failed())
    {
        selector.setTooltip (result.getErrorMessage());
        if (onError) onError (result.getErrorMessage());
    }
}

void PresetControls::beginAction (Action action)
{
    if (action == Action::saveAs || action == Action::rename || action == Action::duplicate)
    {
        if (action != Action::saveAs && selectedName().isEmpty()) return;
        pendingAction = action;
        const juce::ScopedValueSetter<bool> guard (changing, true);
        selector.setEditableText (true);
        auto name = selectedName();
        if (action == Action::duplicate && name.isNotEmpty()) name += " copy";
        selector.setText (name, juce::dontSendNotification);
        selector.setTooltip ("Enter a name and press Return to save; Escape cancels.");
        actions.setButtonText ("Save...");
        prepareNameEditor();
        if (selector.isShowing()) selector.showEditor();
        return;
    }
    if ((action == Action::update || action == Action::remove || action == Action::exportFile) && selectedName().isEmpty()) return;
    report (onAction ? onAction (action, selectedId, {}) : juce::Result::fail ("Preset actions are unavailable."));
}

void PresetControls::revertToSaved()
{
    if (! dirty || selectedName().isEmpty() || ! selector.isEnabled()) return;
    report (onApply ? onApply (selectedId) : juce::Result::fail ("Preset application is unavailable."));
    refreshSelection();
}

void PresetControls::submitDraft()
{
    if (! pendingAction) return;
    const auto name = selector.getText().trim();
    if (name.isEmpty())
    {
        report (juce::Result::fail ("Enter a name to save this " + noun.toLowerCase() + "."));
        actions.setButtonText ("Retry...");
        return;
    }
    const auto result = onAction ? onAction (*pendingAction, selectedId, name)
                                : juce::Result::fail ("Preset actions are unavailable.");
    if (result.failed())
    {
        report (result);
        actions.setButtonText ("Retry...");
        return;
    }
    pendingAction.reset();
    refreshSelection();
}

void PresetControls::cancelDraft()
{
    for (auto* child : selector.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child)) label->hideEditor (true);
    pendingAction.reset();
    refreshSelection();
}

bool PresetControls::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    if (! pendingAction || (key != juce::KeyPress::returnKey && key != juce::KeyPress::escapeKey)) return false;
    auto* editor = dynamic_cast<juce::TextEditor*> (origin);
    if (editor == nullptr) return false;
    const auto draft = editor->getText();
    const auto cancel = key == juce::KeyPress::escapeKey;
    const auto safe = lifetime;
    juce::MessageManager::callAsync ([this, safe, draft, cancel]
    {
        if (safe == nullptr || ! pendingAction) return;
        for (auto* child : selector.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child)) label->hideEditor (true);
        if (cancel) { cancelDraft(); return; }
        selector.setText (draft, juce::dontSendNotification);
        submitDraft();
    });
    return true;
}

void PresetControls::showActions()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&actions.getLookAndFeel());
    if (pendingAction)
    {
        menu.addItem (20, "Save name");
        menu.addItem (21, "Edit name...");
        menu.addItem (22, "Cancel name edit");
    }
    else
    {
        const auto saved = selectedName().isNotEmpty();
        menu.addItem (1, "Save as...");
        menu.addItem (2, "Update current", saved);
        menu.addItem (8, "Revert to saved", saved && dirty);
        menu.addSeparator();
        menu.addItem (3, "Rename...", saved);
        menu.addItem (4, "Duplicate...", saved);
        menu.addItem (5, "Delete", saved);
        menu.addSeparator();
        menu.addItem (6, "Import...");
        menu.addItem (7, "Export...", saved);
    }
    const auto safe = lifetime;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&actions), [this, safe] (int result)
    {
        if (safe == nullptr) return;
        if (result >= 1 && result <= 7) beginAction (static_cast<Action> (result - 1));
        if (result == 8) revertToSaved();
        if (result == 20) submitDraft();
        if (result == 21) selector.showEditor();
        if (result == 22) cancelDraft();
    });
}
