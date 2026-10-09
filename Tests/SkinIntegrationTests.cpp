#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinLayoutEngine.h"
#include "skin/SkinRenderer.h"
#include "ui/JuceSkinAdapter.h"

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

juce::Colour renderPixelWithContext (const SkinRenderStyleEntry& style,
                                     SkinRenderContext context,
                                     int width, int height, int x, int y)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    context.bounds = image.getBounds().toFloat();
    context.contentBounds = context.bounds;
    context.state.enabled = true;
    SkinRenderer renderer (style.assets);
    SilentFaultSink sink;
    renderer.draw (graphics, style.renderer, context, true, sink);
    return image.getPixelAt (x, y);
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

const SkinRenderLayer* findLayer (
    const std::vector<SkinRenderLayer>& layers, juce::StringRef layerId)
{
    for (const auto& layer : layers)
    {
        if (layer.id == layerId) return &layer;
        if (const auto* child = findLayer (layer.children, layerId)) return child;
    }
    return nullptr;
}

std::optional<juce::Colour> colourProperty (
    const SkinRenderStyleEntry& style, juce::StringRef layerId,
    juce::StringRef property)
{
    const auto* layer = findLayer (style.renderer.baseLayers, layerId);
    if (layer == nullptr) return std::nullopt;
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
    const auto* layer = findLayer (style.renderer.baseLayers, layerId);
    if (layer == nullptr) return std::nullopt;
    const auto binding = layer->properties.find (juce::String (property));
    if (binding == layer->properties.end()) return std::nullopt;
    const auto resolved = binding->second.resolve ({}, {});
    if (const auto* number = std::get_if<double> (&resolved)) return *number;
    return std::nullopt;
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
            expect (! foundation->layouts.contains (SkinLayoutEntry::library));
            expect (! foundation->layouts.contains (SkinLayoutEntry::collection));
            expect (foundation->resolveStyle (SkinRenderTarget::playlistGrid, {}) == nullptr);
            for (const auto& targetName : skinRenderTargetNames())
            {
                const auto target = skinRenderTargetFromName (targetName);
                if (target == SkinRenderTarget::playlistGrid) continue;
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
                          juce::String ("listening-listening-root"));
        }

        beginTest ("playlist card surfaces own visible focus validation");
        if (defaultPackage && foundation)
            for (const auto cardOwnsFocus : { false, true })
            {
                SkinValidationReport report;
                std::vector<SkinPackage::EmbeddedFile> files;
                for (const auto& path : defaultPackage->listFiles())
                {
                    const auto gridRenderer = path.endsWith ("-collection-grid.json");
                    if (gridRenderer || (! cardOwnsFocus && path.endsWith ("-collection-surface.json")))
                    {
                        auto renderer = juce::JSON::parse (defaultPackage->readText (path, report));
                        if (gridRenderer)
                        {
                            renderer.getDynamicObject()->removeProperty ("variants");
                            if (! cardOwnsFocus)
                                renderer.getDynamicObject()->setProperty ("variants", juce::JSON::parse (
                                    R"json({"focused":{"background.fill":"#FFFFFFFF"}})json"));
                        }
                        else renderer.getProperty ("variants", {}).getDynamicObject()->removeProperty ("focused");
                        files.push_back ({ path, bytes (juce::JSON::toString (renderer, true)) });
                    }
                    else files.push_back ({ path, defaultPackage->readBinary (path, report) });
                }
                const auto package = embedded (cardOwnsFocus ? "card-focus-only" : "grid-focus-only",
                                               std::move (files), report);
                expect (package.has_value(), report.toText());
                if (! package) continue;
                const auto skin = SkinCompiler::compile (*package, foundation, report);
                if (cardOwnsFocus)
                    expect (skin != nullptr, "The grid itself cannot receive keyboard focus. " + report.toText());
                else
                {
                    expect (skin == nullptr, "A focused grid background cannot replace the card's focus feedback.");
                    expect (report.hasCode ("RENDER004"), report.toText());
                }
            }

        beginTest ("complete packages compile named ancillary styles and require default fallbacks");
        if (foundation)
        {
            SkinValidationReport report;
            auto index = juce::JSON::parse (foundationPackage.readText ("components/index.json", report));
            auto layout = juce::JSON::parse (foundationPackage.readText ("layouts/effects.json", report));
            auto manifest = juce::JSON::parse (foundationPackage.readText ("manifest.json", report));
            manifest.getDynamicObject()->setProperty ("id", "com.example.ancillary-variants");
            auto listeningStyles = juce::var (new juce::DynamicObject());
            auto studioStyles = juce::var (new juce::DynamicObject());
            for (const auto* target : { "popupMenu", "tooltip", "scrollBar" })
            {
                auto group = index.getProperty (target, {});
                group.getDynamicObject()->setProperty ("studio", group.getProperty ("default", {}));
                group.getDynamicObject()->setProperty ("listening", group.getProperty ("default", {}));
                listeningStyles.getDynamicObject()->setProperty (target, juce::String (target) + ".listening");
                studioStyles.getDynamicObject()->setProperty (target, juce::String (target) + ".studio");
            }
            layout.getDynamicObject()->setProperty ("ancillaryStyles", listeningStyles);
            auto studioRoot = layout.getProperty ("root", {}).clone();
            const auto suffixIds = [&] (const auto& self, juce::var node) -> void
            {
                if (! node.isObject()) return;
                node.getDynamicObject()->setProperty ("id", node.getProperty ("id", {}).toString() + "-studio");
                if (const auto* children = node.getProperty ("children", {}).getArray())
                    for (const auto& child : *children) self (self, child);
                self (self, node.getProperty ("child", {}));
            };
            suffixIds (suffixIds, studioRoot);
            auto condition = juce::var (new juce::DynamicObject());
            condition.getDynamicObject()->setProperty ("workspace", "studio");
            auto breakpoint = juce::var (new juce::DynamicObject());
            breakpoint.getDynamicObject()->setProperty ("when", condition);
            breakpoint.getDynamicObject()->setProperty ("root", studioRoot);
            breakpoint.getDynamicObject()->setProperty ("ancillaryStyles", studioStyles);
            layout.getDynamicObject()->setProperty ("breakpoints", juce::Array<juce::var> { breakpoint });
            std::vector<SkinPackage::EmbeddedFile> files;
            for (const auto& path : foundationPackage.listFiles())
            {
                if (path == "manifest.json") files.push_back ({ path, bytes (juce::JSON::toString (manifest, true)) });
                else if (path == "components/index.json") files.push_back ({ path, bytes (juce::JSON::toString (index, true)) });
                else if (path == "layouts/effects.json") files.push_back ({ path, bytes (juce::JSON::toString (layout, true)) });
                else files.push_back ({ path, foundationPackage.readBinary (path, report) });
            }
            const auto package = embedded ("ancillary-variants", files, report);
            const auto skin = package ? SkinCompiler::compile (*package, {}, report) : nullptr;
            expect (skin != nullptr, report.toText());
            if (skin)
                for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
                {
                    const auto resolved = SkinLayoutEngine::resolve (skin->layouts.at (SkinLayoutEntry::effects),
                        { 0, 0, 1240, 800 }, 1.0f, report, workspace);
                    expect (resolved.has_value(), report.toText());
                    if (! resolved) continue;
                    for (const auto target : { SkinRenderTarget::popupMenu, SkinRenderTarget::tooltip,
                                               SkinRenderTarget::scrollBar })
                    {
                        const auto name = skinRenderTargetName (target);
                        const auto requested = resolved->ancillaryStyles.at (name);
                        expectEquals (requested, name + "." + toString (workspace));
                        expect (skin->resolveStyle (target, requested) == &skin->renderers.at (requested));
                        expect (skin->resolveStyle (target, {}) == &skin->renderers.at (name + ".default"));
                    }
                }
            for (const auto* target : { "popupMenu", "tooltip", "scrollBar" })
            {
                auto missingDefault = index.clone();
                missingDefault.getProperty (target, {}).getDynamicObject()->removeProperty ("default");
                auto incompleteFiles = files;
                for (auto& file : incompleteFiles)
                    if (file.path == "components/index.json")
                        file.bytes = bytes (juce::JSON::toString (missingDefault, true));
                SkinValidationReport missingReport;
                const auto incomplete = embedded ("ancillary-without-default", std::move (incompleteFiles), missingReport);
                expect (incomplete && ! SkinCompiler::compile (*incomplete, {}, missingReport));
                expect (missingReport.hasCode ("RENDER004"), missingReport.toText());
            }
        }

        beginTest ("built-in typography retains semantic roles and renderer ownership");
        if (foundation && defaultSkin)
        {
            const auto* foundationBody = foundation->tokens.find<SkinFontValue> ("fonts.body");
            const auto* foundationTitle = foundation->tokens.find<SkinFontValue> ("fonts.title");
            expect (foundationBody != nullptr && foundationBody->weight == 400
                    && foundationBody->families == juce::StringArray ({ "Helvetica Neue", "Arial", "sans-serif" }));
            expect (foundationTitle != nullptr && foundationTitle->weight == 600
                    && foundationTitle->families == juce::StringArray ({ "Avenir Next", "Helvetica Neue", "sans-serif" }));
            const auto* body = defaultSkin->tokens.find<SkinFontValue> ("fonts.workspaceBody");
            const auto* medium = defaultSkin->tokens.find<SkinFontValue> ("fonts.workspaceMedium");
            const auto* title = defaultSkin->tokens.find<SkinFontValue> ("fonts.workspaceTitle");
            const auto* data = defaultSkin->tokens.find<SkinFontValue> ("fonts.workspaceData");
            expect (body && medium && title && data);
            if (body && medium && title && data)
            {
                expect (body->families.contains ("sans-serif") && medium->families.contains ("sans-serif")
                        && title->families.contains ("sans-serif"));
                expect (body->weight < medium->weight && body->weight < title->weight);
                expect (data->families.contains ("monospace"));
            }
            for (const auto& theme : { juce::String ("studio"), juce::String ("listening") })
            {
                const auto& table = defaultSkin->renderers.at ("table." + theme);
                for (const auto* slot : { "header.text", "cell.title", "cell.location", "cell.duration" })
                {
                    expect (table.slotStyles.contains (slot), theme + ":" + slot);
                    if (! table.slotStyles.contains (slot)) continue;
                    const auto style = table.slotStyles.at (slot);
                    expect (style.startsWith ("label." + theme), style);
                    expect (defaultSkin->resolveSlotStyle (table, slot, SkinRenderTarget::label)
                            == &defaultSkin->renderers.at (style), style);
                }
                const auto& metadata = defaultSkin->renderers.at (table.slotStyles.at ("cell.location"));
                for (const auto* context : { "row.artist", "row.album", "text.primary" })
                    expect (metadata.renderer.contextPaths.contains (context), theme + ":" + context);
                const auto titleSize = numberProperty (defaultSkin->renderers.at ("label." + theme + "Title"),
                                                      "label", "fontSize").value_or (0.0);
                const auto bodySize = numberProperty (defaultSkin->renderers.at ("label." + theme),
                                                     "label", "fontSize").value_or (0.0);
                expect (bodySize > 0.0 && titleSize > bodySize, theme + " must distinguish title and body roles");
                for (const auto* suffix : { "", "Data", "Table" })
                {
                    const auto style = "label." + theme + suffix;
                    expect (defaultSkin->renderers.at (style).renderer.contextPaths.contains ("text.align"), style);
                }
                const auto icon = "surface." + theme + "OnPrimaryIcon";
                expect (defaultSkin->renderers.at (icon).renderer.contextPaths.contains ("icon.path"));
                expectEquals (defaultSkin->renderers.at ("textButton." + theme + "IconPrimary")
                                  .slotStyles.at ("icon.play"), icon);
                const auto& skinList = defaultSkin->renderers.at ("skinList." + theme);
                for (const auto* slot : { "row.surface", "row.name", "row.author", "row.version", "row.status" })
                    expect (skinList.slotStyles.contains (slot), theme + ":" + slot);
            }
            const auto& meter = defaultSkin->renderers.at ("outputStage.studio").renderer;
            for (const auto* context : { "meter.mode", "meter.channelCount", "item.peakDb", "item.label", "meter.interrupted" })
                expect (meter.contextPaths.contains (context), context);
            const auto channels = std::find_if (meter.baseLayers.begin(), meter.baseLayers.end(),
                [] (const auto& layer) { return layer.repeatSource == "meter.channels"; });
            expect (channels != meter.baseLayers.end() && channels->repeatLimit >= 16,
                    "Studio meters must use the actual channel list through 9.1.6");
            expectEquals (foundation->manifest.version, juce::String ("1.0.0"));
            expectEquals (foundation->manifest.minAppVersion, juce::String ("0.6.1"));
            expectEquals (foundation->manifest.license, juce::String ("AGPL-3.0-only"));
            expectEquals (defaultSkin->manifest.version, juce::String ("1.0.0"));
            expectEquals (defaultSkin->manifest.minAppVersion, juce::String ("0.7.0"));
            expectEquals (defaultSkin->manifest.license, juce::String ("AGPL-3.0-only"));
        }

        beginTest ("Default player, library, collection and settings routes preserve theme, focus and content at both window sizes");
        if (defaultSkin)
        {
            const auto* studioBackground = defaultSkin->tokens.find<juce::Colour> ("colors.studio.background");
            const auto* listeningBackground = defaultSkin->tokens.find<juce::Colour> ("colors.listening.background");
            expect (studioBackground && listeningBackground && *studioBackground != *listeningBackground);
            if (studioBackground && listeningBackground)
                expect (studioBackground->getPerceivedBrightness() < listeningBackground->getPerceivedBrightness());
            for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
                for (const auto entry : { workspace == WorkspaceMode::studio
                                             ? SkinLayoutEntry::studio : SkinLayoutEntry::listening,
                                         SkinLayoutEntry::library, SkinLayoutEntry::collection,
                                         SkinLayoutEntry::effects, SkinLayoutEntry::settings })
                    for (const auto bounds : { juce::Rectangle<float> (0, 0, 1040, 700),
                                               juce::Rectangle<float> (0, 0, 1240, 800) })
                    {
                        SkinValidationReport report;
                        const auto resolved = SkinLayoutEngine::resolve (
                            defaultSkin->layouts.at (entry), bounds, 1.0f, report, workspace, defaultSkin.get(),
                            { entry == SkinLayoutEntry::studio || entry == SkinLayoutEntry::listening });
                        expect (resolved.has_value(), report.toText());
                        if (! resolved) continue;
                        expect (resolved->root.bounds == bounds, "routes must retain the window's outer bounds");
                        const auto theme = toString (workspace);
                        const auto* background = defaultSkin->resolveStyle (SkinRenderTarget::surface,
                                                                           resolved->root.style);
                        expect (background != nullptr);
                        if (background && studioBackground && listeningBackground)
                            expect (renderCentre (*background) == (workspace == WorkspaceMode::studio
                                ? *studioBackground : *listeningBackground));
                        for (const auto target : { SkinRenderTarget::popupMenu, SkinRenderTarget::tooltip,
                                                   SkinRenderTarget::scrollBar })
                        {
                            const auto name = skinRenderTargetName (target);
                            expect (resolved->ancillaryStyles.contains (name), theme + ":" + name);
                            if (! resolved->ancillaryStyles.contains (name)) continue;
                            const auto style = resolved->ancillaryStyles.at (name);
                            expectEquals (style, name + "." + theme);
                            expect (defaultSkin->resolveStyle (target, style) == &defaultSkin->renderers.at (style));
                        }
                        const auto checkStyleOwners = [&] (const auto& self, const SkinResolvedNode& node) -> void
                        {
                            if (node.componentId.isNotEmpty())
                            {
                                const auto* descriptor = PlayerComponentCatalog::find (node.componentId);
                                expect (descriptor != nullptr, node.componentId);
                                if (descriptor)
                                {
                                    auto targetName = descriptor->defaultRendererTarget;
                                    if (const auto found = descriptor->rendererTargetByPresentation.find (node.presentation);
                                        found != descriptor->rendererTargetByPresentation.end()) targetName = found->second;
                                    const auto target = skinRenderTargetFromName (targetName);
                                    const auto found = defaultSkin->renderers.find (node.style);
                                    expect (node.style.startsWith (targetName + "." + theme), node.componentId + ":" + node.style);
                                    expect (target && found != defaultSkin->renderers.end()
                                            && defaultSkin->resolveStyle (*target, node.style) == &found->second,
                                            node.componentId + " must use its explicit workspace renderer");
                                }
                            }
                            for (const auto& child : node.children) self (self, child);
                        };
                        checkStyleOwners (checkStyleOwners, resolved->root);
                        for (const auto& descriptor : PlayerComponentCatalog::descriptors())
                            if (descriptor.requiredIn.contains (entry))
                            {
                                const auto* component = resolved->component (descriptor.id);
                                for (const auto& alternative : descriptor.requiredAlternatives)
                                    if (component == nullptr || ! component->visible
                                        || component->opacity <= 0.0f || component->bounds.isEmpty())
                                        component = resolved->component (alternative);
                                expect (component != nullptr, descriptor.id);
                                if (! component) continue;
                                expect (component->visible && component->opacity > 0.0f
                                        && ! component->bounds.isEmpty(), descriptor.id);
                                if (descriptor.interactive) expect (component->focusOrder > 0, descriptor.id);
                            }
                        for (const auto* id : { "library.open",
                                                "player.open", "workspace.studio", "workspace.listening" })
                        {
                            const auto* navigation = resolved->component (id);
                            expect (navigation != nullptr && navigation->visible, id);
                            if (navigation != nullptr)
                            {
                                expectWithinAbsoluteError (navigation->bounds.getWidth(), 100.0f, 0.01f);
                                expectWithinAbsoluteError (navigation->bounds.getHeight(), 36.0f, 0.01f);
                            }
                        }
                        expect (resolved->component ("library.browseFavourites") == nullptr);
                        expect (resolved->component ("library.browseQueue") == nullptr);
                        const auto player = entry == SkinLayoutEntry::studio || entry == SkinLayoutEntry::listening;
                        if (player || entry == SkinLayoutEntry::collection)
                        {
                            const auto* tableId = player ? "queue.table" : "library.tracks";
                            const auto* queue = resolved->component (tableId);
                            expect (queue != nullptr, juce::String (tableId) + " must belong to this route");
                            if (queue)
                            {
                                expectEquals (queue->presentation, juce::String (
                                    ! player || workspace == WorkspaceMode::studio ? "table" : "list"));
                                expectEquals (queue->style, "table." + theme + (player ? "" : "Collection"));
                                expect (queue->visible && ! queue->bounds.isEmpty() && bounds.contains (queue->bounds),
                                        juce::String (tableId) + " must fit inside the window");
                            }
                            expect (resolved->component (player ? "library.tracks" : "queue.table") == nullptr,
                                    "Player queues and collection contents retain separate component identities");
                        }
                        if (player)
                        {
                            if (workspace == WorkspaceMode::listening)
                            {
                                const auto* artwork = resolved->component ("artwork.cover");
                                expect (artwork != nullptr && artwork->visible && ! artwork->bounds.isEmpty());
                            }
                            else
                                expect (resolved->component ("output.stage") != nullptr);
                        }
                        if (entry == SkinLayoutEntry::library)
                        {
                            const auto* grid = resolved->component ("library.grid");
                            expect (grid != nullptr, "The library overview must expose collection cards");
                            if (grid)
                            {
                                expectEquals (grid->style, "playlistGrid." + theme);
                                expect (grid->visible && ! grid->bounds.isEmpty() && bounds.contains (grid->bounds),
                                        "The collection grid must fit inside the window");
                            }
                            expect (resolved->component ("queue.table") == nullptr);
                            expect (resolved->component ("library.tracks") == nullptr);
                        }
                        if (entry == SkinLayoutEntry::collection)
                        {
                            for (const auto* id : { "library.detailCover", "library.title", "library.description",
                                                  "library.play", "library.shufflePlay", "library.actions" })
                            {
                                const auto* component = resolved->component (id);
                                expect (component != nullptr && component->visible && ! component->bounds.isEmpty()
                                        && bounds.contains (component->bounds), id);
                            }
                        }
                    }
        }

        beginTest ("built-in focus feedback never adds a control outline");
        for (const auto* skin : { foundation.get(), defaultSkin.get() })
            if (skin != nullptr)
                for (const auto& [name, style] : skin->renderers)
                {
                    expect (numberProperty (style, "focus", "stroke.width").value_or (0.0) == 0.0, name);
                    for (const auto& [property, binding] : style.renderer.variants[4].properties)
                    {
                        juce::ignoreUnused (binding);
                        expect (! property.startsWith ("body.stroke.")
                                && ! property.startsWith ("dial.stroke."), name + ": " + property);
                    }
                }

        beginTest ("effect drag handles stay inside their slot and remain hidden at rest");
        for (const auto& source : { std::pair { foundation.get(), juce::String ("effectChain.default") },
                                   std::pair { defaultSkin.get(), juce::String ("effectChain.studio") },
                                   std::pair { defaultSkin.get(), juce::String ("effectChain.listening") } })
        {
            if (source.first == nullptr) continue;
            const auto* owner = source.first->resolveStyle (SkinRenderTarget::effectChain, source.second);
            const auto* handle = owner != nullptr ? source.first->resolveSlotStyle (
                *owner, "slot.dragIndicator", SkinRenderTarget::surface) : nullptr;
            expect (handle != nullptr);
            if (handle == nullptr) continue;
            for (const auto hover : { false, true })
            {
                juce::Image image (juce::Image::ARGB, 80, 60, true);
                juce::Graphics graphics (image);
                SkinRenderContext context;
                context.bounds = context.contentBounds = { 20.0f, 18.0f, 4.0f, 16.0f };
                context.state.hover = hover;
                SkinRenderer renderer (handle->assets);
                SilentFaultSink sink;
                renderer.draw (graphics, handle->renderer, context, true, sink);
                juce::Rectangle<int> painted;
                for (int y = 0; y < image.getHeight(); ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (! image.getPixelAt (x, y).isTransparent()) painted = painted.getUnion ({ x, y, 1, 1 });
                expect (hover ? ! painted.isEmpty() && context.bounds.toNearestInt().contains (painted)
                              : painted.isEmpty(), source.second);
            }
        }

        beginTest ("scrollbar skins draw the native thumb instead of the full track");
        if (foundation && defaultSkin)
            for (const auto& source : { std::pair { foundation.get(), juce::String ("scrollBar.default") },
                                       std::pair { defaultSkin.get(), juce::String ("scrollBar.studio") },
                                       std::pair { defaultSkin.get(), juce::String ("scrollBar.listening") } })
            {
                SkinRenderer renderer;
                JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding { source.first->resolveStyle (target, source.second) };
                }, renderer);
                for (const auto vertical : { true, false })
                {
                    juce::ScrollBar bar (vertical);
                    const auto render = [&] (int start, int size)
                    {
                        juce::Image image (juce::Image::ARGB, vertical ? 12 : 220, vertical ? 220 : 12, true);
                        juce::Graphics graphics (image);
                        adapter.drawScrollbar (graphics, bar, vertical ? 0 : 10, vertical ? 10 : 0,
                            vertical ? 12 : 200, vertical ? 200 : 12, vertical, start, size, false, false);
                        return image;
                    };
                    const auto first = render (20, 30), middle = render (90, 30), last = render (170, 40);
                    const auto pixel = [vertical] (const juce::Image& image, int position)
                    { return image.getPixelAt (vertical ? 6 : position, vertical ? position : 6); };
                    const auto track = pixel (first, 130);
                    expect (pixel (first, 35) != track, source.second + " first thumb is distinct from its track");
                    expect (pixel (first, 105) == track);
                    expect (pixel (middle, 105) != track, source.second + " thumb follows native scroll position");
                    expect (pixel (middle, 35) == track);
                    expect (pixel (last, 190) != track);
                    expect (pixel (last, 105) == track);
                    const auto empty = render (20, 0);
                    expect (pixel (empty, 35) == track, "A zero-sized native thumb remains hidden.");
                }
            }

        beginTest ("workspace navigation and transport anchors stay fixed across Default routes");
        if (defaultSkin)
            for (const auto bounds : { juce::Rectangle<float> (0, 0, 1040, 700),
                                       juce::Rectangle<float> (0, 0, 1240, 800) })
            {
                std::map<juce::String, juce::Rectangle<float>> navigation, transport;
                std::optional<juce::Rectangle<float>> leftCard, rightCard;
                for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
                    for (const auto entry : { workspace == WorkspaceMode::studio
                                                 ? SkinLayoutEntry::studio : SkinLayoutEntry::listening,
                                             SkinLayoutEntry::effects, SkinLayoutEntry::settings,
                                             SkinLayoutEntry::library, SkinLayoutEntry::collection })
                    {
                        SkinValidationReport report;
                        const auto layout = SkinLayoutEngine::resolve (
                            defaultSkin->layouts.at (entry), bounds, 1.0f, report, workspace, defaultSkin.get(), { true });
                        expect (layout.has_value(), report.toText());
                        if (! layout) continue;
                        const auto checkAnchors = [&] (auto& previous, const auto& ids)
                        {
                            for (const auto* id : ids)
                            {
                                const auto* node = layout->component (id);
                                expect (node != nullptr, id);
                                if (! node) continue;
                                if (previous.contains (id))
                                    expect (node->bounds == previous.at (id), juce::String (id) + " moved during navigation");
                                else previous.emplace (id, node->bounds);
                            }
                        };
                        checkAnchors (navigation, std::array { "app.brand.title", "workspace.studio", "workspace.listening",
                                                               "settings.open" });
                        if (entry == SkinLayoutEntry::studio || entry == SkinLayoutEntry::listening
                            || entry == SkinLayoutEntry::library || entry == SkinLayoutEntry::collection)
                        {
                            checkAnchors (transport, std::array { "transport.previous", "transport.playPause", "transport.next",
                                                                 "transport.seek", "transport.time", "transport.volume",
                                                                 "transport.volumeLabel", "transport.volumeValue",
                                                                 "transport.shuffle", "transport.repeatCycle" });
                            if (entry == SkinLayoutEntry::library || entry == SkinLayoutEntry::collection) continue;
                            const auto& cards = layout->root.children.at (0).children.at (1).children;
                            expectEquals (static_cast<int> (cards.size()), 2);
                            if (cards.size() == 2)
                            {
                                if (leftCard && rightCard)
                                    expect (*leftCard == cards[0].bounds && *rightCard == cards[1].bounds,
                                            "Card boundaries must remain fixed across A/B");
                                leftCard = cards[0].bounds;
                                rightCard = cards[1].bounds;
                                expectWithinAbsoluteError (leftCard->getY(), rightCard->getY(), 0.01f);
                                expectWithinAbsoluteError (leftCard->getBottom(), rightCard->getBottom(), 0.01f);
                            }
                        }
                        if (entry == SkinLayoutEntry::settings)
                        {
                            const auto* library = layout->component ("settings.skins.library");
                            expect (library != nullptr && library->bounds.getHeight() >= 112.0f,
                                    "The two built-in skin rows must fit without shrinking their list");
                        }
                    }
            }

        beginTest ("queue row metadata renders inside its own non-zero cell bounds");
        if (defaultSkin)
        {
            struct RowFaultSink final : SkinRuntimeFaultSink
            {
                void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override { failed = true; }
                bool failed = false;
            } sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
            {
                return JuceSkinAdapter::RuntimeBinding { defaultSkin->resolveStyle (target, style), nullptr, &sink, true };
            }, renderer);
            juce::ListBox list;
            const auto renderRow = [&] (const juce::String& theme, const juce::String& artist,
                                       const juce::String& album, bool compact)
            {
                list.getProperties().set (JuceSkinAdapter::styleProperty, "table." + theme);
                juce::Image image (juce::Image::ARGB, 460, 84, true);
                juce::Graphics graphics (image);
                const JuceSkinAdapter::ListBoxRowGeometry geometry {
                    { 12.0f, 10.0f, compact ? 170.0f : 350.0f, compact ? 40.0f : 56.0f },
                    compact ? juce::Rectangle<float> (188, 10, 192, 40) : juce::Rectangle<float>(),
                    { 386.0f, 10.0f, 62.0f, compact ? 40.0f : 56.0f }
                };
                adapter.drawListBoxRow (graphics, list, 0, 460, 84, false, "Track title",
                    "/Music/Album/Track.flac", "3:12", geometry, false, false, true, artist, album);
                return image;
            };
            const auto expectChangesOnlyWithin = [&] (const juce::Image& before, const juce::Image& after,
                                                      juce::Rectangle<int> expected)
            {
                int changed = 0;
                bool inside = true;
                for (int y = 0; y < before.getHeight(); ++y)
                    for (int x = 0; x < before.getWidth(); ++x)
                        if (before.getPixelAt (x, y) != after.getPixelAt (x, y))
                        {
                            ++changed;
                            inside = inside && expected.contains (x, y);
                        }
                expect (changed > 0, "row.artist and row.album must reach the resolved label renderer");
                expect (inside, "metadata must not draw in the title or duration column");
            };
            const auto& listTitle = defaultSkin->renderers.at (
                defaultSkin->renderers.at ("table.listening").slotStyles.at ("cell.title"));
            expect (listTitle.renderer.contextPaths.contains ("row.title"));
            expect (listTitle.renderer.contextPaths.contains ("row.artist"));
            expectChangesOnlyWithin (renderRow ("listening", "", "", false),
                                     renderRow ("listening", "Artist name", "", false),
                                     { 12, 38, 350, 28 });
            expectChangesOnlyWithin (renderRow ("studio", "First artist", "First album", true),
                                     renderRow ("studio", "Second artist", "Second album", true),
                                     { 188, 10, 192, 40 });
            expect (! sink.failed, "compiled Default row styles must render without runtime faults");
        }

        beginTest ("built-in routes keep editing actions with their content and retain Settings navigation");
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
                    {
                        const auto anchor = skin == defaultSkin.get() ? "settings.open" : "queue.title";
                        expect (parents.contains (anchor) && parents.at ("library.add") == parents.at (anchor),
                                "Add Music stays accessible when the Default queue is collapsed.");
                    }
                    expect (parents.contains ("settings.open"));
                    expect (! parents.contains ("effects.open"));
                    if (entry == SkinLayoutEntry::studio)
                        expect (parents.contains ("effects.chain") && parents.contains ("output.stage"),
                                "Studio exposes the effect chain beside output monitoring.");
                    if (skin == foundation.get())
                        expectWithinAbsoluteError (
                            skin->layouts.at (entry).base.common.padding.left.value,
                            0.0f, 0.001f);
                }

                if (skin == defaultSkin.get())
                {
                    const auto overview = componentParents (skin->layouts.at (SkinLayoutEntry::library));
                    const auto detail = componentParents (skin->layouts.at (SkinLayoutEntry::collection));
                    for (const auto* id : { "library.newPlaylist", "library.actions" })
                        expect (overview.contains ("library.title") && overview.contains (id)
                                && overview.at ("library.title") == overview.at (id),
                                juce::String (id) + " must share the overview heading");
                    for (const auto* id : { "library.shufflePlay", "library.actions" })
                        expect (detail.contains ("library.play") && detail.contains (id)
                                && detail.at ("library.play") == detail.at (id),
                                juce::String (id) + " must share the collection action row");
                    for (const auto* route : { &overview, &detail })
                        expect (route->contains ("library.add") && route->contains ("settings.open")
                                && route->at ("library.add") == route->at ("settings.open"),
                                "Add Music must remain in the shared navigation row.");
                    expect (detail.contains ("library.title") && detail.contains ("library.collections")
                            && detail.at ("library.title") == detail.at ("library.collections"),
                            "The collection name editor must occupy its title heading");
                }

                const auto settings = componentParents (
                    skin->layouts.at (SkinLayoutEntry::settings));
                for (const auto& id : {
                         "settings.title",
                         "settings.audio.device", "settings.audio.sampleRate",
                         "settings.audio.bufferSize", "settings.audio.channelLayout",
                         "settings.audio.routing", "settings.audio.deviceConfiguration", "settings.plugins.formats",
                         "settings.plugins.knownList", "settings.plugins.scanAll",
                         "settings.plugins.scanFolder", "settings.plugins.cancel",
                         "settings.plugins.progress", "settings.skins.library",
                         "settings.skins.import", "settings.skins.remove",
                         "settings.skins.preview", "settings.skins.apply",
                         "settings.skins.cancelPreview", "settings.skins.reload" })
                    expect (settings.contains (id), id);
                expect (settings.contains ("library.add") == (skin == defaultSkin.get()));
                expect (! settings.contains ("settings.close"));
                if (skin == defaultSkin.get())
                {
                    expect (settings.contains ("library.open"));
                    expect (settings.contains ("player.open"));
                }
                for (const auto& [id, parent] : settings)
                {
                    juce::ignoreUnused (parent);
                    expect (id == "settings.title" || id == "settings.close"
                            || (skin == defaultSkin.get()
                                && (id == "app.brand.title" || id == "app.status"
                                    || id == "workspace.studio" || id == "workspace.listening"
                                    || id == "library.open" || id == "player.open"
                                    || id == "library.add"
                                    || id == "effects.open" || id == "settings.open"))
                            || id.startsWith ("settings.audio.")
                            || id.startsWith ("settings.plugins.")
                            || id.startsWith ("settings.skins."), id);
                }

                const auto effects = componentParents (
                    skin->layouts.at (SkinLayoutEntry::effects));
                for (const auto& required : {
                         "effects.filter", "effects.catalog",
                         "effects.add", "effects.chain" })
                    expect (effects.contains (required), required);
                expect (! effects.contains ("effects.close"));
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

            expect (renderCentre (defaultSkin->renderers.at ("surface.listeningListRow"))
                    != renderCentre (defaultSkin->renderers.at ("surface.listeningListRow"), true),
                    "Listening skin rows must render a visible selected surface.");
            expect (renderCentre (foundation->renderers.at ("surface.default"))
                    != renderCentre (foundation->renderers.at ("surface.default"), true),
                    "Foundation skin rows must render a visible selected surface.");

            for (const auto& probe : {
                     std::tuple { "comboBox.listening", "body", "fill",
                                  "colors.listening.control" },
                     std::tuple { "textEditor.listening", "body", "fill",
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

            SkinRenderContext focusedTable;
            focusedTable.state.focused = true;
            for (const auto& [skin, style] : {
                     std::pair { defaultSkin.get(), "table.studio" },
                     std::pair { defaultSkin.get(), "table.listening" },
                     std::pair { foundation.get(), "table.default" } })
                expect (renderPixelWithContext (
                            skin->renderers.at (style), focusedTable,
                            160, 36, 4, 18).getAlpha() <= 0x10,
                        juce::String (style)
                            + " must not draw a nested or fragmented focus card.");

            SkinRenderContext selectedRow;
            selectedRow.state.focused = true;
            selectedRow.state.selected = true;
            for (const auto& [skin, style] : {
                     std::pair { defaultSkin.get(), "surface.studioListRow" },
                     std::pair { defaultSkin.get(), "surface.listeningListRow" },
                     std::pair { defaultSkin.get(), "surface.listeningSkinCard" },
                     std::pair { foundation.get(), "surface.listRow" },
                     std::pair { foundation.get(), "surface.skinRow" } })
                expect (! renderPixelWithContext (
                             skin->renderers.at (style), selectedRow,
                             160, 36, 2, 18).isTransparent(),
                        juce::String (style)
                            + " must use one flat full-width selection surface.");

            const auto artworkPixel = [&] (const juce::String& iconName, bool hasImage = false)
            {
                SkinRenderContext artwork;
                artwork.iconName = iconName;
                artwork.extraLookup = [hasTrack = iconName == "track", hasImage] (
                    std::string_view path)
                    -> std::optional<SkinExpressionValue>
                {
                    if (path == "artwork.hasTrack")
                        return SkinExpressionValue (hasTrack);
                    if (path == "artwork.hasImage")
                        return SkinExpressionValue (hasImage);
                    if (path == "artwork.aspectRatio")
                        return SkinExpressionValue (1.0);
                    return std::nullopt;
                };
                return renderPixelWithContext (
                    defaultSkin->renderers.at ("artwork.listening"), artwork,
                    160, 160, 80, 50);
            };
            expect (! artworkPixel ("empty").isTransparent(), "empty artwork must have a skin-owned placeholder");
            expect (artworkPixel ("track") == artworkPixel ("empty"),
                    "tracks without artwork use the same neutral placeholder");
            expect (artworkPixel ("track") != artworkPixel ("track", true),
                    "the placeholder must disappear when actual artwork is available");
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
        {
            const auto routingContext = skinRenderContextForCompositeSlot (
                SkinRenderTarget::channelMap, "row.output", SkinRenderTarget::comboBox);
            expect (routingContext.contains ("combo.popupOpen"));
            expect (! routingContext.contains ("channel.index"));
        }
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
