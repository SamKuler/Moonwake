#include "skin/SkinLayoutEngine.h"
#include "skin/SkinManager.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/SkinLayoutHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
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
            return std::make_unique<juce::Slider>();
        case SkinComponentKind::table:
            return std::make_unique<PaintedTable>();
        case SkinComponentKind::progressBar:
        case SkinComponentKind::artwork:
        case SkinComponentKind::effectChain:
        case SkinComponentKind::outputStage:
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
            const juce::StringArray ids {
                "app.brand.title", "app.status", "workspace.studio",
                "workspace.listening", "settings.open", "library.add",
                "queue.title", "queue.count", "queue.table",
                "artwork.cover", "artwork.metadata", "nowPlaying.title",
                "nowPlaying.metadata",
                "transport.previous", "transport.playPause", "transport.next",
                "transport.seek", "transport.time", "transport.volume",
                "effects.open"
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
            const juce::Rectangle<int> appliedBounds (37, 23, 1160, 740);
            expect (host.apply (SkinLayoutEntry::listening, appliedBounds,
                                1.25f, report), report.toText());

            const auto session = manager.activeSession();
            expect (session != nullptr);
            const auto skin = session != nullptr ? session->compiledSkin() : nullptr;
            expect (skin != nullptr);
            if (skin != nullptr)
            {
                auto expected = SkinLayoutEngine::resolve (
                    skin->layouts.at (SkinLayoutEntry::listening),
                    appliedBounds.toFloat(), 1.25f, report);
                expect (expected.has_value(), report.toText());
                if (expected)
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
            if (volume != nullptr)
                expect (volume->getSliderStyle()
                        == juce::Slider::RotaryHorizontalVerticalDrag);
            expect (components.at ("queue.title")->isVisible());

            const auto previousBounds = rootBoundsFor (
                root, *components.at ("transport.playPause"));
            auto* previousParent = components.at ("transport.playPause")
                                       ->getParentComponent();
            SkinValidationReport failure;
            expect (! host.apply (SkinLayoutEntry::studio, appliedBounds,
                                  1.25f, failure));
            expect (rootBoundsFor (root, *components.at ("transport.playPause"))
                    == previousBounds);
            expect (components.at ("transport.playPause")->getParentComponent()
                    == previousParent);

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
