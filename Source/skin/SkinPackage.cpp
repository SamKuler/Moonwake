#include "SkinPackage.h"

#include "app/ProductIdentity.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <map>
#include <set>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

#if JUCE_MAC || JUCE_LINUX
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
constexpr int maximumEntries = 2048;
constexpr int maximumAssetFiles = 512;
constexpr int64_t maximumArchiveBytes = 25ll * 1024ll * 1024ll;
constexpr int64_t maximumExpandedBytes = 100ll * 1024ll * 1024ll;
constexpr int64_t maximumJsonBytes = 2ll * 1024ll * 1024ll;
constexpr int maximumPathBytes = 512;
constexpr int maximumSegmentBytes = 255;

struct PackageEntry
{
    int64_t size = 0;
};

struct PackageStateData
{
    enum class Mode { directory, archive, embedded };

    Mode mode = Mode::embedded;
    juce::String packageId;
    juce::File root;
    juce::File archiveFile;
    std::map<juce::String, PackageEntry> entries;
    std::map<juce::String, juce::MemoryBlock> embeddedFiles;
    juce::String digest;
};

enum class PathIssue { none, unsafe, length, normalisation };

int utf8Bytes (const juce::String& value)
{
    return static_cast<int> (value.getNumBytesAsUTF8());
}

PathIssue inspectPath (const juce::String& value)
{
    if (value.isEmpty() || value.startsWithChar ('/') || value.endsWithChar ('/')
        || value.containsChar ('\\') || value.contains ("//")
        || (value.length() >= 2 && juce::CharacterFunctions::isLetter (value[0])
                              && value[1] == ':'))
        return PathIssue::unsafe;

    if (utf8Bytes (value) > maximumPathBytes)
        return PathIssue::length;

    if (value.convertToPrecomposedUnicode() != value)
        return PathIssue::normalisation;

    int segmentStart = 0;
    while (segmentStart < value.length())
    {
        auto separator = value.indexOfChar (segmentStart, '/');
        if (separator < 0)
            separator = value.length();

        const auto segment = value.substring (segmentStart, separator);
        if (segment.isEmpty() || segment == "." || segment == "..")
            return PathIssue::unsafe;
        if (utf8Bytes (segment) > maximumSegmentBytes)
            return PathIssue::length;

        segmentStart = separator + 1;
    }

    return PathIssue::none;
}

juce::String pathIssueCode (PathIssue issue)
{
    switch (issue)
    {
        case PathIssue::unsafe:        return "PKG001";
        case PathIssue::length:        return "PKG004";
        case PathIssue::normalisation: return "PKG005";
        case PathIssue::none:          break;
    }
    return {};
}

juce::String unicodeCaseFold (const juce::String& value)
{
#if JUCE_MAC
    const auto utf8 = value.toRawUTF8();
    auto* immutable = CFStringCreateWithBytes (kCFAllocatorDefault,
                                                reinterpret_cast<const UInt8*> (utf8),
                                                static_cast<CFIndex> (value.getNumBytesAsUTF8()),
                                                kCFStringEncodingUTF8, false);
    if (immutable == nullptr)
        return value.toLowerCase().convertToPrecomposedUnicode();

    auto* folded = CFStringCreateMutableCopy (kCFAllocatorDefault, 0, immutable);
    CFRelease (immutable);
    if (folded == nullptr)
        return value.toLowerCase().convertToPrecomposedUnicode();

    CFStringNormalize (folded, kCFStringNormalizationFormC);
    CFStringFold (folded, kCFCompareCaseInsensitive | kCFCompareNonliteral, nullptr);
    const auto maximum = CFStringGetMaximumSizeForEncoding (CFStringGetLength (folded),
                                                            kCFStringEncodingUTF8) + 1;
    std::vector<char> result (static_cast<size_t> (maximum));
    const auto converted = CFStringGetCString (folded, result.data(), maximum,
                                               kCFStringEncodingUTF8);
    CFRelease (folded);
    return converted ? juce::String::fromUTF8 (result.data())
                     : value.toLowerCase().convertToPrecomposedUnicode();
#else
    return value.toLowerCase().convertToPrecomposedUnicode();
#endif
}

bool addPathDiagnostic (SkinValidationReport& report, const juce::String& code,
                        const juce::String& path, const juce::String& message)
{
    report.addError (code, path, {}, message,
                     "Use unique NFC relative paths with forward slashes.");
    return false;
}

bool validateEntryMetadata (const std::vector<std::pair<juce::String, PackageEntry>>& files,
                            const juce::StringArray& directories,
                            SkinValidationReport& report)
{
    if (static_cast<int> (files.size()) + directories.size() > maximumEntries)
        return addPathDiagnostic (report, "PKG004", {},
                                  "Skin package contains more than 2048 entries.");

    std::set<juce::String> exactPaths;
    std::set<juce::String> foldedPaths;
    int assetFiles = 0;
    int64_t expandedBytes = 0;

    const auto validatePath = [&] (const juce::String& path)
    {
        if (const auto issue = inspectPath (path); issue != PathIssue::none)
            return addPathDiagnostic (report, pathIssueCode (issue), path,
                                      "Skin package path is not canonical and safe.");

        if (! exactPaths.insert (path).second
            || ! foldedPaths.insert (unicodeCaseFold (path)).second)
            return addPathDiagnostic (report, "PKG005", path,
                                      "Skin package paths collide after Unicode normalisation or case folding.");
        return true;
    };

    for (const auto& directory : directories)
        if (! validatePath (directory))
            return false;

    for (const auto& [path, entry] : files)
    {
        if (! validatePath (path))
            return false;
        if (entry.size < 0 || entry.size > maximumExpandedBytes
            || expandedBytes > maximumExpandedBytes - entry.size)
            return addPathDiagnostic (report, "PKG003", path,
                                      "Skin package expanded size exceeds 100 MiB.");
        expandedBytes += entry.size;

        if (path.endsWithIgnoreCase (".json") && entry.size > maximumJsonBytes)
            return addPathDiagnostic (report, "PKG003", path,
                                      "A JSON document exceeds the 2 MiB limit.");

        if (path.startsWith ("assets/") && ++assetFiles > maximumAssetFiles)
            return addPathDiagnostic (report, "PKG009", path,
                                      "Skin package contains more than 512 asset files.");
    }

    return true;
}

void makeWritableAndRemove (const juce::File& root)
{
    if (root == juce::File() || ! root.exists())
        return;

    std::error_code error;
    const std::filesystem::path path (root.getFullPathName().toStdString());
    std::filesystem::permissions (path, std::filesystem::perms::owner_all,
                                  std::filesystem::perm_options::add, error);
    error.clear();
    for (std::filesystem::recursive_directory_iterator iterator (
             path, std::filesystem::directory_options::none, error), end;
         ! error && iterator != end; iterator.increment (error))
    {
        std::filesystem::permissions (iterator->path(), std::filesystem::perms::owner_all,
                                      std::filesystem::perm_options::add, error);
        if (error)
            error.clear();
    }
    error.clear();
    std::filesystem::remove_all (path, error);
}

bool setOwnerPermissions (const juce::File& file, int mode)
{
#if JUCE_MAC || JUCE_LINUX
    return ::chmod (file.getFullPathName().toRawUTF8(), static_cast<mode_t> (mode)) == 0;
#else
    return mode == 0700 ? file.setReadOnly (false) : file.setReadOnly (true);
#endif
}

bool copyRegularFileNoFollow (const juce::File& source, const juce::File& destination,
                              bool rejectHardLinks)
{
    destination.getParentDirectory().createDirectory();

#if JUCE_MAC || JUCE_LINUX
    const auto sourcePath = source.getFullPathName();
    const auto descriptor = ::open (sourcePath.toRawUTF8(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return false;

    struct stat information {};
    if (::fstat (descriptor, &information) != 0 || ! S_ISREG (information.st_mode)
        || (rejectHardLinks && information.st_nlink > 1))
    {
        ::close (descriptor);
        return false;
    }

    juce::FileOutputStream output (destination);
    if (! output.openedOk())
    {
        ::close (descriptor);
        return false;
    }

    std::array<char, 32768> buffer {};
    bool ok = true;
    for (;;)
    {
        const auto amount = ::read (descriptor, buffer.data(), buffer.size());
        if (amount == 0)
            break;
        if (amount < 0 || ! output.write (buffer.data(), static_cast<size_t> (amount)))
        {
            ok = false;
            break;
        }
    }
    output.flush();
    ::close (descriptor);
    return ok && ! output.getStatus().failed();
#else
    return source.copyFileTo (destination);
#endif
}

bool readStreamBounded (juce::InputStream&, int64_t, juce::MemoryBlock&);

bool readFileBounded (const juce::File& file, int64_t expectedSize,
                      juce::MemoryBlock& result)
{
    if (expectedSize < 0 || expectedSize > maximumExpandedBytes || file.getSize() != expectedSize)
        return false;
    juce::FileInputStream input (file);
    if (! input.openedOk())
        return false;
    return readStreamBounded (input, expectedSize, result);
}

bool readStreamBounded (juce::InputStream& input, int64_t expectedSize,
                        juce::MemoryBlock& result)
{
    if (expectedSize < 0 || expectedSize > maximumExpandedBytes)
        return false;

    juce::MemoryOutputStream output (result, false);
    std::array<char, 32768> buffer {};
    int64_t total = 0;
    for (;;)
    {
        const auto allowance = static_cast<int> (
            std::min<int64_t> (static_cast<int64_t> (buffer.size()), expectedSize + 1 - total));
        if (allowance <= 0)
            return false;
        const auto amount = input.read (buffer.data(), allowance);
        if (amount < 0)
            return false;
        if (amount == 0)
            break;
        output.write (buffer.data(), static_cast<size_t> (amount));
        total += amount;
        if (total > expectedSize)
            return false;
    }
    return total == expectedSize;
}

uint16_t little16 (const uint8_t* bytes, size_t offset)
{
    return static_cast<uint16_t> (bytes[offset])
         | static_cast<uint16_t> (bytes[offset + 1] << 8);
}

uint32_t little32 (const uint8_t* bytes, size_t offset)
{
    return static_cast<uint32_t> (bytes[offset])
         | (static_cast<uint32_t> (bytes[offset + 1]) << 8)
         | (static_cast<uint32_t> (bytes[offset + 2]) << 16)
         | (static_cast<uint32_t> (bytes[offset + 3]) << 24);
}

bool rangeFits (size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

bool rawNameToString (const uint8_t* bytes, size_t length, juce::String& result)
{
    if (std::find (bytes, bytes + length, uint8_t {}) != bytes + length
        || ! juce::CharPointer_UTF8::isValidString (reinterpret_cast<const char*> (bytes),
                                                    static_cast<int> (length)))
        return false;
    result = juce::String::fromUTF8 (reinterpret_cast<const char*> (bytes),
                                     static_cast<int> (length));
    return static_cast<size_t> (result.getNumBytesAsUTF8()) == length;
}

bool indexArchive (const juce::File& archive,
                   std::map<juce::String, PackageEntry>& entries,
                   SkinValidationReport& report)
{
    if (archive.getSize() < 22 || archive.getSize() > maximumArchiveBytes)
        return addPathDiagnostic (report, "PKG003", archive.getFileName(),
                                  "Published skin archive exceeds the 25 MiB compressed limit.");

    juce::MemoryBlock data;
    if (! archive.loadFileAsData (data))
        return addPathDiagnostic (report, "PKG008", archive.getFileName(),
                                  "Skin archive could not be read.");

    const auto* bytes = static_cast<const uint8_t*> (data.getData());
    const auto size = data.getSize();
    size_t eocd = std::numeric_limits<size_t>::max();
    const auto earliest = size > 22 + 65535 ? size - 22 - 65535 : 0;
    for (size_t position = size - 22;; --position)
    {
        if (little32 (bytes, position) == 0x06054b50
            && rangeFits (position, 22, size)
            && static_cast<size_t> (little16 (bytes, position + 20)) == size - position - 22)
        {
            eocd = position;
            break;
        }
        if (position == earliest)
            break;
    }

    if (eocd == std::numeric_limits<size_t>::max())
        return addPathDiagnostic (report, "PKG006", archive.getFileName(),
                                  "ZIP end-of-central-directory record is invalid.");

    const auto disk = little16 (bytes, eocd + 4);
    const auto centralDisk = little16 (bytes, eocd + 6);
    const auto entriesOnDisk = little16 (bytes, eocd + 8);
    const auto entryCount = little16 (bytes, eocd + 10);
    const auto centralSize = static_cast<size_t> (little32 (bytes, eocd + 12));
    const auto centralOffset = static_cast<size_t> (little32 (bytes, eocd + 16));
    if (disk != 0 || centralDisk != 0 || entriesOnDisk != entryCount)
        return addPathDiagnostic (report, "PKG006", archive.getFileName(),
                                  "Multi-volume ZIP archives are not supported.");
    if (entryCount > maximumEntries)
        return addPathDiagnostic (report, "PKG004", archive.getFileName(),
                                  "Skin archive contains more than 2048 entries.");
    if (! rangeFits (centralOffset, centralSize, eocd)
        || centralOffset + centralSize != eocd)
        return addPathDiagnostic (report, "PKG006", archive.getFileName(),
                                  "ZIP central directory bounds are inconsistent.");

    std::vector<std::pair<juce::String, PackageEntry>> files;
    juce::StringArray directories;
    size_t position = centralOffset;
    for (uint16_t index = 0; index < entryCount; ++index)
    {
        if (! rangeFits (position, 46, eocd) || little32 (bytes, position) != 0x02014b50)
            return addPathDiagnostic (report, "PKG006", archive.getFileName(),
                                      "ZIP central directory entry is malformed.");

        const auto madeBy = bytes[position + 5];
        const auto flags = little16 (bytes, position + 8);
        const auto method = little16 (bytes, position + 10);
        const auto crc = little32 (bytes, position + 16);
        const auto compressed = little32 (bytes, position + 20);
        const auto expanded = little32 (bytes, position + 24);
        const auto nameLength = little16 (bytes, position + 28);
        const auto extraLength = little16 (bytes, position + 30);
        const auto commentLength = little16 (bytes, position + 32);
        const auto startDisk = little16 (bytes, position + 34);
        const auto externalAttributes = little32 (bytes, position + 38);
        const auto localOffset = static_cast<size_t> (little32 (bytes, position + 42));
        const auto centralEntrySize = static_cast<size_t> (46 + nameLength + extraLength + commentLength);

        if (! rangeFits (position, centralEntrySize, eocd)
            || compressed == 0xffffffffu || expanded == 0xffffffffu
            || localOffset == 0xffffffffu || startDisk != 0)
            return addPathDiagnostic (report, "PKG006", archive.getFileName(),
                                      "ZIP64 or split entries are outside Skin API 1 limits.");
        if ((flags & 0x0041u) != 0 || (flags & 0x0800u) == 0 || (method != 0 && method != 8))
            return addPathDiagnostic (report, "PKG006", archive.getFileName(),
                                      "Encrypted or unsupported ZIP entries are not allowed.");

        const auto* nameBytes = bytes + position + 46;
        juce::String path;
        if (! rawNameToString (nameBytes, nameLength, path))
            return addPathDiagnostic (report, "PKG001", archive.getFileName(),
                                      "ZIP entry name is not valid UTF-8 without NUL bytes.");

        if (! rangeFits (localOffset, 30, centralOffset)
            || little32 (bytes, localOffset) != 0x04034b50)
            return addPathDiagnostic (report, "PKG006", path,
                                      "ZIP local file header is missing or out of bounds.");

        const auto localFlags = little16 (bytes, localOffset + 6);
        const auto localMethod = little16 (bytes, localOffset + 8);
        const auto localCrc = little32 (bytes, localOffset + 14);
        const auto localCompressed = little32 (bytes, localOffset + 18);
        const auto localExpanded = little32 (bytes, localOffset + 22);
        const auto localNameLength = little16 (bytes, localOffset + 26);
        const auto localExtraLength = little16 (bytes, localOffset + 28);
        const auto dataOffset = localOffset + 30 + localNameLength + localExtraLength;
        if (localFlags != flags || localMethod != method
            || localNameLength != nameLength
            || ! rangeFits (localOffset + 30, localNameLength + localExtraLength, centralOffset)
            || std::memcmp (bytes + localOffset + 30, nameBytes, nameLength) != 0
            || ! rangeFits (dataOffset, compressed, centralOffset))
            return addPathDiagnostic (report, "PKG006", path,
                                      "ZIP local and central headers disagree.");

        if ((flags & 0x0008u) == 0)
        {
            if (localCrc != crc || localCompressed != compressed || localExpanded != expanded)
                return addPathDiagnostic (report, "PKG006", path,
                                          "ZIP local and central sizes disagree.");
        }
        else
        {
            if ((localCrc != 0 && localCrc != crc)
                || (localCompressed != 0 && localCompressed != compressed)
                || (localExpanded != 0 && localExpanded != expanded))
                return addPathDiagnostic (report, "PKG006", path,
                                          "ZIP data descriptor header values disagree.");

            auto descriptor = dataOffset + compressed;
            if (rangeFits (descriptor, 4, centralOffset)
                && little32 (bytes, descriptor) == 0x08074b50)
                descriptor += 4;
            if (! rangeFits (descriptor, 12, centralOffset)
                || little32 (bytes, descriptor) != crc
                || little32 (bytes, descriptor + 4) != compressed
                || little32 (bytes, descriptor + 8) != expanded)
                return addPathDiagnostic (report, "PKG006", path,
                                          "ZIP data descriptor is missing or inconsistent.");
        }

        const auto unixMode = externalAttributes >> 16;
        const auto fileType = unixMode & 0170000u;
        if ((madeBy == 3 || madeBy == 19)
            && fileType != 0 && fileType != 0100000u && fileType != 0040000u)
            return addPathDiagnostic (report, "PKG002", path,
                                      "ZIP entries may not be links or special files.");

        const auto isDirectory = path.endsWithChar ('/');
        if (isDirectory)
        {
            const auto trimmed = path.dropLastCharacters (1);
            if (trimmed.isEmpty())
                return addPathDiagnostic (report, "PKG001", path,
                                          "ZIP root directory entries are not valid package paths.");
            directories.add (trimmed);
        }
        else
        {
            files.push_back ({ path, { static_cast<int64_t> (expanded) } });
        }

        position += centralEntrySize;
    }

    if (position != centralOffset + centralSize
        || ! validateEntryMetadata (files, directories, report))
        return false;

    juce::ZipFile zip (archive);
    if (zip.getNumEntries() != entryCount)
        return addPathDiagnostic (report, "PKG008", archive.getFileName(),
                                  "ZIP entry index could not be read consistently.");

    entries.clear();
    for (const auto& [path, entry] : files)
        entries.emplace (path, entry);
    return true;
}

struct SourceFile
{
    std::filesystem::path source;
    juce::String relativePath;
    int64_t size = 0;
};

bool captureDirectory (const juce::File& source, const juce::File& snapshot,
                       std::map<juce::String, PackageEntry>& entries,
                       SkinValidationReport& report)
{
    std::error_code error;
    const std::filesystem::path sourcePath (source.getFullPathName().toStdString());
    const auto rootStatus = std::filesystem::symlink_status (sourcePath, error);
    if (error || ! std::filesystem::is_directory (rootStatus)
        || std::filesystem::is_symlink (rootStatus))
        return addPathDiagnostic (report, "PKG002", source.getFullPathName(),
                                  "Skin development directory may not be a symbolic link.");

    std::vector<SourceFile> sourceFiles;
    std::vector<std::pair<juce::String, PackageEntry>> metadata;
    juce::StringArray directories;
    std::filesystem::recursive_directory_iterator iterator (
        sourcePath, std::filesystem::directory_options::none, error), end;
    for (; ! error && iterator != end; iterator.increment (error))
    {
        const auto status = iterator->symlink_status (error);
        if (error)
            break;

        const auto relativeBytes = iterator->path().lexically_relative (sourcePath).generic_string();
        const auto relative = juce::String::fromUTF8 (relativeBytes.data(),
                                                       static_cast<int> (relativeBytes.size()));
        if (std::filesystem::is_symlink (status))
            return addPathDiagnostic (report, "PKG002", relative,
                                      "Skin directories may not contain symbolic links.");

        if (std::filesystem::is_directory (status))
        {
            directories.add (relative);
            continue;
        }
        if (! std::filesystem::is_regular_file (status))
            return addPathDiagnostic (report, "PKG002", relative,
                                      "Skin directories may contain regular files only.");

        const auto links = std::filesystem::hard_link_count (iterator->path(), error);
        if (error || links > 1)
            return addPathDiagnostic (report, "PKG002", relative,
                                      "Skin directories may not contain hard links.");
        const auto size = std::filesystem::file_size (iterator->path(), error);
        if (error || size > static_cast<uintmax_t> (std::numeric_limits<int64_t>::max()))
            return addPathDiagnostic (report, "PKG008", relative,
                                      "Skin source file size could not be read.");

        metadata.push_back ({ relative, { static_cast<int64_t> (size) } });
        sourceFiles.push_back ({ iterator->path(), relative, static_cast<int64_t> (size) });
    }
    if (error)
        return addPathDiagnostic (report, "PKG008", source.getFullPathName(),
                                  "Skin source directory could not be enumerated.");
    if (! validateEntryMetadata (metadata, directories, report))
        return false;

    for (const auto& directory : directories)
        if (! snapshot.getChildFile (directory).createDirectory())
            return addPathDiagnostic (report, "PKG010", directory,
                                      "Private snapshot directory could not be created.");

    for (const auto& file : sourceFiles)
    {
        if (! copyRegularFileNoFollow (juce::File (file.source.string()),
                                       snapshot.getChildFile (file.relativePath), true))
            return addPathDiagnostic (report, "PKG002", file.relativePath,
                                      "Source changed or became a link while the snapshot was captured.");
        if (snapshot.getChildFile (file.relativePath).getSize() != file.size)
            return addPathDiagnostic (report, "PKG010", file.relativePath,
                                      "Private snapshot file size changed during capture.");
    }

    entries.clear();
    for (const auto& [path, entry] : metadata)
        entries.emplace (path, entry);
    return true;
}

bool indexDirectorySnapshot (const juce::File& root,
                             std::map<juce::String, PackageEntry>& entries,
                             SkinValidationReport& report)
{
    std::error_code error;
    const std::filesystem::path rootPath (root.getFullPathName().toStdString());
    std::vector<std::pair<juce::String, PackageEntry>> metadata;
    juce::StringArray directories;
    std::filesystem::recursive_directory_iterator iterator (
        rootPath, std::filesystem::directory_options::none, error), end;
    for (; ! error && iterator != end; iterator.increment (error))
    {
        const auto status = iterator->symlink_status (error);
        if (error)
            break;
        const auto relativeBytes = iterator->path().lexically_relative (rootPath).generic_string();
        const auto relative = juce::String::fromUTF8 (relativeBytes.data(),
                                                       static_cast<int> (relativeBytes.size()));
        if (std::filesystem::is_symlink (status))
            return addPathDiagnostic (report, "PKG002", relative,
                                      "Snapshot unexpectedly contains a symbolic link.");
        if (std::filesystem::is_directory (status))
        {
            directories.add (relative);
            continue;
        }
        if (! std::filesystem::is_regular_file (status))
            return addPathDiagnostic (report, "PKG002", relative,
                                      "Snapshot unexpectedly contains a special file.");
        const auto links = std::filesystem::hard_link_count (iterator->path(), error);
        const auto size = std::filesystem::file_size (iterator->path(), error);
        if (error || links > 1 || size > static_cast<uintmax_t> (std::numeric_limits<int64_t>::max()))
            return addPathDiagnostic (report, links > 1 ? "PKG002" : "PKG008", relative,
                                      "Snapshot file metadata is invalid.");
        metadata.push_back ({ relative, { static_cast<int64_t> (size) } });
    }
    if (error || ! validateEntryMetadata (metadata, directories, report))
        return false;
    entries.clear();
    for (const auto& [path, entry] : metadata)
        entries.emplace (path, entry);
    return true;
}

bool readPackageEntry (const PackageStateData& state, const juce::String& path,
                       int64_t expectedSize, juce::MemoryBlock& result)
{
    switch (state.mode)
    {
        case PackageStateData::Mode::embedded:
        {
            const auto found = state.embeddedFiles.find (path);
            if (found == state.embeddedFiles.end())
                return false;
            result = found->second;
            return static_cast<int64_t> (result.getSize()) == expectedSize;
        }
        case PackageStateData::Mode::directory:
            return readFileBounded (state.root.getChildFile (path), expectedSize, result);
        case PackageStateData::Mode::archive:
        {
            juce::ZipFile archive (state.archiveFile);
            const auto* entry = archive.getEntry (path, false);
            if (entry == nullptr || entry->isSymbolicLink
                || entry->uncompressedSize != expectedSize)
                return false;
            std::unique_ptr<juce::InputStream> input (archive.createStreamForEntry (*entry));
            return input != nullptr && readStreamBounded (*input, expectedSize, result);
        }
    }
    return false;
}

std::optional<juce::String> canonicalDigest (
    const PackageStateData& state,
    const std::map<juce::String, PackageEntry>& entries)
{
    juce::MemoryOutputStream canonical;
    for (const auto& [path, entry] : entries)
    {
        juce::MemoryBlock contents;
        if (! readPackageEntry (state, path, entry.size, contents))
            return std::nullopt;
        canonical.writeInt64BigEndian (static_cast<int64_t> (path.getNumBytesAsUTF8()));
        canonical.write (path.toRawUTF8(), path.getNumBytesAsUTF8());
        canonical.writeInt64BigEndian (entry.size);
        canonical.write (contents.getData(), contents.getSize());
    }
    return juce::SHA256 (canonical.getData(), canonical.getDataSize()).toHexString();
}

bool lockSnapshotContents (const PackageStateData& state)
{
    if (state.mode == PackageStateData::Mode::archive)
        return setOwnerPermissions (state.archiveFile, 0400);
    if (state.mode != PackageStateData::Mode::directory)
        return true;

    bool succeeded = true;
    std::error_code error;
    const std::filesystem::path rootPath (state.root.getFullPathName().toStdString());
    for (std::filesystem::recursive_directory_iterator iterator (
             rootPath, std::filesystem::directory_options::none, error), end;
         ! error && iterator != end; iterator.increment (error))
    {
        succeeded = setOwnerPermissions (juce::File (iterator->path().string()),
                                         iterator->is_directory (error) ? 0500 : 0400)
                 && succeeded;
        if (error)
            error.clear();
    }
    return succeeded && ! error;
}
} // namespace

struct SkinPackage::State final : PackageStateData
{
    ~State()
    {
        if (mode != Mode::embedded)
            makeWritableAndRemove (root);
    }
};

SkinPackage::SkinPackage (std::shared_ptr<State> stateIn)
    : state (std::move (stateIn))
{
}

std::optional<SkinPackage> SkinPackage::captureSnapshot (
    const juce::File& source, const juce::File& privateTemporaryRoot,
    SkinValidationReport& report)
{
    if (source.isSymbolicLink())
    {
        report.addError ("PKG002", source.getFullPathName(), {},
                         "Skin source may not be a symbolic link.");
        return std::nullopt;
    }
    if (! source.exists()
        || (! source.isDirectory()
            && ! source.hasFileExtension (MoonwakeIdentity::skinArchiveExtension)))
    {
        report.addError ("PKG007", source.getFullPathName(), {},
                         "Input is neither a development directory nor a readable .moonwake-skin archive.");
        return std::nullopt;
    }

    if ((privateTemporaryRoot.exists() && (! privateTemporaryRoot.isDirectory()
                                           || privateTemporaryRoot.isSymbolicLink()))
        || (! privateTemporaryRoot.exists() && ! privateTemporaryRoot.createDirectory())
        || ! setOwnerPermissions (privateTemporaryRoot, 0700))
    {
        report.addError ("PKG010", privateTemporaryRoot.getFullPathName(), {},
                         "Private snapshot root could not be secured.");
        return std::nullopt;
    }

    const auto snapshot = privateTemporaryRoot.getChildFile (juce::Uuid().toString());
    if (! snapshot.createDirectory() || ! setOwnerPermissions (snapshot, 0700))
    {
        report.addError ("PKG010", snapshot.getFullPathName(), {},
                         "Private 0700 snapshot directory could not be created.");
        makeWritableAndRemove (snapshot);
        return std::nullopt;
    }

    auto captured = std::make_shared<State>();
    captured->root = snapshot;
    bool indexed = false;
    if (source.isDirectory())
    {
        captured->mode = PackageStateData::Mode::directory;
        indexed = captureDirectory (source, snapshot, captured->entries, report);
    }
    else
    {
        captured->mode = PackageStateData::Mode::archive;
        captured->archiveFile = snapshot.getChildFile (
            "package" + juce::String (MoonwakeIdentity::skinArchiveExtension));
        if (source.getSize() > maximumArchiveBytes)
            report.addError ("PKG003", source.getFileName(), {},
                             "Published skin archive exceeds the 25 MiB compressed limit.");
        else if (! copyRegularFileNoFollow (source, captured->archiveFile, false))
            report.addError ("PKG010", source.getFileName(), {},
                             "Published skin archive could not be captured safely.");
        else
            indexed = indexArchive (captured->archiveFile, captured->entries, report);
    }

    if (! indexed)
        return std::nullopt;

    const auto digest = canonicalDigest (*captured, captured->entries);
    if (! digest)
    {
        report.addError ("PKG011", {}, {},
                         "Canonical snapshot SHA-256 could not be generated.");
        return std::nullopt;
    }

    captured->digest = *digest;
    if (! lockSnapshotContents (*captured))
    {
        report.addError ("PKG010", snapshot.getFullPathName(), {},
                         "Private snapshot could not be made read-only.");
        return std::nullopt;
    }
    return SkinPackage (std::move (captured));
}

std::optional<SkinPackage> SkinPackage::fromEmbedded (
    juce::String packageId, std::vector<EmbeddedFile> files,
    SkinValidationReport& report)
{
    std::vector<std::pair<juce::String, PackageEntry>> metadata;
    metadata.reserve (files.size());
    for (const auto& file : files)
        metadata.push_back ({ file.path, { static_cast<int64_t> (file.bytes.getSize()) } });

    if (! validateEntryMetadata (metadata, {}, report))
        return std::nullopt;

    auto embedded = std::make_shared<State>();
    embedded->mode = PackageStateData::Mode::embedded;
    embedded->packageId = std::move (packageId);
    for (auto& file : files)
    {
        embedded->entries.emplace (file.path,
                                   PackageEntry { static_cast<int64_t> (file.bytes.getSize()) });
        embedded->embeddedFiles.emplace (std::move (file.path), std::move (file.bytes));
    }

    const auto digest = canonicalDigest (*embedded, embedded->entries);
    if (! digest)
    {
        report.addError ("PKG011", embedded->packageId, {},
                         "Embedded package SHA-256 could not be generated.");
        return std::nullopt;
    }
    embedded->digest = *digest;
    return SkinPackage (std::move (embedded));
}

bool SkinPackage::isArchive() const
{
    return state->mode == PackageStateData::Mode::archive;
}

juce::String SkinPackage::readText (const juce::String& relativePath,
                                    SkinValidationReport& report) const
{
    const auto diagnosticCount = report.getDiagnostics().size();
    const auto bytes = readBinary (relativePath, report);
    if (report.getDiagnostics().size() != diagnosticCount)
        return {};
    return juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()),
                                   static_cast<int> (bytes.getSize()));
}

juce::MemoryBlock SkinPackage::readBinary (const juce::String& relativePath,
                                           SkinValidationReport& report) const
{
    if (! isSafeRelativePath (relativePath))
    {
        report.addError ("PKG001", relativePath, {},
                         "Requested package path is not a safe canonical relative path.");
        return {};
    }

    const auto found = state->entries.find (relativePath);
    if (found == state->entries.end())
    {
        report.addError ("PKG008", relativePath, {}, "Package entry does not exist.");
        return {};
    }

    juce::MemoryBlock result;
    if (! readPackageEntry (*state, relativePath, found->second.size, result))
    {
        report.addError ("PKG008", relativePath, {},
                         "Package entry could not be read within its declared bound.");
        return {};
    }
    return result;
}

bool SkinPackage::contains (const juce::String& relativePath) const
{
    return isSafeRelativePath (relativePath) && state->entries.contains (relativePath);
}

juce::StringArray SkinPackage::listFiles() const
{
    juce::StringArray result;
    for (const auto& [path, ignored] : state->entries)
    {
        juce::ignoreUnused (ignored);
        result.add (path);
    }
    return result;
}

const juce::File& SkinPackage::snapshotRoot() const
{
    return state->root;
}

juce::String SkinPackage::sha256() const
{
    return state->digest;
}

bool SkinPackage::verifySnapshotDigest (SkinValidationReport& report) const
{
    if (state->mode == PackageStateData::Mode::embedded)
        return true;

    SkinValidationReport scratchReport;
    std::map<juce::String, PackageEntry> currentEntries;
    const auto indexed = state->mode == PackageStateData::Mode::archive
        ? indexArchive (state->archiveFile, currentEntries, scratchReport)
        : indexDirectorySnapshot (state->root, currentEntries, scratchReport);
    const auto current = indexed ? canonicalDigest (*state, currentEntries)
                                 : std::optional<juce::String>();
    if (! current || *current != state->digest)
    {
        report.addError ("PKG012", {}, {},
                         "Private skin snapshot changed after capture.",
                         "Reload the original skin to capture a fresh snapshot.");
        return false;
    }
    return true;
}

bool SkinPackage::isSafeRelativePath (const juce::String& path)
{
    return inspectPath (path) == PathIssue::none;
}
