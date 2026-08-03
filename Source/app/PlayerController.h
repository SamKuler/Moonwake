#pragma once

#include "audio/AudioEngine.h"
#include "audio/TrackMetadataReader.h"
#include "app/PlaybackCoordinator.h"
#include "app/PlayerViewState.h"
#include "playlist/PlaylistModel.h"
#include "plugins/EffectChainState.h"
#include "plugins/PluginManager.h"
#include "state/AppStateStore.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

struct SkinPersistenceState
{
    juce::String activeSkinId = "builtin.default";
    juce::String activeSkinVersion;
    juce::File developerDirectory;
};

class PlayerController
{
public:
    using PluginInsertionCallback = std::function<void (bool, const juce::String&)>;

    PlayerController();
    explicit PlayerController (std::unique_ptr<IAudioDeviceBackend>);
    ~PlayerController();

    juce::String addFiles (const juce::Array<juce::File>& files);
    bool selectTrack (int index);
    bool playOrPause();
    bool nextTrack();
    bool previousTrack();
    bool handlePlaybackFinished();
    bool removeTrack (int index);
    bool moveTrack (int fromIndex, int toIndex);
    void clearPlaylist();
    void seekToRatio (double ratio);
    void setShuffleEnabled (bool shouldShuffle);
    void setRepeatMode (RepeatMode mode);
    void setVolume (double volume);
    void setSkinPersistenceState (SkinPersistenceState);
    juce::String setOutputLayout (OutputLayout layout);
    juce::String selectOutputDevice (const juce::String& deviceName);
    juce::String setAudioSampleRate (double sampleRate);
    juce::String setAudioBufferSize (int bufferSize);
    void setWorkspaceMode (WorkspaceMode mode);
    int scanPluginsInDirectory (const juce::File& directory);
    bool startPluginScan (const juce::File& customVst3Directory = {});
    void cancelPluginScan();
    [[nodiscard]] PluginScanStatus getPluginScanStatus() const;
    void addPluginByIndexAsync (int pluginIndex, PluginInsertionCallback callback);
    bool removeEffectAtIndex (int effectIndex);
    bool moveEffect (int fromIndex, int toIndex);
    bool setEffectBypassedAtIndex (int effectIndex, bool shouldBypass);
    bool saveState (const juce::File& stateFile, const juce::File& pluginListFile);
    bool loadState (const juce::File& stateFile, const juce::File& pluginListFile);

    [[nodiscard]] const PlaylistModel& getPlaylist() const;
    [[nodiscard]] const EffectChainState& getEffectChain() const;
    [[nodiscard]] juce::Array<juce::PluginDescription> getKnownPlugins() const;
    [[nodiscard]] juce::Array<juce::PluginDescription> getInsertEffectPlugins() const;
    [[nodiscard]] juce::StringArray getRegisteredPluginFormats() const;
    [[nodiscard]] juce::AudioPluginInstance* getEffectPluginForEditor (int effectIndex) const;
    [[nodiscard]] uint64_t getEffectChainRevision() const;
    [[nodiscard]] double getPlaybackRatio() const;
    [[nodiscard]] juce::String getPlaybackTimeText() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getLengthInSeconds() const;
    [[nodiscard]] juce::String getLastStatusMessage() const;
    [[nodiscard]] AudioDeviceSettingsView getAudioDeviceSettings() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] PlayerViewState getViewState() const;
    [[nodiscard]] SkinPersistenceState getSkinPersistenceState() const;

private:
    PlaylistModel playlist;
    EffectChainState effectChain;
    AudioEngine audioEngine;
    PluginManager pluginManager;
    TrackMetadataReader metadataReader;
    PlaybackCoordinator playbackCoordinator;
    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    SkinPersistenceState skinPersistenceState;
    juce::String lastStatusMessage;
    std::shared_ptr<std::atomic_bool> lifetimeFlag { std::make_shared<std::atomic_bool> (true) };
    uint64_t effectChainRevision = 0;
    bool restoringEffects = false;

    bool execute (const PlaybackAction& action);
    bool loadTrackById (const juce::String& trackId, bool shouldPlay);
    [[nodiscard]] juce::String getSelectedTrackId() const;
    [[nodiscard]] PlayerAppState createStateSnapshot() const;
    void restoreStateSnapshot (const PlayerAppState& state);
    void restoreNextEffect (std::shared_ptr<std::vector<EffectSlotState>> slots, size_t index);
    [[nodiscard]] std::vector<int> getAvailableEffectSlotIds() const;
};
