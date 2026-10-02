#include "PlayerComponentCatalog.h"

#include "SkinRenderDefinition.h"

#include <algorithm>

namespace
{
juce::String targetFor (SkinComponentKind kind)
{
    switch (kind)
    {
        case SkinComponentKind::label: return "label";
        case SkinComponentKind::textButton: return "textButton";
        case SkinComponentKind::toggleButton: return "toggleButton";
        case SkinComponentKind::comboBox: return "comboBox";
        case SkinComponentKind::textEditor: return "textEditor";
        case SkinComponentKind::linearSlider:
        case SkinComponentKind::slider: return "linearSlider";
        case SkinComponentKind::progressBar: return "progressBar";
        case SkinComponentKind::table: return "table";
        case SkinComponentKind::artwork: return "artwork";
        case SkinComponentKind::effectChain: return "effectChain";
        case SkinComponentKind::outputStage: return "outputStage";
        case SkinComponentKind::channelMap: return "channelMap";
        case SkinComponentKind::channelMixer: return "channelMixer";
        case SkinComponentKind::pluginList: return "pluginList";
        case SkinComponentKind::skinList: return "skinList";
    }
    return {};
}

bool isInteractive (SkinComponentKind kind)
{
    switch (kind)
    {
        case SkinComponentKind::label:
        case SkinComponentKind::artwork:
        case SkinComponentKind::outputStage:
        case SkinComponentKind::progressBar:
            return false;
        case SkinComponentKind::textButton:
        case SkinComponentKind::toggleButton:
        case SkinComponentKind::comboBox:
        case SkinComponentKind::textEditor:
        case SkinComponentKind::linearSlider:
        case SkinComponentKind::slider:
        case SkinComponentKind::table:
        case SkinComponentKind::effectChain:
        case SkinComponentKind::channelMixer:
        case SkinComponentKind::channelMap:
        case SkinComponentKind::pluginList:
        case SkinComponentKind::skinList:
            return true;
    }
    return false;
}

SkinComponentDescriptor makeDescriptor (
    juce::String id, SkinComponentKind kind,
    std::set<SkinLayoutEntry> required = {},
    juce::StringArray presentations = {},
    std::map<juce::String, juce::String> targets = {},
    std::vector<SkinComponentDescriptor::Slot> slots = {})
{
    return { std::move (id), kind, targetFor (kind), std::move (targets),
             std::move (slots), std::move (required), isInteractive (kind),
             std::move (presentations), "left", {}, {} };
}

SkinComponentDescriptor makeReadOnlyDescriptor (
    juce::String id, SkinComponentKind kind,
    std::vector<SkinComponentDescriptor::Slot> slots = {})
{
    auto result = makeDescriptor (std::move (id), kind, {}, {}, {},
                                  std::move (slots));
    result.interactive = false;
    return result;
}

SkinComponentDescriptor makeLabelDescriptor (
    juce::String id, juce::String alignment = "left",
    std::set<SkinLayoutEntry> required = {})
{
    auto result = makeDescriptor (std::move (id), SkinComponentKind::label,
                                  std::move (required));
    result.textAlignment = std::move (alignment);
    return result;
}

SkinComponentDescriptor makeIconDescriptor (
    juce::String id, juce::String iconName,
    std::set<SkinLayoutEntry> required = {},
    juce::String iconPresentation = "only")
{
    auto result = makeDescriptor (std::move (id), SkinComponentKind::textButton,
                                  std::move (required));
    result.iconName = std::move (iconName);
    result.iconPresentation = std::move (iconPresentation);
    result.slots.push_back ({ "icon." + result.iconName, "surface" });
    if (result.iconName == "play")
        result.slots.push_back ({ "icon.pause", "surface" });
    return result;
}

std::set<SkinLayoutEntry> workspaces()
{
    return { SkinLayoutEntry::studio, SkinLayoutEntry::listening };
}

SkinComponentDescriptor makeImportDescriptor()
{
    auto result = makeIconDescriptor ("library.add", "add", workspaces());
    result.slots.push_back ({ "icon.close", "surface" });
    return result;
}

const std::map<juce::String, juce::String>& iconPathData()
{
    static const std::map<juce::String, juce::String> paths {
        { "add", "M12 4 L12 20 M4 12 L20 12" },
        { "settings", "M4 7 L9 7 M15 7 L20 7 M12 4 L12 10 M4 17 L13 17 M17 17 L20 17 M15 14 L15 20" },
        { "effects", "M4 9 L4 15 M8 5 L8 19 M12 8 L12 16 M16 4 L16 20 M20 9 L20 15" },
        { "close", "M5 5 L19 19 M19 5 L5 19" },
        { "previous", "M18 5 L9 12 L18 19 L18 5 M7 5 L7 19" },
        { "play", "M8 5 L19 12 L8 19 L8 5" },
        { "pause", "M9 5 L9 19 M15 5 L15 19" },
        { "next", "M6 5 L15 12 L6 19 L6 5 M17 5 L17 19" },
        { "remove", "M7 7 L17 7 L16 20 L8 20 L7 7 M5 7 L19 7 M9 4 L15 4 M10 10 L10 17 M14 10 L14 17" },
        { "moveUp", "M5 14 L12 7 L19 14 M12 7 L12 20" },
        { "moveDown", "M5 10 L12 17 L19 10 M12 4 L12 17" },
        { "scan", "M18 8 C16 4 10 3 6 6 C2 10 4 17 9 19 C13 21 18 18 19 14 M18 4 L18 9 L13 9" },
        { "folderSearch", "M3 7 L10 7 L12 9 L21 9 L21 16 M3 7 L3 19 L12 19 M15 16 C15 14 17 13 19 14 C21 15 21 18 19 19 C17 20 15 18 15 16 M20 19 L22 21" },
        { "import", "M12 4 L12 15 M7 10 L12 15 L17 10 M5 18 L5 20 L19 20 L19 18" },
        { "reload", "M19 8 C17 4 11 3 7 6 C3 9 4 16 8 19 C12 22 18 19 19 14 M19 4 L19 9 L14 9" },
        { "preview", "M3 12 C7 6 17 6 21 12 C17 18 7 18 3 12 M9 12 C9 8 15 8 15 12 C15 16 9 16 9 12" }
    };
    return paths;
}

const std::vector<SkinComponentDescriptor>& catalog()
{
    using Kind = SkinComponentKind;
    using Entry = SkinLayoutEntry;
    using Slot = SkinComponentDescriptor::Slot;
    static const std::vector<SkinComponentDescriptor> values {
        makeLabelDescriptor ("app.brand.eyebrow"),
        makeLabelDescriptor ("app.brand.title"),
        makeLabelDescriptor ("app.deviceStatus", "right"),
        makeLabelDescriptor ("app.status", "left", workspaces()),
        makeDescriptor ("workspace.studio", Kind::textButton, workspaces()),
        makeDescriptor ("workspace.listening", Kind::textButton, workspaces()),
        makeIconDescriptor ("settings.open", "settings", workspaces()),

        makeImportDescriptor(),
        makeDescriptor ("library.search", Kind::textEditor),
        makeLabelDescriptor ("queue.title"),
        makeLabelDescriptor ("queue.count", "right"),
        makeDescriptor ("queue.table", Kind::table, workspaces(), { "table", "list" },
                        { { "table", "table" }, { "list", "table" } }, {
            Slot { "header.surface", "surface" }, Slot { "header.text", "label" },
            Slot { "row.surface", "surface" }, Slot { "cell.title", "label" },
            Slot { "cell.location", "label" }, Slot { "cell.duration", "label" }
        }),
        makeLabelDescriptor ("queue.empty", "center"),
        makeIconDescriptor ("queue.remove", "remove"),
        makeDescriptor ("queue.clear", Kind::textButton),
        makeIconDescriptor ("queue.moveUp", "moveUp"),
        makeIconDescriptor ("queue.moveDown", "moveDown"),

        makeDescriptor ("artwork.cover", Kind::artwork, {},
                        { "contain", "cover" },
                        { { "contain", "artwork" }, { "cover", "artwork" } }, {
            Slot { "frame.background", "surface" }, Slot { "frame.overlay", "surface" }
        }),
        makeLabelDescriptor ("artwork.metadata", "center"),
        makeLabelDescriptor ("nowPlaying.eyebrow"),
        makeLabelDescriptor ("nowPlaying.title"),
        makeLabelDescriptor ("nowPlaying.metadata"),

        makeIconDescriptor ("transport.previous", "previous", workspaces()),
        makeIconDescriptor ("transport.playPause", "play", workspaces()),
        makeIconDescriptor ("transport.next", "next", workspaces()),
        makeDescriptor ("transport.seek", Kind::linearSlider, workspaces(),
                        { "linearHorizontal" }, { { "linearHorizontal", "linearSlider" } }),
        makeLabelDescriptor ("transport.time", "center", workspaces()),
        makeDescriptor ("transport.shuffle", Kind::toggleButton),
        makeDescriptor ("transport.repeat", Kind::comboBox),
        makeLabelDescriptor ("transport.volumeLabel", "right"),
        makeLabelDescriptor ("transport.volumeValue", "right"),
        makeDescriptor ("transport.volume", Kind::slider, workspaces(),
                        { "linearHorizontal", "rotary" },
                        { { "linearHorizontal", "linearSlider" },
                          { "rotary", "rotarySlider" } }),

        makeIconDescriptor ("effects.open", "effects"),
        makeIconDescriptor ("effects.close", "close"),
        makeLabelDescriptor ("effects.summary"),
        makeLabelDescriptor ("effects.title"),
        makeLabelDescriptor ("effects.filterLabel"),
        makeDescriptor ("effects.filter", Kind::textEditor, { Entry::effects }),
        makeDescriptor ("effects.catalog", Kind::comboBox, { Entry::effects }),
        makeDescriptor ("effects.scope", Kind::comboBox),
        makeLabelDescriptor ("effects.scopeLabel"),
        makeIconDescriptor ("effects.add", "add", { Entry::effects }),
        makeDescriptor ("effects.chain", Kind::effectChain, { Entry::effects }, {}, {}, {
            Slot { "slot.surface", "surface" }, Slot { "slot.name", "label" },
            Slot { "slot.bypass", "textButton" }, Slot { "slot.editor", "textButton" },
            Slot { "slot.remove", "textButton" }, Slot { "slot.scope", "textButton" },
            Slot { "slot.dragIndicator", "surface" }
        }),
        makeLabelDescriptor ("effects.empty", "center"),
        makeDescriptor ("effects.outputLayout", Kind::comboBox),

        makeDescriptor ("output.stage", Kind::outputStage),
        makeDescriptor ("output.mixer", Kind::channelMixer, {}, {}, {}, {
            Slot { "row.surface", "surface" }, Slot { "row.source", "label" },
            Slot { "row.destination", "label" }, Slot { "row.peak", "label" },
            Slot { "row.gain", "linearSlider" }, Slot { "row.mute", "toggleButton" },
            Slot { "row.solo", "toggleButton" }
        }),
        makeDescriptor ("output.mode", Kind::comboBox),
        makeDescriptor ("output.reset", Kind::textButton),

        makeLabelDescriptor ("settings.title"),
        makeIconDescriptor ("settings.close", "close"),
        makeLabelDescriptor ("settings.audio.heading"),
        makeDescriptor ("settings.audio.device", Kind::comboBox, { Entry::settings }),
        makeDescriptor ("settings.audio.sampleRate", Kind::comboBox, { Entry::settings }),
        makeDescriptor ("settings.audio.bufferSize", Kind::comboBox, { Entry::settings }),
        makeDescriptor ("settings.audio.channelLayout", Kind::comboBox, { Entry::settings }),
        makeReadOnlyDescriptor ("settings.audio.channelMap", Kind::channelMap, {
            Slot { "row.surface", "surface" }, Slot { "row.source", "label" },
            Slot { "row.destination", "label" },
            Slot { "header.label", "label" }, Slot { "empty.message", "label" }
        }),
        makeLabelDescriptor ("settings.plugins.formats"),
        makeDescriptor ("settings.plugins.knownList", Kind::pluginList, {}, {}, {}, {
            Slot { "row.surface", "surface" }, Slot { "row.name", "label" },
            Slot { "row.manufacturer", "label" }, Slot { "row.format", "label" },
            Slot { "row.status", "label" }, Slot { "header.label", "label" },
            Slot { "empty.message", "label" }
        }),
        makeIconDescriptor ("settings.plugins.scanAll", "scan", { Entry::settings }, "leading"),
        makeIconDescriptor ("settings.plugins.scanFolder", "folderSearch", { Entry::settings }, "leading"),
        makeDescriptor ("settings.plugins.cancel", Kind::textButton, { Entry::settings }),
        makeDescriptor ("settings.plugins.progress", Kind::progressBar, { Entry::settings }),
        makeLabelDescriptor ("settings.skins.heading"),
        makeDescriptor ("settings.skins.library", Kind::skinList, { Entry::settings }, {}, {}, {
            Slot { "row.surface", "surface" }, Slot { "row.name", "label" },
            Slot { "row.author", "label" }, Slot { "row.version", "label" },
            Slot { "row.status", "label" }, Slot { "empty.message", "label" }
        }),
        makeIconDescriptor ("settings.skins.import", "import", { Entry::settings }, "leading"),
        makeIconDescriptor ("settings.skins.remove", "remove", {}, "leading"),
        makeIconDescriptor ("settings.skins.preview", "preview", {}, "leading"),
        makeDescriptor ("settings.skins.apply", Kind::textButton, { Entry::settings }),
        makeDescriptor ("settings.skins.cancelPreview", Kind::textButton, { Entry::settings }),
        makeIconDescriptor ("settings.skins.reload", "reload", {}, "leading")
    };
    return values;
}
} // namespace

const std::vector<SkinComponentDescriptor>& PlayerComponentCatalog::descriptors()
{
    return catalog();
}

const SkinComponentDescriptor* PlayerComponentCatalog::find (const juce::String& id)
{
    const auto& values = catalog();
    const auto found = std::find_if (values.begin(), values.end(), [&] (const auto& value)
    {
        return value.id == id;
    });
    return found == values.end() ? nullptr : &*found;
}

juce::String PlayerComponentCatalog::fallbackIconPathData (juce::StringRef iconNameRef)
{
    const juce::String iconName (iconNameRef);
    const auto& paths = iconPathData();
    const auto found = paths.find (iconName);
    return found == paths.end() ? juce::String() : found->second;
}

std::shared_ptr<const juce::Path> PlayerComponentCatalog::fallbackIconPath (
    juce::StringRef iconNameRef)
{
    const juce::String iconName (iconNameRef);
    static const auto compiled = []
    {
        std::map<juce::String, std::shared_ptr<const juce::Path>> result;
        for (const auto& [name, data] : iconPathData())
            if (const auto path = skinCompileSvgPathData (data, 256))
                result.emplace (name, path->path);
        return result;
    }();
    const auto found = compiled.find (iconName);
    return found == compiled.end() ? nullptr : found->second;
}

SkinComponentDimensions PlayerComponentCatalog::dimensionsForTarget (juce::StringRef targetRef)
{
    const juce::String target (targetRef);
    if (target == "label") return { 0, 16, 80, 20 };
    if (target == "textButton") return { 28, 28, 84, 34 };
    if (target == "toggleButton") return { 28, 28, 104, 30 };
    if (target == "comboBox") return { 60, 28, 132, 32 };
    if (target == "textEditor") return { 60, 28, 160, 32 };
    if (target == "linearSlider") return { 60, 24, 140, 28 };
    if (target == "rotarySlider") return { 48, 48, 72, 72 };
    if (target == "progressBar") return { 40, 4, 140, 6 };
    if (target == "table") return { 180, 100, 360, 220 };
    if (target == "artwork") return { 120, 120, 300, 300 };
    if (target == "effectChain") return { 220, 120, 360, 300 };
    if (target == "outputStage") return { 180, 140, 300, 420 };
    if (target == "channelMixer" || target == "channelMap" || target == "pluginList" || target == "skinList")
        return { 180, 100, 360, 240 };
    return {};
}
