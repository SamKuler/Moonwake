#pragma once

#include "TrackItem.h"

#include <vector>

struct PlaylistFileEntry
{
    juce::File file;
    juce::String title;
    double durationSeconds = -1.0;
};

struct PlaylistFileReadResult
{
    std::vector<PlaylistFileEntry> entries;
    juce::StringArray warnings;
    juce::String error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class PlaylistFile
{
public:
    static constexpr size_t maximumFileBytes = 16 * 1024 * 1024;
    static constexpr size_t maximumEntries = 100000;

    // UTF-8 filesystem paths and local file:// URLs. Missing files remain entries;
    // remote references are skipped with warnings. Fatal errors return no entries.
    // These operations perform I/O.
    [[nodiscard]] static PlaylistFileReadResult read (const juce::File&);

    [[nodiscard]] static bool writeM3u8 (const juce::File& target,
                                       const std::vector<TrackItem>& tracks,
                                       bool relativePaths,
                                       juce::String& error);
};
