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
#include "SkinFallbackDiagnostic.h"
#include "SkinLayoutHost.h"
#include "SkinEffectChainViewport.h"
#include "SkinTableListBox.h"
#include "skin/SkinManager.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/settings/AudioSettingsPanel.h"
#include "ui/settings/PluginSettingsPanel.h"
#include "ui/settings/SkinLibraryPanel.h"

#include <atomic>
#include <memory>

class MainComponent final : public juce::Component,
                            private juce::TableListBoxModel,
                            private juce::Timer,
                            private juce::FileDragAndDropTarget,
                            private SkinManager::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseUp (const juce::MouseEvent&) override;
    void importFilesAndFolders (const juce::Array<juce::File>&);
private:
    class EffectSlotRow;
    class PluginEditorWindow;

    void timerCallback() override;
    void showStatus (const juce::String&, bool isError = false);
    void skinChanged() override;
    int getNumRows() override;
    void paintRowBackground (juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::String getCellTooltip (int rowNumber, int columnId) override;
    void selectedRowsChanged (int lastRowSelected) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void chooseFiles();
    void startLibraryImport (juce::Array<juce::File>);
    void setLibraryImportRunning (bool);
    void choosePluginDirectory();
    void chooseSkinArchive();
    void chooseDeveloperSkinDirectory();
    void refreshPlaylist();
    void updateEmptyQueueState();
    void refreshPlugins();
    void refreshEffects();
    void updateTransportState();
    [[nodiscard]] bool shouldDeferTransportCommands() const;
    void dispatchQueuedTransportCommands();
    void handleRemoteTransportCommand (const RemoteTransportCommand&);
    void openPluginEditor (int effectIndex);
    void removeEffect (int effectIndex);
    void moveEffectFromDrag (int fromIndex, int yInRackContent);
    void closePluginEditors();
    void setWorkspaceMode (WorkspaceMode mode);
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
    std::shared_ptr<std::atomic_bool> remoteCommandLifetime;
    AutoSaveScheduler autoSaveScheduler;
    StateSaveService stateSaveService;
    StateRestoreService stateRestoreService;
    enum class RestoreStatus { loading, ready, failed };
    RestoreStatus restoreStatus = RestoreStatus::loading;
    std::vector<RemoteTransportCommand> queuedTransportCommands;
    LibraryFilter libraryFilter;
    LibraryImportService libraryImportService;
    SkinManager skinManager;
    SkinComponentBindings skinBindings;
    SkinRenderer skinRenderer;
    JuceSkinAdapter skinAdapter;
    AudioSettingsPanel audioSettingsPanel;
    PluginSettingsPanel pluginSettingsPanel;
    SkinLibraryPanel skinLibraryPanel;
    std::shared_ptr<SkinSession> appliedSkinSession;
    bool hasAppliedSkinLayout = false;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<std::unique_ptr<PluginEditorWindow>> pluginEditorWindows;
    std::vector<std::unique_ptr<EffectSlotRow>> effectRows;
    juce::Array<juce::File> queuedLibraryImportInputs;
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
    juce::TextEditor pluginFilterEditor;
    juce::ComboBox pluginCombo;
    juce::TextButton addEffectButton { "Add Effect" };
    SkinEffectChainViewport effectRackViewport;
    juce::Component effectRackContent;
    juce::TextButton previousButton { "Prev" };
    juce::TextButton playPauseButton { "Play" };
    juce::TextButton nextButton { "Next" };
    juce::ToggleButton shuffleButton { "Shuffle" };
    juce::ComboBox repeatModeBox;
    juce::ComboBox outputLayoutBox;
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
    AlbumArtworkComponent albumArtwork;
    OutputStageComponent outputStage;
    SkinFallbackDiagnostic skinFallbackDiagnostic;
    SkinLayoutHost skinLayoutHost;
    juce::TooltipWindow tooltipWindow { this, 600 };

    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    bool effectsDrawerOpen = false;
    bool settingsDrawerOpen = false;
    SkinLayoutEntry activeSkinLayoutEntry = SkinLayoutEntry::listening;

    uint64_t lastEffectChainRevision = 0;
    bool pluginInsertionInProgress = false;
    bool refreshingPlaylist = false;

    static constexpr int effectRowHeight = 44;
    static constexpr int effectRowGap = 8;
};
