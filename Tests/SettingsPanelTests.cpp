#include "ui/settings/AudioSettingsPanel.h"
#include "ui/settings/PluginSettingsPanel.h"
#include "ui/settings/SkinLibraryPanel.h"
#include "ui/settings/SettingsSkinContext.h"
#include "ui/SkinLayoutHost.h"
#include "plugins/PluginManager.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>

namespace
{
class PanelAudioBackend final : public IAudioDeviceBackend
{
public:
    struct Capability
    {
        juce::Array<double> rates;
        juce::Array<int> buffers;
        int outputs = 0;
    };

    PanelAudioBackend()
    {
        capabilities.emplace ("Built-in Output",
                              Capability { { 44100.0, 48000.0 }, { 128, 256 }, 2 });
        capabilities.emplace ("Aggregate 10ch",
                              Capability { { 48000.0, 96000.0 }, { 64, 128, 256 }, 10 });
        setup.outputDeviceName = "Built-in Output";
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
    }

    juce::String initialiseWithDefaultDevices (int, int) override { return {}; }
    juce::StringArray getOutputDeviceNames() const override
    {
        return { "Built-in Output", "Aggregate 10ch" };
    }
    AudioDeviceBackendState getCurrentDeviceState() const override
    {
        AudioDeviceBackendState state;
        const auto found = capabilities.find (setup.outputDeviceName);
        if (found == capabilities.end()) return state;
        state.open = true;
        state.name = setup.outputDeviceName;
        state.sampleRate = setup.sampleRate;
        state.sampleRates = found->second.rates;
        state.bufferSize = setup.bufferSize;
        state.bufferSizes = found->second.buffers;
        state.availableOutputChannels = found->second.outputs;
        return state;
    }
    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override
    {
        return setup;
    }
    juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup& requested, bool) override
    {
        if (nextError.isNotEmpty())
        {
            auto result = nextError;
            nextError.clear();
            return result;
        }
        const auto found = capabilities.find (requested.outputDeviceName);
        if (found == capabilities.end()) return "Unknown device.";
        auto applied = requested;
        if (applied.sampleRate == 0.0) applied.sampleRate = found->second.rates.getFirst();
        if (applied.bufferSize == 0) applied.bufferSize = found->second.buffers.getFirst();
        if (applied.outputChannels.countNumberOfSetBits() > found->second.outputs)
            return "Not enough outputs.";
        setup = applied;
        return {};
    }
    void addAudioCallback (juce::AudioIODeviceCallback*) override {}
    void removeAudioCallback (juce::AudioIODeviceCallback*) override {}

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    juce::String nextError;

private:
    std::map<juce::String, Capability> capabilities;
};

juce::File freshPanelDirectory (const juce::String& name)
{
    const auto result = juce::File::getCurrentWorkingDirectory()
                            .getChildFile ("build")
                            .getChildFile ("settings-panel-tests")
                            .getChildFile (name + "-" + juce::Uuid().toString());
    result.createDirectory();
    return result;
}

juce::File writeDeveloperSkin (const juce::File& root)
{
    const auto source = root.getChildFile ("developer-skin");
    source.createDirectory();
    source.getChildFile ("manifest.json").replaceWithText (R"json({
      "skinApi": 1,
      "id": "com.example.settings-developer",
      "name": "Settings Developer",
      "version": "1.0.0",
      "author": { "name": "Tests" },
      "minAppVersion": "0.1.0",
      "extends": "builtin.foundation",
      "capabilities": ["tokens"],
      "entrypoints": { "tokens": "tokens.json" }
    })json");
    source.getChildFile ("tokens.json").replaceWithText (
        R"json({"colors":{"settingsTest":"#cc8844"}})json");
    return source;
}

juce::File writeImportableSkin (const juce::File& root, int index)
{
    const auto source = root.getChildFile ("import-skin-" + juce::String (index));
    source.createDirectory();
    source.getChildFile ("manifest.json").replaceWithText (
        juce::String (R"json({
          "skinApi": 1,
          "id": "com.example.settingsskin)json") + juce::String (index) + R"json(",
          "name": "Scrollable Skin )json" + juce::String (index) + R"json(",
          "version": "1.0.0",
          "author": { "name": "Tests" },
          "minAppVersion": "0.1.0",
          "extends": "builtin.foundation",
          "capabilities": ["tokens"],
          "entrypoints": { "tokens": "tokens.json" }
        })json");
    source.getChildFile ("tokens.json").replaceWithText (
        "{\"colors\":{\"scrollProbe\":\"#"
        + juce::Colour::fromHSV (static_cast<float> (index) / 12.0f,
                                 0.5f, 0.8f, 1.0f)
              .toDisplayString (false)
        + "\"}}");
    return source;
}

juce::MouseEvent wheelEventFor (juce::Component& component)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(),
             { 1.0f, 1.0f }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &component, &component, now, { 1.0f, 1.0f }, now, 1, false };
}

class SettingsPanelTests final : public juce::UnitTest
{
public:
    SettingsPanelTests() : UnitTest ("SettingsPanel", "ui") {}

    void runTest() override
    {
        const auto expectAccessibleList = [this] (
            juce::Component& component, int expectedRows,
            juce::StringRef expectedFirstRowText, bool firstRowSelected)
        {
            juce::Component accessibilityHost;
            accessibilityHost.setBounds (0, 0,
                                          juce::jmax (1, component.getWidth()),
                                          juce::jmax (1, component.getHeight()));
            accessibilityHost.addAndMakeVisible (component);
            accessibilityHost.addToDesktop (0);
            auto* handler = component.getAccessibilityHandler();
            expect (handler != nullptr);
            if (handler == nullptr)
                return;
            expect (handler->getRole() == juce::AccessibilityRole::list);
            auto* table = handler->getTableInterface();
            expect (table != nullptr);
            if (table == nullptr)
                return;
            expectEquals (table->getNumRows(), expectedRows);
            if (expectedRows <= 0)
                return;
            auto* row = table->getRowHandler (0);
            expect (row != nullptr);
            if (row == nullptr)
                return;
            expect (row->getTitle().containsIgnoreCase (expectedFirstRowText));
            expect (row->getCurrentState().isSelected() == firstRowSelected);
        };

        beginTest ("custom settings targets expose every frozen third-party context field");
        {
            const auto channel = [&] (std::string_view path)
            {
                return SettingsSkinContext::channelMapValue (
                    path, 3, "LFE", "CoreAudio Out 4", true);
            };
            expectWithinAbsoluteError (std::get<double> (*channel ("channel.index")),
                                       3.0, 0.001);
            expectEquals (std::get<juce::String> (*channel ("channel.sourceLabel")),
                          juce::String ("LFE"));
            expectEquals (std::get<juce::String> (*channel ("channel.deviceLabel")),
                          juce::String ("CoreAudio Out 4"));
            expect (std::get<bool> (*channel ("channel.connected")));

            const auto plugin = [&] (std::string_view path)
            {
                return SettingsSkinContext::pluginListValue (
                    path, 2, "Compressor", "Sam Kuler", "VST3", true);
            };
            expectWithinAbsoluteError (std::get<double> (*plugin ("plugin.index")),
                                       2.0, 0.001);
            expectEquals (std::get<juce::String> (*plugin ("plugin.name")),
                          juce::String ("Compressor"));
            expectEquals (std::get<juce::String> (*plugin ("plugin.manufacturer")),
                          juce::String ("Sam Kuler"));
            expectEquals (std::get<juce::String> (*plugin ("plugin.format")),
                          juce::String ("VST3"));
            expect (std::get<bool> (*plugin ("plugin.enabled")));

            const auto skin = [&] (std::string_view path)
            {
                return SettingsSkinContext::skinListValue (
                    path, 1, "com.example.skin", "Example Skin", "Example Author",
                    "2.0.0", false, true, true);
            };
            expectWithinAbsoluteError (std::get<double> (*skin ("skin.index")),
                                       1.0, 0.001);
            expectEquals (std::get<juce::String> (*skin ("skin.id")),
                          juce::String ("com.example.skin"));
            expectEquals (std::get<juce::String> (*skin ("skin.name")),
                          juce::String ("Example Skin"));
            expectEquals (std::get<juce::String> (*skin ("skin.author")),
                          juce::String ("Example Author"));
            expectEquals (std::get<juce::String> (*skin ("skin.version")),
                          juce::String ("2.0.0"));
            expect (! std::get<bool> (*skin ("skin.builtin")));
            expect (std::get<bool> (*skin ("skin.active")));
            expect (std::get<bool> (*skin ("skin.previewed")));
            expect (! skin ("skin.builtIn").has_value());
        }

        beginTest ("Audio controls mirror capabilities and roll back rejected changes");
        {
            auto backend = std::make_unique<PanelAudioBackend>();
            auto* fake = backend.get();
            PlayerController controller (std::move (backend));
            AudioSettingsPanel panel (controller);

            expectEquals (panel.deviceControl().getNumItems(), 2);
            expect (panel.deviceControl().getText().contains ("Built-in Output"));
            expectEquals (panel.sampleRateControl().getNumItems(), 2);
            expectEquals (panel.bufferSizeControl().getNumItems(), 2);
            expectEquals (panel.displayedChannelCount(), 2);

            panel.deviceControl().setSelectedItemIndex (1, juce::sendNotificationSync);
            expect (panel.deviceControl().getText().contains ("Aggregate 10ch"));
            expectEquals (panel.sampleRateControl().getNumItems(), 2);
            expectEquals (panel.bufferSizeControl().getNumItems(), 3);

            panel.channelLayoutControl().setSelectedId (4, juce::sendNotificationSync);
            expectEquals (panel.displayedChannelCount(), 10);
            panel.channelMapControl().setBounds (0, 0, 420, 460);
            bool mapAcceptsClicks = true;
            bool mapChildrenAcceptClicks = true;
            panel.channelMapControl().getInterceptsMouseClicks (
                mapAcceptsClicks, mapChildrenAcceptClicks);
            expect (! panel.channelMapControl().getWantsKeyboardFocus());
            expect (! mapAcceptsClicks && ! mapChildrenAcceptClicks,
                    "Output map must not expose click or focus affordances.");
            expectAccessibleList (panel.channelMapControl(), 10, "L", false);

            panel.sampleRateControl().setSelectedItemIndex (1, juce::sendNotificationSync);
            expectWithinAbsoluteError (controller.getAudioDeviceSettings().currentSampleRate,
                                       96000.0, 0.01);
            fake->nextError = "CoreAudio rejected the rate.";
            const auto previousRate = panel.sampleRateControl().getSelectedId();
            panel.sampleRateControl().setSelectedItemIndex (0, juce::sendNotificationSync);
            expectEquals (panel.sampleRateControl().getSelectedId(), previousRate);
            expectWithinAbsoluteError (controller.getAudioDeviceSettings().currentSampleRate,
                                       96000.0, 0.01);
        }

        beginTest ("Controller persists committed skin and developer configuration");
        {
            const auto root = freshPanelDirectory ("skin-controller-state");
            const auto stateFile = root.getChildFile ("state.xml");
            const auto pluginFile = root.getChildFile ("plugins.xml");
            const SkinPersistenceState expected {
                "com.example.remembered", "2.4.0",
                root.getChildFile ("developer")
            };

            {
                PlayerController writer (std::make_unique<PanelAudioBackend>());
                writer.setSkinPersistenceState (expected);
                expect (writer.saveState (stateFile, pluginFile),
                        writer.getLastStatusMessage());
            }

            PlayerController reader (std::make_unique<PanelAudioBackend>());
            expect (reader.loadState (stateFile, pluginFile),
                    reader.getLastStatusMessage());
            const auto restored = reader.getSkinPersistenceState();
            expectEquals (restored.activeSkinId, expected.activeSkinId);
            expectEquals (restored.activeSkinVersion, expected.activeSkinVersion);
            expectEquals (restored.developerDirectory.getFullPathName(),
                          expected.developerDirectory.getFullPathName());
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("Controller reports every state and plug-in cache save failure");
        {
            const auto root = freshPanelDirectory ("save-errors");
            const auto stateParent = root.getChildFile ("state-parent");
            const auto pluginParent = root.getChildFile ("plugin-parent");
            expect (stateParent.replaceWithText ("not a directory"));
            expect (pluginParent.replaceWithText ("not a directory"));

            PlayerController controller (std::make_unique<PanelAudioBackend>());
            expect (! controller.saveState (stateParent.getChildFile ("state.xml"),
                                            pluginParent.getChildFile ("plugins.xml")));
            const auto status = controller.getLastStatusMessage();
            expect (status.containsIgnoreCase ("application state"), status);
            expect (status.containsIgnoreCase ("plug-in cache"), status);
            expect (status.contains (stateParent.getFullPathName()), status);
            expect (status.contains (pluginParent.getFullPathName()), status);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("Plug-in controls expose formats and mutually exclusive scan actions");
        {
            PlayerController controller (std::make_unique<PanelAudioBackend>());
            PluginSettingsPanel panel (controller);
            panel.refreshInventory();
            panel.refreshScanState();
            expect (panel.formatsControl().getText().containsIgnoreCase ("VST3"));
            expect (panel.formatsControl().getText().containsIgnoreCase ("AudioUnit")
                    || panel.formatsControl().getText().containsIgnoreCase ("AU"));
            expect (panel.scanAllControl().isEnabled());
            expect (panel.scanFolderControl().isEnabled());
            expect (! panel.cancelControl().isEnabled());
            expectEquals (panel.knownPluginCount(), controller.getKnownPlugins().size());
        }

        beginTest ("Long plug-in inventories support keyboard paging and visible scroll state");
        {
            const auto root = freshPanelDirectory ("plugin-scroll");
            const auto cache = root.getChildFile ("plugins.xml");
            PluginManager writer;
            for (int index = 0; index < 20; ++index)
            {
                juce::PluginDescription description;
                description.name = "Effect " + juce::String (index).paddedLeft ('0', 2);
                description.pluginFormatName = index % 2 == 0 ? "VST3" : "AudioUnit";
                description.fileOrIdentifier = "/tmp/Effect" + juce::String (index);
                description.uniqueId = 1000 + index;
                writer.getKnownPluginList().addType (description);
            }
            juce::String cacheError;
            expect (writer.writeKnownPluginCacheAtomically (cache, cacheError), cacheError);

            PlayerController controller (std::make_unique<PanelAudioBackend>());
            static_cast<void> (controller.loadState (root.getChildFile ("missing-state.xml"),
                                                     cache));
            PluginSettingsPanel panel (controller);
            panel.knownListControl().setBounds (0, 0, 320, 150);
            panel.refreshInventory();
            expectEquals (panel.knownPluginCount(), 20);
            expectAccessibleList (panel.knownListControl(), 20,
                                  "Effect 00", false);
            expect (panel.knownListControl().keyPressed (
                juce::KeyPress (juce::KeyPress::downKey)));
            expectEquals (panel.knownListScrollPosition(), 1);
            if (auto* list = dynamic_cast<juce::ListBox*> (
                    &panel.knownListControl()))
                expectEquals (list->getViewport()->getViewPositionY(), 32);
            else
                expect (false, "Known plug-in control must remain a native ListBox.");
            expect (panel.knownListControl().keyPressed (
                juce::KeyPress (juce::KeyPress::upKey)));
            expectEquals (panel.knownListScrollPosition(), 0);
            expect (panel.knownListControl().keyPressed (
                juce::KeyPress (juce::KeyPress::pageDownKey)));
            expect (panel.knownListScrollPosition() > 1);
            expect (panel.knownListControl().keyPressed (
                juce::KeyPress (juce::KeyPress::pageUpKey)));
            expectEquals (panel.knownListScrollPosition(), 0);

            const auto wheelEvent = wheelEventFor (panel.knownListControl());
            panel.knownListControl().mouseWheelMove (
                wheelEvent, { 0.0f, -1.0f, false, false, false });
            expectEquals (panel.knownListScrollPosition(), 1);
            panel.knownListControl().mouseWheelMove (
                wheelEvent, { 0.0f, 1.0f, false, false, false });
            expectEquals (panel.knownListScrollPosition(), 0);

            expect (panel.knownListControl().keyPressed (
                juce::KeyPress (juce::KeyPress::endKey)));
            expect (panel.knownListScrollPosition() > 0);
            expect (panel.knownListControl().keyPressed (
                juce::KeyPress (juce::KeyPress::homeKey)));
            expectEquals (panel.knownListScrollPosition(), 0);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("Skin actions distinguish selection preview apply cancel and developer reload");
        {
            const auto root = freshPanelDirectory ("skins");
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installed"), report),
                    report.toText());
            for (int index = 0; index < 8; ++index)
            {
                const auto importReport = manager.importSkin (
                    writeImportableSkin (root, index));
                expect (! importReport.hasErrors(), importReport.toText());
            }
            const auto descriptions = manager.installedSkinDescriptions();
            const auto imported = std::find_if (
                descriptions.begin(), descriptions.end(), [] (const auto& description)
                {
                    return description.selection.id == "com.example.settingsskin0";
                });
            expect (imported != descriptions.end());
            if (imported != descriptions.end())
            {
                expectEquals (imported->name, juce::String ("Scrollable Skin 0"));
                expectEquals (imported->author, juce::String ("Tests"));
                expect (! imported->builtIn);
            }
            SkinLibraryPanel panel (manager);
            panel.refresh();
            expect (panel.skinCount() >= 10);
            panel.libraryControl().setBounds (0, 0, 320, 170);
            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::homeKey)));
            expectAccessibleList (panel.libraryControl(), panel.skinCount(),
                                  panel.selectedSkin()->id, true);
            const auto firstSkin = panel.selectedSkin();
            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::downKey)));
            expect (panel.selectedSkin() != firstSkin);
            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::upKey)));
            expect (panel.selectedSkin() == firstSkin);
            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::pageDownKey)));
            expect (panel.libraryScrollPosition() > 0);
            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::pageUpKey)));
            expectEquals (panel.libraryScrollPosition(), 0);

            const auto wheelEvent = wheelEventFor (panel.libraryControl());
            panel.libraryControl().mouseWheelMove (
                wheelEvent, { 0.0f, -1.0f, false, false, false });
            expectEquals (panel.libraryScrollPosition(), 1);
            if (auto* list = dynamic_cast<juce::ListBox*> (
                    &panel.libraryControl()))
                expectEquals (list->getViewport()->getViewPositionY(), 56);
            else
                expect (false, "Skin library control must remain a native ListBox.");
            panel.libraryControl().mouseWheelMove (
                wheelEvent, { 0.0f, 1.0f, false, false, false });
            expectEquals (panel.libraryScrollPosition(), 0);

            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::endKey)));
            expect (panel.libraryScrollPosition() > 0);
            expect (panel.selectedSkin() == manager.installedSkins().back());
            expect (panel.libraryControl().keyPressed (
                juce::KeyPress (juce::KeyPress::homeKey)));
            expectEquals (panel.libraryScrollPosition(), 0);

            const SkinSelection foundation { "builtin.foundation", "1.0.0" };
            expect (panel.selectSkin (foundation));
            expect (! panel.removeControl().isEnabled());
            expect (panel.previewControl().isEnabled());
            panel.previewControl().onClick();
            expect (manager.isPreviewing());
            expect (panel.applyControl().isEnabled());
            expect (panel.cancelPreviewControl().isEnabled());
            panel.cancelPreviewControl().onClick();
            expect (! manager.isPreviewing());
            expect (manager.activeSelection()
                    == SkinSelection { "builtin.default", "1.0.0" });

            expect (panel.selectSkin (foundation));
            panel.previewControl().onClick();
            panel.applyControl().onClick();
            expect (! manager.isPreviewing());
            expect (manager.committedSelection() == foundation);

            const auto developer = writeDeveloperSkin (root);
            panel.setDeveloperDirectory (developer);
            expect (manager.developerDirectory() == developer);
            expect (manager.isPreviewing());
            expect (manager.activeSelection().id
                    == juce::String ("com.example.settings-developer"));
            expect (! panel.applyControl().isEnabled());
            expect (panel.cancelPreviewControl().isEnabled());
            manager.cancelPreview();
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("The real Settings route places every public panel leaf declaratively");
        {
            const auto rootDirectory = freshPanelDirectory ("layout-host");
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (rootDirectory.getChildFile ("installed"), report),
                    report.toText());
            PlayerController controller (std::make_unique<PanelAudioBackend>());
            AudioSettingsPanel audio (controller);
            PluginSettingsPanel plugins (controller);
            SkinLibraryPanel skins (manager);
            juce::Component root;
            juce::Label title;
            juce::TextButton close { "Close" };
            root.addAndMakeVisible (title);
            root.addAndMakeVisible (close);
            audio.addTo (root);
            plugins.addTo (root);
            skins.addTo (root);

            SkinComponentBindings bindings;
            expect (bindings.bind ("settings.title", title, report));
            expect (bindings.bind ("settings.close", close, report));
            audio.bind (bindings, report);
            plugins.bind (bindings, report);
            skins.bind (bindings, report);
            expect (! report.hasErrors(), report.toText());

            SkinLayoutHost host (manager, bindings);
            root.setSize (1240, 800);
            expect (host.apply (SkinLayoutEntry::settings, root.getLocalBounds(),
                                1.0f, report),
                    report.toText());
            expect (audio.deviceControl().isVisible(), "Audio device control must be visible.");
            expect (audio.headingControl().isVisible(), "Audio heading must be visible.");
            const auto audioHeadingBounds = root.getLocalArea (
                &audio.headingControl(), audio.headingControl().getLocalBounds());
            const auto pluginHeadingBounds = root.getLocalArea (
                &plugins.formatsControl(), plugins.formatsControl().getLocalBounds());
            const auto skinHeadingBounds = root.getLocalArea (
                &skins.headingControl(), skins.headingControl().getLocalBounds());
            expectEquals (audioHeadingBounds.getY(), pluginHeadingBounds.getY());
            expect (skinHeadingBounds.getY() > pluginHeadingBounds.getY(),
                    "Skins must be stacked below plug-ins; plugin y="
                        + juce::String (pluginHeadingBounds.getY())
                        + ", skins y="
                        + juce::String (skinHeadingBounds.getY()));
            expectEquals (pluginHeadingBounds.getX(), skinHeadingBounds.getX());
            expect (audio.deviceControl().getBounds().getWidth() > 340,
                    "Audio column must retain a usable width.");
            expect (plugins.knownListControl().isVisible(),
                    "Plug-in list must be visible.");
            expect (plugins.knownListControl().getBounds().getHeight() > 180,
                    "Plug-in list must retain useful vertical space.");
            expect (skins.libraryControl().isVisible(),
                    "Skin library must be visible.");
            auto* skinList = dynamic_cast<juce::ListBox*> (&skins.libraryControl());
            expect (skinList != nullptr);
            if (skinList != nullptr)
            {
                expectEquals (skinList->getRowHeight(), 56);
                expect (skinList->getHeaderComponent() == nullptr);
            }
            const auto importBounds = root.getLocalArea (
                &skins.importControl(), skins.importControl().getLocalBounds());
            const auto libraryBounds = root.getLocalArea (
                &skins.libraryControl(), skins.libraryControl().getLocalBounds());
            expectEquals (libraryBounds.getHeight(), 112);
            expect (importBounds.getY() > libraryBounds.getY());
            expect (importBounds.getY() - libraryBounds.getBottom() <= 12);
            const auto applyBounds = root.getLocalArea (
                &skins.applyControl(), skins.applyControl().getLocalBounds());
            const auto cancelBounds = root.getLocalArea (
                &skins.cancelPreviewControl(),
                skins.cancelPreviewControl().getLocalBounds());
            expectEquals (applyBounds.getY(), cancelBounds.getY());
            expectEquals (applyBounds.getHeight(), cancelBounds.getHeight());

            root.setSize (1040, 700);
            expect (host.apply (SkinLayoutEntry::settings, root.getLocalBounds(),
                                1.0f, report),
                    report.toText());
            const auto compactAudio = root.getLocalArea (
                &audio.deviceControl(), audio.deviceControl().getLocalBounds());
            const auto compactPlugins = root.getLocalArea (
                &plugins.knownListControl(), plugins.knownListControl().getLocalBounds());
            const auto compactSkins = root.getLocalArea (
                &skins.libraryControl(), skins.libraryControl().getLocalBounds());
            expect (compactAudio.getWidth() >= 330);
            expect (compactPlugins.getX() > compactAudio.getX());
            expect (compactSkins.getX() == compactPlugins.getX());
            expect (compactSkins.getY() > compactPlugins.getY());
            expect (! compactPlugins.intersects (compactSkins));
            host.clear();
            static_cast<void> (rootDirectory.deleteRecursively());
        }
    }
};

SettingsPanelTests settingsPanelTests;
} // namespace
