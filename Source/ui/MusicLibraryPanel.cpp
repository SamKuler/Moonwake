#include "MusicLibraryPanel.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <unordered_set>
#include <utility>

class MusicLibraryPanel::TrackTable final : public SkinTableListBox,
                                            public juce::DragAndDropTarget
{
public:
    explicit TrackTable (MusicLibraryPanel& panel) : owner (panel) {}
    bool keyPressed (const juce::KeyPress& key) override
    {
        return owner.handleKey (key) || SkinTableListBox::keyPressed (key);
    }
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return owner.canReorder()
            && details.description.getProperty ("collection", {}).toString() == owner.collectionId
            && details.description.getProperty ("moonwakeTracks", false);
    }
    void itemDragMove (const SourceDetails& details) override
    {
        const auto point = getViewport()->getLocalPoint (this, details.localPosition);
        getViewport()->autoScroll (point.x, point.y, 20, 8);
        auto row = getRowContainingPosition (details.localPosition.x, details.localPosition.y);
        if (row < 0) row = getNumRows();
        else if (details.localPosition.y > getRowPosition (row, true).getCentreY()) ++row;
        owner.dropBeforeRow = juce::jlimit (0, getNumRows(), row);
        repaint();
    }
    void itemDragExit (const SourceDetails&) override { owner.dropBeforeRow = -1; repaint(); }
    void itemDropped (const SourceDetails& details) override
    {
        if (! isInterestedInDragSource (details)) return;
        itemDragMove (details);
        std::vector<juce::String> ids;
        const auto entries = details.description.getProperty ("entries", {});
        if (const auto* values = entries.getArray())
            for (const auto& value : *values) ids.push_back (value.toString());
        const auto before = owner.dropBeforeRow;
        owner.dropBeforeRow = -1;
        owner.changed (owner.controller.moveCollectionEntries (owner.collectionId, ids, before));
    }
private:
    MusicLibraryPanel& owner;
};

MusicLibraryPanel::MusicLibraryPanel (PlayerController& owner, JuceSkinAdapter& skin)
    : controller (owner), adapter (skin), table (std::make_unique<TrackTable> (*this))
{
    table->setName ("Music collection tracks");
    table->setModel (this);
    table->setOutlineThickness (0);
    table->setMultipleSelectionEnabled (true);
    table->getHeader().addColumn ("Track", 1, 240, 80, -1);
    table->getHeader().addColumn ("Artist / album", 2, 320, 100, -1);
    table->getHeader().addColumn ("Time", 3, 80, 40, 120);
    table->getHeader().addColumn ("Album", 4, 240, 80, -1);
    adapter.configureListBox (*table);
    collections.setName ("Browse library and playlists");
    collections.setTooltip ("All songs, favourites, playlists, albums, artists and the play queue");
    collections.onChange = [this]
    {
        if (refreshing || renameId.isNotEmpty()) return;
        const auto index = collections.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (index, static_cast<int> (collectionItems.size())))
            selectCollection (collectionItems[static_cast<size_t> (index)].id);
    };
    actions.setName ("Collection and track actions");
    actions.onClick = [this] { showActions(); };
    create.onClick = [this]
    {
        finishRename (true);
        juce::PopupMenu menu;
        menu.setLookAndFeel (&adapter);
        menu.addItem (20, "Playlist");
        menu.addItem (42, "Smart playlist...");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&create),
            [safe = juce::Component::SafePointer<juce::ComboBox> (&collections), this] (int result)
            { if (safe != nullptr && result != 0) performAction (result, {}, {}, collectionId); });
    };
    libraryOpen.onClick = back.onClick = [this] { showLibrary(); };
    playerOpen.onClick = [this] { showPlayer(); };
    playerOpen.setTooltip ("Show the current track and playback workspace");
    browseQueue.setTooltip ("View, reorder and remove tracks in the play queue");
    browseFavourites.setTooltip ("Show your favourite songs");
    for (auto* button : { &libraryOpen, &playerOpen, &browsePlaylists, &browseAlbums, &browseArtists })
        button->setClickingTogglesState (true);
    browsePlaylists.onClick = [this] { setCategory ("playlists"); };
    browseAlbums.onClick = [this] { setCategory ("albums"); };
    browseArtists.onClick = [this] { setCategory ("artists"); };
    browseSongs.onClick = [this] { selectCollection ("all"); };
    browseFavourites.onClick = [this] { selectCollection ("favourites"); };
    browseQueue.onClick = [this] { if (onShowQueue) onShowQueue(); };
    changeArtwork.onClick = [this]
    { if (onChoosePlaylistArtwork && collectionId.startsWith ("playlist:")) onChoosePlaylistArtwork (collectionId.substring (9)); };
    play.onClick = [this] { playCollection (false); };
    shufflePlay.onClick = [this] { playCollection (true); };
    gridSearch.setTitle ("Search playlists, albums and artists");
    gridSearch.setTextToShowWhenEmpty ("Search playlists", adapter.findColour (juce::TextEditor::textColourId));
    gridSearch.onTextChange = [this] { grid.setQuery (gridSearch.getText()); updateVisibility(); };
    grid.onOpenCollection = [this] (const auto& id) { selectCollection (id); };
    grid.onContextMenu = [this] (const auto& id) { showCollectionActions (id); };
    detailCover.addMouseListener (this, false);
    title.addMouseListener (this, false);
    empty.setJustificationType (juce::Justification::centred);
    empty.setInterceptsMouseClicks (false, false);
    refresh();
}

MusicLibraryPanel::~MusicLibraryPanel()
{
    detailCover.removeMouseListener (this);
    title.removeMouseListener (this);
    artworkJobs.removeAllJobs (true, -1);
    table->setModel (nullptr);
}

void MusicLibraryPanel::addTo (juce::Component& parent)
{
    for (auto* component : std::initializer_list<juce::Component*> {
             table.get(), &collections, &actions, &create, &title, &count, &empty,
             &grid, &detailCover, &libraryOpen, &playerOpen, &back, &description,
             &browsePlaylists, &browseAlbums, &browseArtists, &browseSongs,
             &browseFavourites, &browseQueue, &play, &shufflePlay, &gridSearch, &changeArtwork })
        parent.addAndMakeVisible (component);
}

void MusicLibraryPanel::bind (SkinComponentBindings& bindings, SkinValidationReport& report)
{
    bindings.bind ("library.collections", collections, report);
    bindings.bind ("library.actions", actions, report);
    bindings.bind ("library.newPlaylist", create, report);
    bindings.bind ("library.title", title, report);
    bindings.bind ("library.count", count, report);
    bindings.bind ("library.empty", empty, report);
    bindings.bind ("library.tracks", *table, report);
    for (const auto& [id, component] : std::initializer_list<std::pair<const char*, juce::Component*>> {
             { "library.grid", &grid }, { "library.detailCover", &detailCover },
             { "library.open", &libraryOpen }, { "player.open", &playerOpen },
             { "library.back", &back }, { "library.description", &description },
             { "library.browsePlaylists", &browsePlaylists }, { "library.browseAlbums", &browseAlbums },
             { "library.browseArtists", &browseArtists }, { "library.browseSongs", &browseSongs },
             { "library.browseFavourites", &browseFavourites }, { "library.browseQueue", &browseQueue },
             { "library.play", &play }, { "library.shufflePlay", &shufflePlay },
             { "library.gridSearch", &gridSearch }, { "library.changeArtwork", &changeArtwork } })
        bindings.bind (id, *component, report);
}

void MusicLibraryPanel::setEnabled (bool enabled)
{
    for (auto* component : std::initializer_list<juce::Component*> {
             table.get(), &collections, &actions, &create, &grid, &libraryOpen, &playerOpen,
             &back, &browsePlaylists, &browseAlbums, &browseArtists, &browseSongs,
             &browseFavourites, &browseQueue, &play, &shufflePlay, &gridSearch, &changeArtwork }) component->setEnabled (enabled);
}

bool MusicLibraryPanel::isActive() const
{
    return (table->isVisible() && ! table->getBounds().isEmpty())
        || (grid.isVisible() && ! grid.getBounds().isEmpty());
}
juce::Component& MusicLibraryPanel::tableControl() { return *table; }

void MusicLibraryPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    overviewNeedsRefresh = true;
    detailSummary.reset();
    collectionId = controller.getLibraryCollectionId();
    const auto availableCollections = libraryCollections (controller.getLibrary());
    auto selected = std::find_if (availableCollections.begin(), availableCollections.end(), [this] (const auto& item)
    {
        return item.id == collectionId;
    });
    if (selected == availableCollections.end())
    {
        collectionId = "all";
        controller.setLibraryBrowseState (collectionId, controller.getLibrarySearchQuery());
        selected = availableCollections.begin();
    }
    if (renameId.isEmpty())
    {
        if (availableCollections != collectionItems)
        {
            collections.hidePopup();
            collectionItems = availableCollections;
            collections.clear (juce::dontSendNotification);
            juce::String section;
            for (size_t i = 0; i < collectionItems.size(); ++i)
            {
                const auto& item = collectionItems[i];
                if (item.section != section && item.section.isNotEmpty()) collections.addSectionHeading (item.section);
                section = item.section;
                collections.addItem (item.name, static_cast<int> (i + 1));
            }
        }
        collections.setSelectedId (static_cast<int> (std::distance (availableCollections.begin(), selected)) + 1,
                                   juce::dontSendNotification);
    }
    title.setText (currentPage == Page::overview ? "Library" : selected->name, juce::dontSendNotification);
    tracks = libraryTracks (controller.getLibrary(), controller.getPlaylist(), collectionId);
    applyViewSort();
    refreshRows();
    if (currentPage == Page::overview && grid.isVisible() && ! grid.getBounds().isEmpty()) refreshOverview();
    refreshDescription();
}

void MusicLibraryPanel::refreshRows()
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    filter.setQuery (controller.getLibrarySearchQuery());
    filter.rebuild (tracks);
    table->updateContent();
    restoreSelection();
    bool playable = false;
    for (int row = 0; row < filter.visibleCount() && ! playable; ++row)
        if (const auto* track = trackAtRow (row)) playable = track->available;
    play.setEnabled (table->isEnabled() && playable);
    shufflePlay.setEnabled (play.isEnabled());
    count.setText (juce::String (filter.visibleCount()) + (filter.visibleCount() == 1 ? " track" : " tracks"),
                   juce::dontSendNotification);
    empty.setText (tracks.empty() ? (collectionId == "queue" ? "Your play queue is empty" : "No songs in this collection")
                                 : "No matching songs", juce::dontSendNotification);
    updateVisibility();
    updatePlayback();
    table->repaint();
}

void MusicLibraryPanel::updateVisibility()
{
    const auto placed = [] (const juce::Component& component)
    {
        return ! component.getBounds().isEmpty() && component.getParentComponent() != nullptr;
    };
    const auto overview = grid.isVisible() && ! grid.getBounds().isEmpty();
    const auto detailHeading = detailCover.isVisible() && placed (detailCover)
        && placed (title) && placed (collections);
    if (detailHeading)
    {
        title.setVisible (renameId.isEmpty());
        collections.setVisible (renameId.isNotEmpty());
    }
    if (overview)
    {
        const auto size = grid.visibleCollectionCount();
        count.setText (juce::String (size) + " " + (size == 1 ? category.dropLastCharacters (1) : category),
                       juce::dontSendNotification);
        empty.setText (gridSearch.getText().isEmpty() ? "No " + category + " yet\n"
            + (category == "playlists" ? "Create a playlist or import an M3U file" : "Add music to build your library")
            : "No matching " + category, juce::dontSendNotification);
        empty.setVisible (placed (empty) && size == 0);
    }
    else empty.setVisible (placed (empty) && table->isVisible() && filter.visibleCount() == 0);
}

void MusicLibraryPanel::setQuery (const juce::String& query)
{
    controller.setLibraryBrowseState (controller.getLibraryCollectionId(), query);
    refreshRows();
}

void MusicLibraryPanel::selectCollection (const juce::String& id)
{
    if (id == "queue") { if (onShowQueue) onShowQueue(); return; }
    if (currentPage == Page::collection && table->isVisible())
        scrollByCollection[collectionId] = table->getViewport()->getViewPositionY();
    finishRename (false);
    controller.setLibraryBrowseState (id, id == controller.getLibraryCollectionId()
        ? controller.getLibrarySearchQuery() : juce::String {});
    currentPage = Page::collection;
    refresh();
    if (onPageChanged) onPageChanged();
    table->getViewport()->setViewPosition (0, scrollByCollection[id]);
    if (onChanged) onChanged();
}

void MusicLibraryPanel::updatePlayback()
{
    const auto* current = controller.getPlaylist().getSelectedTrack();
    if (current != nullptr && current->id != controller.getLoadedTrackId()) current = nullptr;
    const auto id = current != nullptr ? current->id : juce::String{};
    const auto libraryId = current != nullptr ? current->libraryId : juce::String{};
    if (id != playingQueueId || libraryId != playingLibraryId)
    {
        prunePlaybackOrigins();
        playingQueueId = id;
        playingLibraryId = libraryId;
        table->repaint();
    }
}

bool MusicLibraryPanel::canStartPlayback() const
{
    if (const auto* selected = trackAtRow (table->getSelectedRow())) return selected->available;
    for (int row = 0; row < filter.visibleCount(); ++row)
        if (const auto* track = trackAtRow (row); track != nullptr && track->available) return true;
    return false;
}

bool MusicLibraryPanel::startPlayback()
{
    auto row = table->getSelectedRow();
    if (row < 0)
        for (int i = 0; i < filter.visibleCount(); ++i)
            if (const auto* track = trackAtRow (i); track != nullptr && track->available) { row = i; break; }
    if (row < 0) return false;
    playFromRow (row);
    return controller.isPlaying();
}

void MusicLibraryPanel::finishEditing() { finishRename (true); }

int MusicLibraryPanel::getNumRows() { return filter.visibleCount(); }
const TrackItem* MusicLibraryPanel::trackAtRow (int row) const
{
    const auto index = filter.playlistIndexForRow (row);
    return juce::isPositiveAndBelow (index, static_cast<int> (tracks.size())) ? &tracks[static_cast<size_t> (index)] : nullptr;
}

void MusicLibraryPanel::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    const auto* track = trackAtRow (row);
    if (track == nullptr) return;
    bool playing = collectionId == "queue" ? track->id == playingQueueId
        : (! collectionId.startsWith ("playlist:") && track->libraryId == playingLibraryId);
    if (collectionId != "queue")
        if (const auto origin = playbackOrigins.find (playingQueueId);
            origin != playbackOrigins.end() && origin->second.collectionId == collectionId)
            playing = origin->second.entryId == track->id;
    const auto seconds = static_cast<int> (std::round (track->durationSeconds));
    const auto duration = seconds > 0 ? juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2) : "--";
    const auto dropEdge = row == dropBeforeRow ? -1 : row == getNumRows() - 1 && dropBeforeRow == getNumRows() ? 1 : 0;
    adapter.drawListBoxRow (g, *table, row, width, height, selected,
        track->title + (track->available ? juce::String{} : " (Missing)"), track->file.getFullPathName(), duration,
        table->rowGeometry (height), false, false, track->available, track->artist, track->album,
        playing, controller.getLibrary().isFavourite (track->libraryId), dropEdge);
}

juce::String MusicLibraryPanel::getCellTooltip (int row, int)
{
    if (const auto* track = trackAtRow (row)) return track->title + "\n" + track->artist + "\n" + track->file.getFullPathName();
    return {};
}

void MusicLibraryPanel::selectedRowsChanged (int)
{
    if (refreshing) return;
    auto& ids = selectionByCollection[collectionId];
    ids.clear();
    for (const auto& entry : selectedEntries()) ids.push_back (entry.entryId);
}

std::vector<MusicLibraryPanel::SelectedEntry> MusicLibraryPanel::selectedEntries() const
{
    std::vector<SelectedEntry> entries;
    const auto selectedRows = table->getSelectedRows();
    for (int i = 0; i < selectedRows.size(); ++i)
        if (const auto* track = trackAtRow (selectedRows[i])) entries.push_back ({ track->id, track->libraryId });
    return entries;
}

void MusicLibraryPanel::playFromRow (int row)
{
    const auto* requested = trackAtRow (row);
    if (requested == nullptr) return;
    if (collectionId == "queue")
    {
        changed (controller.selectTrack (controller.getPlaylist().indexOfId (requested->id)) && controller.setPlaying (true));
        return;
    }
    std::vector<juce::String> ids, origins;
    for (int i = 0; i < getNumRows(); ++i)
        if (const auto* track = trackAtRow (i)) { ids.push_back (track->libraryId); origins.push_back (track->id); }
    const auto started = controller.playLibraryTracks (ids, row);
    if (started) recordPlaybackOrigins (collectionId, origins);
    else prunePlaybackOrigins();
    changed (started);
}

void MusicLibraryPanel::prunePlaybackOrigins()
{
    if (playbackOrigins.empty()) return;
    std::unordered_set<std::string> present;
    for (const auto& track : controller.getPlaylist().getTracks()) present.insert (track.id.toStdString());
    std::erase_if (playbackOrigins, [&] (const auto& entry) { return ! present.contains (entry.first.toStdString()); });
}

void MusicLibraryPanel::recordPlaybackOrigins (
    const juce::String& origin, const std::vector<juce::String>& entries,
    const std::unordered_set<std::string>& previousQueueIds)
{
    prunePlaybackOrigins();
    size_t index = 0;
    for (const auto& track : controller.getPlaylist().getTracks())
        if (! previousQueueIds.contains (track.id.toStdString()) && index < entries.size())
            playbackOrigins[track.id] = { origin, entries[index++] };
}

void MusicLibraryPanel::cellDoubleClicked (int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu()) playFromRow (row);
}
void MusicLibraryPanel::cellClicked (int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu()) return;
    if (! table->isRowSelected (row)) table->selectRow (row);
    std::vector<TrackItem> selected;
    const auto rows = table->getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (const auto* track = trackAtRow (rows[i])) selected.push_back (*track);
    showActionsForTracks (std::move (selected), collectionId, *table, false);
}

void MusicLibraryPanel::showQueueActions (juce::Component& anchor)
{
    showActionsForTracks ({}, "queue", anchor, false);
}

void MusicLibraryPanel::showQueueTrackActions (const TrackItem& track, juce::Component& anchor)
{
    showActionsForTracks ({ track }, "queue", anchor, false);
}

bool MusicLibraryPanel::isSmartCollection() const
{
    const auto* saved = collectionId.startsWith ("playlist:")
        ? controller.getLibrary().getPlaylist (collectionId.substring (9)) : nullptr;
    return saved != nullptr && saved->smartRules.has_value();
}

bool MusicLibraryPanel::canReorder() const
{
    return filter.query().isEmpty() && ! isSmartCollection()
        && (collectionId == "queue" || collectionId.startsWith ("playlist:"));
}

juce::var MusicLibraryPanel::getDragSourceDescription (const juce::SparseSet<int>&)
{
    if (! canReorder()) return {};
    auto* value = new juce::DynamicObject;
    value->setProperty ("moonwakeTracks", true);
    value->setProperty ("collection", collectionId);
    juce::Array<juce::var> ids;
    for (const auto& entry : selectedEntries()) ids.add (entry.entryId);
    value->setProperty ("entries", ids);
    return juce::var (value);
}

void MusicLibraryPanel::sortOrderChanged (int column, bool ascending)
{
    const auto key = column == 2 ? "artist" : column == 3 ? "duration" : column == 4 ? "album" : "title";
    if (! isSmartCollection() && (collectionId == "queue" || collectionId.startsWith ("playlist:")))
        changed (controller.sortCollectionEntries (collectionId, key, ! ascending));
    else
    {
        viewSorts[collectionId] = { column, ascending };
        refresh();
    }
}

void MusicLibraryPanel::applyViewSort()
{
    const auto sort = viewSorts.find (collectionId);
    if (sort == viewSorts.end()) return;
    const auto [column, ascending] = sort->second;
    std::stable_sort (tracks.begin(), tracks.end(), [column, ascending] (const auto& a, const auto& b)
    {
        const auto result = column == 3 ? (a.durationSeconds < b.durationSeconds ? -1 : a.durationSeconds > b.durationSeconds ? 1 : 0)
            : (column == 2 ? a.artist : column == 4 ? a.album : a.title).compareNatural (column == 2 ? b.artist : column == 4 ? b.album : b.title);
        return ascending ? result < 0 : result > 0;
    });
}

void MusicLibraryPanel::restoreSelection()
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    auto& ids = selectionByCollection[collectionId];
    if (! ids.empty())
    {
        std::unordered_set<std::string> present;
        for (const auto& track : tracks) present.insert (track.id.toStdString());
        std::erase_if (ids, [&present] (const auto& id) { return ! present.contains (id.toStdString()); });
    }
    juce::SparseSet<int> rows;
    std::unordered_set<std::string> selected;
    for (const auto& id : ids) selected.insert (id.toStdString());
    for (int row = 0; row < getNumRows(); ++row)
        if (const auto* track = trackAtRow (row))
            if (selected.contains (track->id.toStdString())) rows.addRange ({ row, row + 1 });
    table->setSelectedRows (rows, juce::dontSendNotification);
}

void MusicLibraryPanel::changed (bool success)
{
    if (onStatus && ! success) onStatus (controller.getLastStatusMessage(), true);
    if (onChanged) onChanged();
    else refresh();
}

void MusicLibraryPanel::newPlaylist()
{
    finishRename (true);
    const auto id = controller.createSavedPlaylist ("New playlist");
    if (id.isEmpty()) { changed (false); return; }
    selectCollection ("playlist:" + id);
    beginRename();
}

void MusicLibraryPanel::beginRename()
{
    if (! collectionId.startsWith ("playlist:")) return;
    if (const auto* playlist = controller.getLibrary().getPlaylist (collectionId.substring (9)))
    {
        renameId = playlist->id;
        const auto generation = ++renameGeneration;
        updateVisibility();
        collections.setEditableText (true);
        for (auto* child : collections.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
            {
                label->onEditorShow = [this, label]
                {
                    if (auto* editor = label->getCurrentTextEditor()) adapter.configureTextEditor (*editor);
                };
                label->onEditorHide = [safe = juce::Component::SafePointer<juce::ComboBox> (&collections), this, generation]
                {
                    juce::MessageManager::callAsync ([safe, this, generation]
                    {
                        if (safe != nullptr && renameGeneration == generation) finishRename (true);
                    });
                };
            }
        collections.showEditor();
    }
}

void MusicLibraryPanel::finishRename (bool commit)
{
    if (renameId.isEmpty()) return;
    ++renameGeneration;
    const auto id = std::exchange (renameId, {});
    auto name = collections.getText().trim();
    for (auto* child : collections.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            if (const auto* editor = label->getCurrentTextEditor()) name = editor->getText().trim();
    if (const auto* saved = controller.getLibrary().getPlaylist (id);
        commit && saved != nullptr && saved->name != name)
        if (! controller.renameSavedPlaylist (id, name))
            if (onStatus) onStatus (controller.getLastStatusMessage(), true);
    collections.setEditableText (false);
    collectionItems.clear();
    refresh();
}

void MusicLibraryPanel::removeSelection()
{
    if (isSmartCollection()) return;
    std::vector<juce::String> ids;
    for (const auto& entry : selectedEntries()) ids.push_back (entry.entryId);
    if (! ids.empty()) changed (controller.removeCollectionEntries (collectionId, ids));
}

bool MusicLibraryPanel::handleKey (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey) { playFromRow (table->getSelectedRow()); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (collectionId == "queue" || collectionId == "favourites" || collectionId.startsWith ("playlist:")) removeSelection();
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
    {
        changed (controller.undoCollectionEdit (collectionId));
        return true;
    }
    return false;
}

void MusicLibraryPanel::showActions()
{
    if (currentPage == Page::overview && grid.isVisible() && ! grid.getBounds().isEmpty())
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&adapter);
        menu.addItem (20, "New playlist");
        menu.addItem (42, "New smart playlist...");
        menu.addItem (40, juce::String::fromUTF8 ("Import M3U / M3U8…"));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&actions),
            [safe = juce::Component::SafePointer<juce::ComboBox> (&collections), this] (int result)
            {
                if (safe == nullptr) return;
                if (result == 20) newPlaylist();
                if (result == 42 && onEditSmartPlaylist) onEditSmartPlaylist ({});
                if (result == 40 && onImportPlaylist) onImportPlaylist();
            });
        return;
    }
    std::vector<TrackItem> selected;
    const auto rows = table->getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (const auto* track = trackAtRow (rows[i])) selected.push_back (*track);
    showActionsForTracks (std::move (selected), collectionId, actions, true);
}

void MusicLibraryPanel::showActionsForTracks (std::vector<TrackItem> selected, juce::String origin,
                                             juce::Component& anchor, bool includeCollectionActions)
{
    std::vector<juce::String> libraryIds, entryIds;
    for (const auto& entry : selected)
    {
        libraryIds.push_back (entry.libraryId);
        entryIds.push_back (entry.id);
    }
    const auto selection = ! libraryIds.empty();
    const auto saved = origin.startsWith ("playlist:");
    const auto* selectedPlaylist = saved ? controller.getLibrary().getPlaylist (origin.substring (9)) : nullptr;
    const auto smart = selectedPlaylist != nullptr && selectedPlaylist->smartRules.has_value();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&adapter);
    std::vector<juce::String> playlistIds;
    if (selection || origin != "queue")
    {
        menu.addItem (1, "Play selected", selection);
        if (origin != "queue")
        {
            menu.addItem (2, "Play next", selection);
            menu.addItem (3, "Add to end of queue", selection);
        }
        menu.addSeparator();
        bool allFavourite = selection;
        for (const auto& id : libraryIds) allFavourite = allFavourite && controller.getLibrary().isFavourite (id);
        menu.addItem (allFavourite ? 5 : 4, allFavourite ? "Remove from favourites" : "Add to favourites", selection);
        juce::PopupMenu playlists;
        for (const auto& playlist : controller.getLibrary().getPlaylists())
        {
            if (playlist.smartRules) continue;
            playlistIds.push_back (playlist.id);
            playlists.addItem (1000 + static_cast<int> (playlistIds.size()) - 1, playlist.name);
        }
        if (! playlistIds.empty()) playlists.addSeparator();
        playlists.addItem (9, juce::String::fromUTF8 ("New playlist…"));
        menu.addSubMenu ("Add to playlist", playlists, selection);
        const auto removableQueue = origin == "queue" && std::none_of (entryIds.begin(), entryIds.end(),
            [this] (const auto& id) { return id == controller.getLoadedTrackId(); });
        if (! smart)
        {
            menu.addItem (6, origin == "queue" ? "Remove from play queue" : "Remove selected entries",
                          selection && (saved || removableQueue || origin == "favourites"));
            if (origin != "queue") menu.addItem (7, "Undo list edit", controller.canUndoCollectionEdit (origin));
        }
        menu.addItem (8, juce::String::fromUTF8 ("Locate missing file…"), selection && libraryIds.size() == 1
                      && controller.getLibrary().getTrack (libraryIds.front()) != nullptr
                      && ! controller.getLibrary().getTrack (libraryIds.front())->available);
    }
    if (includeCollectionActions)
    {
        menu.addSeparator();
        menu.addItem (20, "New playlist");
        menu.addItem (42, "New smart playlist...");
        menu.addItem (21, "Rename playlist", saved);
        menu.addItem (22, "Duplicate playlist", saved);
        menu.addItem (27, "Change cover...", saved);
        const auto* savedPlaylist = controller.getLibrary().getPlaylist (origin.substring (9));
        menu.addItem (28, "Use automatic cover", savedPlaylist != nullptr && savedPlaylist->artworkCacheKey.isNotEmpty());
        juce::PopupMenu deletion;
        if (const auto* playlist = controller.getLibrary().getPlaylist (origin.substring (9)))
            deletion.addItem (23, "Delete \"" + playlist->name + "\"");
        menu.addSubMenu ("Delete playlist", deletion, saved);
        menu.addItem (24, "Save queue as playlist", ! controller.getPlaylist().isEmpty());
        menu.addItem (25, "Move playlist up", saved);
        menu.addItem (26, "Move playlist down", saved);
        juce::PopupMenu sorting;
        sorting.addItem (30, "Title"); sorting.addItem (31, "Artist");
        sorting.addItem (32, "Album"); sorting.addItem (33, "Duration");
        if (! smart) menu.addSubMenu ("Sort by", sorting, saved || origin == "queue");
        if (smart)
        {
            menu.addItem (43, "Edit rules...");
            menu.addItem (44, "Save as regular playlist");
        }
        menu.addSeparator();
        menu.addItem (40, juce::String::fromUTF8 ("Import M3U / M3U8…"));
        menu.addItem (41, juce::String::fromUTF8 ("Export M3U8…"), ! tracks.empty());
    }
    if (origin == "queue")
    {
        if (selection)
        {
            menu.addSeparator();
            menu.addItem (52, "Move up", entryIds.size() == 1 && controller.canMoveUpcomingQueueEntry (entryIds.front(), -1));
            menu.addItem (53, "Move down", entryIds.size() == 1 && controller.canMoveUpcomingQueueEntry (entryIds.front(), 1));
        }
        menu.addSeparator();
        const auto nonempty = ! controller.getPlaylist().isEmpty();
        menu.addItem (7, "Undo list edit", controller.canUndoCollectionEdit ("queue"));
        menu.addItem (24, "Save queue as playlist", nonempty);
        menu.addItem (41, "Export queue...", nonempty);
        juce::PopupMenu clear;
        clear.addItem (10, "Clear play queue", nonempty);
        menu.addSubMenu ("Clear queue", clear, nonempty);
    }
    const auto options = juce::PopupMenu::Options().withTargetComponent (&anchor);
    menu.showMenuAsync (includeCollectionActions || ! selection ? options : options.withMousePosition(),
        [safe = juce::Component::SafePointer<juce::ComboBox> (&collections), this,
         libraryIds, entryIds, origin, playlistIds,
         exportTracks = origin == "queue" ? controller.getPlaylist().getTracks()
             : includeCollectionActions ? tracks : std::vector<TrackItem>{}] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 41)
            {
                if (onExportPlaylist) onExportPlaylist (exportTracks);
            }
            else if (result >= 1000 && juce::isPositiveAndBelow (result - 1000, static_cast<int> (playlistIds.size())))
                changed (controller.addTracksToSavedPlaylist (playlistIds[static_cast<size_t> (result - 1000)], libraryIds));
            else performAction (result, libraryIds, entryIds, origin);
        });
}

void MusicLibraryPanel::performAction (int action, std::vector<juce::String> ids,
                                       std::vector<juce::String> entries, juce::String origin)
{
    if (action == 1)
    {
        if (origin == "queue")
            changed (! entries.empty() && controller.selectTrack (controller.getPlaylist().indexOfId (entries.front()))
                     && controller.setPlaying (true));
        else
        {
            const auto started = controller.playLibraryTracks (ids);
            if (started) recordPlaybackOrigins (origin, entries);
            else prunePlaybackOrigins();
            changed (started);
        }
        return;
    }
    if (action == 2 || action == 3)
    {
        std::unordered_set<std::string> previousQueueIds;
        for (const auto& track : controller.getPlaylist().getTracks()) previousQueueIds.insert (track.id.toStdString());
        const auto queued = controller.enqueueLibraryTracks (ids, action == 2);
        recordPlaybackOrigins (origin, entries, previousQueueIds);
        changed (queued);
        return;
    }
    if (action == 4 || action == 5) { changed (controller.setTrackFavourites (ids, action == 4)); return; }
    if (action == 6 || action == 7)
    {
        const auto edited = action == 6 ? (origin == "queue" ? controller.removeUpcomingQueueEntries (entries)
                                                                          : controller.removeCollectionEntries (origin, entries))
                                        : controller.undoCollectionEdit (origin);
        if (origin == "queue") prunePlaybackOrigins();
        changed (edited);
        return;
    }
    if (action == 8) { if (! ids.empty() && onRelocateTrack) onRelocateTrack (ids.front()); return; }
    if (action == 9)
    {
        const auto id = controller.createSavedPlaylist ("New playlist");
        if (id.isNotEmpty() && controller.addTracksToSavedPlaylist (id, ids))
        {
            selectCollection ("playlist:" + id);
            beginRename();
        }
        else changed (false);
        return;
    }
    if (action == 10)
    {
        std::vector<juce::String> queueIds;
        for (const auto& track : controller.getPlaylist().getTracks()) queueIds.push_back (track.id);
        changed (controller.removeCollectionEntries ("queue", queueIds));
        return;
    }
    if (action == 52 || action == 53)
    {
        if (entries.size() == 1) changed (controller.moveUpcomingQueueEntry (entries.front(), action == 52 ? -1 : 1));
        return;
    }
    if (action == 20) { newPlaylist(); return; }
    if (action == 42 || action == 43)
    {
        if (onEditSmartPlaylist) onEditSmartPlaylist (action == 42 ? juce::String {} : origin.substring (9));
        return;
    }
    if (action == 44)
    {
        const auto id = controller.materializeSmartPlaylist (origin.substring (9));
        if (id.isNotEmpty()) selectCollection ("playlist:" + id); else changed (false);
        return;
    }
    if (action == 27) { if (onChoosePlaylistArtwork) onChoosePlaylistArtwork (origin.substring (9)); return; }
    if (action == 28) { changed (controller.resetPlaylistArtwork (origin.substring (9))); return; }
    if (action == 21) { selectCollection (origin); beginRename(); return; }
    if (action == 22)
    {
        const auto id = controller.duplicateSavedPlaylist (origin.substring (9));
        if (id.isNotEmpty()) selectCollection ("playlist:" + id); else changed (false);
        return;
    }
    if (action == 23) { changed (controller.deleteSavedPlaylist (origin.substring (9))); return; }
    if (action == 24)
    {
        const auto id = controller.saveQueueAsPlaylist ("Saved queue");
        if (id.isNotEmpty()) { selectCollection ("playlist:" + id); beginRename(); } else changed (false);
        return;
    }
    if (action == 25 || action == 26)
    {
        const auto& playlists = controller.getLibrary().getPlaylists();
        for (size_t i = 0; i < playlists.size(); ++i)
            if (playlists[i].id == origin.substring (9))
            {
                changed (controller.moveSavedPlaylist (playlists[i].id, static_cast<int> (i) + (action == 25 ? -1 : 1)));
                return;
            }
    }
    if (action >= 30 && action <= 33)
    {
        changed (controller.sortCollectionEntries (origin, juce::StringArray { "title", "artist", "album", "duration" }[action - 30]));
        return;
    }
    if (action == 40 && onImportPlaylist) onImportPlaylist();
}

void MusicLibraryPanel::showLibrary()
{
    if (currentPage == Page::collection) scrollByCollection[collectionId] = table->getViewport()->getViewPositionY();
    finishRename (true);
    currentPage = Page::overview;
    refreshOverview();
    if (onPageChanged) onPageChanged();
}

void MusicLibraryPanel::showPlayer()
{
    if (currentPage == Page::collection) scrollByCollection[collectionId] = table->getViewport()->getViewPositionY();
    finishRename (true);
    currentPage = Page::player;
    if (onPageChanged) onPageChanged();
}

void MusicLibraryPanel::setCategory (juce::String next)
{
    if (category != next) overviewNeedsRefresh = true;
    category = std::move (next);
    gridSearch.clear();
    showLibrary();
}

void MusicLibraryPanel::updateNavigation (bool supported)
{
    const auto favouritesOpen = supported && currentPage == Page::collection && collectionId == "favourites";
    libraryOpen.setToggleState (supported && currentPage != Page::player, juce::dontSendNotification);
    browseQueue.setToggleState (false, juce::dontSendNotification);
    browseFavourites.setToggleState (favouritesOpen, juce::dontSendNotification);
    playerOpen.setToggleState (! supported || currentPage == Page::player, juce::dontSendNotification);
    browsePlaylists.setToggleState (category == "playlists", juce::dontSendNotification);
    browseAlbums.setToggleState (category == "albums", juce::dontSendNotification);
    browseArtists.setToggleState (category == "artists", juce::dontSendNotification);
    if (supported && currentPage == Page::overview)
    {
        title.setText ("Library", juce::dontSendNotification);
        title.setTooltip ({});
        if (grid.isVisible()) refreshOverview();
    }
    updateVisibility();
    refreshDescription();
}

void MusicLibraryPanel::refreshOverview()
{
    if (overviewNeedsRefresh)
    {
        grid.setCollections (libraryCollectionSummaries (controller.getLibrary(), category),
            [this] (const auto& key) { return controller.resolveArtworkFile (key); });
        overviewNeedsRefresh = false;
    }
    gridSearch.setTextToShowWhenEmpty ("Search " + category,
        adapter.findColour (juce::TextEditor::textColourId).withAlpha (0.58f));
    grid.setQuery (gridSearch.getText());
    updateVisibility();
}

void MusicLibraryPanel::refreshDescription()
{
    changeArtwork.setEnabled (collectionId.startsWith ("playlist:") && controller.getLibrary().getPlaylist (collectionId.substring (9)) != nullptr);
    if (! detailSummary.has_value())
        detailSummary = libraryCollectionSummary (controller.getLibrary(), controller.getPlaylist(), collectionId);
    const auto& summary = *detailSummary;
    if (currentPage == Page::collection)
    {
        title.setText (summary.name, juce::dontSendNotification);
        title.setTooltip (summary.name);
    }
    const auto minutes = static_cast<int> (summary.durationSeconds / 60.0);
    auto text = juce::String (summary.trackCount) + (summary.trackCount == 1 ? " song" : " songs");
    if (summary.subtitle.isNotEmpty()) text = summary.subtitle + juce::String::fromUTF8 (" · ") + text;
    if (summary.durationSeconds > 0)
        text += juce::String::fromUTF8 (" · ") + (minutes >= 60
            ? juce::String (minutes / 60) + " hr " + juce::String (minutes % 60) + " min"
            : minutes > 0 ? juce::String (minutes) + " min" : "Under a minute");
    if (summary.createdAtMs > 0)
        text += juce::String::fromUTF8 (" · Created ") + juce::Time (summary.createdAtMs).formatted ("%b %d, %Y");
    description.setText (text, juce::dontSendNotification);
    if (! detailCover.isVisible() || detailCover.getBounds().isEmpty()) return;
    const auto key = collectionId + "|" + summary.artworkKeys.joinIntoString ("|");
    if (key == detailArtworkKey && summary.name == detailArtworkName) return;
    detailArtworkKey = key;
    detailArtworkName = summary.name;
    const auto generation = ++artworkGeneration;
    detailCover.setImage (summary.id, summary.name, {});
    std::vector<juce::File> files;
    for (const auto& artworkKey : summary.artworkKeys) files.push_back (controller.resolveArtworkFile (artworkKey));
    if (files.empty()) return;
    artworkJobs.removeAllJobs (true, 0);
    artworkJobs.addJob ([this, safe = juce::Component::SafePointer<juce::ComboBox> (&collections),
                        coverFiles = std::move (files), generation, id = summary.id, name = summary.name]
    {
        auto image = PlaylistGridComponent::buildCollectionCover (coverFiles);
        juce::MessageManager::callAsync ([this, safe, generation, id, name, cover = std::move (image)]
        {
            if (safe != nullptr && artworkGeneration == generation) detailCover.setImage (id, name, cover);
        });
    });
}

void MusicLibraryPanel::playCollection (bool shuffled)
{
    auto row = -1;
    std::vector<int> available;
    for (int i = 0; i < getNumRows(); ++i)
        if (const auto* track = trackAtRow (i); track != nullptr && track->available) available.push_back (i);
    if (! available.empty())
        row = available[shuffled ? static_cast<size_t> (juce::Random::getSystemRandom().nextInt (static_cast<int> (available.size()))) : 0];
    if (row >= 0)
    {
        controller.setShuffleEnabled (shuffled);
        playFromRow (row);
    }
}

bool MusicLibraryPanel::focusOverviewSearch()
{
    if (! gridSearch.isShowing()) return false;
    gridSearch.grabKeyboardFocus();
    gridSearch.selectAll();
    return true;
}

void MusicLibraryPanel::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() && currentPage == Page::collection)
        showCollectionActions (collectionId);
}

void MusicLibraryPanel::showCollectionActions (const juce::String& id)
{
    const auto summary = libraryCollectionSummary (controller.getLibrary(), controller.getPlaylist(), id);
    juce::PopupMenu menu;
    menu.setLookAndFeel (&adapter);
    if (currentPage != Page::collection || collectionId != id) menu.addItem (1, "Open");
    menu.addItem (2, "Play", summary.trackCount > 0);
    if (id.startsWith ("playlist:"))
    {
        menu.addSeparator();
        menu.addItem (21, "Rename");
        menu.addItem (22, "Duplicate");
        menu.addItem (27, "Change cover...");
        const auto* saved = controller.getLibrary().getPlaylist (id.substring (9));
        if (saved != nullptr && saved->smartRules)
        {
            menu.addItem (43, "Edit rules...");
            menu.addItem (44, "Save as regular playlist");
        }
        menu.addItem (28, "Use automatic cover", saved != nullptr && saved->artworkCacheKey.isNotEmpty());
        juce::PopupMenu deletion;
        deletion.addItem (23, "Delete \"" + summary.name + "\"");
        menu.addSubMenu ("Delete playlist", deletion);
    }
    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<juce::ComboBox> (&collections), this, id] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 1) selectCollection (id);
            else if (result == 2)
            {
                const auto rows = libraryTracks (controller.getLibrary(), controller.getPlaylist(), id);
                std::vector<juce::String> ids, entries;
                auto start = -1;
                for (const auto& track : rows)
                {
                    if (start < 0 && track.available) start = static_cast<int> (ids.size());
                    ids.push_back (track.libraryId);
                    entries.push_back (track.id);
                }
                if (start >= 0)
                {
                    const auto started = controller.playLibraryTracks (ids, start);
                    if (started) recordPlaybackOrigins (id, entries);
                    else prunePlaybackOrigins();
                    changed (started);
                }
            }
            else performAction (result, {}, {}, id);
        });
}
