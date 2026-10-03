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
    std::vector<TrackItem> inserted;
    inserted.push_back (std::move (track));
    insertTracks (size(), std::move (inserted));
}

bool PlaylistModel::insertTracks (int beforeIndex, std::vector<TrackItem> inserted)
{
    if (beforeIndex < 0 || beforeIndex > size()) return false;
    if (inserted.empty()) return true;

    std::unordered_set<std::string> ids;
    ids.reserve (inserted.size());
    std::vector<juce::String> identities, paths;
    std::vector<std::string> identityKeys, pathKeys;
    identities.reserve (inserted.size());
    paths.reserve (inserted.size());
    identityKeys.reserve (inserted.size());
    pathKeys.reserve (inserted.size());
    for (auto& track : inserted)
    {
        while (track.id.isEmpty() || occurrenceIdSet.contains (track.id.toStdString())
               || ! ids.insert (track.id.toStdString()).second)
            track.id = juce::Uuid().toString();
        identities.push_back (resolveFileIdentity (track.file));
        paths.push_back (track.file.getFullPathName());
        identityKeys.push_back (identities.back().toStdString());
        pathKeys.push_back (paths.back().toStdString());
    }

    const auto newSize = tracks.size() + inserted.size();
    if (newSize > tracks.capacity())
        tracks.reserve (std::max (newSize, tracks.capacity() * 2));
    if (newSize > trackFileIdentities.capacity())
        trackFileIdentities.reserve (std::max (newSize, trackFileIdentities.capacity() * 2));
    if (newSize > trackFilePaths.capacity())
        trackFilePaths.reserve (std::max (newSize, trackFilePaths.capacity() * 2));
    if (shuffleEnabled && navigation.shuffleRemainingIds.size() + inserted.size()
                              > navigation.shuffleRemainingIds.capacity())
        navigation.shuffleRemainingIds.reserve (std::max (
            navigation.shuffleRemainingIds.size() + inserted.size(),
            navigation.shuffleRemainingIds.capacity() * 2));

    if (newSize > occurrenceIdSet.bucket_count())
        occurrenceIdSet.reserve (std::max (newSize, occurrenceIdSet.size() * 2));
    size_t identitiesAdded = 0, pathsAdded = 0;
    try
    {
        for (const auto& key : identityKeys)
        {
            ++fileIdentityCounts[key];
            ++identitiesAdded;
        }
        for (const auto& key : pathKeys)
        {
            ++filePathCounts[key];
            ++pathsAdded;
        }
    }
    catch (...)
    {
        const auto undoCounts = [] (auto& counts, const auto& keys, size_t added)
        {
            for (size_t index = 0; index < added; ++index)
            {
                const auto position = counts.find (keys[index]);
                if (--position->second == 0) counts.erase (position);
            }
        };
        undoCounts (fileIdentityCounts, identityKeys, identitiesAdded);
        undoCounts (filePathCounts, pathKeys, pathsAdded);
        throw;
    }

    occurrenceIdSet.merge (ids);
    const auto wasEmpty = tracks.empty();
    if (shuffleEnabled)
        for (size_t index = 0; index < inserted.size(); ++index)
            if (! wasEmpty || index != 0)
                navigation.shuffleRemainingIds.push_back (inserted[index].id);
    tracks.insert (tracks.begin() + beforeIndex,
                   std::make_move_iterator (inserted.begin()),
                   std::make_move_iterator (inserted.end()));
    trackFileIdentities.insert (trackFileIdentities.begin() + beforeIndex,
                               std::make_move_iterator (identities.begin()),
                               std::make_move_iterator (identities.end()));
    trackFilePaths.insert (trackFilePaths.begin() + beforeIndex,
                          std::make_move_iterator (paths.begin()),
                          std::make_move_iterator (paths.end()));
    if (wasEmpty)
        navigation.selectedIndex = 0;
    else if (beforeIndex <= navigation.selectedIndex)
        navigation.selectedIndex += static_cast<int> (inserted.size());
    return true;
}

bool PlaylistModel::prioritiseNextTracks (const std::vector<juce::String>& ids)
{
    if (getSelectedTrack() == nullptr || ids.empty()) return false;
    std::unordered_set<std::string> requested;
    std::vector<NavigationState::PendingTrack> ordered;
    for (const auto& id : ids)
    {
        if (id == getSelectedTrack()->id || ! occurrenceIdSet.contains (id.toStdString())) return false;
        if (requested.insert (id.toStdString()).second) ordered.push_back ({ id, navigation.historyIndex + 1 });
    }
    recordSelection();
    for (auto& pending : ordered) pending.historyIndex = navigation.historyIndex + 1;
    for (const auto& pending : navigation.playNextTracks)
        if (! requested.contains (pending.id.toStdString())) ordered.push_back (pending);
    navigation.playNextTracks = std::move (ordered);
    std::erase_if (navigation.shuffleRemainingIds,
        [&] (const auto& id) { return requested.contains (id.toStdString()); });
    return true;
}

std::vector<PlaylistPendingNextTrack> PlaylistModel::capturePendingNextTracks() const
{
    std::vector<PlaylistPendingNextTrack> result;
    std::unordered_map<std::string, int> historyOccurrences;
    size_t historyIndex = 0;
    for (const auto& pending : navigation.playNextTracks)
    {
        PlaylistPendingNextTrack snapshot;
        snapshot.id = pending.id;
        while (historyIndex < navigation.history.size() && static_cast<int> (historyIndex) <= pending.historyIndex)
            ++historyOccurrences[navigation.history[historyIndex++].toStdString()];
        if (pending.historyIndex < static_cast<int> (navigation.history.size()))
        {
            snapshot.beforeHistoryTrackId = navigation.history[static_cast<size_t> (pending.historyIndex)];
            snapshot.beforeHistoryOccurrence = historyOccurrences[snapshot.beforeHistoryTrackId.toStdString()];
        }
        result.push_back (std::move (snapshot));
    }
    return result;
}

void PlaylistModel::restorePendingNextTracks (const std::vector<PlaylistPendingNextTrack>& restored)
{
    std::unordered_map<std::string, std::vector<int>> historyOccurrences;
    for (size_t index = 0; index < navigation.history.size(); ++index)
        historyOccurrences[navigation.history[index].toStdString()].push_back (static_cast<int> (index));
    const auto* selected = getSelectedTrack();
    navigation.playNextTracks.clear();
    for (const auto& pending : restored)
        if (occurrenceIdSet.contains (pending.id.toStdString())
            && ! historyOccurrences.contains (pending.id.toStdString())
            && (selected == nullptr || selected->id != pending.id))
        {
            auto insertion = pending.beforeHistoryTrackId.isEmpty()
                ? static_cast<int> (navigation.history.size()) : navigation.historyIndex + 1;
            if (const auto found = historyOccurrences.find (pending.beforeHistoryTrackId.toStdString());
                found != historyOccurrences.end()
                && juce::isPositiveAndBelow (pending.beforeHistoryOccurrence - 1, static_cast<int> (found->second.size())))
                insertion = found->second[static_cast<size_t> (pending.beforeHistoryOccurrence - 1)];
            navigation.playNextTracks.push_back ({ pending.id, std::max (navigation.historyIndex + 1, insertion) });
        }
    std::stable_sort (navigation.playNextTracks.begin(), navigation.playNextTracks.end(),
        [] (const auto& a, const auto& b) { return a.historyIndex < b.historyIndex; });
    std::unordered_set<std::string> pending;
    for (const auto& track : navigation.playNextTracks) pending.insert (track.id.toStdString());
    std::erase_if (navigation.shuffleRemainingIds,
        [&] (const auto& id) { return pending.contains (id.toStdString()); });
}

void PlaylistModel::orderPendingNextTracks()
{
    if (navigation.playNextTracks.empty()) return;
    std::unordered_set<std::string> pending;
    for (const auto& track : navigation.playNextTracks) pending.insert (track.id.toStdString());
    size_t index = 0;
    for (const auto& track : tracks)
        if (pending.contains (track.id.toStdString())) navigation.playNextTracks[index++].id = track.id;
}

bool PlaylistModel::removeTracks (const std::vector<juce::String>& occurrenceIds)
{
    std::unordered_set<std::string> removed;
    for (const auto& id : occurrenceIds) removed.insert (id.toStdString());
    if (removed.empty()) return true;

    std::vector<TrackItem> replacement;
    replacement.reserve (tracks.size());
    size_t found = 0;
    for (const auto& track : tracks)
        if (removed.contains (track.id.toStdString()))
            ++found;
        else
            replacement.push_back (track);
    if (found != removed.size()) return false;
    return replaceTracks (std::move (replacement));
}

bool PlaylistModel::moveTracks (const std::vector<juce::String>& occurrenceIds, int beforeIndex)
{
    if (beforeIndex < 0 || beforeIndex > size()) return false;
    std::unordered_set<std::string> moved;
    for (const auto& id : occurrenceIds) moved.insert (id.toStdString());
    if (moved.empty()) return true;

    std::vector<size_t> movedIndices, retainedIndices;
    movedIndices.reserve (moved.size());
    retainedIndices.reserve (tracks.size());
    size_t insertionIndex = 0;
    for (int index = 0; index < size(); ++index)
        if (moved.contains (tracks[static_cast<size_t> (index)].id.toStdString()))
            movedIndices.push_back (static_cast<size_t> (index));
        else
        {
            retainedIndices.push_back (static_cast<size_t> (index));
            if (index < beforeIndex) ++insertionIndex;
        }
    if (movedIndices.size() != moved.size()) return false;
    retainedIndices.insert (retainedIndices.begin() + static_cast<ptrdiff_t> (insertionIndex),
                            movedIndices.begin(), movedIndices.end());
    if (std::is_sorted (retainedIndices.begin(), retainedIndices.end())) return true;

    std::vector<TrackItem> reordered;
    std::vector<juce::String> identities, paths;
    reordered.reserve (tracks.size());
    identities.reserve (tracks.size());
    paths.reserve (tracks.size());
    const auto selectedId = getSelectedTrack() != nullptr ? getSelectedTrack()->id : juce::String{};
    for (const auto index : retainedIndices)
    {
        reordered.push_back (std::move (tracks[index]));
        identities.push_back (std::move (trackFileIdentities[index]));
        paths.push_back (std::move (trackFilePaths[index]));
    }
    tracks.swap (reordered);
    trackFileIdentities.swap (identities);
    trackFilePaths.swap (paths);
    navigation.selectedIndex = indexOfId (selectedId);
    orderPendingNextTracks();
    return true;
}

bool PlaylistModel::replaceTracks (std::vector<TrackItem> replacement)
{
    std::unordered_map<std::string, size_t> oldIndices, newIndices;
    oldIndices.reserve (tracks.size());
    newIndices.reserve (replacement.size());
    for (size_t index = 0; index < tracks.size(); ++index)
        oldIndices.emplace (tracks[index].id.toStdString(), index);

    std::vector<juce::String> identities, paths;
    identities.reserve (replacement.size());
    paths.reserve (replacement.size());
    std::unordered_map<std::string, size_t> identityCounts, pathCounts;
    bool orderChanged = false;
    size_t previousOldIndex = 0;
    bool hasRetainedTrack = false;
    for (size_t index = 0; index < replacement.size(); ++index)
    {
        auto& track = replacement[index];
        while (track.id.isEmpty() || ! newIndices.emplace (track.id.toStdString(), index).second)
            track.id = juce::Uuid().toString();
        const auto old = oldIndices.find (track.id.toStdString());
        if (old != oldIndices.end())
        {
            orderChanged = orderChanged || (hasRetainedTrack && old->second < previousOldIndex);
            previousOldIndex = old->second;
            hasRetainedTrack = true;
        }
        const auto path = track.file.getFullPathName();
        identities.push_back (old != oldIndices.end() && trackFilePaths[old->second] == path
                                  ? trackFileIdentities[old->second]
                                  : resolveFileIdentity (track.file));
        paths.push_back (path);
        ++identityCounts[identities.back().toStdString()];
        ++pathCounts[path.toStdString()];
    }

    auto updated = navigation;
    updated.selectedIndex = -1;
    const auto newIndex = [&] (const juce::String& id)
    {
        const auto found = newIndices.find (id.toStdString());
        return found != newIndices.end() ? static_cast<int> (found->second) : -1;
    };
    for (auto index = navigation.selectedIndex; isValidIndex (index); ++index)
        if ((updated.selectedIndex = newIndex (tracks[static_cast<size_t> (index)].id)) >= 0)
            break;
    if (updated.selectedIndex < 0)
        for (auto index = navigation.selectedIndex - 1; index >= 0; --index)
            if ((updated.selectedIndex = newIndex (tracks[static_cast<size_t> (index)].id)) >= 0)
                break;
    if (updated.selectedIndex < 0 && ! replacement.empty()) updated.selectedIndex = 0;

    updated.history.clear();
    updated.historyIndex = -1;
    std::vector<int> historyBoundaries (navigation.history.size() + 1, 0);
    for (size_t index = 0; index < navigation.history.size(); ++index)
    {
        if (newIndex (navigation.history[index]) >= 0)
        {
            updated.history.push_back (navigation.history[index]);
            if (static_cast<int> (index) <= navigation.historyIndex) ++updated.historyIndex;
        }
        historyBoundaries[index + 1] = static_cast<int> (updated.history.size());
    }
    const auto selectedId = updated.selectedIndex >= 0
        ? replacement[static_cast<size_t> (updated.selectedIndex)].id : juce::String{};
    const auto retainedSelection = getSelectedTrack() != nullptr && getSelectedTrack()->id == selectedId;
    std::erase_if (updated.playNextTracks, [&] (const auto& pending)
        { return (! retainedSelection && pending.id == selectedId) || newIndex (pending.id) < 0; });
    for (auto& pending : updated.playNextTracks)
        pending.historyIndex = historyBoundaries[static_cast<size_t> (pending.historyIndex)];
    std::unordered_set<std::string> priorityIds;
    for (const auto& pending : updated.playNextTracks) priorityIds.insert (pending.id.toStdString());
    std::erase_if (updated.shuffleRemainingIds, [&] (const auto& id)
    {
        return (! retainedSelection && id == selectedId) || newIndex (id) < 0;
    });
    if (shuffleEnabled)
    {
        std::unordered_set<std::string> remaining;
        for (const auto& id : updated.shuffleRemainingIds) remaining.insert (id.toStdString());
        for (const auto& track : replacement)
        {
            const auto old = oldIndices.find (track.id.toStdString());
            const auto newlyAvailable = old != oldIndices.end()
                && ! tracks[old->second].available && track.available;
            if (track.id != selectedId && ! priorityIds.contains (track.id.toStdString())
                && (old == oldIndices.end() || newlyAvailable)
                && remaining.insert (track.id.toStdString()).second)
                updated.shuffleRemainingIds.push_back (track.id);
        }
    }

    std::unordered_set<std::string> replacementIds;
    replacementIds.reserve (newIndices.size());
    for (const auto& [id, index] : newIndices) { juce::ignoreUnused (index); replacementIds.insert (id); }
    occurrenceIdSet.swap (replacementIds);
    tracks.swap (replacement);
    trackFileIdentities.swap (identities);
    trackFilePaths.swap (paths);
    fileIdentityCounts.swap (identityCounts);
    filePathCounts.swap (pathCounts);
    navigation = std::move (updated);
    if (orderChanged) orderPendingNextTracks();
    return true;
}

bool PlaylistModel::moveTrack (int fromIndex, int toIndex)
{
    if (! isValidIndex (fromIndex) || ! isValidIndex (toIndex)) return false;
    if (fromIndex == toIndex) return true;
    return moveTracks ({ tracks[static_cast<size_t> (fromIndex)].id },
                       toIndex > fromIndex ? toIndex + 1 : toIndex);
}

void PlaylistModel::clear()
{
    tracks.clear();
    occurrenceIdSet.clear();
    trackFileIdentities.clear();
    trackFilePaths.clear();
    fileIdentityCounts.clear();
    filePathCounts.clear();
    navigation.selectedIndex = -1;
    navigation.shuffleRemainingIds.clear();
    navigation.playNextTracks.clear();
    navigation.history.clear();
    navigation.historyIndex = -1;
}

bool PlaylistModel::removeTrack (int index)
{
    return isValidIndex (index) && removeTracks ({ tracks[static_cast<size_t> (index)].id });
}

bool PlaylistModel::selectTrack (int index)
{
    if (! isValidIndex (index))
        return false;

    if (navigation.selectedIndex != index)
    {
        navigation.playNextTracks.clear();
        navigation.history.resize (static_cast<size_t> (navigation.historyIndex + 1));
    }
    navigation.selectedIndex = index;
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
        if (! shuffleEnabled)
        {
            for (auto index = navigation.selectedIndex + 1; index < size(); ++index)
                if (tracks[static_cast<size_t> (index)].available) return true;
            return false;
        }
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
    return std::any_of (navigation.playNextTracks.begin(), navigation.playNextTracks.end(),
                       [&] (const auto& pending) { return available (pending.id); })
        || std::any_of (navigation.shuffleRemainingIds.begin(), navigation.shuffleRemainingIds.end(), available);
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

    if (shuffleEnabled)
    {
        auto priority = future.playNextTracks.begin();
        for (auto index = future.historyIndex + 1;
             index <= static_cast<int> (future.history.size()); ++index)
        {
            while (priority != future.playNextTracks.end() && priority->historyIndex <= index)
                append ((priority++)->id);
            if (index < static_cast<int> (future.history.size()))
                append (future.history[static_cast<size_t> (index)]);
        }
        future.playNextTracks.clear();
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

    const auto wasPrioritised = std::any_of (navigation.playNextTracks.begin(), navigation.playNextTracks.end(),
        [&] (const auto& pending) { return pending.id == trackId; });
    const auto forward = std::find (navigation.history.begin() + navigation.historyIndex + 1,
                                    navigation.history.end(), trackId);
    if (shuffleEnabled)
    {
        if (navigation.shuffleRemainingIds.empty() && index != navigation.selectedIndex
            && ! wasPrioritised && forward == navigation.history.end())
            rebuildShuffleCycle (navigation);
        if (wasPrioritised || forward == navigation.history.end())
            std::erase (navigation.shuffleRemainingIds, trackId);
    }

    const auto repeatsCurrent = repeatMode == RepeatMode::one && index == navigation.selectedIndex;
    if (! repeatsCurrent && ! wasPrioritised && forward != navigation.history.end())
    {
        navigation.historyIndex = static_cast<int> (std::distance (navigation.history.begin(), forward));
        if (shuffleEnabled)
            std::erase_if (navigation.playNextTracks,
                [&] (const auto& pending) { return pending.historyIndex <= navigation.historyIndex; });
    }
    else if (! repeatsCurrent && shuffleEnabled && ! wasPrioritised && forward == navigation.history.end())
        navigation.playNextTracks.clear();
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
        && std::none_of (navigation.playNextTracks.begin(), navigation.playNextTracks.end(),
                        [&] (const auto& pending) { return pending.id == trackId; })
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

    if (! shuffleEnabled)
    {
        for (auto index = navigation.selectedIndex + 1; index < size(); ++index)
            if (offer (index)) return true;
        if (repeatMode == RepeatMode::all)
            for (int index = 0; index <= navigation.selectedIndex; ++index)
                if (offer (index)) return true;
        return false;
    }

    for (auto historyIndex = next.historyIndex + 1;
         historyIndex <= static_cast<int> (next.history.size()); ++historyIndex)
    {
        size_t offeredPriorities = 0;
        while (offeredPriorities < next.playNextTracks.size()
               && next.playNextTracks[offeredPriorities].historyIndex <= historyIndex)
        {
            if (offer (indexForId (next.playNextTracks[offeredPriorities++].id))) return true;
        }
        next.playNextTracks.erase (next.playNextTracks.begin(),
                                   next.playNextTracks.begin() + static_cast<ptrdiff_t> (offeredPriorities));
        if (historyIndex < static_cast<int> (next.history.size()))
        {
            next.historyIndex = historyIndex;
            if (offer (indexForId (next.history[static_cast<size_t> (historyIndex)])))
                return true;
        }
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
    std::unordered_set<std::string> priorityIds;
    for (const auto& pending : state.playNextTracks) priorityIds.insert (pending.id.toStdString());
    for (const auto& track : tracks)
        if (track.id != selectedId && ! priorityIds.contains (track.id.toStdString()))
            state.shuffleRemainingIds.push_back (track.id);
}

void PlaylistModel::recordSelection()
{
    const auto* selected = getSelectedTrack();
    if (selected == nullptr) return;
    if (const auto priority = std::find_if (navigation.playNextTracks.begin(), navigation.playNextTracks.end(),
            [&] (const auto& pending) { return pending.id == selected->id; });
        priority != navigation.playNextTracks.end())
    {
        const auto insertion = juce::jlimit (navigation.historyIndex + 1,
            static_cast<int> (navigation.history.size()), priority->historyIndex);
        navigation.playNextTracks.erase (navigation.playNextTracks.begin(), std::next (priority));
        if (shuffleEnabled)
        {
            navigation.history.insert (navigation.history.begin() + insertion, selected->id);
            navigation.historyIndex = insertion;
            for (auto& pending : navigation.playNextTracks) ++pending.historyIndex;
            return;
        }
    }
    if (navigation.historyIndex >= 0
        && navigation.history[static_cast<size_t> (navigation.historyIndex)] == selected->id)
        return;
    navigation.history.resize (static_cast<size_t> (navigation.historyIndex + 1));
    navigation.history.push_back (selected->id);
    navigation.historyIndex = static_cast<int> (navigation.history.size()) - 1;
    for (auto& pending : navigation.playNextTracks)
        pending.historyIndex = static_cast<int> (navigation.history.size());
}
