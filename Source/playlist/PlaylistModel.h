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
    bool advanceToNextTrack();
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
    int selectedIndex = -1;
    RepeatMode repeatMode = RepeatMode::off;
    bool shuffleEnabled = false;
    juce::Random random;
    std::vector<juce::String> shuffleRemainingIds;

    [[nodiscard]] bool isValidIndex (int index) const;
    void addFileIdentity (const juce::String&);
    void removeFileIdentity (const juce::String&);
    void addFilePath (const juce::String&);
    void removeFilePath (const juce::String&);
    [[nodiscard]] int chooseShuffledIndex();
    void rebuildShuffleCycle();
    void clampSelectionAfterRemoval();
};
