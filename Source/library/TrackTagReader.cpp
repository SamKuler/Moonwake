#include "TrackTagReader.h"

#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace
{
using Bytes = std::span<const unsigned char>;
constexpr size_t maximumTagBytes = 16 * 1024 * 1024;
constexpr size_t maximumCommentBytes = 1024 * 1024;
constexpr size_t maximumTextBytes = 64 * 1024;
enum class TagField { artist, lyrics, metadata };

juce::String id3MetadataKey (Bytes frame)
{
    if (std::memcmp (frame.data(), "TIT2", 4) == 0) return "title";
    if (std::memcmp (frame.data(), "TALB", 4) == 0) return "album";
    if (std::memcmp (frame.data(), "TPE2", 4) == 0) return "albumartist";
    if (std::memcmp (frame.data(), "TCON", 4) == 0) return "genre";
    if (std::memcmp (frame.data(), "TDRC", 4) == 0) return "date";
    if (std::memcmp (frame.data(), "TYER", 4) == 0) return "year";
    if (std::memcmp (frame.data(), "TRCK", 4) == 0) return "tracknumber";
    if (std::memcmp (frame.data(), "TPOS", 4) == 0) return "discnumber";
    return {};
}

bool isMetadataKey (const juce::String& key)
{
    return key == "title" || key == "album" || key == "albumartist" || key == "genre"
        || key == "date" || key == "year" || key == "tracknumber" || key == "discnumber"
        || key == "replaygaintrackgain" || key == "replaygaintrackpeak"
        || key == "replaygainalbumgain" || key == "replaygainalbumpeak";
}

bool startsWith (Bytes bytes, const char* prefix, size_t length)
{
    return bytes.size() >= length && std::memcmp (bytes.data(), prefix, length) == 0;
}

uint32_t bigEndian (Bytes bytes, bool syncSafe = false)
{
    uint32_t value = 0;
    for (const auto byte : bytes)
    {
        if (syncSafe && byte >= 128) return UINT32_MAX;
        value = (value << (syncSafe ? 7 : 8)) | byte;
    }
    return value;
}

bool readBytes (juce::InputStream& input, size_t count, std::vector<unsigned char>& bytes)
{
    if (count > maximumTagBytes || static_cast<juce::int64> (count) > input.getTotalLength() - input.getPosition())
        return false;
    bytes.resize (count);
    return count == 0 || input.read (bytes.data(), static_cast<int> (count)) == static_cast<int> (count);
}

void addArtist (juce::StringArray& result, juce::String name)
{
    name = name.substring (0, 1024).trim().replaceCharacters ("\r\n\t", "   ");
    while (name.contains ("  ")) name = name.replace ("  ", " ");
    if (name.isNotEmpty() && result.size() < 128)
        result.addIfNotAlreadyThere (name);
}

std::vector<unsigned char> removeUnsynchronisation (Bytes bytes)
{
    std::vector<unsigned char> result;
    result.reserve (bytes.size());
    for (size_t index = 0; index < bytes.size(); ++index)
    {
        result.push_back (bytes[index]);
        if (bytes[index] == 0xff && index + 1 < bytes.size() && bytes[index + 1] == 0)
            ++index;
    }
    return result;
}

juce::StringArray id3Text (Bytes bytes, int version, TagField field)
{
    if (bytes.empty() || bytes.size() > (field == TagField::lyrics ? maximumCommentBytes : maximumTextBytes)) return {};
    const auto encoding = bytes.front();
    if (encoding > (version == 3 ? 1 : 3)) return {};
    bytes = bytes.subspan (1);
    juce::StringArray values;
    if (encoding == 0 || encoding == 3)
    {
        for (size_t begin = 0; begin < bytes.size();)
        {
            auto end = begin;
            while (end < bytes.size() && bytes[end] != 0) ++end;
            const auto* data = reinterpret_cast<const char*> (bytes.data() + begin);
            juce::String value;
            if (encoding == 3)
            {
                if (! juce::CharPointer_UTF8::isValidString (data, static_cast<int> (end - begin))) return {};
                value = juce::String::fromUTF8 (data, static_cast<int> (end - begin));
            }
            else
                for (auto index = begin; index < end; ++index)
                    value += juce::String::charToString (bytes[index]);
            values.add (value);
            if (version == 3 && field == TagField::artist) break;
            begin = end + 1;
        }
    }
    else
    {
        if (bytes.size() % 2 != 0) return {};
        bool littleEndian = false;
        bool hasByteOrder = encoding == 2;
        std::vector<juce::CharPointer_UTF16::CharType> units;
        const auto appendValue = [&]
        {
            units.push_back (0);
            if (! juce::CharPointer_UTF16::isValidString (units.data(),
                    static_cast<int> (units.size() * sizeof (units.front())))) return false;
            values.add (juce::String (juce::CharPointer_UTF16 (units.data())));
            units.clear();
            return true;
        };
        for (size_t offset = 0; offset < bytes.size(); offset += 2)
        {
            if (encoding == 1 && units.empty()
                && ((bytes[offset] == 0xff && bytes[offset + 1] == 0xfe)
                    || (bytes[offset] == 0xfe && bytes[offset + 1] == 0xff)))
            {
                littleEndian = bytes[offset] == 0xff;
                hasByteOrder = true;
                continue;
            }
            if (! hasByteOrder) return {};
            const auto unit = static_cast<juce::CharPointer_UTF16::CharType> (littleEndian
                ? bytes[offset] | (bytes[offset + 1] << 8)
                : (bytes[offset] << 8) | bytes[offset + 1]);
            if (unit == 0)
            {
                if (! appendValue()) return {};
                if (version == 3 && field == TagField::artist) break;
            }
            else units.push_back (unit);
        }
        if (! units.empty() && ! appendValue()) return {};
    }
    if (field != TagField::artist) return values;
    juce::StringArray result;
    for (const auto& value : values)
    {
        if (version == 3)
            for (const auto& member : juce::StringArray::fromTokens (value, "/", "")) addArtist (result, member);
        else addArtist (result, value);
    }
    return result;
}

juce::StringArray readId3 (juce::InputStream& input, Bytes header, TagField field, juce::StringPairArray* metadata = nullptr)
{
    const auto version = header[3];
    if ((version != 3 && version != 4) || header[4] == 0xff
        || (header[5] & (version == 3 ? 0x1f : 0x0f)) != 0) return {};
    const auto size = bigEndian (header.subspan (6, 4), true);
    std::vector<unsigned char> storage;
    if (! readBytes (input, size, storage)) return {};
    if (version == 3 && (header[5] & 0x80) != 0)
        storage = removeUnsynchronisation (storage);
    Bytes frames = storage;
    if ((header[5] & 0x40) != 0)
    {
        if (frames.size() < 4) return {};
        const auto extended = static_cast<uint64_t> (bigEndian (frames.first (4), version == 4))
                            + (version == 3 ? 4u : 0u);
        if (extended < (version == 3 ? 10u : 6u) || extended > frames.size()) return {};
        frames = frames.subspan (static_cast<size_t> (extended));
    }
    juce::StringPairArray collected;
    while (frames.size() >= 10 && frames[0] != 0)
    {
        const auto frameSize = bigEndian (frames.subspan (4, 4), version == 4);
        if (frameSize == 0 || frameSize > frames.size() - 10) return {};
        auto metadataKey = field == TagField::metadata ? id3MetadataKey (frames) : juce::String {};
        const auto custom = field == TagField::metadata && startsWith (frames, "TXXX", 4);
        if (field == TagField::metadata ? (metadataKey.isNotEmpty() || custom)
                                       : startsWith (frames, field == TagField::artist ? "TPE1" : "USLT", 4))
        {
            const auto flags = frames[9];
            // Compressed or encrypted frames remain the decoder's responsibility.
            if ((flags & (version == 3 ? 0xdf : 0xbc)) != 0 || frameSize > (field == TagField::lyrics ? maximumCommentBytes : maximumTextBytes)) return {};
            auto text = frames.subspan (10, frameSize);
            std::vector<unsigned char> decoded;
            if (version == 4 && ((header[5] & 0x80) != 0 || (flags & 0x02) != 0))
            {
                decoded = removeUnsynchronisation (text);
                text = decoded;
            }
            const auto prefix = static_cast<size_t> ((flags & (version == 3 ? 0x20 : 0x40)) != 0 ? 1 : 0)
                              + static_cast<size_t> (version == 4 && (flags & 0x01) != 0 ? 4 : 0);
            if (prefix > text.size()) return {};
            text = text.subspan (prefix);
            if (field == TagField::lyrics)
            {
                if (text.size() < 5) return {};
                const auto encoding = text.front();
                const bool wide = encoding == 1 || encoding == 2;
                size_t end = 4;
                for (; end + (wide ? 1u : 0u) < text.size(); end += wide ? 2u : 1u)
                    if (text[end] == 0 && (! wide || text[end + 1] == 0)) break;
                const auto content = end + (wide ? 2u : 1u);
                if (content >= text.size()) return {};
                std::vector<unsigned char> value { encoding };
                // UTF-16 text may inherit the byte order of the descriptor.
                if (encoding == 1 && text.size() >= 6
                    && ((text[4] == 0xff && text[5] == 0xfe) || (text[4] == 0xfe && text[5] == 0xff)))
                    value.insert (value.end(), text.begin() + 4, text.begin() + 6);
                value.insert (value.end(), text.begin() + static_cast<std::ptrdiff_t> (content), text.end());
                return id3Text (value, version, field);
            }
            const auto values = id3Text (text, version, field);
            if (custom)
            {
                if (values.size() >= 2)
                {
                    metadataKey = values[0].toLowerCase().removeCharacters (" _-");
                    if (isMetadataKey (metadataKey) && ! collected.containsKey (metadataKey))
                        collected.set (metadataKey, values[1].trim());
                }
                frames = frames.subspan (10 + frameSize);
                continue;
            }
            if (field != TagField::metadata) return values;
            if (! values.isEmpty() && values[0].trim().isNotEmpty() && ! collected.containsKey (metadataKey))
                collected.set (metadataKey, values[0].trim());
        }
        frames = frames.subspan (10 + frameSize);
    }
    if (metadata != nullptr) *metadata = std::move (collected);
    return {};
}

juce::StringArray vorbisComments (Bytes bytes, TagField field, juce::StringPairArray* metadata = nullptr)
{
    const auto readLength = [&bytes] (uint32_t& value)
    {
        if (bytes.size() < 4) return false;
        value = juce::ByteOrder::littleEndianInt (bytes.data());
        bytes = bytes.subspan (4);
        return true;
    };
    uint32_t vendorLength = 0, count = 0;
    if (! readLength (vendorLength) || vendorLength > bytes.size()) return {};
    bytes = bytes.subspan (vendorLength);
    if (! readLength (count) || count > bytes.size() / 4) return {};
    juce::StringArray result;
    juce::StringPairArray collected;
    for (uint32_t index = 0; index < count; ++index)
    {
        uint32_t length = 0;
        if (! readLength (length) || length > bytes.size()) return {};
        if (length <= (field == TagField::lyrics ? maximumCommentBytes : maximumTextBytes))
        {
            const auto* data = reinterpret_cast<const char*> (bytes.data());
            if (juce::CharPointer_UTF8::isValidString (data, static_cast<int> (length)))
            {
                const auto comment = juce::String::fromUTF8 (data, static_cast<int> (length));
                if (field == TagField::metadata)
                {
                    const auto separator = comment.indexOfChar ('=');
                    const auto key = comment.substring (0, separator).toLowerCase().removeCharacters (" _-");
                    if (separator > 0 && isMetadataKey (key) && ! collected.containsKey (key))
                        collected.set (key, comment.substring (separator + 1).trim());
                }
                if (field == TagField::artist && comment.startsWithIgnoreCase ("ARTIST="))
                    addArtist (result, comment.substring (7));
                if (field == TagField::lyrics && result.size() < 16)
                {
                    if (comment.startsWithIgnoreCase ("LYRICS=")) result.insert (0, comment.substring (7));
                    else if (comment.startsWithIgnoreCase ("UNSYNCEDLYRICS=")) result.add (comment.substring (15));
                }
            }
        }
        bytes = bytes.subspan (length);
    }
    if (metadata != nullptr) *metadata = std::move (collected);
    return result;
}

juce::StringArray readFlac (juce::InputStream& input, TagField field, juce::StringPairArray* metadata = nullptr)
{
    while (input.getPosition() + 4 <= static_cast<juce::int64> (maximumTagBytes))
    {
        std::array<unsigned char, 4> header {};
        if (input.read (header.data(), 4) != 4) return {};
        const auto size = bigEndian (Bytes (header).subspan (1));
        if (input.getPosition() + size > static_cast<juce::int64> (maximumTagBytes)) return {};
        if ((header[0] & 0x7f) == 4)
        {
            std::vector<unsigned char> comments;
            return size <= maximumCommentBytes && readBytes (input, size, comments)
                ? vorbisComments (comments, field, metadata) : juce::StringArray {};
        }
        if ((header[0] & 0x80) != 0 || ! input.setPosition (input.getPosition() + size)) return {};
    }
    return {};
}

juce::StringArray readOgg (juce::InputStream& input, TagField field, juce::StringPairArray* metadata = nullptr)
{
    std::vector<unsigned char> packet;
    uint32_t serial = 0, sequence = 0;
    int completedPackets = 0;
    while (input.getPosition() + 27 <= static_cast<juce::int64> (maximumTagBytes))
    {
        std::array<unsigned char, 27> header {};
        if (input.read (header.data(), 27) != 27 || ! startsWith (header, "OggS", 4) || header[4] != 0) return {};
        const auto pageSerial = juce::ByteOrder::littleEndianInt (header.data() + 14);
        const auto pageSequence = juce::ByteOrder::littleEndianInt (header.data() + 18);
        if (input.getPosition() == 27)
        {
            if ((header[5] & 2) == 0) return {};
            serial = pageSerial;
            sequence = pageSequence;
        }
        if (pageSerial != serial || pageSequence != sequence++
            || ((header[5] & 1) != 0) != ! packet.empty()) return {};
        std::vector<unsigned char> segments;
        if (! readBytes (input, header[26], segments)) return {};
        for (const auto length : segments)
        {
            if (packet.size() + length > maximumCommentBytes
                || input.getPosition() + length > static_cast<juce::int64> (maximumTagBytes)) return {};
            const auto offset = packet.size();
            packet.resize (offset + length);
            if (length > 0 && input.read (packet.data() + offset, length) != length) return {};
            if (length == 255) continue;
            ++completedPackets;
            if (completedPackets == 1 && ! startsWith (packet, "\1vorbis", 7)) return {};
            if (completedPackets == 2)
                return startsWith (packet, "\3vorbis", 7) ? vorbisComments (Bytes (packet).subspan (7), field, metadata) : juce::StringArray {};
            packet.clear();
        }
    }
    return {};
}

juce::StringArray readTags (const juce::File& file, TagField field, juce::StringPairArray* metadata = nullptr)
{
    juce::FileInputStream input (file);
    std::array<unsigned char, 10> header {};
    if (! input.openedOk() || input.read (header.data(), 10) != 10) return {};
    if (startsWith (header, "ID3", 3)) return readId3 (input, header, field, metadata);
    if (startsWith (header, "fLaC", 4) && input.setPosition (4)) return readFlac (input, field, metadata);
    if (startsWith (header, "OggS", 4) && input.setPosition (0)) return readOgg (input, field, metadata);
    return {};
}

}

juce::StringArray readTrackArtistTags (const juce::File& file)
{
    return readTags (file, TagField::artist);
}

juce::String readTrackLyricsTag (const juce::File& file)
{
    for (const auto& text : readTags (file, TagField::lyrics))
        if (text.trim().isNotEmpty()) return text;
    return {};
}

juce::StringPairArray readTrackMetadataTags (const juce::File& file)
{
    juce::StringPairArray result;
    static_cast<void> (readTags (file, TagField::metadata, &result));
    return result;
}
