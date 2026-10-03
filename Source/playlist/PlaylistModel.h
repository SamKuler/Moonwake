#pragma once

#include <juce_core/juce_core.h>

#include "TrackItem.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class RepeatMode
{
    off,
    all,
    one
};

enum class PlaylistNavigation
{
    naturalCompletion,
    manualNext,
    previous
};

struct PlaylistPlaybackPlan
{
    std::vector<juce::String> trackIds;
    int repeatFrom = -1;
};

struct PlaylistPendingNextTrack
{
    juce::String id;
    // Empty means the end of actual history. Repeated playback of one queue
    // occurrence is distinguished by its one-based occurrence number.
    juce::String beforeHistoryTrackId;
    int beforeHistoryOccurrence = 0;
};

class PlaylistModel
{
public:
    using FileIdentityResolver = std::function<juce::String (const juce::File&)>;

    PlaylistModel();
    explicit PlaylistModel (FileIdentityResolver);

    void addTrack (TrackItem track);
    // Boundaries refer to the full list before the edit. Duplicate files are independent
    // occurrences; inserted missing or duplicate occurrence IDs receive fresh IDs.
    bool insertTracks (int beforeIndex, std::vector<TrackItem> tracksToInsert);
    // Unknown IDs reject the entire edit; repeated request IDs refer to one occurrence.
    bool prioritiseNextTracks (const std::vector<juce::String>& occurrenceIds);
    [[nodiscard]] std::vector<PlaylistPendingNextTrack> capturePendingNextTracks() const;
    // Restore unplayed priorities after a list undo without restoring playback history.
    void restorePendingNextTracks (const std::vector<PlaylistPendingNextTrack>&);
    bool removeTracks (const std::vector<juce::String>& occurrenceIds);
    // Moved occurrences retain their source-list order, regardless of request order.
    bool moveTracks (const std::vector<juce::String>& occurrenceIds, int beforeIndex);
    // Restores list content while retaining current playback and surviving navigation history.
    bool replaceTracks (std::vector<TrackItem> replacement);
    bool removeTrack (int index);
    bool moveTrack (int fromIndex, int toIndex);
    void clear();
    bool selectTrack (int index);
    bool advanceToNextTrack (PlaylistNavigation kind = PlaylistNavigation::naturalCompletion);
    bool advanceToPreviousTrack();
    // Returns each available candidate at most once without consuming navigation state.
    [[nodiscard]] std::vector<juce::String> navigationCandidates (PlaylistNavigation kind) const;
    [[nodiscard]] bool hasNavigationCandidate (PlaylistNavigation kind) const;
    // Includes the current item and preserves each forward-history occurrence.
    // Only the current item may be unavailable; repeatFrom identifies a playable cycle.
    [[nodiscard]] PlaylistPlaybackPlan continuousPlaybackPlan() const;
    // Commit a candidate after its load succeeds, preserving the planned navigation state.
    bool commitPlayback (const juce::String& trackId,
                         PlaylistNavigation kind = PlaylistNavigation::naturalCompletion);
    // Record a track that the audio layer has already started, without choosing a candidate.
    bool recordPlayback (const juce::String& trackId);
    bool setTrackAvailable (const juce::String& trackId, bool available);
    void setRepeatMode (RepeatMode modeToUse);
    void setShuffleEnabled (bool shouldShuffle);
    void setShuffleSeed (juce::int64 seed);

    [[nodiscard]] int size() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] int getSelectedIndex() const;
    [[nodiscard]] RepeatMode getRepeatMode() const;
    [[nodiscard]] bool isShuffleEnabled() const;
    [[nodiscard]] const TrackItem* getSelectedTrack() const;
    [[nodiscard]] const TrackItem* getTrack (int index) const;
    [[nodiscard]] const std::vector<TrackItem>& getTracks() const;
    [[nodiscard]] int indexOfId (const juce::String& trackId) const;
    [[nodiscard]] bool containsFile (const juce::File& file) const;

private:
    FileIdentityResolver resolveFileIdentity;
    std::vector<TrackItem> tracks;
    std::vector<juce::String> trackFileIdentities;
    std::vector<juce::String> trackFilePaths;
    std::unordered_set<std::string> occurrenceIdSet;
    std::unordered_map<std::string, size_t> fileIdentityCounts;
    std::unordered_map<std::string, size_t> filePathCounts;
    struct NavigationState
    {
        int selectedIndex = -1;
        juce::Random random;
        std::vector<juce::String> shuffleRemainingIds;
        // Pending occurrences are separate from actual playback history. The insertion
        // position keeps Previous/Next traversal ahead of priorities scheduled later.
        struct PendingTrack
        {
            juce::String id;
            int historyIndex = 0;
        };
        std::vector<PendingTrack> playNextTracks;
        std::vector<juce::String> history;
        int historyIndex = -1;
    } navigation;
    RepeatMode repeatMode = RepeatMode::off;
    bool shuffleEnabled = false;

    [[nodiscard]] bool isValidIndex (int index) const;
    bool visitNavigationCandidates (
        PlaylistNavigation,
        const std::function<bool (int, const NavigationState&)>& visitor) const;
    void rebuildShuffleCycle (NavigationState&) const;
    void orderPendingNextTracks();
    [[nodiscard]] static juce::String takeShuffledTrackId (NavigationState&);
    void recordSelection();
};
