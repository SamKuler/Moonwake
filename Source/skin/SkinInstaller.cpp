#include "SkinInstaller.h"

#include "SkinCompatibilityChecker.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <regex>
#include <set>

#if JUCE_MAC || JUCE_LINUX
 #include <fcntl.h>
 #include <sys/file.h>
 #include <unistd.h>
#endif

namespace
{
void appendReport (SkinValidationReport& destination,
                   const SkinValidationReport& source)
{
    for (const auto& diagnostic : source.getDiagnostics())
    {
        if (diagnostic.severity == SkinDiagnosticSeverity::error)
            destination.addError (diagnostic.code, diagnostic.file, diagnostic.pointer,
                                  diagnostic.message, diagnostic.suggestion);
        else
            destination.addWarning (diagnostic.code, diagnostic.file, diagnostic.pointer,
                                    diagnostic.message, diagnostic.suggestion);
    }
}

bool syncPath (const juce::File& file)
{
#if JUCE_MAC || JUCE_LINUX
    const auto descriptor = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return false;
    const auto ok = ::fsync (descriptor) == 0;
    ::close (descriptor);
    return ok;
#else
    juce::ignoreUnused (file);
    return true;
#endif
}

bool syncDirectoryTree (const juce::File& directory)
{
    if (! directory.isDirectory() || directory.isSymbolicLink()) return false;
    for (const auto& child : directory.findChildFiles (
             juce::File::findDirectories, false))
        if (! syncDirectoryTree (child)) return false;
    return syncPath (directory);
}

std::optional<juce::String> readDigestFile (const juce::File& file)
{
    if (! file.existsAsFile() || file.isSymbolicLink()
        || file.getSize() <= 0 || file.getSize() > 128)
        return std::nullopt;
    juce::FileInputStream input (file);
    if (! input.openedOk()) return std::nullopt;
    const auto digest = input.readEntireStreamAsString().trim().toLowerCase();
    return digest.length() == 64
               && digest.containsOnly ("0123456789abcdef")
        ? std::optional<juce::String> (digest) : std::nullopt;
}

bool defaultWriteFile (const juce::File& target, const juce::MemoryBlock& bytes)
{
    if (! target.getParentDirectory().createDirectory()) return false;
    juce::FileOutputStream output (target);
    if (! output.openedOk() || ! output.write (bytes.getData(), bytes.getSize()))
        return false;
    output.flush();
    return syncPath (target);
}

juce::MemoryBlock textBytes (const juce::String& text)
{
    return { text.toRawUTF8(), text.getNumBytesAsUTF8() };
}

class InstallGuard final
{
public:
    explicit InstallGuard (juce::File directoryIn)
        : directory (std::move (directoryIn)) {}
    ~InstallGuard()
    {
        if (! released) static_cast<void> (directory.deleteRecursively());
    }
    void release() noexcept { released = true; }

private:
    juce::File directory;
    bool released = false;
};

bool isBuiltIn (const SkinSelection& selection)
{
    return selection.id == "builtin.foundation" || selection.id == "builtin.default";
}

bool validSkinId (const juce::String& id)
{
    static const std::regex pattern (R"(^[a-z0-9]+([.-][a-z0-9]+)+$)");
    return id.length() <= 128 && std::regex_match (id.toStdString(), pattern);
}

std::mutex& processInstallMutex()
{
    static std::mutex mutex;
    return mutex;
}

class ScopedSkinInstallLock final
{
public:
    ScopedSkinInstallLock (const juce::File& root, const juce::String& skinId)
        : processLock (processInstallMutex())
    {
        if ((! root.exists() && ! root.createDirectory() && ! root.isDirectory())
            || ! root.isDirectory() || root.isSymbolicLink())
            return;

        const auto lockRoot = root.getChildFile (".locks");
        if ((! lockRoot.exists() && ! lockRoot.createDirectory()
             && ! lockRoot.isDirectory())
            || ! lockRoot.isDirectory() || lockRoot.isSymbolicLink())
            return;
#if JUCE_MAC || JUCE_LINUX
        auto flags = O_CREAT | O_RDWR | O_CLOEXEC;
 #ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
 #endif
        descriptor = ::open (lockRoot.getChildFile (skinId + ".lock")
                                  .getFullPathName().toRawUTF8(),
                             flags, 0600);
        if (descriptor < 0 || ::flock (descriptor, LOCK_EX) != 0)
        {
            if (descriptor >= 0) ::close (descriptor);
            descriptor = -1;
            return;
        }
#endif
        locked = true;
    }

    ~ScopedSkinInstallLock()
    {
#if JUCE_MAC || JUCE_LINUX
        if (descriptor >= 0)
        {
            static_cast<void> (::flock (descriptor, LOCK_UN));
            ::close (descriptor);
        }
#endif
    }

    [[nodiscard]] bool acquired() const noexcept { return locked; }

private:
    std::unique_lock<std::mutex> processLock;
#if JUCE_MAC || JUCE_LINUX
    int descriptor = -1;
#endif
    bool locked = false;
};

} // namespace

SkinInstaller::SkinInstaller (juce::File installRoot, FileWriter writer)
    : installDirectory (std::move (installRoot)),
      writeFile (writer ? std::move (writer) : FileWriter (defaultWriteFile))
{
}

bool SkinInstaller::recover (SkinValidationReport& report)
{
    if (! installDirectory.exists()) return true;
    if (! installDirectory.isDirectory() || installDirectory.isSymbolicLink())
    {
        report.addError ("PKG010", installDirectory.getFullPathName(), {},
                         "Skin installation root is not a safe directory.");
        return false;
    }
    for (const auto& idRoot : installDirectory.findChildFiles (
             juce::File::findDirectories, false))
    {
        if (idRoot.isSymbolicLink() || ! validSkinId (idRoot.getFileName())
            || idRoot.getFileName().startsWith ("builtin."))
            continue;
        ScopedSkinInstallLock lock (installDirectory, idRoot.getFileName());
        if (! lock.acquired())
        {
            report.addError ("PKG010", idRoot.getFullPathName(), {},
                             "Interrupted skin transactions could not be locked for recovery.");
            return false;
        }
        if (! recoverIdRoot (idRoot, report)) return false;
    }
    return true;
}

bool SkinInstaller::recoverIdRoot (const juce::File& idRoot,
                                   SkinValidationReport& report)
{
    const auto digestRoot = idRoot.getChildFile (".digests");
    for (const auto& marker : idRoot.findChildFiles (
             juce::File::findFiles, false, ".pruning-*"))
    {
        if (marker.isSymbolicLink() || marker.getSize() <= 0
            || marker.getSize() > 128)
        {
            report.addError ("PKG010", marker.getFullPathName(), {},
                             "Interrupted prune marker is not safe to recover.");
            return false;
        }
        juce::FileInputStream input (marker);
        const auto incomingVersion = input.openedOk()
            ? input.readEntireStreamAsString().trim() : juce::String();
        if (! SkinSemVersion::parse (incomingVersion))
        {
            report.addError ("PKG010", marker.getFullPathName(), {},
                             "Interrupted prune marker has an invalid target version.");
            return false;
        }
        const auto transactionId = marker.getFileName().substring (9);
        const auto prefix = ".removing-" + transactionId + "-";
        std::vector<StagedRemoval> staged;
        for (const auto& stagedVersion : idRoot.findChildFiles (
                 juce::File::findDirectories, false, prefix + "*"))
        {
            if (stagedVersion.isSymbolicLink())
            {
                report.addError ("PKG010", stagedVersion.getFullPathName(), {},
                                 "Interrupted prune directory is not safe to recover.");
                return false;
            }
            const auto version = stagedVersion.getFileName().substring (
                prefix.length());
            if (! SkinSemVersion::parse (version))
            {
                report.addError ("PKG010", stagedVersion.getFullPathName(), {},
                                 "Interrupted prune directory has an invalid version.");
                return false;
            }
            staged.push_back ({
                idRoot.getChildFile (version), stagedVersion,
                digestRoot.getChildFile (version + ".sha256"),
                digestRoot.getChildFile (stagedVersion.getFileName() + ".sha256"),
                digestRoot.getChildFile (
                    stagedVersion.getFileName() + ".sha256").existsAsFile()
            });
        }

        const auto incomingTarget = idRoot.getChildFile (incomingVersion);
        const auto incomingDigest = readDigestFile (
            digestRoot.getChildFile (incomingVersion + ".sha256"));
        if (incomingTarget.isDirectory() && ! incomingTarget.isSymbolicLink()
            && incomingDigest.has_value())
            finalisePrune (staged, marker, report);
        else
            rollbackPrune (staged, marker);
    }
    return syncPath (idRoot) && (! digestRoot.isDirectory()
                                || syncPath (digestRoot));
}

bool SkinInstaller::install (
    const SkinPackage& package, const SkinManifest& manifest,
    std::optional<SkinSelection> protectedSelection,
    SkinValidationReport& report)
{
    if (! validSkinId (manifest.id) || manifest.id.startsWith ("builtin.")
        || ! SkinSemVersion::parse (manifest.version)
        || package.snapshotRoot() == juce::File())
    {
        report.addError ("PKG010", "manifest.json", {},
                         "Only validated private third-party snapshots can be installed.");
        return false;
    }
    if (! package.verifySnapshotDigest (report)) return false;

    ScopedSkinInstallLock installLock (installDirectory, manifest.id);
    if (! installLock.acquired())
    {
        report.addError ("PKG010", manifest.id, manifest.version,
                         "Skin installation could not acquire its cross-process lock.");
        return false;
    }
    if (! installDirectory.isDirectory() || installDirectory.isSymbolicLink())
    {
        report.addError ("PKG010", installDirectory.getFullPathName(), {},
                         "Skin installation root is not a safe directory.");
        return false;
    }
    const auto idRoot = installDirectory.getChildFile (manifest.id);
    const auto idRootExisted = idRoot.exists();
    if ((idRoot.exists() && (! idRoot.isDirectory() || idRoot.isSymbolicLink()))
        || (! idRoot.exists() && ! idRoot.createDirectory()))
    {
        report.addError ("PKG010", idRoot.getFullPathName(), {},
                         "Skin version directory could not be created.");
        return false;
    }
    if (! recoverIdRoot (idRoot, report))
    {
        report.addError ("PKG010", idRoot.getFullPathName(), {},
                         "Interrupted skin transactions could not be recovered.");
        return false;
    }
    if ((! idRootExisted && ! syncPath (installDirectory))
        || ! syncPath (idRoot))
    {
        report.addError ("PKG010", idRoot.getFullPathName(), {},
                         "Skin version directory could not be synchronised safely.");
        return false;
    }
    const auto target = idRoot.getChildFile (manifest.version);
    const auto digestRoot = idRoot.getChildFile (".digests");
    const auto digestRootExisted = digestRoot.exists();
    if ((digestRoot.exists() && (! digestRoot.isDirectory()
                                 || digestRoot.isSymbolicLink()))
        || (! digestRoot.exists() && ! digestRoot.createDirectory()))
    {
        report.addError ("PKG010", digestRoot.getFullPathName(), {},
                         "Skin installation digest directory could not be created safely.");
        return false;
    }
    if ((! digestRootExisted && ! syncPath (idRoot))
        || ! syncPath (digestRoot))
    {
        report.addError ("PKG010", digestRoot.getFullPathName(), {},
                         "Skin digest directory could not be synchronised safely.");
        return false;
    }
    const auto digestTarget = digestRoot.getChildFile (
        manifest.version + ".sha256");
    const auto verificationRoot = installDirectory.getChildFile (".verification");
    if (target.exists())
    {
        SkinValidationReport existingReport;
        const auto existing = SkinPackage::captureSnapshot (
            target, verificationRoot, existingReport);
        const auto existingDigest = readDigestFile (digestTarget);
        if (existing && existing->sha256() == package.sha256()
            && existingDigest && *existingDigest == package.sha256())
            return true;
        report.addError ("PKG008", target.getFullPathName(), {},
                         "A different package is already installed at this id and version.");
        return false;
    }

    const auto temporary = idRoot.getChildFile (
        ".installing-" + juce::Uuid().toString());
    if (! temporary.createDirectory())
    {
        report.addError ("PKG010", temporary.getFullPathName(), {},
                         "Atomic skin installation directory could not be created.");
        return false;
    }
    InstallGuard guard (temporary);
    for (const auto& path : package.listFiles())
    {
        const auto before = report.getDiagnostics().size();
        const auto bytes = package.readBinary (path, report);
        if (report.getDiagnostics().size() != before
            || ! writeFile (temporary.getChildFile (path), bytes))
        {
            report.addError ("PKG010", path, {},
                             "Skin package could not be copied into atomic installation storage.");
            return false;
        }
    }

    SkinValidationReport verificationReport;
    const auto verification = SkinPackage::captureSnapshot (
        temporary, verificationRoot, verificationReport);
    if (! verification || verification->sha256() != package.sha256())
    {
        appendReport (report, verificationReport);
        report.addError ("PKG012", temporary.getFullPathName(), {},
                         "Installed skin bytes do not match the validated snapshot.");
        return false;
    }
    if (! syncDirectoryTree (temporary))
    {
        report.addError ("PKG010", target.getFullPathName(), {},
                         "Skin version could not be synchronised before commit.");
        return false;
    }

    std::vector<StagedRemoval> stagedPrune;
    juce::File pruneMarker;
    if (! stagePruneVersions (manifest, std::move (protectedSelection),
                              stagedPrune, pruneMarker, report))
        return false;

    const auto digestTemporary = digestRoot.getChildFile (
        "." + manifest.version + "-" + juce::Uuid().toString());
    if (! writeFile (digestTemporary, textBytes (package.sha256() + "\n"))
        || (digestTarget.exists()
            && (! digestTarget.existsAsFile() || digestTarget.isSymbolicLink()
                || ! digestTarget.deleteFile()))
        || ! digestTemporary.moveFileTo (digestTarget)
        || ! syncPath (digestRoot))
    {
        static_cast<void> (digestTemporary.deleteFile());
        if (digestTarget.existsAsFile() && ! digestTarget.isSymbolicLink())
            static_cast<void> (digestTarget.deleteFile());
        rollbackPrune (stagedPrune, pruneMarker);
        report.addError ("PKG010", digestTarget.getFullPathName(), {},
                         "Skin installation digest could not be committed atomically.");
        return false;
    }
    if (! temporary.moveFileTo (target))
    {
        static_cast<void> (digestTarget.deleteFile());
        rollbackPrune (stagedPrune, pruneMarker);
        report.addError ("PKG010", target.getFullPathName(), {},
                         "Skin version could not be committed atomically.");
        return false;
    }
    guard.release();

    if (! syncPath (idRoot))
        report.addWarning ("PKG010", target.getFullPathName(), {},
                           "The committed skin is visible, but its parent-directory durability sync failed.");
    finalisePrune (stagedPrune, pruneMarker, report);
    return true;
}

bool SkinInstaller::stagePruneVersions (
    const SkinManifest& manifest,
    std::optional<SkinSelection> protectedSelection,
    std::vector<StagedRemoval>& staged,
    juce::File& transactionMarker,
    SkinValidationReport& report)
{
    const auto idRoot = installDirectory.getChildFile (manifest.id);
    const auto foundVersions = idRoot.findChildFiles (
        juce::File::findDirectories, false);
    std::vector<juce::File> versions (foundVersions.begin(), foundVersions.end());
    versions.erase (std::remove_if (versions.begin(), versions.end(), [] (const auto& file)
    {
        return file.isSymbolicLink()
            || ! SkinSemVersion::parse (file.getFileName());
    }), versions.end());
    std::sort (versions.begin(), versions.end(), [] (const auto& first, const auto& second)
    {
        return *SkinSemVersion::parse (first.getFileName())
             > *SkinSemVersion::parse (second.getFileName());
    });

    std::set<juce::String> keep { manifest.version };
    if (protectedSelection && protectedSelection->id == manifest.id
        && idRoot.getChildFile (protectedSelection->version).isDirectory())
        keep.insert (protectedSelection->version);
    for (const auto& version : versions)
        if (keep.size() < 2) keep.insert (version.getFileName());

    const auto digestRoot = idRoot.getChildFile (".digests");
    const auto transactionId = juce::Uuid().toString();
    transactionMarker = idRoot.getChildFile (".pruning-" + transactionId);
    if (! writeFile (transactionMarker,
                     textBytes (manifest.version + "\n"))
        || ! syncPath (idRoot))
    {
        static_cast<void> (transactionMarker.deleteFile());
        report.addError ("PKG010", transactionMarker.getFullPathName(), {},
                         "Rollback pruning transaction could not be prepared.");
        return false;
    }

    for (const auto& version : versions)
    {
        if (keep.contains (version.getFileName())) continue;
        StagedRemoval item {
            version,
            idRoot.getChildFile (".removing-" + transactionId
                                 + "-" + version.getFileName()),
            digestRoot.getChildFile (version.getFileName() + ".sha256"),
            digestRoot.getChildFile (".removing-" + transactionId
                                     + "-" + version.getFileName() + ".sha256")
        };
        if (! item.version.moveFileTo (item.stagedVersion))
        {
            report.addError ("PKG010", version.getFullPathName(), {},
                             "Obsolete rollback skin version could not be staged for removal.");
            rollbackPrune (staged, transactionMarker);
            return false;
        }
        if (item.digest.exists())
        {
            if (! item.digest.existsAsFile() || item.digest.isSymbolicLink()
                || ! item.digest.moveFileTo (item.stagedDigest))
            {
                static_cast<void> (item.stagedVersion.moveFileTo (item.version));
                report.addError ("PKG010", item.digest.getFullPathName(), {},
                                 "Obsolete rollback digest could not be staged safely.");
                rollbackPrune (staged, transactionMarker);
                return false;
            }
            item.digestWasMoved = true;
        }
        staged.push_back (std::move (item));
    }

    if (! syncPath (idRoot) || ! syncPath (digestRoot))
    {
        report.addError ("PKG010", idRoot.getFullPathName(), {},
                         "Rollback version pruning could not be synchronised.");
        rollbackPrune (staged, transactionMarker);
        return false;
    }
    return true;
}

void SkinInstaller::rollbackPrune (
    std::vector<StagedRemoval>& staged,
    const juce::File& transactionMarker)
{
    bool restored = true;
    for (auto item = staged.rbegin(); item != staged.rend(); ++item)
    {
        if (item->digestWasMoved && item->stagedDigest.existsAsFile()
            && ! item->digest.exists())
            restored = item->stagedDigest.moveFileTo (item->digest) && restored;
        if (item->stagedVersion.isDirectory() && ! item->version.exists())
            restored = item->stagedVersion.moveFileTo (item->version) && restored;
    }
    if (restored) static_cast<void> (transactionMarker.deleteFile());
    if (! staged.empty())
    {
        static_cast<void> (syncPath (staged.front().version.getParentDirectory()
                                         .getChildFile (".digests")));
        static_cast<void> (syncPath (staged.front().version.getParentDirectory()));
    }
}

void SkinInstaller::finalisePrune (
    std::vector<StagedRemoval>& staged,
    const juce::File& transactionMarker,
    SkinValidationReport& report)
{
    bool cleaned = true;
    for (const auto& item : staged)
    {
        if (item.stagedVersion.exists() && ! item.stagedVersion.deleteRecursively())
        {
            cleaned = false;
            report.addWarning ("PKG010", item.stagedVersion.getFullPathName(), {},
                               "A staged obsolete skin version will be cleaned up later.");
        }
        if (item.digestWasMoved && item.stagedDigest.exists()
            && ! item.stagedDigest.deleteFile())
        {
            cleaned = false;
            report.addWarning ("PKG010", item.stagedDigest.getFullPathName(), {},
                               "A staged obsolete digest will be cleaned up later.");
        }
    }
    if (cleaned && ! transactionMarker.deleteFile())
        report.addWarning ("PKG010", transactionMarker.getFullPathName(), {},
                           "A completed skin transaction marker will be cleaned up later.");
    if (! staged.empty())
    {
        const auto idRoot = staged.front().version.getParentDirectory();
        if (! syncPath (idRoot)
            || ! syncPath (idRoot.getChildFile (".digests")))
            report.addWarning ("PKG010", idRoot.getFullPathName(), {},
                               "Obsolete-version cleanup durability sync failed.");
    }
}

bool SkinInstaller::remove (
    const SkinSelection& selection,
    std::optional<SkinSelection> protectedSelection,
    SkinValidationReport& report)
{
    if (! validSkinId (selection.id) || ! SkinSemVersion::parse (selection.version))
    {
        report.addError ("PKG007", selection.id, selection.version,
                         "Requested skin selection is not a safe id and version.");
        return false;
    }
    if (isBuiltIn (selection))
    {
        report.addError ("PKG008", selection.id, {},
                         "Built-in skins cannot be removed.");
        return false;
    }
    if (protectedSelection && *protectedSelection == selection)
    {
        report.addError ("PKG008", selection.id, selection.version,
                         "The active skin version cannot be removed.");
        return false;
    }
    ScopedSkinInstallLock installLock (installDirectory, selection.id);
    if (! installLock.acquired())
    {
        report.addError ("PKG010", selection.id, selection.version,
                         "Skin removal could not acquire its cross-process lock.");
        return false;
    }
    const auto idRoot = installDirectory.getChildFile (selection.id);
    if (! idRoot.isDirectory() || idRoot.isSymbolicLink())
    {
        report.addError ("PKG007", selection.id, selection.version,
                         "Requested skin id directory was not found safely.");
        return false;
    }
    const auto target = idRoot.getChildFile (selection.version);
    if (! target.isDirectory() || target.isSymbolicLink()
        || ! SkinSemVersion::parse (selection.version))
    {
        report.addError ("PKG007", selection.id, selection.version,
                         "Requested installed skin version was not found.");
        return false;
    }
    if (! target.deleteRecursively())
    {
        report.addError ("PKG010", target.getFullPathName(), {},
                         "Installed skin version could not be removed.");
        return false;
    }
    static_cast<void> (idRoot.getChildFile (".digests")
                             .getChildFile (selection.version + ".sha256")
                             .deleteFile());
    const auto digestRoot = idRoot.getChildFile (".digests");
    if (digestRoot.isDirectory()
        && digestRoot.getNumberOfChildFiles (
               juce::File::findFilesAndDirectories) == 0)
        static_cast<void> (digestRoot.deleteRecursively());
    if (idRoot.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        static_cast<void> (idRoot.deleteRecursively());
    return true;
}
