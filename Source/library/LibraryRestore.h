#pragma once

#include "playlist/PlaylistModel.h"
#include "library/MusicLibrary.h"
#include "library/MusicDirectory.h"

#include <atomic>

struct PreparedRestoreArtwork
{
    explicit PreparedRestoreArtwork (juce::File root) : cacheRoot (std::move (root)) {}
    ~PreparedRestoreArtwork();
    PreparedRestoreArtwork (const PreparedRestoreArtwork&) = delete;
    PreparedRestoreArtwork& operator= (const PreparedRestoreArtwork&) = delete;
    juce::File cacheRoot;
    // Clear after committing the prepared state; otherwise destruction removes staged files.
    juce::StringArray keys;
};

[[nodiscard]] bool prepareLibraryRestore (
    MusicLibrary& library,
    PlaylistModel& playlist,
    const juce::File& artworkCacheRoot,
    const std::atomic_bool* cancelled,
    juce::StringArray& warnings,
    const std::vector<MusicDirectory>& managedDirectories = {},
    PreparedRestoreArtwork* preparedArtwork = nullptr);
