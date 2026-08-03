#include "skin/SkinRenderCompiler.h"

#include "skin/SkinSchemaValidator.h"

#include <juce_core/juce_core.h>

namespace
{
juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

juce::var array (std::vector<juce::var> values)
{
    juce::Array<juce::var> result;
    for (auto& value : values) result.add (std::move (value));
    return juce::var (std::move (result));
}

juce::var binding (const char* key, const juce::String& value)
{
    auto result = object();
    result.getDynamicObject()->setProperty (key, value);
    return result;
}

juce::var explicitFrame (juce::var width = 100, juce::var height = 40)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("x", 0);
    result.getDynamicObject()->setProperty ("y", 0);
    result.getDynamicObject()->setProperty ("width", std::move (width));
    result.getDynamicObject()->setProperty ("height", std::move (height));
    return result;
}

juce::var anchoredFrame()
{
    auto result = object();
    result.getDynamicObject()->setProperty ("anchor", "bounds");
    result.getDynamicObject()->setProperty ("inset", 1);
    return result;
}

juce::var baseLayer (const juce::String& id, const juce::String& type)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("id", id);
    result.getDynamicObject()->setProperty ("type", type);
    return result;
}

juce::var rectLayer (const juce::String& id)
{
    auto result = baseLayer (id, "rect");
    result.getDynamicObject()->setProperty ("frame", anchoredFrame());
    result.getDynamicObject()->setProperty ("fill", "#112233FF");
    return result;
}

juce::var renderDocument (const juce::String& target, std::vector<juce::var> layers)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("apiVersion", 1);
    result.getDynamicObject()->setProperty ("target", target);
    result.getDynamicObject()->setProperty ("layers", array (std::move (layers)));
    return result;
}

SkinTokenStore makeTokens()
{
    auto tokens = object();
    auto colors = object();
    colors.getDynamicObject()->setProperty ("accent", "#D69762FF");
    tokens.getDynamicObject()->setProperty ("colors", colors);
    auto metrics = object();
    metrics.getDynamicObject()->setProperty ("radius", 8.0);
    tokens.getDynamicObject()->setProperty ("metrics", metrics);
    auto durations = object();
    durations.getDynamicObject()->setProperty ("fast", 120);
    tokens.getDynamicObject()->setProperty ("durations", durations);
    auto fonts = object();
    auto body = object();
    body.getDynamicObject()->setProperty ("families", array ({ "Arial", "sans-serif" }));
    body.getDynamicObject()->setProperty ("weight", 400);
    fonts.getDynamicObject()->setProperty ("body", body);
    tokens.getDynamicObject()->setProperty ("fonts", fonts);
    auto gradients = object();
    auto gradient = object();
    gradient.getDynamicObject()->setProperty ("type", "linear");
    gradient.getDynamicObject()->setProperty ("angle", 0);
    auto first = object();
    first.getDynamicObject()->setProperty ("position", 0.0);
    first.getDynamicObject()->setProperty ("color", "#000000FF");
    auto second = object();
    second.getDynamicObject()->setProperty ("position", 1.0);
    second.getDynamicObject()->setProperty ("color", "#FFFFFFFF");
    gradient.getDynamicObject()->setProperty ("stops", array ({ first, second }));
    gradients.getDynamicObject()->setProperty ("panel", gradient);
    tokens.getDynamicObject()->setProperty ("gradients", gradients);
    auto strings = object();
    strings.getDynamicObject()->setProperty ("unsafeAsset", "../../outside.png");
    strings.getDynamicObject()->setProperty ("glyphPath", "M 0 0 L 2 2 Z");
    tokens.getDynamicObject()->setProperty ("strings", strings);

    SkinValidationReport report;
    const auto validated = SkinSchemaValidator::validate (
        SkinSchemaDocument::tokens, tokens, "tokens.json", report);
    jassert (validated.has_value());
    const auto compiled = SkinTokenStore::compile (*validated, nullptr, report);
    jassert (compiled.has_value());
    return *compiled;
}

std::optional<SkinRenderProgram> compile (
    const juce::var& raw, const SkinTokenStore& tokens, SkinValidationReport& report)
{
    const auto validated = SkinSchemaValidator::validate (
        SkinSchemaDocument::render, raw, "renderer.json", report);
    if (! validated) return std::nullopt;
    return SkinRenderCompiler::compileProgram (*validated, tokens, report);
}

juce::MemoryBlock bytes (const juce::String& value)
{
    return juce::MemoryBlock (value.toRawUTF8(), value.getNumBytesAsUTF8());
}

class SkinRenderCompilerTests final : public juce::UnitTest
{
public:
    SkinRenderCompilerTests() : UnitTest ("SkinRenderCompiler", "skin") {}

    void runTest() override
    {
        const auto tokens = makeTokens();

        beginTest ("publishes generated renderer schemas and the frozen default table");
        {
            const auto renderSchema = SkinSchemaRegistry::jsonSchema (SkinSchemaDocument::render);
            const auto indexSchema = SkinSchemaRegistry::jsonSchema (
                SkinSchemaDocument::componentIndex);
            expectEquals (renderSchema.getProperty ("$id", {}).toString(),
                          juce::String ("urn:uuid:f0c2d818-4a3f-4d3e-9c4a-c5142029e7ef"));
            expectEquals (indexSchema.getProperty ("$id", {}).toString(),
                          juce::String ("urn:uuid:f37db386-b854-462a-a26f-b281c16b38fb"));
            expectEquals (renderSchema.getProperty ("x-generatedBy", {}).toString(),
                          juce::String ("Moonwake SkinSchemaRegistry"));
            expectEquals (static_cast<int> (skinRenderTargetNames().size()), 19);
            expectEquals (static_cast<int> (skinPrimitiveNames().size()), 11);
            expectEquals (static_cast<int> (skinRepeatSourceDescriptors().size()), 3);

            const auto* opacity = skinRenderPropertyDescriptor (
                SkinPrimitive::rectangle, "opacity");
            const auto* pivotX = skinRenderPropertyDescriptor (
                SkinPrimitive::rectangle, "transform.pivotX");
            const auto* tint = skinRenderPropertyDescriptor (
                SkinPrimitive::image, "tint");
            const auto* overflow = skinRenderPropertyDescriptor (
                SkinPrimitive::text, "overflow");
            expect (opacity != nullptr && std::get<double> (opacity->fallback.literal) == 1.0);
            expect (pivotX != nullptr
                    && pivotX->fallback.strategy
                        == SkinRenderPropertyFallback::Strategy::layerFrameCentreX);
            expect (tint != nullptr
                    && std::get<juce::Colour> (tint->fallback.literal)
                        == juce::Colour::fromRGBA (255, 255, 255, 255));
            expect (overflow != nullptr
                    && std::get<juce::String> (overflow->fallback.literal) == "clip"
                    && ! overflow->expressionAllowed);
        }

        beginTest ("publishes semantic text alignment in label render context");
        {
            const auto labelContext = skinRenderContextForTarget (SkinRenderTarget::label);
            expect (labelContext.contains ("text.align"));

            auto text = baseLayer ("label", "text");
            text.getDynamicObject()->setProperty ("frame", anchoredFrame());
            text.getDynamicObject()->setProperty ("text", "Probe");
            text.getDynamicObject()->setProperty ("font", binding ("$ref", "fonts.body"));
            text.getDynamicObject()->setProperty ("fontSize", 13);
            text.getDynamicObject()->setProperty ("color", binding ("$ref", "colors.accent"));
            text.getDynamicObject()->setProperty (
                "align", binding ("$context", "text.align"));
            SkinValidationReport alignmentReport;
            expect (compile (renderDocument ("label", { text }), tokens,
                             alignmentReport).has_value(),
                    alignmentReport.toText());
        }

        beginTest ("compiles every primitive with immutable typed properties");
        {
            std::vector<juce::var> layers;
            auto rect = rectLayer ("rect");
            auto stroke = object();
            stroke.getDynamicObject()->setProperty ("color", "#FFFFFFFF");
            stroke.getDynamicObject()->setProperty ("width", 1.5);
            stroke.getDynamicObject()->setProperty ("placement", "inside");
            rect.getDynamicObject()->setProperty ("stroke", stroke);
            auto shadow = object();
            shadow.getDynamicObject()->setProperty ("color", "#00000040");
            shadow.getDynamicObject()->setProperty ("radius", 8);
            shadow.getDynamicObject()->setProperty ("offsetX", 0);
            shadow.getDynamicObject()->setProperty ("offsetY", 4);
            rect.getDynamicObject()->setProperty ("shadow", shadow);
            layers.push_back (rect);

            auto rounded = baseLayer ("rounded", "roundedRect");
            rounded.getDynamicObject()->setProperty ("frame", explicitFrame());
            rounded.getDynamicObject()->setProperty ("radius", binding ("$ref", "metrics.radius"));
            rounded.getDynamicObject()->setProperty ("fill", binding ("$ref", "gradients.panel"));
            layers.push_back (rounded);

            auto ellipse = baseLayer ("ellipse", "ellipse");
            ellipse.getDynamicObject()->setProperty ("frame", explicitFrame());
            layers.push_back (ellipse);

            auto line = baseLayer ("line", "line");
            auto start = object();
            start.getDynamicObject()->setProperty ("x", 0);
            start.getDynamicObject()->setProperty ("y", 0);
            auto end = object();
            end.getDynamicObject()->setProperty ("x", 100);
            end.getDynamicObject()->setProperty ("y", 40);
            line.getDynamicObject()->setProperty ("start", start);
            line.getDynamicObject()->setProperty ("end", end);
            line.getDynamicObject()->setProperty ("color", "#FFFFFFFF");
            line.getDynamicObject()->setProperty ("width", 2);
            line.getDynamicObject()->setProperty ("cap", "round");
            layers.push_back (line);

            auto arc = baseLayer ("arc", "arc");
            auto centre = object();
            centre.getDynamicObject()->setProperty ("x", 50);
            centre.getDynamicObject()->setProperty ("y", 50);
            arc.getDynamicObject()->setProperty ("center", centre);
            arc.getDynamicObject()->setProperty ("radiusX", 30);
            arc.getDynamicObject()->setProperty ("radiusY", 30);
            arc.getDynamicObject()->setProperty ("startAngle", 0);
            arc.getDynamicObject()->setProperty ("endAngle", 270);
            arc.getDynamicObject()->setProperty ("stroke", stroke.clone());
            layers.push_back (arc);

            auto path = baseLayer ("path", "path");
            path.getDynamicObject()->setProperty ("data", "M 0 0 1e1 0 10 10 Z");
            path.getDynamicObject()->setProperty ("fill", "#FFFFFFFF");
            layers.push_back (path);

            auto text = baseLayer ("text", "text");
            text.getDynamicObject()->setProperty ("frame", anchoredFrame());
            text.getDynamicObject()->setProperty ("text", binding ("$context", "text.primary"));
            text.getDynamicObject()->setProperty ("font", binding ("$ref", "fonts.body"));
            text.getDynamicObject()->setProperty ("fontSize", 13);
            text.getDynamicObject()->setProperty ("color", binding ("$ref", "colors.accent"));
            text.getDynamicObject()->setProperty ("align", "center");
            layers.push_back (text);

            for (const auto* type : { "image", "svg" })
            {
                auto asset = baseLayer (type, type);
                asset.getDynamicObject()->setProperty ("frame", anchoredFrame());
                asset.getDynamicObject()->setProperty ("source",
                                                       "assets/" + juce::String (type) + ".png");
                asset.getDynamicObject()->setProperty ("fit", "cover");
                asset.getDynamicObject()->setProperty ("tint", "#FFFFFFFF");
                layers.push_back (asset);
            }

            auto group = baseLayer ("group", "group");
            group.getDynamicObject()->setProperty ("children", array ({ rectLayer ("group-child") }));
            layers.push_back (group);

            SkinValidationReport report;
            const auto result = compile (renderDocument ("label", std::move (layers)), tokens, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expectEquals (static_cast<int> (result->baseLayers.size()), 10);
                expect (result->baseLayers[1].properties.contains ("fill"));
                expect (std::holds_alternative<SkinGradientValue> (
                    std::get<SkinCompiledRenderValue> (
                        result->baseLayers[1].properties.at ("fill").value)));
                expect (result->baseLayers[5].compiledPath != nullptr);
                expectEquals (result->pathCommandCount, 4);
            }
        }

        beginTest ("uses fixed variant order, validates transitions and resolves fallbacks");
        {
            auto layer = rectLayer ("body.part");
            auto transform = object();
            transform.getDynamicObject()->setProperty (
                "translateX", binding ("$expr", "bounds.width * 1e308"));
            transform.getDynamicObject()->setProperty ("pivotX", 4);
            layer.getDynamicObject()->setProperty ("transform", transform);
            layer.getDynamicObject()->setProperty (
                "visible", binding ("$expr", "bounds.width * 1e308 > 0"));
            layer.getDynamicObject()->setProperty (
                "fill", binding ("$expr", "alpha(color('#FFFFFFFF'), bounds.width * 1e308)"));
            auto raw = renderDocument ("surface", { layer });

            auto selected = object();
            selected.getDynamicObject()->setProperty ("body.part.opacity", 0.9);
            auto dragging = object();
            dragging.getDynamicObject()->setProperty ("body.part.transform.translateY", 2);
            auto disabled = object();
            disabled.getDynamicObject()->setProperty ("body.part.opacity", 0.4);
            auto variants = object();
            variants.getDynamicObject()->setProperty ("selected", selected);
            variants.getDynamicObject()->setProperty ("dragging", dragging);
            variants.getDynamicObject()->setProperty ("disabled", disabled);
            raw.getDynamicObject()->setProperty ("variants", variants);

            auto transition = object();
            transition.getDynamicObject()->setProperty ("durationMs", binding ("$ref", "durations.fast"));
            transition.getDynamicObject()->setProperty ("easing", "easeOut");
            transition.getDynamicObject()->setProperty (
                "properties", array ({ "body.part.opacity" }));
            auto entry = object();
            entry.getDynamicObject()->setProperty ("durationMs", 180);
            entry.getDynamicObject()->setProperty ("easing", "easeOut");
            entry.getDynamicObject()->setProperty (
                "properties", array ({ "body.part.opacity" }));
            auto from = object();
            from.getDynamicObject()->setProperty ("body.part.opacity", 0.0);
            entry.getDynamicObject()->setProperty ("from", from);
            entry.getDynamicObject()->setProperty ("respectReduceMotion", true);
            auto transitions = object();
            transitions.getDynamicObject()->setProperty ("hover", transition);
            transitions.getDynamicObject()->setProperty ("workspaceEnter", entry);
            raw.getDynamicObject()->setProperty ("transitions", transitions);

            SkinValidationReport report;
            const auto result = compile (raw, tokens, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expectEquals (result->variants[0].name, juce::String ("selected"));
                expectEquals (result->variants[3].name, juce::String ("dragging"));
                expectEquals (result->variants[5].name, juce::String ("disabled"));
                expectEquals (result->transitions.at (SkinAnimationTrigger::hover).durationMs, 120);

                SkinExpressionContext context;
                context.lookup = [] (std::string_view path) -> std::optional<SkinExpressionValue>
                {
                    if (path == "bounds.width") return 1.0e308;
                    return std::nullopt;
                };
                SkinValidationReport runtime;
                const auto& compiledLayer = result->baseLayers.front();
                expect (compiledLayer.properties.at ("opacity").descriptor != nullptr);
                const auto number = compiledLayer.properties.at ("transform.translateX")
                    .resolve (context, { 10.0f, 20.0f, 80.0f, 40.0f }, &runtime);
                expectWithinAbsoluteError (std::get<double> (number), 0.0, 0.0001);
                expect (runtime.hasCode ("EXPR020"));
                SkinValidationReport secondRuntime;
                const auto visible = compiledLayer.properties.at ("visible")
                    .resolve (context, {}, &secondRuntime);
                expect (std::get<bool> (visible));
                SkinValidationReport thirdRuntime;
                const auto fill = compiledLayer.properties.at ("fill")
                    .resolve (context, {}, &thirdRuntime);
                expect (std::get<juce::Colour> (fill).isTransparent());
                const auto pivotY = compiledLayer.properties.at ("transform.pivotY")
                    .resolve ({}, { 10.0f, 20.0f, 80.0f, 40.0f });
                expectWithinAbsoluteError (std::get<double> (pivotY), 40.0, 0.0001);
            }
        }

        beginTest ("enforces target context, repeat and expanded operation budgets");
        {
            auto layer = rectLayer ("meter-context");
            layer.getDynamicObject()->setProperty (
                "opacity", binding ("$expr", "meter.channels[0].peak"));
            SkinValidationReport report;
            expect (compile (renderDocument ("outputStage", { layer }), tokens, report).has_value(),
                    report.toText());
        }
        {
            auto layer = rectLayer ("bad-context");
            layer.getDynamicObject()->setProperty (
                "opacity", binding ("$expr", "meter.channelCount * 0.1"));
            SkinValidationReport report;
            expect (! compile (renderDocument ("textButton", { layer }), tokens, report));
            expect (report.hasCode ("EXPR003") || report.hasCode ("RENDER005"));
        }
        {
            auto layer = rectLayer ("row-context-on-owner-body");
            layer.getDynamicObject()->setProperty (
                "visible", binding ("$expr", "plugin.name == 'Compressor'"));
            SkinValidationReport report;
            expect (! compile (renderDocument ("pluginList", { layer }), tokens, report));
            expect (report.hasCode ("EXPR003") || report.hasCode ("RENDER005"));
        }
        {
            auto layer = rectLayer ("progress-context");
            layer.getDynamicObject()->setProperty (
                "opacity", binding ("$expr", "value.minimum"));
            SkinValidationReport report;
            expect (! compile (renderDocument ("progressBar", { layer }), tokens, report));
            expect (report.hasCode ("EXPR003") || report.hasCode ("RENDER005"));
        }
        {
            auto layer = baseLayer ("combo-text", "text");
            layer.getDynamicObject()->setProperty ("frame", anchoredFrame());
            layer.getDynamicObject()->setProperty ("text", binding ("$context", "icon.name"));
            layer.getDynamicObject()->setProperty ("font", binding ("$ref", "fonts.body"));
            layer.getDynamicObject()->setProperty ("fontSize", 12);
            layer.getDynamicObject()->setProperty ("color", "#FFFFFFFF");
            SkinValidationReport report;
            expect (! compile (renderDocument ("comboBox", { layer }), tokens, report));
            expect (report.hasCode ("RENDER005"));
        }
        {
            auto layer = baseLayer ("semantic-icon", "path");
            layer.getDynamicObject()->setProperty (
                "data", binding ("$context", "icon.path"));
            SkinValidationReport report;
            expect (compile (renderDocument ("textButton", { layer }), tokens, report).has_value(),
                    report.toText());
        }
        {
            auto layer = baseLayer ("unsafe-dynamic-path", "path");
            layer.getDynamicObject()->setProperty (
                "data", binding ("$context", "text.primary"));
            SkinValidationReport report;
            expect (! compile (renderDocument ("textButton", { layer }), tokens, report));
            expect (report.hasCode ("RENDER004"));
        }
        {
            auto nestedRepeat = baseLayer ("nested", "repeat");
            nestedRepeat.getDynamicObject()->setProperty ("source", "meter.channels");
            nestedRepeat.getDynamicObject()->setProperty ("limit", 10);
            nestedRepeat.getDynamicObject()->setProperty ("template", rectLayer ("inner"));
            auto outer = baseLayer ("outer", "repeat");
            outer.getDynamicObject()->setProperty ("source", "meter.channels");
            outer.getDynamicObject()->setProperty ("limit", 10);
            outer.getDynamicObject()->setProperty ("template", nestedRepeat);
            SkinValidationReport report;
            expect (! compile (renderDocument ("outputStage", { outer }), tokens, report));
            expect (report.hasCode ("RENDER008"));
        }
        {
            auto repeatedGroup = baseLayer ("channels", "repeat");
            repeatedGroup.getDynamicObject()->setProperty ("source", "meter.channels");
            repeatedGroup.getDynamicObject()->setProperty ("limit", 10);
            auto group = baseLayer ("template-group", "group");
            std::vector<juce::var> children;
            for (int i = 0; i < 501; ++i)
                children.push_back (rectLayer ("operation-" + juce::String (i)));
            group.getDynamicObject()->setProperty ("children", array (std::move (children)));
            repeatedGroup.getDynamicObject()->setProperty ("template", group);
            SkinValidationReport report;
            expect (! compile (renderDocument ("outputStage", { repeatedGroup }), tokens, report));
            expect (report.hasCode ("RENDER010"));
        }

        beginTest ("compiles renderer index, composite slots and ancillary defaults");
        {
            const auto surfaceJson = juce::JSON::toString (
                renderDocument ("surface", { rectLayer ("surface") }), false);
            auto pluginRowLayer = rectLayer ("plugin-row");
            pluginRowLayer.getDynamicObject()->setProperty (
                "visible", binding ("$expr", "plugin.name == 'Compressor'"));
            const auto pluginRowJson = juce::JSON::toString (
                renderDocument ("surface", { pluginRowLayer }), false);
            const auto tableJson = juce::JSON::toString (
                renderDocument ("table", { rectLayer ("table") }), false);
            const auto pluginListJson = juce::JSON::toString (
                renderDocument ("pluginList", { rectLayer ("plugin-list") }), false);
            const auto popupJson = juce::JSON::toString (
                renderDocument ("popupMenu", { rectLayer ("popup") }), false);
            std::vector<SkinPackage::EmbeddedFile> files {
                { "components/surface.json", bytes (surfaceJson) },
                { "components/plugin-row.json", bytes (pluginRowJson) },
                { "components/table.json", bytes (tableJson) },
                { "components/plugin-list.json", bytes (pluginListJson) },
                { "components/popup.json", bytes (popupJson) }
            };
            SkinValidationReport packageReport;
            const auto package = SkinPackage::fromEmbedded ("render-tests", std::move (files),
                                                            packageReport);
            expect (package.has_value(), packageReport.toText());

            auto index = object();
            auto surfaceStyles = object();
            surfaceStyles.getDynamicObject()->setProperty ("tableRow", "components/surface.json");
            surfaceStyles.getDynamicObject()->setProperty (
                "pluginRow", "components/plugin-row.json");
            index.getDynamicObject()->setProperty ("surface", surfaceStyles);
            auto tableStyles = object();
            auto tableEntry = object();
            tableEntry.getDynamicObject()->setProperty ("renderer", "components/table.json");
            auto slots = object();
            slots.getDynamicObject()->setProperty ("row.surface", "surface.tableRow");
            tableEntry.getDynamicObject()->setProperty ("slots", slots);
            tableStyles.getDynamicObject()->setProperty ("queue", tableEntry);
            index.getDynamicObject()->setProperty ("table", tableStyles);
            auto pluginListStyles = object();
            auto pluginListEntry = object();
            pluginListEntry.getDynamicObject()->setProperty (
                "renderer", "components/plugin-list.json");
            auto pluginListSlots = object();
            pluginListSlots.getDynamicObject()->setProperty (
                "row.surface", "surface.pluginRow");
            pluginListEntry.getDynamicObject()->setProperty ("slots", pluginListSlots);
            pluginListStyles.getDynamicObject()->setProperty ("inventory", pluginListEntry);
            index.getDynamicObject()->setProperty ("pluginList", pluginListStyles);
            auto popupStyles = object();
            popupStyles.getDynamicObject()->setProperty ("default", "components/popup.json");
            index.getDynamicObject()->setProperty ("popupMenu", popupStyles);

            SkinValidationReport report;
            const auto validated = SkinSchemaValidator::validate (
                SkinSchemaDocument::componentIndex, index, "components/index.json", report);
            expect (validated.has_value(), report.toText());
            const PlayerComponentCatalog catalog;
            const auto compiled = SkinRenderCompiler::compileIndex (
                *validated, *package, tokens, catalog, report);
            expect (! report.hasErrors(), report.toText());
            expectEquals (static_cast<int> (compiled.size()), 5);
            expectEquals (compiled.at ("table.queue").slotStyles.at ("row.surface"),
                          juce::String ("surface.tableRow"));
            expectEquals (
                compiled.at ("pluginList.inventory").slotStyles.at ("row.surface"),
                juce::String ("surface.pluginRow"));
        }

        beginTest ("rejects unknown patch fields, incompatible tokens and non-default ancillary styles");
        {
            auto raw = renderDocument ("surface", { rectLayer ("body") });
            auto hover = object();
            hover.getDynamicObject()->setProperty ("body.noSuchProperty", 1);
            auto variants = object();
            variants.getDynamicObject()->setProperty ("hover", hover);
            raw.getDynamicObject()->setProperty ("variants", variants);
            SkinValidationReport report;
            expect (! compile (raw, tokens, report));
            expect (report.hasCode ("RENDER012"));
        }
        {
            auto layer = rectLayer ("bad-token");
            layer.getDynamicObject()->setProperty ("opacity", binding ("$ref", "colors.accent"));
            SkinValidationReport report;
            expect (! compile (renderDocument ("surface", { layer }), tokens, report));
            expect (report.hasCode ("RENDER006"));
        }
        {
            auto index = object();
            auto styles = object();
            styles.getDynamicObject()->setProperty ("dark", "components/popup.json");
            index.getDynamicObject()->setProperty ("popupMenu", styles);
            SkinValidationReport report;
            const auto validated = SkinSchemaValidator::validate (
                SkinSchemaDocument::componentIndex, index, "components/index.json", report);
            expect (validated.has_value());
            std::vector<SkinPackage::EmbeddedFile> files;
            const auto package = SkinPackage::fromEmbedded ("empty", std::move (files), report);
            const PlayerComponentCatalog catalog;
            if (package)
                expect (SkinRenderCompiler::compileIndex (
                    *validated, *package, tokens, catalog, report).empty());
            expect (report.hasCode ("RENDER004"));
        }

        beginTest ("schema-validates every bounded renderer file before semantic compilation");
        {
            std::vector<SkinPackage::EmbeddedFile> files {
                { "components/missing-fields.json", bytes ("{}") },
                { "components/malformed.json", bytes ("[") }
            };
            SkinValidationReport report;
            const auto package = SkinPackage::fromEmbedded ("invalid-renderers", std::move (files),
                                                            report);
            auto index = object();
            auto styles = object();
            styles.getDynamicObject()->setProperty ("first", "components/missing-fields.json");
            styles.getDynamicObject()->setProperty ("second", "components/malformed.json");
            index.getDynamicObject()->setProperty ("surface", styles);
            const auto validated = SkinSchemaValidator::validate (
                SkinSchemaDocument::componentIndex, index, "components/index.json", report);
            expect (package.has_value() && validated.has_value(), report.toText());
            if (package && validated)
            {
                const PlayerComponentCatalog catalog;
                expect (SkinRenderCompiler::compileIndex (
                    *validated, *package, tokens, catalog, report).empty());
                expect (report.hasCode ("SCHEMA002"));
                expect (report.hasCode ("SCHEMA004"));
            }
        }

        beginTest ("variant patches cannot bypass path, asset, enum or range semantics");
        {
            auto image = baseLayer ("cover", "image");
            image.getDynamicObject()->setProperty ("frame", anchoredFrame());
            image.getDynamicObject()->setProperty ("source", "assets/cover.png");
            auto raw = renderDocument ("artwork", { image });
            auto hover = object();
            hover.getDynamicObject()->setProperty (
                "cover.source", binding ("$ref", "strings.unsafeAsset"));
            auto variants = object();
            variants.getDynamicObject()->setProperty ("hover", hover);
            raw.getDynamicObject()->setProperty ("variants", variants);
            SkinValidationReport report;
            expect (! compile (raw, tokens, report));
            expect (report.hasCode ("RENDER007"));
        }
        {
            auto path = baseLayer ("glyph", "path");
            path.getDynamicObject()->setProperty ("data", "M 0 0 L 1 1");
            auto raw = renderDocument ("surface", { path });
            auto hover = object();
            hover.getDynamicObject()->setProperty ("glyph.data", "M,,0,,0");
            auto variants = object();
            variants.getDynamicObject()->setProperty ("hover", hover);
            raw.getDynamicObject()->setProperty ("variants", variants);
            SkinValidationReport report;
            expect (! compile (raw, tokens, report));
            expect (report.hasCode ("RENDER013"));
        }
        {
            auto text = baseLayer ("caption", "text");
            text.getDynamicObject()->setProperty ("frame", anchoredFrame());
            text.getDynamicObject()->setProperty ("text", "Caption");
            text.getDynamicObject()->setProperty ("font", binding ("$ref", "fonts.body"));
            text.getDynamicObject()->setProperty ("fontSize", 12);
            text.getDynamicObject()->setProperty ("color", "#FFFFFFFF");
            auto raw = renderDocument ("label", { text });
            auto hover = object();
            hover.getDynamicObject()->setProperty ("caption.align", "diagonal");
            hover.getDynamicObject()->setProperty ("caption.maxLines", 100);
            auto variants = object();
            variants.getDynamicObject()->setProperty ("hover", hover);
            raw.getDynamicObject()->setProperty ("variants", variants);
            SkinValidationReport report;
            expect (! compile (raw, tokens, report));
            expect (report.hasCode ("RENDER004"));
        }
        {
            auto path = baseLayer ("animated-glyph", "path");
            path.getDynamicObject()->setProperty ("data", "M 0 0 L 1 1");
            auto raw = renderDocument ("surface", { path });
            auto hover = object();
            hover.getDynamicObject()->setProperty (
                "animated-glyph.data", binding ("$ref", "strings.glyphPath"));
            auto variants = object();
            variants.getDynamicObject()->setProperty ("hover", hover);
            raw.getDynamicObject()->setProperty ("variants", variants);
            SkinValidationReport report;
            const auto compiled = compile (raw, tokens, report);
            expect (compiled.has_value(), report.toText());
            if (compiled)
            {
                const auto& patch = compiled->variants[1].properties.at ("animated-glyph.data");
                expect (patch.compiledPath != nullptr);
                expect (patch.descriptor != nullptr && ! patch.descriptor->interpolable);
                expectEquals (compiled->pathCommandCount, 5);
            }
        }
        {
            auto text = baseLayer ("spring-caption", "text");
            text.getDynamicObject()->setProperty ("frame", anchoredFrame());
            text.getDynamicObject()->setProperty ("text", "Caption");
            text.getDynamicObject()->setProperty ("font", binding ("$ref", "fonts.body"));
            text.getDynamicObject()->setProperty ("fontSize", 12);
            text.getDynamicObject()->setProperty ("color", "#FFFFFFFF");
            auto raw = renderDocument ("label", { text });
            auto spring = object();
            spring.getDynamicObject()->setProperty ("durationMs", 120);
            spring.getDynamicObject()->setProperty ("easing", "springGentle");
            spring.getDynamicObject()->setProperty (
                "properties", array ({ "spring-caption.maxLines" }));
            auto transitions = object();
            transitions.getDynamicObject()->setProperty ("hover", spring);
            raw.getDynamicObject()->setProperty ("transitions", transitions);
            SkinValidationReport report;
            expect (! compile (raw, tokens, report));
            expect (report.hasCode ("RENDER011"));
        }
    }
};

SkinRenderCompilerTests skinRenderCompilerTests;
} // namespace
