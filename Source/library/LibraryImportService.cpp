#include "LibraryImportService.h"

#include "FilesystemPath.h"

#include "playlist/PlaylistFile.h"
#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <utility>

namespace
{
void discardImportedTracks (LibraryImportResult& result, const ArtworkStore& artworkStore)
{
    std::set<juce::String> keys;
    for (const auto& metadata : result.tracks)
        if (metadata.track.artworkCacheKey.isNotEmpty())
            keys.insert (metadata.track.artworkCacheKey);
    for (const auto& key : keys)
        static_cast<void> (artworkStore.remove (key));
    result.tracks.clear();
}
} // namespace

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
    return startImport (std::move (inputs), {}, std::move (progress), std::move (completion));
}

bool LibraryImportService::startPlaylist (juce::File playlistFile,
                                          ProgressCallback progress,
                                          CompletionCallback completion)
{
    return startImport ({}, std::move (playlistFile), std::move (progress), std::move (completion));
}

bool LibraryImportService::startImport (juce::Array<juce::File> inputs,
                                       juce::File playlistFile,
                                       ProgressCallback progress,
                                       CompletionCallback completion)
{
    if ((inputs.isEmpty() && playlistFile == juce::File()) || ! completion)
        return false;

    if (completionPending->load())
        return false;

    if (isThreadRunning() && ! waitForThreadToExit (1000))
        return false;

    bool expected = false;
    if (! completionPending->compare_exchange_strong (expected, true))
        return false;

    pendingInputs = std::move (inputs);
    pendingPlaylistFile = std::move (playlistFile);
    progressCallback = std::move (progress);
    completionCallback = std::move (completion);
    cancellationRequested->store (false);
    if (! startThread())
    {
        pendingInputs.clear();
        pendingPlaylistFile = juce::File {};
        progressCallback = {};
        completionCallback = {};
        completionPending->store (false);
        return false;
    }
    return true;
}

void LibraryImportService::cancel()
{
    cancellationRequested->store (true);
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
            libraryFilesystem::toNativePath (input),
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
                addFile (libraryFilesystem::fromNativePath (iterator->path()));
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
    result.playlistImport = pendingPlaylistFile != juce::File();
    postProgress ({ LibraryImportProgress::Stage::discovering, 0, 0, {} });
    juce::Array<juce::File> files;
    PlaylistFileReadResult playlist;
    if (result.playlistImport)
    {
        result.playlistName = pendingPlaylistFile.getFileNameWithoutExtension();
        if (! cancellationRequested->load())
        {
            playlist = PlaylistFile::read (pendingPlaylistFile);
            result.error = playlist.error;
            result.playlistWarnings = playlist.warnings;
            for (const auto& entry : playlist.entries)
                files.add (entry.file);
        }
    }
    else
    {
        files = collectAudioFiles (
            pendingInputs, cancellationRequested.get(), &result.skippedPaths);
    }
    result.tracks.reserve (static_cast<size_t> (files.size()));

    if (threadShouldExit() || cancellationRequested->load())
        result.cancelled = true;

    TrackMetadataReader reader;
    std::map<juce::String, size_t> preparedFiles;
    auto lastProgressTime = 0.0;
    for (int index = 0; index < files.size(); ++index)
    {
        if (threadShouldExit() || cancellationRequested->load())
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
        const auto identity = result.playlistImport ? trackFileIdentity (files[index]) : juce::String();
        const auto cached = preparedFiles.find (identity);
        auto metadata = cached != preparedFiles.end()
            ? result.tracks[cached->second] : reader.read (files[index]);
        metadata.track.file = files[index];
        if (! metadata.ok && cached == preparedFiles.end())
            result.skippedPaths.add (files[index].getFullPathName() + " (" + metadata.error + ")");
        if (metadata.ok && cached == preparedFiles.end())
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
        }
        if (result.playlistImport)
        {
            const auto& entry = playlist.entries[static_cast<size_t> (index)];
            if (! metadata.ok)
            {
                metadata.track.available = false;
                metadata.track.title = entry.title.isNotEmpty() ? entry.title
                    : entry.file.getFileNameWithoutExtension();
                metadata.track.durationSeconds = entry.durationSeconds;
            }
            preparedFiles.emplace (identity, result.tracks.size());
        }
        if (metadata.ok || result.playlistImport)
            result.tracks.push_back (std::move (metadata));
    }

    if (threadShouldExit() || cancellationRequested->load())
        result.cancelled = true;

    if (result.cancelled)
        discardImportedTracks (result, artworkStore);

    auto completion = std::move (completionCallback);
    const auto lifecycle = completionPending;
    const auto cancellation = cancellationRequested;
    progressCallback = {};
    pendingInputs.clear();
    pendingPlaylistFile = juce::File {};
    juce::MessageManager::callAsync (
        [lifecycle, cancellation, completion, cache = artworkStore,
         completed = std::move (result)] () mutable
        {
            if (cancellation->load())
            {
                completed.cancelled = true;
                discardImportedTracks (completed, cache);
            }
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
