#pragma once

#include "audio/AudioEngine.h"
#include "audio/TrackMetadataReader.h"
#include "app/PlaybackCheckpoint.h"
#include "app/PlaybackCoordinator.h"
#include "app/PlayerViewState.h"
#include "playlist/PlaylistModel.h"
#include "plugins/EffectChainState.h"
#include "plugins/PluginManager.h"
#include "state/AppStateStore.h"
#include "state/ListeningPresetStore.h"
#include "state/StateRestoreService.h"
#include "library/ArtworkStore.h"
#include "library/MusicDirectoryScanService.h"

#include <atomic>
#include <functional>
#include <memory>
#include <map>
#include <tuple>
#include <vector>
#include <unordered_map>

struct SkinPersistenceState
{
    juce::String activeSkinId = "builtin.default";
    juce::String activeSkinVersion;
    juce::File developerDirectory;
};

struct PlaybackQueueEntry
{
    TrackItem track;
    juce::String key;
    bool current = false;
};

class PlayerController : private juce::AudioProcessorListener
{
    friend class PlayerEffectControllerTests;
public:
    using PluginInsertionCallback = std::function<void (bool, const juce::String&)>;
    enum class PresetKind { effectChain, listening };

    [[nodiscard]] const ListeningPresetLibrary& getPresetLibrary() const noexcept { return presetLibrary; }
    [[nodiscard]] juce::String getActiveEffectPresetId() const { return activeEffectPresetId; }
    [[nodiscard]] juce::String getActiveListeningPresetId() const { return activeListeningPresetId; }
    [[nodiscard]] bool isEffectPresetModified() const;
    [[nodiscard]] bool isListeningPresetModified() const;
    [[nodiscard]] bool isPresetApplyRunning() const noexcept { return restoringEffects; }
    juce::Result savePreset (PresetKind, const juce::String& name, const juce::String& overwriteId = {});
    juce::Result renamePreset (PresetKind, const juce::String& id, const juce::String& name);
    juce::Result duplicatePreset (PresetKind, const juce::String& id, const juce::String& name);
    juce::Result deletePreset (PresetKind, const juce::String& id);
    juce::Result importPreset (const juce::File&);
    juce::Result exportPreset (PresetKind, const juce::String& id, const juce::File&);
    juce::Result applyPreset (PresetKind, const juce::String& id, PluginInsertionCallback completed,
                              std::function<void()> beforeCommit = {});
    [[nodiscard]] std::vector<OutputChannelCalibration> getOutputCalibration() const { return audioEngine.getOutputCalibration(); }
    juce::Result setOutputCalibration (const OutputChannelCalibration&);
    void resetOutputCalibration();
    juce::Result setCalibrationTestSignal (CalibrationTestSignal);
    [[nodiscard]] CalibrationTestSignal getCalibrationTestSignal() const { return audioEngine.getCalibrationTestSignal(); }
    void stopCalibrationTestSignal();


    PlayerController();
    explicit PlayerController (std::unique_ptr<IAudioDeviceBackend>);
    PlayerController (std::unique_ptr<IAudioDeviceBackend>, PluginScannerConfiguration,
                      juce::File artworkCacheRoot = {});
    ~PlayerController() override;

    juce::String addFiles (const juce::Array<juce::File>& files);
    juce::String addPreparedTracks (std::vector<TrackMetadataResult> tracks);
    [[nodiscard]] std::vector<PlaybackQueueEntry> getPlaybackQueue() const;
    [[nodiscard]] auto getPlaybackQueueVersion() const
    { return std::make_tuple (playbackGeneration, playbackSequence, playbackFailureRevision, playbackCoordinator.getLoadedTrackId()); }
    [[nodiscard]] const MusicLibrary& getLibrary() const noexcept { return library; }
    [[nodiscard]] juce::File resolveArtworkFile (const juce::String& key) const { return artworkStore.resolve (key); }
    [[nodiscard]] juce::String getLoadedTrackId() const { return playbackCoordinator.getLoadedTrackId(); }
    [[nodiscard]] juce::String getLibraryCollectionId() const { return libraryCollectionId; }
    [[nodiscard]] juce::String getLibrarySearchQuery() const { return librarySearchQuery; }
    void setLibraryBrowseState (juce::String collectionId, juce::String query);
    juce::String importPreparedTracks (std::vector<TrackMetadataResult>,
                                      const juce::String& targetCollectionId);
    juce::String importPreparedPlaylist (juce::String name, std::vector<TrackMetadataResult>);
    juce::String createSavedPlaylist (juce::String name);
    juce::String createSmartPlaylist (juce::String name, const SmartPlaylistRules&);
    bool updateSmartPlaylist (const juce::String& id, juce::String name, const SmartPlaylistRules&);
    juce::String materializeSmartPlaylist (const juce::String& id);
    [[nodiscard]] const std::vector<MusicDirectory>& getMusicDirectories() const noexcept { return musicDirectories; }
    juce::String addMusicDirectory (const juce::File&);
    bool removeMusicDirectory (const juce::String& id);
    bool setMusicDirectoryAutomatic (const juce::String& id, bool enabled);
    bool applyMusicDirectoryScan (MusicDirectoryScanResult);
    void choosePlaylistArtwork (const juce::String& id, juce::File image,
                                std::function<void(bool, const juce::String&)> completion);
    bool resetPlaylistArtwork (const juce::String& id);
    bool renameSavedPlaylist (const juce::String& id, juce::String name);
    juce::String duplicateSavedPlaylist (const juce::String& id);
    bool deleteSavedPlaylist (const juce::String& id);
    bool moveSavedPlaylist (const juce::String& id, int toIndex);
    bool addTracksToSavedPlaylist (const juce::String& id, const std::vector<juce::String>& libraryIds);
    bool setTrackFavourites (const std::vector<juce::String>& libraryIds, bool favourite);
    bool playLibraryTracks (const std::vector<juce::String>& libraryIds, int startIndex = 0);
    bool enqueueLibraryTracks (const std::vector<juce::String>& libraryIds, bool playNext);
    juce::String saveQueueAsPlaylist (juce::String name);
    bool removeCollectionEntries (const juce::String& collectionId,
                                  const std::vector<juce::String>& entryIds);
    bool moveCollectionEntries (const juce::String& collectionId,
                                const std::vector<juce::String>& entryIds, int beforeIndex);
    bool sortCollectionEntries (const juce::String& collectionId,
                                const juce::String& key, bool descending = false);
    bool undoCollectionEdit (const juce::String& collectionId);
    [[nodiscard]] bool canUndoCollectionEdit (const juce::String& collectionId) const;
    bool relocateLibraryTrack (const juce::String& id, TrackMetadataResult replacement);
    bool selectTrack (int index);
    bool playOrPause();
    bool setPlaying (bool shouldPlay);
    bool nextTrack();
    bool previousTrack();
    bool handlePlaybackFinished();
    bool handlePlaybackReadError();
    bool synchronizePlayback (bool finishQueue = true, bool* preparationFailed = nullptr);
    bool removeUpcomingQueueEntries (const std::vector<juce::String>& ids);
    [[nodiscard]] bool canMoveUpcomingQueueEntry (const juce::String& id, int direction) const;
    bool moveUpcomingQueueEntry (const juce::String& id, int direction);
    bool removeTrack (int index);
    bool moveTrack (int fromIndex, int toIndex);
    void clearPlaylist();
    bool seekToRatio (double ratio);
    bool seekToSeconds (double seconds);
    void checkpointPlaybackPosition();
    void setShuffleEnabled (bool shouldShuffle);
    void setRepeatMode (RepeatMode mode);
    void setVolume (double volume);
    [[nodiscard]] std::vector<OutputChannelMix> getOutputChannelMix() const { return audioEngine.getOutputChannelMix(); }
    bool setOutputChannelMix (juce::AudioChannelSet::ChannelType, float gain, bool muted, bool solo);
    void resetOutputChannelMix();
    void setSkinPersistenceState (SkinPersistenceState);
    juce::String setOutputLayout (OutputLayout layout);
    juce::String selectOutputDevice (const juce::String& deviceName);
    juce::String setAudioSampleRate (double sampleRate);
    juce::String setAudioBufferSize (int bufferSize);
    void setWorkspaceMode (WorkspaceMode mode);
    bool startPluginScan (const juce::File& customVst3Directory = {},
                          PluginScanScope scope = PluginScanScope::enabledFormats);
    [[nodiscard]] juce::StringArray getPluginCustomVst3Directories() const;
    [[nodiscard]] juce::StringArray getDefaultVst3Directories() const;
    bool setPluginCustomVst3Directories (juce::StringArray);
    void cancelPluginScan();
    [[nodiscard]] PluginScanStatus getPluginScanStatus() const;
    void addPluginByIdentifierAsync (const juce::String& identifier, PluginInsertionCallback callback,
                                EffectScope scope = {});
    bool removeEffectAtIndex (int effectIndex);
    bool moveEffect (int fromIndex, int toIndex);
    bool setEffectBypassedAtIndex (int effectIndex, bool shouldBypass);
    void setAllEffectsBypassed (bool value) noexcept { audioEngine.setAllEffectsBypassed (value); }
    bool clearEffectTails();
    bool setEffectScopeAtIndex (int effectIndex, const EffectScope&);
    [[nodiscard]] bool captureStateSnapshot (PlayerAppState&,
                                             juce::String& errorMessage);
    juce::StringArray retainArtworkForStateSave (const PlayerAppState&);
    void acknowledgeSavedArtwork (juce::StringArray references);
    bool saveState (const juce::File& stateFile, const juce::File& pluginListFile);
    bool loadState (const juce::File& stateFile, const juce::File& pluginListFile);
    bool applyPreparedState (StateRestoreResult result);

    juce::String takeAudioOutputError();
    [[nodiscard]] const juce::String& getAudioInitialisationError() const noexcept { return audioInitialisationError; }
    [[nodiscard]] const juce::StringArray& getRestoreDiagnostics() const noexcept { return restoreDiagnostics; }
    [[nodiscard]] const PlaylistModel& getPlaylist() const;
    [[nodiscard]] const EffectChainState& getEffectChain() const;
    [[nodiscard]] juce::Array<juce::PluginDescription> getKnownPlugins() const;
    [[nodiscard]] juce::Array<juce::PluginDescription> getInsertEffectPlugins() const;
    [[nodiscard]] juce::StringArray getRegisteredPluginFormats() const;
    [[nodiscard]] PluginFormatPreferences getPluginFormatPreferences() const;
    bool setPluginFormatPreferences (PluginFormatPreferences);
    [[nodiscard]] juce::AudioPluginInstance* getEffectPluginForEditor (int effectIndex) const;
    [[nodiscard]] uint64_t getEffectChainRevision() const;
    [[nodiscard]] double getPlaybackRatio() const;
    [[nodiscard]] juce::String getPlaybackTimeText() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getLengthInSeconds() const;
    [[nodiscard]] juce::String getLastStatusMessage() const;
    [[nodiscard]] AudioDeviceSettingsView getAudioDeviceSettings() const;
    juce::String setOutputRouting (const std::vector<int>&);
    juce::String openAudioDeviceConfiguration();
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] PlayerViewState getViewState() const;
    [[nodiscard]] OutputMeterReadings getOutputMeterReadings() const { return audioEngine.getOutputMeterReadings(); }
    void resetOutputMeter() noexcept { audioEngine.resetOutputMeter(); }
    [[nodiscard]] SkinPersistenceState getSkinPersistenceState() const;
    [[nodiscard]] uint64_t getStateRevision() const noexcept;

private:
    PlaylistModel playlist;
    MusicLibrary library;
    std::vector<MusicDirectory> musicDirectories;
    juce::String libraryCollectionId = "all";
    juce::String librarySearchQuery;
    struct CollectionEdit
    {
        juce::String collectionId;
        std::vector<TrackItem> queue;
        std::vector<PlaylistPendingNextTrack> pendingNextTracks;
        std::vector<PlaylistEntry> entries;
        std::vector<juce::String> favourites;
    };
    std::vector<CollectionEdit> collectionUndo;
    [[nodiscard]] CollectionEdit captureCollectionEdit (const juce::String&) const;
    void recordCollectionEdit (CollectionEdit);
    void invalidateCollectionUndo (const juce::String&);
    juce::String storePreparedLibraryTrack (TrackMetadataResult&, juce::StringArray& warnings,
                                            juce::StringArray& artworkCandidates);
    void discardUnreferencedArtwork (const juce::StringArray& candidates);
    void refreshQueueMetadata();
    void setLibraryTrackAvailable (const juce::String& occurrenceId, bool available);
    bool finishQueueEdit();
    bool makeQueueOccurrences (const std::vector<juce::String>&, std::vector<TrackItem>&);
    ListeningPresetLibrary presetLibrary;
    juce::File presetFile;
    juce::String activeEffectPresetId, activeListeningPresetId;
    uint64_t outputConfigurationRevision = 0;
    std::pair<uint64_t, uint64_t> effectPresetBaseline {};
    std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> listeningPresetBaseline {};
    [[nodiscard]] bool captureEffectChainSnapshot (EffectChainState&, juce::String&, bool force);
    [[nodiscard]] ListeningOutputConfiguration captureListeningOutput() const;
    juce::Result persistPresetLibrary (ListeningPresetLibrary);
    struct PresetRevision
    {
        uint64_t effects = 0, parameters = 0, output = 0, device = 0;
    };
    [[nodiscard]] PresetRevision capturePresetRevision() const;
    void markPresetActive (PresetKind, const juce::String&, const PresetRevision&);
    struct PendingPresetApply;
    void prepareNextPresetEffect (std::shared_ptr<PendingPresetApply>, size_t index);
    void finishPresetApply (const std::shared_ptr<PendingPresetApply>&, juce::String error);
    EffectChainState effectChain;
    AudioEngine audioEngine;
    PluginManager pluginManager;
    TrackMetadataReader metadataReader;
    ArtworkStore artworkStore;
    std::unordered_set<std::string> savedArtworkReferences;
    juce::StringArray deferredArtworkRemoval;
    juce::ThreadPool playlistArtworkJobs { 1 };
    std::map<juce::String, uint64_t> artworkRequests;
    uint64_t nextArtworkRequest = 0;
    PlaybackCoordinator playbackCoordinator;
    PlaybackCheckpoint playbackCheckpoint;
    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    SkinPersistenceState skinPersistenceState;
    AudioDevicePreferences audioDevicePreferences;
    OutputLayout preferredOutputLayout = OutputLayout::stereo;
    juce::String lastStatusMessage;
    juce::String audioInitialisationError;
    std::shared_ptr<std::atomic_bool> lifetimeFlag { std::make_shared<std::atomic_bool> (true) };
    uint64_t effectChainRevision = 0;
    uint64_t stateRevision = 0;
    std::atomic<uint64_t> pluginStateRevision { 0 };
    uint64_t capturedPluginStateRevision = 0;
    uint64_t synchronizedPluginStateRevision = 0;
    void synchronizeNativeEffectBypass();
    bool restoringEffects = false;
    uint64_t effectRestoreGeneration = 0;
    AudioPlaybackPlan playbackPlan;
    std::unordered_map<uint64_t, juce::String> playbackTrackIds;
    std::unordered_map<uint64_t, uint64_t> failedPlaybackItems;
    uint64_t playbackGeneration = 0;
    uint64_t playbackSequence = 0;
    uint64_t playbackFailureRevision = 0;
    bool upcomingPlaybackDirty = false;
    juce::StringArray restoreDiagnostics;

    bool execute (const PlaybackAction& action);
    void rememberAudioDeviceSettings();
    juce::String restoreAudioDeviceSettings (const PlayerAppState&);
    bool loadTrackById (const juce::String& trackId, bool shouldPlay,
                        std::optional<PlaylistNavigation> navigation = {},
                        const PlaylistPlaybackPlan* publishedTail = nullptr);
    bool navigatePlayback (PlaylistNavigation, bool shouldPlay);
    struct PublishedQueue
    {
        PlaylistPlaybackPlan navigation;
        std::vector<uint64_t> sequences;
    };
    [[nodiscard]] PublishedQueue publishedQueueFrom (uint64_t sequence) const;
    bool refreshUpcomingPlayback (const PlaylistPlaybackPlan* publishedTail = nullptr);
    bool synchronizePlayback (const AudioPlaybackProgress&, bool finishQueue, bool* preparationFailed);
    [[nodiscard]] juce::String getSelectedTrackId() const;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (
        juce::AudioProcessor*,
        const juce::AudioProcessorListener::ChangeDetails&) override;
    [[nodiscard]] bool restoreStateSnapshot (PlayerAppState state);
    void restoreNextEffect (std::shared_ptr<std::vector<EffectSlotState>> slots, size_t index,
                            uint64_t generation);
    [[nodiscard]] std::vector<int> getAvailableEffectSlotIds() const;
};
