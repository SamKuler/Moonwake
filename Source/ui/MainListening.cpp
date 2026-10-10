#include "MainComponent.h"
#include "app/ProductPaths.h"

#include <cmath>

void MainComponent::initialiseListeningControls()
{
    lyricsService = std::make_unique<LyricsService> (ProductPaths::applicationDataRoot().getChildFile ("Lyrics"));
    for (auto* component : std::initializer_list<juce::Component*> {
             &lyricsView, &lyricsToggleButton, &lyricsActionsButton, &lyricsFollowButton,
             &fullscreenButton, &miniButton, &restorePlayerButton, &pinPlayerButton, &nowPlayingArtistLabel })
        addAndMakeVisible (component);
    lyricsToggleButton.setButtonText ("Lyrics");
    lyricsToggleButton.setTooltip ("Show or hide lyrics");
    lyricsActionsButton.setButtonText ("Lyrics options");
    lyricsActionsButton.setTooltip ("Find, import or adjust lyrics");
    lyricsFollowButton.setButtonText ("Follow playback");
    lyricsFollowButton.setTooltip ("Return to the current lyric");
    fullscreenButton.setButtonText ("Full screen");
    fullscreenButton.setTooltip ("Open full-screen player");
    miniButton.setButtonText ("Mini player");
    miniButton.setTooltip ("Open mini player");
    restorePlayerButton.setButtonText ("Restore player");
    restorePlayerButton.setTooltip ("Return to the main player (Escape)");
    pinPlayerButton.setButtonText ("Keep on top");
    pinPlayerButton.setTooltip ("Keep mini player above other windows");
    nowPlayingArtistLabel.setInterceptsMouseClicks (false, false);
    lyricsToggleButton.onClick = [this] { setLyricsOpen (! lyricsOpen); };
    lyricsActionsButton.onClick = [this] { showLyricsActions(); };
    lyricsFollowButton.onClick = [this] { lyricsView.setFollowing (true); };
    fullscreenButton.onClick = [this] { requestPresentation (PlayerPresentation::immersive); };
    miniButton.onClick = [this] { requestPresentation (PlayerPresentation::mini); };
    restorePlayerButton.onClick = [this] { requestPresentation (PlayerPresentation::normal); };
    pinPlayerButton.onClick = [this]
    {
        if (onPinRequested) onPinRequested (! pinPlayerButton.getToggleState());
    };
    lyricsView.onFollowingChanged = [this] (bool) { updateLyricsControls(); };
    lyricsView.onSeek = [this] (double lyricTime)
    {
        if (! lyricsTrack || ! lyricsDocument || ! lyricsDocument->isSynced()) return;
        if (! controller.seekToSeconds (juce::jmax (0.0, lyricTime - lyricsOffsetSeconds)))
        {
            showStatus (controller.getLastStatusMessage(), true);
            return;
        }
        lyricsView.setFollowing (true);
        updateTransportState();
    };
    lyricsView.setStatus ("Lyrics", "Choose a song to get started.");
    updateLyricsControls();
}

bool MainComponent::supportsPresentation (PlayerPresentation mode) const
{
    if (mode == PlayerPresentation::normal) return true;
    const auto session = skinManager.activeSession();
    return session && session->compiledSkin()->layouts.contains (
        mode == PlayerPresentation::mini ? SkinLayoutEntry::mini : SkinLayoutEntry::immersive);
}

PlayerWindowSizing MainComponent::windowSizing (PlayerPresentation mode) const
{
    PlayerWindowSizing sizing;
    if (mode != PlayerPresentation::mini) return sizing;
    sizing = { 480, 200, 380, 170, 1200, 800 };
    const auto session = skinManager.activeSession();
    if (! session) return sizing;
    const auto& layouts = session->compiledSkin()->layouts;
    const auto found = layouts.find (SkinLayoutEntry::mini);
    if (found == layouts.end()) return sizing;
    const auto& layout = found->second;
    const auto& root = layout.base.common;
    if (root.minWidth > 0) sizing.minimumWidth = juce::roundToInt (root.minWidth);
    if (root.minHeight > 0) sizing.minimumHeight = juce::roundToInt (root.minHeight);
    if (std::isfinite (root.maxWidth)) sizing.maximumWidth = juce::roundToInt (root.maxWidth);
    if (std::isfinite (root.maxHeight)) sizing.maximumHeight = juce::roundToInt (root.maxHeight);
    sizing.maximumWidth = juce::jmax (sizing.minimumWidth, sizing.maximumWidth);
    sizing.maximumHeight = juce::jmax (sizing.minimumHeight, sizing.maximumHeight);
    if (layout.preferredSize)
    {
        sizing.width = juce::roundToInt (layout.preferredSize->x);
        sizing.height = juce::roundToInt (layout.preferredSize->y);
    }
    sizing.width = juce::jlimit (sizing.minimumWidth, sizing.maximumWidth, sizing.width);
    sizing.height = juce::jlimit (sizing.minimumHeight, sizing.maximumHeight, sizing.height);
    return sizing;
}

void MainComponent::requestPresentation (PlayerPresentation mode)
{
    if (! supportsPresentation (mode)) return;
    if (onPresentationRequested) onPresentationRequested (mode);
}

void MainComponent::setPlayerPresentation (PlayerPresentation mode)
{
    if (mode == playerPresentation) return;
    playerPresentation = mode;
    closeDrawers();
    if (mode != PlayerPresentation::normal)
    {
        controller.stopCalibrationTestSignal();
        workspaceMode = WorkspaceMode::listening;
        controller.setWorkspaceMode (workspaceMode);
        musicLibraryPanel.showPlayer();
        lyricsOpen = true;
        queueOpen = false;
    }
    applyWorkspaceVisuals();
    updateLyricsControls();
    resized();
}

void MainComponent::setMiniPinned (bool pinned)
{
    pinPlayerButton.setToggleState (pinned, juce::dontSendNotification);
    pinPlayerButton.setTooltip (pinned ? "Stop keeping mini player on top" : "Keep mini player above other windows");
}

void MainComponent::setLyricsOpen (bool open)
{
    lyricsOpen = open;
    updateLyricsControls();
    resized();
    if (! open && lyricsToggleButton.isShowing()) lyricsToggleButton.grabKeyboardFocus();
}

void MainComponent::updateLyricsControls()
{
    const auto ready = restoreStatus != RestoreStatus::loading;
    lyricsToggleButton.setToggleState (lyricsOpen, juce::dontSendNotification);
    lyricsToggleButton.setTooltip (lyricsOpen ? "Hide lyrics" : "Show lyrics");
    lyricsToggleButton.setEnabled (ready);
    lyricsActionsButton.setEnabled (ready && lyricsTrack.has_value());
    lyricsFollowButton.setToggleState (lyricsView.isFollowing(), juce::dontSendNotification);
    lyricsFollowButton.setEnabled (lyricsDocument && lyricsDocument->isSynced() && ! lyricsView.isFollowing());
    fullscreenButton.setEnabled (ready && supportsPresentation (PlayerPresentation::immersive));
    miniButton.setEnabled (ready && supportsPresentation (PlayerPresentation::mini));
    pinPlayerButton.setEnabled (playerPresentation == PlayerPresentation::mini);
}

void MainComponent::refreshLyricsTrack()
{
    if (! lyricsService) return;
    const auto* track = controller.getPlaylist().getSelectedTrack();
    nowPlayingArtistLabel.setText (track ? track->artistNames().joinIntoString (", ") : juce::String {},
                                   juce::dontSendNotification);
    const auto signature = track ? LyricsService::trackKey (*track) + "|" + track->file.getFullPathName()
        + "|" + track->title + "|" + track->artist + "|" + track->album
        + "|" + juce::String (track->durationSeconds, 3) + "|" + juce::String (track->fileModifiedAtMs) : juce::String {};
    if (signature == lyricsTrackSignature) return;
    lyricsTrackSignature = signature;
    lyricsService->cancel();
    ++lyricsOffsetRevision;
    lyricsOffsetSeconds = 0;
    lyricsDocument.reset();
    lyricsView.setDocument ({});
    if (track)
    {
        lyricsTrack = *track;
        reloadLyrics();
    }
    else
    {
        lyricsTrack.reset();
        lyricsView.setStatus ("Lyrics", "Choose a song to get started.");
    }
    updateLyricsControls();
}

void MainComponent::reloadLyrics (bool refreshOnline)
{
    if (! lyricsTrack) return;
    lyricsLoading = true;
    lyricsView.setStatus ("Finding lyrics", lyricsTrack->title);
    const auto key = LyricsService::trackKey (*lyricsTrack);
    lyricsService->load (*lyricsTrack,
        [safe = juce::Component::SafePointer<MainComponent> (this), key, revision = lyricsOffsetRevision] (LyricsLoadResult result)
        { if (safe) safe->applyLyricsResult (key, std::move (result), revision); }, refreshOnline);
}

void MainComponent::applyLyricsResult (const juce::String& key, LyricsLoadResult result, uint64_t offsetRevision)
{
    if (! lyricsTrack || key != LyricsService::trackKey (*lyricsTrack)) return;
    lyricsLoading = false;
    if (lyricsOffsetRevision == offsetRevision) lyricsOffsetSeconds = result.offsetSeconds;
    lyricsDocument.reset();
    if (result.state == LyricsLoadResult::State::ready)
    {
        if (result.error.isNotEmpty()) showStatus (result.error, true);
        lyricsDocument = std::make_shared<const LyricsDocument> (std::move (result.document));
        lyricsView.setStatus (lyricsDocument->instrumental ? "Instrumental" : "Lyrics",
            lyricsDocument->instrumental ? "This recording has no vocals." : juce::String {});
        lyricsView.setDocument (lyricsDocument);
        lyricsView.setPlaybackPosition (controller.getCurrentPositionSeconds() + lyricsOffsetSeconds);
    }
    else
    {
        lyricsView.setDocument ({});
        lyricsView.setStatus (result.state == LyricsLoadResult::State::notFound ? "No lyrics found" : "Lyrics unavailable",
            result.error.isNotEmpty() ? result.error : "Use Lyrics options to search online or import an LRC file.");
    }
    updateLyricsControls();
}

LyricsService::LoadCallback MainComponent::lyricsActionCallback (const TrackItem& track)
{
    return [safe = juce::Component::SafePointer<MainComponent> (this), key = LyricsService::trackKey (track),
            revision = lyricsOffsetRevision] (LyricsLoadResult result)
    {
        if (! safe) return;
        const auto current = safe->lyricsTrack && key == LyricsService::trackKey (*safe->lyricsTrack);
        if (current) safe->lyricsLoading = false;
        if (result.error.isNotEmpty())
        {
            safe->showStatus (result.error, true);
            if (result.state != LyricsLoadResult::State::ready || safe->lyricsDocument)
            {
                if (current && ! safe->lyricsDocument)
                    safe->lyricsView.setStatus ("Lyrics unavailable", result.error);
                return;
            }
        }
        safe->applyLyricsResult (key, std::move (result), revision);
    };
}

void MainComponent::chooseLyricsFile (TrackItem track)
{
    lyricsFileChooser = std::make_unique<juce::FileChooser> ("Import lyrics", track.file.getParentDirectory(), "*.lrc;*.txt");
    lyricsFileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent> (this), track] (const auto& chooser)
        {
            if (safe && chooser.getResult() != juce::File {})
                safe->lyricsService->importFile (track, chooser.getResult(), safe->lyricsActionCallback (track));
        });
}

void MainComponent::searchLyrics (TrackItem track)
{
    const auto key = LyricsService::trackKey (track);
    if (! lyricsTrack || key != LyricsService::trackKey (*lyricsTrack)) return;
    lyricsLoading = false;
    if (! lyricsDocument) lyricsView.setStatus ("Searching online", track.title);
    lyricsService->search (track, {},
        [safe = juce::Component::SafePointer<MainComponent> (this), track, key] (LyricsSearchResponse response)
        {
            if (! safe || ! safe->lyricsTrack || key != LyricsService::trackKey (*safe->lyricsTrack)) return;
            if (response.error.isNotEmpty())
            {
                safe->showStatus (response.error, true);
                if (! safe->lyricsDocument) safe->lyricsView.setStatus ("Search unavailable", response.error);
                return;
            }
            if (response.results.empty())
            {
                if (! safe->lyricsDocument) safe->lyricsView.setStatus ("No lyrics found", "Import an LRC file from Lyrics options.");
                safe->showStatus ("No matching lyrics found. You can import an LRC file.");
                return;
            }
            if (! safe->lyricsDocument) safe->lyricsView.setStatus ("Choose a recording", "Select a matching result, or import an LRC file.");
            juce::PopupMenu menu;
            menu.setLookAndFeel (&safe->lyricsActionsButton.getLookAndFeel());
            for (size_t index = 0; index < response.results.size(); ++index)
            {
                const auto& candidate = response.results[index];
                const auto seconds = juce::roundToInt (candidate.durationSeconds);
                auto caption = candidate.title + juce::String::fromUTF8 (" — ") + candidate.artist + "  (" + juce::String (seconds / 60)
                    + ":" + juce::String (seconds % 60).paddedLeft ('0', 2) + ")";
                if (candidate.document.instrumental) caption += juce::String::fromUTF8 (" · Instrumental");
                else if (! candidate.document.isSynced()) caption += juce::String::fromUTF8 (" · Plain text");
                menu.addItem (static_cast<int> (index + 1), caption);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&safe->lyricsActionsButton),
                [safe, track, candidates = std::move (response.results)] (int choice) mutable
                {
                    if (safe && juce::isPositiveAndBelow (choice - 1, static_cast<int> (candidates.size())))
                        safe->lyricsService->chooseResult (track, std::move (candidates[static_cast<size_t> (choice - 1)]),
                                                           safe->lyricsActionCallback (track));
                });
        });
}

void MainComponent::changeLyricsOffset (TrackItem track, double seconds)
{
    const auto key = LyricsService::trackKey (track);
    const auto current = lyricsTrack && key == LyricsService::trackKey (*lyricsTrack);
    const auto previous = lyricsOffsetSeconds;
    const auto revision = current ? ++lyricsOffsetRevision : lyricsOffsetRevision;
    if (current)
    {
        lyricsOffsetSeconds = seconds;
        lyricsView.setPlaybackPosition (controller.getCurrentPositionSeconds() + seconds);
    }
    lyricsService->setTimingOffset (track, seconds,
        [safe = juce::Component::SafePointer<MainComponent> (this), key, previous, revision] (juce::Result result)
        {
            if (! safe || result.wasOk()) return;
            safe->showStatus (result.getErrorMessage(), true);
            if (safe->lyricsOffsetRevision == revision && safe->lyricsTrack && key == LyricsService::trackKey (*safe->lyricsTrack))
                safe->lyricsOffsetSeconds = previous;
        });
}

void MainComponent::showLyricsActions()
{
    if (! lyricsTrack) return;
    const auto track = *lyricsTrack;
    const auto offset = lyricsOffsetSeconds;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lyricsActionsButton.getLookAndFeel());
    if (lyricsDocument && lyricsDocument->sourceLabel.isNotEmpty())
    {
        menu.addItem (99, lyricsDocument->sourceLabel, false);
        menu.addSeparator();
    }
    menu.addItem (1, "Find lyrics online...");
    menu.addItem (2, "Import LRC or text...");
    menu.addItem (3, "Reload lyrics", ! lyricsLoading);
    menu.addItem (4, "Use automatic lyrics");
    menu.addSeparator();
    menu.addItem (98, "Timing: " + juce::String (offset, 2) + " s", false);
    menu.addItem (5, "Show lyrics earlier (+0.25 s)", lyricsDocument && lyricsDocument->isSynced());
    menu.addItem (6, "Show lyrics later (-0.25 s)", lyricsDocument && lyricsDocument->isSynced());
    menu.addItem (7, "Reset timing", ! juce::exactlyEqual (offset, 0.0));
    menu.addSeparator();
    menu.addItem (8, "Find online automatically", true, lyricsService->isOnlineLookupEnabled());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lyricsActionsButton),
        [safe = juce::Component::SafePointer<MainComponent> (this), track, offset] (int action)
        {
            if (! safe) return;
            switch (action)
            {
                case 1: safe->searchLyrics (track); break;
                case 2: safe->chooseLyricsFile (track); break;
                case 3: safe->reloadLyrics (true); break;
                case 4: safe->lyricsService->reset (track, safe->lyricsActionCallback (track)); break;
                case 5: safe->changeLyricsOffset (track, offset + 0.25); break;
                case 6: safe->changeLyricsOffset (track, offset - 0.25); break;
                case 7: safe->changeLyricsOffset (track, 0.0); break;
                case 8:
                {
                    const auto result = safe->lyricsService->setOnlineLookupEnabled (! safe->lyricsService->isOnlineLookupEnabled());
                    if (result.failed()) safe->showStatus (result.getErrorMessage(), true);
                    else safe->reloadLyrics();
                    break;
                }
                default: break;
            }
        });
}
