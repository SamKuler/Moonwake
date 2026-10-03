#include "skin/SkinLayoutEngine.h"
#include "skin/SkinManager.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/SkinLayoutHost.h"
#include "ui/SkinTableListBox.h"
#include "ui/OutputMixerComponent.h"
#include "ui/SkinEffectChainViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <map>
#include <limits>
#include <memory>

namespace
{
juce::File freshHostDirectory()
{
    const auto root = juce::File::getCurrentWorkingDirectory()
                          .getChildFile ("build")
                          .getChildFile ("skin-layout-host-tests")
                          .getChildFile (juce::Uuid().toString());
    static_cast<void> (root.deleteRecursively());
    root.createDirectory();
    return root;
}

juce::File writeScrollableHostSkin (const juce::File& root)
{
    const auto source = root.getChildFile ("scrollable-source");
    source.createDirectory();
    source.getChildFile ("layouts").createDirectory();
    source.getChildFile ("manifest.json").replaceWithText (R"json({
      "skinApi": 1,
      "id": "com.example.scroll-host",
      "name": "Scrollable Host Test",
      "version": "1.0.0",
      "author": { "name": "Tests" },
      "minAppVersion": "0.1.0",
      "extends": "builtin.foundation",
      "capabilities": ["renderers", "layouts", "animations"],
      "entrypoints": {
        "components": "components/index.json",
        "layouts": { "listening": "layouts/listening.json" }
      }
    })json");
    source.getChildFile ("components").createDirectory();
    source.getChildFile ("components/index.json").replaceWithText (R"json({
      "surface": { "animated": "components/surface-animated.json" }
    })json");
    source.getChildFile ("components/surface-animated.json").replaceWithText (R"json({
      "apiVersion": 1,
      "target": "surface",
      "layers": [
        { "id": "body", "type": "roundedRect", "frame": { "anchor": "bounds" },
          "radius": 8, "fill": "#2040D0FF" }
      ],
      "transitions": {
        "workspaceEnter": {
          "durationMs": 1000, "easing": "linear",
          "properties": ["body.opacity"],
          "from": { "body.opacity": 0.1 }
        }
      }
    })json");
    source.getChildFile ("layouts/listening.json").replaceWithText (R"json({
      "apiVersion": 1,
      "root": {
        "id": "host-stack", "type": "stack", "clip": true, "opacity": 0.8,
        "children": [
          { "id": "host-background", "type": "visual", "style": "surface.animated",
            "width": "100%", "height": "100%", "zIndex": 0 },
          {
            "id": "host-content", "type": "column", "width": "100%", "height": "100%",
            "padding": 12, "gap": 8, "opacity": 0.5, "zIndex": 1,
            "children": [
              {
                "id": "host-header", "type": "row", "height": 40, "gap": 8,
                "children": [
                  { "id": "host-studio", "type": "component", "component": "workspace.studio", "width": 108, "height": 34, "focusOrder": 10 },
                  { "id": "host-listening", "type": "component", "component": "workspace.listening", "width": 108, "height": 34, "focusOrder": 20 },
                  { "id": "host-settings", "type": "component", "component": "settings.open", "width": 92, "height": 34, "focusOrder": 30 },
                  { "id": "host-add", "type": "component", "component": "library.add", "width": 92, "height": 34, "focusOrder": 40 }
                ]
              },
              {
                "id": "queue-scroll", "type": "scroll", "direction": "vertical", "height": 80, "padding": 6,
                "child": { "id": "host-queue", "type": "component", "component": "queue.table", "height": 200, "focusOrder": 50 }
              },
              {
                "id": "host-transport", "type": "row", "height": 64, "gap": 8,
                "children": [
                  { "id": "host-previous", "type": "component", "component": "transport.previous", "width": 64, "height": 44, "focusOrder": 60 },
                  { "id": "host-play", "type": "component", "component": "transport.playPause", "width": 72, "height": 48, "focusOrder": 70 },
                  { "id": "host-next", "type": "component", "component": "transport.next", "width": 64, "height": 44, "focusOrder": 80 },
                  { "id": "host-volume", "type": "component", "component": "transport.volume", "width": 56, "height": 56, "focusOrder": 100 }
                ]
              },
              { "id": "host-seek", "type": "component", "component": "transport.seek", "presentation": "linearHorizontal", "height": 30, "focusOrder": 90 },
              { "id": "host-time", "type": "component", "component": "transport.time", "height": 22 },
              { "id": "host-status", "type": "component", "component": "app.status", "height": 28 }
            ]
          }
        ]
      }
    })json");
    return source;
}

class PaintedTable final : public juce::TableListBox
{
public:
    void paintOverChildren (juce::Graphics& graphics) override
    {
        graphics.fillAll (juce::Colours::red);
    }
};

std::unique_ptr<juce::Component> componentFor (juce::StringRef id)
{
    const auto* descriptor = PlayerComponentCatalog::find (juce::String (id));
    if (descriptor == nullptr) return {};
    switch (descriptor->kind)
    {
        case SkinComponentKind::label:
            return std::make_unique<juce::Label>();
        case SkinComponentKind::textButton:
            return std::make_unique<juce::TextButton>();
        case SkinComponentKind::toggleButton:
            return std::make_unique<juce::ToggleButton>();
        case SkinComponentKind::comboBox:
            return std::make_unique<juce::ComboBox>();
        case SkinComponentKind::textEditor:
            return std::make_unique<juce::TextEditor>();
        case SkinComponentKind::linearSlider:
        case SkinComponentKind::slider:
        {
            auto slider = std::make_unique<juce::Slider>();
            slider->setWantsKeyboardFocus (true);
            return slider;
        }
        case SkinComponentKind::table:
            return std::make_unique<PaintedTable>();
        case SkinComponentKind::progressBar:
        {
            static double progress = 0.0;
            return std::make_unique<juce::ProgressBar> (progress);
        }
        case SkinComponentKind::artwork:
        case SkinComponentKind::effectChain:
        case SkinComponentKind::outputStage:
        case SkinComponentKind::channelMixer:
        case SkinComponentKind::channelMap:
        case SkinComponentKind::pluginList:
        case SkinComponentKind::skinList:
            return std::make_unique<juce::Component>();
    }
    return {};
}

juce::Rectangle<int> rootBoundsFor (juce::Component& root,
                                    juce::Component& child)
{
    return root.getLocalArea (&child, child.getLocalBounds());
}

void suffixLayoutIds (juce::var node)
{
    if (! node.isObject()) return;
    node.getDynamicObject()->setProperty ("id", node.getProperty ("id", {}).toString() + "-studio");
    if (const auto* children = node.getProperty ("children", {}).getArray())
        for (const auto& child : *children) suffixLayoutIds (child);
    suffixLayoutIds (node.getProperty ("child", {}));
}

void setQueuePresentation (juce::var node, const juce::String& presentation)
{
    if (! node.isObject()) return;
    if (node.getProperty ("component", {}).toString() == "queue.table")
    {
        node.getDynamicObject()->setProperty ("presentation", presentation);
        node.getDynamicObject()->setProperty ("style", presentation == "list" ? "table.list" : "table.default");
    }
    if (const auto* children = node.getProperty ("children", {}).getArray())
        for (const auto& child : *children) setQueuePresentation (child, presentation);
    setQueuePresentation (node.getProperty ("child", {}), presentation);
}

juce::Viewport* ancestorViewport (juce::Component& component)
{
    for (auto* current = component.getParentComponent(); current != nullptr;
         current = current->getParentComponent())
        if (auto* viewport = dynamic_cast<juce::Viewport*> (current))
            return viewport;
    return nullptr;
}

class SkinLayoutHostTests final : public juce::UnitTest
{
public:
    SkinLayoutHostTests() : UnitTest ("SkinLayoutHost", "skin") {}

    void runTest() override
    {
        beginTest ("keyboard traversal follows declared order across nested layouts and composite controls");
        {
            const auto storage = freshHostDirectory();
            SkinValidationReport report;
            SkinManager manager;
            expect (manager.initialise (storage.getChildFile ("installs"), report), report.toText());
            juce::Component root;
            root.setBounds (0, 0, 1240, 800);
            root.setVisible (true);
            root.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
            SkinComponentBindings bindings;
            std::map<juce::String, std::unique_ptr<juce::Component>> components;
            juce::Component effectContent;
            juce::TextButton effectEdit ("Edit"), effectRemove ("Remove");
            effectContent.addAndMakeVisible (effectEdit);
            effectContent.addAndMakeVisible (effectRemove);
            effectEdit.setBounds (0, 0, 60, 30);
            effectRemove.setBounds (70, 0, 60, 30);
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
            {
                auto component = componentFor (descriptor.id);
                if (descriptor.id == "output.mixer")
                {
                    auto mixer = std::make_unique<OutputMixerComponent>();
                    mixer->setState (OutputLayout::stereo, {}, {});
                    component = std::move (mixer);
                }
                if (descriptor.id == "effects.chain")
                {
                    auto chain = std::make_unique<SkinEffectChainViewport>();
                    chain->setViewedComponent (&effectContent, false);
                    component = std::move (chain);
                }
                root.addAndMakeVisible (*component);
                expect (bindings.bind (descriptor.id, *component, report), report.toText());
                components.emplace (descriptor.id, std::move (component));
            }
            SkinLayoutHost host (manager, bindings);
            const auto ownerFor = [&] (juce::Component* component)
            {
                for (auto* current = component; current != nullptr; current = current->getParentComponent())
                    if (current->getProperties().contains (SkinComponentBindings::componentIdProperty))
                        return current;
                return component;
            };
            const auto checkTraversal = [&]
            {
                auto traverser = host.createKeyboardFocusTraverser();
                const auto actual = traverser->getAllComponents (&root);
                expect (! actual.empty());
                if (actual.empty()) return;
                expect (traverser->getDefaultComponent (&root) == actual.front());
                expect (actual.front()->createKeyboardFocusTraverser()->getAllComponents (&root) == actual,
                        "component-driven Tab traversal must use the skin scene's traverser");
                int previousOrder = 0;
                for (size_t index = 0; index < actual.size(); ++index)
                {
                    auto* component = actual[index];
                    const auto declaredOrder = ownerFor (component)->getExplicitFocusOrder();
                    const auto order = declaredOrder > 0 ? declaredOrder : std::numeric_limits<int>::max();
                    expect (order >= previousOrder, component->getName());
                    previousOrder = order;
                    expect (traverser->getNextComponent (component)
                                == (index + 1 < actual.size() ? actual[index + 1] : nullptr));
                    expect (traverser->getPreviousComponent (component)
                                == (index > 0 ? actual[index - 1] : nullptr));
                }
                auto native = juce::KeyboardFocusTraverser().getAllComponents (&root);
                expectEquals (static_cast<int> (actual.size()), static_cast<int> (native.size()));
                for (auto* component : native)
                    expect (std::find (actual.begin(), actual.end(), component) != actual.end());
            };
            for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
                for (const auto entry : { SkinLayoutEntry::studio, SkinLayoutEntry::listening,
                                          SkinLayoutEntry::effects, SkinLayoutEntry::settings })
                {
                    expect (host.apply (entry, root.getLocalBounds(), 1.0f, report, workspace), report.toText());
                    checkTraversal();
                    if (entry == SkinLayoutEntry::studio)
                    {
                        const auto mixerChildren = juce::KeyboardFocusTraverser().getAllComponents (components.at ("output.mixer").get());
                        expect (mixerChildren.size() >= 6, "both channels must expose gain, mute and solo");
                    }
                    if (entry == SkinLayoutEntry::effects)
                    {
                        const auto actual = host.createKeyboardFocusTraverser()->getAllComponents (&root);
                        expect (std::find (actual.begin(), actual.end(), &effectEdit) != actual.end());
                        expect (std::find (actual.begin(), actual.end(), &effectRemove) != actual.end());
                        expect (std::find (actual.begin(), actual.end(), &effectEdit)
                                    < std::find (actual.begin(), actual.end(), &effectRemove));
                        components.at ("effects.chain")->setEnabled (false);
                        const auto disabled = host.createKeyboardFocusTraverser()->getAllComponents (&root);
                        expect (std::find (disabled.begin(), disabled.end(), &effectEdit) == disabled.end());
                        expect (std::find (disabled.begin(), disabled.end(), &effectRemove) == disabled.end());
                        components.at ("effects.chain")->setEnabled (true);
                    }
                }
            components.at ("workspace.studio")->setEnabled (false);
            components.at ("workspace.listening")->setVisible (false);
            checkTraversal();
            const auto filtered = host.createKeyboardFocusTraverser()->getAllComponents (&root);
            expect (std::find (filtered.begin(), filtered.end(), components.at ("workspace.studio").get()) == filtered.end());
            expect (std::find (filtered.begin(), filtered.end(), components.at ("workspace.listening").get()) == filtered.end());
            components.at ("workspace.studio")->setEnabled (true);
            components.at ("workspace.listening")->setVisible (true);

            beginTest ("relayout preserves live focus without restoring controls removed by the next page");
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f, report), report.toText());
            root.addToDesktop (0);
            root.toFront (true);
            auto& search = dynamic_cast<juce::TextEditor&> (*components.at ("library.search"));
            search.setText ("Moonwake", false);
            search.grabKeyboardFocus();
            expect (search.hasKeyboardFocus (false), "native focus requires a visible test peer");
            expect (host.apply (SkinLayoutEntry::listening, { 0, 0, 1040, 700 }, 1.0f, report), report.toText());
            expect (search.hasKeyboardFocus (false), "same-page resize must retain the focused editor");
            expect (manager.preview ({ "builtin.foundation", "1.0.0" }, report), report.toText());
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f, report), report.toText());
            expect (search.hasKeyboardFocus (false), "skin preview must retain a still-visible editor");
            manager.cancelPreview();
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f, report), report.toText());
            expect (search.hasKeyboardFocus (false));
            expect (host.apply (SkinLayoutEntry::settings, root.getLocalBounds(), 1.0f, report), report.toText());
            expect (! search.hasKeyboardFocus (true), "a hidden editor must not reclaim focus after changing page");
            root.removeFromDesktop();

            beginTest ("declared order interleaves clipped groups and a scroll viewport");
            const auto source = writeScrollableHostSkin (storage);
            const auto file = source.getChildFile ("layouts/listening.json");
            auto document = juce::JSON::parse (file);
            const std::map<juce::String, int> orders {
                { "transport.volume", 10 }, { "queue.table", 20 }, { "transport.previous", 30 },
                { "transport.playPause", 40 }, { "workspace.listening", 50 }, { "settings.open", 60 },
                { "library.add", 70 }, { "transport.next", 80 }, { "workspace.studio", 90 }, { "transport.seek", 100 }
            };
            const auto reorder = [&] (const auto& self, juce::var node) -> void
            {
                if (! node.isObject()) return;
                const auto id = node.getProperty ("component", {}).toString();
                if (const auto found = orders.find (id); found != orders.end())
                    node.getDynamicObject()->setProperty ("focusOrder", found->second);
                if (node.getProperty ("id", {}).toString() == "host-header")
                    node.getDynamicObject()->setProperty ("clip", true);
                if (const auto* children = node.getProperty ("children", {}).getArray())
                    for (const auto& child : *children) self (self, child);
                self (self, node.getProperty ("child", {}));
            };
            reorder (reorder, document.getProperty ("root", {}));
            expect (file.replaceWithText (juce::JSON::toString (document)));
            const auto imported = manager.importSkin (source);
            expect (! imported.hasErrors(), imported.toText());
            expect (manager.activate ({ "com.example.scroll-host", "1.0.0" }, false, report), report.toText());
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f, report), report.toText());
            checkTraversal();
            expect (host.createKeyboardFocusTraverser()->getDefaultComponent (&root)
                        == components.at ("transport.volume").get());
            static_cast<void> (storage.deleteRecursively());
        }

        beginTest ("workspace context switches the resolved tree without changing the layout entry");
        {
            const auto storage = freshHostDirectory();
            const auto source = writeScrollableHostSkin (storage);
            const auto file = source.getChildFile ("layouts/listening.json");
            auto document = juce::JSON::parse (file);
            auto baseStyles = juce::var (new juce::DynamicObject());
            baseStyles.getDynamicObject()->setProperty ("popupMenu", "popupMenu.default");
            document.getDynamicObject()->setProperty ("ancillaryStyles", baseStyles);
            auto themedRoot = document.getProperty ("root", {}).clone();
            suffixLayoutIds (themedRoot);
            themedRoot.getDynamicObject()->setProperty ("padding", 20);
            auto condition = juce::var (new juce::DynamicObject());
            condition.getDynamicObject()->setProperty ("workspace", "studio");
            auto breakpoint = juce::var (new juce::DynamicObject());
            breakpoint.getDynamicObject()->setProperty ("when", condition);
            breakpoint.getDynamicObject()->setProperty ("root", themedRoot);
            auto themedStyles = juce::var (new juce::DynamicObject());
            themedStyles.getDynamicObject()->setProperty ("tooltip", "tooltip.default");
            breakpoint.getDynamicObject()->setProperty ("ancillaryStyles", themedStyles);
            document.getDynamicObject()->setProperty ("breakpoints", juce::Array<juce::var> { breakpoint });
            expect (file.replaceWithText (juce::JSON::toString (document, true)));
            SkinValidationReport report;
            SkinManager manager;
            expect (manager.initialise (storage.getChildFile ("installs"), report), report.toText());
            const auto imported = manager.importSkin (source);
            expect (! imported.hasErrors(), imported.toText());
            expect (manager.preview ({ "com.example.scroll-host", "1.0.0" }, report), report.toText());
            juce::Component root;
            root.setBounds (0, 0, 1240, 800);
            SkinComponentBindings bindings;
            std::vector<std::unique_ptr<juce::Component>> components;
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
                if (descriptor.requiredIn.contains (SkinLayoutEntry::listening))
                {
                    auto component = componentFor (descriptor.id);
                    root.addAndMakeVisible (*component);
                    expect (bindings.bind (descriptor.id, *component, report), report.toText());
                    components.push_back (std::move (component));
                }
            SkinLayoutHost host (manager, bindings);
            auto* queue = bindings.find ("queue.table");
            expect (queue != nullptr);
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f, report), report.toText());
            const auto original = rootBoundsFor (root, *queue);
            expectEquals (host.ancillaryStyleFor (SkinRenderTarget::popupMenu), juce::String ("popupMenu.default"));
            expect (host.ancillaryStyleFor (SkinRenderTarget::tooltip).isEmpty());
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f,
                                report, WorkspaceMode::studio), report.toText());
            expect (rootBoundsFor (root, *queue) != original);
            expectEquals (host.ancillaryStyleFor (SkinRenderTarget::popupMenu), juce::String ("popupMenu.default"));
            expectEquals (host.ancillaryStyleFor (SkinRenderTarget::tooltip), juce::String ("tooltip.default"));
            expect (host.apply (SkinLayoutEntry::listening, root.getLocalBounds(), 1.0f, report), report.toText());
            expect (rootBoundsFor (root, *queue) == original);
            expect (host.ancillaryStyleFor (SkinRenderTarget::tooltip).isEmpty());
            host.clear();
            expect (host.ancillaryStyleFor (SkinRenderTarget::popupMenu).isEmpty());
            storage.deleteRecursively();
        }

        beginTest ("host table and list transitions preserve user columns from initially empty bounds");
        {
            const auto storage = freshHostDirectory();
            const auto source = writeScrollableHostSkin (storage);
            const auto file = source.getChildFile ("layouts/listening.json");
            auto document = juce::JSON::parse (file);
            auto tableRoot = document.getProperty ("root", {});
            setQueuePresentation (tableRoot, "table");
            auto listRoot = tableRoot.clone();
            suffixLayoutIds (listRoot);
            setQueuePresentation (listRoot, "list");
            auto condition = juce::var (new juce::DynamicObject());
            condition.getDynamicObject()->setProperty ("workspace", "studio");
            auto breakpoint = juce::var (new juce::DynamicObject());
            breakpoint.getDynamicObject()->setProperty ("when", condition);
            breakpoint.getDynamicObject()->setProperty ("root", listRoot);
            document.getDynamicObject()->setProperty ("breakpoints", juce::Array<juce::var> { breakpoint });
            expect (file.replaceWithText (juce::JSON::toString (document, true)));
            SkinValidationReport report;
            SkinManager manager;
            expect (manager.initialise (storage.getChildFile ("installs"), report), report.toText());
            const auto imported = manager.importSkin (source);
            expect (! imported.hasErrors(), imported.toText());
            expect (manager.preview ({ "com.example.scroll-host", "1.0.0" }, report), report.toText());
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
            {
                const auto session = manager.activeSession();
                return JuceSkinAdapter::RuntimeBinding {
                    session->compiledSkin()->resolveStyle (target, style),
                    &session->animations(), session.get(), false };
            }, renderer);
            juce::Component root;
            root.setLookAndFeel (&adapter);
            root.setBounds (0, 0, 1240, 800);
            SkinComponentBindings bindings;
            std::vector<std::unique_ptr<juce::Component>> components;
            SkinTableListBox queue;
            queue.setOutlineThickness (0);
            queue.setRowHeight (32);
            queue.setHeaderHeight (24);
            auto& header = queue.getHeader();
            header.addColumn ("Title", 1, 240, 80, -1);
            header.addColumn ("Artist / album", 2, 360, 100, -1);
            header.addColumn ("Duration", 3, 90, 40, 120);
            root.addAndMakeVisible (queue);
            expect (bindings.bind ("queue.table", queue, report), report.toText());
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
                if (descriptor.id != "queue.table" && descriptor.requiredIn.contains (SkinLayoutEntry::listening))
                {
                    auto component = componentFor (descriptor.id);
                    root.addAndMakeVisible (*component);
                    expect (bindings.bind (descriptor.id, *component, report), report.toText());
                    components.push_back (std::move (component));
                }
            expect (queue.getBounds().isEmpty());
            SkinLayoutHost host (manager, bindings);
            const auto apply = [&] (WorkspaceMode workspace, int width)
            {
                expect (host.apply (SkinLayoutEntry::listening, { 0, 0, width, 800 },
                                    1.0f, report, workspace), report.toText());
            };
            apply (WorkspaceMode::listening, 1240);
            header.setColumnWidth (1, 220);
            header.setColumnWidth (3, 88);
            header.setColumnWidth (2, queue.getVisibleContentWidth() - 220 - 88);
            header.moveColumn (3, 0);
            header.setSortColumnId (3, false);
            const auto customised = header.toString();
            const auto titleRatio = 220.0 / (queue.getVisibleContentWidth() - 88);
            apply (WorkspaceMode::listening, 1240);
            expectEquals (header.toString(), customised);
            apply (WorkspaceMode::studio, 1240);
            expectEquals (queue.getHeaderHeight(), 0);
            expectEquals (header.getColumnWidth (3), 56);
            apply (WorkspaceMode::listening, 1240);
            expectEquals (header.toString(), customised);
            for (const auto width : { 1040, 1240 })
            {
                apply (WorkspaceMode::listening, width);
                expectEquals (header.getColumnWidth (3), 88);
                expectEquals (header.getColumnIdOfIndex (0, true), 3);
                expectEquals (header.getSortColumnId(), 3);
                expectWithinAbsoluteError (static_cast<double> (header.getColumnWidth (1)),
                    titleRatio * (queue.getVisibleContentWidth() - 88), 1.0);
                const auto resizedColumns = header.toString();
                apply (WorkspaceMode::studio, width);
                apply (WorkspaceMode::listening, width);
                expectEquals (header.toString(), resizedColumns);
            }
            host.clear();
            expect (queue.getBounds().isEmpty());
            expectEquals (queue.getRowHeight(), 32);
            expectEquals (queue.getHeaderHeight(), 24);
            root.setLookAndFeel (nullptr);
            storage.deleteRecursively();
        }

        beginTest ("active Listening layout positions and styles real bound components");
        {
            const auto storage = freshHostDirectory();
            SkinValidationReport report;
            SkinManager manager;
            expect (manager.initialise (storage.getChildFile ("installs"), report),
                    report.toText());

            juce::Component root;
            root.setBounds (0, 0, 1240, 800);
            root.setVisible (true);
            SkinComponentBindings bindings;
            std::map<juce::String, std::unique_ptr<juce::Component>> components;
            const auto session = manager.activeSession();
            expect (session != nullptr);
            const auto skin = session != nullptr ? session->compiledSkin() : nullptr;
            expect (skin != nullptr);
            if (skin == nullptr) return;
            const juce::Rectangle<int> appliedBounds (37, 23, 1160, 740);
            const auto expected = SkinLayoutEngine::resolve (
                skin->layouts.at (SkinLayoutEntry::listening),
                appliedBounds.toFloat(), 1.25f, report);
            expect (expected.has_value(), report.toText());
            if (! expected) return;
            juce::StringArray ids;
            const auto collectComponents = [&] (const auto& self, const SkinResolvedNode& node) -> void
            {
                if (node.componentId.isNotEmpty()) ids.addIfNotAlreadyThere (node.componentId);
                for (const auto& child : node.children) self (self, child);
            };
            collectComponents (collectComponents, expected->root);
            for (const auto& id : ids)
            {
                auto component = componentFor (id);
                expect (component != nullptr);
                if (component == nullptr) continue;
                if (id == "transport.volume")
                    dynamic_cast<juce::Slider&> (*component).setSliderStyle (
                        juce::Slider::RotaryHorizontalVerticalDrag);
                root.addAndMakeVisible (*component);
                expect (bindings.bind (id, *component, report), report.toText());
                components.emplace (id, std::move (component));
            }

            SkinLayoutHost host (manager, bindings);
            expect (host.apply (SkinLayoutEntry::listening, appliedBounds,
                                1.25f, report), report.toText());

            for (const auto& id : ids)
                if (const auto* node = expected->component (id))
                {
                    auto& component = *components.at (id);
                    expect (rootBoundsFor (root, component)
                                == node->bounds.getSmallestIntegerContainer(),
                            id);
                    expectEquals (
                        component.getProperties().getWithDefault (
                            JuceSkinAdapter::styleProperty, {}).toString(),
                        node->style, id);
                    expectEquals (component.getExplicitFocusOrder(),
                                  node->focusOrder, id);
                    expect (component.isVisible() == node->visible, id);
                    expectWithinAbsoluteError (
                        static_cast<double> (component.getProperties()
                            .getWithDefault (
                                JuceSkinAdapter::scaleFactorProperty, 0.0)),
                        1.25, 0.0001, id);
                }

            juce::Image rendered (juce::Image::ARGB, 1240, 800, true);
            {
                juce::Graphics graphics (rendered);
                root.paintEntireComponent (graphics, true);
            }
            expect (rendered.getPixelAt (40, 26).getAlpha() > 0);

            auto* volume = dynamic_cast<juce::Slider*> (
                components.at ("transport.volume").get());
            expect (volume != nullptr);
            const auto* volumeNode = expected->component ("transport.volume");
            expect (volumeNode != nullptr);
            if (volume != nullptr && volumeNode != nullptr)
                expect (volume->getSliderStyle() == (volumeNode->presentation == "rotary"
                    ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal));
            expect (components.at ("queue.title")->isVisible());

            const auto previousBounds = rootBoundsFor (
                root, *components.at ("transport.playPause"));
            auto* previousParent = components.at ("transport.playPause")
                                       ->getParentComponent();
            SkinComponentBindings missingVolume;
            for (const auto& [id, component] : components)
                if (id != "transport.volume")
                    expect (missingVolume.bind (id, *component, report), report.toText());
            bindings = std::move (missingVolume);
            SkinValidationReport failure;
            expect (! host.apply (SkinLayoutEntry::listening, appliedBounds,
                                  1.25f, failure));
            expect (failure.hasCode ("COMPONENT007"), failure.toText());
            expect (rootBoundsFor (root, *components.at ("transport.playPause"))
                    == previousBounds);
            expect (components.at ("transport.playPause")->getParentComponent()
                    == previousParent);

            expect (bindings.bind ("transport.volume", *components.at ("transport.volume"), report),
                    report.toText());
            failure = {};
            expect (! host.apply (SkinLayoutEntry::listening,
                                  { 0, 0, 100, 100 }, 1.0f, failure));
            expect (rootBoundsFor (root, *components.at ("transport.playPause"))
                    == previousBounds);
            expect (components.at ("transport.playPause")->getParentComponent()
                    == previousParent);

            host.clear();
            for (const auto& [id, component] : components)
            {
                juce::ignoreUnused (id);
                expect (component->getParentComponent() == &root);
            }
            static_cast<void> (storage.deleteRecursively());
        }

        beginTest ("nested clipping, opacity, z-order and scroll state are committed transactionally");
        {
            const auto storage = freshHostDirectory();
            SkinValidationReport report;
            SkinManager manager;
            expect (manager.initialise (storage.getChildFile ("installs"), report),
                    report.toText());
            const auto source = writeScrollableHostSkin (storage);
            const auto importReport = manager.importSkin (source);
            expect (! importReport.hasErrors(), importReport.toText());
            expect (manager.preview ({ "com.example.scroll-host", "1.0.0" }, report),
                    report.toText());

            juce::Component root;
            root.setBounds (0, 0, 1240, 800);
            root.setVisible (true);
            SkinComponentBindings bindings;
            std::map<juce::String, std::unique_ptr<juce::Component>> components;
            const juce::StringArray ids {
                "app.status", "workspace.studio", "workspace.listening",
                "settings.open", "library.add", "queue.table",
                "transport.previous", "transport.playPause", "transport.next",
                "transport.seek", "transport.time", "transport.volume"
            };
            for (const auto& id : ids)
            {
                auto component = componentFor (id);
                expect (component != nullptr);
                if (component == nullptr) continue;
                if (id == "transport.volume")
                    dynamic_cast<juce::Slider&> (*component).setSliderStyle (
                        juce::Slider::RotaryHorizontalVerticalDrag);
                root.addAndMakeVisible (*component);
                expect (bindings.bind (id, *component, report), report.toText());
                components.emplace (id, std::move (component));
            }

            SkinLayoutHost host (manager, bindings);
            const juce::Rectangle<int> appliedBounds (31, 19, 1160, 740);
            expect (host.apply (SkinLayoutEntry::listening, appliedBounds, 1.0f,
                                report), report.toText());
            SkinValidationReport prePaintResizeReport;
            expect (host.apply (SkinLayoutEntry::listening, appliedBounds, 1.0f,
                                prePaintResizeReport),
                    "a resize before the first paint must retain pending entry animations\n"
                        + prePaintResizeReport.toText());
            auto& queue = *components.at ("queue.table");
            auto* viewport = ancestorViewport (queue);
            expect (viewport != nullptr);
            expect (queue.isVisible(), "queue hidden");
            expect (queue.getBounds() == juce::Rectangle<int> (0, 0, 1116, 200),
                    "queue=" + queue.getBounds().toString());
            expect (dynamic_cast<juce::Slider&> (
                        *components.at ("transport.volume")).getSliderStyle()
                        == juce::Slider::LinearHorizontal,
                    "empty presentation must select the catalog default target");
            if (viewport != nullptr)
            {
                expect (root.getLocalArea (viewport, viewport->getLocalBounds())
                        == juce::Rectangle<int> (49, 85, 1124, 68));
                expectEquals (viewport->getHeight(), 68);
                expectEquals (viewport->getScrollBarThickness(), 8);
                expectEquals (viewport->getMaximumVisibleWidth(),
                              queue.getWidth(),
                              "vertical scrollbar must not cover queue content");
                expect (viewport->getViewedComponent() != nullptr);
                if (viewport->getViewedComponent() != nullptr)
                {
                    expectEquals (viewport->getViewedComponent()->getHeight(), 200);
                    expect (viewport->getViewedComponent()->isVisible(),
                            "viewed component hidden");
                }
                viewport->setViewPosition (0, 50);
                expectEquals (viewport->getViewPositionY(), 50);
            }
            expectWithinAbsoluteError (static_cast<double> (queue.getAlpha()),
                                       0.4, 0.0001);

            juce::Image directQueue (juce::Image::ARGB, 40, 40, true);
            {
                juce::Graphics graphics (directQueue);
                queue.paintEntireComponent (graphics, true);
            }
            expect (directQueue.getPixelAt (8, 8).getRed() > 200,
                    "direct=" + directQueue.getPixelAt (8, 8).toDisplayString (true));

            juce::Image directViewport (juce::Image::ARGB, 120, 80, true);
            if (viewport != nullptr)
            {
                juce::Graphics graphics (directViewport);
                viewport->paintEntireComponent (graphics, true);
                expect (directViewport.getPixelAt (8, 8).getRed()
                            > directViewport.getPixelAt (8, 8).getGreen(),
                        "viewport="
                            + directViewport.getPixelAt (8, 8).toDisplayString (true)
                            + " queueRoot=" + rootBoundsFor (root, queue).toString()
                            + " parent=" + queue.getParentComponent()->getBounds().toString()
                            + " content="
                            + viewport->getViewedComponent()->getBounds().toString());
            }

            juce::Image image (juce::Image::ARGB, 1240, 800, true);
            {
                juce::Graphics graphics (image);
                root.paintEntireComponent (graphics, true);
            }
            expect (host.isAnimationDriveActive(),
                    "animated visual layers need an active repaint drive");
            const auto animationProbe = juce::Point<int> (
                appliedBounds.getX() + 2, appliedBounds.getY() + 2);
            const auto firstAnimatedAlpha = image.getPixelAt (
                animationProbe.x, animationProbe.y).getAlpha();
            if (viewport != nullptr)
            {
                const auto visibleArea = root.getLocalArea (
                    viewport, viewport->getLocalBounds());
                const auto inside = image.getPixelAt (
                    visibleArea.getX() + 8, visibleArea.getY() + 8);
                const auto below = image.getPixelAt (
                    visibleArea.getX() + 8, visibleArea.getBottom() + 8);
                expect (inside.getRed() > inside.getGreen(),
                        "inside=" + inside.toDisplayString (true)
                            + " area=" + visibleArea.toString());
                expect (below.getRed() <= below.getGreen() + 8,
                        "below=" + below.toDisplayString (true)
                            + " area=" + visibleArea.toString());
            }

            SkinValidationReport resizeReport;
            juce::Thread::sleep (120);
            expect (host.apply (SkinLayoutEntry::listening,
                                appliedBounds.withWidth (1140), 1.0f,
                                resizeReport), resizeReport.toText());
            auto* replacementViewport = ancestorViewport (queue);
            expect (replacementViewport != nullptr);
            if (replacementViewport != nullptr)
                expectEquals (replacementViewport->getViewPositionY(), 50);
            juce::Image resizedImage (juce::Image::ARGB, 1240, 800, true);
            {
                juce::Graphics graphics (resizedImage);
                root.paintEntireComponent (graphics, true);
            }
            expect (resizedImage.getPixelAt (animationProbe.x, animationProbe.y)
                            .getAlpha()
                        > firstAnimatedAlpha + 8,
                    "resizing the same workspace must continue, not restart, its entry animation");

            host.clear();
            expectWithinAbsoluteError (static_cast<double> (queue.getAlpha()),
                                       1.0, 0.0001);
            expect (queue.getParentComponent() == &root);
            expect (dynamic_cast<juce::Slider&> (
                        *components.at ("transport.volume")).getSliderStyle()
                        == juce::Slider::RotaryHorizontalVerticalDrag,
                    "clear must restore the original slider presentation");
            static_cast<void> (storage.deleteRecursively());
        }
    }
};

SkinLayoutHostTests skinLayoutHostTests;
} // namespace
