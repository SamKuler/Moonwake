#include "MainComponent.h"

#include "EffectRackLayout.h"
#include "EffectScopeMenu.h"
#include "app/ProductIdentity.h"
#include "app/ProductPaths.h"
#include "playlist/PlaylistFile.h"

#include <algorithm>

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
    return juce::String (count) + (count == 1 ? " track" : " tracks");
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
    EffectSlotRow (MainComponent& ownerToUse, int slotIdToUse)
        : owner (ownerToUse),
          slotId (slotIdToUse)
    {
        setRepaintsOnMouseActivity (true);
        nameLabel.setJustificationType (juce::Justification::centredLeft);
        nameLabel.setInterceptsMouseClicks (false, false);

        bypassButton.setClickingTogglesState (true);
        editorButton.setTooltip ("Open plugin editor");
        removeButton.setTooltip ("Remove effect");

        bypassButton.onClick = [this]
        {
            owner.controller.setEffectBypassedAtIndex (index, bypassButton.getToggleState());
            owner.refreshEffects();
        };
        editorButton.onClick = [this] { owner.openPluginEditor (index); };
        removeButton.onClick = [this] { owner.removeEffect (index); };
        scopeButton.setTooltip ("Choose the channels processed by this effect");
        scopeButton.onClick = [this, id = slotIdToUse]
        {
            auto choices = EffectScopeMenu (toAudioChannelSet (owner.controller.getViewState().outputLayout));
            choices.menu.setLookAndFeel (&owner.skinAdapter);
            choices.menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scopeButton),
                [safe = juce::Component::SafePointer<MainComponent> (&owner), id, scopes = std::move (choices.scopes)] (int selected)
                {
                    if (! safe || ! juce::isPositiveAndBelow (selected - 1, static_cast<int> (scopes.size()))) return;
                    const auto& chain = safe->controller.getEffectChain();
                    for (int row = 0; row < chain.size(); ++row)
                        if (chain.getSlot (row)->id == id)
                        {
                            if (! safe->controller.setEffectScopeAtIndex (row, scopes[static_cast<size_t> (selected - 1)]))
                                safe->showStatus (safe->controller.getLastStatusMessage(), true);
                            safe->refreshEffects();
                            break;
                        }
                });
        };
        scopeButton.setSkinPainter ([this] (juce::Graphics& graphics, bool over, bool down)
        {
            return drawSlot (graphics, scopeButton, "slot.scope", SkinRenderTarget::textButton,
                             scopeButton.getButtonText(), {}, over, down, false);
        });

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
                             bypassButton.getToggleState() ? "Bypassed" : "Active", "bypass",
                             over, down, bypassButton.getToggleState());
        });
        editorButton.setSkinPainter ([this] (juce::Graphics& graphics,
                                             bool over, bool down)
        {
            return drawSlot (graphics, editorButton, "slot.editor",
                             SkinRenderTarget::textButton, "Edit", "editor",
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
        addAndMakeVisible (scopeButton);
    }

    [[nodiscard]] int getSlotId() const noexcept { return slotId; }

    void updateState (int indexToUse, int count, const EffectSlotState& slot)
    {
        index = indexToUse;
        slotCount = count;
        bypassed = slot.bypassed;
        hasError = ! slot.available || slot.errorMessage.isNotEmpty();
        slotName = slot.pluginName;
        const auto formatSuffix = slot.pluginFormat.isNotEmpty() ? "  /  " + slot.pluginFormat : juce::String{};
        nameLabel.setText (slot.pluginName + formatSuffix + (slot.available ? juce::String{} : "  /  Unavailable"),
                           juce::dontSendNotification);
        nameLabel.setTooltip (! slot.available && slot.errorMessage.isNotEmpty()
                                  ? slot.errorMessage : nameLabel.getText());
        bypassButton.setTooltip (slot.bypassed ? "Enable effect" : "Bypass effect");
        bypassButton.setToggleState (slot.bypassed, juce::dontSendNotification);
        bypassButton.setEnabled (slot.available);
        editorButton.setEnabled (slot.available);
        scopeButton.setButtonText (::effectScopeLabel (slot.scope));
        scopeButton.setEnabled (slot.available);
        repaint();
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
        repaint();
        owner.moveEffectFromDrag (index, juce::roundToInt (rackEvent.position.y));
    }

    void paint (juce::Graphics& g) override
    {
        const auto frame = [&] (const juce::String& slot)
        {
            const auto found = owner.effectRowLayout.slots.find (slot);
            return found != owner.effectRowLayout.slots.end() ? found->second : juce::Rectangle<float>{};
        };
        auto bounds = frame ("slot.surface");
        if (! bounds.isEmpty())
            static_cast<void> (drawSlot (g, *this, "slot.surface", SkinRenderTarget::surface,
                {}, {}, isMouseOver (false), isMouseButtonDown(), false, bounds));

        const auto indicator = frame ("slot.dragIndicator");
        if (! indicator.isEmpty()) static_cast<void> (drawSlot (
            g, *this, "slot.dragIndicator", SkinRenderTarget::surface,
            {}, "drag", isMouseOver (false), isMouseButtonDown(),
            false, indicator));
    }

    void resized() override
    {
        const auto place = [&] (juce::Component& component, const juce::String& slot)
        {
            const auto found = owner.effectRowLayout.slots.find (slot);
            component.setBounds (found != owner.effectRowLayout.slots.end()
                ? found->second.getSmallestIntegerContainer() : juce::Rectangle<int>{});
        };
        place (nameLabel, "slot.name");
        place (scopeButton, "slot.scope");
        place (bypassButton, "slot.bypass");
        place (editorButton, "slot.editor");
        place (removeButton, "slot.remove");
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
        context.state.focused = JuceSkinAdapter::hasVisibleKeyboardFocus (control);
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
    const int slotId;
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
    RackIconButton scopeButton { "Channels" };
};

MainComponent::MainComponent()
    : nowPlayingCoordinator (createSystemNowPlayingBridge()),
      remoteCommandLifetime (std::make_shared<std::atomic_bool> (true)),
      stateRestoreService (ProductPaths::artworkCacheRoot()),
      libraryImportService (ProductPaths::artworkCacheRoot()),
      musicDirectoryScanService (ProductPaths::artworkCacheRoot()),
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
                      required = PlayerComponentCatalog::isRequired (descriptor->id, activeSkinLayoutEntry);
                      break;
                  }
          return JuceSkinAdapter::RuntimeBinding {
              skin->resolveStyle (target, style == "default"
                  ? skinLayoutHost.ancillaryStyleFor (target) : style), &session->animations(),
              session.get(), required
          };
      }, skinRenderer),
      audioSettingsPanel (controller, &skinAdapter),
      pluginSettingsPanel (controller, &skinAdapter),
      musicDirectoriesPanel (skinAdapter),
      skinLibraryPanel (skinManager, &skinAdapter),
      musicLibraryPanel (controller, skinAdapter),
      skinLayoutHost (skinManager, skinBindings)
{
    workspaceMode = controller.getViewState().workspaceMode;
    SkinValidationReport skinReport;
    const auto skinReady = skinManager.initialise (
        ProductPaths::installedSkinsRoot(), skinReport);
    setLookAndFeel (&skinAdapter);
    tooltipWindow.setOpaque (false);
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

        appliedSkinSession = skinManager.activeSession();
        skinManager.addListener (this);
        if (skinReport.hasErrors())
            skinFallbackDiagnostic.setMessage (skinReport.toText());
    }
    else
    {
        skinFallbackDiagnostic.setMessage (skinReport.toText());
    }

    appEyebrowLabel.setText ("Music for macOS", juce::dontSendNotification);
    appEyebrowLabel.setJustificationType (juce::Justification::centredLeft);

    titleLabel.setText (MoonwakeIdentity::productName,
                        juce::dontSendNotification);
    titleLabel.setTitle (MoonwakeIdentity::productName);
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    nowPlayingEyebrowLabel.setText ("Now playing", juce::dontSendNotification);
    nowPlayingEyebrowLabel.setJustificationType (juce::Justification::centredLeft);

    queueTitleLabel.setText ("Up next", juce::dontSendNotification);
    queueTitleLabel.setJustificationType (juce::Justification::centredLeft);
    queueCountLabel.setJustificationType (juce::Justification::centredRight);
    emptyQueueLabel.setText ("No matching tracks\nDrop music or folders here, or use + Music",
                             juce::dontSendNotification);
    emptyQueueLabel.setJustificationType (juce::Justification::centred);

    effectRackTitleLabel.setText ("Effects", juce::dontSendNotification);
    effectRackTitleLabel.setJustificationType (juce::Justification::centredLeft);
    emptyRackLabel.setText ("No effects yet\nChoose a plug-in above to add it",
                            juce::dontSendNotification);
    emptyRackLabel.setJustificationType (juce::Justification::centred);

    settingsTitleLabel.setJustificationType (juce::Justification::centredLeft);

    statusLabel.setName ("Notification");
    statusLabel.addMouseListener (this, false);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    nowPlayingLabel.setText ("No track loaded", juce::dontSendNotification);
    nowPlayingLabel.setJustificationType (juce::Justification::centredLeft);
    trackMetaLabel.setText ("Select a track to begin", juce::dontSendNotification);
    trackMetaLabel.setJustificationType (juce::Justification::centredLeft);
    artworkMetaLabel.setJustificationType (juce::Justification::centred);
    effectsSummaryLabel.setJustificationType (juce::Justification::centredLeft);
    deviceStatusLabel.setJustificationType (juce::Justification::centredRight);
    volumeLabel.setText ("Volume", juce::dontSendNotification);
    volumeSlider.setName ("Volume");
    volumeSlider.setTooltip ("Output volume");
    seekSlider.setName ("Playback position");
    seekSlider.setTooltip ("Seek within the current track");
    volumeLabel.setJustificationType (juce::Justification::centredRight);
    volumeValueLabel.setJustificationType (juce::Justification::centredRight);
    timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
    timeLabel.setJustificationType (juce::Justification::centred);

    repeatModeBox.addItem ("Repeat off", 1);
    repeatModeBox.addItem ("Repeat all", 2);
    repeatModeBox.addItem ("Repeat one", 3);
    repeatModeBox.setSelectedId (1, juce::dontSendNotification);

    for (const auto& layout : outputLayouts())
        outputLayoutBox.addItem (juce::String (layout.name) + " / " + juce::String (layout.channels.size()) + " ch",
                                 static_cast<int> (layout.layout) + 1);
    outputLayoutBox.setSelectedId (static_cast<int> (OutputLayout::stereo) + 1,
                                   juce::dontSendNotification);

    playlistTable.setModel (this);
    playlistTable.setOutlineThickness (0);
    playlistTable.setRowHeight (32);
    playlistTable.setHeaderHeight (24);
    playlistTable.getHeader().addColumn ("Track", 1, 240, 80, -1, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::sortable);
    playlistTable.getHeader().addColumn ("Artist / album", 2, 360, 100, -1, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::sortable);
    playlistTable.getHeader().addColumn ("Time", 3, 90, 40, 120, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::sortable);
    playlistTable.getHeader().addColumn ("Album", 4, 240, 80, -1, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::sortable);
    skinAdapter.configureListBox (playlistTable);

    librarySearchEditor.setName ("Search works");
    librarySearchEditor.setTitle ("Search title, artist, album, format, or channel count");
    librarySearchEditor.setTextToShowWhenEmpty (
        "Search music",
        skinAdapter.findColour (juce::TextEditor::textColourId).withAlpha (0.58f));
    librarySearchEditor.onTextChange = [this]
    {
        if (musicLibraryPanel.isActive())
        {
            musicLibraryPanel.setQuery (librarySearchEditor.getText());
            updateTransportState();
            return;
        }
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
    pluginFilters.search.setTextToShowWhenEmpty (
        "Search plug-ins",
        skinAdapter.findColour (juce::TextEditor::textColourId));
    pluginCombo.setTextWhenNothingSelected ("No matching effects");
    effectScopeLabel.setText ("Add to", juce::dontSendNotification);
    effectScopeBox.setTitle ("Channels for the next effect");
    meterModeBox.addItem ("Peak", 1);
    meterModeBox.addItem ("Peak + RMS", 2);
    meterModeBox.addItem ("Loudness", 3);
    meterModeBox.addItem ("Mixer", 4);
    meterModeBox.setSelectedId (2, juce::dontSendNotification);
    meterModeBox.setTitle ("Output meter mode");
    resetMeterButton.setTooltip ("Start a new loudness measurement");
    meterModeBox.onChange = [this]
    {
        updateOutputView();
    };
    resetMeterButton.onClick = [this]
    {
        if (outputMixer.isVisible()) controller.resetOutputChannelMix();
        else controller.resetOutputMeter();
        updateTransportState();
    };
    outputMixer.onChange = [this] (auto channel, float gain, bool muted, bool solo)
    {
        if (! controller.setOutputChannelMix (channel, gain, muted, solo))
            showStatus ("Unable to update output channel", true);
        updateTransportState();
    };
    refreshEffectScopes();
    effectRackViewport.setViewedComponent (&effectRackContent, false);
    effectRackViewport.setScrollBarsShown (true, false);

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setWantsKeyboardFocus (true);

    seekSlider.setRange (0.0, 1.0, 0.001);
    seekSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    seekSlider.setWantsKeyboardFocus (true);

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
    effectsButton.setTooltip ("Open the effects rack");
    effectsCloseButton.setTooltip ("Close the effects rack");
    settingsCloseButton.setTooltip ("Close settings");
    addEffectButton.setTooltip ("Add the selected effect");
    clearEffectTailsButton.setTitle ("Clear all effect tails");
    clearEffectTailsButton.setTooltip ("Clear delay and reverb buffers without changing plug-in settings");
    clearEffectTailsButton.onClick = [this]
    {
        if (! controller.clearEffectTails()) showStatus (controller.getLastStatusMessage(), true);
        updateTransportState();
    };
    bypassAllEffectsButton.setTitle ("Bypass all effects");
    bypassAllEffectsButton.setClickingTogglesState (true);
    bypassAllEffectsButton.onClick = [this]
    {
        controller.setAllEffectsBypassed (bypassAllEffectsButton.getToggleState());
        updateTransportState();
    };
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
                                    &pluginCombo,
                                    &effectScopeBox,
                                    &effectScopeLabel,
                                    &meterModeBox,
                                    &resetMeterButton,
                                    &addEffectButton,
                                    &clearEffectTailsButton,
                                    &bypassAllEffectsButton,
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
                                    &repeatButton,
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
                                    &outputMixer,
                                    &skinFallbackDiagnostic };

    for (auto* child : children)
        addAndMakeVisible (child);
    statusLabel.setVisible (false);
    skinFallbackDiagnostic.setVisible (! skinReady);

    pluginFilters.addTo (*this);
    audioSettingsPanel.addTo (*this);
    pluginSettingsPanel.addTo (*this);
    musicDirectoriesPanel.addTo (*this);
    musicDirectoriesPanel.onAdd = [this] { chooseMusicDirectory(); };
    musicDirectoriesPanel.onScan = [this] { requestMusicDirectoryScan (false); };
    musicDirectoriesPanel.onCancel = [this]
    {
        pendingMusicDirectoryScan.reset();
        musicDirectoryScanService.cancel();
        nextDirectoryScanAt = juce::Time::getMillisecondCounterHiRes() / 1000.0 + 60.0;
    };
    musicDirectoriesPanel.onRemove = [this] (const auto& id)
    {
        if (controller.removeMusicDirectory (id)) musicDirectoriesChanged (false);
    };
    musicDirectoriesPanel.onAutomaticChanged = [this] (const auto& id, bool enabled)
    {
        if (controller.setMusicDirectoryAutomatic (id, enabled)) musicDirectoriesChanged (enabled);
    };
    smartPlaylistEditor = std::make_unique<SmartPlaylistEditor> (skinAdapter);
    skinLibraryPanel.addTo (*this);
    musicLibraryPanel.addTo (*this);
    musicLibraryPanel.onChanged = [this] { refreshPlaylist(); updateTransportState(); };
    musicLibraryPanel.onPageChanged = [this]
    {
        if (! queueVisibilityChosen) queueOpen = musicLibraryPanel.page() == MusicLibraryPanel::Page::player;
        closeDrawers();
        applyWorkspaceVisuals();
        resized();
        updateTransportState();
    };
    musicLibraryPanel.onStatus = [this] (const juce::String& message, bool error) { showStatus (message, error); };
    musicLibraryPanel.onImportPlaylist = [this] { choosePlaylistImport(); };
    musicLibraryPanel.onExportPlaylist = [this] (const auto& tracks) { choosePlaylistExport (tracks); };
    musicLibraryPanel.onRelocateTrack = [this] (const auto& id) { chooseTrackRelocation (id); };
    musicLibraryPanel.onChoosePlaylistArtwork = [this] (const auto& id) { choosePlaylistArtwork (id); };
    musicLibraryPanel.onShowQueue = [this] { setQueueOpen (true); };
    musicLibraryPanel.onEditSmartPlaylist = [this] (const auto& id) { showSmartPlaylistEditor (id); };
    addAndMakeVisible (queueToggleButton);
    addAndMakeVisible (queueActionsButton);
    queueActionsButton.onClick = [this] { musicLibraryPanel.showQueueActions (queueActionsButton); };
    queueToggleButton.onClick = [this] { setQueueOpen (! queueOpen); };

    const auto reportStatus = [this] (const juce::String& message, bool isError)
    {
        showStatus (message, isError);
        updateTransportState();
    };
    audioSettingsPanel.setStatusCallback ([this, reportStatus] (const juce::String& message, bool isError)
    {
        refreshEffectScopes();
        refreshEffects();
        if (isError) reportStatus (message, true);
        else updateTransportState();
    });
    pluginSettingsPanel.setStatusCallback (reportStatus);
    pluginSettingsPanel.setScanFolderCallback ([this] { choosePluginDirectory(); });
    skinLibraryPanel.setStatusCallback ([this] (const juce::String& message, bool isError)
    {
        if (isError) showStatus (message, true);
    });
    skinLibraryPanel.setImportCallback ([this] { chooseSkinArchive(); });
    skinLibraryPanel.setChooseDeveloperDirectoryCallback (
        [this] { chooseDeveloperSkinDirectory(); });

    addFilesButton.onClick = [this]
    {
        if (libraryImportService.isRunning())
        {
            addFilesButton.setEnabled (false);
            showStatus ("Cancelling music import...");
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
        const auto* selected = queueEntryAtRow (playlistTable.getSelectedRow());
        if (selected == nullptr) return;
        const auto id = selected->track.id;
        if (! controller.removeUpcomingQueueEntries ({ id }))
            showStatus ("Select a track to remove.", true);

        refreshPlaylist();
        updateTransportState();
    };
    clearPlaylistButton.onClick = [this]
    {
        controller.clearPlaylist();
        refreshPlaylist();
        updateTransportState();
    };
    moveTrackUpButton.onClick = [this]
    {
        if (! canMoveQueueSelection (-1)) return;
        if (const auto* entry = queueEntryAtRow (playlistTable.getSelectedRow()))
            controller.moveUpcomingQueueEntry (entry->track.id, -1);
        refreshPlaylist();
    };
    moveTrackDownButton.onClick = [this]
    {
        if (! canMoveQueueSelection (1)) return;
        if (const auto* entry = queueEntryAtRow (playlistTable.getSelectedRow()))
            controller.moveUpcomingQueueEntry (entry->track.id, 1);
        refreshPlaylist();
    };
    addEffectButton.onClick = [this]
    {
        if (pluginInsertionInProgress)
            return;

        pluginInsertionInProgress = true;
        addEffectButton.setEnabled (false);
        showStatus ("Loading plug-in...");
        controller.addPluginByIdentifierAsync (
            catalogPluginIdentifiers[pluginCombo.getSelectedId() - 1],
            [safeThis = juce::Component::SafePointer<MainComponent> (this)]
            (bool inserted, const juce::String& message)
            {
                if (safeThis == nullptr)
                    return;

                safeThis->pluginInsertionInProgress = false;
                safeThis->addEffectButton.setEnabled (safeThis->pluginCombo.getSelectedId() > 0);
                safeThis->showStatus (inserted ? juce::String{} : message, ! inserted);
                safeThis->refreshEffects();
            }, effectScopeBox.isVisible()
                   ? effectScopeChoices[static_cast<size_t> (effectScopeBox.getSelectedId() - 1)]
                   : EffectScope {});
    };
    pluginFilters.onChange = [this] { refreshPlugins(); };
    pluginSettingsPanel.onCatalogChanged = [this] { refreshPlugins(); };
    playPauseButton.onClick = [this]
    {
        if (! performPlayCommand (true))
            showStatus (controller.getLastStatusMessage(), true);
        updateTransportState();
    };
    nextButton.onClick = [this]
    {
        if (! controller.nextTrack())
            showStatus (controller.getLastStatusMessage(), true);
        refreshPlaylist();
        updateTransportState();
    };
    previousButton.onClick = [this]
    {
        if (! controller.previousTrack())
            showStatus (controller.getLastStatusMessage(), true);
        refreshPlaylist();
        updateTransportState();
    };
    shuffleButton.onClick = [this]
    {
        controller.setShuffleEnabled (shuffleButton.getToggleState());
        refreshPlaylist();
        updateTransportState();
    };
    repeatButton.onClick = [this]
    {
        const auto mode = controller.getViewState().repeatMode;
        controller.setRepeatMode (mode == RepeatMode::off ? RepeatMode::all
                                  : mode == RepeatMode::all ? RepeatMode::one : RepeatMode::off);
        refreshPlaylist();
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
        refreshPlaylist();
        updateTransportState();
    };
    outputLayoutBox.onChange = [this]
    {
        const auto error = controller.setOutputLayout (static_cast<OutputLayout> (outputLayoutBox.getSelectedId() - 1));

        if (error.isNotEmpty())
        {
            showStatus (error, true);
            outputLayoutBox.setSelectedId (static_cast<int> (controller.getViewState().outputLayout) + 1,
                                           juce::dontSendNotification);
        }
        audioSettingsPanel.refresh();
        refreshEffectScopes();
        refreshEffects();
        updateTransportState();
    };
    volumeSlider.onValueChange = [this]
    {
        controller.setVolume (volumeSlider.getValue());
    };
    const auto commitSeek = [this]
    {
        if (! controller.seekToRatio (seekSlider.getValue()))
            showStatus (controller.getLastStatusMessage(), true);
        updateTransportState();
    };
    seekSlider.onDragStart = [this] { seekGestureInProgress = true; };
    seekSlider.onDragEnd = [this, commitSeek]
    {
        seekGestureInProgress = false;
        commitSeek();
    };
    seekSlider.onValueChange = [this, commitSeek]
    {
        if (! seekGestureInProgress) commitSeek();
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
    juce::StringArray startupErrors;
    if (controller.getAudioInitialisationError().isNotEmpty())
        startupErrors.add ("Audio device error: " + controller.getAudioInitialisationError());
    if (skinReport.hasErrors())
        startupErrors.add (skinReport.toText());
    if (! startupErrors.isEmpty())
        showStatus (startupErrors.joinIntoString (" "), true);
    startTimer (1, 200);
    startTimer (2, 33);
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
                                      &pluginFilters.search, &pluginCombo, &addEffectButton,
                                      &clearEffectTailsButton, &bypassAllEffectsButton,
                                      &effectRackViewport };
    int focusIndex = 1;
    for (auto* component : focusOrder)
        component->setExplicitFocusOrder (focusIndex++);

    setSize (1240, 800);
    setRestoreControlsEnabled (false);

    const juce::Component::SafePointer<MainComponent> safeThis (this);
    if (! stateRestoreService.start (getStateFile(), getPluginListFile(),
            [safeThis] (StateRestoreResult result) mutable
            {
                if (safeThis != nullptr)
                    safeThis->finishStateRestore (std::move (result));
            }))
    {
        restoreStatus = RestoreStatus::failed;
        setRestoreControlsEnabled (true);
        updateTransportState();
        showStatus ("Could not start restoring the saved library. Saved data was preserved.", true);
    }
}

MainComponent::~MainComponent()
{
    stopTimer (1);
    stopTimer (2);
    stateRestoreService.cancel();
    remoteCommandLifetime->store (false, std::memory_order_release);
    nowPlayingCoordinator.setCommandHandler ({});
    nowPlayingCoordinator.clear();
    libraryImportService.cancel();
    musicDirectoryScanService.cancel();
    smartPlaylistEditor.reset();
    static_cast<void> (stateSaveService.waitForPendingWrite());
    controller.checkpointPlaybackPosition();
    skinManager.removeListener (this);
    skinLayoutHost.clear();
    updateSkinPersistenceStateFromManager();
    if (restoreStatus == RestoreStatus::ready
        && ! controller.saveState (getStateFile(), getPluginListFile()))
        juce::Logger::writeToLog (
            "Moonwake shutdown save failed: "
            + controller.getLastStatusMessage());
    setLookAndFeel (nullptr);
}

void MainComponent::setRestoreControlsEnabled (bool enabled)
{
    musicLibraryPanel.setEnabled (enabled);
    for (auto* component : std::initializer_list<juce::Component*> {
             &studioModeButton, &listeningModeButton, &settingsButton,
             &effectsButton, &volumeSlider,
             &shuffleButton, &repeatModeBox, &repeatButton, &outputLayoutBox })
        component->setEnabled (enabled);
}

void MainComponent::finishStateRestore (StateRestoreResult result)
{
    const auto restoredState = controller.applyPreparedState (std::move (result));
    restoreStatus = restoredState ? RestoreStatus::ready : RestoreStatus::failed;
    setRestoreControlsEnabled (true);
    SkinValidationReport skinReport;
    if (restoredState && skinManager.activeSession() != nullptr)
    {
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
    }
    workspaceMode = controller.getViewState().workspaceMode;
    librarySearchEditor.setText (controller.getLibrarySearchQuery(), false);
    libraryFilter.setQuery (librarySearchEditor.getText());
    refreshPlaylist();
    refreshPlugins();
    refreshEffectScopes();
    refreshEffects();
    audioSettingsPanel.refresh();
    pluginSettingsPanel.refreshInventory();
    musicDirectoriesPanel.refresh (controller.getMusicDirectories());
    skinLibraryPanel.refresh();
    applyWorkspaceVisuals();
    resized();
    updateTransportState();
    if (! restoredState)
        showStatus (controller.getLastStatusMessage()
                    + " Changes will not be saved until the saved session can be restored.", true);
    else if (! controller.getRestoreDiagnostics().isEmpty() || skinReport.hasErrors())
        showStatus (controller.getRestoreDiagnostics().joinIntoString (" ")
                    + (skinReport.hasErrors() ? " " + skinReport.toText() : juce::String{}), true);
    startLibraryImport ({});
    dispatchQueuedTransportCommands();
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
        { "queue.toggle", &queueToggleButton },
        { "queue.actions", &queueActionsButton },
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
        { "transport.repeatCycle", &repeatButton },
        { "transport.volumeLabel", &volumeLabel },
        { "transport.volumeValue", &volumeValueLabel },
        { "transport.volume", &volumeSlider },
        { "effects.open", &effectsButton },
        { "effects.close", &effectsCloseButton },
        { "effects.summary", &effectsSummaryLabel },
        { "effects.title", &effectRackTitleLabel },
        { "effects.filterLabel", &pluginFilterLabel },
        { "effects.catalog", &pluginCombo },
        { "effects.scope", &effectScopeBox },
        { "effects.scopeLabel", &effectScopeLabel },
        { "effects.add", &addEffectButton },
        { "effects.clearTails", &clearEffectTailsButton },
        { "effects.bypassAll", &bypassAllEffectsButton },
        { "effects.chain", &effectRackViewport },
        { "effects.empty", &emptyRackLabel },
        { "effects.outputLayout", &outputLayoutBox },
        { "output.stage", &outputStage },
        { "output.mixer", &outputMixer },
        { "output.mode", &meterModeBox },
        { "output.reset", &resetMeterButton },
        { "settings.title", &settingsTitleLabel },
        { "settings.close", &settingsCloseButton }
    };
    for (const auto& [id, component] : publicComponents)
        static_cast<void> (skinBindings.bind (id, *component, report));
    pluginFilters.bind (skinBindings, "effects", report);
    audioSettingsPanel.bind (skinBindings, report);
    pluginSettingsPanel.bind (skinBindings, report);
    musicDirectoriesPanel.bind (skinBindings, report);
    skinLibraryPanel.bind (skinBindings, report);
    musicLibraryPanel.bind (skinBindings, report);
    if (report.hasErrors())
        showStatus (report.toText(), true);
    jassert (! report.hasErrors());
}

void MainComponent::skinChanged()
{
    skinLibraryPanel.refresh();
    resized();
    if (smartPlaylistEditor && smartPlaylistEditor->isVisible()) smartPlaylistEditor->resized();
    repaint();
}

void MainComponent::resized()
{
    musicLibraryPanel.finishEditing();
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
    auto requestedEntry = settingsDrawerOpen
                                  ? SkinLayoutEntry::settings
                                  : (effectsDrawerOpen
                                         ? SkinLayoutEntry::effects
                                         : (workspaceMode == WorkspaceMode::studio
                                                ? SkinLayoutEntry::studio
                                                : SkinLayoutEntry::listening));
    const auto& layouts = skinManager.activeSession()->compiledSkin()->layouts;
    const auto libraryPages = layouts.contains (SkinLayoutEntry::library)
                           && layouts.contains (SkinLayoutEntry::collection);
    if (! settingsDrawerOpen && ! effectsDrawerOpen && libraryPages)
    {
        if (musicLibraryPanel.page() == MusicLibraryPanel::Page::overview) requestedEntry = SkinLayoutEntry::library;
        if (musicLibraryPanel.page() == MusicLibraryPanel::Page::collection) requestedEntry = SkinLayoutEntry::collection;
    }
    if (requestedEntry == SkinLayoutEntry::collection)
        librarySearchEditor.setText (controller.getLibrarySearchQuery(), false);
    else if (! libraryPages)
        librarySearchEditor.setText (libraryFilter.query(), false);
    SkinValidationReport report;
    if (skinLayoutHost.apply (requestedEntry, getLocalBounds(),
                              1.0f, report, workspaceMode, { queueOpen }))
    {
        activeSkinLayoutEntry = requestedEntry;
        const auto sessionChanged = appliedSkinSession != skinManager.activeSession();
        appliedSkinSession = skinManager.activeSession();
        if (sessionChanged)
            for (const auto& id : skinBindings.ids())
                if (auto* editor = dynamic_cast<juce::TextEditor*> (skinBindings.find (id)))
                    skinAdapter.configureTextEditor (*editor);
        for (const auto& id : skinBindings.ids())
            if (auto* component = skinBindings.find (id))
            {
                component->sendLookAndFeelChange();
                component->resized();
            }
        if (pluginCombo.isVisible()) pluginFilters.clearHiddenFilters();
        if (requestedEntry == SkinLayoutEntry::settings)
        {
            pluginSettingsPanel.filters.clearHiddenFilters();
            pluginSettingsPanel.refreshScanState();
        }
        hasAppliedSkinLayout = true;
        if (! libraryPages && (requestedEntry == SkinLayoutEntry::studio || requestedEntry == SkinLayoutEntry::listening)
            && ! librarySearchEditor.isVisible() && librarySearchEditor.getText().isNotEmpty())
            librarySearchEditor.clear();
    }
    else
    {
        if (hasAppliedSkinLayout)
        {
            showStatus (report.toText(), true);
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
    statusLabel.setVisible (statusVisibleUntil > juce::Time::getMillisecondCounterHiRes() / 1000.0);
    if (statusLabel.isVisible()) statusLabel.toFront (false);
    updateEmptyQueueState();
    updateOutputView();
    musicLibraryPanel.updateVisibility();
    musicLibraryPanel.updateNavigation (libraryPages);
    layoutEffects();
    if (smartPlaylistEditor && smartPlaylistEditor->isVisible()) smartPlaylistEditor->toFront (false);
    repaint();
}

std::unique_ptr<juce::ComponentTraverser> MainComponent::createKeyboardFocusTraverser()
{
    return skinLayoutHost.createKeyboardFocusTraverser();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (smartPlaylistEditor && smartPlaylistEditor->isVisible()) return false;
    if (key == juce::KeyPress::escapeKey && statusLabel.isVisible())
    {
        for (auto* focused = juce::Component::getCurrentlyFocusedComponent();
             focused != nullptr; focused = focused->getParentComponent())
            if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
                return false;
        showStatus ({});
        return true;
    }
    if (key == juce::KeyPress::escapeKey && (settingsDrawerOpen || effectsDrawerOpen))
    {
        closeDrawers();
        applyWorkspaceVisuals();
        resized();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && queueOpen && queueToggleButton.isShowing())
    {
        setQueueOpen (false);
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        if (shouldDeferTransportCommands())
            handleRemoteTransportCommand ({ RemoteTransportCommandType::togglePlayPause });
        else
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
        if (! settingsDrawerOpen && ! effectsDrawerOpen && musicLibraryPanel.focusOverviewSearch()) return true;
        if (settingsDrawerOpen && pluginSettingsPanel.filters.search.isShowing())
        {
            pluginSettingsPanel.filters.search.grabKeyboardFocus();
            pluginSettingsPanel.filters.search.selectAll();
            return true;
        }
        if (effectsDrawerOpen && pluginFilters.search.isShowing())
        {
            pluginFilters.search.grabKeyboardFocus();
            pluginFilters.search.selectAll();
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
    if (restoreStatus == RestoreStatus::loading) return;
    if (workspaceMode == mode && ! settingsDrawerOpen && ! effectsDrawerOpen)
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
    settingsButton.setTooltip (settingsDrawerOpen ? "Close settings"
                                                 : "Open audio, plug-in, and skin settings");
    effectsButton.setToggleState (effectsDrawerOpen, juce::dontSendNotification);
    queueTitleLabel.setText ("Play queue", juce::dontSendNotification);
    effectRackTitleLabel.setText ("Effects", juce::dontSendNotification);
    settingsTitleLabel.setText ("Settings", juce::dontSendNotification);
    pluginFilterLabel.setText ("Find a plug-in", juce::dontSendNotification);
    repaint();
}

void MainComponent::closeDrawers()
{
    effectsDrawerOpen = false;
    settingsDrawerOpen = false;
}

void MainComponent::showStatus (const juce::String& message, bool isError)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setTooltip (message.isEmpty() ? juce::String{} : message + "\nClick to dismiss.");
    statusVisibleUntil = message.isEmpty() ? 0.0
        : juce::Time::getMillisecondCounterHiRes() / 1000.0 + (isError ? 14.0 : 7.0);
    statusLabel.setVisible (message.isNotEmpty());
    if (statusLabel.isVisible()) statusLabel.toFront (false);
}

void MainComponent::mouseUp (const juce::MouseEvent& event)
{
    if (event.originalComponent == &statusLabel) showStatus ({});
}

void MainComponent::timerCallback (int timerID)
{
    if (timerID == 2)
    {
        if (outputStage.isShowing() || outputMixer.isShowing())
        {
            const auto readings = controller.getOutputMeterReadings();
            if (outputStage.isShowing()) outputStage.setMeterReadings (readings);
            if (outputMixer.isShowing()) outputMixer.setPeaks (readings.samplePeak);
        }
        return;
    }

    if (statusVisibleUntil > 0.0
        && juce::Time::getMillisecondCounterHiRes() / 1000.0 >= statusVisibleUntil)
        showStatus ({});

    if (restoreStatus == RestoreStatus::loading)
        return;

    bool preparationFailed = false;
    const auto playbackChanged = controller.synchronizePlayback (true, &preparationFailed);
    const auto readFailed = controller.handlePlaybackReadError();
    if (playbackChanged || readFailed)
        refreshPlaylist();
    if (preparationFailed || readFailed)
        showStatus ((readFailed ? "Could not read an audio file. " : juce::String{})
                    + controller.getLastStatusMessage(), true);

    controller.checkpointPlaybackPosition();
    pluginSettingsPanel.refreshScanState();

    const auto effectRevision = controller.getEffectChainRevision();
    if (effectRevision != lastEffectChainRevision)
        refreshEffects();

    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (restoreStatus == RestoreStatus::ready)
    {
        if (nowSeconds >= nextDirectoryScanAt && ! musicDirectoryScanService.isRunning())
        {
            nextDirectoryScanAt = nowSeconds + 60.0;
            requestMusicDirectoryScan (true);
        }
        startMusicDirectoryScan();
        const auto minute = juce::Time::currentTimeMillis() / 60000;
        if (minute != smartPlaylistRefreshMinute)
        {
            smartPlaylistRefreshMinute = minute;
            const auto& saved = controller.getLibrary().getPlaylists();
            if (std::any_of (saved.begin(), saved.end(), [] (const auto& item) { return item.smartRules.has_value(); }))
                musicLibraryPanel.refresh();
            if (smartPlaylistEditor && smartPlaylistEditor->isVisible())
                smartPlaylistEditor->refreshPreview();
        }
    }
    const auto revision = controller.getStateRevision();
    if (restoreStatus == RestoreStatus::ready
        && autoSaveScheduler.observe (revision, nowSeconds))
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
            const auto artworkReferences = controller.retainArtworkForStateSave (snapshot);
            if (! stateSaveService.start (
                    std::move (snapshot), getStateFile(),
                    [safeThis, revision, artworkReferences] (bool saved,
                                          juce::String errorMessage)
                    {
                        if (safeThis != nullptr)
                        {
                            if (saved) safeThis->controller.acknowledgeSavedArtwork (artworkReferences);
                            safeThis->handleAutoSaveCompletion (revision, saved, errorMessage);
                        }
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
        showStatus ("Could not autosave Moonwake state: " + detail, true);
    }
    else if (autoSaveFailureVisible)
    {
        autoSaveFailureVisible = false;
        showStatus ("Autosave recovered.");
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
    const auto* entry = queueEntryAtRow (rowNumber);
    const auto* track = entry != nullptr ? &entry->track : nullptr;
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
    skinAdapter.drawListBoxRow (
        graphics, playlistTable, rowNumber, width, height,
        rowIsSelected,
        track->title + (track->available ? juce::String{} : " (Missing)"),
        track->file.getFullPathName(),
        duration, playlistTable.rowGeometry (height),
        false, false, track->available, track->artist, track->album,
        queueEntryAtRow (rowNumber)->current, controller.getLibrary().isFavourite (track->libraryId));
}

void MainComponent::paintCell (juce::Graphics&, int, int, int, int, bool)
{
    // The table renderer paints the complete row, including every cell, in
    // paintRowBackground so composite slot styling stays atomic.
}

juce::String MainComponent::getCellTooltip (int rowNumber, int)
{
    const auto* entry = queueEntryAtRow (rowNumber);
    const auto* track = entry != nullptr ? &entry->track : nullptr;
    if (track == nullptr)
        return {};
    juce::StringArray lines { track->title, track->artist, track->album };
    lines.removeEmptyStrings();
    lines.removeDuplicates (false);
    return lines.joinIntoString ("\n");
}

void MainComponent::selectedRowsChanged (int lastRowSelected)
{
    if (refreshingPlaylist)
        return;
    if (const auto* entry = queueEntryAtRow (lastRowSelected))
        selectedQueueKey = entry->key;
    else selectedQueueKey.clear();
    updateTransportState();
}

void MainComponent::cellClicked (int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu()) return;
    if (const auto* track = controller.getPlaylist().getTrack (queueSourceIndex (row)))
    {
        playlistTable.selectRow (row);
        musicLibraryPanel.showQueueTrackActions (*track, playlistTable);
    }
}

void MainComponent::cellDoubleClicked (int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu()) playQueueRow (row);
}

void MainComponent::returnKeyPressed (int row) { playQueueRow (row); }

const PlaybackQueueEntry* MainComponent::queueEntryAtRow (int row) const
{
    const auto index = libraryFilter.playlistIndexForRow (row);
    return juce::isPositiveAndBelow (index, visibleQueue.size()) ? &visibleQueue[static_cast<size_t> (index)] : nullptr;
}

int MainComponent::queueSourceIndex (int row) const
{
    const auto* entry = queueEntryAtRow (row);
    return entry != nullptr ? controller.getPlaylist().indexOfId (entry->track.id) : -1;
}

bool MainComponent::canMoveQueueSelection (int direction) const
{
    const auto row = playlistTable.getSelectedRow();
    const auto* from = queueEntryAtRow (row);
    const auto* to = queueEntryAtRow (row + direction);
    return from != nullptr && to != nullptr && ! from->current && ! to->current
        && ! controller.getPlaylist().isShuffleEnabled()
        && controller.getPlaylist().getRepeatMode() == RepeatMode::off && libraryFilter.query().isEmpty();
}

void MainComponent::playQueueRow (int row)
{
    const auto index = queueSourceIndex (row);
    if (! controller.selectTrack (index) || ! controller.setPlaying (true))
        showStatus (controller.getLastStatusMessage(), true);
    refreshPlaylist();
    updateTransportState();
}

void MainComponent::chooseFiles()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Add music files or folders",
                                                       juce::File{},
                                                       "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.m4a;*.aac;*.alac;*.caf;*.ogg;*.m3u;*.m3u8");

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
        if (file.isDirectory() || LibraryImportService::isSupportedAudioFile (file) || file.hasFileExtension ("m3u;m3u8"))
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

void MainComponent::chooseMusicDirectory()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Add a music folder", juce::File::getSpecialLocation (juce::File::userMusicDirectory), juce::String {});
    const auto safe = juce::Component::SafePointer<MainComponent> (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr || chooser.getResult() == juce::File()) return;
            if (safe->controller.addMusicDirectory (chooser.getResult()).isEmpty())
                safe->showStatus (safe->controller.getLastStatusMessage(), true);
            else safe->musicDirectoriesChanged (true);
        });
}

void MainComponent::musicDirectoriesChanged (bool scanNow)
{
    musicDirectoryScanService.cancel();
    pendingMusicDirectoryScan.reset();
    nextDirectoryScanAt = juce::Time::getMillisecondCounterHiRes() / 1000.0 + 60.0;
    musicDirectoriesPanel.refresh (controller.getMusicDirectories());
    musicDirectoriesPanel.setScanState (musicDirectoryScanService.isRunning(), controller.getLastStatusMessage());
    if (scanNow) requestMusicDirectoryScan (true);
}

void MainComponent::requestMusicDirectoryScan (bool automaticOnly)
{
    if (! pendingMusicDirectoryScan || ! automaticOnly) pendingMusicDirectoryScan = automaticOnly;
    startMusicDirectoryScan();
}

void MainComponent::startMusicDirectoryScan()
{
    if (! pendingMusicDirectoryScan || restoreStatus != RestoreStatus::ready
        || musicDirectoryScanService.isRunning() || libraryImportService.isRunning()
        || ! pendingLibraryImports.empty()) return;
    const auto automaticOnly = *pendingMusicDirectoryScan;
    pendingMusicDirectoryScan.reset();
    const auto configuration = controller.getMusicDirectories();
    auto directories = configuration;
    if (automaticOnly) std::erase_if (directories, [] (const auto& directory) { return ! directory.automatic; });
    if (directories.empty()) return;
    activeDirectoryScanAutomatic = automaticOnly;
    nextDirectoryScanAt = juce::Time::getMillisecondCounterHiRes() / 1000.0 + 60.0;
    const auto safe = juce::Component::SafePointer<MainComponent> (this);
    const auto artworkRoot = ProductPaths::artworkCacheRoot();
    const auto started = musicDirectoryScanService.start (std::move (directories), controller.getLibrary().getTracks(),
        [safe] (const MusicDirectoryScanProgress& progress)
        {
            if (safe == nullptr) return;
            const auto message = progress.stage == LibraryImportProgress::Stage::discovering
                ? juce::String ("Checking music folders...")
                : "Reading " + juce::String (progress.processed) + " / " + juce::String (progress.total)
                    + "  " + progress.currentFile;
            safe->musicDirectoriesPanel.setScanState (true, message);
        },
        [safe, configuration, automaticOnly, artworkRoot] (MusicDirectoryScanResult result) mutable
        {
            if (safe == nullptr)
            {
                MusicDirectoryScanService::discardPreparedArtwork (result, artworkRoot);
                return;
            }
            const auto cancelled = result.cancelled;
            const auto stale = configuration != safe->controller.getMusicDirectories();
            const auto hasErrors = ! result.errors.isEmpty() || ! result.artworkWarnings.isEmpty();
            juce::String message;
            if (cancelled || stale)
            {
                MusicDirectoryScanService::discardPreparedArtwork (result, artworkRoot);
                message = stale ? safe->controller.getLastStatusMessage() : juce::String ("Music folder scan cancelled.");
            }
            else
            {
                const auto ok = safe->controller.applyMusicDirectoryScan (std::move (result));
                message = safe->controller.getLastStatusMessage();
                safe->refreshPlaylist();
                safe->updateTransportState();
                if (! automaticOnly && (! ok || hasErrors)) safe->showStatus (message, true);
            }
            safe->musicDirectoriesPanel.refresh (safe->controller.getMusicDirectories());
            safe->musicDirectoriesPanel.setScanState (false, message);
            safe->nextDirectoryScanAt = juce::Time::getMillisecondCounterHiRes() / 1000.0 + 60.0;
            juce::MessageManager::callAsync ([safe]
            {
                if (safe == nullptr) return;
                safe->startLibraryImport ({});
                safe->startMusicDirectoryScan();
            });
        });
    if (started) musicDirectoriesPanel.setScanState (true, "Checking music folders...");
    else musicDirectoriesPanel.setScanState (false, "Could not start the music folder scan.");
}

void MainComponent::showSmartPlaylistEditor (const juce::String& id)
{
    SmartPlaylistRules draft;
    draft.rules.push_back ({ SmartPlaylistField::genre, SmartPlaylistOperator::contains, {} });
    juce::String initialName;
    if (id.isNotEmpty())
    {
        const auto* saved = controller.getLibrary().getPlaylist (id);
        if (saved == nullptr || ! saved->smartRules) return;
        initialName = saved->name;
        draft = *saved->smartRules;
    }
    const auto safe = juce::Component::SafePointer<MainComponent> (this);
    smartPlaylistEditor->show (*this, initialName, std::move (draft),
        [safe] (const SmartPlaylistRules& rules)
        {
            if (safe == nullptr) return 0;
            const auto now = juce::Time::currentTimeMillis();
            const auto& library = safe->controller.getLibrary();
            return static_cast<int> (std::count_if (library.getTracks().begin(), library.getTracks().end(),
                [&] (const auto& track) { return matchesSmartPlaylistRules (rules, track, library.isFavourite (track.id), now); }));
        },
        [safe, id] (const juce::String& name, const SmartPlaylistRules& rules)
        {
            if (safe == nullptr) return juce::Result::fail ("The library is no longer available.");
            auto destination = id;
            if (id.isEmpty()) destination = safe->controller.createSmartPlaylist (name, rules);
            else if (! safe->controller.updateSmartPlaylist (id, name, rules))
                return juce::Result::fail (safe->controller.getLastStatusMessage());
            if (destination.isEmpty()) return juce::Result::fail (safe->controller.getLastStatusMessage());
            juce::MessageManager::callAsync ([safe, destination]
            {
                if (safe == nullptr) return;
                safe->refreshPlaylist();
                safe->musicLibraryPanel.selectCollection ("playlist:" + destination);
            });
            return juce::Result::ok();
        });
}

void MainComponent::startLibraryImport (juce::Array<juce::File> files)
{
    if (! files.isEmpty())
    {
        const auto target = musicLibraryPanel.isActive()
            ? (musicLibraryPanel.page() == MusicLibraryPanel::Page::overview ? juce::String ("all")
                                                                           : controller.getLibraryCollectionId())
            : juce::String ("queue");
        juce::Array<juce::File> audioFiles;
        for (const auto& file : files)
            if (file.hasFileExtension ("m3u;m3u8"))
                pendingLibraryImports.push_back ({ {}, file, target, {} });
            else audioFiles.add (file);
        if (! audioFiles.isEmpty()) pendingLibraryImports.push_back ({ std::move (audioFiles), {}, target, {} });
    }
    if (! files.isEmpty() && musicDirectoryScanService.isRunning())
    {
        musicDirectoryScanService.cancel();
        if (! pendingMusicDirectoryScan) pendingMusicDirectoryScan = activeDirectoryScanAutomatic;
    }
    if (pendingLibraryImports.empty() || restoreStatus == RestoreStatus::loading
        || libraryImportService.isRunning() || musicDirectoryScanService.isRunning()) return;

    auto request = std::move (pendingLibraryImports.front());
    pendingLibraryImports.erase (pendingLibraryImports.begin());
    const auto safeThis = juce::Component::SafePointer<MainComponent> (this);
    const auto progress = [safeThis] (const LibraryImportProgress& state)
    {
        if (safeThis == nullptr) return;
        safeThis->addFilesButton.setTooltip (state.stage == LibraryImportProgress::Stage::discovering
            ? "Discovering music..." : "Reading " + juce::String (state.processed + 1)
                + " / " + juce::String (state.total) + "  " + state.currentFile);
    };
    const auto completion = [safeThis, request] (LibraryImportResult result) mutable
    {
        if (safeThis == nullptr) return;
        safeThis->setLibraryImportRunning (false);
        if (result.cancelled) safeThis->showStatus ("Music import cancelled.");
        else if (result.error.isNotEmpty()) safeThis->showStatus (result.error, true);
        else
        {
            juce::String message;
            bool error = false;
            if (request.relocateTrackId.isNotEmpty())
            {
                const auto succeeded = ! result.tracks.empty()
                    && safeThis->controller.relocateLibraryTrack (request.relocateTrackId, std::move (result.tracks.front()));
                message = succeeded ? "File location updated." : "Could not use the replacement audio file.";
                error = ! succeeded;
            }
            else if (result.playlistImport)
            {
                const auto id = safeThis->controller.importPreparedPlaylist (result.playlistName, std::move (result.tracks));
                message = safeThis->controller.getLastStatusMessage();
                error = id.isEmpty();
                if (! error && safeThis->musicLibraryPanel.isActive()
                    && safeThis->musicLibraryPanel.page() == MusicLibraryPanel::Page::collection
                    && safeThis->controller.getLibraryCollectionId() == request.collectionId)
                    safeThis->musicLibraryPanel.selectCollection ("playlist:" + id);
            }
            else message = safeThis->controller.importPreparedTracks (std::move (result.tracks), request.collectionId);
            for (const auto* warnings : { &result.skippedPaths, &result.artworkWarnings, &result.playlistWarnings })
                if (! warnings->isEmpty())
                {
                    message += " " + warnings->joinIntoString (" ");
                    error = true;
                }
            safeThis->showStatus (message, error);
            safeThis->refreshPlaylist();
            safeThis->updateTransportState();
        }
        safeThis->dispatchQueuedTransportCommands();
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr) return;
            safeThis->startLibraryImport ({});
            safeThis->dispatchQueuedTransportCommands();
        });
    };
    const auto started = request.playlistFile != juce::File()
        ? libraryImportService.startPlaylist (request.playlistFile, progress, completion)
        : libraryImportService.start (request.files, progress, completion);
    if (! started)
    {
        setLibraryImportRunning (false);
        showStatus ("Could not start the music import.", true);
        dispatchQueuedTransportCommands();
    }
    else
    {
        setLibraryImportRunning (true);
        updateTransportState();
    }
}

void MainComponent::choosePlaylistImport()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import playlist", juce::File{}, "*.m3u;*.m3u8");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            juce::Array<juce::File> files;
            for (const auto& url : chooser.getURLResults()) if (url.isLocalFile()) files.add (url.getLocalFile());
            safeThis->startLibraryImport (std::move (files));
        });
}

void MainComponent::choosePlaylistExport (std::vector<TrackItem> exportTracks)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Export playlist", juce::File{}, "*.m3u8");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<MainComponent> (this), tracks = std::move (exportTracks)] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr || chooser.getResult() == juce::File{}) return;
            const auto output = chooser.getResult();
            const auto started = juce::Thread::launch ([safeThis, output, tracks]
            {
                juce::String error;
                const auto success = PlaylistFile::writeM3u8 (output, tracks, true, error);
                juce::MessageManager::callAsync ([safeThis, output, success, error]
                {
                    if (safeThis != nullptr) safeThis->showStatus (success ? "Exported " + output.getFileName() + "." : error, ! success);
                });
            });
            if (! started) safeThis->showStatus ("Could not start playlist export.", true);
        });
}

void MainComponent::setQueueOpen (bool open)
{
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    bool restoreQueueFocus = false;
    if (! open && focused != nullptr)
        for (const auto& id : skinBindings.ids())
            if (id.startsWith ("queue.") && id != "queue.toggle")
                if (const auto* component = skinBindings.find (id))
                    restoreQueueFocus = restoreQueueFocus || component == focused || component->isParentOf (focused);
    queueVisibilityChosen = true;
    queueOpen = open;
    queueToggleButton.setToggleState (open, juce::dontSendNotification);
    queueToggleButton.setTooltip (open ? "Hide play queue" : "Show play queue");
    resized();
    if (open && playlistTable.isShowing()) playlistTable.grabKeyboardFocus();
    if (restoreQueueFocus && queueToggleButton.isShowing()) queueToggleButton.grabKeyboardFocus();
}

void MainComponent::choosePlaylistArtwork (juce::String playlistId)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose playlist cover", juce::File {}, "*.png;*.jpg;*.jpeg");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent> (this), playlistId] (const auto& chooser)
        {
            if (safe == nullptr || chooser.getResult() == juce::File {}) return;
            safe->controller.choosePlaylistArtwork (playlistId, chooser.getResult(), [safe] (bool ok, const auto& message)
            {
                if (safe == nullptr) return;
                safe->refreshPlaylist();
                safe->showStatus (message, ! ok);
            });
        });
}

void MainComponent::chooseTrackRelocation (juce::String trackId)
{
    const auto* track = controller.getLibrary().getTrack (trackId);
    if (track == nullptr) return;
    fileChooser = std::make_unique<juce::FileChooser> ("Locate " + track->title, track->file.getParentDirectory(),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.m4a;*.aac;*.alac;*.caf;*.ogg");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MainComponent> (this), trackId] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr || ! chooser.getURLResult().isLocalFile()) return;
            const auto file = chooser.getURLResult().getLocalFile();
            if (file == juce::File{}) return;
            safeThis->pendingLibraryImports.push_back ({ { file }, {}, safeThis->controller.getLibraryCollectionId(), trackId });
            safeThis->startLibraryImport ({});
        });
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
    fileChooser = std::make_unique<juce::FileChooser> ("Add VST3 plug-in folder",
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

                                  auto directories = safeThis->controller.getPluginCustomVst3Directories();
                                  directories.addIfNotAlreadyThere (directory.getFullPathName());
                                  if (! safeThis->controller.setPluginCustomVst3Directories (directories))
                                      safeThis->showStatus (safeThis->controller.getLastStatusMessage(), true);
                                  safeThis->pluginSettingsPanel.refreshInventory();
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
    if (const auto session = skinManager.activeSession(); session != nullptr
        && session->compiledSkin()->layouts.contains (SkinLayoutEntry::library)
        && session->compiledSkin()->layouts.contains (SkinLayoutEntry::collection))
        libraryFilter.setQuery ({});
    const auto nextVersion = controller.getPlaybackQueueVersion();
    juce::String editedSelection;
    if ((std::get<0> (queueViewVersion) != std::get<0> (nextVersion)
         || std::get<3> (nextVersion).isEmpty())
        && std::get<3> (queueViewVersion) == std::get<3> (nextVersion))
    {
        for (const auto& entry : visibleQueue)
            if (entry.key == selectedQueueKey) editedSelection = entry.track.id;
        if (std::count_if (visibleQueue.begin(), visibleQueue.end(),
                [&] (const auto& entry) { return entry.track.id == editedSelection; }) != 1)
            editedSelection.clear();
    }
    queueViewVersion = nextVersion;
    visibleQueue = controller.getPlaybackQueue();
    std::vector<TrackItem> rows;
    rows.reserve (visibleQueue.size());
    for (const auto& item : visibleQueue) rows.push_back (item.track);
    libraryFilter.rebuild (rows);
    const juce::ScopedValueSetter<bool> guard (refreshingPlaylist, true);
    playlistTable.updateContent();
    auto selectedIndex = -1;
    for (size_t i = 0; i < visibleQueue.size(); ++i)
        if (visibleQueue[i].key == selectedQueueKey) selectedIndex = static_cast<int> (i);
    if (selectedIndex < 0 && editedSelection.isNotEmpty())
    {
        const auto matches = std::count_if (visibleQueue.begin(), visibleQueue.end(),
            [&] (const auto& entry) { return entry.track.id == editedSelection; });
        if (matches == 1)
            for (size_t i = 0; i < visibleQueue.size(); ++i)
                if (visibleQueue[i].track.id == editedSelection)
                {
                    selectedIndex = static_cast<int> (i);
                    selectedQueueKey = visibleQueue[i].key;
                    break;
                }
    }
    const auto visibleSelected = libraryFilter.rowForPlaylistIndex (selectedIndex);
    if (visibleSelected >= 0)
        playlistTable.selectRow (visibleSelected);
    else
        playlistTable.deselectAllRows();
    queueCountLabel.setText (
        libraryFilter.query().isEmpty()
            ? workCountText (static_cast<int> (visibleQueue.size()))
            : juce::String (libraryFilter.visibleCount()) + " / "
                + juce::String (visibleQueue.size()),
        juce::dontSendNotification);
    updateEmptyQueueState();
    musicLibraryPanel.refresh();
    if (smartPlaylistEditor && smartPlaylistEditor->isVisible()) smartPlaylistEditor->refreshPreview();
    repaint();
}

void MainComponent::updateEmptyQueueState()
{
    const auto placedBySkin = emptyQueueLabel.getParentComponent() != this;
    const auto hasNoVisibleWorks = libraryFilter.visibleCount() == 0;
    emptyQueueLabel.setText (
        libraryFilter.query().isEmpty()
            ? "No tracks yet\nDrop music or folders here, or use + Music"
            : "No matching tracks\nTry another search",
        juce::dontSendNotification);
    emptyQueueLabel.setVisible (placedBySkin && playlistTable.isVisible()
                                && hasNoVisibleWorks);
}

void MainComponent::refreshPlugins()
{
    const auto oldIndex = pluginCombo.getSelectedId() - 1;
    const auto previous = juce::isPositiveAndBelow (oldIndex, catalogPluginIdentifiers.size())
        ? catalogPluginIdentifiers[oldIndex] : juce::String {};
    pluginCombo.clear (juce::dontSendNotification);
    catalogPluginIdentifiers.clear();
    const auto plugins = controller.getInsertEffectPlugins();
    pluginFilters.setPlugins (plugins);
    const auto filter = pluginFilters.filter();
    int selectedId = 0;
    for (int i = 0; i < plugins.size(); ++i)
    {
        const auto& plugin = plugins.getReference (i);
        const auto identifier = plugin.createIdentifierString();
        catalogPluginIdentifiers.add (identifier);
        if (! filter.matches (plugin)) continue;
        const auto format = plugin.pluginFormatName == "AudioUnit" ? "AU" : plugin.pluginFormatName;
        pluginCombo.addItem ("[" + format + "] " + plugin.name + " - " + PluginFilter::manufacturerName (plugin), i + 1);
        if (identifier == previous) selectedId = i + 1;
    }
    pluginCombo.setTextWhenNothingSelected (plugins.isEmpty()
        ? "No enabled effect plug-ins" : "No matching plug-ins");
    emptyRackLabel.setText (plugins.isEmpty()
        ? "No effects yet\nEnable a format and scan plug-ins in Settings"
        : "No effects yet\nChoose a plug-in above to add it", juce::dontSendNotification);
    pluginCombo.setSelectedId (selectedId > 0 ? selectedId : pluginCombo.getItemId (0), juce::dontSendNotification);
    addEffectButton.setEnabled (! pluginInsertionInProgress && pluginCombo.getSelectedId() > 0);
}

void MainComponent::refreshEffectScopes()
{
    EffectScope selected;
    if (juce::isPositiveAndBelow (effectScopeBox.getSelectedId() - 1, static_cast<int> (effectScopeChoices.size())))
        selected = effectScopeChoices[static_cast<size_t> (effectScopeBox.getSelectedId() - 1)];
    auto choices = EffectScopeMenu (toAudioChannelSet (controller.getViewState().outputLayout));
    effectScopeBox.clear (juce::dontSendNotification);
    *effectScopeBox.getRootMenu() = choices.menu;
    const auto selectedId = choices.idFor (selected);
    effectScopeChoices = std::move (choices.scopes);
    effectScopeBox.setSelectedId (selectedId > 0 ? selectedId : 1, juce::dontSendNotification);
    effectScopeBox.setTooltip ("Channels for the next effect. Existing effects keep their own channel selection.");
}

void MainComponent::refreshEffects()
{
    const auto& chain = controller.getEffectChain();
    std::vector<std::unique_ptr<EffectSlotRow>> nextRows;
    nextRows.reserve (static_cast<size_t> (chain.size()));
    effectRackViewport.setSlotCount (chain.size());
    for (int i = 0; i < chain.size(); ++i)
    {
        if (const auto* slot = chain.getSlot (i))
        {
            const auto existing = std::find_if (effectRows.begin(), effectRows.end(), [slot] (const auto& row)
            {
                return row != nullptr && row->getSlotId() == slot->id;
            });
            // Stable slot identity preserves each row's focus and local control state.
            auto row = existing != effectRows.end() ? std::move (*existing)
                : std::make_unique<EffectSlotRow> (*this, slot->id);
            if (existing == effectRows.end()) effectRackContent.addAndMakeVisible (*row);
            row->updateState (i, chain.size(), *slot);
            nextRows.push_back (std::move (row));
        }
    }
    effectRows = std::move (nextRows);
    lastEffectChainRevision = controller.getEffectChainRevision();
    layoutEffects();
}

void MainComponent::layoutEffects()
{
    const auto& chain = controller.getEffectChain();
    if (effectRackViewport.getLocalBounds().isEmpty()) return;
    SkinRenderContext layoutContext;
    layoutContext.bounds = effectRackViewport.getLocalBounds().toFloat();
    layoutContext.repeatItemCount = chain.size();
    layoutContext.extraLookup = [count = chain.size()] (std::string_view key) -> std::optional<SkinExpressionValue>
    {
        if (key == "chain.slotCount") return SkinExpressionValue (static_cast<double> (count));
        return std::nullopt;
    };
    const auto initialLayout = skinAdapter.rowLayoutFor (effectRackViewport, SkinRenderTarget::effectChain, layoutContext);
    if (! initialLayout) return;
    effectRowHeight = juce::jmax (1, juce::roundToInt (initialLayout->rowHeight));
    effectRowGap = juce::jmax (0, juce::roundToInt (initialLayout->rowGap));
    const auto contentHeight = juce::jmax (effectRackViewport.getHeight(),
        chain.size() * (effectRowHeight + effectRowGap) - (chain.size() > 0 ? effectRowGap : 0));
    const auto scrollbarWidth = contentHeight > effectRackViewport.getHeight()
        ? effectRackViewport.getScrollBarThickness() : 0;
    const auto contentWidth = juce::jmax (1, effectRackViewport.getWidth() - scrollbarWidth);
    layoutContext.bounds.setWidth (static_cast<float> (contentWidth));
    effectRowLayout = skinAdapter.rowLayoutFor (effectRackViewport, SkinRenderTarget::effectChain, layoutContext).value_or (*initialLayout);
    effectRowHeight = juce::jmax (1, juce::roundToInt (effectRowLayout.rowHeight));
    effectRowGap = juce::jmax (0, juce::roundToInt (effectRowLayout.rowGap));
    effectRackContent.setSize (contentWidth, juce::jmax (effectRackViewport.getHeight(),
        chain.size() * (effectRowHeight + effectRowGap) - (chain.size() > 0 ? effectRowGap : 0)));

    auto y = 0;
    for (auto& row : effectRows)
    {
        row->setBounds (0, y, contentWidth, effectRowHeight);
        row->resized();
        y += effectRowHeight + effectRowGap;
    }

    effectRackContent.repaint();
    const auto emptyLabelPlaced = emptyRackLabel.getParentComponent() != nullptr
        && emptyRackLabel.getParentComponent() != this;
    emptyRackLabel.setVisible (emptyLabelPlaced && effectRackViewport.isVisible() && chain.size() == 0);
}

void MainComponent::updateOutputView()
{
    // A bound component lives inside the active skin's placement wrapper.
    const auto mixerPlaced = outputMixer.getParentComponent() != nullptr
        && outputMixer.getParentComponent() != this;
    const auto stagePlaced = outputStage.getParentComponent() != nullptr
        && outputStage.getParentComponent() != this;
    meterModeBox.setItemEnabled (4, mixerPlaced);
    if (! mixerPlaced && meterModeBox.getSelectedId() == 4
        && meterModeBox.getParentComponent() != nullptr && meterModeBox.getParentComponent() != this)
        meterModeBox.setSelectedId (2, juce::dontSendNotification);
    const auto mixing = meterModeBox.getSelectedId() == 4 && mixerPlaced;
    outputMixer.setVisible (mixing);
    outputStage.setVisible (stagePlaced && ! mixing);
    if (meterModeBox.getSelectedId() != 4)
        outputStage.setMeterMode (static_cast<OutputStageComponent::MeterMode> (meterModeBox.getSelectedId() - 1));
    resetMeterButton.setTooltip (mixing ? "Reset all channel gains, mutes and solos"
                                      : "Start a new loudness measurement");
}

void MainComponent::updateTransportState()
{
    if (queueViewVersion != controller.getPlaybackQueueVersion()) refreshPlaylist();
    const auto state = controller.getViewState();
    nowPlayingCoordinator.update (
        state, juce::Time::getMillisecondCounterHiRes() / 1000.0,
        shouldDeferTransportCommands());

    if (! seekGestureInProgress)
        seekSlider.setValue (state.seekRatio, juce::dontSendNotification);

    playPauseButton.setButtonText (state.isPlaying ? "Pause" : "Play");
    playPauseButton.setTooltip (state.isPlaying ? "Pause" : "Play");
    playPauseButton.getProperties().set (
        SkinComponentBindings::iconNameProperty,
        state.isPlaying ? "pause" : "play");
    timeLabel.setText (controller.getPlaybackTimeText(), juce::dontSendNotification);
    volumeSlider.setValue (state.volume, juce::dontSendNotification);
    shuffleButton.setToggleState (state.shuffleEnabled, juce::dontSendNotification);
    shuffleButton.setTooltip (state.shuffleEnabled ? "Shuffle on" : "Shuffle off");
    repeatButton.setToggleState (state.repeatMode != RepeatMode::off, juce::dontSendNotification);
    repeatButton.setButtonText (state.repeatMode == RepeatMode::one ? "Repeat one"
        : state.repeatMode == RepeatMode::all ? "Repeat all" : "Repeat off");
    repeatButton.setTooltip (repeatButton.getButtonText() + "; click to cycle off, all, one");
    repeatButton.getProperties().set (SkinComponentBindings::iconNameProperty,
                                     state.repeatMode == RepeatMode::one ? "repeatOne" : "repeat");

    // Polling must not overwrite a choice before ComboBox delivers its asynchronous notification.
    if (lastSyncedRepeatMode != state.repeatMode)
    {
        switch (state.repeatMode)
        {
            case RepeatMode::all: repeatModeBox.setSelectedId (2, juce::dontSendNotification); break;
            case RepeatMode::one: repeatModeBox.setSelectedId (3, juce::dontSendNotification); break;
            case RepeatMode::off: repeatModeBox.setSelectedId (1, juce::dontSendNotification); break;
        }
        lastSyncedRepeatMode = state.repeatMode;
    }

    if (lastSyncedOutputLayout != state.outputLayout)
    {
        outputLayoutBox.setSelectedId (static_cast<int> (state.outputLayout) + 1, juce::dontSendNotification);
        lastSyncedOutputLayout = state.outputLayout;
    }
    for (const auto& layout : outputLayouts())
        outputLayoutBox.setItemEnabled (static_cast<int> (layout.layout) + 1,
                                        layout.channels.size() <= state.availableOutputChannels);
    outputLayoutBox.setTooltip ("Speaker layout; this device provides "
        + juce::String (state.availableOutputChannels) + " output channels.");

    if (state.selectedIndex >= 0)
    {
        nowPlayingLabel.setText (state.selectedTrackTitle, juce::dontSendNotification);

        if (const auto* track = controller.getPlaylist().getSelectedTrack())
        {
            const auto sampleRateText = track->sampleRate > 0.0
                                            ? juce::String (track->sampleRate / 1000.0, 1) + " kHz"
                                            : juce::String ("-- kHz");
            const auto extension = track->file.getFileExtension().substring (1).toUpperCase();
            const auto format = extension.isNotEmpty() ? extension : track->formatName;
            const auto separator = juce::String::fromUTF8 (" \xc2\xb7 ");
            const auto bitDepth = track->bitDepth > 0
                                      ? juce::String (track->bitDepth) + "-bit" + separator
                                      : juce::String{};
            const auto technical = format + separator + bitDepth + sampleRateText
                + separator + juce::String (track->channelCount)
                + (track->channelCount == 1 ? " channel" : " channels");
            const auto workByline = track->artist.isNotEmpty() || track->album.isNotEmpty()
                ? (track->artist.isNotEmpty() ? track->artist : "Unknown artist")
                    + (track->album.isNotEmpty() && track->album != track->title
                           ? separator + track->album : juce::String{})
                : track->file.getParentDirectory().getFileName();
            trackMetaLabel.setText (workspaceMode == WorkspaceMode::studio
                                        ? technical
                                        : workByline,
                                    juce::dontSendNotification);
            artworkMetaLabel.setText (technical, juce::dontSendNotification);
            nowPlayingLabel.setTooltip (track->title);
            trackMetaLabel.setTooltip (workByline + "\n" + technical);
            albumArtwork.setTrack (track->id, track->title,
                                   juce::File (state.selectedTrackArtworkPath));
        }
    }
    else
    {
        nowPlayingLabel.setText ("No track loaded", juce::dontSendNotification);
        trackMetaLabel.setText (controller.getLibrary().getTracks().empty()
            ? "Use + Music to add audio" : "Double-click a song or press Play", juce::dontSendNotification);
        auto formats = controller.getRegisteredPluginFormats();
        for (auto& format : formats) if (format == "AudioUnit") format = "AU";
        artworkMetaLabel.setText ("Local audio / " + formats.joinIntoString (" + "), juce::dontSendNotification);
        albumArtwork.setTrack ({}, {});
    }

    const auto gain = static_cast<float> (state.volume);
    const auto decibels = gain > 0.00001f ? juce::Decibels::gainToDecibels (gain) : -100.0f;
    volumeValueLabel.setText (gain > 0.00001f ? juce::String (decibels, 1) + " dB" : "-inf dB",
                              juce::dontSendNotification);

    const auto& chain = controller.getEffectChain();
    juce::StringArray activeEffects;
    for (const auto& slot : chain.getSlots())
        if (slot.available && ! slot.bypassed)
            activeEffects.add (slot.pluginName);
    effectsSummaryLabel.setText (juce::String (state.allEffectsBypassed ? 0 : activeEffects.size()) + " active / "
                                 + juce::String (chain.size()) + " total", juce::dontSendNotification);
    effectsSummaryLabel.setTooltip (state.allEffectsBypassed ? "All effects bypassed" : activeEffects.joinIntoString ("\n"));
    const auto hasLoadedEffects = std::any_of (chain.getSlots().begin(), chain.getSlots().end(),
        [] (const auto& slot) { return slot.available; });
    clearEffectTailsButton.setEnabled (restoreStatus != RestoreStatus::loading && hasLoadedEffects);
    bypassAllEffectsButton.setEnabled (restoreStatus != RestoreStatus::loading && (hasLoadedEffects || state.allEffectsBypassed));
    bypassAllEffectsButton.setToggleState (state.allEffectsBypassed, juce::dontSendNotification);
    bypassAllEffectsButton.setTooltip (state.allEffectsBypassed
        ? "All effects bypassed. Click to restore the individual plug-in states."
        : "Monitor without effects. Individual plug-in states are kept.");
    effectsButton.setButtonText ("Effects  " + juce::String (chain.size()));

    const auto deviceName = state.audioDeviceName.isNotEmpty() ? state.audioDeviceName : "No audio device";
    deviceStatusLabel.setText (deviceName + "  /  " + juce::String (state.availableOutputChannels) + " OUT",
                               juce::dontSendNotification);
    outputStage.setState (state.outputLayout, workspaceMode, state.isPlaying, state.selectedTrackTitle,
                          deviceName, state.availableOutputChannels, state.outputPeakLevels);
    const auto meterReadings = controller.getOutputMeterReadings();
    outputStage.setMeterReadings (meterReadings);
    outputMixer.setState (state.outputLayout, controller.getOutputChannelMix(), meterReadings.samplePeak);

    const auto hasSelection = playlistTable.getSelectedRow() >= 0;
    const auto* selectedQueueEntry = queueEntryAtRow (playlistTable.getSelectedRow());
    removeTrackButton.setEnabled (hasSelection && selectedQueueEntry != nullptr
        && selectedQueueEntry->track.id != controller.getLoadedTrackId());
    clearPlaylistButton.setEnabled (state.playlistSize > 0);
    moveTrackUpButton.setEnabled (canMoveQueueSelection (-1));
    moveTrackDownButton.setEnabled (canMoveQueueSelection (1));
    musicLibraryPanel.updatePlayback();
    queueToggleButton.setToggleState (queueOpen, juce::dontSendNotification);
    queueToggleButton.setTooltip (queueOpen ? "Hide play queue" : "Show play queue");
    const auto transportReady = restoreStatus != RestoreStatus::loading;
    playPauseButton.setEnabled (transportReady && (state.canPlay
        || (musicLibraryPanel.isActive() && musicLibraryPanel.canStartPlayback())));
    previousButton.setEnabled (transportReady && state.canGoPrevious);
    nextButton.setEnabled (transportReady && state.canGoNext);
}

bool MainComponent::shouldDeferTransportCommands() const
{
    return restoreStatus == RestoreStatus::loading
        || (controller.getPlaylist().isEmpty()
            && (libraryImportService.isRunning() || ! pendingLibraryImports.empty()));
}

void MainComponent::dispatchQueuedTransportCommands()
{
    if (shouldDeferTransportCommands()) return;
    auto commands = std::move (queuedTransportCommands);
    queuedTransportCommands.clear();
    for (const auto& command : commands) handleRemoteTransportCommand (command);
    updateTransportState();
}

bool MainComponent::performPlayCommand (bool toggle)
{
    if (controller.getLoadedTrackId().isEmpty())
    {
        if (musicLibraryPanel.isActive() && musicLibraryPanel.canStartPlayback())
            return musicLibraryPanel.startPlayback();
        const auto selected = queueSourceIndex (playlistTable.getSelectedRow());
        if (! musicLibraryPanel.isActive() && selected >= 0)
            return controller.selectTrack (selected) && controller.setPlaying (true);
    }
    return toggle ? controller.playOrPause() : controller.setPlaying (true);
}

void MainComponent::handleRemoteTransportCommand (
    const RemoteTransportCommand& command)
{
    if (shouldDeferTransportCommands() || ! queuedTransportCommands.empty())
    {
        queuedTransportCommands.push_back (command);
        dispatchQueuedTransportCommands();
        return;
    }
    bool succeeded = true;
    switch (command.type)
    {
        case RemoteTransportCommandType::play:
            succeeded = performPlayCommand (false);
            break;
        case RemoteTransportCommandType::pause:
            succeeded = controller.setPlaying (false);
            break;
        case RemoteTransportCommandType::togglePlayPause:
            succeeded = performPlayCommand (true);
            break;
        case RemoteTransportCommandType::next:
            succeeded = controller.nextTrack();
            refreshPlaylist();
            break;
        case RemoteTransportCommandType::previous:
            succeeded = controller.previousTrack();
            refreshPlaylist();
            break;
        case RemoteTransportCommandType::seekToSeconds:
            succeeded = controller.seekToSeconds (command.positionSeconds);
            break;
    }

    if (! succeeded) showStatus (controller.getLastStatusMessage(), true);
    updateTransportState();
}

void MainComponent::openPluginEditor (int effectIndex)
{
    const auto* slot = controller.getEffectChain().getSlot (effectIndex);
    if (slot == nullptr || ! slot->available)
    {
        showStatus (slot != nullptr ? slot->errorMessage : "No effect selected.", true);
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
        showStatus ("No effect selected.", true);
        return;
    }

    auto* editor = plugin->getActiveEditor();
    if (editor == nullptr)
        editor = plugin->createEditorIfNeeded();

    if (editor == nullptr)
    {
        showStatus ("This plug-in has no editor.");
        return;
    }

    pluginEditorWindows.push_back (std::make_unique<PluginEditorWindow> (slot->id, plugin->getName(), editor));
}

void MainComponent::removeEffect (int effectIndex)
{
    closePluginEditors();

    if (! controller.removeEffectAtIndex (effectIndex))
        showStatus ("Could not remove effect.", true);

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
        showStatus ("Could not reorder effect.", true);
        return;
    }

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
