#include "LibraryImportService.h"

#include "LibraryReadBatch.h"

#include "platform/FilesystemPath.h"

#include "playlist/PlaylistFile.h"
#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace
{
void discardImportedTracks (LibraryImportResult& result, const ArtworkStore& artworkStore,
                             const juce::StringArray& preparedArtworkKeys)
{
    for (const auto& key : preparedArtworkKeys)
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
            nativeFilesystem::toNativePath (input),
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
                addFile (nativeFilesystem::fromNativePath (iterator->path()));
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

    std::vector<juce::File> uniqueFiles;
    std::vector<size_t> sourceIndices;
    std::map<juce::String, size_t> sourceByIdentity;
    for (const auto& file : files)
    {
        if (threadShouldExit() || cancellationRequested->load()) break;
        const auto identity = result.playlistImport ? trackFileIdentity (file) : juce::String {};
        const auto found = sourceByIdentity.find (identity);
        if (result.playlistImport && found != sourceByIdentity.end())
            sourceIndices.push_back (found->second);
        else
        {
            sourceIndices.push_back (uniqueFiles.size());
            if (result.playlistImport) sourceByIdentity.emplace (identity, uniqueFiles.size());
            uniqueFiles.push_back (file);
        }
    }
    std::vector<std::optional<TrackMetadataResult>> prepared (uniqueFiles.size());
    std::vector<juce::String> artworkErrors (uniqueFiles.size());
    const auto cancelled = [&] { return threadShouldExit() || cancellationRequested->load(); };
    readLibraryBatch (uniqueFiles.size(), cancelled,
        [&] (size_t index, TrackMetadataReader& reader, ArtworkStore::EncodingCache& encoding)
        {
            auto metadata = reader.read (uniqueFiles[index]);
            if (cancelled()) return;
            if (metadata.ok && ! metadata.artworkData.isEmpty() && artworkStore.root() != juce::File())
            {
                metadata.track.artworkCacheKey = artworkStore.store (
                    metadata.track.id, metadata.artworkData, artworkErrors[index], &encoding);
                metadata.artworkData = {};
            }
            prepared[index] = std::move (metadata);
        },
        [&] (size_t completed, size_t index)
        {
            postProgress ({ LibraryImportProgress::Stage::reading, static_cast<int> (completed),
                static_cast<int> (uniqueFiles.size()), uniqueFiles[index].getFileName() });
        });
    juce::StringArray preparedArtworkKeys;
    for (size_t index = 0; index < prepared.size(); ++index)
    {
        if (prepared[index])
        {
            if (prepared[index]->track.artworkCacheKey.isNotEmpty())
                preparedArtworkKeys.add (prepared[index]->track.artworkCacheKey);
            if (! prepared[index]->ok)
                result.skippedPaths.add (uniqueFiles[index].getFullPathName() + " (" + prepared[index]->error + ")");
        }
        if (artworkErrors[index].isNotEmpty())
            result.artworkWarnings.add (uniqueFiles[index].getFullPathName() + " (" + artworkErrors[index] + ")");
    }
    for (int index = 0; index < files.size() && ! cancelled(); ++index)
    {
        const auto& source = prepared[sourceIndices[static_cast<size_t> (index)]];
        if (! source) continue;
        auto metadata = *source;
        if (metadata.ok && trackFileStamp (files[index]) != TrackFileStamp {
                metadata.track.fileIdentity, metadata.track.fileSizeBytes, metadata.track.fileModifiedAtMs })
        {
            metadata = {};
            metadata.error = "Audio file changed after its metadata was read. Import it again to refresh this entry.";
            metadata.track.available = false;
            result.skippedPaths.addIfNotAlreadyThere (files[index].getFullPathName() + " (" + metadata.error + ")");
        }
        metadata.track.file = files[index];
        if (result.playlistImport && ! metadata.ok)
        {
            const auto& entry = playlist.entries[static_cast<size_t> (index)];
            metadata.track.available = false;
            metadata.track.title = entry.title.isNotEmpty() ? entry.title : entry.file.getFileNameWithoutExtension();
            metadata.track.durationSeconds = entry.durationSeconds;
        }
        if (metadata.ok || result.playlistImport) result.tracks.push_back (std::move (metadata));
    }

    std::set<juce::String> retainedArtwork;
    for (const auto& metadata : result.tracks)
        if (metadata.track.artworkCacheKey.isNotEmpty()) retainedArtwork.insert (metadata.track.artworkCacheKey);
    for (int index = preparedArtworkKeys.size(); --index >= 0;)
        if (! retainedArtwork.contains (preparedArtworkKeys[index]))
        {
            static_cast<void> (artworkStore.remove (preparedArtworkKeys[index]));
            preparedArtworkKeys.remove (index);
        }
    if (threadShouldExit() || cancellationRequested->load())
        result.cancelled = true;

    if (result.cancelled)
        discardImportedTracks (result, artworkStore, preparedArtworkKeys);

    auto completion = std::move (completionCallback);
    const auto lifecycle = completionPending;
    const auto cancellation = cancellationRequested;
    progressCallback = {};
    pendingInputs.clear();
    pendingPlaylistFile = juce::File {};
    juce::MessageManager::callAsync (
        [lifecycle, cancellation, completion, cache = artworkStore,
         completed = std::move (result), keys = std::move (preparedArtworkKeys)] () mutable
        {
            if (cancellation->load())
            {
                completed.cancelled = true;
                discardImportedTracks (completed, cache, keys);
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
