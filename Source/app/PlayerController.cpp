#include "PlayerController.h"

#include "app/ProductPaths.h"

#include <cmath>

namespace
{
thread_local const PlayerController* synchronousPluginStateCaptureOwner =
    nullptr;

class ScopedSynchronousPluginStateCapture final
{
public:
    explicit ScopedSynchronousPluginStateCapture (
        const PlayerController* owner)
        : previousOwner (synchronousPluginStateCaptureOwner)
    {
        synchronousPluginStateCaptureOwner = owner;
    }

    ~ScopedSynchronousPluginStateCapture()
    {
        synchronousPluginStateCaptureOwner = previousOwner;
    }

private:
    const PlayerController* previousOwner = nullptr;
};
} // namespace

PlayerController::PlayerController()
    : artworkStore (ProductPaths::artworkCacheRoot())
{
    const auto error = audioEngine.initialise();
    lastStatusMessage = error.isEmpty() ? "Ready" : "Audio device error: " + error;
}

PlayerController::PlayerController (std::unique_ptr<IAudioDeviceBackend> backend)
    : audioEngine (std::move (backend)),
      artworkStore (ProductPaths::artworkCacheRoot())
{
    const auto error = audioEngine.initialise();
    lastStatusMessage = error.isEmpty() ? "Ready" : "Audio device error: " + error;
}

PlayerController::~PlayerController()
{
    lifetimeFlag->store (false);
    audioEngine.shutdown();
    static_cast<void> (audioEngine.clearEffectPlugins());
    static_cast<void> (pluginManager.cancelAndWaitForScan());
}

juce::String PlayerController::addFiles (const juce::Array<juce::File>& files)
{
    std::vector<TrackMetadataResult> prepared;
    juce::StringArray skippedFiles;

    for (const auto& file : files)
    {
        if (playlist.containsFile (file))
        {
            skippedFiles.add (file.getFileName() + " (already in queue)");
            continue;
        }

        auto metadata = metadataReader.read (file);
        if (! metadata.ok)
        {
            skippedFiles.add (file.getFileName() + " (" + metadata.error + ")");
            continue;
        }

        prepared.push_back (std::move (metadata));
    }

    auto message = addPreparedTracks (std::move (prepared));

    if (! skippedFiles.isEmpty())
        message += " Skipped: " + skippedFiles.joinIntoString (", ");
    lastStatusMessage = message;
    return lastStatusMessage;
}

juce::String PlayerController::addPreparedTracks (
    std::vector<TrackMetadataResult> preparedTracks)
{
    int addedCount = 0;
    juce::StringArray skippedFiles;
    juce::StringArray artworkWarnings;
    for (auto& prepared : preparedTracks)
    {
        auto& track = prepared.track;
        if (! prepared.ok || ! track.available)
        {
            static_cast<void> (artworkStore.remove (track.artworkCacheKey));
            skippedFiles.add (track.file.getFileName() + " ("
                              + (prepared.error.isNotEmpty()
                                     ? prepared.error : "unreadable") + ")");
            continue;
        }
        if (playlist.containsFile (track.file))
        {
            static_cast<void> (artworkStore.remove (track.artworkCacheKey));
            skippedFiles.add (track.file.getFileName() + " (already in queue)");
            continue;
        }
        if (prepared.artworkData.getSize() > 0)
        {
            juce::String artworkError;
            track.artworkCacheKey = artworkStore.store (
                track.id, prepared.artworkData, artworkError);
            if (track.artworkCacheKey.isEmpty() && artworkError.isNotEmpty())
                artworkWarnings.add (track.file.getFileName() + " (" + artworkError + ")");
        }
        playlist.addTrack (std::move (track));
        ++addedCount;
    }

    lastStatusMessage = "Added " + juce::String (addedCount)
        + (addedCount == 1 ? " track." : " tracks.");
    if (! skippedFiles.isEmpty())
        lastStatusMessage += " Skipped: " + skippedFiles.joinIntoString (", ");
    if (! artworkWarnings.isEmpty())
        lastStatusMessage += " Artwork warnings: "
                           + artworkWarnings.joinIntoString (", ");
    if (addedCount > 0)
        ++stateRevision;
    return lastStatusMessage;
}

bool PlayerController::selectTrack (int index)
{
    const auto* requestedTrack = playlist.getTrack (index);
    if (requestedTrack == nullptr)
        return false;

    if (index == playlist.getSelectedIndex())
        return true;

    if (! requestedTrack->available)
    {
        lastStatusMessage = "Audio file is unavailable: " + requestedTrack->file.getFullPathName();
        return false;
    }

    if (! playlist.selectTrack (index))
        return false;
    ++stateRevision;

    const auto action = playbackCoordinator.selectionChanged (requestedTrack->id);
    if (! execute (action))
        return false;
    playbackCheckpoint.reset (requestedTrack->id,
                              audioEngine.getCurrentPositionSeconds());

    if (action.type != PlaybackActionType::none)
        lastStatusMessage = (playbackCoordinator.getIntent() == TransportIntent::playing ? "Playing " : "Loaded ")
            + requestedTrack->title;

    return true;
}

bool PlayerController::playOrPause()
{
    if (playlist.getSelectedTrack() == nullptr)
    {
        lastStatusMessage = "Add and select an audio file first.";
        return false;
    }

    const auto action = playbackCoordinator.togglePlay (getSelectedTrackId());
    if (! execute (action))
        return false;
    playbackCheckpoint.reset (getSelectedTrackId(),
                              audioEngine.getCurrentPositionSeconds());
    ++stateRevision;

    lastStatusMessage = playbackCoordinator.getIntent() == TransportIntent::playing
                            ? "Playing " + playlist.getSelectedTrack()->title
                            : "Paused.";
    return true;
}

bool PlayerController::nextTrack()
{
    if (! playlist.advanceToNextTrack())
    {
        lastStatusMessage = "No next track.";
        return false;
    }
    ++stateRevision;

    const auto action = playbackCoordinator.moveToTrack (getSelectedTrackId(), false);
    if (! execute (action))
        return false;
    playbackCheckpoint.reset (getSelectedTrackId(),
                              audioEngine.getCurrentPositionSeconds());

    lastStatusMessage = playbackCoordinator.getIntent() == TransportIntent::playing
                            ? "Playing " + playlist.getSelectedTrack()->title
                            : "Selected " + playlist.getSelectedTrack()->title;
    return true;
}

bool PlayerController::previousTrack()
{
    const auto selected = playlist.getSelectedIndex();
    juce::String previousTrackId;
    for (auto index = selected - 1; index >= 0; --index)
        if (const auto* track = playlist.getTrack (index); track != nullptr && track->available)
        {
            previousTrackId = track->id;
            break;
        }
    const auto action = playbackCoordinator.previous (previousTrackId, audioEngine.getCurrentPositionSeconds());

    if (action.trackId.isNotEmpty() && action.trackId != getSelectedTrackId())
    {
        playlist.selectTrack (playlist.indexOfId (action.trackId));
        ++stateRevision;
    }

    if (! execute (action))
        return false;

    if (action.type == PlaybackActionType::seekToStart)
        ++stateRevision;
    playbackCheckpoint.reset (playbackCoordinator.getLoadedTrackId(),
                              audioEngine.getCurrentPositionSeconds());

    if (action.type == PlaybackActionType::none)
    {
        lastStatusMessage = "No previous track.";
        return false;
    }

    lastStatusMessage = action.type == PlaybackActionType::seekToStart
                            ? "Restarted current track."
                            : (playbackCoordinator.getIntent() == TransportIntent::playing ? "Playing " : "Selected ")
                                  + playlist.getSelectedTrack()->title;
    return true;
}

bool PlayerController::handlePlaybackFinished()
{
    const auto hasNextTrack = playlist.advanceToNextTrack();
    if (hasNextTrack)
        ++stateRevision;
    const auto action = playbackCoordinator.playbackFinished (hasNextTrack ? getSelectedTrackId() : juce::String{});

    if (! execute (action))
        return false;
    if (! hasNextTrack)
        ++stateRevision;
    playbackCheckpoint.reset (playbackCoordinator.getLoadedTrackId(),
                              audioEngine.getCurrentPositionSeconds());

    lastStatusMessage = hasNextTrack ? "Playing " + playlist.getSelectedTrack()->title : "End of queue.";
    return hasNextTrack;
}

bool PlayerController::removeTrack (int index)
{
    const auto* track = playlist.getTrack (index);
    if (track == nullptr)
        return false;

    const auto removedLoadedTrack = track->id == playbackCoordinator.getLoadedTrackId();
    const auto removedTitle = track->title;
    const auto removedArtworkKey = track->artworkCacheKey;
    if (! playlist.removeTrack (index))
        return false;
    static_cast<void> (artworkStore.remove (removedArtworkKey));
    ++stateRevision;

    if (removedLoadedTrack)
    {
        audioEngine.unload();
        playbackCoordinator.loadFailed();
        playbackCheckpoint.reset ({}, 0.0);
    }

    lastStatusMessage = "Removed " + removedTitle + ".";
    return true;
}

bool PlayerController::moveTrack (int fromIndex, int toIndex)
{
    if (! playlist.moveTrack (fromIndex, toIndex))
        return false;

    ++stateRevision;
    lastStatusMessage = "Queue reordered.";
    return true;
}

void PlayerController::clearPlaylist()
{
    audioEngine.unload();
    playbackCoordinator.loadFailed();
    playbackCheckpoint.reset ({}, 0.0);
    for (const auto& track : playlist.getTracks())
        static_cast<void> (artworkStore.remove (track.artworkCacheKey));
    playlist.clear();
    ++stateRevision;
    lastStatusMessage = "Queue cleared.";
}

void PlayerController::seekToRatio (double ratio)
{
    seekToSeconds (juce::jlimit (0.0, 1.0, ratio)
                   * audioEngine.getLengthInSeconds());
}

void PlayerController::seekToSeconds (double seconds)
{
    const auto length = audioEngine.getLengthInSeconds();
    if (! std::isfinite (seconds) || length <= 0.0)
        return;

    const auto position = juce::jlimit (0.0, length, seconds);
    if (std::abs (position - audioEngine.getCurrentPositionSeconds()) < 0.01)
        return;

    audioEngine.seekToSeconds (position);
    playbackCheckpoint.reset (playbackCoordinator.getLoadedTrackId(), position);
    ++stateRevision;
}

void PlayerController::checkpointPlaybackPosition()
{
    if (playbackCheckpoint.observe (
            playbackCoordinator.getLoadedTrackId(),
            audioEngine.getCurrentPositionSeconds(), audioEngine.isPlaying()))
        ++stateRevision;
}

void PlayerController::setShuffleEnabled (bool shouldShuffle)
{
    if (playlist.isShuffleEnabled() == shouldShuffle)
        return;
    playlist.setShuffleEnabled (shouldShuffle);
    ++stateRevision;
}

void PlayerController::setRepeatMode (RepeatMode mode)
{
    if (playlist.getRepeatMode() == mode)
        return;
    playlist.setRepeatMode (mode);
    ++stateRevision;
}

void PlayerController::setVolume (double volume)
{
    if (juce::approximatelyEqual (audioEngine.getGain(), volume))
        return;
    audioEngine.setGain (volume);
    ++stateRevision;
}

void PlayerController::setSkinPersistenceState (SkinPersistenceState state)
{
    if (state.activeSkinId.isEmpty())
        state.activeSkinId = "builtin.default";

    if (skinPersistenceState.activeSkinId == state.activeSkinId
        && skinPersistenceState.activeSkinVersion == state.activeSkinVersion
        && skinPersistenceState.developerDirectory == state.developerDirectory)
        return;
    skinPersistenceState = std::move (state);
    ++stateRevision;
}

juce::String PlayerController::setOutputLayout (OutputLayout layout)
{
    const auto error = audioEngine.setOutputLayout (layout);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Output layout set to " + toString (audioEngine.getOutputLayout()) + ".";
    if (error.isEmpty())
        ++stateRevision;
    return error;
}

juce::String PlayerController::selectOutputDevice (
    const juce::String& deviceName)
{
    const auto error = audioEngine.selectOutputDevice (deviceName);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Output device set to "
                                + audioEngine.getCurrentAudioDeviceName() + ".";
    return error;
}

juce::String PlayerController::setAudioSampleRate (double sampleRate)
{
    const auto error = audioEngine.setSampleRate (sampleRate);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Sample rate set to "
                                + juce::String (sampleRate, 0) + " Hz.";
    return error;
}

juce::String PlayerController::setAudioBufferSize (int bufferSize)
{
    const auto error = audioEngine.setBufferSize (bufferSize);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Buffer size set to "
                                + juce::String (bufferSize) + " samples.";
    return error;
}

void PlayerController::setWorkspaceMode (WorkspaceMode mode)
{
    if (workspaceMode == mode)
        return;
    workspaceMode = mode;
    ++stateRevision;
}

int PlayerController::scanPluginsInDirectory (const juce::File& directory)
{
    return pluginManager.scanDirectory (directory);
}

bool PlayerController::startPluginScan (const juce::File& customVst3Directory)
{
    const auto started = pluginManager.startScan (customVst3Directory);
    lastStatusMessage = started ? "Plug-in scan started." : "Could not start plug-in scan.";
    return started;
}

void PlayerController::cancelPluginScan()
{
    pluginManager.cancelScan();
}

PluginScanStatus PlayerController::getPluginScanStatus() const
{
    return pluginManager.getScanStatus();
}

void PlayerController::addPluginByIndexAsync (int pluginIndex, PluginInsertionCallback callback)
{
    const auto plugins = getInsertEffectPlugins();
    if (pluginIndex < 0 || pluginIndex >= plugins.size())
    {
        callback (false, "No plug-in selected.");
        return;
    }

    if (restoringEffects)
    {
        callback (false, "Wait for the saved effect chain to finish restoring.");
        return;
    }

    const auto description = plugins[pluginIndex];
    const auto alive = lifetimeFlag;
    pluginManager.createPluginInstanceAsync (
        description,
        44100.0,
        512,
        [this, alive, description, completion = std::move (callback)]
        (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& creationError) mutable
        {
            if (! alive->load())
                return;

            if (plugin == nullptr)
            {
                completion (false,
                            creationError.isNotEmpty() ? creationError
                                                       : "The selected plug-in could not be instantiated.");
                return;
            }

            const auto slotId = effectChain.addSlot (description.name,
                                                     description.createIdentifierString(),
                                                     description.pluginFormatName);
            juce::String graphError;
            plugin->addListener (this);
            if (! audioEngine.addEffectPlugin (slotId, std::move (plugin), graphError))
            {
                effectChain.removeSlot (slotId);
                completion (false, graphError);
                return;
            }

            ++effectChainRevision;
            ++stateRevision;
            pluginStateRevision.fetch_add (1, std::memory_order_relaxed);
            lastStatusMessage = "Inserted " + description.name + ".";
            completion (true, lastStatusMessage);
        });
}

bool PlayerController::removeEffectAtIndex (int effectIndex)
{
    const auto* slot = effectChain.getSlot (effectIndex);
    if (slot == nullptr)
        return false;

    const auto slotId = slot->id;
    if (slot->available)
    {
        if (! audioEngine.beginEffectStateCapture())
        {
            lastStatusMessage =
                "Timed out while preparing the effect for removal.";
            return false;
        }

        struct CaptureScope
        {
            AudioEngine& engine;
            ~CaptureScope() { engine.endEffectStateCapture(); }
        } captureScope { audioEngine };

        if (! audioEngine.removeEffectPlugin (slotId))
            return false;
    }

    if (! effectChain.removeSlot (slotId))
        return false;

    ++effectChainRevision;
    ++stateRevision;
    return true;
}

bool PlayerController::moveEffect (int fromIndex, int toIndex)
{
    if (effectChain.getSlot (fromIndex) == nullptr || effectChain.getSlot (toIndex) == nullptr)
        return false;

    if (! effectChain.moveSlot (fromIndex, toIndex))
        return false;

    if (! audioEngine.setEffectPluginOrder (getAvailableEffectSlotIds()))
    {
        static_cast<void> (effectChain.moveSlot (toIndex, fromIndex));
        return false;
    }

    ++effectChainRevision;
    ++stateRevision;
    return true;
}

bool PlayerController::setEffectBypassedAtIndex (int effectIndex, bool shouldBypass)
{
    const auto* slot = effectChain.getSlot (effectIndex);
    if (slot == nullptr)
        return false;

    if (slot->available && ! audioEngine.setEffectBypassed (slot->id, shouldBypass))
        return false;

    if (! effectChain.setBypassed (slot->id, shouldBypass))
        return false;

    ++effectChainRevision;
    ++stateRevision;
    return true;
}

bool PlayerController::saveState (const juce::File& stateFile, const juce::File& pluginListFile)
{
    if (! pluginManager.cancelAndWaitForScan())
    {
        lastStatusMessage = "Timed out while stopping the plug-in scan; state was not saved.";
        return false;
    }

    juce::String stateError;
    PlayerAppState snapshot;
    const auto stateCaptured = captureStateSnapshot (snapshot, stateError);
    const auto stateSaved = stateCaptured
                         && AppStateStore::writeAtomically (
                                snapshot, stateFile, stateError);
    juce::String pluginError;
    const auto pluginCacheSaved = pluginManager.writeKnownPluginCacheAtomically (pluginListFile, pluginError);

    juce::StringArray failures;
    if (! stateSaved)
        failures.add ("Application state: " + stateError);
    if (! pluginCacheSaved)
        failures.add ("Plug-in cache: " + pluginError);

    if (! failures.isEmpty())
    {
        lastStatusMessage = "Could not save Moonwake data:\n"
                          + failures.joinIntoString ("\n");
        return false;
    }

    lastStatusMessage = "Application state and plug-in cache saved.";
    return true;
}

bool PlayerController::loadState (const juce::File& stateFile, const juce::File& pluginListFile)
{
    if (pluginListFile.existsAsFile())
    {
        juce::String pluginError;
        if (! pluginManager.readKnownPluginCache (pluginListFile, pluginError))
            lastStatusMessage = pluginError;
    }

    if (! stateFile.existsAsFile())
        return false;

    PlayerAppState restoredState;
    juce::String stateError;
    if (! AppStateStore::readFromFile (stateFile, restoredState, stateError))
    {
        lastStatusMessage = stateError;
        return false;
    }

    return restoreStateSnapshot (restoredState);
}

const PlaylistModel& PlayerController::getPlaylist() const
{
    return playlist;
}

const EffectChainState& PlayerController::getEffectChain() const
{
    return effectChain;
}

juce::Array<juce::PluginDescription> PlayerController::getKnownPlugins() const
{
    return pluginManager.getKnownPlugins();
}

juce::Array<juce::PluginDescription> PlayerController::getInsertEffectPlugins() const
{
    return PluginManager::filterInsertEffects (pluginManager.getKnownPlugins());
}

juce::StringArray PlayerController::getRegisteredPluginFormats() const
{
    return pluginManager.getRegisteredFormatNames();
}

juce::AudioPluginInstance* PlayerController::getEffectPluginForEditor (int effectIndex) const
{
    const auto* slot = effectChain.getSlot (effectIndex);
    return slot != nullptr && slot->available ? audioEngine.getEffectPlugin (slot->id) : nullptr;
}

uint64_t PlayerController::getEffectChainRevision() const
{
    return effectChainRevision;
}

double PlayerController::getPlaybackRatio() const
{
    const auto length = audioEngine.getLengthInSeconds();
    if (length <= 0.0)
        return 0.0;

    return juce::jlimit (0.0, 1.0, audioEngine.getCurrentPositionSeconds() / length);
}

juce::String PlayerController::getPlaybackTimeText() const
{
    const auto formatTime = [] (double seconds)
    {
        const auto totalSeconds = juce::jmax (0, static_cast<int> (std::round (seconds)));
        return juce::String (totalSeconds / 60) + ":" + juce::String (totalSeconds % 60).paddedLeft ('0', 2);
    };

    return formatTime (audioEngine.getCurrentPositionSeconds()) + " / " + formatTime (audioEngine.getLengthInSeconds());
}

double PlayerController::getCurrentPositionSeconds() const
{
    return audioEngine.getCurrentPositionSeconds();
}

double PlayerController::getLengthInSeconds() const
{
    return audioEngine.getLengthInSeconds();
}

juce::String PlayerController::getLastStatusMessage() const
{
    return lastStatusMessage;
}

AudioDeviceSettingsView PlayerController::getAudioDeviceSettings() const
{
    return audioEngine.getDeviceSettings();
}

bool PlayerController::isPlaying() const
{
    return audioEngine.isPlaying();
}

PlayerViewState PlayerController::getViewState() const
{
    PlayerViewState state;
    state.selectedIndex = playlist.getSelectedIndex();
    state.playlistSize = playlist.size();

    if (const auto* track = playlist.getSelectedTrack())
    {
        state.selectedTrackId = track->id;
        state.selectedTrackTitle = track->title;
        state.selectedTrackArtist = track->artist;
        state.selectedTrackAlbum = track->album;
        state.selectedTrackAlbumArtist = track->albumArtist;
        state.selectedTrackGenre = track->genre;
        state.selectedTrackYear = track->year;
        state.selectedTrackNumber = track->trackNumber;
        state.selectedTrackDiscNumber = track->discNumber;
        state.selectedTrackBitDepth = track->bitDepth;
        state.selectedTrackChannelCount = track->channelCount;
        state.selectedTrackSampleRate = track->sampleRate;
        state.selectedTrackDurationSeconds = track->durationSeconds;
        state.selectedTrackFormat = track->formatName;
        state.selectedTrackArtworkPath = artworkStore.resolve (
            track->artworkCacheKey).getFullPathName();
        state.selectedTrackPath = track->file.getFullPathName();
        state.selectedTrackAvailable = track->available;
    }

    state.transportIntent = playbackCoordinator.getIntent();
    state.isPlaying = audioEngine.isPlaying();
    state.positionSeconds = audioEngine.getCurrentPositionSeconds();
    state.lengthSeconds = audioEngine.getLengthInSeconds();
    state.seekRatio = state.lengthSeconds > 0.0
                          ? juce::jlimit (0.0, 1.0, state.positionSeconds / state.lengthSeconds)
                          : 0.0;
    state.volume = audioEngine.getGain();
    state.shuffleEnabled = playlist.isShuffleEnabled();
    state.repeatMode = playlist.getRepeatMode();
    state.outputLayout = audioEngine.getOutputLayout();
    state.availableOutputChannels = audioEngine.getAvailableOutputChannelCount();
    state.audioDeviceName = audioEngine.getCurrentAudioDeviceName();
    state.audioSampleRate = audioEngine.getCurrentAudioSampleRate();
    state.audioBufferSize = audioEngine.getCurrentAudioBufferSize();
    state.outputPeakLevels = audioEngine.getOutputPeakLevels();
    state.workspaceMode = workspaceMode;
    state.statusMessage = lastStatusMessage;
    return state;
}

SkinPersistenceState PlayerController::getSkinPersistenceState() const
{
    return skinPersistenceState;
}

uint64_t PlayerController::getStateRevision() const noexcept
{
    return stateRevision
         + pluginStateRevision.load (std::memory_order_relaxed);
}

bool PlayerController::execute (const PlaybackAction& action)
{
    switch (action.type)
    {
        case PlaybackActionType::load:
            return loadTrackById (action.trackId, false);
        case PlaybackActionType::loadAndPlay:
            return loadTrackById (action.trackId, true);
        case PlaybackActionType::play:
            audioEngine.play();
            return true;
        case PlaybackActionType::pause:
            audioEngine.pause();
            return true;
        case PlaybackActionType::seekToStart:
            audioEngine.seekToSeconds (0.0);
            return true;
        case PlaybackActionType::stop:
            audioEngine.stop();
            return true;
        case PlaybackActionType::none:
            return true;
    }

    return false;
}

bool PlayerController::loadTrackById (const juce::String& trackId, bool shouldPlay)
{
    const auto trackIndex = playlist.indexOfId (trackId);
    const auto* track = playlist.getTrack (trackIndex);
    if (track == nullptr || ! track->available)
    {
        lastStatusMessage = "Audio file is unavailable.";
        audioEngine.unload();
        playbackCoordinator.loadFailed();
        return false;
    }

    if (! audioEngine.loadFile (track->file))
    {
        lastStatusMessage = "Could not load audio file: " + track->file.getFullPathName();
        audioEngine.unload();
        playbackCoordinator.loadFailed();
        return false;
    }

    if (shouldPlay)
        audioEngine.play();

    return true;
}

juce::String PlayerController::getSelectedTrackId() const
{
    if (const auto* track = playlist.getSelectedTrack())
        return track->id;

    return {};
}

bool PlayerController::captureStateSnapshot (PlayerAppState& state,
                                             juce::String& errorMessage)
{
    errorMessage.clear();
    state = {};
    state.playlist = playlist;
    state.effectChain = effectChain;

    const auto pluginRevision = pluginStateRevision.load (
        std::memory_order_acquire);
    if (pluginRevision != capturedPluginStateRevision)
    {
        if (! audioEngine.beginEffectStateCapture())
        {
            errorMessage = "Timed out while preparing plug-ins for state capture.";
            return false;
        }

        struct CaptureScope
        {
            AudioEngine& engine;
            ~CaptureScope()
            {
                engine.endEffectStateCapture();
            }
        } captureScope { audioEngine };

        try
        {
            for (const auto& slot : effectChain.getSlots())
            {
                auto* savedSlot = state.effectChain.getSlotById (slot.id);
                auto* plugin = audioEngine.getEffectPlugin (slot.id);
                if (savedSlot != nullptr && plugin != nullptr)
                {
                    savedSlot->serializedState.reset();
                    const ScopedSynchronousPluginStateCapture
                        suppressSynchronousNotification (this);
                    const juce::ScopedLock callbackLock (
                        plugin->getCallbackLock());
                    plugin->getStateInformation (
                        savedSlot->serializedState);
                }

                if (savedSlot != nullptr)
                    if (auto* cachedSlot = effectChain.getSlotById (slot.id))
                        cachedSlot->serializedState =
                            savedSlot->serializedState;
            }

            capturedPluginStateRevision = pluginRevision;
        }
        catch (...)
        {
            errorMessage = "A plug-in failed while serializing its state.";
            return false;
        }
    }

    state.volume = audioEngine.getGain();
    state.outputLayout = audioEngine.getOutputLayout();
    state.workspaceMode = workspaceMode;
    state.activeSkinId = skinPersistenceState.activeSkinId;
    state.activeSkinVersion = skinPersistenceState.activeSkinVersion;
    state.developerSkinDirectory =
        skinPersistenceState.developerDirectory.getFullPathName();
    const auto selectedTrackId = getSelectedTrackId();
    if (playbackCoordinator.getLoadedTrackId() == selectedTrackId)
    {
        const auto position = juce::jlimit (
            0.0, juce::jmax (0.0, audioEngine.getLengthInSeconds()),
            audioEngine.getCurrentPositionSeconds());
        state.playbackPositionSeconds = position;
        playbackCheckpoint.reset (selectedTrackId, position);
    }
    else if (const auto checkpoint =
                 playbackCheckpoint.positionForTrack (selectedTrackId))
    {
        state.playbackPositionSeconds = *checkpoint;
    }
    return true;
}

bool PlayerController::restoreStateSnapshot (const PlayerAppState& state)
{
    const auto selectedTrackId = state.playlist.getSelectedTrack() != nullptr
                                     ? state.playlist.getSelectedTrack()->id
                                     : juce::String{};

    PlaylistModel restoredPlaylist;
    restoredPlaylist.setShuffleEnabled (state.playlist.isShuffleEnabled());
    restoredPlaylist.setRepeatMode (state.playlist.getRepeatMode());

    for (const auto& savedTrack : state.playlist.getTracks())
    {
        auto restoredTrack = savedTrack;
        const auto needsArtworkCache = restoredTrack.artworkCacheKey.isEmpty()
                                       || ! artworkStore.isValid (
                                              restoredTrack.artworkCacheKey);
        if (needsArtworkCache && restoredTrack.artworkCacheKey.isNotEmpty())
            static_cast<void> (artworkStore.remove (
                restoredTrack.artworkCacheKey));
        const auto metadata = metadataReader.read (savedTrack.file,
                                                   needsArtworkCache);

        if (metadata.ok)
        {
            restoredTrack.durationSeconds = metadata.track.durationSeconds;
            restoredTrack.sampleRate = metadata.track.sampleRate;
            restoredTrack.channelCount = metadata.track.channelCount;
            restoredTrack.bitDepth = metadata.track.bitDepth;
            restoredTrack.formatName = metadata.track.formatName;
            const auto updateText = [] (juce::String& destination,
                                        const juce::String& refreshed)
            {
                if (refreshed.isNotEmpty()) destination = refreshed;
            };
            updateText (restoredTrack.title, metadata.track.title);
            updateText (restoredTrack.artist, metadata.track.artist);
            updateText (restoredTrack.album, metadata.track.album);
            updateText (restoredTrack.albumArtist, metadata.track.albumArtist);
            updateText (restoredTrack.genre, metadata.track.genre);
            if (metadata.track.year > 0) restoredTrack.year = metadata.track.year;
            if (metadata.track.trackNumber > 0)
                restoredTrack.trackNumber = metadata.track.trackNumber;
            if (metadata.track.discNumber > 0)
                restoredTrack.discNumber = metadata.track.discNumber;
            if (needsArtworkCache && metadata.artworkData.getSize() > 0)
            {
                juce::String artworkError;
                const auto refreshedKey = artworkStore.store (
                    restoredTrack.id, metadata.artworkData, artworkError);
                if (refreshedKey.isNotEmpty())
                    restoredTrack.artworkCacheKey = refreshedKey;
            }
            else if (needsArtworkCache)
                restoredTrack.artworkCacheKey.clear();
            restoredTrack.available = true;
        }
        else
        {
            restoredTrack.available = false;
        }

        restoredPlaylist.addTrack (std::move (restoredTrack));
    }

    if (selectedTrackId.isNotEmpty())
        restoredPlaylist.selectTrack (restoredPlaylist.indexOfId (selectedTrackId));

    if (audioEngine.getEffectPluginCount() > 0)
    {
        if (! audioEngine.beginEffectStateCapture())
        {
            lastStatusMessage =
                "Timed out while preparing the existing effect chain for restore.";
            return false;
        }

        struct CaptureScope
        {
            AudioEngine& engine;
            ~CaptureScope() { engine.endEffectStateCapture(); }
        } captureScope { audioEngine };

        if (! audioEngine.clearEffectPlugins())
        {
            lastStatusMessage =
                "Could not clear the existing effect chain during restore.";
            return false;
        }
    }

    audioEngine.unload();
    playbackCoordinator.loadFailed();
    playbackCheckpoint.reset ({}, 0.0);
    playlist = std::move (restoredPlaylist);

    setVolume (state.volume);
    setOutputLayout (state.outputLayout);
    if (const auto* selectedTrack = playlist.getSelectedTrack();
        selectedTrack != nullptr)
        playbackCheckpoint.reset (selectedTrack->id,
                                  state.playbackPositionSeconds);

    if (const auto* selectedTrack = playlist.getSelectedTrack();
        selectedTrack != nullptr && selectedTrack->available)
    {
        const auto action = playbackCoordinator.preparePausedTrack (
            selectedTrack->id);
        if (execute (action))
        {
            const auto position = juce::jlimit (
                0.0, juce::jmax (0.0, audioEngine.getLengthInSeconds()),
                state.playbackPositionSeconds);
            audioEngine.seekToSeconds (position);
            playbackCheckpoint.reset (selectedTrack->id, position);
        }
    }
    workspaceMode = state.workspaceMode;
    setSkinPersistenceState ({ state.activeSkinId, state.activeSkinVersion,
                               juce::File (state.developerSkinDirectory) });

    effectChain = state.effectChain;
    for (const auto& savedSlot : state.effectChain.getSlots())
        effectChain.setAvailability (savedSlot.id, false, "Restoring plug-in...");

    ++effectChainRevision;
    auto slots = std::make_shared<std::vector<EffectSlotState>> (state.effectChain.getSlots());
    restoringEffects = ! slots->empty();
    if (restoringEffects)
        restoreNextEffect (std::move (slots), 0);
    ++stateRevision;
    return true;
}

void PlayerController::restoreNextEffect (std::shared_ptr<std::vector<EffectSlotState>> slots, size_t index)
{
    if (index >= slots->size())
    {
        restoringEffects = false;
        lastStatusMessage = "Saved effect chain restored.";
        ++effectChainRevision;
        return;
    }

    const auto savedSlot = (*slots)[index];
    auto description = pluginManager.findPluginByIdentifier (savedSlot.pluginIdentifier);
    if (description == nullptr)
    {
        effectChain.setAvailability (savedSlot.id,
                                     false,
                                     "Plug-in is not installed or the cache needs to be rescanned.");
        ++effectChainRevision;
        const auto alive = lifetimeFlag;
        juce::MessageManager::callAsync ([this, alive, restoreSlots = std::move (slots), index]
        {
            if (alive->load())
                restoreNextEffect (restoreSlots, index + 1);
        });
        return;
    }

    const auto alive = lifetimeFlag;
    pluginManager.createPluginInstanceAsync (
        *description,
        44100.0,
        512,
        [this, alive, restoreSlots = std::move (slots), index, savedSlot]
        (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& creationError) mutable
        {
            if (! alive->load())
                return;

            juce::String error = creationError;
            if (plugin != nullptr)
            {
                if (savedSlot.serializedState.getSize() > 0)
                    plugin->setStateInformation (savedSlot.serializedState.getData(),
                                                 static_cast<int> (savedSlot.serializedState.getSize()));

                plugin->addListener (this);
                if (audioEngine.addEffectPlugin (savedSlot.id, std::move (plugin), error))
                {
                    static_cast<void> (audioEngine.setEffectBypassed (savedSlot.id, savedSlot.bypassed));
                    effectChain.setAvailability (savedSlot.id, true);
                }
            }

            if (audioEngine.getEffectPlugin (savedSlot.id) == nullptr)
                effectChain.setAvailability (savedSlot.id,
                                             false,
                                             error.isNotEmpty() ? error : "Plug-in could not be restored.");

            ++effectChainRevision;
            restoreNextEffect (std::move (restoreSlots), index + 1);
        });
}

void PlayerController::audioProcessorParameterChanged (
    juce::AudioProcessor*, int, float)
{
    if (synchronousPluginStateCaptureOwner != this)
        pluginStateRevision.fetch_add (1, std::memory_order_relaxed);
}

void PlayerController::audioProcessorChanged (
    juce::AudioProcessor*,
    const juce::AudioProcessorListener::ChangeDetails&)
{
    if (synchronousPluginStateCaptureOwner != this)
        pluginStateRevision.fetch_add (1, std::memory_order_relaxed);
}

std::vector<int> PlayerController::getAvailableEffectSlotIds() const
{
    std::vector<int> result;
    result.reserve (static_cast<size_t> (effectChain.size()));
    for (const auto& slot : effectChain.getSlots())
        if (slot.available && audioEngine.getEffectPlugin (slot.id) != nullptr)
            result.push_back (slot.id);

    return result;
}
