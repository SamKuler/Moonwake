#pragma once

#include "library/LibraryImportService.h"
#include "library/MusicDirectory.h"

#include <memory>
#include <vector>

using MusicDirectoryScanProgress = LibraryImportProgress;

struct MusicDirectoryScanRootReport
{
    enum class Status { complete, offline, incomplete, cancelled };

    juce::String directoryId;
    juce::File folder;
    Status status = Status::incomplete;
    int discoveredFiles = 0;
    juce::StringArray errors;
};

struct MusicDirectoryScanResult
{
    std::vector<TrackMetadataResult> updates;
    juce::StringArray missingTrackIds;
    std::vector<MusicDirectoryScanRootReport> roots;
    juce::StringArray errors;
    juce::StringArray artworkWarnings;
    // Only these cache entries belong to this result; discard them if the result is stale.
    juce::StringArray preparedArtworkKeys;
    int discoveredFiles = 0;
    int metadataReads = 0;
    int unchangedFiles = 0;
    bool cancelled = false;
};

class MusicDirectoryScanService final : private juce::Thread
{
public:
    using ProgressCallback = std::function<void (const MusicDirectoryScanProgress&)>;
    using CompletionCallback = std::function<void (MusicDirectoryScanResult)>;

    explicit MusicDirectoryScanService (juce::File artworkCacheRoot = {},
                                        LibraryDiscoveryLimits limits = {});
    ~MusicDirectoryScanService() override;

    // Callbacks are delivered on the message thread; busy includes queued completion.
    bool start (std::vector<MusicDirectory>, std::vector<TrackItem> knownTracks,
                ProgressCallback, CompletionCallback);
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;
    static void discardPreparedArtwork (MusicDirectoryScanResult&,
                                         const juce::File& artworkCacheRoot);

private:
    struct Operation;
    void run() override;
    void postProgress (MusicDirectoryScanProgress) const;

    ArtworkStore artworkStore;
    LibraryDiscoveryLimits discoveryLimits;
    std::vector<MusicDirectory> pendingDirectories;
    std::vector<TrackItem> pendingKnownTracks;
    ProgressCallback progressCallback;
    CompletionCallback completionCallback;
    std::shared_ptr<Operation> operation;
};
