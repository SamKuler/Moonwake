#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "app/PlaybackEndDetector.h"
#include "app/PlayerController.h"
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

class MainComponent final : public juce::Component,
                            private juce::TableListBoxModel,
                            private juce::Timer,
                            private SkinManager::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
private:
    class EffectSlotRow;
    class PluginEditorWindow;

    void timerCallback() override;
    void skinChanged() override;
    int getNumRows() override;
    void paintRowBackground (juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void chooseFiles();
    void choosePluginDirectory();
    void chooseSkinArchive();
    void chooseDeveloperSkinDirectory();
    void refreshPlaylist();
    void refreshPlugins();
    void refreshEffects();
    void updateTransportState();
    void openPluginEditor (int effectIndex);
    void removeEffect (int effectIndex);
    void moveEffectFromDrag (int fromIndex, int yInRackContent);
    void closePluginEditors();
    void setWorkspaceMode (WorkspaceMode mode);
    void bindSkinComponents();
    void applyWorkspaceVisuals();
    void closeDrawers();
    void updateSkinPersistenceStateFromManager();
    [[nodiscard]] juce::File getStateDirectory() const;
    [[nodiscard]] juce::File getStateFile() const;
    [[nodiscard]] juce::File getPluginListFile() const;

    PlayerController controller;
    PlaybackEndDetector playbackEndDetector;
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

    juce::TextButton addFilesButton { "Add Files" };
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

    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    bool effectsDrawerOpen = false;
    bool settingsDrawerOpen = false;
    SkinLayoutEntry activeSkinLayoutEntry = SkinLayoutEntry::listening;

    uint64_t lastEffectChainRevision = 0;
    bool pluginInsertionInProgress = false;

    static constexpr int effectRowHeight = 44;
    static constexpr int effectRowGap = 8;
};
