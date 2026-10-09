#include "ui/settings/MusicDirectoriesPanel.h"
#include "ui/settings/SettingsSkinContext.h"

namespace
{
class MusicDirectoriesPanelTests final : public juce::UnitTest
{
public:
    MusicDirectoriesPanelTests() : UnitTest ("MusicDirectoriesPanel", "skin") {}
    void runTest() override
    {
        SkinRenderer renderer;
        JuceSkinAdapter adapter ([] (const auto&, auto, const auto&) { return JuceSkinAdapter::RuntimeBinding {}; }, renderer);
        MusicDirectoriesPanel panel (adapter);
        juce::Component host;
        panel.addTo (host);
        SkinComponentBindings bindings;
        SkinValidationReport report;
        panel.bind (bindings, report);
        const auto button = [&] (const char* id) { return dynamic_cast<juce::TextButton*> (bindings.find (id)); };
        auto* list = dynamic_cast<juce::ListBox*> (bindings.find ("settings.music.directories"));
        auto* automatic = dynamic_cast<juce::ToggleButton*> (bindings.find ("settings.music.automatic"));
        beginTest ("registered folders are visible rows with stable selection and independent automatic state");
        expect (! report.hasErrors(), report.toText());
        expect (list != nullptr && automatic != nullptr);
        if (list == nullptr || automatic == nullptr) return;
        const MusicDirectory first { "one", juce::File ("/music/first"), true };
        const MusicDirectory second { "two", juce::File ("/music/second"), false };
        panel.refresh ({ first, second });
        expectEquals (list->getListBoxModel()->getNumRows(), 2);
        list->selectRow (1);
        expectEquals (panel.selectedDirectoryId(), juce::String ("two"));
        expect (! automatic->getToggleState());
        panel.refresh ({ second, first });
        expectEquals (panel.selectedDirectoryId(), juce::String ("two"));
        expectEquals (list->getSelectedRow(), 0);
        juce::String removed, changed;
        bool monitored = false;
        panel.onRemove = [&] (const auto& id) { removed = id; };
        panel.onAutomaticChanged = [&] (const auto& id, bool enabled) { changed = id; monitored = enabled; };
        button ("settings.music.remove")->onClick();
        expectEquals (removed, juce::String ("two"));
        automatic->setToggleState (true, juce::dontSendNotification);
        automatic->onClick();
        expectEquals (changed, juce::String ("two"));
        expect (monitored);
        beginTest ("scan state prevents conflicting edits and exposes cancellation");
        panel.setScanState (true, "Scanning first");
        expect (! button ("settings.music.add")->isEnabled());
        expect (! button ("settings.music.scan")->isEnabled());
        expect (! button ("settings.music.remove")->isEnabled());
        expect (! automatic->isEnabled());
        expect (button ("settings.music.cancel")->isEnabled());
        int cancelled = 0;
        panel.onCancel = [&] { ++cancelled; };
        button ("settings.music.cancel")->onClick();
        expectEquals (cancelled, 1);
        panel.setScanState (false, "Scan complete");
        panel.refresh ({});
        expect (panel.selectedDirectoryId().isEmpty());
        expect (! button ("settings.music.remove")->isEnabled());
        expect (! button ("settings.music.scan")->isEnabled());
        expect (button ("settings.music.add")->isEnabled());
        beginTest ("directory slot context exposes declared metadata only");
        expect (SettingsSkinContext::directoryListValue ("directory.automatic", 0, "one", "first", "/music/first", true)
            == std::optional<SkinExpressionValue> (true));
        expect (! SettingsSkinContext::directoryListValue ("directory.secret", 0, "one", "first", "/music/first", true));
    }
};
static MusicDirectoriesPanelTests musicDirectoriesPanelTests;
}
