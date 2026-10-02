#include "PlaylistModel.h"

#include "TrackFileIdentity.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

PlaylistModel::PlaylistModel()
    : PlaylistModel (trackFileIdentity)
{
}

PlaylistModel::PlaylistModel (FileIdentityResolver resolver)
    : resolveFileIdentity (std::move (resolver))
{
    if (! resolveFileIdentity)
        resolveFileIdentity = trackFileIdentity;
}

void PlaylistModel::addTrack (TrackItem track)
{
    if (track.id.isEmpty())
        track.id = juce::Uuid().toString();

    const auto identity = resolveFileIdentity (track.file);
    const auto path = track.file.getFullPathName();
    trackFileIdentities.push_back (identity);
    try
    {
        trackFilePaths.push_back (path);
    }
    catch (...)
    {
        trackFileIdentities.pop_back();
        throw;
    }

    try
    {
        tracks.push_back (std::move (track));
    }
    catch (...)
    {
        trackFilePaths.pop_back();
        trackFileIdentities.pop_back();
        throw;
    }

    try
    {
        addFileIdentity (identity);
        addFilePath (path);
    }
    catch (...)
    {
        removeFileIdentity (identity);
        tracks.pop_back();
        trackFilePaths.pop_back();
        trackFileIdentities.pop_back();
        throw;
    }

    if (navigation.selectedIndex < 0)
        navigation.selectedIndex = 0;

    if (shuffleEnabled)
        rebuildShuffleCycle (navigation);
}

bool PlaylistModel::moveTrack (int fromIndex, int toIndex)
{
    if (! isValidIndex (fromIndex) || ! isValidIndex (toIndex))
        return false;

    if (fromIndex == toIndex)
        return true;

    const auto selectedId = getSelectedTrack() != nullptr ? getSelectedTrack()->id : juce::String{};
    auto track = std::move (tracks[static_cast<size_t> (fromIndex)]);
    auto identity = std::move (trackFileIdentities[static_cast<size_t> (fromIndex)]);
    auto path = std::move (trackFilePaths[static_cast<size_t> (fromIndex)]);
    tracks.erase (tracks.begin() + fromIndex);
    trackFileIdentities.erase (trackFileIdentities.begin() + fromIndex);
    trackFilePaths.erase (trackFilePaths.begin() + fromIndex);
    tracks.insert (tracks.begin() + toIndex, std::move (track));
    trackFileIdentities.insert (trackFileIdentities.begin() + toIndex,
                                std::move (identity));
    trackFilePaths.insert (trackFilePaths.begin() + toIndex,
                           std::move (path));

    if (selectedId.isNotEmpty())
        navigation.selectedIndex = indexOfId (selectedId);

    return true;
}

void PlaylistModel::clear()
{
    tracks.clear();
    trackFileIdentities.clear();
    trackFilePaths.clear();
    fileIdentityCounts.clear();
    filePathCounts.clear();
    navigation.selectedIndex = -1;
    navigation.shuffleRemainingIds.clear();
    navigation.history.clear();
    navigation.historyIndex = -1;
}

bool PlaylistModel::removeTrack (int index)
{
    if (! isValidIndex (index))
        return false;

    const auto removedId = tracks[static_cast<size_t> (index)].id;
    for (int historyIndex = static_cast<int> (navigation.history.size()) - 1;
         historyIndex >= 0; --historyIndex)
        if (navigation.history[static_cast<size_t> (historyIndex)] == removedId)
        {
            navigation.history.erase (navigation.history.begin() + historyIndex);
            if (historyIndex <= navigation.historyIndex)
                --navigation.historyIndex;
        }

    removeFileIdentity (trackFileIdentities[static_cast<size_t> (index)]);
    removeFilePath (trackFilePaths[static_cast<size_t> (index)]);
    tracks.erase (tracks.begin() + index);
    trackFileIdentities.erase (trackFileIdentities.begin() + index);
    trackFilePaths.erase (trackFilePaths.begin() + index);

    if (tracks.empty())
    {
        navigation.selectedIndex = -1;
        navigation.shuffleRemainingIds.clear();
        return true;
    }

    if (navigation.selectedIndex > index)
        --navigation.selectedIndex;
    else if (navigation.selectedIndex >= static_cast<int> (tracks.size()))
        navigation.selectedIndex = static_cast<int> (tracks.size()) - 1;

    clampSelectionAfterRemoval();
    if (shuffleEnabled)
        rebuildShuffleCycle (navigation);
    return true;
}

bool PlaylistModel::selectTrack (int index)
{
    if (! isValidIndex (index))
        return false;

    navigation.selectedIndex = index;
    navigation.history.resize (static_cast<size_t> (navigation.historyIndex + 1));
    recordSelection();
    if (shuffleEnabled)
        rebuildShuffleCycle (navigation);
    return true;
}

bool PlaylistModel::advanceToNextTrack (PlaylistNavigation kind)
{
    return visitNavigationCandidates (kind, [this] (int index, const NavigationState& state)
    {
        navigation = state;
        navigation.selectedIndex = index;
        recordSelection();
        return true;
    });
}

bool PlaylistModel::advanceToPreviousTrack()
{
    return advanceToNextTrack (PlaylistNavigation::previous);
}

std::vector<juce::String> PlaylistModel::navigationCandidates (PlaylistNavigation kind) const
{
    std::vector<juce::String> result;
    visitNavigationCandidates (kind, [&] (int index, const NavigationState&)
    {
        result.push_back (tracks[static_cast<size_t> (index)].id);
        return false;
    });
    return result;
}

bool PlaylistModel::hasNavigationCandidate (PlaylistNavigation kind) const
{
    const auto* selected = getSelectedTrack();
    if (selected == nullptr) return false;
    if (kind == PlaylistNavigation::previous && ! shuffleEnabled)
    {
        for (auto index = navigation.selectedIndex - 1; index >= 0; --index)
            if (tracks[static_cast<size_t> (index)].available) return true;
        return false;
    }
    if (kind != PlaylistNavigation::previous)
    {
        if (kind == PlaylistNavigation::naturalCompletion
            && repeatMode == RepeatMode::one && selected->available)
            return true;
        if (repeatMode == RepeatMode::all)
            return std::any_of (tracks.begin(), tracks.end(),
                                [] (const auto& track) { return track.available; });
    }

    std::unordered_set<std::string> availableIds;
    availableIds.reserve (tracks.size());
    for (const auto& track : tracks)
        if (track.available) availableIds.insert (track.id.toStdString());
    if (availableIds.empty()) return false;
    const auto available = [&] (const juce::String& id)
    {
        return availableIds.contains (id.toStdString());
    };
    if (kind == PlaylistNavigation::previous)
    {
        auto index = navigation.historyIndex;
        if (index >= 0 && navigation.history[static_cast<size_t> (index)] == selected->id)
            --index;
        for (; index >= 0; --index)
            if (available (navigation.history[static_cast<size_t> (index)])) return true;
        return false;
    }

    for (auto index = navigation.historyIndex + 1;
         index < static_cast<int> (navigation.history.size()); ++index)
        if (available (navigation.history[static_cast<size_t> (index)])) return true;
    if (shuffleEnabled)
        return std::any_of (navigation.shuffleRemainingIds.begin(),
                            navigation.shuffleRemainingIds.end(), available);
    for (auto index = navigation.selectedIndex + 1; index < size(); ++index)
        if (tracks[static_cast<size_t> (index)].available) return true;
    return false;
}

PlaylistPlaybackPlan PlaylistModel::continuousPlaybackPlan() const
{
    PlaylistPlaybackPlan result;
    const auto* current = getSelectedTrack();
    if (current == nullptr) return result;
    result.trackIds.push_back (current->id);
    if (repeatMode == RepeatMode::one)
    {
        if (current->available)
            result.repeatFrom = 0;
        else
            visitNavigationCandidates (PlaylistNavigation::naturalCompletion,
                [&] (int index, const NavigationState&)
                {
                    result.trackIds.push_back (tracks[static_cast<size_t> (index)].id);
                    result.repeatFrom = 1;
                    return true;
                });
        return result;
    }

    auto future = navigation;
    std::unordered_map<std::string, int> indices;
    indices.reserve (tracks.size());
    for (int index = 0; index < size(); ++index)
        indices.emplace (tracks[static_cast<size_t> (index)].id.toStdString(), index);
    const auto append = [&] (const juce::String& id)
    {
        const auto found = indices.find (id.toStdString());
        if (found == indices.end() || ! tracks[static_cast<size_t> (found->second)].available)
            return;
        result.trackIds.push_back (id);
        future.selectedIndex = found->second;
    };

    for (auto index = future.historyIndex + 1;
         index < static_cast<int> (future.history.size()); ++index)
        append (future.history[static_cast<size_t> (index)]);
    if (shuffleEnabled)
    {
        while (! future.shuffleRemainingIds.empty())
            append (takeShuffledTrackId (future));
    }
    else
    {
        const auto first = future.selectedIndex + 1;
        for (auto index = first; index < size(); ++index)
            append (tracks[static_cast<size_t> (index)].id);
    }
    if (repeatMode != RepeatMode::all) return result;

    const auto cycleStart = static_cast<int> (result.trackIds.size());
    if (shuffleEnabled)
    {
        const auto lastId = tracks[static_cast<size_t> (future.selectedIndex)].id;
        rebuildShuffleCycle (future);
        while (! future.shuffleRemainingIds.empty())
            append (takeShuffledTrackId (future));
        append (lastId);
    }
    else
    {
        for (const auto& track : tracks) append (track.id);
    }
    if (cycleStart < static_cast<int> (result.trackIds.size()))
        result.repeatFrom = cycleStart;
    return result;
}

bool PlaylistModel::commitPlayback (const juce::String& trackId, PlaylistNavigation kind)
{
    return visitNavigationCandidates (kind, [&] (int index, const NavigationState& state)
    {
        if (tracks[static_cast<size_t> (index)].id != trackId)
            return false;
        navigation = state;
        navigation.selectedIndex = index;
        recordSelection();
        return true;
    });
}

bool PlaylistModel::recordPlayback (const juce::String& trackId)
{
    const auto index = indexOfId (trackId);
    if (index < 0) return false;

    if (shuffleEnabled)
    {
        if (navigation.shuffleRemainingIds.empty() && index != navigation.selectedIndex)
            rebuildShuffleCycle (navigation);
        std::erase (navigation.shuffleRemainingIds, trackId);
    }

    const auto repeatsCurrent = repeatMode == RepeatMode::one
        && index == navigation.selectedIndex;
    if (! repeatsCurrent)
    {
        const auto forward = std::find (
            navigation.history.begin() + navigation.historyIndex + 1,
            navigation.history.end(), trackId);
        if (forward != navigation.history.end())
            navigation.historyIndex = static_cast<int> (
                std::distance (navigation.history.begin(), forward));
    }
    navigation.selectedIndex = index;
    recordSelection();
    return true;
}

bool PlaylistModel::setTrackAvailable (const juce::String& trackId, bool available)
{
    const auto index = indexOfId (trackId);
    if (index < 0) return false;
    auto& track = tracks[static_cast<size_t> (index)];
    if (track.available == available) return true;
    track.available = available;
    if (available && shuffleEnabled && index != navigation.selectedIndex
        && std::find (navigation.shuffleRemainingIds.begin(),
                      navigation.shuffleRemainingIds.end(), trackId)
               == navigation.shuffleRemainingIds.end())
        navigation.shuffleRemainingIds.push_back (trackId);
    return true;
}

void PlaylistModel::setRepeatMode (RepeatMode modeToUse)
{
    repeatMode = modeToUse;
}

void PlaylistModel::setShuffleEnabled (bool shouldShuffle)
{
    if (shuffleEnabled == shouldShuffle) return;
    shuffleEnabled = shouldShuffle;
    if (shuffleEnabled)
        rebuildShuffleCycle (navigation);
    else
        navigation.shuffleRemainingIds.clear();
}

void PlaylistModel::setShuffleSeed (juce::int64 seed)
{
    navigation.random = juce::Random (seed);
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
    return navigation.selectedIndex;
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
    return getTrack (navigation.selectedIndex);
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
    const auto identity = resolveFileIdentity (file).toStdString();
    const auto path = file.getFullPathName().toStdString();
    return fileIdentityCounts.find (identity) != fileIdentityCounts.end()
        || filePathCounts.find (path) != filePathCounts.end();
}

bool PlaylistModel::isValidIndex (int index) const
{
    return index >= 0 && index < static_cast<int> (tracks.size());
}

void PlaylistModel::addFileIdentity (const juce::String& identity)
{
    ++fileIdentityCounts[identity.toStdString()];
}

void PlaylistModel::removeFileIdentity (const juce::String& identity)
{
    const auto position = fileIdentityCounts.find (identity.toStdString());
    if (position == fileIdentityCounts.end())
        return;

    if (position->second <= 1)
        fileIdentityCounts.erase (position);
    else
        --position->second;
}

void PlaylistModel::addFilePath (const juce::String& path)
{
    ++filePathCounts[path.toStdString()];
}

void PlaylistModel::removeFilePath (const juce::String& path)
{
    const auto position = filePathCounts.find (path.toStdString());
    if (position == filePathCounts.end())
        return;

    if (position->second <= 1)
        filePathCounts.erase (position);
    else
        --position->second;
}

bool PlaylistModel::visitNavigationCandidates (
    PlaylistNavigation kind,
    const std::function<bool (int, const NavigationState&)>& visitor) const
{
    if (! isValidIndex (navigation.selectedIndex)) return false;
    auto next = navigation;
    std::unordered_map<std::string, int> indices;
    indices.reserve (tracks.size());
    for (int index = 0; index < size(); ++index)
        indices.emplace (tracks[static_cast<size_t> (index)].id.toStdString(), index);
    const auto indexForId = [&] (const juce::String& id)
    {
        const auto found = indices.find (id.toStdString());
        return found != indices.end() ? found->second : -1;
    };
    std::unordered_set<std::string> visited;
    visited.reserve (tracks.size());
    const auto offer = [&] (int index)
    {
        const auto* track = getTrack (index);
        return track != nullptr && track->available
            && visited.insert (track->id.toStdString()).second && visitor (index, next);
    };
    const auto selectedId = getSelectedTrack()->id;

    if (kind == PlaylistNavigation::previous)
    {
        if (! shuffleEnabled)
        {
            std::unordered_map<std::string, int> previousHistoryPositions;
            for (int historyIndex = 0; historyIndex < navigation.historyIndex; ++historyIndex)
                previousHistoryPositions.insert_or_assign (
                    navigation.history[static_cast<size_t> (historyIndex)].toStdString(),
                    historyIndex);
            for (int index = navigation.selectedIndex - 1; index >= 0; --index)
            {
                const auto previous = previousHistoryPositions.find (
                    tracks[static_cast<size_t> (index)].id.toStdString());
                next.historyIndex = previous != previousHistoryPositions.end()
                    ? previous->second : navigation.historyIndex;
                if (offer (index)) return true;
            }
            return false;
        }

        auto historyIndex = next.historyIndex;
        if (historyIndex >= 0
            && next.history[static_cast<size_t> (historyIndex)] == selectedId)
            --historyIndex;
        for (; historyIndex >= 0; --historyIndex)
        {
            next.historyIndex = historyIndex;
            if (offer (indexForId (next.history[static_cast<size_t> (historyIndex)])))
                return true;
        }
        return false;
    }

    if (kind == PlaylistNavigation::naturalCompletion && repeatMode == RepeatMode::one
        && offer (navigation.selectedIndex))
        return true;

    for (auto historyIndex = next.historyIndex + 1;
         historyIndex < static_cast<int> (next.history.size()); ++historyIndex)
    {
        next.historyIndex = historyIndex;
        if (offer (indexForId (next.history[static_cast<size_t> (historyIndex)])))
            return true;
    }

    if (! shuffleEnabled)
    {
        for (auto index = navigation.selectedIndex + 1; index < size(); ++index)
            if (offer (index)) return true;
        if (repeatMode == RepeatMode::all)
            for (int index = 0; index <= navigation.selectedIndex; ++index)
                if (offer (index)) return true;
        return false;
    }

    const auto offerShuffleCycle = [&]
    {
        while (! next.shuffleRemainingIds.empty())
        {
            const auto id = takeShuffledTrackId (next);
            if (offer (indexForId (id))) return true;
        }
        return false;
    };
    if (offerShuffleCycle()) return true;
    if (repeatMode != RepeatMode::all) return false;

    rebuildShuffleCycle (next);
    if (offerShuffleCycle()) return true;
    return offer (navigation.selectedIndex);
}

juce::String PlaylistModel::takeShuffledTrackId (NavigationState& state)
{
    const auto position = state.random.nextInt (static_cast<int> (state.shuffleRemainingIds.size()));
    std::swap (state.shuffleRemainingIds[static_cast<size_t> (position)],
               state.shuffleRemainingIds.back());
    auto id = std::move (state.shuffleRemainingIds.back());
    state.shuffleRemainingIds.pop_back();
    return id;
}

void PlaylistModel::rebuildShuffleCycle (NavigationState& state) const
{
    state.shuffleRemainingIds.clear();
    const auto* selected = getTrack (state.selectedIndex);
    const auto selectedId = selected != nullptr ? selected->id : juce::String{};
    for (const auto& track : tracks)
        if (track.id != selectedId)
            state.shuffleRemainingIds.push_back (track.id);
}

void PlaylistModel::recordSelection()
{
    const auto* selected = getSelectedTrack();
    if (selected == nullptr) return;
    if (navigation.historyIndex >= 0
        && navigation.history[static_cast<size_t> (navigation.historyIndex)] == selected->id)
        return;
    navigation.history.resize (static_cast<size_t> (navigation.historyIndex + 1));
    navigation.history.push_back (selected->id);
    navigation.historyIndex = static_cast<int> (navigation.history.size()) - 1;
}

void PlaylistModel::clampSelectionAfterRemoval()
{
    if (tracks.empty())
    {
        navigation.selectedIndex = -1;
        navigation.shuffleRemainingIds.clear();
        return;
    }

    navigation.selectedIndex = juce::jlimit (0, static_cast<int> (tracks.size()) - 1, navigation.selectedIndex);
}
