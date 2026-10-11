#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "app/NowPlayingCoordinator.h"
#include "app/PlayerController.h"
#include "app/AutoSaveScheduler.h"
#include "library/LibraryFilter.h"
#include "library/LibraryImportService.h"
#include "state/StateSaveService.h"
#include "state/StateRestoreService.h"
#include "AlbumArtworkComponent.h"
#include "OutputStageComponent.h"
#include "OutputMixerComponent.h"
#include "OutputCalibrationComponent.h"
#include "PresetControls.h"
#include "LyricsComponent.h"
#include "PlayerPresentation.h"
#include "lyrics/LyricsService.h"
#include "SkinFallbackDiagnostic.h"
#include "SkinLayoutHost.h"
#include "SkinEffectChainViewport.h"
#include "SkinTableListBox.h"
#include "SkinInputScope.h"
#include "skin/SkinManager.h"
#include "MusicLibraryPanel.h"
#include "SmartPlaylistEditor.h"
#include "ui/settings/MusicDirectoriesPanel.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/settings/AudioSettingsPanel.h"
#include "ui/settings/PluginSettingsPanel.h"
#include "ui/settings/SkinLibraryPanel.h"

#include <atomic>
#include <memory>

class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::TableListBoxModel,
                            private juce::MultiTimer,
                            private juce::FileDragAndDropTarget,
                            private SkinManager::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override;
    void mouseUp (const juce::MouseEvent&) override;
    void importFilesAndFolders (const juce::Array<juce::File>&);
    void showStatus (const juce::String&, bool isError = false);
    [[nodiscard]] bool supportsPresentation (PlayerPresentation) const;
    [[nodiscard]] PlayerWindowSizing windowSizing (PlayerPresentation) const;
    [[nodiscard]] bool canChangePresentation() const;
    void setPlayerPresentation (PlayerPresentation, juce::Rectangle<int> contentBounds);
    [[nodiscard]] PlayerPresentation getPlayerPresentation() const noexcept { return playerPresentation; }
    void setMiniPinned (bool);
    std::function<void (PlayerPresentation)> onPresentationRequested;
    std::function<PlayerPresentation()> presentationIntent;
    std::function<void (bool)> onPinRequested;
private:
    class EffectSlotRow;
    class PluginEditorWindow;

    void timerCallback (int timerID) override;
    void initialiseListeningControls();
    void refreshLyricsTrack();
    void updateLyricsControls();
    void requestPresentation (PlayerPresentation);
    void setLyricsOpen (bool);
    void reloadLyrics (bool refreshOnline = false);
    void applyLyricsResult (const juce::String& key, LyricsLoadResult, uint64_t offsetRevision);
    LyricsService::LoadCallback lyricsActionCallback (const TrackItem&);
    void showLyricsActions();
    void chooseLyricsFile (TrackItem);
    void searchLyrics (TrackItem);
    void changeLyricsOffset (TrackItem, double seconds);
    void skinChanged() override;
    int getNumRows() override;
    void paintRowBackground (juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::String getCellTooltip (int rowNumber, int columnId) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void playQueueRow (int row);
    const PlaybackQueueEntry* queueEntryAtRow (int row) const;
    int queueSourceIndex (int row) const;
    bool canMoveQueueSelection (int direction) const;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void chooseFiles();
    void choosePlaylistImport();
    void choosePlaylistExport (std::vector<TrackItem>);
    void chooseTrackRelocation (juce::String);
    void choosePlaylistArtwork (juce::String);
    void setQueueOpen (bool);
    void startLibraryImport (juce::Array<juce::File>);
    void setLibraryImportRunning (bool);
    void choosePluginDirectory();
    void chooseSkinArchive();
    void chooseDeveloperSkinDirectory();
    void refreshPlaylist();
    void updateEmptyQueueState();
    void refreshPlugins();
    void refreshEffects();
    void layoutEffects();
    void refreshEffectScopes();
    void updateTransportState();
    void updateOutputView();
    void refreshPresetControls();
    juce::Result handlePresetAction (PlayerController::PresetKind, PresetControls::Action,
                                    const juce::String& id, const juce::String& name);
    void choosePresetFile (PlayerController::PresetKind, const juce::String& id, bool importing);
    [[nodiscard]] bool shouldDeferTransportCommands() const;
    void dispatchQueuedTransportCommands();
    void handleRemoteTransportCommand (const RemoteTransportCommand&);
    bool performPlayCommand (bool toggle);
    void openPluginEditor (int effectIndex);
    void removeEffect (int effectIndex);
    void moveEffectFromDrag (int fromIndex, int yInRackContent);
    void closePluginEditors();
    void setWorkspaceMode (WorkspaceMode mode);
    void chooseMusicDirectory();
    void musicDirectoriesChanged (bool scanNow);
    void requestMusicDirectoryScan (bool automaticOnly);
    void startMusicDirectoryScan();
    void showSmartPlaylistEditor (const juce::String& id);
    void bindSkinComponents();
    void applyWorkspaceVisuals();
    void closeDrawers();
    void updateSkinPersistenceStateFromManager();
    void handleAutoSaveCompletion (uint64_t revision,
                                   bool saved,
                                   const juce::String& errorMessage);
    void finishStateRestore (StateRestoreResult);
    void setRestoreControlsEnabled (bool);
    [[nodiscard]] juce::File getStateDirectory() const;
    [[nodiscard]] juce::File getStateFile() const;
    [[nodiscard]] juce::File getPluginListFile() const;

    PlayerController controller;
    NowPlayingCoordinator nowPlayingCoordinator;
    juce::String lastNowPlayingError;
    std::shared_ptr<std::atomic_bool> remoteCommandLifetime;
    AutoSaveScheduler autoSaveScheduler;
    StateSaveService stateSaveService;
    StateRestoreService stateRestoreService;
    enum class RestoreStatus { loading, ready, failed };
    RestoreStatus restoreStatus = RestoreStatus::loading;
    std::vector<RemoteTransportCommand> queuedTransportCommands;
    LibraryFilter libraryFilter;
    std::vector<PlaybackQueueEntry> visibleQueue;
    std::tuple<uint64_t, uint64_t, uint64_t, juce::String> queueViewVersion;
    LibraryImportService libraryImportService;
    MusicDirectoryScanService musicDirectoryScanService;
    std::optional<bool> pendingMusicDirectoryScan;
    bool activeDirectoryScanAutomatic = true;
    double nextDirectoryScanAt = 0.0;
    juce::int64 smartPlaylistRefreshMinute = 0;
    SkinManager skinManager;
    SkinComponentBindings skinBindings;
    SkinRenderer skinRenderer;
    std::shared_ptr<SkinSession> appliedSkinSession;
    JuceSkinAdapter skinAdapter;
    AudioSettingsPanel audioSettingsPanel;
    PluginSettingsPanel pluginSettingsPanel;
    MusicDirectoriesPanel musicDirectoriesPanel;
    SkinLibraryPanel skinLibraryPanel;
    MusicLibraryPanel musicLibraryPanel;
    std::unique_ptr<SmartPlaylistEditor> smartPlaylistEditor;
    bool hasAppliedSkinLayout = false;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<std::unique_ptr<PluginEditorWindow>> pluginEditorWindows;
    std::vector<std::unique_ptr<EffectSlotRow>> effectRows;
    struct PendingLibraryImport
    {
        juce::Array<juce::File> files;
        juce::File playlistFile;
        juce::String collectionId;
        juce::String relocateTrackId;
    };
    std::vector<PendingLibraryImport> pendingLibraryImports;
    juce::String selectedQueueKey;
    bool autoSaveFailureVisible = false;
    double statusVisibleUntil = 0.0;

    juce::TextButton addFilesButton { "Add Files" };
    juce::TextEditor librarySearchEditor;
    juce::TextButton studioModeButton { "Studio A" };
    juce::TextButton listeningModeButton { "Listening B" };
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton effectsButton { "Effects" };
    juce::TextButton effectsCloseButton { "Close Effects" };
    juce::TextButton settingsCloseButton { "Close" };
    juce::TextButton removeTrackButton { "Remove" };
    juce::TextButton clearPlaylistButton { "Clear" };
    juce::TextButton moveTrackUpButton { "Move Up" };
    juce::TextButton moveTrackDownButton { "Move Down" };
    juce::Label pluginFilterLabel;
    PluginFilterControls pluginFilters;
    juce::StringArray catalogPluginIdentifiers;
    juce::ComboBox pluginCombo;
    juce::ComboBox effectScopeBox;
    juce::Label effectScopeLabel;
    std::vector<EffectScope> effectScopeChoices;
    juce::ComboBox meterModeBox;
    juce::TextButton resetMeterButton { "Reset" };
    juce::TextButton addEffectButton { "Add Effect" };
    juce::TextButton clearEffectTailsButton { "Clear tails" };
    juce::TextButton bypassAllEffectsButton { "Bypass all" };
    SkinEffectChainViewport effectRackViewport;
    juce::Component effectRackContent;
    juce::TextButton previousButton { "Prev" };
    juce::TextButton playPauseButton { "Play" };
    juce::TextButton nextButton { "Next" };
    juce::ToggleButton shuffleButton { "Shuffle" };
    juce::ComboBox repeatModeBox;
    juce::TextButton queueActionsButton { "Actions" };
    juce::TextButton queueToggleButton { "Play queue" };
    juce::TextButton repeatButton { "Repeat off" };
    juce::ComboBox outputLayoutBox;
    std::optional<RepeatMode> lastSyncedRepeatMode;
    std::optional<OutputLayout> lastSyncedOutputLayout;
    juce::Label titleLabel;
    juce::Label appEyebrowLabel;
    juce::Label nowPlayingEyebrowLabel;
    juce::Label queueTitleLabel;
    juce::Label queueCountLabel;
    juce::Label emptyQueueLabel;
    juce::Label nowPlayingLabel;
    juce::Label trackMetaLabel;
    juce::Label artworkMetaLabel;
    juce::Label effectsSummaryLabel;
    juce::Label settingsTitleLabel;
    juce::Label calibrationTitleLabel;
    juce::Label emptyRackLabel;
    juce::Label deviceStatusLabel;
    juce::Label volumeLabel;
    juce::Label volumeValueLabel;
    juce::Label timeLabel;
    SkinTableListBox playlistTable;
    juce::Label effectRackTitleLabel;
    juce::Label statusLabel;
    juce::Slider volumeSlider;
    juce::Slider seekSlider;
    bool seekGestureInProgress = false;
    AlbumArtworkComponent albumArtwork;
    OutputStageComponent outputStage;
    OutputMixerComponent outputMixer;
    OutputCalibrationComponent outputCalibration;
    LyricsComponent lyricsView;
    juce::TextButton lyricsToggleButton, lyricsActionsButton, lyricsFollowButton;
    juce::TextButton fullscreenButton, miniButton, restorePlayerButton, pinPlayerButton;
    juce::Label nowPlayingArtistLabel;
    std::unique_ptr<LyricsService> lyricsService;
    std::unique_ptr<juce::FileChooser> lyricsFileChooser;
    std::optional<TrackItem> lyricsTrack;
    std::shared_ptr<const LyricsDocument> lyricsDocument;
    juce::String lyricsTrackSignature;
    double lyricsOffsetSeconds = 0.0;
    uint64_t lyricsOffsetRevision = 0;
    bool lyricsLoading = false, lyricsOpen = false;
    PlayerPresentation playerPresentation = PlayerPresentation::normal;
    bool changingPresentation = false;
    PresetControls listeningPresetControls { "Listening setup" };
    PresetControls effectPresetControls { "Effect preset" };
    SkinFallbackDiagnostic skinFallbackDiagnostic;
    SkinLayoutHost skinLayoutHost;
    juce::TooltipWindow tooltipWindow { this, 600 };

    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    bool queueOpen = false;
    bool queueVisibilityChosen = false;
    bool effectsDrawerOpen = false;
    bool settingsDrawerOpen = false;
    SkinLayoutEntry activeSkinLayoutEntry = SkinLayoutEntry::listening;

    uint64_t lastEffectChainRevision = 0;
    bool pluginInsertionInProgress = false;
    bool refreshingPlaylist = false;

    SkinResolvedRowLayout effectRowLayout;
    int effectRowHeight = 1;
    int effectRowGap = 0;
    SkinInputScope inputScope { *this };
};
