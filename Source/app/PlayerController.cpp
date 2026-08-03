#include "PlayerController.h"

PlayerController::PlayerController()
{
    const auto error = audioEngine.initialise();
    lastStatusMessage = error.isEmpty() ? "Ready" : "Audio device error: " + error;
}

PlayerController::PlayerController (std::unique_ptr<IAudioDeviceBackend> backend)
    : audioEngine (std::move (backend))
{
    const auto error = audioEngine.initialise();
    lastStatusMessage = error.isEmpty() ? "Ready" : "Audio device error: " + error;
}

PlayerController::~PlayerController()
{
    lifetimeFlag->store (false);
    static_cast<void> (pluginManager.cancelAndWaitForScan());
}

juce::String PlayerController::addFiles (const juce::Array<juce::File>& files)
{
    int addedCount = 0;
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

        playlist.addTrack (std::move (metadata.track));
        ++addedCount;
    }

    lastStatusMessage = "Added " + juce::String (addedCount)
        + (addedCount == 1 ? " track." : " tracks.");

    if (! skippedFiles.isEmpty())
        lastStatusMessage += " Skipped: " + skippedFiles.joinIntoString (", ");

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

    const auto action = playbackCoordinator.selectionChanged (requestedTrack->id);
    if (! execute (action))
        return false;

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

    const auto action = playbackCoordinator.moveToTrack (getSelectedTrackId(), false);
    if (! execute (action))
        return false;

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
        playlist.selectTrack (playlist.indexOfId (action.trackId));

    if (! execute (action))
        return false;

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
    const auto action = playbackCoordinator.playbackFinished (hasNextTrack ? getSelectedTrackId() : juce::String{});

    if (! execute (action))
        return false;

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
    if (! playlist.removeTrack (index))
        return false;

    if (removedLoadedTrack)
    {
        audioEngine.unload();
        playbackCoordinator.loadFailed();
    }

    lastStatusMessage = "Removed " + removedTitle + ".";
    return true;
}

bool PlayerController::moveTrack (int fromIndex, int toIndex)
{
    if (! playlist.moveTrack (fromIndex, toIndex))
        return false;

    lastStatusMessage = "Queue reordered.";
    return true;
}

void PlayerController::clearPlaylist()
{
    audioEngine.unload();
    playbackCoordinator.loadFailed();
    playlist.clear();
    lastStatusMessage = "Queue cleared.";
}

void PlayerController::seekToRatio (double ratio)
{
    const auto length = audioEngine.getLengthInSeconds();
    if (length > 0.0)
        audioEngine.seekToSeconds (juce::jlimit (0.0, 1.0, ratio) * length);
}

void PlayerController::setShuffleEnabled (bool shouldShuffle)
{
    playlist.setShuffleEnabled (shouldShuffle);
}

void PlayerController::setRepeatMode (RepeatMode mode)
{
    playlist.setRepeatMode (mode);
}

void PlayerController::setVolume (double volume)
{
    audioEngine.setGain (volume);
}

void PlayerController::setSkinPersistenceState (SkinPersistenceState state)
{
    if (state.activeSkinId.isEmpty())
        state.activeSkinId = "builtin.default";

    skinPersistenceState = std::move (state);
}

juce::String PlayerController::setOutputLayout (OutputLayout layout)
{
    const auto error = audioEngine.setOutputLayout (layout);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Output layout set to " + toString (audioEngine.getOutputLayout()) + ".";
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
    workspaceMode = mode;
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
            if (! audioEngine.addEffectPlugin (slotId, std::move (plugin), graphError))
            {
                effectChain.removeSlot (slotId);
                completion (false, graphError);
                return;
            }

            ++effectChainRevision;
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
    if (slot->available && ! audioEngine.removeEffectPlugin (slotId))
        return false;

    if (! effectChain.removeSlot (slotId))
        return false;

    ++effectChainRevision;
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
    const auto stateSaved = AppStateStore::writeAtomically (createStateSnapshot(), stateFile, stateError);
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

    restoreStateSnapshot (restoredState);
    return true;
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

PlayerAppState PlayerController::createStateSnapshot() const
{
    PlayerAppState state;
    state.playlist = playlist;
    state.effectChain = effectChain;

    for (const auto& slot : effectChain.getSlots())
    {
        auto* savedSlot = state.effectChain.getSlotById (slot.id);
        auto* plugin = audioEngine.getEffectPlugin (slot.id);
        if (savedSlot != nullptr && plugin != nullptr)
        {
            savedSlot->serializedState.reset();
            const juce::ScopedLock callbackLock (plugin->getCallbackLock());
            plugin->getStateInformation (savedSlot->serializedState);
        }
    }

    state.volume = audioEngine.getGain();
    state.outputLayout = audioEngine.getOutputLayout();
    state.workspaceMode = workspaceMode;
    state.activeSkinId = skinPersistenceState.activeSkinId;
    state.activeSkinVersion = skinPersistenceState.activeSkinVersion;
    state.developerSkinDirectory =
        skinPersistenceState.developerDirectory.getFullPathName();
    return state;
}

void PlayerController::restoreStateSnapshot (const PlayerAppState& state)
{
    const auto selectedTrackId = state.playlist.getSelectedTrack() != nullptr
                                     ? state.playlist.getSelectedTrack()->id
                                     : juce::String{};

    playlist = {};
    playlist.setShuffleEnabled (state.playlist.isShuffleEnabled());
    playlist.setRepeatMode (state.playlist.getRepeatMode());

    for (const auto& savedTrack : state.playlist.getTracks())
    {
        auto restoredTrack = savedTrack;
        const auto metadata = metadataReader.read (savedTrack.file);

        if (metadata.ok)
        {
            restoredTrack.durationSeconds = metadata.track.durationSeconds;
            restoredTrack.sampleRate = metadata.track.sampleRate;
            restoredTrack.channelCount = metadata.track.channelCount;
            restoredTrack.available = true;
        }
        else
        {
            restoredTrack.available = false;
        }

        playlist.addTrack (std::move (restoredTrack));
    }

    if (selectedTrackId.isNotEmpty())
        playlist.selectTrack (playlist.indexOfId (selectedTrackId));

    for (const auto& existingSlot : effectChain.getSlots())
        if (existingSlot.available)
            static_cast<void> (audioEngine.removeEffectPlugin (existingSlot.id));

    setVolume (state.volume);
    setOutputLayout (state.outputLayout);
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

std::vector<int> PlayerController::getAvailableEffectSlotIds() const
{
    std::vector<int> result;
    result.reserve (static_cast<size_t> (effectChain.size()));
    for (const auto& slot : effectChain.getSlots())
        if (slot.available && audioEngine.getEffectPlugin (slot.id) != nullptr)
            result.push_back (slot.id);

    return result;
}
