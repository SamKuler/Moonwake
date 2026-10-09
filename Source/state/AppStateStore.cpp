#include "AppStateStore.h"
#include "EffectChainSerialization.h"

#include "app/ProductIdentity.h"

#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace
{
const juce::Identifier appStateType { MoonwakeIdentity::stateRootType };
const juce::Identifier playlistType { "Playlist" };
const juce::Identifier trackType { "Track" };

const juce::Identifier schemaVersionProperty { "schemaVersion" };
constexpr int currentSchemaVersion = 14;
const juce::Identifier playbackPositionProperty { "playbackPositionSeconds" };
const juce::Identifier pathProperty { "path" };
const juce::Identifier idProperty { "id" };
const juce::Identifier titleProperty { "title" };
const juce::Identifier artistProperty { "artist" };
const juce::Identifier albumProperty { "album" };
const juce::Identifier albumArtistProperty { "albumArtist" };
const juce::Identifier genreProperty { "genre" };
const juce::Identifier yearProperty { "year" };
const juce::Identifier trackNumberProperty { "trackNumber" };
const juce::Identifier discNumberProperty { "discNumber" };
const juce::Identifier durationProperty { "duration" };
const juce::Identifier sampleRateProperty { "sampleRate" };
const juce::Identifier channelCountProperty { "channelCount" };
const juce::Identifier bitDepthProperty { "bitDepth" };
const juce::Identifier formatNameProperty { "formatName" };
const juce::Identifier artworkCacheKeyProperty { "artworkCacheKey" };
const juce::Identifier availableProperty { "available" };
const juce::Identifier selectedIndexProperty { "selectedIndex" };
const juce::Identifier selectedTrackIdProperty { "selectedTrackId" };
const juce::Identifier shuffleProperty { "shuffle" };
const juce::Identifier repeatProperty { "repeat" };
const juce::Identifier volumeProperty { "volume" };
const juce::Identifier outputLayoutProperty { "outputLayout" };
const juce::Identifier workspaceModeProperty { "workspaceMode" };
const juce::Identifier activeSkinIdProperty { "activeSkinId" };
const juce::Identifier activeSkinVersionProperty { "activeSkinVersion" };
const juce::Identifier developerSkinDirectoryProperty { "developerSkinDirectory" };

juce::String toString (RepeatMode mode)
{
    switch (mode)
    {
        case RepeatMode::off: return "off";
        case RepeatMode::all: return "all";
        case RepeatMode::one: return "one";
    }

    return "off";
}

RepeatMode repeatModeFromString (const juce::String& value)
{
    if (value == "all")
        return RepeatMode::all;

    if (value == "one")
        return RepeatMode::one;

    return RepeatMode::off;
}

double finiteNonNegative (double value)
{
    return std::isfinite (value)
               ? juce::jmax (0.0, value)
               : 0.0;
}

juce::ValueTree trackToTree (const TrackItem& track)
{
    juce::ValueTree tree (trackType);
    tree.setProperty (pathProperty, track.file.getFullPathName(), nullptr);
    tree.setProperty (idProperty, track.id, nullptr);
    tree.setProperty (titleProperty, track.title, nullptr);
    tree.setProperty (artistProperty, track.artist, nullptr);
    if (! track.artists.isEmpty())
    {
        juce::ValueTree artists ("Artists");
        for (const auto& name : track.artistNames())
        {
            juce::ValueTree artist ("Artist");
            artist.setProperty ("name", name, nullptr);
            artists.addChild (artist, -1, nullptr);
        }
        tree.addChild (artists, -1, nullptr);
    }
    tree.setProperty (albumProperty, track.album, nullptr);
    tree.setProperty (albumArtistProperty, track.albumArtist, nullptr);
    tree.setProperty (genreProperty, track.genre, nullptr);
    tree.setProperty (yearProperty, track.year, nullptr);
    tree.setProperty (trackNumberProperty, track.trackNumber, nullptr);
    tree.setProperty (discNumberProperty, track.discNumber, nullptr);
    tree.setProperty (durationProperty, track.durationSeconds, nullptr);
    tree.setProperty (sampleRateProperty, track.sampleRate, nullptr);
    tree.setProperty (channelCountProperty, track.channelCount, nullptr);
    tree.setProperty (bitDepthProperty, track.bitDepth, nullptr);
    tree.setProperty (formatNameProperty, track.formatName, nullptr);
    tree.setProperty (artworkCacheKeyProperty, track.artworkCacheKey, nullptr);
    tree.setProperty (availableProperty, track.available, nullptr);
    tree.setProperty ("addedAtMs", track.addedAtMs, nullptr);
    tree.setProperty ("fileSizeBytes", track.fileSizeBytes, nullptr);
    tree.setProperty ("fileModifiedAtMs", track.fileModifiedAtMs, nullptr);
    tree.setProperty ("fileIdentity", track.fileIdentity, nullptr);
    tree.setProperty ("reservedEffectChainState", track.reservedEffectChainState, nullptr);
    return tree;
}

TrackItem trackFromTree (const juce::ValueTree& tree)
{
    TrackItem track { juce::File (tree.getProperty (pathProperty).toString()),
                      tree.getProperty (titleProperty).toString(),
                      static_cast<double> (tree.getProperty (durationProperty, 0.0)) };
    const auto id = tree.getProperty (idProperty).toString();
    if (id.isNotEmpty()) track.id = id;
    track.artist = tree.getProperty (artistProperty).toString();
    for (const auto member : tree.getChildWithName ("Artists"))
        if (const auto name = member.getProperty ("name").toString().trim();
            member.hasType ("Artist") && name.isNotEmpty())
            track.artists.addIfNotAlreadyThere (name);
    track.album = tree.getProperty (albumProperty).toString();
    track.albumArtist = tree.getProperty (albumArtistProperty).toString();
    track.genre = tree.getProperty (genreProperty).toString();
    track.year = juce::jmax (0, static_cast<int> (tree.getProperty (yearProperty, 0)));
    track.trackNumber = juce::jmax (0, static_cast<int> (tree.getProperty (trackNumberProperty, 0)));
    track.discNumber = juce::jmax (0, static_cast<int> (tree.getProperty (discNumberProperty, 0)));
    track.sampleRate = static_cast<double> (tree.getProperty (sampleRateProperty, 0.0));
    track.channelCount = static_cast<int> (tree.getProperty (channelCountProperty, 0));
    track.bitDepth = juce::jmax (0, static_cast<int> (tree.getProperty (bitDepthProperty, 0)));
    track.formatName = tree.getProperty (formatNameProperty).toString();
    track.artworkCacheKey = tree.getProperty (artworkCacheKeyProperty).toString();
    track.available = static_cast<bool> (tree.getProperty (availableProperty, true));
    track.addedAtMs = juce::jmax (juce::int64 { 0 }, static_cast<juce::int64> (tree.getProperty ("addedAtMs", 0)));
    track.fileSizeBytes = juce::jmax (juce::int64 { 0 }, static_cast<juce::int64> (tree.getProperty ("fileSizeBytes", 0)));
    track.fileModifiedAtMs = static_cast<juce::int64> (tree.getProperty ("fileModifiedAtMs", 0));
    track.fileIdentity = tree.getProperty ("fileIdentity").toString();
    track.reservedEffectChainState = tree.getProperty ("reservedEffectChainState").toString();
    return track;
}

constexpr const char* smartFieldNames[] {
    "title", "artist", "album", "albumArtist", "genre", "format",
    "year", "channelCount", "sampleRate", "addedWithinDays", "favourite", "available"
};
constexpr const char* smartComparisonNames[] {
    "contains", "notContains", "equals", "notEquals", "lessThan", "lessOrEqual",
    "greaterThan", "greaterOrEqual", "withinLastDays"
};

template <typename Enum, size_t Size>
bool parseSmartEnum (const juce::String& name, const char* const (&names)[Size], Enum& value)
{
    for (size_t index = 0; index < Size; ++index)
        if (name == names[index])
        {
            value = static_cast<Enum> (index);
            return true;
        }
    return false;
}

juce::ValueTree smartRulesToTree (const SmartPlaylistRules& rules)
{
    juce::ValueTree tree ("SmartRules");
    tree.setProperty ("match", rules.match == SmartPlaylistMatch::all ? "all" : "any", nullptr);
    for (const auto& rule : rules.rules)
    {
        juce::ValueTree child ("Rule");
        child.setProperty ("field", smartFieldNames[static_cast<size_t> (rule.field)], nullptr);
        child.setProperty ("comparison", smartComparisonNames[static_cast<size_t> (rule.comparison)], nullptr);
        child.setProperty ("textValue", rule.textValue, nullptr);
        child.setProperty ("numberValue", rule.numberValue, nullptr);
        child.setProperty ("boolValue", rule.boolValue, nullptr);
        tree.appendChild (child, nullptr);
    }
    return tree;
}

bool smartRulesFromTree (const juce::ValueTree& tree, SmartPlaylistRules& rules)
{
    const auto match = tree.getProperty ("match").toString();
    if (match != "all" && match != "any") return false;
    rules.match = match == "all" ? SmartPlaylistMatch::all : SmartPlaylistMatch::any;
    for (const auto child : tree)
    {
        SmartPlaylistRule rule;
        if (! child.hasType ("Rule")
            || ! parseSmartEnum (child.getProperty ("field").toString(), smartFieldNames, rule.field)
            || ! parseSmartEnum (child.getProperty ("comparison").toString(), smartComparisonNames, rule.comparison))
            return false;
        rule.textValue = child.getProperty ("textValue").toString();
        rule.numberValue = static_cast<double> (child.getProperty ("numberValue", 0.0));
        rule.boolValue = static_cast<bool> (child.getProperty ("boolValue", true));
        const auto kind = smartPlaylistValueKind (rule.field);
        if (kind == SmartPlaylistValueKind::number)
        {
            const auto value = child.getProperty ("numberValue").toString().trim();
            const auto* start = value.toRawUTF8();
            char* end = nullptr;
            rule.numberValue = std::strtod (start, &end);
            if (start == end || *end != '\0') return false;
        }
        else if (kind == SmartPlaylistValueKind::boolean)
        {
            const auto value = child.getProperty ("boolValue").toString();
            if (value != "0" && value != "1" && value != "true" && value != "false") return false;
            rule.boolValue = value == "1" || value == "true";
        }
        rules.rules.push_back (std::move (rule));
    }
    return validateSmartPlaylistRules (rules).wasOk();
}

bool restoreMusicDirectories (const juce::ValueTree& root, std::vector<MusicDirectory>& directories)
{
    std::unordered_set<std::string> ids, paths;
    std::vector<MusicDirectory> restored;
    for (const auto child : root.getChildWithName ("MusicDirectories"))
    {
        const auto id = child.getProperty (idProperty).toString();
        const auto path = child.getProperty (pathProperty).toString();
        if (! child.hasType ("Directory") || id.trim().isEmpty()
            || ! ids.insert (id.toStdString()).second || ! juce::File::isAbsolutePath (path)) return false;
        const juce::File folder (path);
        const auto canonical = folder.getFullPathName();
        const auto key = juce::File::areFileNamesCaseSensitive() ? canonical : canonical.toUpperCase();
        if (! paths.insert (key.toStdString()).second) return false;
        restored.push_back ({ id, folder, static_cast<bool> (child.getProperty ("automatic", true)) });
    }
    directories = std::move (restored);
    return true;
}

bool restoreLibrary (const juce::ValueTree& root, MusicLibrary& library)
{
    const auto libraryTree = root.getChildWithName ("MusicLibrary");
    if (! libraryTree.isValid()
        || ! libraryTree.getChildWithName ("Tracks").isValid()
        || ! libraryTree.getChildWithName ("SavedPlaylists").isValid()
        || ! libraryTree.getChildWithName ("Favourites").isValid()) return false;
    std::vector<TrackItem> tracks;
    std::vector<SavedPlaylist> playlists;
    std::vector<juce::String> favourites;
    for (const auto child : libraryTree.getChildWithName ("Tracks"))
    {
        if (! child.hasType (trackType)
            || ! juce::File::isAbsolutePath (child.getProperty (pathProperty).toString())) return false;
        auto track = trackFromTree (child);
        track.id = child.getProperty (idProperty).toString();
        tracks.push_back (std::move (track));
    }
    for (const auto child : libraryTree.getChildWithName ("SavedPlaylists"))
    {
        if (! child.hasType ("SavedPlaylist")) return false;
        SavedPlaylist playlist;
        playlist.id = child.getProperty (idProperty).toString();
        playlist.name = child.getProperty ("name").toString();
        playlist.artworkCacheKey = child.getProperty (artworkCacheKeyProperty).toString();
        playlist.createdAtMs = static_cast<juce::int64> (child.getProperty ("createdAtMs", 0));
        playlist.updatedAtMs = static_cast<juce::int64> (child.getProperty ("updatedAtMs", 0));
        for (const auto entry : child)
        {
            if (entry.hasType ("SmartRules"))
            {
                if (playlist.smartRules.has_value()) return false;
                playlist.smartRules.emplace();
                if (! smartRulesFromTree (entry, *playlist.smartRules)) return false;
            }
            else if (entry.hasType ("Entry"))
                playlist.entries.push_back ({ entry.getProperty (idProperty).toString(),
                                              entry.getProperty ("trackId").toString() });
            else return false;
        }
        playlists.push_back (std::move (playlist));
    }
    for (const auto child : libraryTree.getChildWithName ("Favourites"))
    {
        if (! child.hasType ("Favourite")) return false;
        favourites.push_back (child.getProperty ("trackId").toString());
    }
    return library.restore (std::move (tracks), std::move (playlists), std::move (favourites));
}

bool restoreCalibration (const juce::ValueTree& root, std::vector<OutputChannelCalibration>& result)
{
    auto restored = defaultOutputCalibration();
    std::unordered_set<int> seen;
    for (const auto channel : root.getChildWithName ("OutputCalibration"))
    {
        if (! channel.hasType ("Channel")) return false;
        const auto parseNumber = [&] (const char* name, double& value)
        {
            const auto text = channel.getProperty (name).toString();
            const auto* start = text.toRawUTF8();
            char* end = nullptr;
            value = std::strtod (start, &end);
            return start != end && *end == '\0' && std::isfinite (value);
        };
        double type = 0.0, trim = 0.0, delay = 0.0;
        if (! parseNumber ("type", type) || type < 1.0 || type > 1024.0 || std::floor (type) != type
            || ! parseNumber ("trimDb", trim) || ! parseNumber ("delayMs", delay)) return false;
        const auto inverted = channel.getProperty ("inverted").toString();
        if (inverted != "0" && inverted != "1" && inverted != "true" && inverted != "false") return false;
        const OutputChannelCalibration value {
            static_cast<juce::AudioChannelSet::ChannelType> (static_cast<int> (type)),
            static_cast<float> (trim), delay, inverted == "1" || inverted == "true" };
        if (! isValidOutputCalibration (value) || ! seen.insert (static_cast<int> (type)).second) return false;
        const auto found = std::find_if (restored.begin(), restored.end(), [&] (const auto& existing)
            { return existing.channel == value.channel; });
        if (found == restored.end()) return false;
        *found = value;
    }
    result = std::move (restored);
    return true;
}

bool readStateTree (const juce::File& sourceFile,
                    juce::ValueTree& tree, juce::String& errorMessage)
{
    if (! sourceFile.existsAsFile())
    {
        errorMessage = "Player state file not found: " + sourceFile.getFullPathName();
        return false;
    }

    auto xml = juce::XmlDocument::parse (sourceFile);
    if (xml == nullptr)
    {
        errorMessage = "Player state file is not valid XML: " + sourceFile.getFullPathName();
        return false;
    }

    tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (appStateType))
    {
        errorMessage = "Player state file has an unsupported root element.";
        return false;
    }

    const auto schemaVersion = static_cast<int> (
        tree.getProperty (schemaVersionProperty, 1));
    if (schemaVersion > currentSchemaVersion)
    {
        errorMessage = "Player state file was created by a newer version of Moonwake.";
        return false;
    }

    std::vector<OutputChannelCalibration> calibration;
    if (! restoreCalibration (tree, calibration))
    {
        errorMessage = "Player state contains invalid output calibration.";
        return false;
    }

    std::vector<MusicDirectory> musicDirectories;
    if (! restoreMusicDirectories (tree, musicDirectories))
    {
        errorMessage = "Player state contains invalid or duplicate music directories.";
        return false;
    }

    if (schemaVersion >= 12 || tree.getChildWithName ("MusicLibrary").isValid())
    {
        MusicLibrary library;
        bool valid = restoreLibrary (tree, library) && tree.getChildWithName (playlistType).isValid();
        std::unordered_set<std::string> queueIds;
        for (const auto entry : tree.getChildWithName (playlistType))
        {
            const auto id = entry.getProperty (idProperty).toString();
            valid = valid && entry.hasType (trackType) && id.isNotEmpty()
                  && queueIds.insert (id.toStdString()).second
                  && library.getTrack (entry.getProperty ("libraryId").toString()) != nullptr;
        }
        if (! valid)
        {
            errorMessage = "Player state contains invalid library identities or playlist references.";
            return false;
        }
    }

    return true;
}
} // namespace

juce::ValueTree AppStateStore::toValueTree (const PlayerAppState& state)
{
    juce::ValueTree root (appStateType);
    root.setProperty (schemaVersionProperty, currentSchemaVersion, nullptr);
    juce::ValueTree directories ("PluginDirectories");
    for (const auto& path : state.customVst3Directories)
    {
        juce::ValueTree directory ("Directory");
        directory.setProperty (pathProperty, path, nullptr);
        directories.appendChild (directory, nullptr);
    }
    root.appendChild (directories, nullptr);
    juce::ValueTree musicDirectories ("MusicDirectories");
    for (const auto& rootDirectory : state.musicDirectories)
    {
        juce::ValueTree directory ("Directory");
        directory.setProperty (idProperty, rootDirectory.id, nullptr);
        directory.setProperty (pathProperty, rootDirectory.folder.getFullPathName(), nullptr);
        directory.setProperty ("automatic", rootDirectory.automatic, nullptr);
        musicDirectories.appendChild (directory, nullptr);
    }
    root.appendChild (musicDirectories, nullptr);
    juce::ValueTree routes ("OutputRoutes");
    for (const auto& route : state.audioDevice.outputRoutes)
    {
        juce::ValueTree entry ("Route");
        entry.setProperty ("device", route.deviceIdentifier, nullptr);
        entry.setProperty ("layout", toString (route.layout), nullptr);
        for (const auto port : route.channels)
        {
            juce::ValueTree channel ("Channel");
            channel.setProperty ("port", port, nullptr);
            entry.appendChild (channel, nullptr);
        }
        routes.appendChild (entry, nullptr);
    }
    root.appendChild (routes, nullptr);
    juce::ValueTree formats ("PluginFormats");
    formats.setProperty ("vst3", state.pluginFormats.vst3, nullptr);
    formats.setProperty ("audioUnit", state.pluginFormats.audioUnit, nullptr);
    root.appendChild (formats, nullptr);
    root.setProperty (volumeProperty, state.volume, nullptr);
    root.setProperty (playbackPositionProperty,
                      finiteNonNegative (state.playbackPositionSeconds), nullptr);
    root.setProperty (outputLayoutProperty, toString (state.outputLayout), nullptr);
    juce::ValueTree channelMix ("OutputChannelMix");
    for (const auto& mix : state.outputChannelMix)
    {
        juce::ValueTree channel ("Channel");
        channel.setProperty ("type", static_cast<int> (mix.channel), nullptr);
        channel.setProperty ("gain", mix.gain, nullptr);
        channel.setProperty ("muted", mix.muted, nullptr);
        channel.setProperty ("solo", mix.solo, nullptr);
        channelMix.appendChild (channel, nullptr);
    }
    root.appendChild (channelMix, nullptr);
    juce::ValueTree calibration ("OutputCalibration");
    for (const auto& value : state.outputCalibration)
    {
        juce::ValueTree channel ("Channel");
        channel.setProperty ("type", static_cast<int> (value.channel), nullptr);
        channel.setProperty ("trimDb", value.trimDb, nullptr);
        channel.setProperty ("delayMs", value.delayMs, nullptr);
        channel.setProperty ("inverted", value.inverted, nullptr);
        calibration.appendChild (channel, nullptr);
    }
    root.appendChild (calibration, nullptr);
    juce::ValueTree device ("AudioDevice");
    device.setProperty ("name", state.audioDevice.outputDeviceName, nullptr);
    device.setProperty (sampleRateProperty,
                        finiteNonNegative (state.audioDevice.sampleRate), nullptr);
    device.setProperty ("bufferSize", juce::jmax (0, state.audioDevice.bufferSize), nullptr);
    root.appendChild (device, nullptr);
    root.setProperty (workspaceModeProperty, toString (state.workspaceMode), nullptr);
    root.setProperty (activeSkinIdProperty, state.activeSkinId, nullptr);
    root.setProperty (activeSkinVersionProperty, state.activeSkinVersion, nullptr);
    if (state.developerSkinDirectory.isNotEmpty())
        root.setProperty (developerSkinDirectoryProperty,
                          state.developerSkinDirectory, nullptr);

    juce::ValueTree playlistTree (playlistType);
    playlistTree.setProperty (selectedIndexProperty, state.playlist.getSelectedIndex(), nullptr);
    if (const auto* selectedTrack = state.playlist.getSelectedTrack())
        playlistTree.setProperty (selectedTrackIdProperty, selectedTrack->id, nullptr);
    playlistTree.setProperty (shuffleProperty, state.playlist.isShuffleEnabled(), nullptr);
    playlistTree.setProperty (repeatProperty, toString (state.playlist.getRepeatMode()), nullptr);

    auto library = state.library;
    for (const auto& track : state.playlist.getTracks())
    {
        auto libraryId = track.libraryId;
        if (library.getTrack (libraryId) == nullptr)
            libraryId = library.upsertTrack (track);
        juce::ValueTree entry (trackType);
        entry.setProperty (idProperty, track.id, nullptr);
        entry.setProperty ("libraryId", libraryId, nullptr);
        playlistTree.appendChild (entry, nullptr);
    }
    juce::ValueTree libraryTree ("MusicLibrary");
    juce::ValueTree tracksTree ("Tracks");
    for (const auto& track : library.getTracks())
        tracksTree.appendChild (trackToTree (track), nullptr);
    libraryTree.appendChild (tracksTree, nullptr);
    juce::ValueTree savedPlaylists ("SavedPlaylists");
    for (const auto& playlist : library.getPlaylists())
    {
        juce::ValueTree saved ("SavedPlaylist");
        saved.setProperty (idProperty, playlist.id, nullptr);
        saved.setProperty ("name", playlist.name, nullptr);
        saved.setProperty (artworkCacheKeyProperty, playlist.artworkCacheKey, nullptr);
        saved.setProperty ("createdAtMs", playlist.createdAtMs, nullptr);
        saved.setProperty ("updatedAtMs", playlist.updatedAtMs, nullptr);
        if (playlist.smartRules.has_value()) saved.appendChild (smartRulesToTree (*playlist.smartRules), nullptr);
        for (const auto& entry : playlist.entries)
        {
            juce::ValueTree child ("Entry");
            child.setProperty (idProperty, entry.id, nullptr);
            child.setProperty ("trackId", entry.trackId, nullptr);
            saved.appendChild (child, nullptr);
        }
        savedPlaylists.appendChild (saved, nullptr);
    }
    libraryTree.appendChild (savedPlaylists, nullptr);
    juce::ValueTree favourites ("Favourites");
    for (const auto& id : library.getFavouriteTrackIds())
    {
        juce::ValueTree favourite ("Favourite");
        favourite.setProperty ("trackId", id, nullptr);
        favourites.appendChild (favourite, nullptr);
    }
    libraryTree.appendChild (favourites, nullptr);
    root.appendChild (libraryTree, nullptr);
    root.setProperty ("libraryCollectionId", state.libraryCollectionId, nullptr);
    root.setProperty ("librarySearchQuery", state.librarySearchQuery, nullptr);

    root.appendChild (playlistTree, nullptr);

    root.appendChild (EffectChainSerialization::toValueTree (state.effectChain), nullptr);
    return root;
}

PlayerAppState AppStateStore::fromValueTree (const juce::ValueTree& tree)
{
    PlayerAppState state;
    if (! tree.hasType (appStateType))
        return state;

    if (! restoreMusicDirectories (tree, state.musicDirectories)) return {};

    for (const auto directory : tree.getChildWithName ("PluginDirectories"))
    {
        const auto path = directory.getProperty (pathProperty).toString();
        if (directory.hasType ("Directory") && juce::File::isAbsolutePath (path))
            state.customVst3Directories.addIfNotAlreadyThere (path);
    }
    for (const auto entry : tree.getChildWithName ("OutputRoutes"))
    {
        if (! entry.hasType ("Route")) continue;
        AudioOutputRoute route;
        route.deviceIdentifier = entry.getProperty ("device").toString();
        const auto layoutName = entry.getProperty ("layout").toString();
        route.layout = outputLayoutFromString (layoutName);
        if (route.deviceIdentifier.isEmpty() || toString (route.layout) != layoutName) continue;
        for (const auto channel : entry)
            if (channel.hasType ("Channel"))
                route.channels.push_back (static_cast<int> (channel.getProperty ("port", -1)));
        state.audioDevice.outputRoutes.push_back (std::move (route));
    }
    state.volume = static_cast<double> (tree.getProperty (volumeProperty, state.volume));
    const auto formats = tree.getChildWithName ("PluginFormats");
    state.pluginFormats.vst3 = static_cast<bool> (formats.getProperty ("vst3", state.pluginFormats.vst3));
    state.pluginFormats.audioUnit = static_cast<bool> (formats.getProperty ("audioUnit", state.pluginFormats.audioUnit));
    state.playbackPositionSeconds = finiteNonNegative (static_cast<double> (
        tree.getProperty (playbackPositionProperty,
                          state.playbackPositionSeconds)));
    state.outputLayout = outputLayoutFromString (tree.getProperty (outputLayoutProperty, "stereo").toString());
    restoreCalibration (tree, state.outputCalibration);
    for (const auto channel : tree.getChildWithName ("OutputChannelMix"))
    {
        if (! channel.hasType ("Channel")) continue;
        const OutputChannelMix mix {
            static_cast<juce::AudioChannelSet::ChannelType> (static_cast<int> (channel.getProperty ("type", 0))),
            static_cast<float> (channel.getProperty ("gain", 1.0)),
            static_cast<bool> (channel.getProperty ("muted", false)),
            static_cast<bool> (channel.getProperty ("solo", false)) };
        if (! isValidOutputChannelMix (mix)) continue;
        const auto found = std::find_if (state.outputChannelMix.begin(), state.outputChannelMix.end(),
            [&] (const auto& existing) { return existing.channel == mix.channel; });
        *found = mix;
    }
    const auto device = tree.getChildWithName ("AudioDevice");
    state.audioDevice.outputDeviceName = device.getProperty ("name").toString();
    state.audioDevice.sampleRate = finiteNonNegative (
        static_cast<double> (device.getProperty (sampleRateProperty, 0.0)));
    state.audioDevice.bufferSize = juce::jmax (0,
        static_cast<int> (device.getProperty ("bufferSize", 0)));
    state.workspaceMode = workspaceModeFromString (
        tree.getProperty (workspaceModeProperty, "listening").toString());
    state.activeSkinId = tree.getProperty (
        activeSkinIdProperty, "builtin.default").toString();
    if (state.activeSkinId.isEmpty()) state.activeSkinId = "builtin.default";
    state.activeSkinVersion = tree.getProperty (
        activeSkinVersionProperty, {}).toString();
    state.developerSkinDirectory = tree.getProperty (
        developerSkinDirectoryProperty, {}).toString();

    const auto hasLibrary = tree.getChildWithName ("MusicLibrary").isValid();
    if (hasLibrary && ! restoreLibrary (tree, state.library))
        return {};
    state.libraryCollectionId = tree.getProperty ("libraryCollectionId", "all").toString();
    state.librarySearchQuery = tree.getProperty ("librarySearchQuery").toString();
    const auto playlistTree = tree.getChildWithName (playlistType);
    std::vector<juce::String> migratedIds;
    if (playlistTree.isValid())
    {
        std::vector<TrackItem> queue;
        queue.reserve (static_cast<size_t> (playlistTree.getNumChildren()));
        for (const auto trackTree : playlistTree)
        {
            if (! trackTree.hasType (trackType)) continue;
            TrackItem track;
            if (hasLibrary)
            {
                const auto libraryId = trackTree.getProperty ("libraryId").toString();
                const auto* record = state.library.getTrack (libraryId);
                if (record == nullptr) return {};
                track = *record;
                track.id = trackTree.getProperty (idProperty).toString();
                track.libraryId = libraryId;
            }
            else
            {
                track = trackFromTree (trackTree);
                track.libraryId = state.library.upsertTrack (track);
                migratedIds.push_back (track.libraryId);
            }
            queue.push_back (std::move (track));
        }
        state.playlist.insertTracks (0, std::move (queue));

        const auto selectedTrackId = playlistTree.getProperty (selectedTrackIdProperty).toString();
        const auto selectedIndex = selectedTrackId.isNotEmpty()
                                       ? state.playlist.indexOfId (selectedTrackId)
                                       : static_cast<int> (playlistTree.getProperty (selectedIndexProperty, -1));
        state.playlist.selectTrack (selectedIndex);
        state.playlist.setRepeatMode (repeatModeFromString (
            playlistTree.getProperty (repeatProperty, "off").toString()));
        state.playlist.setShuffleEnabled (static_cast<bool> (
            playlistTree.getProperty (shuffleProperty, false)));
    }

    if (! migratedIds.empty())
    {
        SavedPlaylist imported;
        imported.name = "Imported queue";
        for (const auto& id : migratedIds) imported.entries.push_back ({ juce::Uuid().toString(), id });
        // Legacy queue files contain no playlist creation or modification timestamps.
        auto migratedTracks = state.library.getTracks();
        for (auto& track : migratedTracks) track.addedAtMs = 0;
        if (! state.library.restore (std::move (migratedTracks), { std::move (imported) },
                                     state.library.getFavouriteTrackIds())) return {};
    }

    state.effectChain = EffectChainSerialization::fromValueTree (tree.getChildWithName ("EffectChain"));
    for (const auto& slot : state.effectChain.getSlots())
    {
        juce::String scopeError;
        if (! validateEffectScope (slot.scope, toAudioChannelSet (state.outputLayout), scopeError))
            state.effectChain.setAvailability (slot.id, false, scopeError);
    }

    return state;
}

bool AppStateStore::writeAtomically (const PlayerAppState& state,
                                     const juce::File& targetFile,
                                     juce::String& errorMessage)
{
    errorMessage.clear();

    if (targetFile.existsAsFile())
    {
        juce::ValueTree existing;
        if (! readStateTree (targetFile, existing, errorMessage))
        {
            errorMessage = "Existing state was preserved. " + errorMessage
                + " Move the file aside before saving a new session: "
                + targetFile.getFullPathName();
            return false;
        }
    }

    const auto parentDirectory = targetFile.getParentDirectory();
    if (! parentDirectory.isDirectory() && ! parentDirectory.createDirectory())
    {
        errorMessage = "Could not create state directory: " + parentDirectory.getFullPathName();
        return false;
    }

    const auto tree = toValueTree (state);
    std::vector<OutputChannelCalibration> calibration;
    if (! restoreCalibration (tree, calibration))
    {
        errorMessage = "Cannot save invalid output calibration.";
        return false;
    }
    std::vector<MusicDirectory> musicDirectories;
    if (! restoreMusicDirectories (tree, musicDirectories))
    {
        errorMessage = "Cannot save invalid or duplicate music directories.";
        return false;
    }
    auto xml = tree.createXml();
    if (xml == nullptr)
    {
        errorMessage = "Could not serialize player state.";
        return false;
    }

    juce::TemporaryFile temporaryFile (targetFile);
    if (! xml->writeTo (temporaryFile.getFile()))
    {
        errorMessage = "Could not write temporary state file: " + temporaryFile.getFile().getFullPathName();
        return false;
    }

    if (! temporaryFile.overwriteTargetFileWithTemporary())
    {
        errorMessage = "Could not replace player state file: " + targetFile.getFullPathName();
        return false;
    }

    return true;
}

bool AppStateStore::readFromFile (const juce::File& sourceFile,
                                  PlayerAppState& state,
                                  juce::String& errorMessage)
{
    errorMessage.clear();
    juce::ValueTree tree;
    if (! readStateTree (sourceFile, tree, errorMessage))
        return false;

    state = fromValueTree (tree);
    return true;
}
