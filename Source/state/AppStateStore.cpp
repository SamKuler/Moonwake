#include "AppStateStore.h"

#include "app/ProductIdentity.h"

#include <cmath>

namespace
{
const juce::Identifier appStateType { MoonwakeIdentity::stateRootType };
const juce::Identifier playlistType { "Playlist" };
const juce::Identifier trackType { "Track" };
const juce::Identifier effectChainType { "EffectChain" };
const juce::Identifier slotType { "Slot" };

const juce::Identifier schemaVersionProperty { "schemaVersion" };
constexpr int currentSchemaVersion = 8;
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
const juce::Identifier pluginNameProperty { "pluginName" };
const juce::Identifier pluginIdentifierProperty { "pluginIdentifier" };
const juce::Identifier pluginFormatProperty { "pluginFormat" };
const juce::Identifier bypassedProperty { "bypassed" };
const juce::Identifier availablePropertyForSlot { "available" };
const juce::Identifier errorMessageProperty { "errorMessage" };
const juce::Identifier serializedStateProperty { "serializedState" };

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

    return true;
}
} // namespace

juce::ValueTree AppStateStore::toValueTree (const PlayerAppState& state)
{
    juce::ValueTree root (appStateType);
    root.setProperty (schemaVersionProperty, currentSchemaVersion, nullptr);
    root.setProperty (volumeProperty, state.volume, nullptr);
    root.setProperty (playbackPositionProperty,
                      finiteNonNegative (state.playbackPositionSeconds), nullptr);
    root.setProperty (outputLayoutProperty, toString (state.outputLayout), nullptr);
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

    for (const auto& track : state.playlist.getTracks())
    {
        juce::ValueTree trackTree (trackType);
        trackTree.setProperty (pathProperty, track.file.getFullPathName(), nullptr);
        trackTree.setProperty (idProperty, track.id, nullptr);
        trackTree.setProperty (titleProperty, track.title, nullptr);
        trackTree.setProperty (artistProperty, track.artist, nullptr);
        trackTree.setProperty (albumProperty, track.album, nullptr);
        trackTree.setProperty (albumArtistProperty, track.albumArtist, nullptr);
        trackTree.setProperty (genreProperty, track.genre, nullptr);
        trackTree.setProperty (yearProperty, track.year, nullptr);
        trackTree.setProperty (trackNumberProperty, track.trackNumber, nullptr);
        trackTree.setProperty (discNumberProperty, track.discNumber, nullptr);
        trackTree.setProperty (durationProperty, track.durationSeconds, nullptr);
        trackTree.setProperty (sampleRateProperty, track.sampleRate, nullptr);
        trackTree.setProperty (channelCountProperty, track.channelCount, nullptr);
        trackTree.setProperty (bitDepthProperty, track.bitDepth, nullptr);
        trackTree.setProperty (formatNameProperty, track.formatName, nullptr);
        trackTree.setProperty (artworkCacheKeyProperty, track.artworkCacheKey, nullptr);
        trackTree.setProperty (availableProperty, track.available, nullptr);
        playlistTree.appendChild (trackTree, nullptr);
    }

    root.appendChild (playlistTree, nullptr);

    juce::ValueTree chainTree (effectChainType);
    for (const auto& slot : state.effectChain.getSlots())
    {
        juce::ValueTree slotTree (slotType);
        slotTree.setProperty (idProperty, slot.id, nullptr);
        slotTree.setProperty (pluginNameProperty, slot.pluginName, nullptr);
        slotTree.setProperty (pluginIdentifierProperty, slot.pluginIdentifier, nullptr);
        slotTree.setProperty (pluginFormatProperty, slot.pluginFormat, nullptr);
        slotTree.setProperty (bypassedProperty, slot.bypassed, nullptr);
        slotTree.setProperty (availablePropertyForSlot, slot.available, nullptr);
        slotTree.setProperty (errorMessageProperty, slot.errorMessage, nullptr);
        if (slot.serializedState.getSize() > 0)
            slotTree.setProperty (serializedStateProperty, slot.serializedState.toBase64Encoding(), nullptr);
        chainTree.appendChild (slotTree, nullptr);
    }

    root.appendChild (chainTree, nullptr);
    return root;
}

PlayerAppState AppStateStore::fromValueTree (const juce::ValueTree& tree)
{
    PlayerAppState state;
    if (! tree.hasType (appStateType))
        return state;

    state.volume = static_cast<double> (tree.getProperty (volumeProperty, state.volume));
    state.playbackPositionSeconds = finiteNonNegative (static_cast<double> (
        tree.getProperty (playbackPositionProperty,
                          state.playbackPositionSeconds)));
    state.outputLayout = outputLayoutFromString (tree.getProperty (outputLayoutProperty, "stereo").toString());
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

    const auto playlistTree = tree.getChildWithName (playlistType);
    if (playlistTree.isValid())
    {
        for (int i = 0; i < playlistTree.getNumChildren(); ++i)
        {
            const auto trackTree = playlistTree.getChild (i);
            if (! trackTree.hasType (trackType))
                continue;

            TrackItem track { juce::File (trackTree.getProperty (pathProperty).toString()),
                              trackTree.getProperty (titleProperty).toString(),
                              static_cast<double> (trackTree.getProperty (durationProperty, 0.0)) };
            const auto restoredId = trackTree.getProperty (idProperty).toString();
            if (restoredId.isNotEmpty())
                track.id = restoredId;
            track.artist = trackTree.getProperty (artistProperty).toString();
            track.album = trackTree.getProperty (albumProperty).toString();
            track.albumArtist = trackTree.getProperty (albumArtistProperty).toString();
            track.genre = trackTree.getProperty (genreProperty).toString();
            track.year = juce::jmax (0, static_cast<int> (
                trackTree.getProperty (yearProperty, 0)));
            track.trackNumber = juce::jmax (0, static_cast<int> (
                trackTree.getProperty (trackNumberProperty, 0)));
            track.discNumber = juce::jmax (0, static_cast<int> (
                trackTree.getProperty (discNumberProperty, 0)));
            track.sampleRate = static_cast<double> (trackTree.getProperty (sampleRateProperty, 0.0));
            track.channelCount = static_cast<int> (trackTree.getProperty (channelCountProperty, 0));
            track.bitDepth = juce::jmax (0, static_cast<int> (
                trackTree.getProperty (bitDepthProperty, 0)));
            track.formatName = trackTree.getProperty (formatNameProperty).toString();
            track.artworkCacheKey = trackTree.getProperty (
                artworkCacheKeyProperty).toString();
            track.available = static_cast<bool> (trackTree.getProperty (availableProperty, true));
            state.playlist.addTrack (std::move (track));
        }

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

    const auto chainTree = tree.getChildWithName (effectChainType);
    if (chainTree.isValid())
    {
        for (int i = 0; i < chainTree.getNumChildren(); ++i)
        {
            const auto slotTree = chainTree.getChild (i);
            if (! slotTree.hasType (slotType))
                continue;

            const auto savedId = static_cast<int> (slotTree.getProperty (idProperty, 0));
            const auto pluginName = slotTree.getProperty (pluginNameProperty).toString();
            const auto pluginIdentifier = slotTree.getProperty (pluginIdentifierProperty).toString();
            const auto pluginFormat = slotTree.getProperty (pluginFormatProperty).toString();
            const auto id = savedId > 0
                                ? state.effectChain.addSlotWithId (savedId,
                                                                   pluginName,
                                                                   pluginFormat,
                                                                   pluginIdentifier)
                                : state.effectChain.addSlot (pluginName,
                                                             pluginIdentifier,
                                                             pluginFormat);

            if (id == 0)
                continue;

            state.effectChain.setBypassed (id, static_cast<bool> (slotTree.getProperty (bypassedProperty, false)));
            state.effectChain.setAvailability (id,
                                               static_cast<bool> (slotTree.getProperty (availablePropertyForSlot, true)),
                                               slotTree.getProperty (errorMessageProperty).toString());

            if (auto* slot = state.effectChain.getSlotById (id))
                slot->serializedState.fromBase64Encoding (slotTree.getProperty (serializedStateProperty).toString());
        }
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

    auto xml = toValueTree (state).createXml();
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
