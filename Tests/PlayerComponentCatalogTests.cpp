#include "skin/PlayerComponentCatalog.h"
#include "skin/SkinComponentBindings.h"
#include "skin/SkinRenderDefinition.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>

namespace
{
juce::StringArray exactIds()
{
    return {
        "app.brand.eyebrow", "app.brand.title", "app.deviceStatus", "app.status",
        "workspace.studio", "workspace.listening", "settings.open",
        "library.add", "library.open", "player.open", "library.back",
        "library.browsePlaylists", "library.browseAlbums", "library.browseArtists",
        "library.browseSongs", "library.browseFavourites", "library.browseQueue",
        "library.gridSearch", "library.grid", "library.detailCover", "library.description",
        "library.play", "library.shufflePlay", "library.search", "library.collections", "library.actions", "library.newPlaylist",
        "library.title", "library.count", "library.empty", "library.tracks",
        "queue.toggle", "queue.actions", "library.changeArtwork", "queue.title", "queue.count", "queue.table", "queue.empty",
        "queue.remove", "queue.clear", "queue.moveUp", "queue.moveDown",
        "artwork.cover", "artwork.metadata", "nowPlaying.eyebrow", "nowPlaying.title",
        "nowPlaying.metadata", "transport.previous", "transport.playPause",
        "transport.next", "transport.seek", "transport.time", "transport.shuffle",
        "transport.repeat", "transport.repeatCycle", "transport.volumeLabel", "transport.volumeValue",
        "transport.volume", "effects.open", "effects.close", "effects.summary",
        "effects.title", "effects.filterLabel", "effects.filter",
        "effects.typeFilter", "effects.manufacturerFilter", "effects.formatFilter", "effects.catalog", "effects.scope", "effects.scopeLabel",
        "effects.add", "effects.clearTails", "effects.bypassAll", "effects.chain", "effects.empty", "effects.outputLayout",
        "output.stage", "output.mixer", "output.mode", "output.reset", "settings.title", "settings.close", "settings.audio.heading",
        "settings.audio.device", "settings.audio.deviceConfiguration",
        "settings.audio.sampleRate", "settings.audio.bufferSize",
        "settings.audio.channelLayout", "settings.audio.channelMap", "settings.audio.routing",
        "settings.plugins.formats",
        "settings.plugins.enableVst3", "settings.plugins.enableAu", "settings.plugins.filter",
        "settings.plugins.typeFilter", "settings.plugins.manufacturerFilter", "settings.plugins.formatFilter", "settings.plugins.knownList",
        "settings.plugins.scanAll", "settings.plugins.scanFolder",
        "settings.plugins.cancel", "settings.plugins.progress",
        "settings.plugins.scanScope", "settings.plugins.defaultLocations", "settings.plugins.customDirectories", "settings.plugins.removeDirectory",
        "settings.skins.heading", "settings.skins.library", "settings.skins.import", "settings.skins.remove",
        "settings.skins.preview", "settings.skins.apply",
        "settings.skins.cancelPreview", "settings.skins.reload",
        "settings.music.heading", "settings.music.directories", "settings.music.add",
        "settings.music.scan", "settings.music.cancel", "settings.music.remove",
        "settings.music.automatic", "settings.music.status",
        "library.smartPlaylistEditor", "library.smartPlaylistRules"
    };
}

std::set<juce::String> requiredFor (SkinLayoutEntry entry)
{
    std::set<juce::String> result;
    for (const auto& descriptor : PlayerComponentCatalog::descriptors())
        if (descriptor.requiredIn.contains (entry))
            result.insert (descriptor.id);
    return result;
}

std::set<juce::String> setOf (std::initializer_list<const char*> values)
{
    std::set<juce::String> result;
    for (const auto* value : values) result.insert (value);
    return result;
}

std::map<juce::String, juce::String> slotsOf (juce::StringRef id)
{
    std::map<juce::String, juce::String> result;
    if (const auto* descriptor = PlayerComponentCatalog::find (juce::String (id)))
        for (const auto& slot : descriptor->slots)
            result.emplace (slot.name, slot.rendererTarget);
    return result;
}

std::unique_ptr<juce::Component> componentFor (SkinComponentKind kind, double& progress)
{
    switch (kind)
    {
        case SkinComponentKind::label: return std::make_unique<juce::Label>();
        case SkinComponentKind::textButton: return std::make_unique<juce::TextButton>();
        case SkinComponentKind::toggleButton: return std::make_unique<juce::ToggleButton>();
        case SkinComponentKind::comboBox: return std::make_unique<juce::ComboBox>();
        case SkinComponentKind::textEditor: return std::make_unique<juce::TextEditor>();
        case SkinComponentKind::linearSlider:
        case SkinComponentKind::slider: return std::make_unique<juce::Slider>();
        case SkinComponentKind::progressBar: return std::make_unique<juce::ProgressBar> (progress);
        case SkinComponentKind::table: return std::make_unique<juce::TableListBox>();
        case SkinComponentKind::artwork:
        case SkinComponentKind::effectChain:
        case SkinComponentKind::outputStage:
        case SkinComponentKind::channelMixer:
        case SkinComponentKind::channelMap:
        case SkinComponentKind::pluginList:
        case SkinComponentKind::skinList:
        case SkinComponentKind::directoryList:
        case SkinComponentKind::smartPlaylistEditor:
        case SkinComponentKind::smartPlaylistRuleList:
        case SkinComponentKind::playlistGrid: return std::make_unique<juce::Component>();
    }
    return {};
}

class PlayerComponentCatalogTests final : public juce::UnitTest
{
public:
    PlayerComponentCatalogTests() : UnitTest ("PlayerComponentCatalog") {}

    void runTest() override
    {
        beginTest ("freezes the exact Skin API 1 ID list and unique descriptor table");
        {
            const auto expected = exactIds();
            const auto& descriptors = PlayerComponentCatalog::descriptors();
            expectEquals (static_cast<int> (descriptors.size()), expected.size());
            std::set<juce::String> unique;
            for (int i = 0; i < expected.size() && i < static_cast<int> (descriptors.size()); ++i)
            {
                expectEquals (descriptors[static_cast<size_t> (i)].id, expected[i]);
                unique.insert (descriptors[static_cast<size_t> (i)].id);
                expect (PlayerComponentCatalog::find (expected[i]) != nullptr);
            }
            expectEquals (static_cast<int> (unique.size()), expected.size());
            expect (PlayerComponentCatalog::find ("unknown.component") == nullptr);
            expect (PlayerComponentCatalog::find ("effects.close") != nullptr);
            const auto* search = PlayerComponentCatalog::find ("library.search");
            expect (search != nullptr && search->kind == SkinComponentKind::textEditor
                    && search->defaultRendererTarget == "textEditor"
                    && search->interactive && search->requiredIn.empty());
        }

        beginTest ("freezes required sets for Studio, Listening, Effects and Settings");
        {
            const auto workspace = setOf ({
                "library.add", "queue.table", "transport.previous", "transport.playPause",
                "transport.next", "transport.seek", "transport.time", "transport.volume",
                "settings.open", "workspace.studio", "workspace.listening", "app.status"
            });
            expect (requiredFor (SkinLayoutEntry::studio) == workspace);
            expect (requiredFor (SkinLayoutEntry::listening) == workspace);
            expect (requiredFor (SkinLayoutEntry::effects) == setOf ({
                "effects.filter", "effects.catalog", "effects.add",
                "effects.chain"
            }));
            expect (requiredFor (SkinLayoutEntry::settings) == setOf ({
                "settings.audio.device", "settings.audio.sampleRate",
                "settings.audio.bufferSize", "settings.audio.channelLayout",
                "settings.plugins.scanAll", "settings.plugins.scanFolder",
                "settings.plugins.cancel", "settings.plugins.progress",
                "settings.skins.library", "settings.skins.import", "settings.skins.apply",
                "settings.skins.cancelPreview"
            }));
        }

        beginTest ("maps default targets and the two volume presentations exactly");
        {
            const auto* seek = PlayerComponentCatalog::find ("transport.seek");
            expect (seek != nullptr && seek->defaultRendererTarget == "linearSlider"
                    && seek->presentations == juce::StringArray ({ "linearHorizontal" }));
            const auto* volume = PlayerComponentCatalog::find ("transport.volume");
            expect (volume != nullptr && volume->defaultRendererTarget == "linearSlider");
            if (volume)
            {
                expect (volume->presentations
                        == juce::StringArray ({ "linearHorizontal", "rotary" }));
                expect (volume->rendererTargetByPresentation
                        == std::map<juce::String, juce::String> ({
                            { "linearHorizontal", "linearSlider" },
                            { "rotary", "rotarySlider" }
                        }));
            }
            const auto* queue = PlayerComponentCatalog::find ("queue.table");
            expect (queue != nullptr && queue->defaultRendererTarget == "table");
            const auto* outputMap = PlayerComponentCatalog::find (
                "settings.audio.channelMap");
            expect (outputMap != nullptr && ! outputMap->interactive,
                    "The legacy channel map retains its read-only interaction contract.");
        }

        beginTest ("library browsing is an optional workspace table alternative");
        {
            const auto* queue = PlayerComponentCatalog::find ("queue.table");
            const auto* library = PlayerComponentCatalog::find ("library.tracks");
            expect (queue != nullptr && library != nullptr);
            if (queue && library)
            {
                expect (queue->requiredAlternatives == juce::StringArray { "library.tracks" });
                expect (library->requiredIn == std::set<SkinLayoutEntry> { SkinLayoutEntry::collection });
                expect (library->kind == queue->kind && library->interactive);
                expectEquals (library->defaultRendererTarget, queue->defaultRendererTarget);
                expect (library->presentations == queue->presentations);
                expect (library->rendererTargetByPresentation == queue->rendererTargetByPresentation);
                expect (slotsOf (library->id) == slotsOf (queue->id));
            }
            for (const auto entry : { SkinLayoutEntry::studio, SkinLayoutEntry::listening })
            {
                expect (PlayerComponentCatalog::isRequired ("queue.table", entry));
                expect (PlayerComponentCatalog::isRequired ("library.tracks", entry));
                expect (! PlayerComponentCatalog::isRequired ("library.collections", entry));
            }
            expect (! PlayerComponentCatalog::isRequired ("library.tracks", SkinLayoutEntry::settings));
            for (const auto& [id, kind] : std::map<juce::String, SkinComponentKind> {
                     { "library.collections", SkinComponentKind::comboBox },
                     { "library.newPlaylist", SkinComponentKind::textButton },
                     { "library.actions", SkinComponentKind::textButton } })
            {
                const auto* descriptor = PlayerComponentCatalog::find (id);
                expect (descriptor != nullptr && descriptor->kind == kind
                        && descriptor->interactive && descriptor->requiredIn.empty(), id);
            }
        }

        beginTest ("optional library pages share navigation and playback requirements");
        {
            auto required = setOf ({
                "library.open", "player.open", "transport.previous", "transport.playPause",
                "transport.next", "transport.seek", "transport.time", "transport.volume",
                "settings.open", "workspace.studio", "workspace.listening", "app.status"
            });
            auto overview = required;
            overview.insert ("library.grid");
            expect (requiredFor (SkinLayoutEntry::library) == overview);
            auto detail = required;
            detail.insert ("library.tracks");
            detail.insert ("library.back");
            expect (requiredFor (SkinLayoutEntry::collection) == detail);
            expect (requiredSkinLayoutEntries() == std::set<SkinLayoutEntry> {
                SkinLayoutEntry::studio, SkinLayoutEntry::listening, SkinLayoutEntry::effects, SkinLayoutEntry::settings
            });
            for (const auto& name : skinLayoutEntryNames())
            {
                const auto entry = skinLayoutEntryFromName (name);
                expect (entry.has_value());
                if (entry) expectEquals (skinLayoutEntryName (*entry), name);
            }
            expect (! skinLayoutEntryFromName ("unknown"));
            expect (slotsOf ("library.grid") == std::map<juce::String, juce::String> {
                { "card.surface", "surface" }, { "card.cover", "artwork" },
                { "card.title", "label" }, { "card.summary", "label" }, { "card.date", "label" }
            });
            expect (slotsOf ("library.detailCover") == slotsOf ("artwork.cover"));
        }

        beginTest ("freezes semantic alignment for every label component");
        {
            const std::map<juce::String, juce::String> nonLeftAlignments {
                { "queue.count", "right" },
                { "library.count", "right" },
                { "app.deviceStatus", "right" },
                { "transport.volumeLabel", "right" },
                { "transport.volumeValue", "right" },
                { "transport.time", "center" },
                { "queue.empty", "center" },
                { "library.empty", "center" },
                { "artwork.metadata", "center" },
                { "effects.empty", "center" }
            };
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
            {
                if (descriptor.kind != SkinComponentKind::label) continue;
                const auto expected = nonLeftAlignments.find (descriptor.id);
                expectEquals (descriptor.textAlignment,
                              expected == nonLeftAlignments.end()
                                  ? juce::String ("left") : expected->second,
                              descriptor.id);
            }
        }

        beginTest ("freezes semantic icons for universally recognisable actions");
        {
            const std::map<juce::String, juce::String> expectedIcons {
                { "settings.open", "settings" }, { "library.add", "add" },
                { "queue.toggle", "queue" }, { "queue.remove", "remove" }, { "queue.moveUp", "moveUp" },
                { "queue.moveDown", "moveDown" },
                { "transport.previous", "previous" }, { "transport.repeatCycle", "repeat" },
                { "transport.playPause", "play" }, { "transport.next", "next" },
                { "effects.open", "effects" }, { "effects.close", "close" },
                { "effects.add", "add" }, { "settings.close", "close" },
                { "settings.plugins.scanAll", "scan" },
                { "settings.plugins.scanFolder", "folderSearch" },
                { "settings.plugins.removeDirectory", "remove" },
                { "settings.skins.import", "import" },
                { "settings.skins.remove", "remove" },
                { "settings.skins.preview", "preview" },
                { "settings.skins.reload", "reload" }
            };
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
            {
                const auto expected = expectedIcons.find (descriptor.id);
                expectEquals (descriptor.iconName,
                              expected == expectedIcons.end()
                                  ? juce::String() : expected->second,
                              descriptor.id);
                if (expected != expectedIcons.end())
                {
                    const auto expectedPresentation =
                        (descriptor.id.startsWith ("settings.plugins.") && descriptor.id != "settings.plugins.removeDirectory")
                            || descriptor.id.startsWith ("settings.skins.")
                        ? juce::String ("leading") : juce::String ("only");
                    expectEquals (descriptor.iconPresentation,
                                  expectedPresentation, descriptor.id);
                    expect (skinCompileSvgPathData (
                                PlayerComponentCatalog::fallbackIconPathData (
                                    descriptor.iconName)).has_value(),
                            descriptor.iconName);
                    expect (PlayerComponentCatalog::fallbackIconPath (
                                descriptor.iconName) != nullptr,
                            descriptor.iconName);
                }
            }
        }

        beginTest ("freezes every composite slot name and target type");
        expect (slotsOf ("queue.table") == std::map<juce::String, juce::String> ({
            { "header.surface", "surface" }, { "header.text", "label" },
            { "row.surface", "surface" }, { "cell.title", "label" },
            { "cell.location", "label" }, { "cell.duration", "label" },
            { "cell.artist", "label" }, { "cell.album", "label" }
        }));
        expect (slotsOf ("artwork.cover") == std::map<juce::String, juce::String> ({
            { "frame.background", "surface" }, { "frame.overlay", "surface" }
        }));
        expect (slotsOf ("effects.chain") == std::map<juce::String, juce::String> ({
            { "slot.surface", "surface" }, { "slot.name", "label" },
            { "slot.bypass", "textButton" }, { "slot.editor", "textButton" },
            { "slot.remove", "textButton" }, { "slot.scope", "textButton" }, { "slot.dragIndicator", "surface" }
        }));
        expect (slotsOf ("output.mixer") == std::map<juce::String, juce::String> ({
            { "row.surface", "surface" }, { "row.source", "label" },
            { "row.destination", "label" }, { "row.peak", "label" },
            { "row.gain", "linearSlider" }, { "row.mute", "toggleButton" },
            { "row.solo", "toggleButton" }
        }));
        expect (slotsOf ("settings.audio.channelMap")
                == std::map<juce::String, juce::String> ({
                    { "row.surface", "surface" }, { "row.source", "label" },
                    { "row.destination", "label" }, { "header.label", "label" }, { "empty.message", "label" }
                }));
        expect (slotsOf ("settings.audio.routing")
                == std::map<juce::String, juce::String> ({
                    { "row.surface", "surface" }, { "row.source", "label" },
                    { "row.destination", "label" }, { "row.output", "comboBox" },
                    { "header.label", "label" }, { "empty.message", "label" }
                }));
        expect (slotsOf ("settings.plugins.knownList")
                == std::map<juce::String, juce::String> ({
                    { "row.surface", "surface" }, { "row.name", "label" },
                    { "row.manufacturer", "label" }, { "row.format", "label" },
                    { "row.status", "label" }, { "header.label", "label" }, { "empty.message", "label" }
                }));
        expect (slotsOf ("settings.skins.library")
                == std::map<juce::String, juce::String> ({
                    { "row.surface", "surface" }, { "row.name", "label" },
                    { "row.author", "label" }, { "row.version", "label" },
                    { "row.status", "label" }, { "empty.message", "label" }
                }));

        beginTest ("runtime bindings reject unknown, duplicate and JUCE type mismatches");
        {
            SkinComponentBindings bindings;
            juce::Label label;
            SkinValidationReport report;
            expect (! bindings.bind ("unknown.component", label, report));
            expect (report.hasCode ("COMPONENT001"));
        }
        {
            SkinComponentBindings bindings;
            juce::TextButton first, second;
            SkinValidationReport report;
            expect (bindings.bind ("library.add", first, report));
            expectEquals (first.getProperties()[SkinComponentBindings::iconNameProperty]
                              .toString(),
                          juce::String ("add"));
            expect (! bindings.bind ("library.add", second, report));
            expect (report.hasCode ("COMPONENT007"));
        }
        {
            SkinComponentBindings bindings;
            juce::Label wrong;
            SkinValidationReport report;
            expect (! bindings.bind ("library.add", wrong, report));
            expect (report.hasCode ("COMPONENT007"));
        }

        beginTest ("accepts one matching runtime binding for every descriptor");
        {
            SkinComponentBindings bindings;
            SkinValidationReport report;
            std::vector<std::unique_ptr<juce::Component>> components;
            double progress = 0.0;
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
            {
                auto component = componentFor (descriptor.kind, progress);
                expect (component != nullptr);
                if (component)
                {
                    expect (bindings.bind (descriptor.id, *component, report),
                            descriptor.id + " " + report.toText());
                    components.push_back (std::move (component));
                }
            }
            expectEquals (bindings.ids().size(),
                          static_cast<int> (PlayerComponentCatalog::descriptors().size()));
            for (const auto& descriptor : PlayerComponentCatalog::descriptors())
                expect (bindings.find (descriptor.id) != nullptr);
        }
    }
};

PlayerComponentCatalogTests playerComponentCatalogTests;
} // namespace
