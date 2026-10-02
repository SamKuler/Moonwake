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
    audioInitialisationError = audioEngine.initialise();
    rememberAudioDeviceSettings();
    lastStatusMessage = audioInitialisationError.isEmpty()
        ? "Ready" : "Audio device error: " + audioInitialisationError;
}

PlayerController::PlayerController (std::unique_ptr<IAudioDeviceBackend> backend)
    : audioEngine (std::move (backend)),
      artworkStore (ProductPaths::artworkCacheRoot())
{
    audioInitialisationError = audioEngine.initialise();
    rememberAudioDeviceSettings();
    lastStatusMessage = audioInitialisationError.isEmpty()
        ? "Ready" : "Audio device error: " + audioInitialisationError;
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
    {
        ++stateRevision;
        const auto importStatus = lastStatusMessage;
        if (refreshUpcomingPlayback()) lastStatusMessage = importStatus;
    }
    return lastStatusMessage;
}

bool PlayerController::selectTrack (int index)
{
    synchronizePlayback();
    const auto* track = playlist.getTrack (index);
    if (track == nullptr) return false;
    if (track->id == playbackCoordinator.getLoadedTrackId()) return true;
    const auto id = track->id;
    return loadTrackById (id, playbackCoordinator.getIntent() == TransportIntent::playing);
}

bool PlayerController::playOrPause()
{
    synchronizePlayback();
    return setPlaying (playbackCoordinator.getIntent() != TransportIntent::playing);
}

bool PlayerController::setPlaying (bool shouldPlay)
{
    if (! shouldPlay) audioEngine.pause();
    const AudioEngine::ScopedPlaybackEdit edit (audioEngine);
    synchronizePlayback();
    if (! shouldPlay && playbackCoordinator.getIntent() != TransportIntent::playing)
        return true;
    if (playlist.getSelectedTrack() == nullptr)
    {
        lastStatusMessage = "Add and select an audio file first.";
        return false;
    }
    if (shouldPlay && playbackCoordinator.getLoadedTrackId() != getSelectedTrackId())
    {
        if (loadTrackById (getSelectedTrackId(), true)) return true;
        return navigatePlayback (PlaylistNavigation::manualNext, true);
    }
    if ((playbackCoordinator.getIntent() == TransportIntent::playing) == shouldPlay)
        return true;
    if (! execute (playbackCoordinator.togglePlay (getSelectedTrackId()))) return false;
    playbackCheckpoint.reset (getSelectedTrackId(), audioEngine.getCurrentPositionSeconds());
    ++stateRevision;
    lastStatusMessage = playbackCoordinator.getIntent() == TransportIntent::playing
        ? "Playing " + playlist.getSelectedTrack()->title : "Paused.";
    return true;
}

bool PlayerController::nextTrack()
{
    synchronizePlayback();
    return navigatePlayback (PlaylistNavigation::manualNext,
                             playbackCoordinator.getIntent() == TransportIntent::playing);
}

bool PlayerController::previousTrack()
{
    synchronizePlayback();
    const auto previous = playlist.navigationCandidates (PlaylistNavigation::previous);
    if (playbackCoordinator.getLoadedTrackId().isNotEmpty()
        && (audioEngine.getCurrentPositionSeconds() >= 3.0 || previous.empty()))
    {
        if (! execute ({ PlaybackActionType::seekToStart,
                         playbackCoordinator.getLoadedTrackId() })) return false;
        playbackCheckpoint.reset (getSelectedTrackId(), 0.0);
        ++stateRevision;
        lastStatusMessage = "Restarted current track.";
        return true;
    }
    return navigatePlayback (PlaylistNavigation::previous,
                             playbackCoordinator.getIntent() == TransportIntent::playing);
}

bool PlayerController::handlePlaybackFinished()
{
    return synchronizePlayback();
}

bool PlayerController::handlePlaybackReadError()
{
    if (! audioEngine.hasReadError()) return false;
    audioEngine.pause();
    synchronizePlayback (false);
    const auto id = playbackCoordinator.getLoadedTrackId();
    const auto shouldPlay = playbackCoordinator.getIntent() == TransportIntent::playing;
    playlist.setTrackAvailable (id, false);
    playbackCheckpoint.reset (id, audioEngine.getCurrentPositionSeconds());
    audioEngine.unload();
    playbackCoordinator.loadFailed();
    ++stateRevision;
    if (shouldPlay && navigatePlayback (PlaylistNavigation::manualNext, true)) return true;
    lastStatusMessage = "Could not read the audio file. No playable track remains.";
    return true;
}

bool PlayerController::navigatePlayback (PlaylistNavigation kind, bool shouldPlay)
{
    const auto candidates = playlist.navigationCandidates (kind);
    int skipped = 0;
    for (const auto& id : candidates)
    {
        if (loadTrackById (id, shouldPlay, kind))
        {
            if (skipped > 0)
                lastStatusMessage += " Skipped " + juce::String (skipped) + " unavailable tracks.";
            return true;
        }
        ++skipped;
    }
    lastStatusMessage = skipped > 0
        ? "Skipped " + juce::String (skipped) + " unavailable tracks. No playable track remains."
        : (kind == PlaylistNavigation::previous ? "No previous track." : "No next track.");
    return false;
}

bool PlayerController::refreshUpcomingPlayback()
{
    audioEngine.pause();
    const AudioEngine::ScopedPlaybackEdit edit (audioEngine);
    synchronizePlayback();
    const auto currentId = playbackCoordinator.getLoadedTrackId();
    const auto* current = playlist.getTrack (playlist.indexOfId (currentId));
    if (current == nullptr)
        return true;
    const auto currentToken = audioEngine.getPlaybackProgress().token;
    AudioPlaybackPlan nextPlan;
    std::unordered_map<uint64_t, juce::String> ids;
    nextPlan.items.push_back ({ current->file, currentToken });
    ids.emplace (currentToken, currentId);
    const auto navigation = playlist.continuousPlaybackPlan();
    if (navigation.trackIds.size() > 1)
    {
        std::unordered_map<std::string, const TrackItem*> tracks;
        for (const auto& track : playlist.getTracks()) tracks.emplace (track.id.toStdString(), &track);
        uint64_t token = 1;
        for (size_t index = 1; index < navigation.trackIds.size(); ++index)
        {
            const auto& id = navigation.trackIds[index];
            const auto found = tracks.find (id.toStdString());
            if (found == tracks.end()) continue;
            if (token == currentToken) ++token;
            nextPlan.items.push_back ({ found->second->file, token });
            ids.emplace (token++, id);
        }
    }
    nextPlan.repeatFrom = navigation.repeatFrom;
    std::vector<AudioPlaybackItem> upcoming (nextPlan.items.begin() + 1, nextPlan.items.end());
    if (! audioEngine.setUpcomingPlayback (std::move (upcoming), nextPlan.repeatFrom))
    {
        lastStatusMessage = "Could not prepare continuous playback.";
        return false;
    }
    playbackPlan = std::move (nextPlan);
    playbackTrackIds = std::move (ids);
    const auto progress = audioEngine.getPlaybackProgress();
    playbackGeneration = progress.generation;
    playbackSequence = 0;
    failedPlaybackItems.clear();
    if (playbackCoordinator.getIntent() == TransportIntent::playing)
        audioEngine.play();
    return true;
}

bool PlayerController::synchronizePlayback (bool finishQueue, bool* preparationFailed)
{
    if (preparationFailed != nullptr) *preparationFailed = false;
    const auto progress = audioEngine.getPlaybackProgress();
    bool changed = false;
    juce::StringArray failures;
    for (const auto& failure : audioEngine.drainPreparationFailures())
    {
        if (failure.generation != playbackGeneration) continue;
        if (preparationFailed != nullptr) *preparationFailed = true;
        const auto firstUnplayed = failure.sequence + (failure.hadBufferedAudio ? 1u : 0u);
        const auto [position, inserted] = failedPlaybackItems.emplace (failure.token, firstUnplayed);
        if (! inserted) position->second = std::min (position->second, firstUnplayed);
        if (const auto found = playbackTrackIds.find (failure.token); found != playbackTrackIds.end())
        {
            playlist.setTrackAvailable (found->second, false);
            failures.add (failure.error);
        }
        changed = true;
    }
    if (progress.generation == playbackGeneration && ! playbackPlan.items.empty())
    {
        while (playbackSequence < progress.sequence)
        {
            const auto sequence = ++playbackSequence;
            auto index = sequence;
            if (index >= playbackPlan.items.size())
            {
                if (playbackPlan.repeatFrom < 0) break;
                const auto loopStart = static_cast<uint64_t> (playbackPlan.repeatFrom);
                index = loopStart + (index - playbackPlan.items.size())
                    % (playbackPlan.items.size() - loopStart);
            }
            const auto token = playbackPlan.items[static_cast<size_t> (index)].token;
            if (const auto failure = failedPlaybackItems.find (token);
                failure != failedPlaybackItems.end() && sequence >= failure->second)
                continue;
            const auto found = playbackTrackIds.find (token);
            if (found == playbackTrackIds.end()) continue;
            if (! playlist.recordPlayback (found->second)) continue;
            playbackCoordinator.trackStarted (found->second, playbackCoordinator.getIntent());
            ++stateRevision;
            changed = true;
        }
        if (changed)
        {
            playbackCheckpoint.reset (playbackCoordinator.getLoadedTrackId(), progress.positionSeconds);
            if (const auto* track = playlist.getSelectedTrack())
                lastStatusMessage = (playbackCoordinator.getIntent() == TransportIntent::playing
                                         ? "Playing " : "Loaded ") + track->title;
        }
        if (finishQueue && progress.finished && ! progress.readError
            && playbackCoordinator.getIntent() == TransportIntent::playing)
        {
            execute (playbackCoordinator.playbackFinished ({}));
            ++stateRevision;
            changed = true;
            lastStatusMessage = "End of queue.";
        }
    }
    if (! failures.isEmpty())
    {
        ++stateRevision;
        lastStatusMessage = "Skipped unavailable audio: " + failures.joinIntoString (" ");
    }
    return changed;
}

bool PlayerController::removeTrack (int index)
{
    if (playlist.getTrack (index) == nullptr) return false;
    audioEngine.pause();
    const AudioEngine::ScopedPlaybackEdit edit (audioEngine);
    synchronizePlayback();
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

    refreshUpcomingPlayback();
    lastStatusMessage = "Removed " + removedTitle + ".";
    return true;
}

bool PlayerController::moveTrack (int fromIndex, int toIndex)
{
    synchronizePlayback();
    if (! playlist.moveTrack (fromIndex, toIndex))
        return false;

    ++stateRevision;
    refreshUpcomingPlayback();
    lastStatusMessage = "Queue reordered.";
    return true;
}

void PlayerController::clearPlaylist()
{
    audioEngine.unload();
    playbackPlan = {};
    playbackTrackIds.clear();
    failedPlaybackItems.clear();
    playbackGeneration = 0;
    playbackSequence = 0;
    playbackCoordinator.loadFailed();
    playbackCheckpoint.reset ({}, 0.0);
    for (const auto& track : playlist.getTracks())
        static_cast<void> (artworkStore.remove (track.artworkCacheKey));
    playlist.clear();
    ++stateRevision;
    lastStatusMessage = "Queue cleared.";
}

bool PlayerController::seekToRatio (double ratio)
{
    return seekToSeconds (juce::jlimit (0.0, 1.0, ratio)
                          * audioEngine.getLengthInSeconds());
}

bool PlayerController::seekToSeconds (double seconds)
{
    synchronizePlayback();
    const auto length = audioEngine.getLengthInSeconds();
    if (! std::isfinite (seconds) || length <= 0.0)
    {
        lastStatusMessage = length <= 0.0 ? "No seekable audio is loaded."
                                         : "The requested playback position is invalid.";
        return false;
    }

    const auto position = juce::jlimit (0.0, length, seconds);
    if (std::abs (position - audioEngine.getCurrentPositionSeconds()) < 0.01)
        return true;

    if (! audioEngine.seekToSeconds (position))
    {
        handlePlaybackReadError();
        return false;
    }
    playbackCheckpoint.reset (playbackCoordinator.getLoadedTrackId(), position);
    ++stateRevision;
    return true;
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
    synchronizePlayback();
    if (playlist.isShuffleEnabled() == shouldShuffle)
        return;
    playlist.setShuffleEnabled (shouldShuffle);
    refreshUpcomingPlayback();
    ++stateRevision;
}

void PlayerController::setRepeatMode (RepeatMode mode)
{
    synchronizePlayback();
    if (playlist.getRepeatMode() == mode)
        return;
    playlist.setRepeatMode (mode);
    refreshUpcomingPlayback();
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
    {
        preferredOutputLayout = layout;
        ++stateRevision;
    }
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
    if (error.isEmpty())
    {
        rememberAudioDeviceSettings();
        ++stateRevision;
    }
    return error;
}

juce::String PlayerController::setAudioSampleRate (double sampleRate)
{
    const auto error = audioEngine.setSampleRate (sampleRate);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Sample rate set to "
                                + juce::String (sampleRate, 0) + " Hz.";
    if (error.isEmpty())
    {
        rememberAudioDeviceSettings();
        ++stateRevision;
    }
    return error;
}

juce::String PlayerController::setAudioBufferSize (int bufferSize)
{
    const auto error = audioEngine.setBufferSize (bufferSize);
    lastStatusMessage = error.isNotEmpty()
                            ? error
                            : "Buffer size set to "
                                + juce::String (bufferSize) + " samples.";
    if (error.isEmpty())
    {
        rememberAudioDeviceSettings();
        ++stateRevision;
    }
    return error;
}

void PlayerController::rememberAudioDeviceSettings()
{
    audioDevicePreferences = { audioEngine.getCurrentAudioDeviceName(),
                               audioEngine.getCurrentAudioSampleRate(),
                               audioEngine.getCurrentAudioBufferSize() };
}

juce::String PlayerController::restoreAudioDeviceSettings (const PlayerAppState& state)
{
    const auto& preferences = state.audioDevice;
    if (preferences.outputDeviceName.isEmpty()
        && preferences.sampleRate <= 0.0 && preferences.bufferSize <= 0)
        return {};

    audioDevicePreferences = preferences;
    juce::StringArray errors;
    if (preferences.outputDeviceName.isNotEmpty())
    {
        const auto error = audioEngine.selectOutputDevice (preferences.outputDeviceName);
        if (error.isNotEmpty())
            return "Could not restore output device " + preferences.outputDeviceName
                + ". Using " + audioEngine.getCurrentAudioDeviceName() + ". " + error;
    }
    if (preferences.sampleRate > 0.0)
    {
        const auto error = audioEngine.setSampleRate (preferences.sampleRate);
        if (error.isNotEmpty()) errors.add (error);
    }
    if (preferences.bufferSize > 0)
    {
        const auto error = audioEngine.setBufferSize (preferences.bufferSize);
        if (error.isNotEmpty()) errors.add (error);
    }
    return errors.joinIntoString (" ");
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
    return applyPreparedState (StateRestoreService::prepare (
        stateFile, pluginListFile, artworkStore.root()));
}

bool PlayerController::applyPreparedState (StateRestoreResult result)
{
    if (result.cancelled)
    {
        lastStatusMessage = "Session restore cancelled.";
        return false;
    }

    const auto failed = result.errorMessage.isNotEmpty();
    restoreDiagnostics = std::move (result.warnings);
    if (failed)
        restoreDiagnostics.add (std::move (result.errorMessage));
    if (result.pluginCache != nullptr)
        pluginManager.restoreKnownPluginsFromXml (*result.pluginCache);
    if (! result.state || failed)
    {
        if (! restoreDiagnostics.isEmpty())
            lastStatusMessage = restoreDiagnostics.joinIntoString (" ");
        return ! failed;
    }

    if (! restoreStateSnapshot (std::move (*result.state)))
    {
        if (! restoreDiagnostics.isEmpty())
            lastStatusMessage += " " + restoreDiagnostics.joinIntoString (" ");
        return false;
    }
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
    state.canPlay = state.selectedIndex >= 0;
    state.canGoNext = playlist.hasNavigationCandidate (PlaylistNavigation::manualNext);
    state.canGoPrevious = playbackCoordinator.getLoadedTrackId().isNotEmpty()
        || playlist.hasNavigationCandidate (PlaylistNavigation::previous);
    state.isPlaying = audioEngine.isPlaying();
    state.playbackFinished = ! state.isPlaying && audioEngine.hasStreamFinished();
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
            if (! audioEngine.seekToSeconds (0.0))
            {
                handlePlaybackReadError();
                return false;
            }
            return true;
        case PlaybackActionType::stop:
            audioEngine.stop();
            return true;
        case PlaybackActionType::none:
            return true;
    }

    return false;
}

bool PlayerController::loadTrackById (const juce::String& trackId, bool shouldPlay,
                                      std::optional<PlaylistNavigation> navigation)
{
    const auto index = playlist.indexOfId (trackId);
    const auto* track = playlist.getTrack (index);
    if (track == nullptr) return false;
    const auto title = track->title;
    const auto path = track->file.getFullPathName();
    if (! audioEngine.loadFile (track->file))
    {
        playlist.setTrackAvailable (trackId, false);
        lastStatusMessage = "Could not load audio file: " + path;
        audioEngine.unload();
        playbackCoordinator.loadFailed();
        ++stateRevision;
        return false;
    }
    playlist.setTrackAvailable (trackId, true);
    if (navigation.has_value()) playlist.commitPlayback (trackId, *navigation);
    else playlist.selectTrack (index);
    playbackCoordinator.trackStarted (trackId, shouldPlay ? TransportIntent::playing : TransportIntent::paused);
    if (! refreshUpcomingPlayback())
    {
        audioEngine.unload();
        playbackCoordinator.loadFailed();
        return false;
    }
    if (shouldPlay) audioEngine.play();
    playbackCheckpoint.reset (trackId, audioEngine.getCurrentPositionSeconds());
    ++stateRevision;
    lastStatusMessage = (shouldPlay ? "Playing " : "Loaded ") + title;
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
    synchronizePlayback();
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
    state.outputLayout = preferredOutputLayout;
    state.audioDevice = audioDevicePreferences;
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

bool PlayerController::restoreStateSnapshot (PlayerAppState state)
{
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
    playlist = std::move (state.playlist);
    playbackPlan = {};
    playbackTrackIds.clear();
    failedPlaybackItems.clear();
    playbackGeneration = 0;
    playbackSequence = 0;

    preferredOutputLayout = state.outputLayout;
    if (const auto deviceWarning = restoreAudioDeviceSettings (state);
        deviceWarning.isNotEmpty())
        restoreDiagnostics.add (deviceWarning);
    setVolume (state.volume);
    if (const auto layoutWarning = setOutputLayout (state.outputLayout);
        layoutWarning.isNotEmpty())
        restoreDiagnostics.add (layoutWarning);
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
            if (audioEngine.seekToSeconds (position))
                playbackCheckpoint.reset (selectedTrack->id, position);
            else
            {
                handlePlaybackReadError();
                restoreDiagnostics.add (lastStatusMessage);
            }
        }
        else
            restoreDiagnostics.add (lastStatusMessage);
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
    lastStatusMessage = "Saved session restored.";
    if (! restoreDiagnostics.isEmpty())
        lastStatusMessage += " " + restoreDiagnostics.joinIntoString (" ");
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
        if (! restoreDiagnostics.isEmpty())
            lastStatusMessage += " " + restoreDiagnostics.joinIntoString (" ");
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
