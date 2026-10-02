#pragma once

#include "playlist/PlaylistModel.h"

#include <atomic>

[[nodiscard]] bool prepareLibraryRestore (
    PlaylistModel& playlist,
    const juce::File& artworkCacheRoot,
    const std::atomic_bool* cancelled,
    juce::StringArray& warnings);
