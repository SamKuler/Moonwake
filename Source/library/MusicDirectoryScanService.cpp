#include "MusicDirectoryScanService.h"

#include "platform/FilesystemPath.h"

#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace
{
struct FileStamp
{
    juce::String identity;
    juce::int64 size = 0;
    juce::int64 modifiedAtMs = 0;

    bool operator== (const FileStamp&) const = default;
};

FileStamp fileStamp (const juce::File& file)
{
    if (! file.existsAsFile()) return {};
    return { trackFileIdentity (file), file.getSize(),
             file.getLastModificationTime().toMilliseconds() };
}

bool matchesStamp (const TrackItem& track, const FileStamp& stamp)
{
    return track.fileIdentity.isNotEmpty() && track.fileIdentity == stamp.identity
        && track.fileSizeBytes == stamp.size && track.fileModifiedAtMs == stamp.modifiedAtMs;
}

void preserveLibraryFields (TrackItem& update, const TrackItem& known)
{
    update.id = known.id;
    update.libraryId = known.libraryId;
    update.addedAtMs = known.addedAtMs;
    update.reservedEffectChainState = known.reservedEffectChainState;
}

void discardCancelledResult (MusicDirectoryScanResult& result)
{
    result.cancelled = true;
    result.updates.clear();
    result.missingTrackIds.clear();
    result.preparedArtworkKeys.clear();
    for (auto& root : result.roots)
        if (root.status == MusicDirectoryScanRootReport::Status::complete)
            root.status = MusicDirectoryScanRootReport::Status::cancelled;
}
} // namespace

struct MusicDirectoryScanService::Operation
{
    explicit Operation (ArtworkStore store) : artworkStore (std::move (store)) {}

    void discardArtwork()
    {
        const std::lock_guard lock (artworkMutex);
        for (const auto& key : preparedArtworkKeys)
            static_cast<void> (artworkStore.remove (key));
        preparedArtworkKeys.clear();
    }

    std::atomic_bool cancelled { false };
    std::atomic_bool busy { false };
    ArtworkStore artworkStore;
    std::mutex artworkMutex;
    juce::StringArray preparedArtworkKeys;
};

MusicDirectoryScanService::MusicDirectoryScanService (juce::File root,
                                                      LibraryDiscoveryLimits limits)
    : juce::Thread ("Moonwake Music Directory Scan"), artworkStore (std::move (root)),
      discoveryLimits (limits), operation (std::make_shared<Operation> (artworkStore))
{
}

MusicDirectoryScanService::~MusicDirectoryScanService()
{
    cancel();
    static_cast<void> (waitForThreadToExit (-1));
    operation->discardArtwork();
}

bool MusicDirectoryScanService::start (std::vector<MusicDirectory> directories,
                                       std::vector<TrackItem> known,
                                       ProgressCallback progress,
                                       CompletionCallback completion)
{
    if (directories.empty() || ! completion || isRunning()) return false;
    if (isThreadRunning() && ! waitForThreadToExit (1000)) return false;

    operation = std::make_shared<Operation> (artworkStore);
    operation->busy.store (true);
    pendingDirectories = std::move (directories);
    pendingKnownTracks = std::move (known);
    progressCallback = std::move (progress);
    completionCallback = std::move (completion);
    if (startThread()) return true;

    pendingDirectories.clear();
    pendingKnownTracks.clear();
    progressCallback = {};
    completionCallback = {};
    operation->busy.store (false);
    return false;
}

void MusicDirectoryScanService::cancel()
{
    operation->cancelled.store (true);
    signalThreadShouldExit();
}

bool MusicDirectoryScanService::isRunning() const noexcept
{
    return operation->busy.load();
}

void MusicDirectoryScanService::discardPreparedArtwork (MusicDirectoryScanResult& result,
                                                         const juce::File& root)
{
    const ArtworkStore store (root);
    for (const auto& key : result.preparedArtworkKeys)
        static_cast<void> (store.remove (key));
    result.preparedArtworkKeys.clear();
}

void MusicDirectoryScanService::run()
{
    MusicDirectoryScanResult result;
    const auto lifecycle = operation;
    const auto cancelled = [&] { return lifecycle->cancelled.load() || threadShouldExit(); };
    std::map<juce::String, const TrackItem*> knownByPath;
    std::map<juce::String, std::vector<const TrackItem*>> knownByIdentity;
    for (const auto& track : pendingKnownTracks)
    {
        knownByPath.emplace (track.file.getFullPathName(), &track);
        if (track.fileIdentity.isNotEmpty()) knownByIdentity[track.fileIdentity].push_back (&track);
    }

    postProgress ({ MusicDirectoryScanProgress::Stage::discovering, 0, 0, {} });
    std::map<juce::String, juce::File> filesByIdentity;
    for (const auto& directory : pendingDirectories)
    {
        MusicDirectoryScanRootReport report;
        report.directoryId = directory.id;
        report.folder = directory.folder;
        report.status = MusicDirectoryScanRootReport::Status::complete;
        const auto addError = [&] (const juce::String& message)
        {
            report.status = MusicDirectoryScanRootReport::Status::incomplete;
            report.errors.addIfNotAlreadyThere (message);
        };
        const auto rootPath = directory.folder.getFullPathName();
        const auto nativeRoot = nativeFilesystem::toNativePath (directory.folder);
        std::error_code error;
        const auto rootStatus = std::filesystem::status (nativeRoot, error);
        if (cancelled())
            report.status = MusicDirectoryScanRootReport::Status::cancelled;
        else if (error || ! std::filesystem::is_directory (rootStatus))
        {
            report.status = (! std::filesystem::exists (rootStatus)
                              && (! error || error == std::errc::no_such_file_or_directory))
                ? MusicDirectoryScanRootReport::Status::offline
                : MusicDirectoryScanRootReport::Status::incomplete;
            report.errors.add (rootPath + " (" + (error ? juce::String (error.message())
                                                       : juce::String ("directory is unavailable")) + ")");
        }
        else
        {
            const auto rootIdentity = trackFileIdentity (directory.folder);
            std::filesystem::recursive_directory_iterator iterator (nativeRoot, error);
            const std::filesystem::recursive_directory_iterator end;
            if (error) addError (rootPath + " (" + juce::String (error.message()) + ")");
            std::set<juce::String> rootFiles;
            int visitedEntries = 0;
            while (! error && iterator != end && ! cancelled())
            {
                if (++visitedEntries > juce::jmax (1, discoveryLimits.maximumEntries))
                {
                    addError (rootPath + " (directory-entry safety limit reached)");
                    break;
                }

                const auto entryPath = nativeFilesystem::fromNativePath (iterator->path()).getFullPathName();
                std::error_code entryError;
                const auto entryStatus = iterator->symlink_status (entryError);
                if (entryError)
                    addError (entryPath + " (" + juce::String (entryError.message()) + ")");
                else if (std::filesystem::is_symlink (entryStatus)
                         && iterator->is_directory (entryError))
                {
                    iterator.disable_recursion_pending();
                    addError (entryPath + " (symbolic-link directory was not traversed)");
                }
                else if (std::filesystem::is_directory (entryStatus)
                         && iterator.depth() >= juce::jmax (0, discoveryLimits.maximumDepth))
                {
                    iterator.disable_recursion_pending();
                    addError (entryPath + " (directory-depth safety limit reached)");
                }
                else if (iterator->is_regular_file (entryError))
                {
                    const juce::File file (entryPath);
                    if (LibraryImportService::isSupportedAudioFile (file))
                    {
                        const auto identity = trackFileIdentity (file);
                        rootFiles.insert (identity);
                        const auto existing = filesByIdentity.find (identity);
                        if (existing == filesByIdentity.end())
                        {
                            if (static_cast<int> (filesByIdentity.size())
                                >= juce::jmax (1, discoveryLimits.maximumFiles))
                            {
                                addError (rootPath + " (file-count safety limit reached)");
                                break;
                            }
                            filesByIdentity.emplace (identity, file);
                        }
                        else if (knownByPath.contains (entryPath)
                                 && ! knownByPath.contains (existing->second.getFullPathName()))
                            existing->second = file;
                    }
                }
                if (entryError)
                    addError (entryPath + " (" + juce::String (entryError.message()) + ")");
                iterator.increment (error);
                if (error) addError (rootPath + " (" + juce::String (error.message()) + ")");
            }
            report.discoveredFiles = static_cast<int> (rootFiles.size());
            if (cancelled()) report.status = MusicDirectoryScanRootReport::Status::cancelled;
            else if (! directory.folder.isDirectory()
                     || trackFileIdentity (directory.folder) != rootIdentity)
                addError (rootPath + " (directory changed during discovery)");
        }
        for (const auto& message : report.errors) result.errors.addIfNotAlreadyThere (message);
        result.roots.push_back (std::move (report));
    }

    std::vector<juce::File> files;
    files.reserve (filesByIdentity.size());
    for (const auto& [identity, file] : filesByIdentity)
    {
        juce::ignoreUnused (identity);
        files.push_back (file);
    }
    std::sort (files.begin(), files.end(), [] (const auto& left, const auto& right)
    {
        return left.getFullPathName().compareNatural (right.getFullPathName(), true) < 0;
    });
    result.discoveredFiles = static_cast<int> (files.size());
    const auto completeDiscovery = std::all_of (result.roots.begin(), result.roots.end(), [] (const auto& root)
    {
        return root.status == MusicDirectoryScanRootReport::Status::complete;
    });
    TrackMetadataReader reader;
    std::set<juce::String> seenTrackIds;
    auto lastProgressTime = 0.0;
    for (size_t index = 0; index < files.size() && ! cancelled(); ++index)
    {
        const auto& file = files[index];
        const auto before = fileStamp (file);
        const auto identityMatch = knownByIdentity.find (before.identity);
        const auto pathMatch = knownByPath.find (file.getFullPathName());
        const auto pathMatchHasMoved = pathMatch != knownByPath.end()
            && pathMatch->second->fileIdentity != before.identity
            && filesByIdentity.contains (pathMatch->second->fileIdentity);
        const auto exactIdentityPath = pathMatch != knownByPath.end()
            && pathMatch->second->fileIdentity == before.identity;
        const TrackItem* known = identityMatch != knownByIdentity.end()
            ? (exactIdentityPath ? pathMatch->second : identityMatch->second.front())
            : pathMatch != knownByPath.end() && ! pathMatchHasMoved ? pathMatch->second : nullptr;
        if (known != nullptr) seenTrackIds.insert (known->id);

        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (index == 0 || index + 1 == files.size() || now - lastProgressTime >= 100.0)
        {
            postProgress ({ MusicDirectoryScanProgress::Stage::reading,
                            static_cast<int> (index), result.discoveredFiles, file.getFileName() });
            lastProgressTime = now;
        }
        if (! completeDiscovery && ! pathMatchHasMoved && pathMatch != knownByPath.end()
            && pathMatch->second->fileIdentity.isNotEmpty()
            && ! pathMatch->second->fileIdentity.startsWith ("path:")
            && pathMatch->second->fileIdentity != before.identity)
        {
            seenTrackIds.insert (pathMatch->second->id);
            result.errors.add (file.getFullPathName()
                + " (file identity changed; refresh deferred until directory discovery is complete)");
            continue;
        }

        std::vector<const TrackItem*> records;
        if (known != nullptr)
        {
            records.push_back (known);
            if (const auto aliases = knownByIdentity.find (known->fileIdentity); aliases != knownByIdentity.end())
                for (const auto* alias : aliases->second)
                    if (alias != known && alias->file.existsAsFile()
                        && trackFileIdentity (alias->file) == before.identity)
                    {
                        records.push_back (alias);
                        seenTrackIds.insert (alias->id);
                    }
        }
        if (! records.empty() && std::all_of (records.begin(), records.end(), [&] (const auto* record)
            { return record->available && matchesStamp (*record, before)
                && (record->artist.isEmpty() || ! record->artists.isEmpty()); }))
        {
            ++result.unchangedFiles;
            if (known->file != file)
            {
                TrackMetadataResult moved;
                moved.ok = true;
                moved.track = *known;
                moved.track.file = file;
                result.updates.push_back (std::move (moved));
            }
            continue;
        }

        ++result.metadataReads;
        auto metadata = reader.read (file);
        if (cancelled()) break;
        if (before.identity.isEmpty() || before != fileStamp (file))
        {
            metadata.ok = false;
            metadata.error = "Audio file changed while its metadata was being read.";
        }
        if (! metadata.ok)
        {
            result.errors.add (file.getFullPathName() + " (" + metadata.error + ")");
            for (const auto* record : records)
            {
                TrackMetadataResult failed;
                failed.track = *record;
                if (record == known) failed.track.file = file;
                failed.track.available = false;
                failed.error = metadata.error;
                result.updates.push_back (std::move (failed));
            }
            continue;
        }

        if (known != nullptr) preserveLibraryFields (metadata.track, *known);
        metadata.track.fileIdentity = before.identity;
        metadata.track.fileSizeBytes = before.size;
        metadata.track.fileModifiedAtMs = before.modifiedAtMs;
        if (! metadata.artworkData.isEmpty() && artworkStore.root() != juce::File())
        {
            juce::String artworkError;
            metadata.track.artworkCacheKey = artworkStore.store (
                "scan-" + juce::Uuid().toString(), metadata.artworkData, artworkError);
            metadata.artworkData = {};
            if (metadata.track.artworkCacheKey.isNotEmpty())
            {
                const std::lock_guard lock (lifecycle->artworkMutex);
                lifecycle->preparedArtworkKeys.add (metadata.track.artworkCacheKey);
                result.preparedArtworkKeys.add (metadata.track.artworkCacheKey);
            }
            else if (artworkError.isNotEmpty())
                result.artworkWarnings.add (file.getFullPathName() + " (" + artworkError + ")");
        }
        for (const auto* record : records)
            if (record != known)
            {
                auto alias = metadata;
                preserveLibraryFields (alias.track, *record);
                alias.track.file = record->file;
                result.updates.push_back (std::move (alias));
            }
        result.updates.push_back (std::move (metadata));
    }

    if (! cancelled())
        for (const auto& known : pendingKnownTracks)
        {
            if (seenTrackIds.contains (known.id)) continue;
            for (const auto& root : result.roots)
                if (root.status == MusicDirectoryScanRootReport::Status::complete
                    && known.file.isAChildOf (root.folder))
                {
                    result.missingTrackIds.addIfNotAlreadyThere (known.id);
                    break;
                }
        }

    if (cancelled())
    {
        lifecycle->discardArtwork();
        discardCancelledResult (result);
    }
    auto handler = std::move (completionCallback);
    progressCallback = {};
    pendingDirectories.clear();
    pendingKnownTracks.clear();
    if (! juce::MessageManager::callAsync (
        [lifecycle, completion = std::move (handler), completed = std::move (result)] () mutable
        {
            if (lifecycle->cancelled.load())
            {
                lifecycle->discardArtwork();
                discardCancelledResult (completed);
            }
            else
            {
                const std::lock_guard lock (lifecycle->artworkMutex);
                lifecycle->preparedArtworkKeys.clear();
            }
            if (completion) completion (std::move (completed));
            lifecycle->busy.store (false);
        }))
    {
        lifecycle->discardArtwork();
        lifecycle->busy.store (false);
    }
}

void MusicDirectoryScanService::postProgress (MusicDirectoryScanProgress progress) const
{
    const auto callback = progressCallback;
    if (! callback) return;
    const auto lifecycle = operation;
    juce::MessageManager::callAsync ([lifecycle, callback, snapshot = std::move (progress)]
    {
        if (! lifecycle->cancelled.load()) callback (snapshot);
    });
}
