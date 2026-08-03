#include "PlaylistModel.h"

#include <algorithm>

void PlaylistModel::addTrack (TrackItem track)
{
    if (track.id.isEmpty())
        track.id = juce::Uuid().toString();

    tracks.push_back (std::move (track));

    if (selectedIndex < 0)
        selectedIndex = 0;

    if (shuffleEnabled)
        rebuildShuffleCycle();
}

bool PlaylistModel::moveTrack (int fromIndex, int toIndex)
{
    if (! isValidIndex (fromIndex) || ! isValidIndex (toIndex))
        return false;

    if (fromIndex == toIndex)
        return true;

    const auto selectedId = getSelectedTrack() != nullptr ? getSelectedTrack()->id : juce::String{};
    auto track = std::move (tracks[static_cast<size_t> (fromIndex)]);
    tracks.erase (tracks.begin() + fromIndex);
    tracks.insert (tracks.begin() + toIndex, std::move (track));

    if (selectedId.isNotEmpty())
        selectedIndex = indexOfId (selectedId);

    return true;
}

void PlaylistModel::clear()
{
    tracks.clear();
    selectedIndex = -1;
    shuffleRemainingIds.clear();
}

bool PlaylistModel::removeTrack (int index)
{
    if (! isValidIndex (index))
        return false;

    tracks.erase (tracks.begin() + index);

    if (tracks.empty())
    {
        selectedIndex = -1;
        shuffleRemainingIds.clear();
        return true;
    }

    if (selectedIndex > index)
        --selectedIndex;
    else if (selectedIndex >= static_cast<int> (tracks.size()))
        selectedIndex = static_cast<int> (tracks.size()) - 1;

    clampSelectionAfterRemoval();
    if (shuffleEnabled)
        rebuildShuffleCycle();
    return true;
}

bool PlaylistModel::selectTrack (int index)
{
    if (! isValidIndex (index))
        return false;

    selectedIndex = index;
    if (shuffleEnabled)
        rebuildShuffleCycle();
    return true;
}

bool PlaylistModel::advanceToNextTrack()
{
    if (tracks.empty() || selectedIndex < 0)
        return false;

    if (repeatMode == RepeatMode::one)
        return getSelectedTrack() != nullptr && getSelectedTrack()->available;

    if (shuffleEnabled)
    {
        auto nextIndex = chooseShuffledIndex();
        if (nextIndex < 0 && repeatMode == RepeatMode::all)
        {
            rebuildShuffleCycle();
            nextIndex = chooseShuffledIndex();
            if (nextIndex < 0 && getSelectedTrack() != nullptr && getSelectedTrack()->available)
                return true;
        }

        if (nextIndex < 0)
            return false;

        selectedIndex = nextIndex;
        return true;
    }

    for (auto nextIndex = selectedIndex + 1; nextIndex < static_cast<int> (tracks.size()); ++nextIndex)
        if (tracks[static_cast<size_t> (nextIndex)].available)
        {
            selectedIndex = nextIndex;
            return true;
        }

    if (repeatMode == RepeatMode::all)
    {
        for (auto nextIndex = 0; nextIndex <= selectedIndex; ++nextIndex)
            if (tracks[static_cast<size_t> (nextIndex)].available)
            {
                selectedIndex = nextIndex;
                return true;
            }
    }

    return false;
}

void PlaylistModel::setRepeatMode (RepeatMode modeToUse)
{
    repeatMode = modeToUse;
}

void PlaylistModel::setShuffleEnabled (bool shouldShuffle)
{
    shuffleEnabled = shouldShuffle;
    if (shuffleEnabled)
        rebuildShuffleCycle();
    else
        shuffleRemainingIds.clear();
}

void PlaylistModel::setShuffleSeed (juce::int64 seed)
{
    random = juce::Random (seed);
}

int PlaylistModel::size() const
{
    return static_cast<int> (tracks.size());
}

bool PlaylistModel::isEmpty() const
{
    return tracks.empty();
}

int PlaylistModel::getSelectedIndex() const
{
    return selectedIndex;
}

RepeatMode PlaylistModel::getRepeatMode() const
{
    return repeatMode;
}

bool PlaylistModel::isShuffleEnabled() const
{
    return shuffleEnabled;
}

const TrackItem* PlaylistModel::getSelectedTrack() const
{
    return getTrack (selectedIndex);
}

const TrackItem* PlaylistModel::getTrack (int index) const
{
    if (! isValidIndex (index))
        return nullptr;

    return &tracks[static_cast<size_t> (index)];
}

const std::vector<TrackItem>& PlaylistModel::getTracks() const
{
    return tracks;
}

int PlaylistModel::indexOfId (const juce::String& trackId) const
{
    const auto position = std::find_if (tracks.begin(), tracks.end(), [&trackId] (const auto& track)
    {
        return track.id == trackId;
    });

    return position == tracks.end() ? -1 : static_cast<int> (std::distance (tracks.begin(), position));
}

bool PlaylistModel::containsFile (const juce::File& file) const
{
    return std::any_of (tracks.begin(), tracks.end(), [&file] (const auto& track)
    {
        return track.file == file;
    });
}

bool PlaylistModel::isValidIndex (int index) const
{
    return index >= 0 && index < static_cast<int> (tracks.size());
}

int PlaylistModel::chooseShuffledIndex()
{
    while (! shuffleRemainingIds.empty())
    {
        const auto position = random.nextInt (static_cast<int> (shuffleRemainingIds.size()));
        const auto id = shuffleRemainingIds[static_cast<size_t> (position)];
        shuffleRemainingIds.erase (shuffleRemainingIds.begin() + position);
        const auto index = indexOfId (id);
        if (const auto* track = getTrack (index); track != nullptr && track->available)
            return index;
    }

    return -1;
}

void PlaylistModel::rebuildShuffleCycle()
{
    shuffleRemainingIds.clear();
    const auto selectedId = getSelectedTrack() != nullptr ? getSelectedTrack()->id : juce::String{};
    for (const auto& track : tracks)
        if (track.available && track.id != selectedId)
            shuffleRemainingIds.push_back (track.id);
}

void PlaylistModel::clampSelectionAfterRemoval()
{
    if (tracks.empty())
    {
        selectedIndex = -1;
        shuffleRemainingIds.clear();
        return;
    }

    selectedIndex = juce::jlimit (0, static_cast<int> (tracks.size()) - 1, selectedIndex);
}
