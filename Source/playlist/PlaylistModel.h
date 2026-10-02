#pragma once

#include <juce_core/juce_core.h>

#include "TrackItem.h"

#include <functional>
#include <string>
#include <unordered_map>
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

class PlaylistModel
{
public:
    using FileIdentityResolver = std::function<juce::String (const juce::File&)>;

    PlaylistModel();
    explicit PlaylistModel (FileIdentityResolver);

    void addTrack (TrackItem track);
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
    std::unordered_map<std::string, size_t> fileIdentityCounts;
    std::unordered_map<std::string, size_t> filePathCounts;
    struct NavigationState
    {
        int selectedIndex = -1;
        juce::Random random;
        std::vector<juce::String> shuffleRemainingIds;
        std::vector<juce::String> history;
        int historyIndex = -1;
    } navigation;
    RepeatMode repeatMode = RepeatMode::off;
    bool shuffleEnabled = false;

    [[nodiscard]] bool isValidIndex (int index) const;
    void addFileIdentity (const juce::String&);
    void removeFileIdentity (const juce::String&);
    void addFilePath (const juce::String&);
    void removeFilePath (const juce::String&);
    bool visitNavigationCandidates (
        PlaylistNavigation,
        const std::function<bool (int, const NavigationState&)>& visitor) const;
    void rebuildShuffleCycle (NavigationState&) const;
    [[nodiscard]] static juce::String takeShuffledTrackId (NavigationState&);
    void recordSelection();
    void clampSelectionAfterRemoval();
};
