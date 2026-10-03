#pragma once

#include "playlist/PlaylistModel.h"
#include "library/MusicLibrary.h"

#include <atomic>

[[nodiscard]] bool prepareLibraryRestore (
    MusicLibrary& library,
    PlaylistModel& playlist,
    const juce::File& artworkCacheRoot,
    const std::atomic_bool* cancelled,
    juce::StringArray& warnings);
