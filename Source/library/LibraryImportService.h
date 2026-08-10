#pragma once

#include "audio/TrackMetadataReader.h"
#include "library/ArtworkStore.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

struct LibraryImportProgress
{
    enum class Stage { discovering, reading };

    Stage stage = Stage::discovering;
    int processed = 0;
    int total = 0;
    juce::String currentFile;
};

struct LibraryImportResult
{
    std::vector<TrackMetadataResult> tracks;
    juce::StringArray skippedPaths;
    juce::StringArray artworkWarnings;
    bool cancelled = false;
};

struct LibraryDiscoveryLimits
{
    int maximumFiles = 100000;
    int maximumEntries = 250000;
    int maximumDepth = 64;
};

class LibraryImportService final : private juce::Thread
{
public:
    using ProgressCallback = std::function<void (const LibraryImportProgress&)>;
    using CompletionCallback = std::function<void (LibraryImportResult)>;

    explicit LibraryImportService (juce::File artworkCacheRoot = {});
    ~LibraryImportService() override;

    bool start (juce::Array<juce::File> inputs,
                ProgressCallback,
                CompletionCallback);
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;

    [[nodiscard]] static juce::Array<juce::File> collectAudioFiles (
        const juce::Array<juce::File>& inputs,
        const std::atomic_bool* cancelled = nullptr,
        juce::StringArray* skippedPaths = nullptr,
        LibraryDiscoveryLimits limits = {});
    [[nodiscard]] static bool isSupportedAudioFile (const juce::File&);

private:
    void run() override;
    void postProgress (LibraryImportProgress) const;

    juce::Array<juce::File> pendingInputs;
    ProgressCallback progressCallback;
    CompletionCallback completionCallback;
    ArtworkStore artworkStore;
    std::atomic_bool cancellationRequested { false };
    std::shared_ptr<std::atomic_bool> completionPending {
        std::make_shared<std::atomic_bool> (false)
    };
};
