#pragma once

#include "app/PlayerController.h"
#include "library/LibraryFilter.h"
#include "library/LibraryProjection.h"
#include "skin/SkinComponentBindings.h"
#include "ui/SkinTableListBox.h"
#include "PlaylistGridComponent.h"
#include "AlbumArtworkComponent.h"

#include <functional>
#include <map>
#include <string>
#include <unordered_set>

class MusicLibraryPanel final : private juce::TableListBoxModel, private juce::MouseListener
{
public:
    enum class Page { overview, collection, player };
    MusicLibraryPanel (PlayerController&, JuceSkinAdapter&);
    ~MusicLibraryPanel() override;
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, SkinValidationReport&);
    void refresh();
    void updateVisibility();
    void updatePlayback();
    [[nodiscard]] bool canStartPlayback() const;
    bool startPlayback();
    void finishEditing();
    void setEnabled (bool);
    void setQuery (const juce::String&);
    void selectCollection (const juce::String&);
    void showLibrary();
    void showPlayer();
    void showQueueActions (juce::Component& anchor);
    void showQueueTrackActions (const TrackItem&, juce::Component& anchor);
    [[nodiscard]] Page page() const { return currentPage; }
    void updateNavigation (bool libraryPageSupported);
    bool focusOverviewSearch();
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] juce::Component& tableControl();
    [[nodiscard]] juce::ComboBox& collectionsControl() { return collections; }
    [[nodiscard]] std::vector<TrackItem> currentTracks() const { return tracks; }
    std::function<void()> onChanged, onImportPlaylist, onPageChanged, onShowQueue;
    std::function<void(const juce::String&)> onChoosePlaylistArtwork, onEditSmartPlaylist;
    std::function<void(const std::vector<TrackItem>&)> onExportPlaylist;
    std::function<void(const juce::String&)> onRelocateTrack;
    std::function<void(const juce::String&, bool)> onStatus;

private:
    class TrackTable;
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int, int, int, int, bool) override {}
    void selectedRowsChanged (int) override;
    void mouseDown (const juce::MouseEvent&) override;
    void cellDoubleClicked (int, int, const juce::MouseEvent&) override;
    void cellClicked (int, int, const juce::MouseEvent&) override;
    juce::String getCellTooltip (int, int) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    void sortOrderChanged (int, bool) override;
    [[nodiscard]] const TrackItem* trackAtRow (int) const;
    struct SelectedEntry
    {
        juce::String entryId, libraryId;
    };
    [[nodiscard]] std::vector<SelectedEntry> selectedEntries() const;
    [[nodiscard]] bool canReorder() const;
    [[nodiscard]] bool isSmartCollection() const;
    void playFromRow (int);
    void prunePlaybackOrigins();
    void recordPlaybackOrigins (const juce::String&, const std::vector<juce::String>&,
                                const std::unordered_set<std::string>& previousQueueIds = {});
    void showActions();
    void showActionsForTracks (std::vector<TrackItem>, juce::String origin,
                               juce::Component& anchor, bool includeCollectionActions);
    void showCollectionActions (const juce::String&);
    void performAction (int, std::vector<juce::String>, std::vector<juce::String>, juce::String);
    void changed (bool success = true);
    void newPlaylist();
    void beginRename();
    void finishRename (bool commit);
    void removeSelection();
    void restoreSelection();
    void applyViewSort();
    void refreshRows();
    void refreshOverview();
    void refreshDescription();
    void setCategory (juce::String);
    void playCollection (bool shuffled);
    bool handleKey (const juce::KeyPress&);

    PlayerController& controller;
    JuceSkinAdapter& adapter;
    std::unique_ptr<TrackTable> table;
    PlaylistGridComponent grid;
    AlbumArtworkComponent detailCover;
    juce::ThreadPool artworkJobs { 1 };
    juce::TextButton libraryOpen { "Library" }, playerOpen { "Now playing" }, back { "Back to library" };
    juce::TextButton browsePlaylists { "Playlists" }, browseAlbums { "Albums" }, browseArtists { "Artists" };
    juce::TextButton browseSongs { "Songs" }, browseFavourites { "Favourites" }, browseQueue { "Play queue" };
    juce::TextButton changeArtwork { "Change cover" };
    juce::TextButton play { "Play" }, shufflePlay { "Shuffle" };
    juce::TextEditor gridSearch;
    juce::Label description;
    juce::ComboBox collections;
    juce::TextButton actions { "Actions" }, create { "New playlist" };
    juce::Label title, count, empty;
    LibraryFilter filter;
    std::vector<LibraryCollection> collectionItems;
    std::vector<TrackItem> tracks;
    std::map<juce::String, std::vector<juce::String>> selectionByCollection;
    std::map<juce::String, int> scrollByCollection;
    std::map<juce::String, std::pair<int, bool>> viewSorts;
    juce::String collectionId, renameId, playingQueueId, playingLibraryId;
    uint64_t renameGeneration = 0;
    struct PlaybackOrigin
    {
        juce::String collectionId, entryId;
    };
    std::map<juce::String, PlaybackOrigin> playbackOrigins;
    bool refreshing = false;
    Page currentPage = Page::overview;
    juce::String category { "playlists" }, detailArtworkKey, detailArtworkName;
    uint64_t artworkGeneration = 0;
    int dropBeforeRow = -1;
};
