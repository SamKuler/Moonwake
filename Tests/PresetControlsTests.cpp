#include "ui/PresetControls.h"
#include "fixtures/TestComponentPeer.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class PresetControlsTests final : public juce::UnitTest
{
public:
    PresetControlsTests() : UnitTest ("PresetControls", "skin") {}
    void runTest() override
    {
        TestUiRoot nameRoot;
        nameRoot.setWantsKeyboardFocus (true);
        nameRoot.setBounds (0, 0, 500, 260);
        nameRoot.addToDesktop (0);
        nameRoot.setVisible (true);
        PresetControls presets ("Listening setup");
        presets.addTo (nameRoot);
        presets.selector.setBounds (0, 0, 300, 32);
        presets.actions.setBounds (308, 0, 100, 32);
        const auto submitName = [&] (const juce::String& name)
        {
            presets.selector.showEditor();
            for (auto* child : presets.selector.getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    if (auto* editor = label->getCurrentTextEditor())
                    {
                        editor->setText (name, false);
                        editor->grabKeyboardFocus();
                    }
            expect (nameRoot.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::returnKey)));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        };
        int applyCount = 0, actionCount = 0, errorCount = 0;
        juce::String applied, named;
        auto actionResult = juce::Result::ok();
        auto applyResult = juce::Result::ok();
        presets.onApply = [&] (const juce::String& id) { ++applyCount; applied = id; return applyResult; };
        presets.onAction = [&] (auto, const auto&, const juce::String& name)
        {
            ++actionCount; named = name; return actionResult;
        };
        presets.onError = [&] (const auto&) { ++errorCount; };

        beginTest ("empty preset collections keep save and import available");
        presets.setState ({}, {});
        expectEquals (presets.selector.getNumItems(), 0);
        expect (presets.actions.isEnabled());
        presets.beginAction (PresetControls::Action::update);
        expectEquals (actionCount, 0);
        presets.beginAction (PresetControls::Action::importFile);
        expectEquals (actionCount, 1);

        beginTest ("applying accepts a request while the confirmed ID remains authoritative");
        presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "one");
        presets.selector.setSelectedId (2, juce::sendNotificationSync);
        expectEquals (applyCount, 1);
        expectEquals (applied, juce::String ("two"));
        expectEquals (presets.selector.getText(), juce::String ("Desk"));
        presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two", true);
        expectEquals (presets.selector.getText(), juce::String ("Headphones (modified)"));

        beginTest ("selecting the current modified preset preserves its unsaved-change marker");
        const auto beforeSameSelection = applyCount;
        presets.selector.setSelectedId (2, juce::sendNotificationSync);
        presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two", true);
        expectEquals (applyCount, beforeSameSelection);
        expectEquals (presets.selector.getText(), juce::String ("Headphones (modified)"));

        beginTest ("unchanged polling preserves the live selection without Value churn");
        {
            presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            struct SelectionChanges final : juce::Value::Listener
            {
                void valueChanged (juce::Value&) override { ++count; }
                int count = 0;
            } selectionChanges;
            auto& selectedValue = presets.selector.getSelectedIdAsValue();
            selectedValue.addListener (&selectionChanges);
            for (int iteration = 0; iteration < 5; ++iteration)
                presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            expectEquals (selectionChanges.count, 0);
            expectEquals (presets.selector.getSelectedId(), 2);
            expectEquals (presets.selector.getText(), juce::String ("Headphones"));
            presets.setState ({ { "one", "Desk" }, { "two", "Headphones updated" } }, "two", true);
            expectEquals (presets.selector.getText(), juce::String ("Headphones updated (modified)"));
            selectedValue.removeListener (&selectionChanges);
            presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two", true);
        }

        beginTest ("failed names remain editable across refresh and busy states");
        presets.beginAction (PresetControls::Action::rename);
        actionResult = juce::Result::fail ("Name already exists. Choose another name.");
        submitName ("Desk");
        expect (presets.isEditingName());
        expectEquals (named, juce::String ("Desk"));
        expectEquals (errorCount, 1);
        presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two");
        presets.setEnabled (false);
        expect (! presets.selector.isEnabled() && ! presets.actions.isEnabled());
        expectEquals (presets.selector.getText(), juce::String ("Desk"));
        presets.setEnabled (true);
        actionResult = juce::Result::ok();
        submitName ("Evening headphones");
        expect (! presets.isEditingName());
        expectEquals (named, juce::String ("Evening headphones"));

        beginTest ("blank names do not invoke persistence and cancellation restores the selection");
        presets.beginAction (PresetControls::Action::saveAs);
        const auto before = actionCount;
        submitName ("   ");
        expectEquals (actionCount, before);
        expect (presets.isEditingName());
        presets.cancelEditing();
        expect (! presets.isEditingName());
        expectEquals (presets.selector.getText(), juce::String ("Headphones"));

        beginTest ("name changes remain drafts until explicitly saved");
        presets.beginAction (PresetControls::Action::saveAs);
        const auto beforeDraft = actionCount;
        presets.selector.setText ("Unsubmitted name", juce::sendNotificationSync);
        expectEquals (actionCount, beforeDraft);
        expect (presets.isEditingName());
        presets.cancelEditing();

        beginTest ("opening Actions and choosing Cancel with the mouse never saves a name draft");
        {
            struct MenuLookAndFeel final : juce::LookAndFeel_V4
            {
                explicit MenuLookAndFeel (juce::Component& owner) : parent (owner) {}
                juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options&) override { return &parent; }
                void getIdealPopupMenuItemSize (const juce::String& text, bool separator, int height, int& width, int& itemHeight) override
                {
                    captions.addIfNotAlreadyThere (text);
                    juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text, separator, height, width, itemHeight);
                }
                juce::Component& parent;
                juce::StringArray captions;
            } menuLookAndFeel (nameRoot);
            presets.actions.setLookAndFeel (&menuLookAndFeel);
            presets.beginAction (PresetControls::Action::saveAs);
            for (auto* child : presets.selector.getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    if (auto* editor = label->getCurrentTextEditor()) editor->setText ("Cancelled mouse draft", false);
            const auto beforeCancel = actionCount;
            const auto click = [&] (juce::Component& target)
            {
                const auto point = nameRoot.getLocalPoint (&target, target.getLocalBounds().getCentre()).toFloat();
                expect (nameRoot.getComponentAt (point) == &target);
                const auto time = juce::Time::currentTimeMillis();
                auto* peer = nameRoot.getPeer();
                const juce::ScopedValueSetter<juce::ModifierKeys> modifiers (juce::ModifierKeys::currentModifiers, {});
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0, 0, time);
                juce::ModifierKeys::currentModifiers = juce::ModifierKeys::leftButtonModifier;
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point,
                                       juce::ModifierKeys::leftButtonModifier, 0, 0, time + 1);
                juce::ModifierKeys::currentModifiers = {};
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0, 0, time + 2);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            };
            click (presets.actions);
            // The modal Label editor consumes the first click while ending text input.
            if (juce::Component::getCurrentlyModalComponent() == nullptr) click (presets.actions);
            auto* menu = juce::Component::getCurrentlyModalComponent();
            expect (menuLookAndFeel.captions == juce::StringArray { "Save name", "Edit name...", "Cancel name edit" });
            const juce::Component::SafePointer<juce::Component> cancel (
                menu != nullptr && menu->getNumChildComponents() == 3 ? menu->getChildComponent (2) : nullptr);
            expect (cancel != nullptr);
            if (cancel != nullptr)
            {
                const auto point = nameRoot.getLocalPoint (cancel, cancel->getLocalBounds().getCentre()).toFloat();
                nameRoot.getPeer()->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point,
                                                     {}, 0, 0, juce::Time::currentTimeMillis());
                // Let the opening-click guard expire without polling the unrelated OS cursor.
                juce::Thread::sleep (275);
                expect (cancel != nullptr, "The name actions menu must remain available for selection.");
                if (cancel != nullptr) click (*cancel);
            }
            expectEquals (actionCount, beforeCancel);
            expect (! presets.isEditingName());
            expectEquals (presets.selector.getText(), juce::String ("Headphones"));
            juce::PopupMenu::dismissAllActiveMenus();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            presets.cancelEditing();
            presets.actions.setLookAndFeel (nullptr);
        }

        beginTest ("Revert to saved preserves modified state until successful application is confirmed");
        presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two", true);
        const auto beforeRevert = applyCount;
        applyResult = juce::Result::fail ("Device unavailable. Retry after reconnecting.");
        presets.revertToSaved();
        expectEquals (applyCount, beforeRevert + 1);
        expectEquals (applied, juce::String ("two"));
        expectEquals (presets.selector.getText(), juce::String ("Headphones (modified)"));
        applyResult = juce::Result::ok();
        presets.revertToSaved();
        expectEquals (applyCount, beforeRevert + 2);
        expectEquals (presets.selector.getText(), juce::String ("Headphones (modified)"));
        presets.setState ({ { "one", "Desk" }, { "two", "Headphones" } }, "two");
        expectEquals (presets.selector.getText(), juce::String ("Headphones"));
        presets.revertToSaved();
        expectEquals (applyCount, beforeRevert + 2);

        beginTest ("stable IDs bind the native controls");
        SkinComponentBindings bindings;
        SkinValidationReport report;
        presets.bind (bindings, "listeningPreset", report);
        expect (! report.hasErrors(), report.toText());
        expect (bindings.find ("listeningPreset.select") == &presets.selector);
        expect (bindings.find ("listeningPreset.actions") == &presets.actions);

        beginTest ("rebuilding skin controls retains the live name draft and its keyboard actions");
        {
            TestUiRoot root;
            root.setBounds (0, 0, 420, 80);
            presets.addTo (root);
            presets.selector.setBounds (0, 0, 300, 32);
            presets.actions.setBounds (308, 0, 100, 32);
            root.addToDesktop (0);
            root.setVisible (true);
            const auto editorFor = [&]() -> juce::TextEditor*
            {
                for (auto* child : presets.selector.getChildren())
                    if (auto* label = dynamic_cast<juce::Label*> (child)) return label->getCurrentTextEditor();
                return nullptr;
            };
            for (const auto save : { false, true })
            {
                presets.beginAction (PresetControls::Action::saveAs);
                auto* editor = editorFor();
                expect (editor != nullptr);
                if (editor == nullptr) continue;
                editor->setText ("Evening listening draft", false);
                editor->setHighlightedRegion ({ 3, 8 });
                const auto beforeRebuild = actionCount;
                const auto edit = presets.captureEditingState();
                presets.selector.sendLookAndFeelChange();
                presets.restoreEditingState (edit);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                editor = editorFor();
                expect (editor != nullptr);
                expectEquals (actionCount, beforeRebuild);
                if (editor == nullptr) continue;
                expectEquals (editor->getText(), juce::String ("Evening listening draft"));
                expect (editor->getHighlightedRegion() == juce::Range<int> (3, 8));
                expect (editor->hasKeyboardFocus (false));
                expect (root.getPeer()->handleKeyPress (juce::KeyPress (save ? juce::KeyPress::returnKey : juce::KeyPress::escapeKey)));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                expect (! presets.isEditingName());
                expectEquals (actionCount, beforeRebuild + (save ? 1 : 0));
                if (save) expectEquals (named, juce::String ("Evening listening draft"));
            }
            root.removeChildComponent (&presets.selector);
            root.removeChildComponent (&presets.actions);
        }
    }
};
PresetControlsTests presetControlsTests;
}
