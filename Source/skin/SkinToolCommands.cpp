#include "SkinToolCommands.h"
#include "SkinControlStyle.h"

#include "CompiledSkin.h"
#include "SkinBuiltInPackages.h"
#include "SkinCompiler.h"
#include "SkinLayoutEngine.h"
#include "SkinPackage.h"
#include "SkinRenderer.h"
#include "SkinSchemaRegistry.h"
#include "SkinSchemaValidator.h"
#include "app/ProductIdentity.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>

namespace SkinTool
{
namespace
{
constexpr int usageError = 2;
constexpr int validationError = 1;
constexpr int maximumSnapshotDimension = 4096;
const juce::String generatedBy = MoonwakeIdentity::skinSchemaGenerator;

struct LoadedSkin
{
    ~LoadedSkin()
    {
        compiled.reset();
        package.reset();
        if (temporaryRoot != juce::File())
            temporaryRoot.deleteRecursively();
    }

    juce::File temporaryRoot;
    SkinValidationReport report;
    std::optional<SkinPackage> package;
    std::shared_ptr<const CompiledSkin> compiled;
};

struct DocumentationMarker
{
    SkinSchemaDocument schema = SkinSchemaDocument::manifest;
    std::optional<SkinLayoutEntry> route;
    juce::String semantics;
};

class SnapshotFaultSink final : public SkinRuntimeFaultSink
{
public:
    explicit SnapshotFaultSink (SkinValidationReport& reportIn) : report (reportIn) {}

    void reportRenderFault (const SkinDiagnostic& diagnostic,
                            SkinRenderStatus status) override
    {
        if (status == SkinRenderStatus::fatalFault
            || diagnostic.severity == SkinDiagnosticSeverity::error)
            report.addError (diagnostic.code, diagnostic.file, diagnostic.pointer,
                             diagnostic.message, diagnostic.suggestion);
        else
            report.addWarning (diagnostic.code, diagnostic.file, diagnostic.pointer,
                               diagnostic.message, diagnostic.suggestion);
    }

private:
    SkinValidationReport& report;
};

juce::File commandFile (const juce::String& path)
{
    if (juce::File::isAbsolutePath (path))
        return juce::File (path);
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

std::optional<SkinLayoutEntry> workspaceFromName (const juce::String& name)
{
    if (name == "studio") return SkinLayoutEntry::studio;
    if (name == "listening") return SkinLayoutEntry::listening;
    if (name == "effects") return SkinLayoutEntry::effects;
    if (name == "settings") return SkinLayoutEntry::settings;
    return std::nullopt;
}

juce::String workspaceName (SkinLayoutEntry entry)
{
    switch (entry)
    {
        case SkinLayoutEntry::studio: return "studio";
        case SkinLayoutEntry::listening: return "listening";
        case SkinLayoutEntry::effects: return "effects";
        case SkinLayoutEntry::settings: return "settings";
    }
    return {};
}

std::optional<std::pair<int, int>> parseSize (const juce::String& text)
{
    const auto separator = text.indexOfChar ('x');
    if (separator <= 0 || separator != text.lastIndexOfChar ('x'))
        return std::nullopt;
    const auto width = text.substring (0, separator);
    const auto height = text.substring (separator + 1);
    if (width.isEmpty() || height.isEmpty()
        || width.containsOnly ("0123456789") == false
        || height.containsOnly ("0123456789") == false)
        return std::nullopt;
    const auto parsedWidth = width.getIntValue();
    const auto parsedHeight = height.getIntValue();
    if (parsedWidth <= 0 || parsedHeight <= 0
        || parsedWidth > maximumSnapshotDimension
        || parsedHeight > maximumSnapshotDimension)
        return std::nullopt;
    return std::pair (parsedWidth, parsedHeight);
}

ParseResult usage (juce::String message)
{
    return { std::nullopt, "ERROR TOOL001 command#\n" + std::move (message) };
}

ParseResult missingUsage (juce::String message)
{
    return { std::nullopt, "ERROR TOOL002 command#\n" + std::move (message) };
}

bool hasWarnings (const SkinValidationReport& report)
{
    return std::any_of (report.getDiagnostics().begin(),
                        report.getDiagnostics().end(), [] (const auto& diagnostic)
    {
        return diagnostic.severity == SkinDiagnosticSeverity::warning;
    });
}

void appendReport (SkinValidationReport& destination,
                   const SkinValidationReport& source)
{
    for (const auto& diagnostic : source.getDiagnostics())
        if (diagnostic.severity == SkinDiagnosticSeverity::error)
            destination.addError (diagnostic.code, diagnostic.file,
                                  diagnostic.pointer, diagnostic.message,
                                  diagnostic.suggestion);
        else
            destination.addWarning (diagnostic.code, diagnostic.file,
                                    diagnostic.pointer, diagnostic.message,
                                    diagnostic.suggestion);
}

std::shared_ptr<const CompiledSkin> compileFoundation (
    SkinValidationReport& report)
{
    const auto package = SkinBuiltInPackages::create (
        SkinBuiltInPackages::Package::foundation, report);
    return package ? SkinCompiler::compile (*package, {}, report) : nullptr;
}

std::unique_ptr<LoadedSkin> loadSkin (const juce::File& input)
{
    auto loaded = std::make_unique<LoadedSkin>();
#if JUCE_MAC
    const auto temporaryBase = juce::File ("/private/tmp");
#else
    const auto temporaryBase = juce::File::getSpecialLocation (
        juce::File::tempDirectory);
#endif
    loaded->temporaryRoot = temporaryBase.getChildFile (
        "moonwake-skin-tool-" + juce::Uuid().toString());
    const auto foundation = compileFoundation (loaded->report);
    if (foundation == nullptr)
        return loaded;
    loaded->package = SkinPackage::captureSnapshot (
        input, loaded->temporaryRoot, loaded->report);
    if (loaded->package)
        loaded->compiled = SkinCompiler::compile (
            *loaded->package, foundation, loaded->report);
    return loaded;
}

ExecutionResult validationFailure (const SkinValidationReport& report)
{
    return { validationError, {}, report.toText() };
}

bool outputResolvesOutsideInput (const juce::File& input,
                                 const juce::File& output)
{
    std::error_code error;
    const auto inputPath = std::filesystem::weakly_canonical (
        std::filesystem::path (input.getFullPathName().toStdString()), error);
    if (error) return false;
    const auto outputPath = std::filesystem::weakly_canonical (
        std::filesystem::path (output.getFullPathName().toStdString()), error);
    if (error) return false;
    const juce::File canonicalInput (inputPath.string());
    const juce::File canonicalOutput (outputPath.string());
    return canonicalOutput != canonicalInput
        && ! canonicalOutput.isAChildOf (canonicalInput);
}

ExecutionResult validateCommand (const Options& options)
{
    auto loaded = loadSkin (options.input);
    if (loaded->compiled == nullptr || loaded->report.hasErrors()
        || (options.warningsAsErrors && hasWarnings (loaded->report)))
        return validationFailure (loaded->report);

    ExecutionResult result;
    if (! loaded->report.getDiagnostics().empty())
        result.standardError = loaded->report.toText();
    result.standardOutput = "Validated " + loaded->compiled->manifest.id + " "
        + loaded->compiled->manifest.version + " " + loaded->package->sha256() + "\n";
    return result;
}

ExecutionResult packCommand (const Options& options)
{
    if (! options.input.isDirectory())
        return { validationError, {},
                 "ERROR TOOL005 " + options.input.getFullPathName()
                     + "#\nPack input must be a skin development directory.\n" };
    if (options.output.isSymbolicLink()
        || ! outputResolvesOutsideInput (options.input, options.output))
        return { validationError, {},
                 "ERROR TOOL007 " + options.output.getFullPathName()
                     + "#\nPack output must be outside the input and may not be a symbolic link.\n" };
    if (options.output.exists() && ! options.force)
        return { validationError, {},
                 "ERROR TOOL003 " + options.output.getFullPathName()
                     + "#\nOutput already exists; pass --force to replace it.\n" };

    auto loaded = loadSkin (options.input);
    if (loaded->compiled == nullptr || loaded->report.hasErrors())
        return validationFailure (loaded->report);

    auto paths = loaded->package->listFiles();
    paths.sort (false);
    juce::ZipFile::Builder builder;
    const juce::Time normalisedTimestamp (1980, 0, 1, 0, 0, 0, 0, true);
    for (const auto& path : paths)
    {
        const auto bytes = loaded->package->readBinary (path, loaded->report);
        if (loaded->report.hasErrors())
            return validationFailure (loaded->report);
        builder.addEntry (new juce::MemoryInputStream (bytes, true), 9,
                          path, normalisedTimestamp);
    }

    juce::MemoryOutputStream archive;
    if (! builder.writeToStream (archive, nullptr)
        || ! options.output.getParentDirectory().createDirectory()
        || ! options.output.replaceWithData (archive.getData(), archive.getDataSize()))
        return { validationError, {},
                 "ERROR TOOL004 " + options.output.getFullPathName()
                     + "#\nThe reproducible archive could not be written.\n" };

    return { 0, "Packed " + juce::String (paths.size()) + " files to "
                    + options.output.getFullPathName() + "\n", {} };
}

juce::String numberText (float value)
{
    if (std::abs (value - std::round (value)) < 0.0005f)
        return juce::String (juce::roundToInt (value));
    auto text = juce::String (value, 3);
    while (text.endsWithChar ('0')) text = text.dropLastCharacters (1);
    if (text.endsWithChar ('.')) text = text.dropLastCharacters (1);
    return text;
}

void appendLayoutComponents (const SkinResolvedNode& node, juce::String& output)
{
    if (node.visible && node.type == SkinLayoutNodeType::component)
    {
        output << node.componentId << " " << numberText (node.bounds.getX())
               << " " << numberText (node.bounds.getY())
               << " " << numberText (node.bounds.getWidth())
               << " " << numberText (node.bounds.getHeight())
               << " " << node.focusOrder << " "
               << (node.style.isEmpty() ? "-" : node.style) << "\n";
    }
    for (const auto& child : node.children)
        appendLayoutComponents (child, output);
}

std::optional<SkinResolvedLayout> resolveLayout (
    const CompiledSkin& skin, const Options& options,
    SkinValidationReport& report)
{
    if (! options.workspace)
        return std::nullopt;
    const auto found = skin.layouts.find (*options.workspace);
    if (found == skin.layouts.end())
    {
        report.addError ("TOOL006", "layout", {},
                         "Requested workspace is unavailable in the compiled skin.");
        return std::nullopt;
    }
    return SkinLayoutEngine::resolve (
        found->second,
        juce::Rectangle<float> (0.0f, 0.0f,
                                static_cast<float> (options.width),
                                static_cast<float> (options.height)),
        1.0f, report, options.theme, &skin);
}

ExecutionResult layoutCommand (const Options& options)
{
    auto loaded = loadSkin (options.input);
    if (loaded->compiled == nullptr || loaded->report.hasErrors())
        return validationFailure (loaded->report);
    const auto resolved = resolveLayout (*loaded->compiled, options, loaded->report);
    if (! resolved || loaded->report.hasErrors())
        return validationFailure (loaded->report);
    juce::String output;
    appendLayoutComponents (resolved->root, output);
    return { 0, output, {} };
}

std::optional<SkinExpressionValue> representativeValue (
    std::string_view path,
    const std::map<juce::String, SkinExpressionType>& types)
{
    const auto key = juce::String::fromUTF8 (
        path.data(), static_cast<int> (path.size()));
    const auto found = types.find (key);
    if (found == types.end()) return std::nullopt;
    switch (found->second)
    {
        case SkinExpressionType::number: return SkinExpressionValue (0.62);
        case SkinExpressionType::boolean: return SkinExpressionValue (key != "combo.popupOpen");
        case SkinExpressionType::string:
            if (key == "plugin.format") return SkinExpressionValue (juce::String ("VST3"));
            if (key == "output.layout") return SkinExpressionValue (juce::String ("5.1"));
            if (key == "meter.mode") return SkinExpressionValue (juce::String ("rms"));
            if (key == "meter.status") return SkinExpressionValue (juce::String ("Meter preview"));
            return SkinExpressionValue (juce::String ("Preview"));
        case SkinExpressionType::colour:
            return SkinExpressionValue (juce::Colours::white);
    }
    return std::nullopt;
}

bool drawStyle (juce::Graphics& graphics, const CompiledSkin& skin,
                const SkinRenderStyleEntry& style,
                const SkinResolvedNode& node, bool required,
                bool selected, SkinTool::SnapshotState snapshotState,
                std::optional<int> representativeFocusOrder,
                SkinValidationReport& report)
{
    juce::Graphics::ScopedSaveState localState (graphics);
    graphics.addTransform (juce::AffineTransform::translation (
        node.bounds.getX(), node.bounds.getY()));
    SkinRenderContext context;
    context.bounds = juce::Rectangle<float> (
        0.0f, 0.0f, node.bounds.getWidth(), node.bounds.getHeight());
    context.contentBounds = context.bounds;
    context.scaleFactor = 1.0f;
    context.state.enabled = true;
    context.state.selected = selected;
    context.state.focused = representativeFocusOrder
        && node.focusOrder == *representativeFocusOrder;
    if (snapshotState != SkinTool::SnapshotState::representative)
    {
        context.state.enabled = snapshotState != SkinTool::SnapshotState::disabled;
        context.state.hover = snapshotState == SkinTool::SnapshotState::hover;
        context.state.pressed = snapshotState == SkinTool::SnapshotState::pressed;
        context.state.selected = snapshotState == SkinTool::SnapshotState::selected;
        context.state.focused = snapshotState == SkinTool::SnapshotState::focused;
    }
    context.normalisedValue = 0.62;
    context.minimum = 0.0;
    context.maximum = 1.0;
    context.primaryText = node.componentId.fromLastOccurrenceOf (".", false, false)
                              .replaceCharacter ('_', ' ');
    if (context.primaryText.isEmpty()) context.primaryText = "Preview";
    context.secondaryText = "Skin preview";
    const auto* descriptor = PlayerComponentCatalog::find (node.componentId);
    if (descriptor != nullptr)
    {
        context.textAlignment = descriptor->textAlignment;
        context.iconName = descriptor->iconName;
        context.iconPathData = PlayerComponentCatalog::fallbackIconPathData (
            context.iconName);
        context.iconPath = PlayerComponentCatalog::fallbackIconPath (
            context.iconName);
    }
    context.displayValue = "62%";
    context.tableRowCount = 4;
    context.tableColumnCount = 3;
    context.repeatItemCount = 6;
    context.repeatLabels = { "L", "R", "C", "LFE", "Ls", "Rs" };
    context.meterLabels = context.repeatLabels;
    for (int i = 0; i < 6; ++i)
    {
        context.meterPeaks[static_cast<size_t> (i)] = 0.28f + 0.08f * static_cast<float> (i);
        context.meterIsLfe[static_cast<size_t> (i)] = i == 3;
    }
    const auto types = skinRenderContextForTarget (style.renderer.target);
    context.extraLookup = [&types] (std::string_view path)
    {
        return representativeValue (path, types);
    };

    SnapshotFaultSink sink (report);
    SkinRenderer renderer (style.assets != nullptr ? style.assets : skin.assets);
    const auto status = renderer.draw (
        graphics, style.renderer, context, required, sink);
    if (status == SkinRenderStatus::fatalFault
        || descriptor == nullptr || context.iconName.isEmpty()
        || context.iconPathData.isEmpty())
        return status != SkinRenderStatus::fatalFault;

    const auto slotName = "icon." + context.iconName;
    const auto mapped = style.slotStyles.find (slotName);
    if (mapped == style.slotStyles.end()) return true;
    const auto* iconStyle = skin.resolveStyle (
        SkinRenderTarget::surface, mapped->second);
    if (iconStyle == nullptr) return false;

    auto iconContext = context;
    const auto* controlOwner = &style;
    if (! style.renderer.controlStyle)
        controlOwner = skin.resolveStyle (SkinRenderTarget::textButton, "default");
    const auto control = controlOwner != nullptr
        ? resolveSkinControlStyle (controlOwner->renderer, context, report) : std::nullopt;
    if (! control || ! control->iconBounds) return false;
    iconContext.bounds = *control->iconBounds;
    iconContext.contentBounds = iconContext.bounds;
    SkinRenderer iconRenderer (
        iconStyle->assets != nullptr ? iconStyle->assets : skin.assets);
    return iconRenderer.draw (graphics, iconStyle->renderer, iconContext,
                              required, sink) != SkinRenderStatus::fatalFault;
}

bool drawResolvedNode (juce::Graphics& graphics, const CompiledSkin& skin,
                       const SkinResolvedNode& node, SkinLayoutEntry workspace,
                       WorkspaceMode theme,
                       SkinTool::SnapshotState snapshotState,
                       std::optional<int> representativeFocusOrder,
                       SkinValidationReport& report)
{
    if (! node.visible) return true;
    graphics.saveState();
    graphics.reduceClipRegion (node.clipBounds.toNearestInt());
    graphics.beginTransparencyLayer (juce::jlimit (0.0f, 1.0f, node.opacity));

    bool ok = true;
    if (node.type != SkinLayoutNodeType::component && node.style.isNotEmpty())
    {
        const auto* style = skin.resolveStyle (SkinRenderTarget::surface, node.style);
        ok = style != nullptr && drawStyle (graphics, skin, *style, node,
                                            false, false, snapshotState,
                                            representativeFocusOrder, report);
    }
    else if (node.type == SkinLayoutNodeType::component)
    {
        const auto* descriptor = PlayerComponentCatalog::find (node.componentId);
        if (descriptor == nullptr)
            ok = false;
        else
        {
            auto targetName = descriptor->defaultRendererTarget;
            if (node.presentation.isNotEmpty())
                if (const auto found = descriptor->rendererTargetByPresentation.find (
                        node.presentation);
                    found != descriptor->rendererTargetByPresentation.end())
                    targetName = found->second;
            const auto target = skinRenderTargetFromName (targetName);
            const auto* style = target ? skin.resolveStyle (*target, node.style) : nullptr;
            const auto selected = node.componentId == "workspace." + toString (theme);
            ok = style != nullptr
                && drawStyle (graphics, skin, *style, node,
                              descriptor->requiredIn.contains (workspace),
                              selected, snapshotState,
                              representativeFocusOrder, report);
        }
    }

    for (const auto& child : node.children)
        ok = drawResolvedNode (graphics, skin, child, workspace, theme,
                               snapshotState, representativeFocusOrder,
                               report) && ok;

    graphics.endTransparencyLayer();
    graphics.restoreState();
    return ok;
}

std::optional<int> firstVisibleFocusOrder (const SkinResolvedNode& node)
{
    if (! node.visible) return std::nullopt;
    std::optional<int> result;
    if (node.type == SkinLayoutNodeType::component && node.focusOrder > 0)
        result = node.focusOrder;
    for (const auto& child : node.children)
        if (const auto candidate = firstVisibleFocusOrder (child);
            candidate && (! result || *candidate < *result))
            result = candidate;
    return result;
}

ExecutionResult snapshotCommand (const Options& options)
{
    if (options.output.isSymbolicLink())
        return { validationError, {},
                 "ERROR TOOL007 " + options.output.getFullPathName()
                     + "#\nSnapshot output may not be a symbolic link.\n" };
    if (options.output.exists() && ! options.force)
        return { validationError, {},
                 "ERROR TOOL003 " + options.output.getFullPathName()
                     + "#\nOutput already exists; pass --force to replace it.\n" };
    auto loaded = loadSkin (options.input);
    if (loaded->compiled == nullptr || loaded->report.hasErrors())
        return validationFailure (loaded->report);
    const auto resolved = resolveLayout (*loaded->compiled, options, loaded->report);
    if (! resolved || loaded->report.hasErrors())
        return validationFailure (loaded->report);

    juce::Image image (juce::Image::ARGB, options.width, options.height, true);
    juce::Graphics graphics (image);
    const auto representativeFocusOrder = firstVisibleFocusOrder (resolved->root);
    if (! drawResolvedNode (graphics, *loaded->compiled, resolved->root,
                            *options.workspace,
                            options.theme.value_or (loaded->compiled->layouts.at (*options.workspace).defaultWorkspace),
                            options.snapshotState,
                            representativeFocusOrder,
                            loaded->report)
        || loaded->report.hasErrors())
        return validationFailure (loaded->report);

    juce::MemoryOutputStream encoded;
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, encoded)
        || ! options.output.getParentDirectory().createDirectory()
        || ! options.output.replaceWithData (encoded.getData(), encoded.getDataSize()))
        return { validationError, {},
                 "ERROR TOOL004 " + options.output.getFullPathName()
                     + "#\nSnapshot PNG could not be written.\n" };
    return { 0, "Snapshot " + workspaceName (*options.workspace) + " "
                    + juce::String (options.width) + "x" + juce::String (options.height)
                    + " -> " + options.output.getFullPathName() + "\n", {} };
}

juce::String schemaFileName (SkinSchemaDocument document)
{
    switch (document)
    {
        case SkinSchemaDocument::manifest: return "manifest.schema.json";
        case SkinSchemaDocument::tokens: return "tokens.schema.json";
        case SkinSchemaDocument::componentIndex: return "component-index.schema.json";
        case SkinSchemaDocument::layout: return "layout.schema.json";
        case SkinSchemaDocument::render: return "render.schema.json";
    }
    return {};
}

std::optional<SkinSchemaDocument> schemaFromName (const juce::String& name)
{
    if (name == "manifest") return SkinSchemaDocument::manifest;
    if (name == "tokens") return SkinSchemaDocument::tokens;
    if (name == "component-index") return SkinSchemaDocument::componentIndex;
    if (name == "layout") return SkinSchemaDocument::layout;
    if (name == "render") return SkinSchemaDocument::render;
    return std::nullopt;
}

bool isGeneratedSchema (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    juce::var value;
    const auto parseResult = juce::JSON::parse (file.loadFileAsString(), value);
    return parseResult.wasOk() && value.isObject()
        && value.getProperty ("x-generatedBy", {}).toString() == generatedBy
        && static_cast<int> (value.getProperty ("x-skinApi", 0)) == 1;
}

ExecutionResult schemaCommand (const Options& options)
{
    if (options.outputDirectory.isSymbolicLink())
        return { validationError, {},
                 "ERROR TOOL007 " + options.outputDirectory.getFullPathName()
                     + "#\nSchema output directory may not be a symbolic link.\n" };
    const auto schemas = SkinSchemaRegistry::serialisedSchemas();
    for (const auto& [document, ignored] : schemas)
    {
        juce::ignoreUnused (ignored);
        const auto target = options.outputDirectory.getChildFile (
            schemaFileName (document));
        if (target.exists() && ! options.force && ! isGeneratedSchema (target))
            return { validationError, {},
                     "ERROR TOOL003 " + target.getFullPathName()
                         + "#\nRefusing to overwrite a non-generated schema file.\n" };
    }
    if (! options.outputDirectory.createDirectory())
        return { validationError, {},
                 "ERROR TOOL004 " + options.outputDirectory.getFullPathName()
                     + "#\nSchema output directory could not be created.\n" };
    for (const auto& [document, json] : schemas)
    {
        const auto target = options.outputDirectory.getChildFile (
            schemaFileName (document));
        if (! target.replaceWithText (json + "\n"))
            return { validationError, {},
                     "ERROR TOOL004 " + target.getFullPathName()
                         + "#\nSchema file could not be written.\n" };
    }
    return { 0, "Generated 5 Skin API 1 schemas in "
                    + options.outputDirectory.getFullPathName() + "\n", {} };
}

std::optional<DocumentationMarker> parseMarker (const juce::String& line)
{
    const auto prefix = juce::String ("<!-- skin-doc-test: ");
    const auto suffix = juce::String (" -->");
    if (! line.startsWith (prefix) || ! line.endsWith (suffix))
        return std::nullopt;
    const auto body = line.substring (prefix.length(),
                                      line.length() - suffix.length());
    auto fields = juce::StringArray::fromTokens (body, " ", "");
    fields.removeEmptyStrings();
    std::map<juce::String, juce::String> values;
    for (const auto& field : fields)
    {
        const auto equals = field.indexOfChar ('=');
        if (equals <= 0 || equals == field.length() - 1)
            return std::nullopt;
        values.insert_or_assign (field.substring (0, equals),
                                 field.substring (equals + 1));
    }
    if (values.size() != 3 || ! values.contains ("schema")
        || ! values.contains ("route") || ! values.contains ("semantics"))
        return std::nullopt;
    const auto schema = schemaFromName (values.at ("schema"));
    if (! schema || (values.at ("semantics") != "schema"
                    && values.at ("semantics") != "complete"))
        return std::nullopt;
    DocumentationMarker marker;
    marker.schema = *schema;
    marker.route = workspaceFromName (values.at ("route"));
    if (*schema == SkinSchemaDocument::layout && ! marker.route)
        return std::nullopt;
    marker.semantics = values.at ("semantics");
    return marker;
}

bool validateCompleteDocumentationBlock (
    const juce::var& raw, const DocumentationMarker& marker,
    std::shared_ptr<const CompiledSkin> foundation,
    SkinValidationReport& report)
{
    if (marker.schema != SkinSchemaDocument::layout || ! marker.route)
    {
        report.addError ("TOOL008", "guide", {},
                         "Complete semantics currently require a layout route.");
        return false;
    }
    const auto route = workspaceName (*marker.route);
    const auto manifest = juce::String (R"json({
      "skinApi":1,
      "id":"com.samkuler.moonwake.documentation.)json") + route + R"json(",
      "name":"Documentation Fixture",
      "version":"1.0.0",
      "author":{"name":"Sam Kuler"},
      "license":"Proprietary",
      "minAppVersion":"0.1.0",
      "extends":"builtin.foundation",
      "capabilities":["layouts"],
      "entrypoints":{"layouts":{")json" + route + R"json(":"doc-layout.json"}}
    })json";
    std::vector<SkinPackage::EmbeddedFile> files;
    files.push_back ({ "manifest.json", juce::MemoryBlock (
        manifest.toRawUTF8(), manifest.getNumBytesAsUTF8()) });
    const auto layout = juce::JSON::toString (raw, true);
    files.push_back ({ "doc-layout.json", juce::MemoryBlock (
        layout.toRawUTF8(), layout.getNumBytesAsUTF8()) });
    const auto package = SkinPackage::fromEmbedded (
        "documentation-" + route, std::move (files), report);
    return package && SkinCompiler::compile (*package, std::move (foundation), report)
        != nullptr;
}

ExecutionResult verifyDocsCommand (const Options& options)
{
    SkinValidationReport report;
    if (! options.guide.existsAsFile() || options.guide.isSymbolicLink())
        report.addError ("TOOL008", options.guide.getFullPathName(), {},
                         "Developer guide must be a readable regular file.");
    const auto foundation = compileFoundation (report);
    if (report.hasErrors() || foundation == nullptr)
        return validationFailure (report);

    const auto lines = juce::StringArray::fromLines (
        options.guide.loadFileAsString());
    for (int index = 0; index < lines.size(); ++index)
        if (lines[index].startsWith ("<!-- skin-doc-test:"))
        {
            if (! parseMarker (lines[index]))
                report.addError (
                    "TOOL008", options.guide.getFullPathName(),
                    "/line/" + juce::String (index + 1),
                    "skin-doc-test marker is malformed or uses an unknown value.");
            else if (index + 1 >= lines.size()
                     || (lines[index + 1] != "```json"
                         && lines[index + 1] != "~~~json"))
                report.addError (
                    "TOOL008", options.guide.getFullPathName(),
                    "/line/" + juce::String (index + 1),
                    "skin-doc-test marker must be immediately followed by an exact json fence.");
        }
    int jsonBlocks = 0;
    int annotatedBlocks = 0;
    for (int index = 0; index < lines.size(); ++index)
    {
        const auto opening = lines[index];
        if (opening != "```json" && opening != "~~~json")
            continue;
        ++jsonBlocks;
        const auto closing = opening.startsWith ("```") ? juce::String ("```")
                                                         : juce::String ("~~~");
        const auto firstLine = index + 2;
        juce::String block;
        int cursor = index + 1;
        for (; cursor < lines.size() && lines[cursor] != closing; ++cursor)
            block << lines[cursor] << "\n";
        if (cursor == lines.size())
        {
            report.addError ("TOOL008", options.guide.getFullPathName(),
                             "/line/" + juce::String (firstLine),
                             "JSON fence is not closed.");
            break;
        }

        juce::var raw;
        const auto parseResult = juce::JSON::parse (block, raw);
        if (parseResult.failed())
        {
            report.addError ("TOOL008", options.guide.getFullPathName(),
                             "/line/" + juce::String (firstLine),
                             "Fenced JSON is not one complete value: "
                                 + parseResult.getErrorMessage());
            index = cursor;
            continue;
        }

        std::optional<DocumentationMarker> marker;
        if (index > 0)
            marker = parseMarker (lines[index - 1]);
        if (marker)
        {
            ++annotatedBlocks;
            const auto validated = SkinSchemaValidator::validate (
                marker->schema, raw, options.guide.getFileName()
                    + ":" + juce::String (firstLine), report);
            if (validated && marker->semantics == "complete")
                validateCompleteDocumentationBlock (
                    validated->value(), *marker, foundation, report);
        }
        index = cursor;
    }

    auto example = loadSkin (options.example);
    appendReport (report, example->report);
    if (example->compiled == nullptr && ! report.hasErrors())
        report.addError ("TOOL008", options.example.getFullPathName(), {},
                         "Complete example did not produce a compiled skin.");
    if (report.hasErrors())
        return validationFailure (report);
    return { 0, "Verified " + juce::String (jsonBlocks) + " JSON blocks ("
                    + juce::String (annotatedBlocks) + " annotated) and example "
                    + example->compiled->manifest.id + "\n", {} };
}
} // namespace

juce::String helpText()
{
    return R"text(MoonwakeSkinTool - Moonwake Skin API 1 developer utility

Usage:
  MoonwakeSkinTool validate <path> [--warnings-as-errors]
  MoonwakeSkinTool pack <directory> --output <file.moonwake-skin> [--force]
  MoonwakeSkinTool layout <path> --workspace <studio|listening|effects|settings> --size <WxH> [--theme <studio|listening>]
  MoonwakeSkinTool snapshot <path> --workspace <studio|listening|effects|settings> --size <WxH> --output <file.png> [--theme <studio|listening>] [--state <representative|normal|hover|pressed|selected|focused|disabled>] [--force]
  MoonwakeSkinTool schema --output-dir <directory> [--force]
  MoonwakeSkinTool verify-docs --guide <file.md> --example <skin-directory>

Exit codes: 0 success, 1 validation or I/O failure, 2 command usage error.
)text";
}

ParseResult parse (const juce::StringArray& arguments)
{
    if (arguments.isEmpty() || arguments[0] == "help" || arguments[0] == "--help"
        || arguments[0] == "-h")
        return { Options {}, {} };

    Options options;
    const auto command = arguments[0];
    if (command == "validate") options.command = Command::validate;
    else if (command == "pack") options.command = Command::pack;
    else if (command == "layout") options.command = Command::layout;
    else if (command == "snapshot") options.command = Command::snapshot;
    else if (command == "schema") options.command = Command::schema;
    else if (command == "verify-docs") options.command = Command::verifyDocs;
    else return usage ("Unknown command: " + command);

    bool hasInput = false, hasOutput = false, hasOutputDirectory = false;
    bool hasWorkspace = false, hasSize = false, hasGuide = false, hasExample = false;
    bool hasSnapshotState = false, hasTheme = false;
    for (int index = 1; index < arguments.size(); ++index)
    {
        const auto argument = arguments[index];
        const auto takeValue = [&] (juce::String& value)
        {
            if (index + 1 >= arguments.size()) return false;
            value = arguments[++index];
            return value.isNotEmpty() && ! value.startsWith ("--");
        };

        if (argument == "--warnings-as-errors" && options.command == Command::validate
            && ! options.warningsAsErrors)
        {
            options.warningsAsErrors = true;
            continue;
        }
        if (argument == "--force"
            && (options.command == Command::pack || options.command == Command::snapshot
                || options.command == Command::schema)
            && ! options.force)
        {
            options.force = true;
            continue;
        }
        juce::String value;
        if (argument == "--output"
            && (options.command == Command::pack || options.command == Command::snapshot)
            && ! hasOutput)
        {
            if (! takeValue (value)) return missingUsage ("--output requires a path");
            options.output = commandFile (value);
            hasOutput = true;
            continue;
        }
        if (argument == "--output-dir" && options.command == Command::schema
            && ! hasOutputDirectory)
        {
            if (! takeValue (value)) return missingUsage ("--output-dir requires a path");
            options.outputDirectory = commandFile (value);
            hasOutputDirectory = true;
            continue;
        }
        if (argument == "--workspace"
            && (options.command == Command::layout || options.command == Command::snapshot)
            && ! hasWorkspace)
        {
            if (! takeValue (value)) return missingUsage ("--workspace requires a value");
            options.workspace = workspaceFromName (value);
            if (! options.workspace) return usage ("Invalid workspace: " + value);
            hasWorkspace = true;
            continue;
        }
        if (argument == "--size"
            && (options.command == Command::layout || options.command == Command::snapshot)
            && ! hasSize)
        {
            if (! takeValue (value)) return missingUsage ("--size requires WxH");
            const auto size = parseSize (value);
            if (! size) return usage ("Invalid size: " + value);
            options.width = size->first;
            options.height = size->second;
            hasSize = true;
            continue;
        }
        if (argument == "--theme"
            && (options.command == Command::layout || options.command == Command::snapshot)
            && ! hasTheme)
        {
            if (! takeValue (value)) return missingUsage ("--theme requires a value");
            if (value != "studio" && value != "listening") return usage ("Invalid theme: " + value);
            options.theme = workspaceModeFromString (value);
            hasTheme = true;
            continue;
        }
        if (argument == "--state" && options.command == Command::snapshot
            && ! hasSnapshotState)
        {
            if (! takeValue (value)) return missingUsage ("--state requires a value");
            if (value == "representative")
                options.snapshotState = SnapshotState::representative;
            else if (value == "normal") options.snapshotState = SnapshotState::normal;
            else if (value == "hover") options.snapshotState = SnapshotState::hover;
            else if (value == "pressed") options.snapshotState = SnapshotState::pressed;
            else if (value == "selected") options.snapshotState = SnapshotState::selected;
            else if (value == "focused") options.snapshotState = SnapshotState::focused;
            else if (value == "disabled") options.snapshotState = SnapshotState::disabled;
            else return usage ("Invalid snapshot state: " + value);
            hasSnapshotState = true;
            continue;
        }
        if (argument == "--guide" && options.command == Command::verifyDocs
            && ! hasGuide)
        {
            if (! takeValue (value)) return missingUsage ("--guide requires a path");
            options.guide = commandFile (value);
            hasGuide = true;
            continue;
        }
        if (argument == "--example" && options.command == Command::verifyDocs
            && ! hasExample)
        {
            if (! takeValue (value)) return missingUsage ("--example requires a path");
            options.example = commandFile (value);
            hasExample = true;
            continue;
        }
        if (! argument.startsWith ("--")
            && (options.command == Command::validate || options.command == Command::pack
                || options.command == Command::layout || options.command == Command::snapshot)
            && ! hasInput)
        {
            options.input = commandFile (argument);
            hasInput = true;
            continue;
        }
        return usage ("Unknown or duplicate option: " + argument);
    }

    if ((options.command == Command::validate || options.command == Command::pack
         || options.command == Command::layout || options.command == Command::snapshot)
        && ! hasInput)
        return missingUsage ("Command requires an input path");
    if ((options.command == Command::pack || options.command == Command::snapshot)
        && ! hasOutput)
        return missingUsage ("Command requires --output");
    if (options.command == Command::pack
        && ! options.output.hasFileExtension (MoonwakeIdentity::skinArchiveExtension))
        return usage ("Pack output must use the "
                      + juce::String (MoonwakeIdentity::skinArchiveExtension)
                      + " extension");
    if (options.command == Command::snapshot
        && ! options.output.hasFileExtension ("png"))
        return usage ("Snapshot output must use the .png extension");
    if ((options.command == Command::layout || options.command == Command::snapshot)
        && (! hasWorkspace || ! hasSize))
        return missingUsage ("Command requires --workspace and --size");
    if (options.command == Command::schema && ! hasOutputDirectory)
        return missingUsage ("Schema command requires --output-dir");
    if (options.command == Command::verifyDocs && (! hasGuide || ! hasExample))
        return missingUsage ("verify-docs requires --guide and --example");
    return { options, {} };
}

ExecutionResult execute (const Options& options)
{
    switch (options.command)
    {
        case Command::help: return { 0, helpText(), {} };
        case Command::validate: return validateCommand (options);
        case Command::pack: return packCommand (options);
        case Command::layout: return layoutCommand (options);
        case Command::snapshot: return snapshotCommand (options);
        case Command::schema: return schemaCommand (options);
        case Command::verifyDocs: return verifyDocsCommand (options);
    }
    return { usageError, {}, "Unknown command state.\n" };
}

ExecutionResult run (const juce::StringArray& arguments)
{
    const auto parsed = parse (arguments);
    if (! parsed.options)
        return { usageError, {}, parsed.error + "\n\n" + helpText() };
    return execute (*parsed.options);
}
} // namespace SkinTool
