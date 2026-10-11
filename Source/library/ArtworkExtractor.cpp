#include "ArtworkExtractor.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace
{
using Bytes = std::span<const unsigned char>;

uint32_t tagSize (Bytes bytes, bool syncSafe)
{
    uint32_t result = 0;
    for (const auto byte : bytes)
    {
        if (syncSafe && byte >= 128) return UINT32_MAX;
        result = (result << (syncSafe ? 7 : 8)) | byte;
    }
    return result;
}

std::vector<unsigned char> unsynchronisedBytes (Bytes bytes)
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

juce::MemoryBlock pictureBytes (Bytes bytes, int version, bool& frontCover)
{
    if (bytes.empty() || bytes[0] > (version == 3 ? 1 : 3)) return {};
    const auto encoding = bytes[0];
    const auto mimeEnd = std::find (bytes.begin() + 1, bytes.end(), 0);
    const auto mimeSize = static_cast<size_t> (mimeEnd - bytes.begin()) - 1;
    if (mimeEnd == bytes.end() || mimeSize > 128) return {};
    if (! std::all_of (bytes.begin() + 1, mimeEnd, [] (auto byte) { return byte >= 32 && byte <= 126; }))
        return {};
    const auto mime = juce::String::fromUTF8 (reinterpret_cast<const char*> (bytes.data() + 1),
                                            static_cast<int> (mimeSize)).toLowerCase();
    // Linked pictures ("-->") are never followed. Decoding and dimension limits
    // remain ArtworkStore's responsibility, just as for decoder-provided images.
    if (mime != "image/png" && mime != "image/jpeg" && mime != "image/jpg") return {};
    auto offset = mimeSize + 2;
    if (offset >= bytes.size()) return {};
    frontCover = bytes[offset++] == 3;
    const size_t unit = encoding == 1 || encoding == 2 ? 2 : 1;
    for (; offset + unit <= bytes.size(); offset += unit)
        if (bytes[offset] == 0 && (unit == 1 || bytes[offset + 1] == 0))
        {
            const auto image = bytes.subspan (offset + unit);
            if (image.empty() || image.size() > ArtworkExtractor::maximumEncodedBytes) return {};
            return { image.data(), image.size() };
        }
    return {};
}

juce::MemoryBlock readId3Artwork (const juce::File& file)
{
    juce::FileInputStream input (file);
    std::array<unsigned char, 10> header {};
    if (! input.openedOk() || input.read (header.data(), 10) != 10
        || std::memcmp (header.data(), "ID3", 3) != 0) return {};
    const auto version = header[3];
    if ((version != 3 && version != 4) || header[4] == 0xff
        || (header[5] & (version == 3 ? 0x1f : 0x0f)) != 0) return {};
    const auto size = tagSize (Bytes (header).subspan (6, 4), true);
    constexpr uint32_t maximumTagBytes = 32 * 1024 * 1024;
    if (size > maximumTagBytes || static_cast<juce::int64> (size) > input.getTotalLength() - 10) return {};
    std::vector<unsigned char> storage (size);
    if (input.read (storage.data(), static_cast<int> (size)) != static_cast<int> (size)) return {};
    if (version == 3 && (header[5] & 0x80) != 0) storage = unsynchronisedBytes (storage);
    Bytes frames = storage;
    if ((header[5] & 0x40) != 0)
    {
        if (frames.size() < 4) return {};
        const auto extended = static_cast<uint64_t> (tagSize (frames.first (4), version == 4))
                            + (version == 3 ? 4u : 0u);
        if (extended < (version == 3 ? 10u : 6u) || extended > frames.size()) return {};
        frames = frames.subspan (static_cast<size_t> (extended));
    }
    juce::MemoryBlock fallback;
    while (frames.size() >= 10 && frames[0] != 0)
    {
        const auto frameSize = tagSize (frames.subspan (4, 4), version == 4);
        if (frameSize == 0 || frameSize > frames.size() - 10) return {};
        if (std::memcmp (frames.data(), "APIC", 4) == 0)
        {
            const auto flags = frames[9];
            // Do not interpret compressed, encrypted, or reserved frame formats.
            if ((frames[8] & (version == 3 ? 0x1f : 0x8f)) == 0
                && (flags & (version == 3 ? 0xdf : 0xbc)) == 0)
            {
                auto picture = frames.subspan (10, frameSize);
                std::vector<unsigned char> decoded;
                if (version == 4 && ((header[5] & 0x80) != 0 || (flags & 0x02) != 0))
                {
                    decoded = unsynchronisedBytes (picture);
                    picture = decoded;
                }
                const auto prefix = static_cast<size_t> ((flags & (version == 3 ? 0x20 : 0x40)) != 0 ? 1 : 0)
                                  + static_cast<size_t> (version == 4 && (flags & 0x01) != 0 ? 4 : 0);
                if (prefix <= picture.size())
                {
                    bool frontCover = false;
                    auto image = pictureBytes (picture.subspan (prefix), version, frontCover);
                    if (! image.isEmpty())
                    {
                        if (frontCover) return image;
                        if (fallback.isEmpty()) fallback = std::move (image);
                    }
                }
            }
        }
        frames = frames.subspan (10 + frameSize);
    }
    return fallback;
}

juce::MemoryBlock flacPictureBytes (Bytes bytes, bool& frontCover)
{
    // RFC 9639 section 8.8: all picture fields use big-endian lengths.
    if (bytes.size() < 32) return {};
    frontCover = tagSize (bytes.first (4), false) == 3;
    const auto mimeSize = tagSize (bytes.subspan (4, 4), false);
    if (mimeSize > 128 || mimeSize > bytes.size() - 8) return {};
    const auto mimeBytes = bytes.subspan (8, mimeSize);
    if (! std::all_of (mimeBytes.begin(), mimeBytes.end(), [] (auto byte) { return byte >= 32 && byte <= 126; }))
        return {};
    const auto mime = juce::String::fromUTF8 (reinterpret_cast<const char*> (mimeBytes.data()),
                                            static_cast<int> (mimeSize)).toLowerCase();
    // Do not follow linked pictures ("-->") or accept unsupported image formats.
    if (mime != "image/png" && mime != "image/jpeg" && mime != "image/jpg") return {};
    bytes = bytes.subspan (8 + mimeSize);
    if (bytes.size() < 4) return {};
    const auto descriptionSize = tagSize (bytes.first (4), false);
    if (descriptionSize > bytes.size() - 4) return {};
    bytes = bytes.subspan (4 + descriptionSize);
    // Ignore the informational dimensions; ArtworkStore validates the image itself.
    if (bytes.size() < 20) return {};
    const auto imageSize = tagSize (bytes.subspan (16, 4), false);
    if (imageSize == 0 || imageSize > ArtworkExtractor::maximumEncodedBytes
        || imageSize != bytes.size() - 20) return {};
    return { bytes.data() + 20, imageSize };
}

juce::MemoryBlock readFlacArtwork (const juce::File& file)
{
    juce::FileInputStream input (file);
    std::array<unsigned char, 4> header {};
    if (! input.openedOk() || input.read (header.data(), 4) != 4
        || std::memcmp (header.data(), "fLaC", 4) != 0) return {};
    constexpr juce::int64 maximumMetadataBytes = 32 * 1024 * 1024;
    juce::MemoryBlock fallback;
    for (int block = 0; block < 4096; ++block)
    {
        if (input.read (header.data(), 4) != 4) return {};
        const auto type = header[0] & 0x7f;
        const auto size = tagSize (Bytes (header).subspan (1), false);
        const auto end = input.getPosition() + size;
        if (end > input.getTotalLength() || end > maximumMetadataBytes || type == 127
            || (block == 0 && (type != 0 || size != 34))
            || (block != 0 && type == 0)) return {};
        if (type == 6)
        {
            // The 24-bit block length bounds allocation before parsing inner lengths.
            std::vector<unsigned char> picture (size);
            if (input.read (picture.data(), static_cast<int> (size)) != static_cast<int> (size)) return {};
            bool frontCover = false;
            auto image = flacPictureBytes (picture, frontCover);
            if (! image.isEmpty())
            {
                if (frontCover) return image;
                if (fallback.isEmpty()) fallback = std::move (image);
            }
        }
        else if (! input.setPosition (end)) return {};
        if ((header[0] & 0x80) != 0) return fallback;
    }
    return {};
}

juce::String normaliseKey (juce::String key)
{
    return key.trim().toLowerCase().removeCharacters (" _-./\\:()[]{}");
}

bool mayContainArtwork (const juce::String& key)
{
    const auto normalised = normaliseKey (key);
    return normalised.contains ("artwork")
        || normalised.contains ("picture")
        || normalised.contains ("cover");
}

juce::MemoryBlock decodeMetadataValue (juce::String value)
{
    value = value.trim();
    if (const auto comma = value.indexOfChar (',');
        value.startsWithIgnoreCase ("data:image/") && comma >= 0)
        value = value.substring (comma + 1);

    if (value.isEmpty() || value.length() > static_cast<int> (
            ArtworkExtractor::maximumEncodedBytes * 2u))
        return {};

    juce::MemoryBlock result;
    if (! result.fromBase64Encoding (value)
        || result.getSize() == 0
        || result.getSize() > ArtworkExtractor::maximumEncodedBytes)
        return {};
    return result;
}

juce::MemoryBlock readBoundedFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};
    const auto size = file.getSize();
    if (size <= 0 || size > static_cast<juce::int64> (
            ArtworkExtractor::maximumEncodedBytes))
        return {};

    juce::MemoryBlock result;
    if (! file.loadFileAsData (result)
        || result.getSize() > ArtworkExtractor::maximumEncodedBytes)
        return {};
    return result;
}
} // namespace

juce::MemoryBlock ArtworkExtractor::find (
    const juce::File& audioFile,
    const juce::StringPairArray& metadata)
{
    const auto keys = metadata.getAllKeys();
    const auto values = metadata.getAllValues();
    for (int index = 0; index < keys.size(); ++index)
        if (mayContainArtwork (keys[index]))
            if (auto bytes = decodeMetadataValue (values[index]); bytes.getSize() > 0)
                return bytes;

    if (auto bytes = readId3Artwork (audioFile); ! bytes.isEmpty())
        return bytes;

    if (auto bytes = readFlacArtwork (audioFile); ! bytes.isEmpty())
        return bytes;

    const auto parent = audioFile.getParentDirectory();
    const auto stem = audioFile.getFileNameWithoutExtension();
    const std::array<juce::String, 18> names {
        stem + ".png", stem + ".jpg", stem + ".jpeg",
        "cover.png", "cover.jpg", "cover.jpeg",
        "Cover.png", "Cover.jpg", "Cover.jpeg",
        "folder.png", "folder.jpg", "folder.jpeg",
        "Folder.png", "Folder.jpg", "Folder.jpeg",
        "front.png", "front.jpg", "front.jpeg"
    };

    for (const auto& name : names)
        if (auto bytes = readBoundedFile (parent.getChildFile (name)); bytes.getSize() > 0)
            return bytes;
    return {};
}
