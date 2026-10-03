#include "SkinRegistry.h"

#include "app/ProductIdentity.h"
#include "app/ProductPaths.h"

#include "SkinBuiltInPackages.h"
#include "SkinCompatibilityChecker.h"

#include <algorithm>
#include <optional>
#include <regex>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
bool validSkinId (const juce::String& id)
{
    static const std::regex pattern (R"(^[a-z0-9]+([.-][a-z0-9]+)+$)");
    return id.length() <= 128 && std::regex_match (id.toStdString(), pattern);
}

bool validDigest (const juce::String& digest)
{
    return digest.length() == 64 && digest.containsOnly ("0123456789abcdef");
}

std::optional<juce::String> readDigestFile (const juce::File& file)
{
    if (! file.existsAsFile() || file.isSymbolicLink()
        || file.getSize() <= 0 || file.getSize() > 128)
        return std::nullopt;
    juce::FileInputStream input (file);
    if (! input.openedOk()) return std::nullopt;
    const auto digest = input.readEntireStreamAsString().trim().toLowerCase();
    return validDigest (digest) ? std::optional<juce::String> (digest)
                                : std::nullopt;
}

bool newerVersion (const SkinRegistryEntry& first, const SkinRegistryEntry& second)
{
    if (first.selection.id != second.selection.id)
        return first.selection.id < second.selection.id;
    const auto firstVersion = SkinSemVersion::parse (first.selection.version);
    const auto secondVersion = SkinSemVersion::parse (second.selection.version);
    if (firstVersion && secondVersion && *firstVersion != *secondVersion)
        return *firstVersion > *secondVersion;
    return first.selection.version > second.selection.version;
}

struct RegistryMetadata
{
    juce::String name;
    juce::String author;
};

std::optional<juce::String> readBoundedRegularFileNoFollow (
    const juce::File& file, juce::int64 maximumBytes)
{
   #if JUCE_WINDOWS
    const auto handle = ::CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    struct ScopedHandle
    {
        HANDLE value;
        ~ScopedHandle() { ::CloseHandle (value); }
    } scoped { handle };

    BY_HANDLE_FILE_INFORMATION information {};
    LARGE_INTEGER size {};
    if (::GetFileType (handle) != FILE_TYPE_DISK
        || ! ::GetFileInformationByHandle (handle, &information)
        || (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0
        || ! ::GetFileSizeEx (handle, &size)
        || size.QuadPart <= 0 || size.QuadPart > maximumBytes)
        return std::nullopt;

    juce::MemoryBlock bytes (static_cast<size_t> (size.QuadPart), true);
    DWORD count = 0;
    if (! ::ReadFile (handle, bytes.getData(), static_cast<DWORD> (bytes.getSize()), &count, nullptr)
        || count != bytes.getSize())
        return std::nullopt;
    char overflow = 0;
    if (! ::ReadFile (handle, &overflow, 1, &count, nullptr) || count != 0)
        return std::nullopt;
   #else
    const auto descriptor = ::open (file.getFullPathName().toRawUTF8(),
                                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return std::nullopt;
    struct ScopedDescriptor
    {
        int value = -1;
        ~ScopedDescriptor() { if (value >= 0) ::close (value); }
    } scoped { descriptor };

    struct stat information {};
    if (::fstat (descriptor, &information) != 0
        || ! S_ISREG (information.st_mode)
        || information.st_size <= 0
        || information.st_size > maximumBytes)
        return std::nullopt;

    juce::MemoryBlock bytes (static_cast<size_t> (information.st_size), true);
    size_t offset = 0;
    while (offset < bytes.getSize())
    {
        const auto count = ::read (
            descriptor, static_cast<char*> (bytes.getData()) + offset,
            bytes.getSize() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return std::nullopt;
        offset += static_cast<size_t> (count);
    }
    char overflow = 0;
    ssize_t overflowCount = -1;
    do
    {
        overflowCount = ::read (descriptor, &overflow, 1);
    }
    while (overflowCount < 0 && errno == EINTR);
    if (overflowCount != 0) return std::nullopt;
   #endif
    return juce::String::fromUTF8 (
        static_cast<const char*> (bytes.getData()),
        static_cast<int> (bytes.getSize()));
}

RegistryMetadata readRegistryMetadata (const juce::File& versionDirectory,
                                       const juce::String& expectedId,
                                       const juce::String& expectedVersion)
{
    RegistryMetadata result { expectedId, {} };
    const auto manifestFile = versionDirectory.getChildFile ("manifest.json");
    constexpr juce::int64 maximumManifestBytes = 1024 * 1024;
    const auto text = readBoundedRegularFileNoFollow (
        manifestFile, maximumManifestBytes);
    if (! text) return result;
    juce::var manifest;
    if (juce::JSON::parse (*text, manifest).failed()
        || ! manifest.isObject() || manifest.isArray()
        || manifest.getProperty ("id", {}).toString() != expectedId
        || manifest.getProperty ("version", {}).toString() != expectedVersion)
        return result;

    const auto name = manifest.getProperty ("name", {}).toString();
    if (name.isNotEmpty() && name.length() <= 256)
        result.name = name;
    const auto author = manifest.getProperty ("author", {});
    if (author.isObject() && ! author.isArray())
    {
        const auto authorName = author.getProperty ("name", {}).toString();
        if (authorName.length() <= 256) result.author = authorName;
    }
    return result;
}
} // namespace

bool SkinRegistry::initialise (const juce::File& installRoot,
                               SkinValidationReport& report)
{
    installDirectory = installRoot;
    if ((installDirectory.exists() && (! installDirectory.isDirectory()
                                       || installDirectory.isSymbolicLink()))
        || (! installDirectory.exists() && ! installDirectory.createDirectory()))
    {
        report.addError ("PKG010", installDirectory.getFullPathName(), {},
                         "Skin installation root could not be created safely.");
        return false;
    }
    return refresh (report);
}

bool SkinRegistry::refresh (SkinValidationReport& report)
{
    registered = {
        { { "builtin.foundation", "1.0.0" }, true, {}, {},
          "Studio A Foundation", MoonwakeIdentity::authorName },
        { { "builtin.default", "1.0.0" }, true, {}, {},
          "Listening B", MoonwakeIdentity::authorName }
    };
    if (! installDirectory.isDirectory())
    {
        report.addError ("PKG010", installDirectory.getFullPathName(), {},
                         "Skin installation root is unavailable.");
        return false;
    }

    for (const auto& idDirectory : installDirectory.findChildFiles (
             juce::File::findDirectories, false))
    {
        const auto id = idDirectory.getFileName();
        if (idDirectory.isSymbolicLink() || ! validSkinId (id)
            || id.startsWith ("builtin."))
            continue;
        const auto digestRoot = idDirectory.getChildFile (".digests");
        if (! digestRoot.isDirectory() || digestRoot.isSymbolicLink()) continue;
        for (const auto& versionDirectory : idDirectory.findChildFiles (
                 juce::File::findDirectories, false))
        {
            const auto version = versionDirectory.getFileName();
            if (versionDirectory.isSymbolicLink()
                || ! SkinSemVersion::parse (version))
                continue;
            const auto digest = readDigestFile (
                digestRoot.getChildFile (version + ".sha256"));
            if (! digest) continue;
            const auto metadata = readRegistryMetadata (
                versionDirectory, id, version);
            registered.push_back ({ { id, version }, false,
                                    versionDirectory, *digest,
                                    metadata.name, metadata.author });
        }
    }
    std::sort (registered.begin(), registered.end(), newerVersion);
    return true;
}

std::vector<SkinRegistryEntry> SkinRegistry::entries() const
{
    return registered;
}

std::optional<SkinSelection> SkinRegistry::resolve (
    const SkinSelection& requested) const
{
    const auto found = std::find_if (registered.begin(), registered.end(),
                                     [&] (const auto& entry)
    {
        return entry.selection.id == requested.id
            && (requested.version.isEmpty()
                || entry.selection.version == requested.version);
    });
    return found == registered.end() ? std::nullopt
                                     : std::optional<SkinSelection> (found->selection);
}

bool SkinRegistry::contains (const SkinSelection& selection) const
{
    return resolve (selection).has_value();
}

std::optional<SkinPackage> SkinRegistry::packageFor (
    const SkinSelection& requested, const juce::File& privateSnapshotRoot,
    SkinValidationReport& report) const
{
    const auto selection = resolve (requested);
    if (! selection)
    {
        report.addError ("PKG007", requested.id, requested.version,
                         "Requested skin version is not installed.");
        return std::nullopt;
    }
    if (selection->id == "builtin.foundation")
        return SkinBuiltInPackages::create (
            SkinBuiltInPackages::Package::foundation, report);
    if (selection->id == "builtin.default")
        return SkinBuiltInPackages::create (
            SkinBuiltInPackages::Package::defaultSkin, report);

    const auto found = std::find_if (registered.begin(), registered.end(),
                                     [&] (const auto& entry)
    {
        return entry.selection == *selection;
    });
    if (found == registered.end() || found->builtIn)
        return std::nullopt;
    auto package = SkinPackage::captureSnapshot (
        found->packageDirectory, privateSnapshotRoot, report);
    if (! package) return std::nullopt;
    if (package->sha256() != found->sha256)
    {
        report.addError ("RUNTIME004", found->packageDirectory.getFullPathName(), {},
                         "Installed skin files no longer match their installation digest.",
                         "Re-import the skin or restore builtin.foundation.");
        return std::nullopt;
    }
    return package;
}

const juce::File& SkinRegistry::root() const noexcept
{
    return installDirectory;
}

juce::File SkinRegistry::defaultInstallRoot()
{
    return ProductPaths::installedSkinsRoot();
}
