#include "SmartPlaylistEditor.h"

#include "skin/SkinComponentBindings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace
{
using Field = SmartPlaylistField;
using Operator = SmartPlaylistOperator;
const std::array<std::pair<Field, const char*>, 12> fields {{
    { Field::title, "Title" }, { Field::artist, "Artist" }, { Field::album, "Album" },
    { Field::albumArtist, "Album artist" }, { Field::genre, "Genre" }, { Field::format, "Format" },
    { Field::year, "Year" }, { Field::channelCount, "Channels" }, { Field::sampleRate, "Sample rate (Hz)" },
    { Field::addedWithinDays, "Added recently" }, { Field::favourite, "Favourite" }, { Field::available, "Available" }
}};
const char* operatorLabel (Operator comparison)
{
    switch (comparison)
    {
        case Operator::contains: return "contains";
        case Operator::notContains: return "does not contain";
        case Operator::equals: return "is";
        case Operator::notEquals: return "is not";
        case Operator::lessThan: return "is less than";
        case Operator::lessOrEqual: return "is at most";
        case Operator::greaterThan: return "is greater than";
        case Operator::greaterOrEqual: return "is at least";
        case Operator::withinLastDays: return "within last (days)";
    }
    return "";
}
void place (juce::Component& component, const SkinResolvedRowLayout& layout, const juce::String& slot)
{
    if (const auto frame = layout.slots.find (slot); frame != layout.slots.end())
        component.setBounds (frame->second.toNearestInt());
}
}

class SmartPlaylistEditor::RuleList final : public juce::Component
{
public:
    explicit RuleList (SmartPlaylistEditor& editor) : owner (editor)
    {
        getProperties().set (SkinComponentBindings::componentIdProperty, "library.smartPlaylistRules");
        setName ("Smart playlist rules");
        setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    }
    ~RuleList() override { viewport.setViewedComponent (nullptr, false); }
    void rebuild()
    {
        const auto scroll = viewport.getViewPosition();
        rows.clear();
        for (size_t i = 0; i < owner.draft.rules.size(); ++i)
        {
            auto row = std::make_unique<Row> (*this, i);
            content.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        resized();
        viewport.setViewPosition (scroll);
    }
    void focusRule (size_t requested)
    {
        if (rows.empty()) { owner.add.grabKeyboardFocus(); return; }
        auto& row = *rows[juce::jmin (requested, rows.size() - 1)];
        row.focusField();
    }
    void reveal (const juce::Component& row)
    {
        const auto visible = viewport.getViewArea();
        if (row.getY() < visible.getY()) viewport.setViewPosition (0, row.getY());
        else if (row.getBottom() > visible.getBottom())
            viewport.setViewPosition (0, row.getBottom() - viewport.getMaximumVisibleHeight());
    }
    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext context;
        context.bounds = context.contentBounds = getLocalBounds().toFloat();
        owner.adapter.drawCustomComponent (graphics, *this, SkinRenderTarget::smartPlaylistRuleList, context);
    }
    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        if (getWidth() <= 0 || getHeight() <= 0) return;
        const auto layoutRows = [this] (int width)
        {
            SkinRenderContext context;
            context.bounds = { 0, 0, static_cast<float> (width), static_cast<float> (getHeight()) };
            context.tableRowCount = static_cast<int> (rows.size());
            const auto layout = owner.adapter.rowLayoutFor (*this, SkinRenderTarget::smartPlaylistRuleList, context);
            if (! layout) return;
            const auto stride = juce::roundToInt (layout->rowHeight + layout->rowGap);
            content.setSize (width, stride * static_cast<int> (rows.size()));
            for (size_t i = 0; i < rows.size(); ++i)
            {
                rows[i]->setBounds (0, static_cast<int> (i) * stride, width, juce::roundToInt (layout->rowHeight));
                rows[i]->apply (*layout);
            }
        };
        layoutRows (viewport.getMaximumVisibleWidth());
        layoutRows (viewport.getMaximumVisibleWidth());
    }

private:
    class Row final : public juce::Component
    {
        template <typename Control>
        class FocusControl final : public Control
        {
        public:
            FocusControl (Row& row, const juce::String& controlName = {}) : Control (controlName), owner (row) {}
            void focusGained (FocusChangeType cause) override
            {
                Control::focusGained (cause);
                owner.list.reveal (owner);
            }
        private:
            Row& owner;
        };
    public:
        Row (RuleList& ownerList, size_t ruleIndex) : list (ownerList), index (ruleIndex)
        {
            for (const auto& [fieldValue, label] : fields) field.addItem (label, static_cast<int> (fieldValue) + 1);
            field.setSelectedId (static_cast<int> (rule().field) + 1, juce::dontSendNotification);
            field.setName ("Rule " + juce::String (static_cast<int> (index + 1)) + " field");
            comparison.setName ("Rule comparison");
            value.setName ("Rule value");
            boolean.setName ("Rule value");
            boolean.addItem ("Yes", 1);
            boolean.addItem ("No", 2);
            remove.setTooltip ("Remove this rule");
            int order = 1;
            for (auto* control : std::array<juce::Component*, 5> { &field, &comparison, &value, &boolean, &remove })
            {
                addAndMakeVisible (control);
                control->setExplicitFocusOrder (order++);
            }
            configure();
            field.onChange = [this]
            {
                rule().field = static_cast<Field> (field.getSelectedId() - 1);
                const auto supported = smartPlaylistOperatorsForField (rule().field);
                if (std::find (supported.begin(), supported.end(), rule().comparison) == supported.end())
                    rule().comparison = supported.front();
                configure();
                list.owner.updatePreview();
            };
            comparison.onChange = [this]
            {
                rule().comparison = static_cast<Operator> (comparison.getSelectedId() - 1);
                list.owner.updatePreview();
            };
            value.onTextChange = [this]
            {
                if (smartPlaylistValueKind (rule().field) == SmartPlaylistValueKind::text)
                    rule().textValue = value.getText();
                else
                {
                    const auto text = value.getText().trim().toStdString();
                    double number = 0;
                    const auto parsed = std::from_chars (text.data(), text.data() + text.size(), number);
                    rule().numberValue = parsed.ec == std::errc() && parsed.ptr == text.data() + text.size()
                        ? number : std::numeric_limits<double>::quiet_NaN();
                }
                list.owner.updatePreview();
            };
            value.onEscapeKey = [this] { list.owner.close(); };
            boolean.onChange = [this] { rule().boolValue = boolean.getSelectedId() == 1; list.owner.updatePreview(); };
            remove.onClick = [this]
            {
                list.owner.draft.rules.erase (list.owner.draft.rules.begin() + static_cast<std::ptrdiff_t> (index));
                list.owner.focusRuleAfterRebuild = index;
                list.owner.triggerAsyncUpdate();
            };
        }
        void focusField() { field.grabKeyboardFocus(); }
        void apply (const SkinResolvedRowLayout& layout)
        {
            for (const auto& [control, slot] : std::array<std::pair<juce::Component*, const char*>, 5> {{
                { &field, "rule.field" }, { &comparison, "rule.operator" }, { &value, "rule.value" },
                { &boolean, "rule.boolean" }, { &remove, "rule.remove" }
            }})
            {
                list.owner.adapter.applySlotStyle (*control, list, SkinRenderTarget::smartPlaylistRuleList, slot);
                place (*control, layout, slot);
            }
            list.owner.adapter.configureTextEditor (value);
        }
    private:
        SmartPlaylistRule& rule() { return list.owner.draft.rules[index]; }
        void configure()
        {
            comparison.clear (juce::dontSendNotification);
            for (const auto op : smartPlaylistOperatorsForField (rule().field))
                comparison.addItem (operatorLabel (op), static_cast<int> (op) + 1);
            comparison.setSelectedId (static_cast<int> (rule().comparison) + 1, juce::dontSendNotification);
            const auto kind = smartPlaylistValueKind (rule().field);
            value.setVisible (kind != SmartPlaylistValueKind::boolean);
            boolean.setVisible (kind == SmartPlaylistValueKind::boolean);
            value.setText (kind == SmartPlaylistValueKind::text ? rule().textValue
                : (std::isfinite (rule().numberValue) ? juce::String (rule().numberValue) : juce::String()), false);
            boolean.setSelectedId (rule().boolValue ? 1 : 2, juce::dontSendNotification);
        }
        RuleList& list;
        size_t index;
        FocusControl<juce::ComboBox> field { *this }, comparison { *this }, boolean { *this };
        FocusControl<juce::TextEditor> value { *this };
        FocusControl<juce::TextButton> remove { *this, "Remove" };
    };
    SmartPlaylistEditor& owner;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
};

SmartPlaylistEditor::SmartPlaylistEditor (JuceSkinAdapter& skin)
    : adapter (skin), rules (std::make_unique<RuleList> (*this))
{
    setName ("Smart playlist editor");
    getProperties().set (SkinComponentBindings::componentIdProperty, "library.smartPlaylistEditor");
    setLookAndFeel (&adapter);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    heading.setText ("Smart playlist", juce::dontSendNotification);
    nameLabel.setText ("Name", juce::dontSendNotification);
    name.setName ("Playlist name");
    preview.setName ("Matching tracks and rule validation");
    match.setName ("Match rules");
    match.addItem ("Match all rules", 1);
    match.addItem ("Match any rule", 2);
    int order = 1;
    for (auto* component : std::array<juce::Component*, 9> { &heading, &nameLabel, &name, &match, rules.get(), &add, &preview, &save, &cancel })
    {
        addAndMakeVisible (component);
        component->setExplicitFocusOrder (order++);
    }
    name.onTextChange = [this] { updatePreview(); };
    name.onEscapeKey = [this] { close(); };
    match.onChange = [this] { draft.match = match.getSelectedId() == 2 ? SmartPlaylistMatch::any : SmartPlaylistMatch::all; updatePreview(); };
    add.onClick = [this]
    {
        if (draft.rules.size() >= SmartPlaylistRules::maximumRules) return;
        draft.rules.emplace_back();
        focusRuleAfterRebuild = draft.rules.size() - 1;
        triggerAsyncUpdate();
    };
    save.onClick = [this] { saveDraft(); };
    cancel.onClick = [this] { close(); };
    setVisible (false);
}
SmartPlaylistEditor::~SmartPlaylistEditor()
{
    cancelPendingUpdate();
    if (host != nullptr) host->removeComponentListener (this);
    if (isCurrentlyModal()) exitModalState (0);
    setLookAndFeel (nullptr);
}
void SmartPlaylistEditor::show (juce::Component& parent, juce::String title, SmartPlaylistRules initial,
                                PreviewCount count, Save persist)
{
    if (host != nullptr) host->removeComponentListener (this);
    cancelPendingUpdate();
    if (! isVisible()) returnFocus = juce::Component::getCurrentlyFocusedComponent();
    host = &parent;
    host->addComponentListener (this);
    focusRuleAfterRebuild.reset();
    draft = std::move (initial);
    if (draft.rules.empty()) draft.rules.emplace_back();
    previewCount = std::move (count);
    saveCallback = std::move (persist);
    name.setText (title, false);
    match.setSelectedId (draft.match == SmartPlaylistMatch::any ? 2 : 1, juce::dontSendNotification);
    parent.addAndMakeVisible (*this);
    setBounds (parent.getLocalBounds());
    rules->rebuild();
    resized();
    updatePreview();
    toFront (true);
    if (! isCurrentlyModal()) enterModalState (false);
    name.grabKeyboardFocus();
}
void SmartPlaylistEditor::close()
{
    cancelPendingUpdate();
    if (isCurrentlyModal()) exitModalState (0);
    setVisible (false);
    if (returnFocus != nullptr && returnFocus->isShowing() && returnFocus->isEnabled())
        returnFocus->grabKeyboardFocus();
    returnFocus = nullptr;
    if (onClosed) onClosed();
}
void SmartPlaylistEditor::paint (juce::Graphics& graphics)
{
    SkinRenderContext context;
    context.bounds = context.contentBounds = getLocalBounds().toFloat();
    adapter.drawCustomComponent (graphics, *this, SkinRenderTarget::smartPlaylistEditor, context);
}
void SmartPlaylistEditor::resized()
{
    SkinRenderContext context;
    context.bounds = context.contentBounds = getLocalBounds().toFloat();
    const auto layout = adapter.rowLayoutFor (*this, SkinRenderTarget::smartPlaylistEditor, context);
    if (! layout) return;
    for (const auto& [control, slot] : std::array<std::pair<juce::Component*, const char*>, 9> {{
        { &heading, "editor.heading" }, { &nameLabel, "editor.nameLabel" }, { &name, "editor.name" },
        { &match, "editor.match" }, { rules.get(), "editor.rules" }, { &add, "editor.add" },
        { &preview, "editor.preview" }, { &save, "editor.save" }, { &cancel, "editor.cancel" }
    }})
    {
        adapter.applySlotStyle (*control, *this, SkinRenderTarget::smartPlaylistEditor, slot);
        place (*control, *layout, slot);
    }
    adapter.configureTextEditor (name);
    rules->resized();
}
void SmartPlaylistEditor::componentMovedOrResized (juce::Component& component, bool, bool wasResized)
{
    if (wasResized && host == &component)
    {
        setBounds (component.getLocalBounds());
        if (isVisible()) toFront (false);
    }
}
void SmartPlaylistEditor::refreshPreview()
{
    if (isVisible()) triggerAsyncUpdate();
}
void SmartPlaylistEditor::handleAsyncUpdate()
{
    if (focusRuleAfterRebuild) rules->rebuild();
    updatePreview();
    if (focusRuleAfterRebuild)
    {
        rules->focusRule (*focusRuleAfterRebuild);
        focusRuleAfterRebuild.reset();
    }
}
bool SmartPlaylistEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}
void SmartPlaylistEditor::updatePreview()
{
    const auto validation = validateSmartPlaylistRules (draft);
    const auto named = name.getText().trim().isNotEmpty();
    save.setEnabled (named && validation.wasOk());
    add.setEnabled (draft.rules.size() < SmartPlaylistRules::maximumRules);
    auto text = ! named ? juce::String ("Enter a playlist name.")
        : validation.failed() ? validation.getErrorMessage()
        : juce::String (previewCount ? previewCount (draft) : 0) + " matching tracks";
    preview.setText (text, juce::dontSendNotification);
    preview.setTooltip (text);
}
void SmartPlaylistEditor::saveDraft()
{
    updatePreview();
    if (! save.isEnabled() || ! saveCallback) return;
    const auto result = saveCallback (name.getText().trim(), draft);
    if (result.failed())
    {
        preview.setText (result.getErrorMessage(), juce::dontSendNotification);
        preview.setTooltip (result.getErrorMessage());
        return;
    }
    close();
}
