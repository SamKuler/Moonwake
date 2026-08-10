#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinLayoutEngine.h"
#include "skin/SkinRenderer.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
juce::MemoryBlock bytes (const juce::String& text)
{
    return { text.toRawUTF8(), text.getNumBytesAsUTF8() };
}

juce::String manifestFor (juce::String id, juce::String extraEntrypoints = {},
                          int skinApi = 1, juce::String minApp = "0.1.0",
                          juce::String capabilities = "\"tokens\"")
{
    return "{\"skinApi\":" + juce::String (skinApi)
        + ",\"id\":\"" + id + "\",\"name\":\"Fixture\",\"version\":\"1.0.0\""
        + ",\"author\":{\"name\":\"Tests\"},\"minAppVersion\":\"" + minApp
        + "\",\"extends\":\"builtin.foundation\",\"capabilities\":["
        + capabilities + "],\"entrypoints\":{" + extraEntrypoints + "}}";
}

std::optional<SkinPackage> embedded (
    juce::String id, std::vector<SkinPackage::EmbeddedFile> files,
    SkinValidationReport& report)
{
    return SkinPackage::fromEmbedded (std::move (id), std::move (files), report);
}

juce::String customButton()
{
    return R"json({
      "apiVersion": 1,
      "target": "textButton",
      "layers": [
        { "id": "body", "type": "roundedRect", "frame": { "anchor": "bounds" },
          "radius": 8, "fill": { "$ref": "colors.fixture" } },
        { "id": "focus", "type": "roundedRect", "visible": { "$context": "state.focused" },
          "frame": { "anchor": "bounds", "inset": 1 }, "radius": 8,
          "fill": "#00000000", "stroke": { "color": "#FFFFFFFF", "width": 2 } }
      ]
    })json";
}

juce::String customChain()
{
    return R"json({
      "apiVersion": 1,
      "target": "effectChain",
      "layers": [
        { "id": "body", "type": "roundedRect", "frame": { "anchor": "bounds" },
          "radius": 10, "fill": { "$ref": "colors.fixture" } },
        { "id": "focus", "type": "roundedRect", "visible": { "$context": "state.focused" },
          "frame": { "anchor": "bounds", "inset": 1 }, "radius": 10,
          "fill": "#00000000", "stroke": { "color": "#FFFFFFFF", "width": 2 } }
      ]
    })json";
}

std::optional<SkinPackage> completeFixture (const SkinPackage& foundationPackage,
                                            SkinValidationReport& report)
{
    std::vector<SkinPackage::EmbeddedFile> files {
        { "manifest.json", bytes (manifestFor (
            "com.example.integration",
            "\"tokens\":\"tokens.json\",\"components\":\"components/index.json\","
            "\"layouts\":{\"studio\":\"layouts/studio.json\","
            "\"listening\":\"layouts/listening.json\","
            "\"effects\":\"layouts/effects.json\","
            "\"settings\":\"layouts/settings.json\"}",
            1, "0.1.0", "\"tokens\",\"renderers\",\"layouts\"")) },
        { "tokens.json", bytes (
            "{\"colors\":{\"fixture\":\"#336699FF\",\"control\":\"#010203FF\"},"
            "\"durations\":{\"fast\":333},\"metrics\":{\"radius\":16},"
            "\"strings\":{\"assetProbe\":\"assets/foundation-mark.svg\"},"
            "\"fonts\":{\"body\":{\"source\":\"assets/child-font.ttf\","
            "\"families\":[\"sans-serif\"],\"weight\":500}}}") },
        { "components/index.json", bytes (R"json({
            "textButton": { "custom": "components/button.json" },
            "artwork": { "localProbe": "components/artwork.json" },
            "effectChain": {
              "custom": {
                "renderer": "components/chain.json",
                "slots": {
                  "slot.surface": "surface.default",
                  "slot.name": "label.default",
                  "slot.bypass": "textButton.custom",
                  "slot.editor": "textButton.default",
                  "slot.remove": "textButton.default",
                  "slot.dragIndicator": "surface.default"
                }
              }
            }
        })json") },
        { "components/button.json", bytes (customButton()) },
        { "components/chain.json", bytes (customChain()) },
        { "components/artwork.json", bytes (R"json({
            "apiVersion": 1, "target": "artwork",
            "layers": [{ "id": "mark", "type": "svg",
              "frame": { "anchor": "bounds" },
              "source": "assets/foundation-mark.svg", "fit": "contain",
              "tint": "#FFFFFFFF" }]
        })json") },
        { "assets/foundation-mark.svg", bytes (
            "<svg width=\"8\" height=\"8\" viewBox=\"0 0 8 8\">"
            "<circle cx=\"4\" cy=\"4\" r=\"3\" fill=\"#336699\"/></svg>") },
        { "assets/child-font.ttf", bytes ("invalid child font with declared fallback") }
    };
    for (const auto& name : { "studio", "listening", "effects", "settings" })
    {
        const auto path = "layouts/" + juce::String (name) + ".json";
        const auto before = report.getDiagnostics().size();
        auto layout = foundationPackage.readBinary (path, report);
        if (report.getDiagnostics().size() != before) return std::nullopt;
        files.push_back ({ path, std::move (layout) });
    }
    return embedded ("complete-fixture", std::move (files), report);
}

std::optional<SkinPackage> noOpFocusedSlotFixture (SkinValidationReport& report)
{
    return embedded (
        "no-op-focused-slot",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.no-op-focused-slot",
                "\"tokens\":\"tokens.json\","
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"tokens\",\"renderers\"")) },
            { "tokens.json", bytes ("{\"colors\":{\"fixture\":\"#336699FF\"}}") },
            { "components/index.json", bytes (R"json({
                "textButton": { "noFocus": "components/no-focus-button.json" },
                "effectChain": {
                  "slotProbe": {
                    "renderer": "components/chain.json",
                    "slots": {
                      "slot.surface": "surface.default",
                      "slot.name": "label.default",
                      "slot.bypass": "textButton.noFocus",
                      "slot.editor": "textButton.default",
                      "slot.remove": "textButton.default",
                      "slot.dragIndicator": "surface.default"
                    }
                  }
                }
            })json") },
            { "components/no-focus-button.json", bytes (R"json({
                "apiVersion": 1,
                "target": "textButton",
                "layers": [
                  { "id": "body", "type": "roundedRect",
                    "frame": { "anchor": "bounds" },
                    "radius": 8, "fill": "#336699FF",
                    "visible": { "$expr": "true" } }
                ],
                "variants": {
                  "focused": { "body.visible": { "$expr": "true" } }
                }
            })json") },
            { "components/chain.json", bytes (customChain()) }
        },
        report);
}

std::optional<SkinPackage> exactSlotContextFixture (SkinValidationReport& report)
{
    return embedded (
        "exact-slot-context",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.exact-slot-context",
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"renderers\"")) },
            { "components/index.json", bytes (R"json({
                "surface": { "pluginRow": "components/plugin-row.json" },
                "textButton": { "contextFocus": "components/context-button.json" },
                "pluginList": {
                  "contextual": {
                    "renderer": "components/plugin-list.json",
                    "slots": { "row.surface": "surface.pluginRow" }
                  }
                },
                "effectChain": {
                  "contextFocus": {
                    "renderer": "components/effect-chain.json",
                    "slots": { "slot.bypass": "textButton.contextFocus" }
                  }
                }
            })json") },
            { "components/plugin-row.json", bytes (R"json({
                "apiVersion": 1, "target": "surface",
                "layers": [{
                  "id": "row", "type": "rect",
                  "frame": { "anchor": "bounds" }, "fill": "#FFFFFFFF",
                  "visible": { "$expr": "plugin.name == 'Compressor'" }
                }]
            })json") },
            { "components/plugin-list.json", bytes (R"json({
                "apiVersion": 1, "target": "pluginList",
                "layers": [{ "id": "body", "type": "rect",
                  "frame": { "anchor": "bounds" }, "fill": "#00000000" }]
            })json") },
            { "components/context-button.json", bytes (R"json({
                "apiVersion": 1, "target": "textButton",
                "layers": [{ "id": "body", "type": "rect",
                  "frame": { "anchor": "bounds" }, "fill": "#FFFFFFFF" }],
                "variants": {
                  "focused": {
                    "body.opacity": { "$expr": "0.5 + slot.index * 0" }
                  }
                }
            })json") },
            { "components/effect-chain.json", bytes (R"json({
                "apiVersion": 1, "target": "effectChain",
                "layers": [{ "id": "body", "type": "rect",
                  "frame": { "anchor": "bounds" }, "fill": "#00000000" }]
            })json") }
        },
        report);
}

std::optional<SkinPackage> contaminatedSlotContextFixture (
    SkinValidationReport& report)
{
    return embedded (
        "contaminated-slot-context",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.contaminated-slot-context",
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"renderers\"")) },
            { "components/index.json", bytes (R"json({
                "label": { "pluginRow": "components/plugin-label.json" },
                "table": {
                  "contaminated": {
                    "renderer": "components/table.json",
                    "slots": { "header.text": "label.pluginRow" }
                  }
                }
            })json") },
            { "components/plugin-label.json", bytes (R"json({
                "apiVersion": 1, "target": "label",
                "layers": [{
                  "id": "body", "type": "rect",
                  "frame": { "anchor": "bounds" },
                  "fill": "#FFFFFFFF",
                  "visible": { "$expr": "plugin.name == 'Compressor'" }
                }]
            })json") },
            { "components/table.json", bytes (R"json({
                "apiVersion": 1, "target": "table",
                "layers": [{ "id": "body", "type": "rect",
                  "frame": { "anchor": "bounds" }, "fill": "#00000000" }]
            })json") }
        },
        report);
}

std::optional<SkinPackage> constantFocusLayerFixture (SkinValidationReport& report)
{
    return embedded (
        "constant-focus-layer",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.constant-focus-layer",
                "\"tokens\":\"tokens.json\","
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"tokens\",\"renderers\"")) },
            { "tokens.json", bytes ("{\"colors\":{\"fixture\":\"#336699FF\"}}") },
            { "components/index.json", bytes (R"json({
                "textButton": { "constantFocus": "components/button.json" },
                "effectChain": {
                  "constantProbe": {
                    "renderer": "components/chain.json",
                    "slots": { "slot.bypass": "textButton.constantFocus" }
                  }
                }
            })json") },
            { "components/button.json", bytes (R"json({
                "apiVersion": 1,
                "target": "textButton",
                "layers": [{
                  "id": "body", "type": "roundedRect",
                  "frame": { "anchor": "bounds" }, "radius": 8,
                  "fill": "#336699FF",
                  "visible": { "$expr": "state.focused or true" }
                }]
            })json") },
            { "components/chain.json", bytes (customChain()) }
        },
        report);
}

std::optional<SkinPackage> invisibleFocusLayerFixture (SkinValidationReport& report)
{
    return embedded (
        "invisible-focus-layer",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.invisible-focus-layer",
                "\"tokens\":\"tokens.json\","
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"tokens\",\"renderers\"")) },
            { "tokens.json", bytes ("{\"colors\":{\"fixture\":\"#336699FF\"}}") },
            { "components/index.json", bytes (R"json({
                "textButton": { "invisibleFocus": "components/button.json" },
                "effectChain": {
                  "invisibleProbe": {
                    "renderer": "components/chain.json",
                    "slots": { "slot.bypass": "textButton.invisibleFocus" }
                  }
                }
            })json") },
            { "components/button.json", bytes (R"json({
                "apiVersion": 1,
                "target": "textButton",
                "layers": [{
                  "id": "focus", "type": "roundedRect",
                  "frame": { "anchor": "bounds" }, "radius": 8,
                  "fill": "#FFFFFFFF", "opacity": 0,
                  "visible": { "$context": "state.focused" }
                }]
            })json") },
            { "components/chain.json", bytes (customChain()) }
        },
        report);
}

std::optional<SkinPackage> singleProbeFocusFixture (SkinValidationReport& report)
{
    return embedded (
        "single-probe-focus",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.single-probe-focus",
                "\"tokens\":\"tokens.json\","
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"tokens\",\"renderers\"")) },
            { "tokens.json", bytes ("{\"colors\":{\"fixture\":\"#336699FF\"}}") },
            { "components/index.json", bytes (R"json({
                "textButton": { "singleProbe": "components/button.json" },
                "effectChain": {
                  "singleProbe": {
                    "renderer": "components/chain.json",
                    "slots": { "slot.bypass": "textButton.singleProbe" }
                  }
                }
            })json") },
            { "components/button.json", bytes (R"json({
                "apiVersion": 1,
                "target": "textButton",
                "layers": [{
                  "id": "focus", "type": "roundedRect",
                  "frame": { "anchor": "bounds" }, "radius": 8,
                  "fill": "#FFFFFFFF",
                  "visible": { "$expr": "state.focused and bounds.width == 160" }
                }]
            })json") },
            { "components/chain.json", bytes (customChain()) }
        },
        report);
}

void routeTextButtonsThroughSafeExactStyle (CompiledSkin& skin)
{
    skin.renderers.emplace ("textButton.safe",
                            skin.renderers.at ("textButton.default"));
    for (auto& [ignored, renderer] : skin.renderers)
    {
        juce::ignoreUnused (ignored);
        for (auto& [ignoredSlot, style] : renderer.slotStyles)
        {
            juce::ignoreUnused (ignoredSlot);
            if (style == "textButton.default") style = "textButton.safe";
        }
    }
    const auto visit = [&] (const auto& self, SkinLayoutNode& node) -> void
    {
        if (const auto* component = std::get_if<SkinComponentLayout> (&node.payload))
            if (const auto* descriptor = PlayerComponentCatalog::find (
                    component->componentId);
                descriptor != nullptr
                && descriptor->defaultRendererTarget == "textButton")
                node.common.style = "textButton.safe";
        const auto visitChildren = [&] (auto& children)
        {
            for (auto& child : children) self (self, child);
        };
        if (auto* flex = std::get_if<SkinFlexLayout> (&node.payload))
            visitChildren (flex->children);
        else if (auto* grid = std::get_if<SkinGridLayout> (&node.payload))
            visitChildren (grid->children);
        else if (auto* stack = std::get_if<SkinStackLayout> (&node.payload))
            visitChildren (stack->children);
        else if (auto* scroll = std::get_if<SkinScrollLayout> (&node.payload))
        {
            if (scroll->child != nullptr) self (self, *scroll->child);
        }
    };
    for (auto& [ignored, layout] : skin.layouts)
    {
        juce::ignoreUnused (ignored);
        visit (visit, layout.base);
        for (auto& breakpoint : layout.breakpoints) visit (visit, breakpoint.root);
    }
}

std::optional<SkinPackage> omittedFocusedSlotFixture (SkinValidationReport& report)
{
    return embedded (
        "omitted-focused-slot",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.omitted-focused-slot",
                "\"tokens\":\"tokens.json\","
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"tokens\",\"renderers\"")) },
            { "tokens.json", bytes ("{\"colors\":{\"fixture\":\"#336699FF\"}}") },
            { "components/index.json", bytes (R"json({
                "textButton": { "default": "components/no-focus-button.json" },
                "effectChain": { "omittedProbe": "components/chain.json" }
            })json") },
            { "components/no-focus-button.json", bytes (R"json({
                "apiVersion": 1,
                "target": "textButton",
                "layers": [
                  { "id": "body", "type": "roundedRect",
                    "frame": { "anchor": "bounds" },
                    "radius": 8, "fill": "#336699FF" }
                ]
            })json") },
            { "components/chain.json", bytes (customChain()) }
        },
        report);
}

std::optional<SkinPackage> foundationWithAssetToken (
    const SkinPackage& source, SkinValidationReport& report)
{
    std::vector<SkinPackage::EmbeddedFile> files;
    for (const auto& path : source.listFiles())
    {
        if (path == "tokens.json")
        {
            auto tokens = juce::JSON::parse (source.readText (path, report));
            auto strings = tokens.getProperty ("strings", {});
            strings.getDynamicObject()->setProperty (
                "assetProbe", "assets/foundation-mark.svg");
            auto fonts = tokens.getProperty ("fonts", {});
            auto body = fonts.getProperty ("body", {});
            body.getDynamicObject()->setProperty ("source", "assets/probe-font.ttf");
            files.push_back ({ path, bytes (juce::JSON::toString (tokens)) });
            continue;
        }
        if (path == "components/artwork-asset-probe.json")
        {
            files.push_back ({ path, bytes (R"json({
                "apiVersion": 1,
                "target": "artwork",
                "layers": [{
                  "id": "mark", "type": "svg",
                  "frame": { "anchor": "bounds" },
                  "source": { "$ref": "strings.assetProbe" },
                  "fit": "contain", "tint": "#FFFFFFFF"
                }]
            })json") });
            continue;
        }
        files.push_back ({ path, source.readBinary (path, report) });
    }
    files.push_back ({ "assets/probe-font.ttf",
                       bytes ("invalid parent font with declared fallback") });
    return report.hasErrors()
        ? std::nullopt
        : embedded ("foundation-with-asset-token", std::move (files), report);
}

std::optional<SkinPackage> localInheritedAssetFixture (SkinValidationReport& report)
{
    return embedded (
        "local-inherited-asset",
        {
            { "manifest.json", bytes (manifestFor (
                "com.example.local-inherited-asset",
                "\"components\":\"components/index.json\"",
                1, "0.1.0", "\"renderers\"")) },
            { "components/index.json", bytes (R"json({
                "artwork": { "localInherited": "components/artwork.json" },
                "label": { "localInheritedFont": "components/label.json" }
            })json") },
            { "components/artwork.json", bytes (R"json({
                "apiVersion": 1,
                "target": "artwork",
                "layers": [{
                  "id": "mark", "type": "svg",
                  "frame": { "anchor": "bounds" },
                  "source": { "$ref": "strings.assetProbe" },
                  "fit": "contain", "tint": "#FFFFFFFF"
                }]
            })json") },
            { "components/label.json", bytes (R"json({
                "apiVersion": 1,
                "target": "label",
                "layers": [{
                  "id": "text", "type": "text",
                  "frame": { "anchor": "bounds" },
                  "text": "Probe", "font": { "$ref": "fonts.body" },
                  "fontSize": 12, "color": "#FFFFFFFF"
                }]
            })json") }
        },
        report);
}

class SilentFaultSink final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override {}
};

juce::Colour renderCentre (const SkinRenderStyleEntry& style, bool selected = false)
{
    juce::Image image (juce::Image::ARGB, 32, 32, true);
    juce::Graphics graphics (image);
    SkinRenderContext context;
    context.bounds = image.getBounds().toFloat();
    context.contentBounds = context.bounds;
    context.state.enabled = true;
    context.state.selected = selected;
    SkinRenderer renderer (style.assets);
    SilentFaultSink sink;
    renderer.draw (graphics, style.renderer, context, true, sink);
    return image.getPixelAt (16, 16);
}

juce::Colour renderCentreWithContext (const SkinRenderStyleEntry& style,
                                      SkinRenderContext context)
{
    juce::Image image (juce::Image::ARGB, 160, 36, true);
    juce::Graphics graphics (image);
    context.bounds = image.getBounds().toFloat();
    context.contentBounds = context.bounds;
    context.state.enabled = true;
    SkinRenderer renderer (style.assets);
    SilentFaultSink sink;
    renderer.draw (graphics, style.renderer, context, true, sink);
    return image.getPixelAt (80, 18);
}

template <typename Callback>
void visitLayoutComponents (const SkinLayoutNode& node,
                            const juce::String& parentId,
                            Callback&& callback)
{
    if (const auto* component = std::get_if<SkinComponentLayout> (&node.payload))
        callback (component->componentId, parentId);

    const auto visit = [&] (const auto& children)
    {
        for (const auto& child : children)
            visitLayoutComponents (child, node.common.id, callback);
    };
    if (const auto* flexLayout = std::get_if<SkinFlexLayout> (&node.payload))
        visit (flexLayout->children);
    else if (const auto* gridLayout = std::get_if<SkinGridLayout> (&node.payload))
        visit (gridLayout->children);
    else if (const auto* stackLayout = std::get_if<SkinStackLayout> (&node.payload))
        visit (stackLayout->children);
    else if (const auto* scrollLayout = std::get_if<SkinScrollLayout> (&node.payload))
        if (scrollLayout->child != nullptr)
            visitLayoutComponents (*scrollLayout->child, node.common.id, callback);
}

std::map<juce::String, juce::String> componentParents (
    const SkinLayoutDefinition& layout)
{
    std::map<juce::String, juce::String> result;
    visitLayoutComponents (layout.base, {}, [&] (const auto& id, const auto& parent)
    {
        result.emplace (id, parent);
    });
    return result;
}

std::optional<juce::Colour> colourProperty (
    const SkinRenderStyleEntry& style, juce::StringRef layerId,
    juce::StringRef property)
{
    const auto layer = std::find_if (
        style.renderer.baseLayers.begin(), style.renderer.baseLayers.end(),
        [&] (const auto& candidate) { return candidate.id == layerId; });
    if (layer == style.renderer.baseLayers.end()) return std::nullopt;
    const auto binding = layer->properties.find (juce::String (property));
    if (binding == layer->properties.end()) return std::nullopt;
    const auto resolved = binding->second.resolve ({}, {});
    if (const auto* colour = std::get_if<juce::Colour> (&resolved)) return *colour;
    return std::nullopt;
}

std::optional<double> numberProperty (
    const SkinRenderStyleEntry& style, juce::StringRef layerId,
    juce::StringRef property)
{
    const std::function<const SkinRenderLayer* (
        const std::vector<SkinRenderLayer>&)> findLayer = [&] (const auto& layers)
    {
        for (const auto& candidate : layers)
        {
            if (candidate.id == layerId) return &candidate;
            if (const auto* child = findLayer (candidate.children)) return child;
        }
        return static_cast<const SkinRenderLayer*> (nullptr);
    };
    const auto* layer = findLayer (style.renderer.baseLayers);
    if (layer == nullptr) return std::nullopt;
    const auto binding = layer->properties.find (juce::String (property));
    if (binding == layer->properties.end()) return std::nullopt;
    const auto resolved = binding->second.resolve ({}, {});
    if (const auto* number = std::get_if<double> (&resolved)) return *number;
    return std::nullopt;
}

std::map<juce::String, juce::String> componentStyles (
    const SkinLayoutDefinition& layout)
{
    std::map<juce::String, juce::String> result;
    const std::function<void (const SkinLayoutNode&)> visit = [&] (const auto& node)
    {
        if (const auto* component = std::get_if<SkinComponentLayout> (&node.payload))
            result.emplace (component->componentId, node.common.style);

        const auto visitChildren = [&] (const auto& children)
        {
            for (const auto& child : children) visit (child);
        };
        if (const auto* flex = std::get_if<SkinFlexLayout> (&node.payload))
            visitChildren (flex->children);
        else if (const auto* grid = std::get_if<SkinGridLayout> (&node.payload))
            visitChildren (grid->children);
        else if (const auto* stack = std::get_if<SkinStackLayout> (&node.payload))
            visitChildren (stack->children);
        else if (const auto* scroll = std::get_if<SkinScrollLayout> (&node.payload))
            if (scroll->child != nullptr) visit (*scroll->child);
    };
    visit (layout.base);
    return result;
}

std::optional<double> variantNumber (
    const SkinRenderStyleEntry& style, juce::StringRef variantName,
    juce::StringRef property)
{
    const auto variant = std::find_if (
        style.renderer.variants.begin(), style.renderer.variants.end(),
        [&] (const auto& candidate) { return candidate.name == variantName; });
    if (variant == style.renderer.variants.end()) return std::nullopt;
    const auto binding = variant->properties.find (juce::String (property));
    if (binding == variant->properties.end()) return std::nullopt;
    const auto resolved = binding->second.resolve ({}, {});
    if (const auto* number = std::get_if<double> (&resolved)) return *number;
    return std::nullopt;
}

class SkinIntegrationTests final : public juce::UnitTest
{
public:
    SkinIntegrationTests() : UnitTest ("SkinIntegration", "skin") {}

    void runTest() override
    {
        beginTest ("embedded Foundation and Default compile as complete immutable skins");
        SkinValidationReport foundationReport;
        const auto embeddedFoundation = SkinBuiltInPackages::create (
            SkinBuiltInPackages::Package::foundation, foundationReport);
        expect (embeddedFoundation.has_value(), foundationReport.toText());
        if (! embeddedFoundation) return;
        const auto foundationPackage = *embeddedFoundation;
        const auto foundation = SkinCompiler::compile (foundationPackage, {}, foundationReport);
        expect (foundation != nullptr, foundationReport.toText());
        if (foundation)
        {
            expectEquals (foundation->manifest.id, juce::String ("builtin.foundation"));
            expectEquals (static_cast<int> (foundation->layouts.size()), 4);
            for (const auto& targetName : skinRenderTargetNames())
            {
                const auto target = skinRenderTargetFromName (targetName);
                expect (target && foundation->resolveStyle (*target, {}) != nullptr,
                        "Missing foundation default for " + targetName);
            }
            expect (foundation->resolveStyle (SkinRenderTarget::scrollBar,
                                              "scrollBar.default") != nullptr);
            expect (foundation->resolveStyle (SkinRenderTarget::popupMenu,
                                              "popupMenu.default") != nullptr);
            expect (foundation->resolveStyle (SkinRenderTarget::tooltip,
                                              "tooltip.default") != nullptr);
        }

        SkinValidationReport defaultReport;
        const auto defaultPackage = SkinBuiltInPackages::create (
            SkinBuiltInPackages::Package::defaultSkin, defaultReport);
        const auto defaultSkin = defaultPackage && foundation
            ? SkinCompiler::compile (*defaultPackage, foundation, defaultReport) : nullptr;
        expect (defaultSkin != nullptr, defaultReport.toText());
        if (defaultSkin)
        {
            expectEquals (defaultSkin->manifest.id, juce::String ("builtin.default"));
            expect (defaultSkin->resolveStyle (SkinRenderTarget::surface,
                                               "surface.studio") != nullptr);
            expect (defaultSkin->resolveStyle (SkinRenderTarget::surface,
                                               "surface.listening") != nullptr);
            expect (defaultSkin->resolveStyle (SkinRenderTarget::rotarySlider,
                                               "rotarySlider.listening") != nullptr);
            expect (defaultSkin->resolveStyle (SkinRenderTarget::scrollBar,
                                               "scrollBar.default")
                    == &defaultSkin->renderers.at ("scrollBar.default"));
            expect (defaultSkin->resolveStyle (SkinRenderTarget::popupMenu,
                                               "popupMenu.default")
                    == &defaultSkin->renderers.at ("popupMenu.default"));
            expect (defaultSkin->resolveStyle (SkinRenderTarget::tooltip,
                                               "tooltip.default")
                    == &defaultSkin->renderers.at ("tooltip.default"));
            expectEquals (defaultSkin->layouts.at (SkinLayoutEntry::listening).base.common.id,
                          juce::String ("listening-root"));
            expectWithinAbsoluteError (
                defaultSkin->layouts.at (SkinLayoutEntry::listening).base.common.padding.left.value,
                0.0f, 0.001f);
        }

        beginTest ("built-in typography roles and optical placement are frozen");
        if (foundation && defaultSkin)
        {
            const auto* foundationBody = foundation->tokens.find<SkinFontValue> (
                "fonts.body");
            const auto* foundationTitle = foundation->tokens.find<SkinFontValue> (
                "fonts.title");
            expect (foundationBody != nullptr && foundationBody->weight == 400
                    && foundationBody->families
                        == juce::StringArray ({ "Helvetica Neue", "Arial",
                                                "sans-serif" }));
            expect (foundationTitle != nullptr && foundationTitle->weight == 600
                    && foundationTitle->families
                        == juce::StringArray ({ "Avenir Next", "Helvetica Neue",
                                                "sans-serif" }));

            const auto* body = defaultSkin->tokens.find<SkinFontValue> (
                "fonts.workspaceBody");
            const auto* medium = defaultSkin->tokens.find<SkinFontValue> (
                "fonts.workspaceMedium");
            const auto* title = defaultSkin->tokens.find<SkinFontValue> (
                "fonts.workspaceTitle");
            const auto* data = defaultSkin->tokens.find<SkinFontValue> (
                "fonts.workspaceData");
            expect (body != nullptr && body->weight == 400
                    && body->families == juce::StringArray ({
                        "Avenir Next", "Helvetica Neue", "sans-serif" }));
            expect (medium != nullptr && medium->weight == 600
                    && medium->families == juce::StringArray ({
                        "Avenir Next", "Helvetica Neue", "sans-serif" }));
            expect (title != nullptr && title->weight == 600
                    && title->families == juce::StringArray ({
                        "Avenir Next", "Helvetica Neue", "sans-serif" }));
            expect (data != nullptr && data->weight == 500
                    && data->families == juce::StringArray ({
                        "SF Mono", "Menlo", "monospace" })
                    && std::abs (data->tracking - 0.015f) < 0.0001f);

            for (const auto* styleName : {
                     "label.studioData", "label.studioTable",
                     "label.studioTableMetadata", "label.listeningData",
                     "label.listeningTable", "label.listeningTableMetadata" })
            {
                const auto found = defaultSkin->renderers.find (styleName);
                expect (found != defaultSkin->renderers.end(), styleName);
                if (found != defaultSkin->renderers.end())
                    expect (defaultSkin->resolveStyle (
                                SkinRenderTarget::label, styleName) == &found->second,
                            styleName);
            }

            for (const auto& [tableName, labelName] : {
                     std::pair { "table.studio", "label.studioTable" },
                     std::pair { "table.listening", "label.listeningTable" } })
                for (const auto* slot : { "header.text", "cell.title",
                                          "cell.duration" })
                    expectEquals (defaultSkin->renderers.at (tableName)
                                      .slotStyles.at (slot),
                                  juce::String (labelName));
            expectEquals (defaultSkin->renderers.at ("table.studio")
                              .slotStyles.at ("cell.location"),
                          juce::String ("label.studioTableMetadata"));
            expectEquals (defaultSkin->renderers.at ("table.listening")
                              .slotStyles.at ("cell.location"),
                          juce::String ("label.listeningTableMetadata"));
            for (const auto* styleName : { "label.studioTableMetadata",
                                           "label.listeningTableMetadata" })
            {
                const auto& paths = defaultSkin->renderers.at (styleName)
                                        .renderer.contextPaths;
                expect (paths.contains ("row.artist"), styleName);
                expect (paths.contains ("row.album"), styleName);
                expect (paths.contains ("text.primary"), styleName);
            }

            for (const auto* styleName : { "label.studioTable",
                                           "label.listeningTable" })
            {
                const auto found = defaultSkin->renderers.find (styleName);
                if (found == defaultSkin->renderers.end()) continue;
                expectWithinAbsoluteError (
                    numberProperty (found->second, "label", "frame.inset.left")
                        .value_or (-1.0), 8.0, 0.0001);
                expectWithinAbsoluteError (
                    numberProperty (found->second, "label", "frame.inset.right")
                        .value_or (-1.0), 8.0, 0.0001);
            }

            const auto checkNumber = [&] (const char* style, const char* layer,
                                          const char* property, double expected)
            {
                expectWithinAbsoluteError (
                    numberProperty (defaultSkin->renderers.at (style), layer, property)
                        .value_or (-1.0), expected, 0.0001,
                    juce::String (style) + ":" + property);
            };
            for (const auto& [style, size] : {
                     std::pair { "label.studioBrand", 17.0 },
                     std::pair { "label.studioTitle", 22.0 },
                     std::pair { "label.studio", 13.0 },
                     std::pair { "label.studioData", 11.5 },
                     std::pair { "label.studioTable", 12.5 },
                     std::pair { "label.listeningBrand", 18.0 },
                     std::pair { "label.listeningTitle", 27.0 },
                     std::pair { "label.listening", 13.0 },
                     std::pair { "label.listeningMuted", 12.0 },
                     std::pair { "label.listeningData", 12.5 },
                     std::pair { "label.listeningSection", 11.5 },
                     std::pair { "label.listeningTable", 13.0 } })
            {
                checkNumber (style, "label", "fontSize", size);
                expect (defaultSkin->renderers.at (style).renderer.contextPaths
                            .contains ("text.align"), style);
            }
            for (const auto* style : { "textButton.studioNeutral",
                                       "textButton.studioPrimary" })
            {
                checkNumber (style, "label", "frame.inset.left", 10.0);
                checkNumber (style, "label", "frame.inset.right", 10.0);
                checkNumber (style, "label", "frame.inset.bottom", 1.0);
                checkNumber (style, "label", "fontSize", 12.5);
            }
            for (const auto* style : { "textButton.listeningNeutral",
                                       "textButton.listeningPrimary" })
            {
                checkNumber (style, "label", "frame.inset.left", 10.0);
                checkNumber (style, "label", "frame.inset.right", 10.0);
                checkNumber (style, "label", "frame.inset.bottom", 1.0);
                checkNumber (style, "label", "fontSize", 13.0);
            }
            checkNumber ("comboBox.listening", "label", "frame.inset.left", 12.0);
            checkNumber ("comboBox.listening", "label", "frame.inset.right", 34.0);
            for (const auto* layer : { "heading", "layout", "meter-label" })
                checkNumber ("outputStage.studio", layer, "fontSize", 11.5);
            for (const auto* layer : { "source-label", "device", "matrix-mode" })
                checkNumber ("outputStage.studio", layer, "fontSize", 10.5);

            const auto studioStyles = componentStyles (
                defaultSkin->layouts.at (SkinLayoutEntry::studio));
            expectEquals (studioStyles.at ("queue.count"),
                          juce::String ("label.studioData"));
            expectEquals (studioStyles.at ("transport.time"),
                          juce::String ("label.studioData"));
            expectEquals (studioStyles.at ("app.status"),
                          juce::String ("label.studioData"));
            for (const auto& [component, style] : {
                     std::pair { "settings.open", "textButton.studioIconNeutral" },
                     std::pair { "library.add", "textButton.studioIconPrimary" },
                     std::pair { "effects.open", "textButton.studioIconNeutral" },
                     std::pair { "transport.previous", "textButton.studioIconNeutral" },
                     std::pair { "transport.playPause", "textButton.studioIconPrimary" },
                     std::pair { "transport.next", "textButton.studioIconNeutral" } })
                expectEquals (studioStyles.at (component), juce::String (style));
            const auto listeningStyles = componentStyles (
                defaultSkin->layouts.at (SkinLayoutEntry::listening));
            expectEquals (listeningStyles.at ("queue.count"),
                          juce::String ("label.listeningData"));
            expectEquals (listeningStyles.at ("transport.time"),
                          juce::String ("label.listeningData"));
            expectEquals (listeningStyles.at ("app.status"),
                          juce::String ("label.listeningMuted"));
            for (const auto& [component, style] : {
                     std::pair { "settings.open", "textButton.listeningIconNeutral" },
                     std::pair { "library.add", "textButton.listeningIconPrimary" },
                     std::pair { "effects.open", "textButton.listeningIconNeutral" },
                     std::pair { "transport.previous", "textButton.listeningIconNeutral" },
                     std::pair { "transport.playPause", "textButton.listeningIconPrimary" },
                     std::pair { "transport.next", "textButton.listeningIconNeutral" } })
                expectEquals (listeningStyles.at (component), juce::String (style));

            const auto effectsStyles = componentStyles (
                defaultSkin->layouts.at (SkinLayoutEntry::effects));
            expectEquals (effectsStyles.at ("effects.close"),
                          juce::String ("textButton.listeningIconNeutral"));
            expectEquals (effectsStyles.at ("effects.add"),
                          juce::String ("textButton.listeningIconPrimary"));
            expectEquals (effectsStyles.at ("effects.filter"),
                          juce::String ("textEditor.listening"));
            expectEquals (effectsStyles.at ("effects.catalog"),
                          juce::String ("comboBox.listening"));
            expectEquals (effectsStyles.at ("effects.chain"),
                          juce::String ("effectChain.listening"));

            const auto settingsStyles = componentStyles (
                defaultSkin->layouts.at (SkinLayoutEntry::settings));
            expectEquals (settingsStyles.at ("settings.close"),
                          juce::String ("textButton.listeningIconNeutral"));
            expectEquals (settingsStyles.at ("settings.audio.heading"),
                          juce::String ("label.listeningSection"));
            expectEquals (settingsStyles.at ("settings.skins.heading"),
                          juce::String ("label.listeningSection"));
            expectEquals (settingsStyles.at ("settings.plugins.scanAll"),
                          juce::String ("textButton.listeningLeadingPrimary"));
            for (const auto* component : {
                     "settings.plugins.scanFolder", "settings.skins.import",
                     "settings.skins.reload", "settings.skins.preview",
                     "settings.skins.remove" })
                expectEquals (settingsStyles.at (component),
                              juce::String ("textButton.listeningLeadingNeutral"));

            for (const auto* style : { "surface.studioIcon",
                                       "surface.studioOnPrimaryIcon",
                                       "surface.listeningIcon",
                                       "surface.listeningOnPrimaryIcon" })
            {
                expect (defaultSkin->renderers.contains (style), style);
                if (defaultSkin->renderers.contains (style))
                    expect (defaultSkin->renderers.at (style).renderer.contextPaths
                                .contains ("icon.path"), style);
            }
            expectEquals (defaultSkin->renderers.at (
                              "textButton.listeningIconPrimary")
                              .slotStyles.at ("icon.play"),
                          juce::String ("surface.listeningOnPrimaryIcon"));
            checkNumber ("textButton.listeningLeadingNeutral", "label",
                         "frame.inset.left", 34.0);

            const auto& skinListSlots = defaultSkin->renderers.at (
                "skinList.listening").slotStyles;
            const std::map<juce::String, juce::String> expectedSkinListSlots {
                { "row.surface", "surface.listeningSkinCard" },
                { "row.name", "label.listening" },
                { "row.author", "label.listeningMuted" },
                { "row.version", "label.listeningData" },
                { "row.status", "label.listeningSkinStatus" }
            };
            expect (skinListSlots == expectedSkinListSlots,
                    "The public skin library anatomy must remain explicit.");
            expect (defaultSkin->renderers.contains ("surface.listeningSkinCard"));
            expect (defaultSkin->renderers.contains ("label.listeningSkinStatus"));

            expectEquals (foundation->manifest.version, juce::String ("1.0.0"));
            expectEquals (foundation->manifest.minAppVersion, juce::String ("0.2.0"));
            expectEquals (foundation->manifest.license, juce::String ("AGPL-3.0-only"));
            expectEquals (defaultSkin->manifest.version, juce::String ("1.0.0"));
            expectEquals (defaultSkin->manifest.minAppVersion, juce::String ("0.2.0"));
            expectEquals (defaultSkin->manifest.license, juce::String ("AGPL-3.0-only"));
        }

        beginTest ("built-in routes keep library actions out of Settings and in queue headers");
        if (foundation && defaultSkin)
        {
            for (const auto* skin : { foundation.get(), defaultSkin.get() })
            {
                for (const auto entry : { SkinLayoutEntry::studio,
                                          SkinLayoutEntry::listening })
                {
                    const auto parents = componentParents (skin->layouts.at (entry));
                    expect (parents.contains ("library.add"));
                    if (parents.contains ("library.add"))
                        expect (parents.at ("library.add").contains ("queue-header"),
                                "Add Music must live in the queue header");
                    expect (parents.contains ("settings.open"));
                    expect (parents.contains ("effects.open"));
                    expectWithinAbsoluteError (
                        skin->layouts.at (entry).base.common.padding.left.value,
                        0.0f, 0.001f,
                        "A/B root padding must match at zero");
                }

                const auto settings = componentParents (
                    skin->layouts.at (SkinLayoutEntry::settings));
                for (const auto& id : {
                         "settings.title", "settings.close",
                         "settings.audio.device", "settings.audio.sampleRate",
                         "settings.audio.bufferSize", "settings.audio.channelLayout",
                         "settings.audio.channelMap", "settings.plugins.formats",
                         "settings.plugins.knownList", "settings.plugins.scanAll",
                         "settings.plugins.scanFolder", "settings.plugins.cancel",
                         "settings.plugins.progress", "settings.skins.library",
                         "settings.skins.import", "settings.skins.remove",
                         "settings.skins.preview", "settings.skins.apply",
                         "settings.skins.cancelPreview", "settings.skins.reload" })
                    expect (settings.contains (id), id);
                expect (! settings.contains ("library.add"));
                for (const auto& [id, parent] : settings)
                {
                    juce::ignoreUnused (parent);
                    expect (id == "settings.title" || id == "settings.close"
                            || id.startsWith ("settings.audio.")
                            || id.startsWith ("settings.plugins.")
                            || id.startsWith ("settings.skins."), id);
                }

                const auto effects = componentParents (
                    skin->layouts.at (SkinLayoutEntry::effects));
                for (const auto& required : {
                         "effects.close", "effects.filter", "effects.catalog",
                         "effects.add", "effects.chain" })
                    expect (effects.contains (required), required);
            }
        }

        beginTest ("Listening B explicitly supplies a complete bright native-control family");
        if (defaultSkin)
        {
            const std::vector<std::pair<SkinRenderTarget, juce::String>> styles {
                { SkinRenderTarget::label, "label.listening" },
                { SkinRenderTarget::textButton, "textButton.listeningNeutral" },
                { SkinRenderTarget::textButton, "textButton.listeningPrimary" },
                { SkinRenderTarget::toggleButton, "toggleButton.listening" },
                { SkinRenderTarget::comboBox, "comboBox.listening" },
                { SkinRenderTarget::textEditor, "textEditor.listening" },
                { SkinRenderTarget::linearSlider, "linearSlider.listening" },
                { SkinRenderTarget::rotarySlider, "rotarySlider.listening" },
                { SkinRenderTarget::progressBar, "progressBar.listening" },
                { SkinRenderTarget::table, "table.listening" },
                { SkinRenderTarget::surface, "surface.listening" },
                { SkinRenderTarget::artwork, "artwork.listening" },
                { SkinRenderTarget::effectChain, "effectChain.listening" },
                { SkinRenderTarget::channelMap, "channelMap.listening" },
                { SkinRenderTarget::pluginList, "pluginList.listening" },
                { SkinRenderTarget::skinList, "skinList.listening" }
            };
            for (const auto& [target, style] : styles)
                expect (defaultSkin->resolveStyle (target, style)
                            == &defaultSkin->renderers.at (style), style);

            const auto token = [&] (juce::StringRef path)
            {
                return defaultSkin->tokens.find<juce::Colour> (path);
            };
            for (const auto* path : {
                     "colors.listening.background", "colors.listening.surface",
                     "colors.listening.control", "colors.listening.track" })
            {
                const auto* colour = token (path);
                expect (colour != nullptr && colour->getPerceivedBrightness() > 0.62f,
                        path);
            }

            const auto neutralFill = colourProperty (
                defaultSkin->renderers.at ("textButton.listeningNeutral"),
                "body", "fill");
            const auto primaryFill = colourProperty (
                defaultSkin->renderers.at ("textButton.listeningPrimary"),
                "body", "fill");
            expect (neutralFill && token ("colors.listening.control")
                    && *neutralFill == *token ("colors.listening.control"));
            expect (primaryFill && token ("colors.listening.active")
                    && *primaryFill == *token ("colors.listening.active"));
            for (const auto* style : { "textButton.listeningNeutral",
                                       "textButton.listeningPrimary" })
            {
                const auto opacity = variantNumber (
                    defaultSkin->renderers.at (style), "disabled", "label.opacity");
                expect (opacity && *opacity >= 0.55, style);
            }

            expect (renderCentre (defaultSkin->renderers.at ("surface.listening"))
                    != renderCentre (defaultSkin->renderers.at ("surface.listening"), true),
                    "Listening skin rows must render a visible selected surface.");
            expect (renderCentre (foundation->renderers.at ("surface.default"))
                    != renderCentre (foundation->renderers.at ("surface.default"), true),
                    "Foundation skin rows must render a visible selected surface.");

            for (const auto& probe : {
                     std::tuple { "comboBox.listening", "body", "fill",
                                  "colors.listening.control" },
                     std::tuple { "textEditor.listening", "body", "fill",
                                  "colors.listening.surface" },
                     std::tuple { "table.listening", "body", "fill",
                                  "colors.listening.surface" },
                     std::tuple { "popupMenu.default", "body", "fill",
                                  "colors.listening.surface" } })
            {
                const auto [style, layer, property, tokenPath] = probe;
                const auto colour = colourProperty (
                    defaultSkin->renderers.at (style), layer, property);
                expect (colour && token (tokenPath) && *colour == *token (tokenPath),
                        style);
            }

            for (const auto* skin : { foundation.get(), defaultSkin.get() })
            {
                const auto& popup = skin->renderers.at ("popupMenu.default");
                SkinRenderContext background;
                background.menuPart = "background";
                expect (! renderCentreWithContext (popup, background).isTransparent(),
                        "A popup must draw one outer surface.");

                SkinRenderContext normalItem;
                normalItem.menuPart = "item";
                normalItem.menuItemIndex = 0;
                expect (renderCentreWithContext (popup, normalItem).isTransparent(),
                        "A normal popup item must not redraw the outer card.");

                normalItem.state.hover = true;
                expect (! renderCentreWithContext (popup, normalItem).isTransparent(),
                        "A hovered popup item must draw a single highlight.");
            }
        }

        beginTest ("full pipeline preserves inherited exact styles and composite slot tables");
        if (foundation)
        {
            SkinValidationReport fixtureReport;
            const auto fixturePackage = completeFixture (foundationPackage, fixtureReport);
            auto parentWithExact = std::make_shared<CompiledSkin> (*foundation);
            parentWithExact->renderers.emplace (
                "textButton.parentExact", foundation->renderers.at ("textButton.default"));
            auto expressionStyle = foundation->renderers.at ("textButton.default");
            SkinValidationReport expressionReport;
            const auto compileExpression = [&] (const juce::String& source)
            {
                return SkinExpression::compile (
                    source,
                    skinRenderContextForTarget (SkinRenderTarget::textButton),
                    parentWithExact->tokens, expressionReport,
                    "parent-expression.json", "/test");
            };
            const auto baseExpression = compileExpression ("token.metrics.radius / 8");
            const auto variantExpression = compileExpression ("token.metrics.radius / 16");
            const auto fromExpression = compileExpression ("token.metrics.radius / 32");
            expect (baseExpression && variantExpression && fromExpression,
                    expressionReport.toText());
            if (baseExpression && variantExpression && fromExpression)
            {
                auto baseBinding = expressionStyle.renderer.baseLayers.front()
                                       .properties.at ("opacity");
                baseBinding.value = *baseExpression;
                expressionStyle.renderer.baseLayers.front().properties["opacity"] = baseBinding;
                auto& hover = *std::find_if (
                    expressionStyle.renderer.variants.begin(),
                    expressionStyle.renderer.variants.end(),
                    [] (const auto& patch) { return patch.name == "hover"; });
                auto variantBinding = baseBinding;
                variantBinding.value = *variantExpression;
                hover.properties["body.opacity"] = variantBinding;
                auto fromBinding = baseBinding;
                fromBinding.value = *fromExpression;
                expressionStyle.renderer.transitions[SkinAnimationTrigger::workspaceEnter] = {
                    100, 0, SkinAnimationEasing::linear, { "body.opacity" },
                    { { "body.opacity", fromBinding } }, true, {}
                };
                parentWithExact->renderers.emplace (
                    "textButton.parentExpression", std::move (expressionStyle));
            }
            const auto fixture = fixturePackage
                ? SkinCompiler::compile (*fixturePackage, parentWithExact, fixtureReport)
                : nullptr;
            expect (fixture != nullptr, fixtureReport.toText());
            if (fixture)
            {
                expectEquals (fixture->manifest.id,
                              juce::String ("com.example.integration"));
                const auto* token = fixture->tokens.find<juce::Colour> ("colors.fixture");
                expect (token != nullptr && token->getRed() == 0x33);
                expect (fixture->resolveStyle (SkinRenderTarget::textButton,
                                               "textButton.parentExact")
                        == &fixture->renderers.at ("textButton.parentExact"));
                const auto& inheritedButton = fixture->renderers.at (
                    "textButton.default").renderer;
                const auto* inheritedFill = std::get_if<SkinCompiledRenderValue> (
                    &inheritedButton.baseLayers.front().properties.at ("fill").value);
                const auto* inheritedColour = inheritedFill != nullptr
                    ? std::get_if<juce::Colour> (inheritedFill) : nullptr;
                expect (inheritedColour != nullptr
                        && inheritedColour->getRed() == 0x01
                        && inheritedColour->getGreen() == 0x02
                        && inheritedColour->getBlue() == 0x03,
                        "Child token overrides must rebind inherited renderer properties.");
                expectEquals (inheritedButton.transitions.at (
                                  SkinAnimationTrigger::hover).durationMs,
                              333);
                const auto& inheritedExpression = fixture->renderers.at (
                    "textButton.parentExpression").renderer;
                const auto evaluateNumber = [&] (const SkinRenderPropertyBinding& binding)
                {
                    const auto* expression = std::get_if<SkinExpression> (&binding.value);
                    const auto value = expression != nullptr
                        ? expression->evaluate (SkinExpressionContext {}) : std::nullopt;
                    return value && std::holds_alternative<double> (*value)
                        ? std::get<double> (*value) : -1.0;
                };
                expectWithinAbsoluteError (evaluateNumber (
                    inheritedExpression.baseLayers.front().properties.at ("opacity")),
                    2.0, 0.000001);
                const auto& inheritedHover = *std::find_if (
                    inheritedExpression.variants.begin(), inheritedExpression.variants.end(),
                    [] (const auto& patch) { return patch.name == "hover"; });
                expectWithinAbsoluteError (evaluateNumber (
                    inheritedHover.properties.at ("body.opacity")), 1.0, 0.000001);
                expectWithinAbsoluteError (evaluateNumber (
                    inheritedExpression.transitions.at (
                        SkinAnimationTrigger::workspaceEnter).from.at ("body.opacity")),
                    0.5, 0.000001);
                const auto& chain = fixture->renderers.at ("effectChain.custom");
                expectEquals (chain.slotStyles.at ("slot.bypass"),
                              juce::String ("textButton.custom"));
                expect (fixture->resolveSlotStyle (chain, "slot.bypass",
                                                   SkinRenderTarget::textButton)
                        == &fixture->renderers.at ("textButton.custom"));
                expect (fixture->renderers.at ("table.default").slotStyles.contains (
                    "cell.title"));
                const auto& inheritedAsset = fixture->renderers.at (
                    "artwork.assetProbe");
                const auto& localAsset = fixture->renderers.at ("artwork.localProbe");
                expect (inheritedAsset.assets == foundation->assets);
                expect (localAsset.assets == fixture->assets);
                expect (inheritedAsset.assets != localAsset.assets);
                if (inheritedAsset.assets != nullptr && localAsset.assets != nullptr)
                {
                    expect (inheritedAsset.assets->findSvg (
                        "assets/foundation-mark.svg") != nullptr);
                    expect (localAsset.assets->findSvg (
                        "assets/foundation-mark.svg") != nullptr);
                }
                expectEquals (static_cast<int> (fixture->layouts.size()), 4);
            }
        }

        beginTest ("composite contexts are exact to each owner and slot");
        if (foundation)
        {
            SkinValidationReport exactReport;
            const auto exactPackage = exactSlotContextFixture (exactReport);
            const auto exactSkin = exactPackage
                ? SkinCompiler::compile (*exactPackage, foundation, exactReport)
                : nullptr;
            expect (exactSkin != nullptr, exactReport.toText());

            SkinValidationReport contaminatedReport;
            const auto contaminatedPackage = contaminatedSlotContextFixture (
                contaminatedReport);
            expect (contaminatedPackage
                    && ! SkinCompiler::compile (*contaminatedPackage, foundation,
                                                contaminatedReport));
            expect (contaminatedReport.hasCode ("RENDER005"),
                    contaminatedReport.toText());
        }

        beginTest ("interactive composite slots reject no-op focused variants");
        if (foundation)
        {
            SkinValidationReport focusReport;
            const auto focusPackage = noOpFocusedSlotFixture (focusReport);
            expect (focusPackage
                    && ! SkinCompiler::compile (*focusPackage, foundation, focusReport));
            expect (focusReport.hasCode ("RENDER004"), focusReport.toText());
        }

        beginTest ("a focus expression must change between unfocused and focused states");
        if (foundation)
        {
            SkinValidationReport constantReport;
            const auto constantPackage = constantFocusLayerFixture (constantReport);
            expect (constantPackage
                    && ! SkinCompiler::compile (*constantPackage, foundation,
                                                constantReport));
            expect (constantReport.hasCode ("RENDER004"), constantReport.toText());
        }

        beginTest ("focus validation rejects changes that produce no visible pixels");
        {
            SkinValidationReport invisibleReport;
            const auto invisiblePackage = invisibleFocusLayerFixture (invisibleReport);
            expect (invisiblePackage
                    && ! SkinCompiler::compile (
                        *invisiblePackage, foundation, invisibleReport));
            expect (invisibleReport.hasCode ("RENDER004"),
                    invisibleReport.toText());
        }

        beginTest ("focus validation requires visible pixels at every probe size");
        {
            SkinValidationReport probeReport;
            const auto probePackage = singleProbeFocusFixture (probeReport);
            expect (probePackage
                    && ! SkinCompiler::compile (*probePackage, foundation, probeReport));
            expect (probeReport.hasCode ("RENDER004"), probeReport.toText());
        }

        beginTest ("omitted composite slots still validate their default interactive style");
        if (foundation)
        {
            auto safeParent = std::make_shared<CompiledSkin> (*foundation);
            routeTextButtonsThroughSafeExactStyle (*safeParent);
            SkinValidationReport omittedReport;
            const auto omittedPackage = omittedFocusedSlotFixture (omittedReport);
            expect (omittedPackage
                    && ! SkinCompiler::compile (*omittedPackage, safeParent, omittedReport));
            expect (omittedReport.hasCode ("RENDER004"), omittedReport.toText());
        }

        beginTest ("asset token provenance follows the package that declared the token");
        {
            SkinValidationReport assetFoundationReport;
            const auto assetFoundationPackage = foundationWithAssetToken (
                foundationPackage, assetFoundationReport);
            const auto assetFoundation = assetFoundationPackage
                ? SkinCompiler::compile (*assetFoundationPackage, {}, assetFoundationReport)
                : nullptr;
            expect (assetFoundation != nullptr, assetFoundationReport.toText());
            if (assetFoundation && assetFoundationPackage)
            {
                SkinValidationReport overrideReport;
                const auto overridePackage = completeFixture (
                    *assetFoundationPackage, overrideReport);
                const auto overridden = overridePackage
                    ? SkinCompiler::compile (*overridePackage, assetFoundation,
                                             overrideReport)
                    : nullptr;
                expect (overridden != nullptr, overrideReport.toText());
                if (overridden)
                {
                    const auto colour = renderCentre (
                        overridden->renderers.at ("artwork.assetProbe"));
                    expect (colour.getRed() == 0x33 && colour.getGreen() == 0x66
                            && colour.getBlue() == 0x99,
                            "Inherited renderer must use the child-owned asset token cache.");
                    const auto& inheritedLabel = overridden->renderers.at (
                        "label.default").renderer.baseLayers.front();
                    expect (inheritedLabel.properties.at ("font").assets
                            == overridden->assets,
                            "Inherited font override must bind to the child cache.");
                }

                SkinValidationReport localReport;
                const auto localPackage = localInheritedAssetFixture (localReport);
                const auto local = localPackage
                    ? SkinCompiler::compile (*localPackage, assetFoundation, localReport)
                    : nullptr;
                expect (local != nullptr, localReport.toText());
                if (local)
                {
                    const auto colour = renderCentre (
                        local->renderers.at ("artwork.localInherited"));
                    expect (colour.getRed() == 0xD6 && colour.getGreen() == 0x97
                            && colour.getBlue() == 0x62,
                            "Local renderer must reuse its inherited token's parent cache.");
                    const auto& localLabel = local->renderers.at (
                        "label.localInheritedFont").renderer.baseLayers.front();
                    expect (localLabel.properties.at ("font").assets
                            == assetFoundation->assets,
                            "Local inherited font token must retain the parent cache.");
                }
            }
        }

        beginTest ("production compile rejects every compatibility class before entrypoint reads");
        if (foundation)
        {
            const auto expectCompatibility = [&] (juce::String manifest, juce::String code)
            {
                SkinValidationReport report;
                const auto package = embedded (
                    "compat-" + code,
                    { { "manifest.json", bytes (manifest) } }, report);
                expect (package && ! SkinCompiler::compile (*package, foundation, report));
                expect (report.hasCode (code), report.toText());
                expect (! report.hasCode ("PKG008"), report.toText());
            };
            expectCompatibility (manifestFor ("com.example.future", {}, 2), "COMPAT001");
            expectCompatibility (manifestFor ("com.example.old", {}, 0), "COMPAT004");
            expectCompatibility (manifestFor ("com.example.new-app", {}, 1, "999.0.0"),
                                 "COMPAT002");
            expectCompatibility (manifestFor ("com.example.capability", {}, 1, "0.1.0",
                                              "\"scripts\""), "COMPAT003");
        }

        beginTest ("schema and token stages stop semantic render compilation in order");
        if (foundation)
        {
            SkinValidationReport schemaReport;
            const auto schemaPackage = embedded (
                "schema-order",
                {
                    { "manifest.json", bytes (manifestFor (
                        "com.example.schema-order", "\"tokens\":\"tokens.json\"")) },
                    { "tokens.json", bytes ("{\"unknownGroup\":{}}") }
                }, schemaReport);
            expect (schemaPackage
                    && ! SkinCompiler::compile (*schemaPackage, foundation, schemaReport));
            expect (schemaReport.hasCode ("SCHEMA001"), schemaReport.toText());
            expect (! schemaReport.hasCode ("TOKEN001"), schemaReport.toText());

            SkinValidationReport renderSchemaReport;
            const auto renderSchemaPackage = embedded (
                "render-schema-order",
                {
                    { "manifest.json", bytes (manifestFor (
                        "com.example.render-schema-order",
                        "\"tokens\":\"tokens.json\","
                        "\"components\":\"components/index.json\"",
                        1, "0.1.0", "\"tokens\",\"renderers\"")) },
                    { "tokens.json", bytes ("{}") },
                    { "components/index.json", bytes (
                        "{\"surface\":{\"broken\":\"components/broken.json\"}}") },
                    { "components/broken.json", bytes ("{}") }
                }, renderSchemaReport);
            expect (renderSchemaPackage
                    && ! SkinCompiler::compile (*renderSchemaPackage, foundation,
                                                renderSchemaReport));
            expect (renderSchemaReport.hasCode ("SCHEMA004"),
                    renderSchemaReport.toText());
            expect (! renderSchemaReport.hasCode ("RENDER004"),
                    renderSchemaReport.toText());

            SkinValidationReport tokenReport;
            const auto tokenPackage = embedded (
                "token-order",
                {
                    { "manifest.json", bytes (manifestFor (
                        "com.example.token-order",
                        "\"tokens\":\"tokens.json\","
                        "\"components\":\"components/index.json\"",
                        1, "0.1.0", "\"tokens\",\"renderers\"")) },
                    { "tokens.json", bytes (
                        "{\"colors\":{\"a\":{\"$ref\":\"colors.b\"},"
                        "\"b\":{\"$ref\":\"colors.a\"}}}") },
                    { "components/index.json", bytes (
                        "{\"surface\":{\"broken\":\"components/broken.json\"}}") },
                    { "components/broken.json", bytes (
                        "{\"apiVersion\":1,\"target\":\"surface\",\"layers\":[{"
                        "\"id\":\"body\",\"type\":\"roundedRect\","
                        "\"frame\":{\"anchor\":\"bounds\"},\"radius\":8,"
                        "\"fill\":{\"$ref\":\"colors.notThere\"}}]}") }
                }, tokenReport);
            expect (tokenPackage
                    && ! SkinCompiler::compile (*tokenPackage, foundation, tokenReport));
            expect (tokenReport.hasCode ("TOKEN004"), tokenReport.toText());
            expect (! tokenReport.hasCode ("RENDER006"), tokenReport.toText());
        }

        beginTest ("a schema-valid layout with missing required components never returns a skin");
        if (foundation)
        {
            SkinValidationReport layoutReport;
            const auto layoutPackage = embedded (
                "layout-order",
                {
                    { "manifest.json", bytes (manifestFor (
                        "com.example.layout-order",
                        "\"layouts\":{\"studio\":\"layouts/studio.json\"}",
                        1, "0.1.0", "\"layouts\"")) },
                    { "layouts/studio.json", bytes (
                        "{\"apiVersion\":1,\"root\":{\"id\":\"root\","
                        "\"type\":\"visual\",\"style\":\"surface.default\"}}") }
                }, layoutReport);
            expect (layoutPackage
                    && ! SkinCompiler::compile (*layoutPackage, foundation, layoutReport));
            expect (layoutReport.hasCode ("COMPONENT004"), layoutReport.toText());
        }
    }
};

SkinIntegrationTests skinIntegrationTests;
} // namespace
