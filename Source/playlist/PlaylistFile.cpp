#include "PlaylistFile.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace
{
bool isValidUtf8Text (const juce::MemoryBlock& bytes)
{
    const auto* data = static_cast<const char*> (bytes.getData());
    const auto size = bytes.getSize();
    if (! juce::CharPointer_UTF8::isValidString (data, static_cast<int> (size)))
        return false;

    for (size_t i = 0; i < size; ++i)
    {
        const auto byte = static_cast<unsigned char> (data[i]);
        if ((byte < 0x20 && byte != '\t' && byte != '\r' && byte != '\n') || byte == 0x7f)
            return false;

        if (byte >= 0x80)
        {
            // JUCE validates scalar ranges and length; continuation bytes also
            // need checking before its permissive UTF-8 decoder is used.
            const auto continuationCount = byte >= 0xf0 ? 3u : byte >= 0xe0 ? 2u : 1u;
            for (unsigned int j = 0; j < continuationCount; ++j)
                if (++i >= size || (static_cast<unsigned char> (data[i]) & 0xc0) != 0x80)
                    return false;
        }
    }

    return true;
}

bool isUrlOrNetworkPath (const juce::String& path)
{
    if (path.startsWith ("//") || path.startsWith ("\\\\"))
        return true;
    if (juce::File::isAbsolutePath (path))
        return false;

    const auto colon = path.indexOfChar (':');
    if (colon <= 0)
        return false;

    const auto scheme = path.substring (0, colon);
    return scheme.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+.-")
        && juce::CharacterFunctions::isLetter (scheme[0]);
}

bool parseDuration (const juce::String& text, double& duration)
{
    const auto value = text.trim().toStdString();
    const auto parsed = std::from_chars (value.data(), value.data() + value.size(), duration);
    return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size()
        && std::isfinite (duration) && (duration >= 0.0 || juce::exactlyEqual (duration, -1.0));
}

bool readLocalFileUrl (const juce::String& reference, juce::File& file, juce::String& warning)
{
    const auto pathStart = reference.indexOfChar (7, '/');
    const auto authority = reference.substring (7, pathStart < 0 ? reference.length() : pathStart);
    if (authority.isNotEmpty() && ! authority.equalsIgnoreCase ("localhost"))
    {
        warning = "Remote file URL hosts are not supported: " + reference;
        return false;
    }
    if (pathStart < 0 || reference.containsAnyOf ("?#"))
    {
        warning = "Invalid local file URL; use an absolute path and percent-encode ? and #: " + reference;
        return false;
    }

    const auto path = reference.substring (pathStart);
    const auto encoded = path.toStdString();
    juce::MemoryOutputStream decoded;
    for (size_t i = 0; i < encoded.size(); ++i)
    {
        auto byte = encoded[i];
        if (byte == '%')
        {
            const auto high = i + 1 < encoded.size() ? juce::CharacterFunctions::getHexDigitValue (encoded[i + 1]) : -1;
            const auto low = i + 2 < encoded.size() ? juce::CharacterFunctions::getHexDigitValue (encoded[i + 2]) : -1;
            if (high < 0 || low < 0)
            {
                warning = "Invalid percent encoding in local file URL: " + reference;
                return false;
            }
            byte = static_cast<char> ((high << 4) | low);
            i += 2;
        }
        decoded.writeByte (byte);
    }
    if (! isValidUtf8Text (decoded.getMemoryBlock()))
    {
        warning = "Invalid UTF-8 or control character in local file URL: " + reference;
        return false;
    }
    if (isUrlOrNetworkPath (decoded.toUTF8()))
    {
        warning = "Network file URLs are not supported: " + reference;
        return false;
    }

    // JUCE treats localhost as a path component on POSIX, so normalize the
    // permitted local authority before decoding the file URL.
    file = juce::URL ("file://" + path).getLocalFile();
    return true;
}

bool containsUnrepresentablePathCharacter (const juce::String& path)
{
    for (auto character : path)
        if ((character < 0x20 && character != '\t') || character == 0x7f)
            return true;

    return false;
}

juce::String singleLineTitle (const juce::String& title)
{
    auto clean = title.toStdString();
    for (auto& character : clean)
        if (static_cast<unsigned char> (character) < 0x20 || character == 0x7f)
            character = ' ';
    return juce::String::fromUTF8 (clean.data(), static_cast<int> (clean.size()))
        .replaceCharacter (0x2028, ' ').replaceCharacter (0x2029, ' ');
}
} // namespace

PlaylistFileReadResult PlaylistFile::read (const juce::File& source)
{
    PlaylistFileReadResult result;
    const auto fail = [&result] (juce::String message)
    {
        result.entries.clear();
        result.error = std::move (message);
        return result;
    };
    const auto warn = [&result] (int lineNumber, const juce::String& message)
    {
        constexpr int maximumWarnings = 100;
        if (result.warnings.size() < maximumWarnings)
            result.warnings.add ("Line " + juce::String (lineNumber) + ": " + message);
        else if (result.warnings.size() == maximumWarnings)
            result.warnings.add ("Further playlist warnings omitted.");
    };

    if (! source.existsAsFile())
        return fail ("Playlist file is missing or is not a regular file: " + source.getFullPathName());

    if (source.getSize() > static_cast<juce::int64> (maximumFileBytes))
        return fail ("Playlist exceeds the 16 MiB file limit.");

    auto input = source.createInputStream();
    if (input == nullptr)
        return fail ("Could not open playlist: " + source.getFullPathName());

    juce::MemoryBlock bytes;
    input->readIntoMemoryBlock (bytes, static_cast<ssize_t> (maximumFileBytes + 1));
    if (input->getStatus().failed())
        return fail ("Could not read playlist: " + input->getStatus().getErrorMessage());
    if (bytes.getSize() > maximumFileBytes)
        return fail ("Playlist exceeds the 16 MiB file limit.");
    if (bytes.getSize() == 0)
        return result;
    if (! isValidUtf8Text (bytes))
        return fail ("Playlist must contain valid UTF-8 text without binary control characters.");

    juce::MemoryInputStream lines (bytes, false);
    if (bytes.getSize() >= 3 && std::memcmp (bytes.getData(), "\xef\xbb\xbf", 3) == 0)
        lines.setPosition (3);

    PlaylistFileEntry metadata;
    bool hasMetadata = false;
    size_t referenceCount = 0;
    int lineNumber = 0;
    while (! lines.isExhausted())
    {
        const auto line = lines.readNextLine();
        const auto directive = line.trimStart();
        ++lineNumber;
        if (directive.isEmpty())
            continue;

        if (directive.startsWithChar ('#'))
        {
            if (directive.startsWithIgnoreCase ("#EXT-X-"))
                return fail ("HLS streaming playlists are not supported.");

            if (directive.startsWithIgnoreCase ("#EXTINF:"))
            {
                if (hasMetadata)
                    warn (lineNumber, "Previous EXTINF has no file reference.");
                metadata = {};
                hasMetadata = true;
                const auto comma = directive.indexOfChar (',');
                if (comma < 0)
                {
                    warn (lineNumber, "Malformed EXTINF; expected duration,title.");
                    continue;
                }
                metadata.title = directive.substring (comma + 1);
                if (! parseDuration (directive.substring (8, comma), metadata.durationSeconds))
                {
                    metadata.durationSeconds = -1.0;
                    warn (lineNumber, "Invalid EXTINF duration; using unknown duration.");
                }
            }
            continue;
        }

        if (++referenceCount > maximumEntries)
            return fail ("Playlist exceeds the 100,000 file reference limit.");

        auto entry = std::move (metadata);
        metadata = {};
        hasMetadata = false;
        const auto reference = line.trim();
        if (reference.startsWithIgnoreCase ("file://"))
        {
            juce::String warning;
            if (! readLocalFileUrl (reference, entry.file, warning))
            {
                warn (lineNumber, warning);
                continue;
            }
        }
        else if (isUrlOrNetworkPath (reference))
        {
            warn (lineNumber, "Unsupported URL or network reference: " + line);
            continue;
        }
        else
        {
            // Prefixing relative references keeps a literal leading tilde from
            // invoking File's home-directory expansion.
            entry.file = source.getParentDirectory().getChildFile (
                juce::File::isAbsolutePath (line) ? line : "./" + line);
        }
        if (! entry.file.existsAsFile())
            warn (lineNumber, "Local file is missing or is not a regular file: " + entry.file.getFullPathName());
        result.entries.push_back (std::move (entry));
    }

    if (hasMetadata)
        warn (lineNumber, "EXTINF has no file reference.");
    return result;
}

bool PlaylistFile::writeM3u8 (const juce::File& target,
                              const std::vector<TrackItem>& tracks,
                              bool relativePaths,
                              juce::String& error)
{
    error.clear();
    if (target == juce::File() || target.isDirectory())
    {
        error = "Choose a playlist file as the export destination.";
        return false;
    }
    if (tracks.size() > maximumEntries)
    {
        error = "Playlist exceeds the 100,000 file reference limit.";
        return false;
    }

    juce::MemoryOutputStream contents;
    contents << "#EXTM3U\n";
    for (const auto& track : tracks)
    {
        if (track.file == juce::File() || containsUnrepresentablePathCharacter (track.file.getFullPathName()))
        {
            error = "Track has an empty path or control characters that M3U8 cannot represent: " + track.title;
            return false;
        }

        auto path = relativePaths ? track.file.getRelativePathFrom (target.getParentDirectory())
                                  : track.file.getFullPathName();
        // Cross-volume paths remain absolute; relative names may themselves begin with '~'.
        if (relativePaths && path != track.file.getFullPathName())
            path = "./" + path;

        const auto duration = std::isfinite (track.durationSeconds) && track.durationSeconds >= 0.0
            ? juce::String (track.durationSeconds, 6) : juce::String ("-1");
        const auto entry = "#EXTINF:" + duration + "," + singleLineTitle (track.title) + "\n" + path + "\n";
        if (contents.getDataSize() + entry.getNumBytesAsUTF8() > maximumFileBytes)
        {
            error = "Playlist exceeds the 16 MiB file limit.";
            return false;
        }
        contents << entry;
    }

    juce::TemporaryFile temporary (target);
    {
        juce::FileOutputStream output (temporary.getFile());
        if (output.failedToOpen())
        {
            error = "Could not create playlist export: " + output.getStatus().getErrorMessage();
            return false;
        }
        const auto written = output.write (contents.getData(), contents.getDataSize());
        output.flush();
        if (! written || output.getStatus().failed())
        {
            error = "Could not write playlist export: " + output.getStatus().getErrorMessage();
            return false;
        }
    }

    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "Could not replace playlist file: " + target.getFullPathName();
        return false;
    }
    return true;
}
