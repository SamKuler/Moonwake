#include "MainComponent.h"

#include "EffectRackLayout.h"
#include "app/ProductIdentity.h"
#include "app/ProductPaths.h"

namespace
{
void appendReport (SkinValidationReport& destination,
                   const SkinValidationReport& source)
{
    for (const auto& diagnostic : source.getDiagnostics())
    {
        if (diagnostic.severity == SkinDiagnosticSeverity::error)
            destination.addError (diagnostic.code, diagnostic.file,
                                  diagnostic.pointer, diagnostic.message,
                                  diagnostic.suggestion);
        else
            destination.addWarning (diagnostic.code, diagnostic.file,
                                    diagnostic.pointer, diagnostic.message,
                                    diagnostic.suggestion);
    }
}

juce::String workCountText (int count)
{
    return juce::String (count) + (count == 1 ? " WORK" : " WORKS");
}

class RackIconButton final : public juce::Button
{
public:
    using SkinPainter = std::function<bool (juce::Graphics&, bool, bool)>;

    explicit RackIconButton (juce::String nameToUse)
        : Button (std::move (nameToUse))
    {
    }

    void setSkinPainter (SkinPainter painterToUse)
    {
        skinPainter = std::move (painterToUse);
    }

    void paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        if (skinPainter)
            static_cast<void> (skinPainter (g, isMouseOverButton, isButtonDown));
    }

private:
    SkinPainter skinPainter;
};

class RackSkinLabel final : public juce::Label
{
public:
    using SkinPainter = std::function<bool (juce::Graphics&)>;

    void setSkinPainter (SkinPainter painterToUse)
    {
        skinPainter = std::move (painterToUse);
    }

    void paint (juce::Graphics& graphics) override
    {
        if (skinPainter)
            static_cast<void> (skinPainter (graphics));
    }

private:
    SkinPainter skinPainter;
};

} // namespace

class MainComponent::PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow (int slotIdToUse, juce::String title, juce::AudioProcessorEditor* editor)
        : DocumentWindow (
              std::move (title),
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::closeButton),
          slotId (slotIdToUse)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (true, true);
        centreWithSize (juce::jmax (420, getWidth()), juce::jmax (320, getHeight()));
        showWindow();
    }

    ~PluginEditorWindow() override
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            editor->processor.editorBeingDeleted (editor);
    }

    [[nodiscard]] bool ownsEffect (int id) const
    {
        return slotId == id;
    }

    void showWindow()
    {
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }

private:
    int slotId = 0;
};

class MainComponent::EffectSlotRow final : public juce::Component
{
public:
    EffectSlotRow (MainComponent& ownerToUse, int indexToUse, const EffectSlotState& slot)
        : owner (ownerToUse),
          index (indexToUse),
          slotCount (ownerToUse.controller.getEffectChain().size()),
          bypassed (slot.bypassed),
          hasError (! slot.available || slot.errorMessage.isNotEmpty()),
          slotName (slot.pluginName)
    {
        const auto formatSuffix = slot.pluginFormat.isNotEmpty() ? "  /  " + slot.pluginFormat : juce::String{};
        nameLabel.setText (slot.pluginName + formatSuffix + (slot.available ? juce::String{} : "  /  Unavailable"),
                           juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centredLeft);
        nameLabel.setInterceptsMouseClicks (false, false);

        bypassButton.setTooltip (slot.bypassed ? "Enable effect" : "Bypass effect");
        bypassButton.setClickingTogglesState (true);
        bypassButton.setToggleState (slot.bypassed, juce::dontSendNotification);
        editorButton.setTooltip ("Open plugin editor");
        removeButton.setTooltip ("Remove effect");
        bypassButton.setEnabled (slot.available);
        editorButton.setEnabled (slot.available);
        if (! slot.available && slot.errorMessage.isNotEmpty())
            nameLabel.setTooltip (slot.errorMessage);

        bypassButton.onClick = [this]
        {
            owner.controller.setEffectBypassedAtIndex (index, bypassButton.getToggleState());
            owner.refreshEffects();
        };
        editorButton.onClick = [this] { owner.openPluginEditor (index); };
        removeButton.onClick = [this] { owner.removeEffect (index); };

        nameLabel.setSkinPainter ([this] (juce::Graphics& graphics)
        {
            return drawSlot (graphics, nameLabel, "slot.name",
                             SkinRenderTarget::label, nameLabel.getText(), {},
                             nameLabel.isMouseOver (true), false, false);
        });
        bypassButton.setSkinPainter ([this] (juce::Graphics& graphics,
                                             bool over, bool down)
        {
            return drawSlot (graphics, bypassButton, "slot.bypass",
                             SkinRenderTarget::textButton,
                             bypassButton.getToggleState() ? "ON" : "OFF", "bypass",
                             over, down, bypassButton.getToggleState());
        });
        editorButton.setSkinPainter ([this] (juce::Graphics& graphics,
                                             bool over, bool down)
        {
            return drawSlot (graphics, editorButton, "slot.editor",
                             SkinRenderTarget::textButton, "EDIT", "editor",
                             over, down, false);
        });
        removeButton.setSkinPainter ([this] (juce::Graphics& graphics,
                                             bool over, bool down)
        {
            return drawSlot (graphics, removeButton, "slot.remove",
                             SkinRenderTarget::textButton,
                             juce::String::charToString (0x00d7), "remove",
                             over, down, false);
        });

        addAndMakeVisible (nameLabel);
        addAndMakeVisible (bypassButton);
        addAndMakeVisible (editorButton);
        addAndMakeVisible (removeButton);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragging = false;
        dragStart = event.position;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (event.position.getDistanceFrom (dragStart) > 4.0f)
        {
            dragging = true;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! dragging)
            return;

        dragging = false;
        const auto rackEvent = event.getEventRelativeTo (&owner.effectRackContent);
        owner.moveEffectFromDrag (index, juce::roundToInt (rackEvent.position.y));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        if (! drawSlot (g, *this, "slot.surface", SkinRenderTarget::surface,
                        {}, {}, isMouseOver (true), isMouseButtonDown(), false,
                        bounds))
            return;

        const juce::Rectangle<float> indicator (8.0f, 13.0f, 8.0f, 20.0f);
        static_cast<void> (drawSlot (
            g, *this, "slot.dragIndicator", SkinRenderTarget::surface,
            {}, "drag", isMouseOver (true), isMouseButtonDown(),
            false, indicator));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (10, 6);
        removeButton.setBounds (bounds.removeFromRight (32));
        bounds.removeFromRight (6);
        editorButton.setBounds (bounds.removeFromRight (32));
        bounds.removeFromRight (6);
        bypassButton.setBounds (bounds.removeFromRight (32));
        bounds.removeFromRight (10);
        bounds.removeFromLeft (18);
        nameLabel.setBounds (bounds);
    }

private:
    bool drawSlot (juce::Graphics& graphics, const juce::Component& control,
                   juce::StringRef slot, SkinRenderTarget target,
                   juce::String primaryText, juce::String icon,
                   bool over, bool down, bool selected,
                   juce::Rectangle<float> bounds = {})
    {
        SkinRenderContext context;
        context.bounds = bounds.isEmpty() ? control.getLocalBounds().toFloat() : bounds;
        context.contentBounds = context.bounds;
        context.primaryText = std::move (primaryText);
        context.iconName = std::move (icon);
        context.state.enabled = control.isEnabled();
        context.state.hover = over;
        context.state.pressed = down;
        context.state.focused = control.hasKeyboardFocus (true);
        context.state.selected = selected;
        context.state.dragging = dragging;
        context.extraLookup = [count = slotCount, row = index,
                               isBypassed = bypassed, error = hasError,
                               name = slotName] (std::string_view path)
            -> std::optional<SkinExpressionValue>
        {
            if (path == "chain.slotCount")
                return SkinExpressionValue (static_cast<double> (count));
            if (path == "slot.index")
                return SkinExpressionValue (static_cast<double> (row));
            if (path == "slot.bypassed") return SkinExpressionValue (isBypassed);
            if (path == "slot.hasError") return SkinExpressionValue (error);
            if (path == "slot.name") return SkinExpressionValue (name);
            return std::nullopt;
        };
        return owner.skinAdapter.drawCustomSlot (
            graphics, owner.effectRackViewport, SkinRenderTarget::effectChain,
            slot, target, std::move (context),
            "slot:" + juce::String (index) + ":" + juce::String (slot));
    }

    MainComponent& owner;
    int index = -1;
    int slotCount = 0;
    bool bypassed = false;
    bool hasError = false;
    juce::String slotName;
    bool dragging = false;
    juce::Point<float> dragStart;
    RackSkinLabel nameLabel;
    RackIconButton bypassButton { "Bypass" };
    RackIconButton editorButton { "Editor" };
    RackIconButton removeButton { "Remove" };
};

MainComponent::MainComponent()
    : nowPlayingCoordinator (createSystemNowPlayingBridge()),
      remoteCommandLifetime (std::make_shared<std::atomic_bool> (true)),
      libraryImportService (ProductPaths::artworkCacheRoot()),
      skinAdapter ([this] (const juce::Component& component,
                           SkinRenderTarget target,
                           const juce::String& style)
      {
          const auto session = appliedSkinSession != nullptr
                                 ? appliedSkinSession : skinManager.activeSession();
          const auto skin = session != nullptr ? session->compiledSkin() : nullptr;
          if (skin == nullptr) return JuceSkinAdapter::RuntimeBinding {};

          bool required = false;
          for (const auto& id : skinBindings.ids())
              if (skinBindings.find (id) == &component)
                  if (const auto* descriptor = PlayerComponentCatalog::find (id))
                  {
                      required = descriptor->requiredIn.contains (
                          activeSkinLayoutEntry);
                      break;
                  }
          return JuceSkinAdapter::RuntimeBinding {
              skin->resolveStyle (target, style), &session->animations(),
              session.get(), required
          };
      }, skinRenderer),
      audioSettingsPanel (controller, &skinAdapter),
      pluginSettingsPanel (controller, &skinAdapter),
      skinLibraryPanel (skinManager, &skinAdapter),
      skinLayoutHost (skinManager, skinBindings)
{
    controller.loadState (getStateFile(), getPluginListFile());
    workspaceMode = controller.getViewState().workspaceMode;
    SkinValidationReport skinReport;
    const auto skinReady = skinManager.initialise (
        ProductPaths::installedSkinsRoot(), skinReport);
    setLookAndFeel (&skinAdapter);
    if (skinReady)
    {
        skinManager.setPersistenceCallback ([this] (const SkinSelection& selection)
        {
            auto state = controller.getSkinPersistenceState();
            state.activeSkinId = selection.id;
            state.activeSkinVersion = selection.version;
            state.developerDirectory = skinManager.developerDirectory();
            controller.setSkinPersistenceState (std::move (state));
        });

        const auto restored = controller.getSkinPersistenceState();
        if (restored.developerDirectory != juce::File())
        {
            SkinValidationReport developerReport;
            if (! skinManager.restoreDeveloperDirectory (
                    restored.developerDirectory, developerReport))
            {
                appendReport (skinReport, developerReport);
                auto corrected = restored;
                corrected.developerDirectory = juce::File{};
                controller.setSkinPersistenceState (std::move (corrected));
            }
        }

        SkinValidationReport restoreReport;
        if (! skinManager.activate ({ restored.activeSkinId,
                                      restored.activeSkinVersion },
                                    false, restoreReport))
        {
            appendReport (skinReport, restoreReport);
        }

        updateSkinPersistenceStateFromManager();
        appliedSkinSession = skinManager.activeSession();
        skinManager.addListener (this);
        if (skinReport.hasErrors())
            skinFallbackDiagnostic.setMessage (skinReport.toText());
    }
    else
    {
        skinFallbackDiagnostic.setMessage (skinReport.toText());
    }

    appEyebrowLabel.setText ("MACOS AUDIO PLAYER", juce::dontSendNotification);
    appEyebrowLabel.setJustificationType (juce::Justification::centredLeft);

    titleLabel.setText (juce::String::fromUTF8 (MoonwakeIdentity::displayCredit),
                        juce::dontSendNotification);
    titleLabel.setTitle (juce::String::fromUTF8 (MoonwakeIdentity::displayCredit));
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    nowPlayingEyebrowLabel.setText ("NOW PLAYING", juce::dontSendNotification);
    nowPlayingEyebrowLabel.setJustificationType (juce::Justification::centredLeft);

    queueTitleLabel.setText ("LISTENING QUEUE", juce::dontSendNotification);
    queueTitleLabel.setJustificationType (juce::Justification::centredLeft);
    queueCountLabel.setJustificationType (juce::Justification::centredRight);
    emptyQueueLabel.setText ("No matching works\nDrop music or folders here, or use + Music",
                             juce::dontSendNotification);
    emptyQueueLabel.setJustificationType (juce::Justification::centred);

    effectRackTitleLabel.setText ("INSERT CHAIN", juce::dontSendNotification);
    effectRackTitleLabel.setJustificationType (juce::Justification::centredLeft);
    emptyRackLabel.setText ("No effects in this chain\nOpen Settings to scan VST3 or Audio Units",
                            juce::dontSendNotification);
    emptyRackLabel.setJustificationType (juce::Justification::centred);

    settingsTitleLabel.setJustificationType (juce::Justification::centredLeft);

    statusLabel.setText ("Ready", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    nowPlayingLabel.setText ("No track loaded", juce::dontSendNotification);
    nowPlayingLabel.setJustificationType (juce::Justification::centredLeft);
    trackMetaLabel.setText ("SELECT A TRACK TO BEGIN", juce::dontSendNotification);
    trackMetaLabel.setJustificationType (juce::Justification::centredLeft);
    artworkMetaLabel.setJustificationType (juce::Justification::centred);
    effectsSummaryLabel.setJustificationType (juce::Justification::centredLeft);
    deviceStatusLabel.setJustificationType (juce::Justification::centredRight);
    volumeLabel.setText ("VOLUME", juce::dontSendNotification);
    volumeLabel.setJustificationType (juce::Justification::centredRight);
    volumeValueLabel.setJustificationType (juce::Justification::centredRight);
    timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
    timeLabel.setJustificationType (juce::Justification::centred);

    repeatModeBox.addItem ("Repeat Off", 1);
    repeatModeBox.addItem ("Repeat All", 2);
    repeatModeBox.addItem ("Repeat One", 3);
    repeatModeBox.setSelectedId (1);

    outputLayoutBox.addItem ("Stereo", 1);
    outputLayoutBox.addItem ("5.1", 2);
    outputLayoutBox.addItem ("5.1.2", 3);
    outputLayoutBox.addItem ("7.1.2", 4);
    outputLayoutBox.setSelectedId (1);

    playlistTable.setModel (this);
    playlistTable.setOutlineThickness (0);
    playlistTable.setRowHeight (32);
    playlistTable.setHeaderHeight (24);
    playlistTable.getHeader().addColumn ("WORK", 1, 240, 120, 500);
    playlistTable.getHeader().addColumn ("LOCATION", 2, 360, 180, 800);
    playlistTable.getHeader().addColumn ("DURATION", 3, 90, 70, 120);
    skinAdapter.configureListBox (playlistTable);

    librarySearchEditor.setName ("Search works");
    librarySearchEditor.setTitle ("Search title, artist, album, format, or channel count");
    librarySearchEditor.setTextToShowWhenEmpty (
        "Search title, artist, album, format...",
        skinAdapter.findColour (juce::TextEditor::textColourId).withAlpha (0.58f));
    librarySearchEditor.onTextChange = [this]
    {
        libraryFilter.setQuery (librarySearchEditor.getText());
        refreshPlaylist();
    };
    librarySearchEditor.onEscapeKey = [this]
    {
        librarySearchEditor.clear();
        grabKeyboardFocus();
    };

    pluginFilterLabel.setText ("Filter", juce::dontSendNotification);
    pluginFilterLabel.setJustificationType (juce::Justification::centredLeft);
    pluginFilterEditor.setTextToShowWhenEmpty (
        "Search by plug-in name, maker, or format",
        skinAdapter.findColour (juce::TextEditor::textColourId));
    pluginCombo.setTextWhenNothingSelected ("No matching effects");
    effectRackViewport.setViewedComponent (&effectRackContent, false);
    effectRackViewport.setScrollBarsShown (true, false);

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (0.8);
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    seekSlider.setRange (0.0, 1.0, 0.001);
    seekSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    previousButton.setButtonText ("Prev");
    playPauseButton.setButtonText ("Play");
    nextButton.setButtonText ("Next");
    moveTrackUpButton.setButtonText ("Up");
    moveTrackDownButton.setButtonText ("Down");

    studioModeButton.setClickingTogglesState (true);
    listeningModeButton.setClickingTogglesState (true);
    studioModeButton.setTooltip ("Switch to Studio A (1)");
    listeningModeButton.setTooltip ("Switch to Listening B (2)");
    studioModeButton.setRadioGroupId (101);
    listeningModeButton.setRadioGroupId (101);
    settingsButton.setClickingTogglesState (true);
    effectsButton.setClickingTogglesState (true);
    addFilesButton.setButtonText ("+ Music");
    settingsCloseButton.setButtonText ("Close");
    addFilesButton.setTooltip ("Add music files or folders");
    settingsButton.setTooltip ("Open audio, plug-in, and skin settings");
    effectsButton.setTooltip ("Open the effects rack");
    effectsCloseButton.setTooltip ("Close the effects rack");
    settingsCloseButton.setTooltip ("Close settings");
    addEffectButton.setTooltip ("Add the selected effect");
    previousButton.setTooltip ("Previous track");
    playPauseButton.setTooltip ("Play");
    nextButton.setTooltip ("Next track");
    removeTrackButton.setTooltip ("Remove the selected track");
    moveTrackUpButton.setTooltip ("Move the selected track up");
    moveTrackDownButton.setTooltip ("Move the selected track down");

    juce::Component* children[] = { &appEyebrowLabel,
                                    &titleLabel,
                                    &studioModeButton,
                                    &listeningModeButton,
                                    &settingsButton,
                                    &effectsButton,
                                    &effectsCloseButton,
                                    &settingsCloseButton,
                                    &addFilesButton,
                                    &librarySearchEditor,
                                    &removeTrackButton,
                                    &clearPlaylistButton,
                                    &moveTrackUpButton,
                                    &moveTrackDownButton,
                                    &pluginFilterLabel,
                                    &pluginFilterEditor,
                                    &pluginCombo,
                                    &addEffectButton,
                                    &nowPlayingEyebrowLabel,
                                    &queueTitleLabel,
                                    &queueCountLabel,
                                    &playlistTable,
                                    &emptyQueueLabel,
                                    &nowPlayingLabel,
                                    &trackMetaLabel,
                                    &artworkMetaLabel,
                                    &effectsSummaryLabel,
                                    &settingsTitleLabel,
                                    &deviceStatusLabel,
                                    &previousButton,
                                    &playPauseButton,
                                    &nextButton,
                                    &shuffleButton,
                                    &repeatModeBox,
                                    &seekSlider,
                                    &timeLabel,
                                    &volumeLabel,
                                    &volumeValueLabel,
                                    &volumeSlider,
                                    &outputLayoutBox,
                                    &effectRackTitleLabel,
                                    &effectRackViewport,
                                    &emptyRackLabel,
                                    &statusLabel,
                                    &albumArtwork,
                                    &outputStage,
                                    &skinFallbackDiagnostic };

    for (auto* child : children)
        addAndMakeVisible (child);
    skinFallbackDiagnostic.setVisible (! skinReady);

    audioSettingsPanel.addTo (*this);
    pluginSettingsPanel.addTo (*this);
    skinLibraryPanel.addTo (*this);

    const auto showStatus = [this] (const juce::String& message)
    {
        statusLabel.setText (message, juce::dontSendNotification);
        updateTransportState();
    };
    audioSettingsPanel.setStatusCallback (showStatus);
    pluginSettingsPanel.setStatusCallback (showStatus);
    pluginSettingsPanel.setScanFolderCallback ([this] { choosePluginDirectory(); });
    skinLibraryPanel.setStatusCallback (showStatus);
    skinLibraryPanel.setImportCallback ([this] { chooseSkinArchive(); });
    skinLibraryPanel.setChooseDeveloperDirectoryCallback (
        [this] { chooseDeveloperSkinDirectory(); });

    addFilesButton.onClick = [this]
    {
        if (libraryImportService.isRunning())
        {
            addFilesButton.setEnabled (false);
            statusLabel.setText ("Cancelling music import...",
                                 juce::dontSendNotification);
            libraryImportService.cancel();
        }
        else
        {
            chooseFiles();
        }
    };
    studioModeButton.onClick = [this] { setWorkspaceMode (WorkspaceMode::studio); };
    listeningModeButton.onClick = [this] { setWorkspaceMode (WorkspaceMode::listening); };
    settingsButton.onClick = [this]
    {
        settingsDrawerOpen = settingsButton.getToggleState();
        if (settingsDrawerOpen)
        {
            effectsDrawerOpen = false;
            audioSettingsPanel.refresh();
            pluginSettingsPanel.refreshInventory();
            pluginSettingsPanel.refreshScanState();
            skinLibraryPanel.refresh();
        }
        applyWorkspaceVisuals();
        resized();
    };
    effectsButton.onClick = [this]
    {
        effectsDrawerOpen = effectsButton.getToggleState();
        if (effectsDrawerOpen)
            settingsDrawerOpen = false;
        applyWorkspaceVisuals();
        resized();
    };
    effectsCloseButton.onClick = [this]
    {
        effectsDrawerOpen = false;
        applyWorkspaceVisuals();
        resized();
    };
    settingsCloseButton.onClick = [this]
    {
        closeDrawers();
        applyWorkspaceVisuals();
        resized();
    };
    removeTrackButton.onClick = [this]
    {
        const auto index = libraryFilter.playlistIndexForRow (
            playlistTable.getSelectedRow());
        if (! controller.removeTrack (index))
            statusLabel.setText ("Select a track to remove.", juce::dontSendNotification);
        else
            statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);

        refreshPlaylist();
        updateTransportState();
    };
    clearPlaylistButton.onClick = [this]
    {
        controller.clearPlaylist();
        refreshPlaylist();
        statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
        updateTransportState();
    };
    moveTrackUpButton.onClick = [this]
    {
        const auto index = libraryFilter.playlistIndexForRow (
            playlistTable.getSelectedRow());
        if (controller.moveTrack (index, index - 1))
        {
            refreshPlaylist();
            statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
        }
    };
    moveTrackDownButton.onClick = [this]
    {
        const auto index = libraryFilter.playlistIndexForRow (
            playlistTable.getSelectedRow());
        if (controller.moveTrack (index, index + 1))
        {
            refreshPlaylist();
            statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
        }
    };
    addEffectButton.onClick = [this]
    {
        if (pluginInsertionInProgress)
            return;

        pluginInsertionInProgress = true;
        addEffectButton.setEnabled (false);
        statusLabel.setText ("Loading plug-in...", juce::dontSendNotification);
        controller.addPluginByIndexAsync (
            pluginCombo.getSelectedId() - 1,
            [safeThis = juce::Component::SafePointer<MainComponent> (this)]
            (bool, const juce::String& message)
            {
                if (safeThis == nullptr)
                    return;

                safeThis->pluginInsertionInProgress = false;
                safeThis->addEffectButton.setEnabled (safeThis->pluginCombo.getSelectedId() > 0);
                safeThis->statusLabel.setText (message, juce::dontSendNotification);
                safeThis->refreshEffects();
            });
    };
    pluginFilterEditor.onTextChange = [this] { refreshPlugins(); };
    playPauseButton.onClick = [this]
    {
        controller.playOrPause();
        statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
        updateTransportState();
    };
    nextButton.onClick = [this]
    {
        controller.nextTrack();
        refreshPlaylist();
        statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
        updateTransportState();
    };
    previousButton.onClick = [this]
    {
        controller.previousTrack();
        refreshPlaylist();
        statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
        updateTransportState();
    };
    shuffleButton.onClick = [this]
    {
        controller.setShuffleEnabled (shuffleButton.getToggleState());
        updateTransportState();
    };
    repeatModeBox.onChange = [this]
    {
        switch (repeatModeBox.getSelectedId())
        {
            case 2: controller.setRepeatMode (RepeatMode::all); break;
            case 3: controller.setRepeatMode (RepeatMode::one); break;
            default: controller.setRepeatMode (RepeatMode::off); break;
        }
        updateTransportState();
    };
    outputLayoutBox.onChange = [this]
    {
        juce::String error;
        switch (outputLayoutBox.getSelectedId())
        {
            case 2: error = controller.setOutputLayout (OutputLayout::fiveOne); break;
            case 3: error = controller.setOutputLayout (OutputLayout::fiveOneTwo); break;
            case 4: error = controller.setOutputLayout (OutputLayout::sevenOneTwo); break;
            default: error = controller.setOutputLayout (OutputLayout::stereo); break;
        }

        statusLabel.setText (error.isNotEmpty() ? error : controller.getLastStatusMessage(),
                             juce::dontSendNotification);
        audioSettingsPanel.refresh();
        updateTransportState();
    };
    volumeSlider.onValueChange = [this]
    {
        controller.setVolume (volumeSlider.getValue());
    };
    seekSlider.onDragEnd = [this]
    {
        controller.seekToRatio (seekSlider.getValue());
        updateTransportState();
    };

    const auto remoteLifetime = remoteCommandLifetime;
    nowPlayingCoordinator.setCommandHandler (
        [this, remoteLifetime] (const RemoteTransportCommand& command)
        {
            juce::MessageManager::callAsync (
                [this, remoteLifetime, command]
                {
                    if (remoteLifetime->load (std::memory_order_acquire))
                        handleRemoteTransportCommand (command);
                });
        });

    refreshPlugins();
    audioSettingsPanel.refresh();
    pluginSettingsPanel.refreshInventory();
    pluginSettingsPanel.refreshScanState();
    skinLibraryPanel.refresh();
    refreshEffects();
    refreshPlaylist();
    bindSkinComponents();
    applyWorkspaceVisuals();
    updateTransportState();
    statusLabel.setText (
        SkinFallbackDiagnostic::makeStartupStatusText (
            skinReport, controller.getLastStatusMessage()),
        juce::dontSendNotification);
    startTimerHz (5);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    juce::Component* focusOrder[] = { &listeningModeButton, &studioModeButton, &settingsButton,
                                      &effectsButton, &effectsCloseButton,
                                      &settingsCloseButton, &addFilesButton,
                                      &librarySearchEditor,
                                      &removeTrackButton, &clearPlaylistButton,
                                      &moveTrackUpButton, &moveTrackDownButton, &playlistTable,
                                      &previousButton, &playPauseButton, &nextButton,
                                      &seekSlider, &outputLayoutBox,
                                      &audioSettingsPanel.deviceControl(),
                                      &audioSettingsPanel.sampleRateControl(),
                                      &audioSettingsPanel.bufferSizeControl(),
                                      &audioSettingsPanel.channelLayoutControl(),
                                      &pluginFilterEditor, &pluginCombo, &addEffectButton,
                                      &effectRackViewport };
    int focusIndex = 1;
    for (auto* component : focusOrder)
        component->setExplicitFocusOrder (focusIndex++);

    setSize (1240, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    remoteCommandLifetime->store (false, std::memory_order_release);
    nowPlayingCoordinator.setCommandHandler ({});
    nowPlayingCoordinator.clear();
    libraryImportService.cancel();
    static_cast<void> (stateSaveService.waitForPendingWrite());
    controller.checkpointPlaybackPosition();
    skinManager.removeListener (this);
    skinLayoutHost.clear();
    updateSkinPersistenceStateFromManager();
    if (! controller.saveState (getStateFile(), getPluginListFile()))
        juce::Logger::writeToLog (
            "Moonwake shutdown save failed: "
            + controller.getLastStatusMessage());
    setLookAndFeel (nullptr);
}

void MainComponent::updateSkinPersistenceStateFromManager()
{
    if (skinManager.activeSession() == nullptr)
        return;

    const auto selection = skinManager.committedSelection();
    controller.setSkinPersistenceState ({ selection.id, selection.version,
                                          skinManager.developerDirectory() });
}

void MainComponent::bindSkinComponents()
{
    SkinValidationReport report;
    const std::pair<const char*, juce::Component*> publicComponents[] {
        { "app.brand.eyebrow", &appEyebrowLabel },
        { "app.brand.title", &titleLabel },
        { "app.deviceStatus", &deviceStatusLabel },
        { "app.status", &statusLabel },
        { "workspace.studio", &studioModeButton },
        { "workspace.listening", &listeningModeButton },
        { "settings.open", &settingsButton },
        { "library.add", &addFilesButton },
        { "library.search", &librarySearchEditor },
        { "queue.title", &queueTitleLabel },
        { "queue.count", &queueCountLabel },
        { "queue.table", &playlistTable },
        { "queue.empty", &emptyQueueLabel },
        { "queue.remove", &removeTrackButton },
        { "queue.clear", &clearPlaylistButton },
        { "queue.moveUp", &moveTrackUpButton },
        { "queue.moveDown", &moveTrackDownButton },
        { "artwork.cover", &albumArtwork },
        { "artwork.metadata", &artworkMetaLabel },
        { "nowPlaying.eyebrow", &nowPlayingEyebrowLabel },
        { "nowPlaying.title", &nowPlayingLabel },
        { "nowPlaying.metadata", &trackMetaLabel },
        { "transport.previous", &previousButton },
        { "transport.playPause", &playPauseButton },
        { "transport.next", &nextButton },
        { "transport.seek", &seekSlider },
        { "transport.time", &timeLabel },
        { "transport.shuffle", &shuffleButton },
        { "transport.repeat", &repeatModeBox },
        { "transport.volumeLabel", &volumeLabel },
        { "transport.volumeValue", &volumeValueLabel },
        { "transport.volume", &volumeSlider },
        { "effects.open", &effectsButton },
        { "effects.close", &effectsCloseButton },
        { "effects.summary", &effectsSummaryLabel },
        { "effects.title", &effectRackTitleLabel },
        { "effects.filterLabel", &pluginFilterLabel },
        { "effects.filter", &pluginFilterEditor },
        { "effects.catalog", &pluginCombo },
        { "effects.add", &addEffectButton },
        { "effects.chain", &effectRackViewport },
        { "effects.empty", &emptyRackLabel },
        { "effects.outputLayout", &outputLayoutBox },
        { "output.stage", &outputStage },
        { "settings.title", &settingsTitleLabel },
        { "settings.close", &settingsCloseButton }
    };
    for (const auto& [id, component] : publicComponents)
        static_cast<void> (skinBindings.bind (id, *component, report));
    audioSettingsPanel.bind (skinBindings, report);
    pluginSettingsPanel.bind (skinBindings, report);
    skinLibraryPanel.bind (skinBindings, report);
    if (report.hasErrors())
        statusLabel.setText (report.toText(), juce::dontSendNotification);
    jassert (! report.hasErrors());
}

void MainComponent::skinChanged()
{
    skinLibraryPanel.refresh();
    resized();
    if (! librarySearchEditor.isShowing()
        && librarySearchEditor.getText().isNotEmpty())
        librarySearchEditor.clear();
    repaint();
}

void MainComponent::resized()
{
    if (skinManager.activeSession() == nullptr)
    {
        skinLayoutHost.clear();
        appliedSkinSession.reset();
        hasAppliedSkinLayout = false;
        for (const auto& id : skinBindings.ids())
            if (auto* component = skinBindings.find (id))
                component->setVisible (false);
        skinFallbackDiagnostic.setBounds (getLocalBounds());
        skinFallbackDiagnostic.setVisible (true);
        skinFallbackDiagnostic.toFront (false);
        return;
    }

    skinFallbackDiagnostic.setVisible (false);
    const auto requestedEntry = settingsDrawerOpen
                                  ? SkinLayoutEntry::settings
                                  : (effectsDrawerOpen
                                         ? SkinLayoutEntry::effects
                                         : (workspaceMode == WorkspaceMode::studio
                                                ? SkinLayoutEntry::studio
                                                : SkinLayoutEntry::listening));
    SkinValidationReport report;
    if (skinLayoutHost.apply (requestedEntry, getLocalBounds(),
                              1.0f, report))
    {
        activeSkinLayoutEntry = requestedEntry;
        appliedSkinSession = skinManager.activeSession();
        hasAppliedSkinLayout = true;
    }
    else
    {
        if (hasAppliedSkinLayout)
        {
            statusLabel.setText (report.toText(), juce::dontSendNotification);
        }
        else
        {
            skinLayoutHost.clear();
            for (const auto& id : skinBindings.ids())
                if (auto* component = skinBindings.find (id))
                    component->setVisible (false);
            skinFallbackDiagnostic.setMessage (report.toText());
            skinFallbackDiagnostic.setBounds (getLocalBounds());
            skinFallbackDiagnostic.setVisible (true);
            skinFallbackDiagnostic.toFront (false);
        }
    }
    updateEmptyQueueState();
    refreshEffects();
    repaint();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        playPauseButton.triggerClick();
        return true;
    }

    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'o' || key.getKeyCode() == 'O'))
    {
        chooseFiles();
        return true;
    }

    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'f' || key.getKeyCode() == 'F'))
    {
        if (effectsDrawerOpen && pluginFilterEditor.isShowing())
        {
            pluginFilterEditor.grabKeyboardFocus();
            pluginFilterEditor.selectAll();
            return true;
        }

        if (! settingsDrawerOpen && librarySearchEditor.isShowing())
        {
            librarySearchEditor.grabKeyboardFocus();
            librarySearchEditor.selectAll();
            return true;
        }

        return false;
    }

    if (key.getKeyCode() == '1')
    {
        setWorkspaceMode (WorkspaceMode::studio);
        return true;
    }

    if (key.getKeyCode() == '2')
    {
        setWorkspaceMode (WorkspaceMode::listening);
        return true;
    }

    return false;
}

void MainComponent::setWorkspaceMode (WorkspaceMode mode)
{
    if (workspaceMode == mode)
        return;

    closeDrawers();
    workspaceMode = mode;
    controller.setWorkspaceMode (mode);
    applyWorkspaceVisuals();
    updateTransportState();
    resized();
}

void MainComponent::applyWorkspaceVisuals()
{
    const auto isStudio = workspaceMode == WorkspaceMode::studio;
    studioModeButton.setToggleState (isStudio, juce::dontSendNotification);
    listeningModeButton.setToggleState (! isStudio, juce::dontSendNotification);
    settingsButton.setToggleState (settingsDrawerOpen, juce::dontSendNotification);
    effectsButton.setToggleState (effectsDrawerOpen, juce::dontSendNotification);
    queueTitleLabel.setText (isStudio ? "WORKS  /  PROGRAM" : "WORKS  /  UP NEXT",
                             juce::dontSendNotification);
    effectRackTitleLabel.setText ("Effects", juce::dontSendNotification);
    settingsTitleLabel.setText ("Settings", juce::dontSendNotification);
    pluginFilterLabel.setText ("FILTER PLUG-INS", juce::dontSendNotification);
    repaint();
}

void MainComponent::closeDrawers()
{
    effectsDrawerOpen = false;
    settingsDrawerOpen = false;
}

void MainComponent::timerCallback()
{
    const auto playbackState = controller.getViewState();
    if (playbackEndDetector.update (playbackState.isPlaying,
                                    playbackState.positionSeconds,
                                    playbackState.lengthSeconds)
        && playbackState.transportIntent == TransportIntent::playing)
    {
        controller.handlePlaybackFinished();
        refreshPlaylist();
        statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);
    }

    controller.checkpointPlaybackPosition();
    if (pluginSettingsPanel.refreshScanState())
        refreshPlugins();

    const auto effectRevision = controller.getEffectChainRevision();
    if (effectRevision != lastEffectChainRevision)
    {
        lastEffectChainRevision = effectRevision;
        refreshEffects();
    }

    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto revision = controller.getStateRevision();
    if (autoSaveScheduler.observe (revision, nowSeconds))
    {
        PlayerAppState snapshot;
        juce::String captureError;
        if (! controller.captureStateSnapshot (snapshot, captureError))
        {
            handleAutoSaveCompletion (revision, false, captureError);
        }
        else
        {
            const juce::Component::SafePointer<MainComponent> safeThis (this);
            if (! stateSaveService.start (
                    std::move (snapshot), getStateFile(),
                    [safeThis, revision] (bool saved,
                                          juce::String errorMessage)
                    {
                        if (safeThis != nullptr)
                            safeThis->handleAutoSaveCompletion (
                                revision, saved, errorMessage);
                    }))
            {
                handleAutoSaveCompletion (
                    revision, false,
                    "The background state writer was unavailable.");
            }
        }
    }

    updateTransportState();
}

void MainComponent::handleAutoSaveCompletion (
    uint64_t revision,
    bool saved,
    const juce::String& errorMessage)
{
    const auto completedAtSeconds =
        juce::Time::getMillisecondCounterHiRes() / 1000.0;
    autoSaveScheduler.acknowledge (revision, completedAtSeconds, saved);
    if (! saved)
    {
        autoSaveFailureVisible = true;
        const auto detail = errorMessage.isNotEmpty()
                                ? errorMessage
                                : "Unknown write error.";
        statusLabel.setText ("Could not autosave Moonwake state: " + detail,
                             juce::dontSendNotification);
    }
    else if (autoSaveFailureVisible)
    {
        autoSaveFailureVisible = false;
        statusLabel.setText ("Autosave recovered.",
                             juce::dontSendNotification);
    }
}

int MainComponent::getNumRows()
{
    return libraryFilter.visibleCount();
}

void MainComponent::paintRowBackground (juce::Graphics& graphics,
                                        int rowNumber,
                                        int width,
                                        int height,
                                        bool rowIsSelected)
{
    const auto playlistIndex = libraryFilter.playlistIndexForRow (rowNumber);
    const auto* track = controller.getPlaylist().getTrack (playlistIndex);
    if (track == nullptr)
        return;

    auto duration = juce::String ("--");
    if (track->durationSeconds > 0.0)
    {
        const auto totalSeconds = juce::jmax (
            0, static_cast<int> (std::round (track->durationSeconds)));
        duration = juce::String (totalSeconds / 60) + ":"
                 + juce::String (totalSeconds % 60).paddedLeft ('0', 2);
    }
    const auto firstWidth = playlistTable.getHeader().getColumnWidth (1);
    const auto secondWidth = playlistTable.getHeader().getColumnWidth (2);
    const JuceSkinAdapter::ListBoxRowGeometry geometry {
        { 0.0f, 0.0f, static_cast<float> (firstWidth),
          static_cast<float> (height) },
        { static_cast<float> (firstWidth), 0.0f,
          static_cast<float> (secondWidth), static_cast<float> (height) },
        { static_cast<float> (firstWidth + secondWidth), 0.0f,
          static_cast<float> (juce::jmax (0, width - firstWidth - secondWidth)),
          static_cast<float> (height) }
    };
    skinAdapter.drawListBoxRow (
        graphics, playlistTable, rowNumber, width, height,
        rowIsSelected || playlistIndex == controller.getPlaylist().getSelectedIndex(),
        track->title + (track->available ? juce::String{} : " (Missing)"),
        track->file.getFullPathName(),
        duration, geometry,
        false, false, track->available, track->artist, track->album);
}

void MainComponent::paintCell (juce::Graphics&, int, int, int, int, bool)
{
    // The table renderer paints the complete row, including every cell, in
    // paintRowBackground so composite slot styling stays atomic.
}

void MainComponent::selectedRowsChanged (int lastRowSelected)
{
    if (refreshingPlaylist)
        return;
    const auto playlistIndex = libraryFilter.playlistIndexForRow (lastRowSelected);
    if (! controller.selectTrack (playlistIndex) && playlistIndex >= 0)
        statusLabel.setText (controller.getLastStatusMessage(), juce::dontSendNotification);

    refreshPlaylist();
    updateTransportState();
}

void MainComponent::chooseFiles()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Add music files or folders",
                                                       juce::File{},
                                                       "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.m4a;*.aac;*.alac;*.caf;*.ogg");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safeThis = juce::Component::SafePointer<MainComponent> (this)]
                              (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  juce::Array<juce::File> files;
                                  for (const auto& url : chooser.getURLResults())
                                      if (url.isLocalFile())
                                          files.add (url.getLocalFile());

                                  safeThis->startLibraryImport (std::move (files));
                              });
}

void MainComponent::importFilesAndFolders (const juce::Array<juce::File>& files)
{
    startLibraryImport (files);
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.isDirectory() || LibraryImportService::isSupportedAudioFile (file))
            return true;
    }
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& paths, int, int)
{
    juce::Array<juce::File> files;
    for (const auto& path : paths)
        files.add (juce::File (path));
    startLibraryImport (std::move (files));
}

void MainComponent::startLibraryImport (juce::Array<juce::File> files)
{
    if (files.isEmpty())
        return;
    if (libraryImportService.isRunning())
    {
        queuedLibraryImportInputs.addArray (files);
        statusLabel.setText (
            juce::String (files.size())
                + (files.size() == 1 ? " import item queued."
                                     : " import items queued."),
                             juce::dontSendNotification);
        return;
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    if (! libraryImportService.start (
            std::move (files),
            [safeThis] (const LibraryImportProgress& progress)
            {
                if (safeThis == nullptr)
                    return;
                if (progress.stage == LibraryImportProgress::Stage::discovering)
                    safeThis->statusLabel.setText (
                        "Discovering music...", juce::dontSendNotification);
                else
                    safeThis->statusLabel.setText (
                        "Reading " + juce::String (progress.processed + 1)
                            + " / " + juce::String (progress.total)
                            + "  " + progress.currentFile,
                        juce::dontSendNotification);
            },
            [safeThis] (LibraryImportResult result) mutable
            {
                if (safeThis == nullptr)
                    return;
                safeThis->setLibraryImportRunning (false);
                if (result.cancelled)
                {
                    safeThis->statusLabel.setText (
                        "Music import cancelled.", juce::dontSendNotification);
                }
                else
                {
                    auto message = safeThis->controller.addPreparedTracks (
                        std::move (result.tracks));
                    if (! result.skippedPaths.isEmpty())
                        message += " " + juce::String (result.skippedPaths.size())
                                 + (result.skippedPaths.size() == 1
                                        ? " item was skipped." : " items were skipped.");
                    if (! result.artworkWarnings.isEmpty())
                        message += " " + juce::String (result.artworkWarnings.size())
                                 + (result.artworkWarnings.size() == 1
                                        ? " artwork warning." : " artwork warnings.");
                    safeThis->statusLabel.setText (
                        message, juce::dontSendNotification);
                    safeThis->refreshPlaylist();
                    safeThis->updateTransportState();
                }

                if (! safeThis->queuedLibraryImportInputs.isEmpty())
                {
                    auto queuedInputs = std::move (
                        safeThis->queuedLibraryImportInputs);
                    safeThis->queuedLibraryImportInputs.clear();
                    juce::MessageManager::callAsync (
                        [safeThis,
                         pendingInputs = std::move (queuedInputs)] () mutable
                        {
                            if (safeThis != nullptr)
                                safeThis->startLibraryImport (
                                    std::move (pendingInputs));
                        });
                }
            }))
    {
        setLibraryImportRunning (false);
        statusLabel.setText ("Could not start the music import.",
                             juce::dontSendNotification);
    }
    else
    {
        setLibraryImportRunning (true);
    }
}

void MainComponent::setLibraryImportRunning (bool running)
{
    addFilesButton.setEnabled (true);
    addFilesButton.setButtonText (running ? "Cancel Import" : "+ Music");
    addFilesButton.setTooltip (running ? "Cancel the current music import"
                                       : "Add music files or folders");
    addFilesButton.getProperties().set (
        SkinComponentBindings::iconNameProperty,
        running ? "close" : "add");
    addFilesButton.repaint();
}

void MainComponent::choosePluginDirectory()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Scan VST3 plugin folder",
                                                       juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                       "*");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis = juce::Component::SafePointer<MainComponent> (this)]
                              (const juce::FileChooser& chooser)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  const auto directory = chooser.getResult();
                                  if (! directory.isDirectory())
                                      return;

                                  if (! safeThis->controller.startPluginScan (directory))
                                      safeThis->statusLabel.setText (safeThis->controller.getLastStatusMessage(),
                                                                    juce::dontSendNotification);
                              });
}

void MainComponent::chooseSkinArchive()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import Moonwake skin",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.moonwake-skin");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MainComponent> (this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safeThis->skinLibraryPanel.importFile (file);
        });
}

void MainComponent::chooseDeveloperSkinDirectory()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose developer skin directory",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<MainComponent> (this)]
        (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;
            const auto directory = chooser.getResult();
            if (directory.isDirectory())
            {
                safeThis->skinLibraryPanel.setDeveloperDirectory (directory);
                safeThis->updateSkinPersistenceStateFromManager();
            }
        });
}

void MainComponent::refreshPlaylist()
{
    libraryFilter.rebuild (controller.getPlaylist());
    const juce::ScopedValueSetter<bool> guard (refreshingPlaylist, true);
    playlistTable.updateContent();
    const auto visibleSelected = libraryFilter.rowForPlaylistIndex (
        controller.getPlaylist().getSelectedIndex());
    if (visibleSelected >= 0)
        playlistTable.selectRow (visibleSelected);
    else
        playlistTable.deselectAllRows();
    queueCountLabel.setText (
        libraryFilter.query().isEmpty()
            ? workCountText (controller.getPlaylist().size())
            : juce::String (libraryFilter.visibleCount()) + " / "
                + juce::String (controller.getPlaylist().size()),
        juce::dontSendNotification);
    updateEmptyQueueState();
    repaint();
}

void MainComponent::updateEmptyQueueState()
{
    const auto placedBySkin = emptyQueueLabel.getParentComponent() != this;
    const auto hasNoVisibleWorks = libraryFilter.visibleCount() == 0;
    emptyQueueLabel.setText (
        libraryFilter.query().isEmpty()
            ? "No works yet\nDrop music or folders here, or use + Music"
            : "No matching works\nTry another search",
        juce::dontSendNotification);
    emptyQueueLabel.setVisible (placedBySkin && playlistTable.isVisible()
                                && hasNoVisibleWorks);
}

void MainComponent::refreshPlugins()
{
    pluginCombo.clear (juce::dontSendNotification);

    const auto plugins = controller.getInsertEffectPlugins();
    const auto filter = pluginFilterEditor.getText().trim();
    for (int i = 0; i < plugins.size(); ++i)
    {
        const auto searchable = plugins[i].name + " " + plugins[i].manufacturerName
            + " " + plugins[i].category + " " + plugins[i].pluginFormatName;
        if (filter.isNotEmpty() && ! searchable.containsIgnoreCase (filter))
            continue;

        auto manufacturer = plugins[i].manufacturerName.trim();
        if (manufacturer.isEmpty())
            manufacturer = "Unknown";
        pluginCombo.addItem ("[" + plugins[i].pluginFormatName + "] " + plugins[i].name
                                 + " - " + manufacturer,
                             i + 1);
    }

    if (pluginCombo.getNumItems() > 0)
        pluginCombo.setSelectedItemIndex (0);

    addEffectButton.setEnabled (! pluginInsertionInProgress && pluginCombo.getSelectedId() > 0);
}

void MainComponent::refreshEffects()
{
    for (auto& row : effectRows)
        effectRackContent.removeChildComponent (row.get());

    effectRows.clear();

    const auto& chain = controller.getEffectChain();
    effectRackViewport.setSlotCount (chain.size());
    for (int i = 0; i < chain.size(); ++i)
    {
        if (const auto* slot = chain.getSlot (i))
        {
            auto row = std::make_unique<EffectSlotRow> (*this, i, *slot);
            effectRackContent.addAndMakeVisible (*row);
            effectRows.push_back (std::move (row));
        }
    }

    const auto contentWidth = juce::jmax (1, effectRackViewport.getWidth() - 14);
    effectRackContent.setSize (contentWidth,
                               juce::jmax (effectRackViewport.getHeight(),
                                           chain.size() * (effectRowHeight + effectRowGap)));

    auto y = 0;
    for (auto& row : effectRows)
    {
        row->setBounds (0, y, contentWidth, effectRowHeight);
        y += effectRowHeight + effectRowGap;
    }

    effectRackContent.repaint();
    if (skinManager.activeSession() == nullptr)
    {
        emptyRackLabel.setVisible (effectRackViewport.isVisible()
                                   && chain.size() == 0);
        emptyRackLabel.toFront (false);
    }
}

void MainComponent::updateTransportState()
{
    const auto state = controller.getViewState();
    nowPlayingCoordinator.update (
        state, juce::Time::getMillisecondCounterHiRes() / 1000.0);

    if (! seekSlider.isMouseButtonDown())
        seekSlider.setValue (state.seekRatio, juce::dontSendNotification);

    playPauseButton.setButtonText (state.isPlaying ? "Pause" : "Play");
    playPauseButton.setTooltip (state.isPlaying ? "Pause" : "Play");
    playPauseButton.getProperties().set (
        SkinComponentBindings::iconNameProperty,
        state.isPlaying ? "pause" : "play");
    timeLabel.setText (controller.getPlaybackTimeText(), juce::dontSendNotification);
    volumeSlider.setValue (state.volume, juce::dontSendNotification);
    shuffleButton.setToggleState (state.shuffleEnabled, juce::dontSendNotification);

    switch (state.repeatMode)
    {
        case RepeatMode::all: repeatModeBox.setSelectedId (2, juce::dontSendNotification); break;
        case RepeatMode::one: repeatModeBox.setSelectedId (3, juce::dontSendNotification); break;
        case RepeatMode::off: repeatModeBox.setSelectedId (1, juce::dontSendNotification); break;
    }

    switch (state.outputLayout)
    {
        case OutputLayout::fiveOne: outputLayoutBox.setSelectedId (2, juce::dontSendNotification); break;
        case OutputLayout::fiveOneTwo: outputLayoutBox.setSelectedId (3, juce::dontSendNotification); break;
        case OutputLayout::sevenOneTwo: outputLayoutBox.setSelectedId (4, juce::dontSendNotification); break;
        case OutputLayout::stereo: outputLayoutBox.setSelectedId (1, juce::dontSendNotification); break;
    }

    if (state.selectedIndex >= 0)
    {
        nowPlayingLabel.setText (state.selectedTrackTitle, juce::dontSendNotification);

        if (const auto* track = controller.getPlaylist().getSelectedTrack())
        {
            const auto sampleRateText = track->sampleRate > 0.0
                                            ? juce::String (track->sampleRate / 1000.0, 1) + " kHz"
                                            : juce::String ("-- kHz");
            const auto format = track->formatName.isNotEmpty()
                                    ? track->formatName.toUpperCase() : "AUDIO";
            const auto bitDepth = track->bitDepth > 0
                                      ? juce::String (track->bitDepth) + "-BIT  /  "
                                      : juce::String{};
            const auto technical = format + "  /  " + bitDepth + sampleRateText
                + "  /  " + juce::String (track->channelCount)
                + (track->channelCount == 1 ? " CHANNEL" : " CHANNELS");
            const auto workByline = track->artist.isNotEmpty() || track->album.isNotEmpty()
                ? (track->artist.isNotEmpty() ? track->artist : "Unknown Artist")
                    + (track->album.isNotEmpty() ? "  /  " + track->album : juce::String{})
                : track->file.getParentDirectory().getFileName();
            trackMetaLabel.setText (workspaceMode == WorkspaceMode::studio
                                        ? (state.isPlaying ? "PLAYING  /  " : "READY  /  ") + technical
                                        : workByline,
                                    juce::dontSendNotification);
            artworkMetaLabel.setText (technical.toUpperCase(), juce::dontSendNotification);
            albumArtwork.setTrack (track->id, track->title,
                                   juce::File (state.selectedTrackArtworkPath));
        }
    }
    else
    {
        nowPlayingLabel.setText ("No track loaded", juce::dontSendNotification);
        trackMetaLabel.setText ("Use + Music to add audio and begin", juce::dontSendNotification);
        artworkMetaLabel.setText ("LOCAL AUDIO  /  VST3 + AU", juce::dontSendNotification);
        albumArtwork.setTrack ({}, {});
    }

    if (libraryFilter.query().isEmpty())
        queueCountLabel.setText (workCountText (state.playlistSize),
                                 juce::dontSendNotification);

    const auto gain = static_cast<float> (state.volume);
    const auto decibels = gain > 0.00001f ? juce::Decibels::gainToDecibels (gain) : -100.0f;
    volumeValueLabel.setText (gain > 0.00001f ? juce::String (decibels, 1) + " dB" : "-inf dB",
                              juce::dontSendNotification);

    const auto& chain = controller.getEffectChain();
    juce::StringArray activeEffects;
    for (const auto& slot : chain.getSlots())
        if (slot.available && ! slot.bypassed)
            activeEffects.add (slot.pluginName);
    auto chainSummary = activeEffects.isEmpty() ? juce::String ("No active effects")
                                                 : activeEffects.joinIntoString ("  /  ");
    if (chainSummary.length() > 46)
        chainSummary = chainSummary.substring (0, 43) + "...";
    effectsSummaryLabel.setText ("LISTENING CHAIN\n" + chainSummary, juce::dontSendNotification);
    effectsButton.setButtonText ("Effects  " + juce::String (chain.size()));

    const auto deviceName = state.audioDeviceName.isNotEmpty() ? state.audioDeviceName : "No audio device";
    deviceStatusLabel.setText (deviceName + "  /  " + juce::String (state.availableOutputChannels) + " OUT",
                               juce::dontSendNotification);
    outputStage.setState (state.outputLayout, workspaceMode, state.isPlaying, state.selectedTrackTitle,
                          deviceName, state.availableOutputChannels, state.outputPeakLevels);

    const auto hasSelection = playlistTable.getSelectedRow() >= 0;
    removeTrackButton.setEnabled (hasSelection);
    clearPlaylistButton.setEnabled (state.playlistSize > 0);
    moveTrackUpButton.setEnabled (hasSelection && libraryFilter.query().isEmpty()
                                  && state.selectedIndex > 0);
    moveTrackDownButton.setEnabled (hasSelection && libraryFilter.query().isEmpty()
                                    && state.selectedIndex < state.playlistSize - 1);
    playPauseButton.setEnabled (state.selectedIndex >= 0 && state.selectedTrackAvailable);
    previousButton.setEnabled (state.selectedIndex >= 0);
    nextButton.setEnabled (state.selectedIndex >= 0);
}

void MainComponent::handleRemoteTransportCommand (
    const RemoteTransportCommand& command)
{
    const auto before = controller.getViewState();
    switch (command.type)
    {
        case RemoteTransportCommandType::play:
            if (! before.isPlaying)
                controller.playOrPause();
            break;
        case RemoteTransportCommandType::pause:
            if (before.isPlaying)
                controller.playOrPause();
            break;
        case RemoteTransportCommandType::togglePlayPause:
            controller.playOrPause();
            break;
        case RemoteTransportCommandType::next:
            controller.nextTrack();
            refreshPlaylist();
            break;
        case RemoteTransportCommandType::previous:
            controller.previousTrack();
            refreshPlaylist();
            break;
        case RemoteTransportCommandType::seekToSeconds:
            controller.seekToSeconds (command.positionSeconds);
            break;
    }

    statusLabel.setText (controller.getLastStatusMessage(),
                         juce::dontSendNotification);
    updateTransportState();
}

void MainComponent::openPluginEditor (int effectIndex)
{
    const auto* slot = controller.getEffectChain().getSlot (effectIndex);
    if (slot == nullptr || ! slot->available)
    {
        statusLabel.setText (slot != nullptr ? slot->errorMessage : "No effect selected.",
                             juce::dontSendNotification);
        return;
    }

    for (auto& window : pluginEditorWindows)
    {
        if (window->ownsEffect (slot->id))
        {
            window->showWindow();
            return;
        }
    }

    auto* plugin = controller.getEffectPluginForEditor (effectIndex);

    if (plugin == nullptr)
    {
        statusLabel.setText ("No effect selected.", juce::dontSendNotification);
        return;
    }

    auto* editor = plugin->getActiveEditor();
    if (editor == nullptr)
        editor = plugin->createEditorIfNeeded();

    if (editor == nullptr)
    {
        statusLabel.setText ("This plugin has no editor.", juce::dontSendNotification);
        return;
    }

    pluginEditorWindows.push_back (std::make_unique<PluginEditorWindow> (slot->id, plugin->getName(), editor));
}

void MainComponent::removeEffect (int effectIndex)
{
    closePluginEditors();

    if (! controller.removeEffectAtIndex (effectIndex))
        statusLabel.setText ("Could not remove effect.", juce::dontSendNotification);

    refreshEffects();
}

void MainComponent::moveEffectFromDrag (int fromIndex, int yInRackContent)
{
    const auto toIndex = calculateEffectDropIndex (yInRackContent,
                                                   effectRowHeight,
                                                   effectRowGap,
                                                   controller.getEffectChain().size());
    if (toIndex < 0 || toIndex == fromIndex)
        return;

    closePluginEditors();

    if (! controller.moveEffect (fromIndex, toIndex))
    {
        statusLabel.setText ("Could not reorder effect.", juce::dontSendNotification);
        return;
    }

    statusLabel.setText ("Effect reordered.", juce::dontSendNotification);
    refreshEffects();
}

void MainComponent::closePluginEditors()
{
    pluginEditorWindows.clear();
}

juce::File MainComponent::getStateDirectory() const
{
    return ProductPaths::applicationDataRoot();
}

juce::File MainComponent::getStateFile() const
{
    return getStateDirectory().getChildFile ("state.xml");
}

juce::File MainComponent::getPluginListFile() const
{
    return getStateDirectory().getChildFile ("plugins.xml");
}
