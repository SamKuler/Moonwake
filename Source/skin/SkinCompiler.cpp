#include "SkinCompiler.h"

#include "SkinCompatibilityChecker.h"
#include "SkinLayoutCompiler.h"
#include "SkinLayoutEngine.h"
#include "SkinRenderCompiler.h"
#include "SkinRenderer.h"
#include "SkinRowLayout.h"
#include "SkinSchemaValidator.h"
#include "app/ProductIdentity.h"

#include <cmath>
#include <cstring>
#include <set>

namespace
{
std::optional<juce::var> readJson (const SkinPackage& package, const juce::String& path,
                                   SkinValidationReport& report)
{
    const auto before = report.getDiagnostics().size();
    const auto text = package.readText (path, report);
    if (report.getDiagnostics().size() != before) return std::nullopt;

    juce::var value;
    const auto parsed = juce::JSON::parse (text, value);
    if (parsed.failed())
    {
        report.addError ("SCHEMA002", path, {},
                         "JSON could not be parsed: " + parsed.getErrorMessage());
        return std::nullopt;
    }
    return value;
}

std::optional<SkinValidatedDocument> readValidated (
    const SkinPackage& package, const juce::String& path, SkinSchemaDocument kind,
    SkinValidationReport& report)
{
    const auto raw = readJson (package, path, report);
    return raw ? SkinSchemaValidator::validate (kind, *raw, path, report)
               : std::nullopt;
}

bool prevalidateRenderDocuments (const SkinValidatedDocument& index,
                                 const SkinPackage& package,
                                 SkinValidationReport& report)
{
    std::set<juce::String> paths;
    const auto& groups = index.value().getDynamicObject()->getProperties();
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const auto styles = groups.getValueAt (groupIndex);
        if (! styles.isObject()) continue;
        const auto& entries = styles.getDynamicObject()->getProperties();
        for (int styleIndex = 0; styleIndex < entries.size(); ++styleIndex)
        {
            const auto entry = entries.getValueAt (styleIndex);
            const auto path = entry.isString()
                ? entry.toString() : entry.getProperty ("renderer", {}).toString();
            if (path.isNotEmpty()) paths.insert (path);
        }
    }

    for (const auto& path : paths)
        readValidated (package, path, SkinSchemaDocument::render, report);
    return ! report.hasErrors();
}

std::optional<SkinLayoutEntry> layoutEntry (juce::StringRef name)
{
    const juce::String value (name);
    if (value == "studio") return SkinLayoutEntry::studio;
    if (value == "listening") return SkinLayoutEntry::listening;
    if (value == "effects") return SkinLayoutEntry::effects;
    if (value == "settings") return SkinLayoutEntry::settings;
    return std::nullopt;
}

bool isResourceBinding (const SkinRenderPropertyBinding& binding)
{
    return binding.descriptor != nullptr
        && (binding.descriptor->semantic == SkinRenderPropertySemantic::assetPath
            || binding.descriptor->type == SkinRenderValueType::font);
}

void collectBindingAssets (const SkinRenderPropertyBinding& binding,
                           const SkinTokenStore& tokens,
                           const juce::String& currentOrigin,
                           bool localRenderer,
                           std::set<juce::String>& assets,
                           std::vector<SkinFontValue>& fonts)
{
    if (! isResourceBinding (binding)) return;
    const auto owner = binding.tokenReference.isNotEmpty()
        ? tokens.originOf (binding.tokenReference)
        : (localRenderer ? currentOrigin : juce::String());
    if (owner != currentOrigin) return;
    const auto* compiled = std::get_if<SkinCompiledRenderValue> (&binding.value);
    if (compiled == nullptr) return;
    if (binding.descriptor != nullptr
        && binding.descriptor->semantic == SkinRenderPropertySemantic::assetPath)
        if (const auto* path = std::get_if<juce::String> (compiled)) assets.insert (*path);
    if (const auto* font = std::get_if<SkinFontValue> (compiled))
    {
        const auto duplicate = std::find_if (fonts.begin(), fonts.end(), [&] (const auto& value)
        {
            return value.source == font->source && value.families == font->families
                && value.weight == font->weight && value.italic == font->italic
                && std::abs (value.tracking - font->tracking) <= 0.000001f;
        });
        if (duplicate == fonts.end()) fonts.push_back (*font);
    }
}

void collectLayerAssets (const SkinRenderLayer& layer, const SkinTokenStore& tokens,
                         const juce::String& currentOrigin, bool localRenderer,
                         std::set<juce::String>& assets,
                         std::vector<SkinFontValue>& fonts)
{
    for (const auto& [ignored, binding] : layer.properties)
    {
        juce::ignoreUnused (ignored);
        collectBindingAssets (binding, tokens, currentOrigin, localRenderer, assets, fonts);
    }
    for (const auto& child : layer.children)
        collectLayerAssets (child, tokens, currentOrigin, localRenderer, assets, fonts);
}

void collectProgramAssets (const SkinRenderProgram& program,
                           const SkinTokenStore& tokens,
                           const juce::String& currentOrigin,
                           bool localRenderer,
                           std::set<juce::String>& assets,
                           std::vector<SkinFontValue>& fonts)
{
    for (const auto& layer : program.baseLayers)
        collectLayerAssets (layer, tokens, currentOrigin, localRenderer, assets, fonts);
    for (const auto& variant : program.variants)
        for (const auto& [ignored, binding] : variant.properties)
        {
            juce::ignoreUnused (ignored);
            collectBindingAssets (
                binding, tokens, currentOrigin, localRenderer, assets, fonts);
        }
    for (const auto& [ignored, transition] : program.transitions)
        for (const auto& [ignoredPath, binding] : transition.from)
        {
            juce::ignoreUnused (ignored, ignoredPath);
            collectBindingAssets (
                binding, tokens, currentOrigin, localRenderer, assets, fonts);
        }
}

bool assignBindingAssetCache (
    SkinRenderPropertyBinding& binding, const SkinTokenStore& tokens,
    const juce::String& currentOrigin, bool localRenderer,
    const std::map<juce::String, std::shared_ptr<const SkinAssetCache>>& caches,
    SkinValidationReport& report)
{
    if (! isResourceBinding (binding)) return true;
    auto owner = binding.tokenReference.isNotEmpty()
        ? tokens.originOf (binding.tokenReference)
        : (localRenderer ? currentOrigin : juce::String());
    if (owner.isEmpty())
    {
        if (binding.assets != nullptr) return true;
        report.addError ("ASSET001", "components/index.json", binding.tokenReference,
                         "Renderer resource binding lost its package provenance.");
        return false;
    }
    const auto found = caches.find (owner);
    if (found == caches.end() || found->second == nullptr)
    {
        report.addError ("ASSET001", "components/index.json", binding.tokenReference,
                         "Renderer resource token has no package cache provenance.");
        return false;
    }
    binding.assets = found->second;
    return true;
}

bool assignLayerAssetCaches (
    SkinRenderLayer& layer, const SkinTokenStore& tokens,
    const juce::String& currentOrigin, bool localRenderer,
    const std::map<juce::String, std::shared_ptr<const SkinAssetCache>>& caches,
    SkinValidationReport& report)
{
    for (auto& [ignored, binding] : layer.properties)
    {
        juce::ignoreUnused (ignored);
        if (! assignBindingAssetCache (
                binding, tokens, currentOrigin, localRenderer, caches, report))
            return false;
    }
    for (auto& child : layer.children)
        if (! assignLayerAssetCaches (
                child, tokens, currentOrigin, localRenderer, caches, report))
            return false;
    return true;
}

bool assignProgramAssetCaches (
    SkinRenderProgram& program, const SkinTokenStore& tokens,
    const juce::String& currentOrigin, bool localRenderer,
    const std::map<juce::String, std::shared_ptr<const SkinAssetCache>>& caches,
    SkinValidationReport& report)
{
    for (auto& layer : program.baseLayers)
        if (! assignLayerAssetCaches (
                layer, tokens, currentOrigin, localRenderer, caches, report))
            return false;
    for (auto& variant : program.variants)
        for (auto& [ignored, binding] : variant.properties)
        {
            juce::ignoreUnused (ignored);
            if (! assignBindingAssetCache (
                    binding, tokens, currentOrigin, localRenderer, caches, report))
                return false;
        }
    for (auto& [ignored, transition] : program.transitions)
        for (auto& [ignoredPath, binding] : transition.from)
        {
            juce::ignoreUnused (ignored, ignoredPath);
            if (! assignBindingAssetCache (
                    binding, tokens, currentOrigin, localRenderer, caches, report))
                return false;
        }
    return true;
}

bool layerReferencesFocus (const SkinRenderLayer& layer)
{
    for (const auto& [ignored, binding] : layer.properties)
    {
        juce::ignoreUnused (ignored);
        if (const auto* expression = std::get_if<SkinExpression> (&binding.value))
            if (expression->referencesContext ("state.focused")) return true;
    }
    return std::any_of (layer.children.begin(), layer.children.end(), layerReferencesFocus);
}

bool exposesFocus (const SkinRenderProgram& program)
{
    if (std::any_of (program.baseLayers.begin(), program.baseLayers.end(),
                    layerReferencesFocus))
        return true;
    const auto focused = std::find_if (program.variants.begin(), program.variants.end(),
                                      [] (const auto& value) { return value.name == "focused"; });
    return focused != program.variants.end()
        && ! focused->properties.empty();
}

class FocusValidationFaultSink final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus statusIn) override
    {
        status = statusIn;
    }

    SkinRenderStatus status = SkinRenderStatus::ok;
};

bool imagesDiffer (const juce::Image& first, const juce::Image& second)
{
    if (first.getBounds() != second.getBounds()) return true;
    juce::Image::BitmapData firstData (first, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData secondData (second, juce::Image::BitmapData::readOnly);
    const auto bytesPerRow = static_cast<size_t> (
        firstData.width * firstData.pixelStride);
    for (int y = 0; y < firstData.height; ++y)
        if (std::memcmp (firstData.getLinePointer (y), secondData.getLinePointer (y),
                         bytesPerRow) != 0)
            return true;
    return false;
}

std::optional<SkinExpressionValue> syntheticContextValue (
    std::string_view requested,
    const std::map<juce::String, SkinExpressionType>& allowedContext)
{
    const auto path = juce::String::fromUTF8 (
        requested.data(), static_cast<int> (requested.size()));
    const auto found = allowedContext.find (path);
    if (found == allowedContext.end()) return std::nullopt;
    switch (found->second)
    {
        case SkinExpressionType::number:
            return SkinExpressionValue (1.0);
        case SkinExpressionType::boolean:
            return SkinExpressionValue (path != "combo.popupOpen");
        case SkinExpressionType::string:
            if (path == "plugin.format")
                return SkinExpressionValue (juce::String ("VST3"));
            if (path == "skin.version")
                return SkinExpressionValue (juce::String ("1.0.0"));
            if (path == "column.id")
                return SkinExpressionValue (juce::String ("title"));
            return SkinExpressionValue (juce::String ("Probe"));
        case SkinExpressionType::colour:
            return SkinExpressionValue (juce::Colours::white);
    }
    return std::nullopt;
}

std::optional<juce::Image> renderFocusProbe (
    const SkinRenderStyleEntry& style, juce::Rectangle<float> bounds, bool focused,
    const std::map<juce::String, SkinExpressionType>& allowedContext)
{
    juce::Image image (juce::Image::ARGB, juce::roundToInt (bounds.getWidth()),
                       juce::roundToInt (bounds.getHeight()), true);
    juce::Graphics graphics (image);
    SkinRenderContext context;
    context.bounds = bounds;
    context.contentBounds = bounds.reduced (2.0f);
    context.state.focused = focused;
    context.normalisedValue = 0.5;
    context.minimum = 0.0;
    context.maximum = 1.0;
    context.primaryText = "Focus";
    context.secondaryText = "Keyboard focus";
    context.displayValue = "50%";
    context.iconName = "play";
    context.meterPeaks.fill (0.5f);
    context.meterIsLfe.fill (false);
    for (int channel = 0; channel < 10; ++channel)
        context.meterLabels.add ("Channel " + juce::String (channel + 1));
    context.repeatItemCount = 3;
    context.repeatLabels.addArray ({ "One", "Two", "Three" });
    context.scrollOrientation = "vertical";
    context.scrollPart = "thumb";
    context.menuPart = "item";
    context.scrollThumbStart = 0.2;
    context.scrollThumbSize = 0.4;
    context.menuItemIndex = 0;
    context.tableRowCount = 3;
    context.tableColumnCount = 3;
    context.extraLookup = [&allowedContext] (std::string_view path)
    {
        return syntheticContextValue (path, allowedContext);
    };

    FocusValidationFaultSink sink;
    SkinRenderer renderer (style.assets);
    if (renderer.draw (graphics, style.renderer, context, true, sink)
            != SkinRenderStatus::ok
        || sink.status != SkinRenderStatus::ok)
        return std::nullopt;
    return image;
}

bool rendersVisibleFocus (
    const SkinRenderStyleEntry& style,
    const std::map<juce::String, SkinExpressionType>& allowedContext)
{
    if (! exposesFocus (style.renderer)) return false;
    for (const auto size : { juce::Point<int> (160, 48),
                             juce::Point<int> (320, 160) })
    {
        const juce::Rectangle<float> bounds (
            0.0f, 0.0f, static_cast<float> (size.x), static_cast<float> (size.y));
        const auto unfocused = renderFocusProbe (
            style, bounds, false, allowedContext);
        const auto focused = renderFocusProbe (
            style, bounds, true, allowedContext);
        if (! unfocused || ! focused || ! imagesDiffer (*unfocused, *focused))
            return false;
    }
    return true;
}

bool rendersVisibleFocus (const SkinRenderStyleEntry& style)
{
    return rendersVisibleFocus (
        style, skinRenderContextForTarget (style.renderer.target));
}

bool isInteractiveRendererTarget (SkinRenderTarget target)
{
    switch (target)
    {
        case SkinRenderTarget::textButton:
        case SkinRenderTarget::toggleButton:
        case SkinRenderTarget::comboBox:
        case SkinRenderTarget::textEditor:
        case SkinRenderTarget::linearSlider:
        case SkinRenderTarget::rotarySlider:
        case SkinRenderTarget::scrollBar:
        case SkinRenderTarget::table:
        case SkinRenderTarget::effectChain:
        case SkinRenderTarget::channelMixer:
        case SkinRenderTarget::channelMap:
        case SkinRenderTarget::pluginList:
        case SkinRenderTarget::skinList:
            return true;
        case SkinRenderTarget::label:
        case SkinRenderTarget::progressBar:
        case SkinRenderTarget::popupMenu:
        case SkinRenderTarget::tooltip:
        case SkinRenderTarget::surface:
        case SkinRenderTarget::artwork:
        case SkinRenderTarget::outputStage:
            return false;
    }
    return false;
}

template <typename Callback>
void visitLayoutNode (const SkinLayoutNode& node, Callback&& callback)
{
    callback (node);
    const auto visitChildren = [&] (const auto& children)
    {
        for (const auto& child : children) visitLayoutNode (child, callback);
    };
    if (const auto* flex = std::get_if<SkinFlexLayout> (&node.payload))
        visitChildren (flex->children);
    else if (const auto* grid = std::get_if<SkinGridLayout> (&node.payload))
        visitChildren (grid->children);
    else if (const auto* stack = std::get_if<SkinStackLayout> (&node.payload))
        visitChildren (stack->children);
    else if (const auto* scroll = std::get_if<SkinScrollLayout> (&node.payload))
    {
        if (scroll->child != nullptr) visitLayoutNode (*scroll->child, callback);
    }
}

std::optional<juce::String> unavailableCompositeContext (
    const SkinRenderProgram& program,
    const std::map<juce::String, SkinExpressionType>& allowedContext)
{
    const auto directContext = skinRenderContextForTarget (program.target);
    const auto candidateSlotContext = skinRenderContextForAnyCompositeSlot (
        program.target);
    for (const auto& path : program.contextPaths)
        if (! directContext.contains (path)
            && candidateSlotContext.contains (path)
            && ! allowedContext.contains (path))
            return path;
    return std::nullopt;
}

bool validateLayoutStyles (const CompiledSkin& skin, SkinValidationReport& report)
{
    bool ok = true;
    const auto validateDirectContext = [&] (
        const SkinRenderStyleEntry& style, const juce::String& nodeId)
    {
        if (const auto path = unavailableCompositeContext (
                style.renderer,
                skinRenderContextForTarget (style.renderer.target)))
        {
            report.addError (
                "RENDER005", "layout.json", nodeId,
                "Style uses slot-only context '" + *path
                    + "' from a direct layout invocation.");
            ok = false;
        }
    };
    for (const auto& [entry, definition] : skin.layouts)
    {
        juce::ignoreUnused (entry);
        const auto validateAncillary = [&] (const auto& styles, const juce::String& pointer)
        {
            for (const auto& [targetName, styleName] : styles)
            {
                const auto target = skinRenderTargetFromName (targetName);
                const auto found = skin.renderers.find (styleName);
                const auto* style = target && found != skin.renderers.end()
                    && found->second.renderer.target == *target ? &found->second : nullptr;
                if (style == nullptr)
                {
                    report.addError ("LAYOUT013", "layout.json", pointer + "/" + targetName,
                                     "Ancillary style cannot be resolved for its declared target.");
                    ok = false;
                }
                else
                    validateDirectContext (*style, pointer + "/" + targetName);
            }
        };
        validateAncillary (definition.ancillaryStyles, "/ancillaryStyles");
        for (size_t index = 0; index < definition.breakpoints.size(); ++index)
            validateAncillary (definition.breakpoints[index].ancillaryStyles,
                               "/breakpoints/" + juce::String (static_cast<int> (index)) + "/ancillaryStyles");
        const auto validateRoot = [&] (const SkinLayoutNode& root)
        {
            visitLayoutNode (root, [&] (const SkinLayoutNode& node)
            {
                if (node.type() == SkinLayoutNodeType::visual)
                {
                    const auto* style = skin.resolveStyle (
                        SkinRenderTarget::surface, node.common.style);
                    if (style == nullptr)
                    {
                        report.addError ("LAYOUT013", "layout.json", node.common.id,
                                         "Visual or container style cannot be resolved.");
                        ok = false;
                    }
                    else
                        validateDirectContext (*style, node.common.id);
                    return;
                }
                const auto* component = std::get_if<SkinComponentLayout> (&node.payload);
                if (component == nullptr)
                {
                    if (node.common.style.isNotEmpty())
                    {
                        const auto* style = skin.resolveStyle (
                            SkinRenderTarget::surface, node.common.style);
                        if (style == nullptr)
                        {
                            report.addError (
                                "LAYOUT013", "layout.json", node.common.id,
                                "Container style cannot be resolved.");
                            ok = false;
                        }
                        else
                            validateDirectContext (*style, node.common.id);
                    }
                    return;
                }

                const auto* descriptor = PlayerComponentCatalog::find (component->componentId);
                if (descriptor == nullptr) return;
                auto targetName = descriptor->defaultRendererTarget;
                if (component->presentation.isNotEmpty())
                    if (const auto found = descriptor->rendererTargetByPresentation.find (
                            component->presentation);
                        found != descriptor->rendererTargetByPresentation.end())
                        targetName = found->second;
                const auto target = skinRenderTargetFromName (targetName);
                const auto* style = target ? skin.resolveStyle (*target, node.common.style) : nullptr;
                if (style == nullptr)
                {
                    report.addError ("LAYOUT013", "layout.json", node.common.id,
                                     "Component style cannot be resolved.");
                    ok = false;
                }
                else if (descriptor->interactive && ! rendersVisibleFocus (*style))
                {
                    report.addError ("RENDER004", "layout.json", node.common.id,
                                     "Interactive component style has no visible focus state.");
                    ok = false;
                }
                if (style != nullptr)
                    validateDirectContext (*style, node.common.id);
            });
        };
        validateRoot (definition.base);
        for (const auto& breakpoint : definition.breakpoints) validateRoot (breakpoint.root);
    }
    return ok;
}

bool validateSlotStyles (const CompiledSkin& skin, SkinValidationReport& report)
{
    bool ok = true;
    for (const auto& [styleName, style] : skin.renderers)
    {
        std::map<juce::String, SkinRenderTarget> expectedSlots;
        const auto ownerName = skinRenderTargetName (style.renderer.target);
        for (const auto& component : PlayerComponentCatalog::descriptors())
        {
            bool usesOwner = component.defaultRendererTarget == ownerName;
            for (const auto& [ignoredPresentation, target] :
                 component.rendererTargetByPresentation)
            {
                juce::ignoreUnused (ignoredPresentation);
                usesOwner = usesOwner || target == ownerName;
            }
            if (! usesOwner) continue;
            for (const auto& slot : component.slots)
                if (const auto target = skinRenderTargetFromName (slot.rendererTarget))
                    expectedSlots.insert_or_assign (slot.name, *target);
        }

        for (const auto& [slotName, expected] : expectedSlots)
        {
            const auto* resolved = skin.resolveSlotStyle (style, slotName, expected);
            if (resolved == nullptr)
            {
                report.addError ("COMPONENT005", "components/index.json",
                                 styleName + "/" + slotName,
                                 "Composite slot style cannot be resolved.");
                ok = false;
            }
            if (resolved != nullptr)
            {
                const auto allowed = skinRenderContextForCompositeSlot (
                    style.renderer.target, slotName, expected);
                if (const auto path = unavailableCompositeContext (
                        resolved->renderer, allowed))
                {
                    report.addError (
                        "RENDER005", "components/index.json",
                        styleName + "/" + slotName,
                        "Slot renderer uses context '" + *path
                            + "' that is unavailable for this owner and slot.");
                    ok = false;
                }
                else if (isInteractiveRendererTarget (expected)
                         && ! rendersVisibleFocus (*resolved, allowed))
                {
                    report.addError (
                        "RENDER004", "components/index.json",
                        styleName + "/" + slotName,
                        "Interactive composite slot style has no visible focus state.");
                    ok = false;
                }
            }
        }
    }
    return ok;
}
} // namespace

std::shared_ptr<const CompiledSkin> SkinCompiler::compile (
    const SkinPackage& package, std::shared_ptr<const CompiledSkin> foundation,
    SkinValidationReport& report)
{
    if (! package.verifySnapshotDigest (report)) return {};

    const auto rawManifest = readJson (package, "manifest.json", report);
    const auto currentVersion = SkinSemVersion::parse (MoonwakeIdentity::version);
    if (! rawManifest || ! currentVersion
        || ! SkinCompatibilityChecker::checkManifestHeader (
            *rawManifest, *currentVersion, report))
        return {};

    const auto manifestDocument = SkinSchemaValidator::validate (
        SkinSchemaDocument::manifest, *rawManifest, "manifest.json", report);
    const auto manifest = manifestDocument
        ? SkinManifest::parse (*manifestDocument, report) : std::nullopt;
    if (! manifest || report.hasErrors()) return {};

    std::shared_ptr<const CompiledSkin> parent;
    if (manifest->extends == "builtin.foundation")
    {
        if (foundation == nullptr || foundation->manifest.id != "builtin.foundation")
        {
            report.addError ("MANIFEST006", "manifest.json", "/extends",
                             "builtin.foundation inheritance requires the embedded foundation skin.");
            return {};
        }
        parent = std::move (foundation);
    }

    std::optional<SkinValidatedDocument> tokenDocument;
    std::optional<SkinValidatedDocument> componentDocument;
    std::map<SkinLayoutEntry, SkinValidatedDocument> layoutDocuments;
    if (manifest->tokensPath.isNotEmpty())
        tokenDocument = readValidated (package, manifest->tokensPath,
                                       SkinSchemaDocument::tokens, report);
    if (manifest->componentsPath.isNotEmpty())
        componentDocument = readValidated (package, manifest->componentsPath,
                                           SkinSchemaDocument::componentIndex, report);
    for (const auto& [name, path] : manifest->layouts)
        if (const auto entry = layoutEntry (name))
            if (auto document = readValidated (package, path,
                                               SkinSchemaDocument::layout, report))
                layoutDocuments.emplace (*entry, std::move (*document));
    if (componentDocument)
        prevalidateRenderDocuments (*componentDocument, package, report);
    if (report.hasErrors()) return {};

    auto compiled = std::make_shared<CompiledSkin>();
    compiled->manifest = *manifest;
    const auto currentOrigin = manifest->id + "@" + manifest->version
                             + "#" + package.sha256();
    if (parent != nullptr)
    {
        compiled->tokens = parent->tokens;
        compiled->renderers = parent->renderers;
        compiled->layouts = parent->layouts;
        compiled->assetCaches = parent->assetCaches;
    }

    if (tokenDocument)
    {
        const auto tokens = SkinTokenStore::compile (
            *tokenDocument, parent != nullptr ? &parent->tokens : nullptr,
            report, currentOrigin);
        if (! tokens || report.hasErrors()) return {};
        compiled->tokens = *tokens;
        if (parent != nullptr)
            for (auto& [ignored, inherited] : compiled->renderers)
            {
                juce::ignoreUnused (ignored);
                if (! SkinRenderCompiler::rebindProgramTokens (
                        inherited.renderer, compiled->tokens, report))
                    return {};
            }
    }
    else if (parent == nullptr)
    {
        report.addError ("MANIFEST006", "manifest.json", "/entrypoints/tokens",
                         "A standalone skin must provide a token entrypoint.");
        return {};
    }

    std::map<juce::String, SkinRenderStyleEntry> localRenderers;
    if (componentDocument)
    {
        const PlayerComponentCatalog catalog;
        localRenderers = SkinRenderCompiler::compileIndex (
            *componentDocument, package, compiled->tokens, catalog, report);
        if (report.hasErrors()) return {};
        for (auto& [name, renderer] : localRenderers)
        {
            if (const auto inherited = compiled->renderers.find (name); inherited != compiled->renderers.end())
            {
                if (! renderer.renderer.rowLayout) renderer.renderer.rowLayout = inherited->second.renderer.rowLayout;
                if (! renderer.renderer.controlStyle) renderer.renderer.controlStyle = inherited->second.renderer.controlStyle;
            }
            compiled->renderers.insert_or_assign (name, renderer);
        }
    }
    else if (parent == nullptr)
    {
        report.addError ("MANIFEST006", "manifest.json", "/entrypoints/components",
                         "A standalone skin must provide a renderer entrypoint.");
        return {};
    }

    for (auto& [entry, document] : layoutDocuments)
    {
        auto layout = SkinLayoutCompiler::compile (document, entry, report);
        if (layout) compiled->layouts.insert_or_assign (entry, std::move (*layout));
    }
    if (report.hasErrors()) return {};

    for (const auto target : { SkinLayoutEntry::studio, SkinLayoutEntry::listening,
                               SkinLayoutEntry::effects, SkinLayoutEntry::settings })
        if (! compiled->layouts.contains (target))
            report.addError ("MANIFEST006", "manifest.json", "/entrypoints/layouts",
                             "Skin does not provide or inherit all four layout entries.");

    for (const auto& targetName : skinRenderTargetNames())
    {
        const auto target = skinRenderTargetFromName (targetName);
        if (! target || compiled->resolveStyle (*target, {}) == nullptr)
            report.addError ("RENDER004", "components/index.json", targetName,
                             "Skin does not provide or inherit target.default.");
    }
    if (report.hasErrors()) return {};

    for (const auto& [entry, layout] : compiled->layouts)
    {
        juce::ignoreUnused (entry);
        for (const auto bounds : { juce::Rectangle<float> (0, 0, 1040, 700),
                                   juce::Rectangle<float> (0, 0, 1240, 800) })
            for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
            {
                const auto resolved = SkinLayoutEngine::resolve (layout, bounds, 1.0f, report, workspace, compiled.get());
                if (! resolved) return {};
                const auto validateRows = [&] (auto&& self, const SkinResolvedNode& node) -> bool
                {
                    if (node.visible && ! node.bounds.isEmpty() && node.type == SkinLayoutNodeType::component)
                    {
                        const auto* descriptor = PlayerComponentCatalog::find (node.componentId);
                        const auto target = descriptor != nullptr ? skinRenderTargetFromName (descriptor->defaultRendererTarget) : std::nullopt;
                        if (target)
                        {
                            auto* style = compiled->resolveStyle (*target, node.style);
                            if (style != nullptr && ! style->renderer.rowLayout)
                                style = compiled->resolveStyle (*target, "default");
                            if (style != nullptr && style->renderer.rowLayout)
                            {
                                SkinRenderContext context;
                                context.bounds = context.contentBounds = node.bounds.withPosition (0, 0);
                                context.tableRowCount = context.repeatItemCount = 3;
                                context.tableColumnCount = 3;
                                context.extraLookup = [presentation = node.presentation] (std::string_view key) -> std::optional<SkinExpressionValue>
                                {
                                    if (key == "table.presentation") return SkinExpressionValue (presentation.isEmpty() ? juce::String ("table") : presentation);
                                    return std::nullopt;
                                };
                                if (! resolveSkinRowLayout (style->renderer, context, report)) return false;
                            }
                        }
                    }
                    for (const auto& child : node.children) if (! self (self, child)) return false;
                    return true;
                };
                if (! validateRows (validateRows, resolved->root)) return {};
            }
    }

    std::set<juce::String> referencedAssets;
    std::vector<SkinFontValue> referencedFonts;
    for (const auto& [name, renderer] : compiled->renderers)
    {
        collectProgramAssets (
            renderer.renderer, compiled->tokens, currentOrigin,
            localRenderers.contains (name), referencedAssets, referencedFonts);
    }
    const auto assetCache = SkinAssetCache::create (
        package, manifest->id, manifest->version, referencedAssets, referencedFonts, report);
    if (! assetCache || report.hasErrors()) return {};
    compiled->assets = *assetCache;
    compiled->assetCaches.insert_or_assign (currentOrigin, compiled->assets);
    for (auto& [name, renderer] : compiled->renderers)
    {
        const auto localRenderer = localRenderers.contains (name);
        if (localRenderer) renderer.assets = compiled->assets;
        if (! assignProgramAssetCaches (
                renderer.renderer, compiled->tokens, currentOrigin, localRenderer,
                compiled->assetCaches, report))
            return {};
    }

    if (! validateSlotStyles (*compiled, report)
        || ! validateLayoutStyles (*compiled, report))
        return {};

    if (! package.verifySnapshotDigest (report)) return {};
    return compiled;
}
