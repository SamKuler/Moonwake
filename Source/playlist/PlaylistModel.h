#pragma once

#include <juce_core/juce_core.h>

#include "TrackItem.h"

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
    std::vector<TrackItem> tracks;
    int selectedIndex = -1;
    RepeatMode repeatMode = RepeatMode::off;
    bool shuffleEnabled = false;
    juce::Random random;
    std::vector<juce::String> shuffleRemainingIds;

    [[nodiscard]] bool isValidIndex (int index) const;
    [[nodiscard]] int chooseShuffledIndex();
    void rebuildShuffleCycle();
    void clampSelectionAfterRemoval();
};
