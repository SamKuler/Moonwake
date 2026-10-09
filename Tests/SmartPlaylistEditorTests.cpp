#include "fixtures/TestComponentPeer.h"
#include "ui/SmartPlaylistEditor.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"

namespace
{
juce::Component* findEditorControl (juce::Component& root, const juce::String& name)
{
    if (root.getName() == name) return &root;
    if (auto* button = dynamic_cast<juce::Button*> (&root); button != nullptr && button->getButtonText() == name) return button;
    for (auto* child : root.getChildren())
        if (auto* found = findEditorControl (*child, name)) return found;
    return nullptr;
}
class SmartPlaylistEditorTests final : public juce::UnitTest
{
public:
    SmartPlaylistEditorTests() : UnitTest ("SmartPlaylistEditor", "skin") {}
    void runTest() override
    {
        SkinValidationReport report;
        const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
        const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
        const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
        const auto skin = package && foundation ? SkinCompiler::compile (*package, foundation, report) : nullptr;
        beginTest ("editor and rule geometry are supplied by both workspace skins at supported window sizes");
        expect (skin != nullptr, report.toText());
        if (! skin) return;
        for (const auto* theme : { "listening", "studio" })
            for (const auto size : { juce::Point<int> (1040, 700), juce::Point<int> (1240, 800) })
            {
                const auto* editorStyle = skin->resolveStyle (SkinRenderTarget::smartPlaylistEditor, "smartPlaylistEditor." + juce::String (theme));
                expect (editorStyle != nullptr);
                if (! editorStyle) continue;
                SkinRenderContext context;
                context.bounds = context.contentBounds = { 0, 0, static_cast<float> (size.x), static_cast<float> (size.y) };
                const auto layout = resolveSkinRowLayout (editorStyle->renderer, context, report);
                expect (layout.has_value(), report.toText());
                if (! layout) continue;
                for (const auto& [slot, frame] : layout->slots)
                    expect (context.bounds.contains (frame), slot);
                const auto* ruleStyle = skin->resolveStyle (SkinRenderTarget::smartPlaylistRuleList,
                                                           editorStyle->slotStyles.at ("editor.rules"));
                expect (ruleStyle != nullptr);
                if (! ruleStyle) continue;
                context.bounds = context.contentBounds = layout->slots.at ("editor.rules").withPosition (0, 0);
                context.bounds.setWidth (context.bounds.getWidth() - 16);
                const auto row = resolveSkinRowLayout (ruleStyle->renderer, context, report);
                expect (row.has_value(), report.toText());
                if (row)
                {
                    expect (! row->slots.at ("rule.field").intersects (row->slots.at ("rule.operator")));
                    expect (! row->slots.at ("rule.operator").intersects (row->slots.at ("rule.value")));
                    expect (! row->slots.at ("rule.value").intersects (row->slots.at ("rule.remove")));
                }
            }
        SkinRenderer renderer;
        JuceSkinAdapter adapter ([&] (const auto&, SkinRenderTarget target, const juce::String& style)
        { return JuceSkinAdapter::RuntimeBinding { skin->resolveStyle (target, style) }; }, renderer);
        TestUiRoot host;
        host.setBounds (0, 0, 1040, 700);
        host.addToDesktop (0);
        host.setVisible (true);
        SmartPlaylistEditor editor (adapter);
        SmartPlaylistRules original;
        original.rules.push_back ({ SmartPlaylistField::artist, SmartPlaylistOperator::contains, "Example" });
        int previewed = 0, saved = 0;
        const auto count = [&] (const SmartPlaylistRules& rules) { ++previewed; return static_cast<int> (rules.rules.size()) * 3; };
        beginTest ("preview reads draft rules and cancel does not call persistence");
        editor.show (host, "Artists", original, count,
                     [&] (const auto&, const auto&) { ++saved; return juce::Result::ok(); });
        expect (previewed > 0);
        expectEquals (dynamic_cast<juce::Label*> (findEditorControl (editor, "Matching tracks and rule validation"))->getText(), juce::String ("3 matching tracks"));
        dynamic_cast<juce::TextButton*> (findEditorControl (editor, "Cancel"))->onClick();
        expect (! editor.isVisible());
        expectEquals (saved, 0);
        expectEquals (original.rules[0].textValue, juce::String ("Example"));
        beginTest ("external preview refresh coalesces and preserves the active rule input");
        int currentCount = 4;
        int refreshed = 0;
        editor.show (host, "Artists", original,
                     [&] (const SmartPlaylistRules&) { ++refreshed; return currentCount; },
                     [&] (const auto&, const auto&) { ++saved; return juce::Result::ok(); });
        auto* value = dynamic_cast<juce::TextEditor*> (findEditorControl (editor, "Rule value"));
        expect (value != nullptr);
        juce::Component::SafePointer<juce::Component> originalInput (value);
        if (value != nullptr)
        {
            value->grabKeyboardFocus();
            value->setHighlightedRegion ({ 1, 4 });
        }
        const auto beforeRefresh = refreshed;
        currentCount = 9;
        editor.refreshPreview();
        editor.refreshPreview();
        editor.refreshPreview();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        expectEquals (refreshed, beforeRefresh + 1);
        expect (originalInput != nullptr && originalInput == findEditorControl (editor, "Rule value"));
        if (value != nullptr)
        {
            expect (value->hasKeyboardFocus (true));
            expect (value->getHighlightedRegion() == juce::Range<int> (1, 4));
            expectEquals (value->getText(), juce::String ("Example"));
        }
        expectEquals (dynamic_cast<juce::Label*> (findEditorControl (editor, "Matching tracks and rule validation"))->getText(), juce::String ("9 matching tracks"));
        editor.close();
        editor.refreshPreview();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        expectEquals (refreshed, beforeRefresh + 1);

        beginTest ("Tab stays inside the editor and Escape restores the invoking control");
        juce::TextButton opener { "New smart playlist" };
        host.addAndMakeVisible (opener);
        opener.setBounds (0, 0, 160, 36);
        opener.grabKeyboardFocus();
        expect (opener.hasKeyboardFocus (true));
        editor.show (host, "Artists", original, count,
                     [&] (const auto&, const auto&) { ++saved; return juce::Result::ok(); });
        auto* nameControl = findEditorControl (editor, "Playlist name");
        auto* cancelControl = findEditorControl (editor, "Cancel");
        expect (nameControl != nullptr && cancelControl != nullptr);
        if (nameControl != nullptr && cancelControl != nullptr)
        {
            cancelControl->grabKeyboardFocus();
            host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey));
            expect (nameControl->hasKeyboardFocus (true));
            host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0));
            expect (cancelControl->hasKeyboardFocus (true));
            for (int step = 0; step < 24; ++step)
            {
                host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::tabKey));
                const auto* focused = juce::Component::getCurrentlyFocusedComponent();
                expect (focused != nullptr && editor.isParentOf (focused));
            }
            host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey));
            expect (! editor.isVisible());
            expect (opener.hasKeyboardFocus (true));
        }
        editor.close();

        beginTest ("failed saves retain draft and allow retry, while successful saves close");
        editor.show (host, "Artists", original, count,
                     [&] (const auto& name, const auto& rules)
                     {
                         ++saved;
                         expectEquals (name, juce::String ("Edited artists"));
                         expect (rules == original);
                         return saved == 1 ? juce::Result::fail ("Storage unavailable") : juce::Result::ok();
                     });
        auto* name = dynamic_cast<juce::TextEditor*> (findEditorControl (editor, "Playlist name"));
        name->setText ("Edited artists", false);
        auto* save = dynamic_cast<juce::TextButton*> (findEditorControl (editor, "Save playlist"));
        save->onClick();
        expect (editor.isVisible());
        expectEquals (name->getText(), juce::String ("Edited artists"));
        expectEquals (dynamic_cast<juce::Label*> (findEditorControl (editor, "Matching tracks and rule validation"))->getText(), juce::String ("Storage unavailable"));
        save->onClick();
        expect (! editor.isVisible());
        expectEquals (saved, 2);
        beginTest ("invalid name and rules block saving through model validation");
        original.rules[0].textValue.clear();
        editor.show (host, "Artists", original, count, [&] (const auto&, const auto&) { ++saved; return juce::Result::ok(); });
        expect (! save->isEnabled());
        save->onClick();
        expectEquals (saved, 2);
        editor.close();

        beginTest ("sixteen-rule editing keeps new and remaining rows visible, focused and intact across save and cancel");
        const auto waitFor = [] (const auto& ready)
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            while (! ready() && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            return ready();
        };
        const auto activate = [&] (juce::TextButton& button)
        {
            expect (button.isEnabled(), button.getButtonText());
            button.grabKeyboardFocus();
            expect (button.hasKeyboardFocus (true), button.getButtonText());
            expect (host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::returnKey)));
        };
        const auto fieldAt = [&] (int ordinal)
        {
            return dynamic_cast<juce::ComboBox*> (findEditorControl (
                editor, "Rule " + juce::String (ordinal) + " field"));
        };
        const auto ruleCount = [&]
        {
            int total = 0;
            const auto visit = [&] (auto&& self, juce::Component& component) -> void
            {
                if (dynamic_cast<juce::ComboBox*> (&component) != nullptr
                    && component.getName().startsWith ("Rule ") && component.getName().endsWith (" field"))
                    ++total;
                for (auto* child : component.getChildren()) self (self, *child);
            };
            visit (visit, editor);
            return total;
        };
        const auto valueAt = [&] (int ordinal) -> juce::TextEditor*
        {
            auto* field = fieldAt (ordinal);
            return field != nullptr ? dynamic_cast<juce::TextEditor*> (
                findEditorControl (*field->getParentComponent(), "Rule value")) : nullptr;
        };
        const auto expectRowVisible = [&] (juce::ComboBox& field)
        {
            auto* row = field.getParentComponent();
            juce::Viewport* viewport = nullptr;
            for (auto* ancestor = row->getParentComponent(); ancestor != nullptr; ancestor = ancestor->getParentComponent())
                if ((viewport = dynamic_cast<juce::Viewport*> (ancestor)) != nullptr) break;
            expect (viewport != nullptr, "Rules are presented inside a scrollable viewport.");
            if (viewport != nullptr && viewport->getViewedComponent() != nullptr)
            {
                expect (viewport->getViewPositionY() > 0, "A long rule list must scroll to its focused last row.");
                expect (viewport->getViewArea().contains (
                    viewport->getViewedComponent()->getLocalArea (row, row->getLocalBounds())),
                    "The focused rule's entire control row must be visible.");
            }
        };
        SmartPlaylistRules persisted;
        persisted.rules.push_back ({ SmartPlaylistField::artist, SmartPlaylistOperator::contains, "Initial artist" });
        SmartPlaylistRules previewRules;
        auto expectedRules = persisted;
        int boundarySaves = 0;
        juce::String persistedName { "Many rules" };
        const auto previewBoundary = [&] (const SmartPlaylistRules& rules)
        {
            previewRules = rules;
            return static_cast<int> (rules.rules.size());
        };
        const auto saveBoundary = [&] (const juce::String& playlistName, const SmartPlaylistRules& rules)
        {
            ++boundarySaves;
            persistedName = playlistName;
            persisted = rules;
            return juce::Result::ok();
        };
        editor.show (host, persistedName, persisted, previewBoundary, saveBoundary);
        auto* firstValue = valueAt (1);
        expect (firstValue != nullptr);
        if (firstValue == nullptr) { editor.close(); return; }
        firstValue->setText ("Retained artist", true);
        expectedRules.rules[0].textValue = "Retained artist";
        expect (waitFor ([&] { return previewRules == expectedRules; }));
        auto* addRule = dynamic_cast<juce::TextButton*> (findEditorControl (editor, "Add rule"));
        expect (addRule != nullptr);
        if (addRule == nullptr) { editor.close(); return; }
        for (int ordinal = 2; ordinal <= 16; ++ordinal)
        {
            activate (*addRule);
            expect (waitFor ([&] { return ruleCount() == ordinal && fieldAt (ordinal) != nullptr
                && fieldAt (ordinal)->hasKeyboardFocus (true); }), "New rule receives keyboard focus.");
            auto* addedValue = valueAt (ordinal);
            expect (addedValue != nullptr);
            if (addedValue == nullptr) { editor.close(); return; }
            const auto text = "Title token " + juce::String (ordinal);
            addedValue->setText (text, true);
            expectedRules.rules.push_back ({ SmartPlaylistField::title, SmartPlaylistOperator::contains, text });
            expect (waitFor ([&] { return previewRules == expectedRules; }), "Edited values survive later row additions.");
        }
        expectEquals (ruleCount(), 16);
        expect (! addRule->isEnabled());
        auto* lastField = fieldAt (16);
        expect (lastField != nullptr);
        if (lastField == nullptr) { editor.close(); return; }
        expect (lastField->hasKeyboardFocus (true));
        expectRowVisible (*lastField);
        auto* removeLast = dynamic_cast<juce::TextButton*> (findEditorControl (*lastField->getParentComponent(), "Remove"));
        expect (removeLast != nullptr);
        if (removeLast == nullptr) { editor.close(); return; }
        activate (*removeLast);
        expectedRules.rules.pop_back();
        expect (waitFor ([&] { return ruleCount() == 15 && fieldAt (16) == nullptr
            && fieldAt (15) != nullptr && fieldAt (15)->hasKeyboardFocus (true); }));
        expect (addRule->isEnabled());
        expect (previewRules == expectedRules);
        if (auto* remainingLast = fieldAt (15)) expectRowVisible (*remainingLast);
        auto* saveMany = dynamic_cast<juce::TextButton*> (findEditorControl (editor, "Save playlist"));
        expect (saveMany != nullptr);
        if (saveMany == nullptr) { editor.close(); return; }
        activate (*saveMany);
        expect (waitFor ([&] { return ! editor.isVisible(); }));
        expectEquals (boundarySaves, 1);
        expect (persisted == expectedRules);
        expectEquals (persistedName, juce::String ("Many rules"));
        editor.show (host, persistedName, persisted, previewBoundary, saveBoundary);
        expectEquals (ruleCount(), 15);
        auto* unsavedValue = valueAt (1);
        expect (unsavedValue != nullptr);
        if (unsavedValue == nullptr) { editor.close(); return; }
        unsavedValue->setText ("Unsaved replacement", true);
        expect (waitFor ([&] { return previewRules.rules[0].textValue == "Unsaved replacement"; }));
        auto* cancelMany = dynamic_cast<juce::TextButton*> (findEditorControl (editor, "Cancel"));
        expect (cancelMany != nullptr);
        if (cancelMany == nullptr) { editor.close(); return; }
        activate (*cancelMany);
        expect (waitFor ([&] { return ! editor.isVisible(); }));
        expectEquals (boundarySaves, 1);
        expect (persisted == expectedRules, "Cancel must preserve every previously saved rule.");
        editor.show (host, persistedName, persisted, previewBoundary, saveBoundary);
        expectEquals (ruleCount(), 15);
        for (int ordinal = 1; ordinal <= 15; ++ordinal)
        {
            const auto* restoredValue = valueAt (ordinal);
            expect (restoredValue != nullptr);
            if (restoredValue != nullptr)
                expectEquals (restoredValue->getText(), expectedRules.rules[static_cast<size_t> (ordinal - 1)].textValue);
        }
        editor.close();
    }
};
static SmartPlaylistEditorTests smartPlaylistEditorTests;
}
