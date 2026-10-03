#include "ui/settings/AudioSettingsPanel.h"
#include "ui/settings/PluginSettingsPanel.h"
#include "ui/settings/SkinLibraryPanel.h"
#include "ui/settings/SettingsSkinContext.h"
#include "ui/SkinLayoutHost.h"
#include "plugins/PluginManager.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"

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

    juce::String initialiseWithDefaultDevices (int, int) override { return initialisationError; }
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
        state.activeOutputChannels = setup.outputChannels;
        for (int channel = 0; channel < found->second.outputs; ++channel)
            state.outputChannelNames.add ("Line Output " + juce::String (channel + 1));
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
    juce::String initialisationError;

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

SkinRenderStyleEntry geometrySlot (juce::Colour colour)
{
    SkinRenderStyleEntry entry;
    entry.renderer.target = SkinRenderTarget::label;
    entry.renderer.worstCaseOperations = 1;
    SkinRenderLayer layer;
    layer.id = "geometry";
    layer.primitive = SkinPrimitive::rectangle;
    const auto property = [&] (const juce::String& name, SkinCompiledRenderValue value)
    {
        const auto* descriptor = skinRenderPropertyDescriptor (layer.primitive, name);
        layer.properties[name] = { std::move (value), descriptor->fallback,
                                    descriptor, false, {}, {}, 0, {} };
    };
    property ("frame.anchor", juce::String ("bounds"));
    property ("visible", true);
    property ("opacity", 1.0);
    property ("transform.scaleX", 1.0);
    property ("transform.scaleY", 1.0);
    property ("fill", colour);
    entry.renderer.baseLayers.push_back (std::move (layer));
    return entry;
}

juce::Rectangle<int> colourBounds (const juce::Image& image, juce::Colour colour)
{
    juce::Rectangle<int> result;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt (x, y) == colour)
                result = result.getUnion ({ x, y, 1, 1 });
    return result;
}

struct PanelSkin
{
    PanelSkin() : adapter ([this] (const juce::Component&, SkinRenderTarget target,
                                   const juce::String& style)
    {
        return JuceSkinAdapter::RuntimeBinding { compiled ? compiled->resolveStyle (target, style) : nullptr };
    }, renderer)
    {
        const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
        if (package) compiled = SkinCompiler::compile (*package, {}, report);
    }

    SkinValidationReport report;
    std::shared_ptr<const CompiledSkin> compiled;
    SkinRenderer renderer;
    JuceSkinAdapter adapter;
};

SkinRenderPropertyBinding rowMetric (juce::StringRef name, double value)
{
    const auto& descriptor = skinRowLayoutMetricDescriptor (name);
    return { SkinCompiledRenderValue { value }, descriptor.fallback,
             &descriptor, false, {}, {}, 0, {} };
}

SkinRenderPropertyBinding rowFrameNumber (juce::StringRef field, double value)
{
    const auto* descriptor = skinRenderPropertyDescriptor (
        SkinPrimitive::rectangle, "frame." + juce::String (field));
    return { SkinCompiledRenderValue { value }, descriptor->fallback,
             descriptor, false, {}, {}, 0, {} };
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

        PanelSkin foundationSkin;
        beginTest ("settings text slots keep skin-defined insets, column gaps and separated metadata lines");
        {
            expect (foundationSkin.compiled != nullptr, foundationSkin.report.toText());
            if (! foundationSkin.compiled) return;
            std::map<juce::String, SkinRenderStyleEntry> slots {
                { "name", geometrySlot (juce::Colours::red) },
                { "detail", geometrySlot (juce::Colours::green) },
                { "data", geometrySlot (juce::Colours::blue) },
                { "status", geometrySlot (juce::Colours::yellow) }
            };
            std::map<SkinRenderTarget, SkinRenderStyleEntry> owners;
            for (const auto target : { SkinRenderTarget::channelMap,
                                       SkinRenderTarget::pluginList, SkinRenderTarget::skinList })
            {
                auto& entry = owners[target];
                entry = *foundationSkin.compiled->resolveStyle (target, {});
                entry.slotStyles = {
                    { "header.label", "name" }, { "empty.message", "status" },
                    { "row.source", "name" }, { "row.destination", "data" },
                    { "row.name", "name" }, { "row.manufacturer", "detail" },
                    { "row.format", "data" }, { "row.author", "detail" },
                    { "row.version", "data" }, { "row.status", "status" }
                };
                entry.renderer.rowLayout->base.headerHeight = rowMetric ("headerHeight", 32);
            }
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target,
                                          const juce::String& style)
            {
                if (target == SkinRenderTarget::label)
                {
                    const auto found = slots.find (style);
                    return JuceSkinAdapter::RuntimeBinding {
                        found != slots.end() ? &found->second : nullptr };
                }
                const auto found = owners.find (target);
                return JuceSkinAdapter::RuntimeBinding {
                    found != owners.end() ? &found->second : nullptr };
            }, renderer);
            PlayerController controller (std::make_unique<PanelAudioBackend>());
            juce::KnownPluginList plugins;
            juce::PluginDescription description;
            description.name = "Concert Hall Reverb";
            description.manufacturerName = "Example Audio";
            description.pluginFormatName = "AudioUnit";
            description.fileOrIdentifier = "test:concert-hall";
            plugins.addType (description);
            description.name = "Stereo Delay";
            description.fileOrIdentifier = "test:stereo-delay";
            description.uniqueId = 2;
            plugins.addType (description);
            StateRestoreResult restored;
            restored.pluginCache = plugins.createXml();
            expect (controller.applyPreparedState (std::move (restored)));
            AudioSettingsPanel audio (controller, &adapter);
            PluginSettingsPanel effects (controller, &adapter);
            const auto paintRow = [&] (juce::Component& component, int width)
            {
                auto* list = dynamic_cast<juce::ListBox*> (&component);
                expect (list != nullptr);
                juce::Image image (juce::Image::ARGB, width, list->getRowHeight(), true);
                juce::Graphics graphics (image);
                list->getListBoxModel()->paintListBoxItem (0, graphics, width, list->getRowHeight(), false);
                return image;
            };
            for (auto* component : { &audio.channelMapControl(), &effects.knownListControl() })
            {
                component->setBounds (0, 0, 520, 160);
                auto* list = dynamic_cast<juce::ListBox*> (component);
                expect (list != nullptr);
                if (list == nullptr) continue;
                expectEquals (list->getOutlineThickness(), 0);
                const auto row = paintRow (*component, 520);
                auto* header = list->getHeaderComponent();
                const auto image = header->createComponentSnapshot (header->getLocalBounds());
                expectEquals (colourBounds (image, juce::Colours::red).getX(), 10);
                expectEquals (colourBounds (row, juce::Colours::red).getX(), 10);
            }
            for (const auto width : { 180, 320, 520 })
            {
                const auto image = paintRow (effects.knownListControl(), width);
                const auto name = colourBounds (image, juce::Colours::red);
                const auto maker = colourBounds (image, juce::Colours::green);
                const auto format = colourBounds (image, juce::Colours::blue);
                expect (! name.isEmpty() && ! maker.isEmpty() && ! format.isEmpty());
                expect (! name.intersects (maker) && ! name.intersects (format)
                        && ! maker.intersects (format));
                expect (format.getX() - juce::jmax (name.getRight(), maker.getRight()) >= 12);
                if (width < 420)
                {
                    expect (maker.getY() - name.getBottom() >= 2);
                    expect (maker.getHeight() >= 15);
                }
                else expect (maker.getX() - name.getRight() >= 12);
                expectEquals (format.getRight(), width - 10);
            }

            const auto root = freshPanelDirectory ("text-geometry");
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installed"), report), report.toText());
            SkinLibraryPanel skins (manager, &adapter);
            skins.libraryControl().setBounds (0, 0, 520, 112);
            if (auto* list = dynamic_cast<juce::ListBox*> (&skins.libraryControl()))
            {
                expectEquals (skins.skinCount(), 2);
                expectEquals (list->getOutlineThickness(), 0);
                expectEquals (list->getRowHeight(), 56);
                expectEquals (list->getViewport()->getMaximumVisibleHeight(), 112);
                expectEquals (list->getRowPosition (1, true).getBottom(), 112);
                expect (! list->getVerticalScrollBar().isVisible());
                expect (! list->getHorizontalScrollBar().isVisible());
            }
            else
            {
                expect (false, "Skin library must remain a native ListBox.");
            }
            for (const auto width : { 180, 320, 520 })
            {
                const auto image = paintRow (skins.libraryControl(), width);
                const auto name = colourBounds (image, juce::Colours::red);
                const auto author = colourBounds (image, juce::Colours::green);
                const auto version = colourBounds (image, juce::Colours::blue);
                const auto status = colourBounds (image, juce::Colours::yellow);
                expect (! name.isEmpty() && ! author.isEmpty());
                expect (author.getY() - name.getBottom() >= 2);
                expect (version.getX() - name.getRight() >= 8);
                expect (status.getX() - version.getRight() >= 8);
                expect (status.getRight() <= width - 14);
            }
            expectEquals (effects.scanAllControl().getButtonText(), juce::String ("Scan"));
            expectEquals (effects.scanFolderControl().getButtonText(), juce::String ("Add folder"));
            expectEquals (skins.importControl().getButtonText(), juce::String ("Import skin"));
            expectEquals (skins.applyControl().getButtonText(), juce::String ("Apply"));

            beginTest ("skin row profiles control native row hit testing, scroll offsets and equal-bounds reflow");
            const std::array targets { SkinRenderTarget::channelMap, SkinRenderTarget::pluginList, SkinRenderTarget::skinList };
            const std::array<juce::Component*, 3> lists {
                &audio.channelMapControl(), &effects.knownListControl(), &skins.libraryControl() };
            for (size_t index = 0; index < lists.size(); ++index)
            {
                auto& profile = *owners[targets[index]].renderer.rowLayout;
                profile.breakpoints.clear();
                profile.base.rowHeight = rowMetric ("rowHeight", 64.0);
                profile.base.rowGap = rowMetric ("rowGap", 8.0);
                profile.base.headerHeight = rowMetric ("headerHeight", index == 2 ? 0.0 : 26.0);
                const auto nameSlot = index == 0 ? "row.source" : "row.name";
                profile.base.slots[nameSlot].x = rowFrameNumber ("x", 27.0);
                if (index != 2)
                    profile.base.slots["header.label"].x = rowFrameNumber ("x", 23.0);
                auto* list = dynamic_cast<juce::ListBox*> (lists[index]);
                const auto headerHeight = index == 2 ? 0 : 26;
                list->setBounds (0, 0, 520, headerHeight + 72);
                expectEquals (list->getRowHeight(), 72);
                expectEquals (list->getRowPosition (1, true).getY(), headerHeight + 72);
                expectEquals (list->getRowContainingPosition (20, headerHeight + 5), 0);
                list->getViewport()->setViewPosition (0, 72);
                expectEquals (list->getViewport()->getViewPositionY(), 72);
                expectEquals (list->getRowContainingPosition (20, headerHeight + 5), 1);
                expectEquals (colourBounds (paintRow (*list, 520), juce::Colours::red).getX(), 27);
                if (const auto* header = list->getHeaderComponent())
                    expectEquals (header->getHeight(), 26);

                profile.base.rowHeight = rowMetric ("rowHeight", 80.0);
                list->resized();
                expectEquals (list->getRowHeight(), 88,
                              "Reapplying a skin at unchanged component bounds updates row geometry.");
                expectEquals (list->getRowPosition (1, false).getY()
                                  - list->getRowPosition (0, false).getY(), 88);

                profile.base.rowHeight = rowMetric ("rowHeight", 20.0);
                profile.base.rowGap = rowMetric ("rowGap", 0.0);
                const auto probeWidth = 410 + list->getViewport()->getScrollBarThickness() / 2;
                list->setSize (probeWidth, headerHeight + 72);
                expect (! list->getVerticalScrollBar().isVisible(),
                        "The compact skin starts without a scrollbar.");
                profile.base.rowHeight = rowMetric ("rowHeight", 64.0);
                profile.base.rowGap = rowMetric ("rowGap", 8.0);
                SkinRowLayoutDefinition::Breakpoint wide;
                wide.minWidth = 410.0f;
                wide.values.rowHeight = rowMetric ("rowHeight", 80.0);
                profile.breakpoints.push_back (std::move (wide));
                list->resized();
                expect (list->getViewport()->getMaximumVisibleWidth() < 410,
                        "viewport=" + juce::String (list->getViewport()->getMaximumVisibleWidth())
                            + ", scrollbar=" + juce::String (list->getViewport()->getScrollBarThickness())
                            + ", visible=" + juce::String (list->getVerticalScrollBar().isVisible() ? 1 : 0));
                expectEquals (list->getRowHeight(), 72,
                              "Width breakpoints use the same available row width as row painting.");
            }

            effects.knownListControl().setSize (420, 46);
            if (auto* list = dynamic_cast<juce::ListBox*> (&effects.knownListControl()))
            {
                expectEquals (list->getViewport()->getMaximumVisibleHeight(), 20,
                              "A skin row taller than the available viewport must not hide the entire list.");
                expect (list->getRowContainingPosition (20, 31) >= 0);
            }

            beginTest ("empty plugin text uses the profile frame in component coordinates");
            PlayerController emptyController (std::make_unique<PanelAudioBackend>());
            PluginSettingsPanel emptyPlugins (emptyController, &adapter);
            emptyPlugins.knownListControl().setBounds (0, 0, 520, 180);
            const auto emptyImage = emptyPlugins.knownListControl().createComponentSnapshot (
                emptyPlugins.knownListControl().getLocalBounds());
            const auto empty = colourBounds (emptyImage, juce::Colours::yellow);
            expect (empty == juce::Rectangle<int> (10, 36, 500, 134), empty.toString());
            static_cast<void> (root.deleteRecursively());
        }

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

        beginTest ("Controller exposes only actual device initialisation failures");
        {
            PlayerController ready (std::make_unique<PanelAudioBackend>());
            expect (ready.getAudioInitialisationError().isEmpty());
            auto backend = std::make_unique<PanelAudioBackend>();
            backend->initialisationError = "CoreAudio could not open the output device.";
            PlayerController failed (std::move (backend));
            expectEquals (failed.getAudioInitialisationError(),
                          juce::String ("CoreAudio could not open the output device."));
            failed.clearPlaylist();
            expect (failed.getAudioInitialisationError().isNotEmpty(),
                    "Unrelated status messages do not overwrite the startup diagnostic.");
        }

        beginTest ("channel routing presents the active device ports in callback order");
        {
            auto backend = std::make_unique<PanelAudioBackend>();
            PlayerController controller (std::move (backend));
            expect (controller.selectOutputDevice ("Aggregate 10ch").isEmpty());
            expect (controller.setOutputRouting ({ 2, 7 }).isEmpty());
            AudioSettingsPanel panel (controller, &foundationSkin.adapter);
            panel.refresh();
            auto* list = dynamic_cast<juce::ListBox*> (&panel.routingControl());
            expect (list != nullptr);
            if (list)
            {
                expectEquals (list->getListBoxModel()->getNameForRow (0), juce::String ("L to 3 - Line Output 3. Press Return to change output."));
                expectEquals (list->getListBoxModel()->getNameForRow (1), juce::String ("R to 8 - Line Output 8. Press Return to change output."));
                expect (controller.setOutputRouting ({ -1, -1 }).isEmpty());
                panel.refresh();
                expectEquals (list->getListBoxModel()->getNameForRow (0), juce::String ("L to Disconnected. Press Return to change output."));
                expectEquals (list->getListBoxModel()->getNameForRow (1), juce::String ("R to Disconnected. Press Return to change output."));
            }
        }

        beginTest ("Audio controls mirror capabilities and roll back rejected changes");
        {
            auto backend = std::make_unique<PanelAudioBackend>();
            auto* fake = backend.get();
            PlayerController controller (std::move (backend));
            AudioSettingsPanel panel (controller, &foundationSkin.adapter);
            std::vector<std::pair<juce::String, bool>> notices;
            panel.setStatusCallback ([&] (const juce::String& message, bool isError)
            {
                notices.emplace_back (message, isError);
            });
            panel.refresh();
            expect (notices.empty(), "Refreshing capabilities does not announce an operation.");

            expectEquals (panel.deviceControl().getNumItems(), 2);
            expect (panel.deviceControl().getText().contains ("Built-in Output"));
            expectEquals (panel.sampleRateControl().getNumItems(), 2);
            expectEquals (panel.bufferSizeControl().getNumItems(), 2);
            expectEquals (panel.displayedChannelCount(), 2);
            expect (panel.channelLayoutControl().isItemEnabled (static_cast<int> (OutputLayout::stereo) + 1));
            expect (! panel.channelLayoutControl().isItemEnabled (static_cast<int> (OutputLayout::fiveOne) + 1));

            panel.deviceControl().setSelectedItemIndex (1, juce::sendNotificationSync);
            expect (panel.deviceControl().getText().contains ("Aggregate 10ch"));
            expect (! notices.empty() && ! notices.back().second,
                    "A successful user device change is informational.");
            expectEquals (panel.sampleRateControl().getNumItems(), 2);
            expectEquals (panel.bufferSizeControl().getNumItems(), 3);
            expect (panel.channelLayoutControl().isItemEnabled (static_cast<int> (OutputLayout::sevenOneTwo) + 1));
            expect (! panel.channelLayoutControl().isItemEnabled (static_cast<int> (OutputLayout::nineOneSix) + 1));

            panel.channelLayoutControl().setSelectedId (4, juce::sendNotificationSync);
            expectEquals (panel.displayedChannelCount(), 10);
            panel.routingControl().setBounds (0, 0, 420, 460);
            expect (panel.routingControl().getWantsKeyboardFocus());
            expectAccessibleList (panel.routingControl(), 10, "L", false);

            auto* map = dynamic_cast<juce::ListBox*> (&panel.routingControl());
            expect (map != nullptr);
            if (map != nullptr)
            {
                map->setSize (420, 180);
                auto* viewport = map->getViewport();
                auto& scrollbar = viewport->getVerticalScrollBar();
                expect (scrollbar.isVisible(), "Compact channel maps need scrolling.");
                expect (map->isParentOf (map->getComponentAt (viewport->getBounds().getCentre())),
                        "Routing rows receive pointer events for port selection.");
                const auto scrollbarPoint = map->getLocalArea (
                    &scrollbar, scrollbar.getLocalBounds()).getCentre();
                expect (map->getComponentAt (scrollbarPoint) == &scrollbar,
                        "The channel map scrollbar remains reachable.");
                map->mouseWheelMove (wheelEventFor (*map),
                                     { 0.0f, -5.0f, false, false, false });
                const auto scrollDeadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
                while (viewport->getViewPositionY() == 0
                       && juce::Time::getMillisecondCounterHiRes() < scrollDeadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                expect (viewport->getViewPositionY() > 0);
                expectEquals (viewport->getViewArea().getBottom(),
                              viewport->getViewedComponent()->getHeight(),
                              "Scrolling reaches the final output channel.");
                map->selectRow (0);
                expectEquals (map->getSelectedRow(), 0,
                              "Output routing supports keyboard row selection.");
            }

            panel.sampleRateControl().setSelectedItemIndex (1, juce::sendNotificationSync);
            expectWithinAbsoluteError (controller.getAudioDeviceSettings().currentSampleRate,
                                       96000.0, 0.01);
            fake->nextError = "CoreAudio rejected the rate.";
            const auto previousRate = panel.sampleRateControl().getSelectedId();
            panel.sampleRateControl().setSelectedItemIndex (0, juce::sendNotificationSync);
            expectEquals (panel.sampleRateControl().getSelectedId(), previousRate);
            expect (! notices.empty() && notices.back().second,
                    "A rejected device change carries an explicit error flag.");
            expect (notices.back().first.contains ("CoreAudio rejected the rate."));
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
            PluginSettingsPanel panel (controller, &foundationSkin.adapter);
            panel.refreshInventory();
            panel.refreshScanState();
            expectEquals (panel.formatsControl().getText(), juce::String ("Plug-ins"));
            expect (panel.vst3Enabled.getToggleState() && panel.auEnabled.getToggleState());
            expect (panel.scanAllControl().isEnabled());
            expect (panel.scanFolderControl().isEnabled());
            expect (! panel.cancelControl().isEnabled());
            expectEquals (panel.knownPluginCount(), controller.getKnownPlugins().size());
        }

        beginTest ("a hidden scan scope cannot restrict scanning in a legacy skin");
        {
            PlayerController controller (std::make_unique<PanelAudioBackend>());
            PluginSettingsPanel panel (controller, &foundationSkin.adapter);
            panel.scanScopeControl().setVisible (true);
            panel.scanScopeControl().setSelectedId (3, juce::sendNotificationSync);
            expect (controller.setPluginFormatPreferences ({ true, false }));
            panel.refreshScanState();
            expect (! panel.scanAllControl().isEnabled());
            panel.scanScopeControl().setVisible (false);
            panel.refreshScanState();
            expect (panel.scanAllControl().isEnabled(), "Legacy layouts can still scan enabled VST3.");
            panel.scanScopeControl().setVisible (true);
            panel.refreshScanState();
            expectEquals (panel.scanScopeControl().getSelectedId(), 3);
            expect (! panel.scanAllControl().isEnabled());
        }

        beginTest ("completed scans refresh the catalog once even before the first UI poll");
        {
            const auto root = freshPanelDirectory ("completed-plugin-scan");
            juce::KnownPluginList known;
            for (int index = 0; index < 2; ++index)
            {
                juce::PluginDescription plugin;
                plugin.name = index == 0 ? "Zulu" : "Alpha";
                plugin.pluginFormatName = "VST3";
                plugin.fileOrIdentifier = "completed-scan-fixture-" + juce::String (index);
                plugin.uniqueId = 900 + index;
                known.addType (plugin);
            }
            PlayerController controller (std::make_unique<PanelAudioBackend>());
            StateRestoreResult initial;
            initial.pluginCache = known.createXml();
            expect (controller.applyPreparedState (std::move (initial)));
            PluginSettingsPanel panel (controller, &foundationSkin.adapter);
            auto* list = dynamic_cast<juce::ListBox*> (&panel.knownListControl());
            expect (list != nullptr);
            if (list != nullptr)
                expect (list->getListBoxModel()->getNameForRow (0).startsWith ("Zulu"));

            int catalogChanges = 0;
            panel.onCatalogChanged = [&] { ++catalogChanges; };
            expect (controller.startPluginScan (root));
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
            while (controller.getPluginScanStatus().scanning
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (1);
            expect (! controller.getPluginScanStatus().scanning);
            expectEquals (controller.getKnownPlugins()[0].name, juce::String ("Alpha"));

            panel.refreshScanState();
            expectEquals (catalogChanges, 1);
            if (list != nullptr)
                expect (list->getListBoxModel()->getNameForRow (0).startsWith ("Alpha"));
            panel.refreshScanState();
            panel.refreshScanState();
            expectEquals (catalogChanges, 1);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("cancelling a scan preserves seeded inventory, filtered rows and cache after restart");
        for (const auto filtered : { false, true })
        {
            const auto root = freshPanelDirectory ("cancel-populated-scan");
            PluginScannerConfiguration scanner;
            scanner.executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            scanner.resultDirectory = root;
            scanner.argumentsBeforeProtocol = { "--plugin-scan-fixture", "hang" };
            scanner.perCandidateTimeoutMilliseconds = 5000;
            scanner.candidateProvider = [] (const juce::String& format, const juce::FileSearchPath&)
            {
                return format == "VST3" ? juce::StringArray { "/tmp/Pending.vst3" } : juce::StringArray {};
            };
            PlayerController controller (std::make_unique<PanelAudioBackend>(), scanner);
            juce::KnownPluginList known;
            for (int i = 0; i < 2; ++i)
            {
                juce::PluginDescription plugin;
                plugin.name = i == 0 ? "Saved AU" : "Saved VST3";
                plugin.pluginFormatName = i == 0 ? "AudioUnit" : "VST3";
                plugin.fileOrIdentifier = "saved-plugin-" + juce::String (i);
                plugin.uniqueId = i + 50;
                known.addType (plugin);
            }
            StateRestoreResult initial;
            initial.pluginCache = known.createXml();
            expect (controller.applyPreparedState (std::move (initial)));
            PluginSettingsPanel panel (controller, &foundationSkin.adapter);
            panel.knownListControl().setBounds (0, 0, 560, 180);
            panel.knownListControl().setLookAndFeel (&foundationSkin.adapter);
            if (filtered)
            {
                panel.filters.search.setText ("Saved", false);
                panel.filters.format.setText ("AU", juce::sendNotificationSync);
            }
            const auto expected = filtered ? 1 : 2;
            expectEquals (panel.knownPluginCount(), expected);
            const auto beforeScan = panel.knownListControl().createComponentSnapshot (
                panel.knownListControl().getLocalBounds());
            const auto waitUntil = [] (const std::function<bool()>& done)
            {
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
                while (! done() && juce::Time::getMillisecondCounterHiRes() < deadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                return done();
            };
            expect (controller.startPluginScan());
            expect (waitUntil ([&] { return controller.getPluginScanStatus().currentItem.isNotEmpty(); }));
            panel.refreshScanState();
            expect (panel.cancelControl().isEnabled());
            panel.cancelControl().triggerClick();
            expect (waitUntil ([&] { return ! controller.getPluginScanStatus().scanning; }));
            panel.refreshScanState();
            expectEquals (controller.getKnownPlugins().size(), 2);
            expectEquals (panel.knownPluginCount(), expected);
            auto* list = dynamic_cast<juce::ListBox*> (&panel.knownListControl());
            expect (list != nullptr);
            if (list)
            {
                expectEquals (list->getListBoxModel()->getNumRows(), expected);
                expect (list->getRowPosition (0, true).intersects (list->getViewport()->getBounds()));
                const auto image = list->createComponentSnapshot (list->getLocalBounds());
                expect (image.isValid());
                int changedPixels = 0;
                for (int y = 0; y < image.getHeight(); ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (image.getPixelAt (x, y) != beforeScan.getPixelAt (x, y)) ++changedPixels;
                expectEquals (changedPixels, 0, "Cancelling retains the rendered inventory, not only its row count.");
                expect (list->getListBoxModel()->getNameForRow (0).contains ("Saved"));
            }
            panel.knownListControl().setLookAndFeel (nullptr);
            const auto state = root.getChildFile ("state.xml"), cache = root.getChildFile ("plugins.xml");
            expect (controller.saveState (state, cache));
            PlayerController reopened (std::make_unique<PanelAudioBackend>());
            expect (reopened.loadState (state, cache));
            expectEquals (reopened.getKnownPlugins().size(), 2);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("plug-in filters compose and format preferences survive a controller restart");
        {
            const auto root = freshPanelDirectory ("plugin-filters");
            const auto cache = root.getChildFile ("plugins.xml");
            const auto stateFile = root.getChildFile ("state.xml");
            PluginManager writer;
            for (int index = 0; index < 4; ++index)
            {
                juce::PluginDescription plugin;
                plugin.name = index < 2 ? "Clean EQ" : index == 2 ? "Glass Delay" : "Keys Synth";
                plugin.manufacturerName = index < 2 ? "Acme Audio" : "Other";
                plugin.pluginFormatName = index == 1 ? "AudioUnit" : "VST3";
                plugin.category = index < 2 ? "Fx|EQ" : index == 2 ? "Fx|Delay" : "Instrument|Synth";
                plugin.isInstrument = index == 3;
                plugin.fileOrIdentifier = "filter-fixture-" + juce::String (index);
                plugin.uniqueId = 700 + index;
                writer.getKnownPluginList().addType (plugin);
            }
            juce::String error;
            expect (writer.writeKnownPluginCacheAtomically (cache, error), error);
            expect (AppStateStore::writeAtomically ({}, stateFile, error), error);
            PlayerController controller (std::make_unique<PanelAudioBackend>());
            expect (controller.loadState (stateFile, cache));
            PluginSettingsPanel panel (controller, &foundationSkin.adapter);
            const auto select = [this] (juce::ComboBox& box, const juce::String& label)
            {
                for (int i = 0; i < box.getNumItems(); ++i)
                    if (box.getItemText (i) == label)
                    {
                        box.setSelectedItemIndex (i, juce::sendNotificationSync);
                        return;
                    }
                expect (false, "Missing filter choice: " + label);
            };
            expectEquals (panel.knownPluginCount(), 4);
            select (panel.filters.type, "EQ");
            expectEquals (panel.knownPluginCount(), 2);
            select (panel.filters.manufacturer, "Other");
            expectEquals (panel.knownPluginCount(), 0);
            select (panel.filters.manufacturer, "Acme Audio");
            select (panel.filters.format, "AU");
            expectEquals (panel.knownPluginCount(), 1);
            panel.refreshInventory();
            expectEquals (panel.knownPluginCount(), 1);
            expectEquals (panel.filters.type.getText(), juce::String ("EQ"));
            panel.auEnabled.setToggleState (false, juce::sendNotificationSync);
            expectEquals (controller.getKnownPlugins().size(), 4);
            expectEquals (controller.getInsertEffectPlugins().size(), 2);
            auto* list = dynamic_cast<juce::ListBox*> (&panel.knownListControl());
            expect (list && list->getListBoxModel()->getNameForRow (0).contains ("format disabled"));
            panel.vst3Enabled.setToggleState (false, juce::sendNotificationSync);
            expect (! panel.scanAllControl().isEnabled() && panel.scanFolderControl().isEnabled());
            expect (controller.getInsertEffectPlugins().isEmpty());
            expect (controller.saveState (stateFile, cache));
            PlayerController reopened (std::make_unique<PanelAudioBackend>());
            expect (reopened.loadState (stateFile, cache));
            expect (reopened.getPluginFormatPreferences() == PluginFormatPreferences { false, false });
            expectEquals (reopened.getKnownPlugins().size(), 4);
            expect (reopened.getInsertEffectPlugins().isEmpty());
            expect (reopened.setPluginFormatPreferences ({ false, true }));
            expectEquals (reopened.getInsertEffectPlugins().size(), 1);
            panel.filters.search.setVisible (true);
            panel.filters.search.setText ("Clean", false);
            for (auto* box : { &panel.filters.type, &panel.filters.manufacturer, &panel.filters.format })
                box->setVisible (false);
            panel.filters.clearHiddenFilters();
            expectEquals (panel.filters.search.getText(), juce::String ("Clean"));
            expectEquals (panel.knownPluginCount(), 2);
            expectEquals (panel.filters.type.getSelectedId(), 1);
            expectEquals (panel.filters.manufacturer.getSelectedId(), 1);
            expectEquals (panel.filters.format.getSelectedId(), 1);
            panel.filters.search.setVisible (false);
            panel.filters.clearHiddenFilters();
            expectEquals (panel.knownPluginCount(), 4);
            static_cast<void> (root.deleteRecursively());
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
            PluginSettingsPanel panel (controller, &foundationSkin.adapter);
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
            {
                expectEquals (list->getViewport()->getViewPositionY(), 44);
                auto* viewport = list->getViewport();
                for (const auto height : { 132, 232, 280 })
                {
                    list->setSize (360, height);
                    const auto visibleHeight = viewport->getMaximumVisibleHeight();
                    expectEquals (visibleHeight % list->getRowHeight(), 0,
                                  "Only complete plugin rows may occupy the viewport");
                    expect (visibleHeight > 0);
                    list->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
                    expectEquals (viewport->getViewPositionY() + visibleHeight,
                                  panel.knownPluginCount() * list->getRowHeight(),
                                  "The final plugin must remain fully reachable");
                    viewport->setViewPosition (0, 53);
                    expectEquals (viewport->getViewPositionY() % list->getRowHeight(), 0,
                                  "Dragging the scroll bar must retain complete rows");
                }
                list->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
                list->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
            }
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
            SkinLibraryPanel panel (manager, &foundationSkin.adapter);
            std::vector<std::pair<juce::String, bool>> notices;
            panel.setStatusCallback ([&] (const juce::String& message, bool isError)
            {
                notices.emplace_back (message, isError);
            });
            panel.refresh();
            expect (notices.empty(), "Refreshing installed skins does not announce an operation.");
            panel.importFile (root.getChildFile ("missing.moonwake-skin"));
            expect (! notices.empty() && notices.back().second,
                    "Skin import failures carry an explicit error flag.");
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
            expect (! notices.empty() && ! notices.back().second,
                    "Successful skin preview is distinguishable from a validation error.");
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
            AudioSettingsPanel audio (controller, &foundationSkin.adapter);
            PluginSettingsPanel plugins (controller, &foundationSkin.adapter);
            SkinLibraryPanel skins (manager, &foundationSkin.adapter);
            juce::Component root;
            juce::Label title;
            juce::TextButton close { "Close" };
            juce::Label brand, status;
            juce::TextButton studio, listening, effects, settings;
            for (auto* component : std::array<juce::Component*, 6> {
                     &brand, &status, &studio, &listening, &effects, &settings })
                root.addAndMakeVisible (component);
            root.addAndMakeVisible (title);
            root.addAndMakeVisible (close);
            audio.addTo (root);
            plugins.addTo (root);
            skins.addTo (root);

            SkinComponentBindings bindings;
            expect (bindings.bind ("settings.title", title, report));
            expect (bindings.bind ("settings.close", close, report));
            expect (bindings.bind ("app.brand.title", brand, report));
            expect (bindings.bind ("app.status", status, report));
            expect (bindings.bind ("workspace.studio", studio, report));
            expect (bindings.bind ("workspace.listening", listening, report));
            expect (bindings.bind ("effects.open", effects, report));
            expect (bindings.bind ("settings.open", settings, report));
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
