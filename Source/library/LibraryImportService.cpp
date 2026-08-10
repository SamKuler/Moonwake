#include "LibraryImportService.h"

#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <utility>

LibraryImportService::LibraryImportService (juce::File artworkCacheRoot)
    : juce::Thread ("Moonwake Library Import"),
      artworkStore (std::move (artworkCacheRoot))
{
}

LibraryImportService::~LibraryImportService()
{
    cancel();
    static_cast<void> (waitForThreadToExit (-1));
}

bool LibraryImportService::start (juce::Array<juce::File> inputs,
                                  ProgressCallback progress,
                                  CompletionCallback completion)
{
    if (inputs.isEmpty() || ! completion)
        return false;

    if (completionPending->load())
        return false;

    if (isThreadRunning() && ! waitForThreadToExit (1000))
        return false;

    bool expected = false;
    if (! completionPending->compare_exchange_strong (expected, true))
        return false;

    pendingInputs = std::move (inputs);
    progressCallback = std::move (progress);
    completionCallback = std::move (completion);
    cancellationRequested.store (false);
    if (! startThread())
    {
        pendingInputs.clear();
        progressCallback = {};
        completionCallback = {};
        completionPending->store (false);
        return false;
    }
    return true;
}

void LibraryImportService::cancel()
{
    cancellationRequested.store (true);
    signalThreadShouldExit();
}

bool LibraryImportService::isRunning() const noexcept
{
    return completionPending->load();
}

juce::Array<juce::File> LibraryImportService::collectAudioFiles (
    const juce::Array<juce::File>& inputs,
    const std::atomic_bool* cancelled,
    juce::StringArray* skippedPaths,
    LibraryDiscoveryLimits limits)
{
    std::set<juce::String> seen;
    juce::Array<juce::File> files;
    int visitedEntries = 0;
    bool entryLimitReported = false;
    bool fileLimitReported = false;
    bool safetyLimitReached = false;
    const auto isCancelled = [cancelled]
    {
        return cancelled != nullptr && cancelled->load();
    };
    const auto addFile = [&] (const juce::File& file)
    {
        if (! isSupportedAudioFile (file))
            return;

        const auto identity = trackFileIdentity (file);
        if (seen.find (identity) != seen.end())
            return;

        if (files.size() >= juce::jmax (1, limits.maximumFiles))
        {
            if (! fileLimitReported && skippedPaths != nullptr)
                skippedPaths->add ("Import stopped at the file-count safety limit.");
            fileLimitReported = true;
            safetyLimitReached = true;
            return;
        }

        seen.insert (identity);
        files.add (file);
    };

    for (const auto& input : inputs)
    {
        if (isCancelled() || safetyLimitReached)
            break;
        if (input.existsAsFile())
        {
            addFile (input);
            continue;
        }
        if (! input.isDirectory())
        {
            if (skippedPaths != nullptr)
                skippedPaths->addIfNotAlreadyThere (
                    input.getFullPathName() + " (not found)");
            continue;
        }

        std::error_code error;
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator iterator (
            std::filesystem::path (input.getFullPathName().toStdString()),
            options, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error)
        {
            if (skippedPaths != nullptr)
                skippedPaths->addIfNotAlreadyThere (
                    input.getFullPathName() + " ("
                        + juce::String (error.message()) + ")");
            error.clear();
        }
        for (; iterator != end && ! isCancelled() && ! safetyLimitReached;
             iterator.increment (error))
        {
            if (error)
            {
                if (skippedPaths != nullptr)
                    skippedPaths->addIfNotAlreadyThere (
                        juce::String (error.message()));
                error.clear();
                continue;
            }
            if (++visitedEntries > juce::jmax (1, limits.maximumEntries))
            {
                if (! entryLimitReported && skippedPaths != nullptr)
                    skippedPaths->add (
                        "Import stopped at the directory-entry safety limit.");
                entryLimitReported = true;
                safetyLimitReached = true;
                break;
            }
            if (iterator.depth() > juce::jmax (0, limits.maximumDepth))
            {
                iterator.disable_recursion_pending();
                continue;
            }
            std::error_code entryError;
            if (iterator->is_symlink (entryError))
            {
                if (iterator->is_directory (entryError))
                    iterator.disable_recursion_pending();
                continue;
            }
            if (iterator->is_regular_file (entryError))
                addFile (juce::File (iterator->path().string()));
        }
    }

    std::sort (files.begin(), files.end(), [] (const auto& left, const auto& right)
    {
        return left.getFullPathName().compareNatural (
                   right.getFullPathName(), true) < 0;
    });
    return files;
}

bool LibraryImportService::isSupportedAudioFile (const juce::File& file)
{
    static const juce::StringArray extensions {
        ".wav", ".aiff", ".aif", ".mp3", ".flac", ".m4a",
        ".aac", ".alac", ".caf", ".ogg"
    };
    return file.existsAsFile()
        && extensions.contains (file.getFileExtension().toLowerCase());
}

void LibraryImportService::run()
{
    LibraryImportResult result;
    postProgress ({ LibraryImportProgress::Stage::discovering, 0, 0, {} });
    const auto files = collectAudioFiles (
        pendingInputs, &cancellationRequested, &result.skippedPaths);
    result.tracks.reserve (static_cast<size_t> (files.size()));

    if (threadShouldExit() || cancellationRequested.load())
        result.cancelled = true;

    TrackMetadataReader reader;
    auto lastProgressTime = 0.0;
    for (int index = 0; index < files.size(); ++index)
    {
        if (threadShouldExit() || cancellationRequested.load())
        {
            result.cancelled = true;
            break;
        }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (index == 0 || index == files.size() - 1
            || now - lastProgressTime >= 100.0)
        {
            postProgress ({ LibraryImportProgress::Stage::reading,
                            index, files.size(), files[index].getFileName() });
            lastProgressTime = now;
        }
        auto metadata = reader.read (files[index]);
        if (! metadata.ok)
            result.skippedPaths.add (files[index].getFullPathName()
                                     + " (" + metadata.error + ")");
        else
        {
            if (metadata.artworkData.getSize() > 0
                && artworkStore.root() != juce::File())
            {
                juce::String artworkError;
                metadata.track.artworkCacheKey = artworkStore.store (
                    metadata.track.id, metadata.artworkData, artworkError);
                metadata.artworkData = {};
                if (metadata.track.artworkCacheKey.isEmpty()
                    && artworkError.isNotEmpty())
                    result.artworkWarnings.add (
                        files[index].getFullPathName() + " ("
                            + artworkError + ")");
            }
            result.tracks.push_back (std::move (metadata));
        }
    }

    if (threadShouldExit() || cancellationRequested.load())
        result.cancelled = true;

    if (result.cancelled)
    {
        for (const auto& track : result.tracks)
            static_cast<void> (artworkStore.remove (
                track.track.artworkCacheKey));
        result.tracks.clear();
    }

    auto completion = std::move (completionCallback);
    const auto lifecycle = completionPending;
    progressCallback = {};
    pendingInputs.clear();
    juce::MessageManager::callAsync (
        [lifecycle, completion, completed = std::move (result)] () mutable
        {
            if (completion) completion (std::move (completed));
            lifecycle->store (false);
        });
}

void LibraryImportService::postProgress (LibraryImportProgress progress) const
{
    const auto callback = progressCallback;
    if (! callback)
        return;
    juce::MessageManager::callAsync (
        [callback, snapshot = std::move (progress)]
        {
            callback (snapshot);
        });
}
