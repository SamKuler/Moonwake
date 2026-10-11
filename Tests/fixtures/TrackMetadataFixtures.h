#pragma once

#include <juce_core/juce_core.h>

#include <cstring>

namespace trackMetadataFixtures
{
inline juce::MemoryBlock mp3()
{
    juce::MemoryBlock fixture;
    juce::MemoryOutputStream fixtureStream (fixture, false);
    static_cast<void> (juce::Base64::convertFromBase64 (fixtureStream,
                "SUQzAwAAAAADYVRJVDIAAAAOAAAARGF3biBDdXJyZW50AFRQRTEAAAALAAAAU2FtIEt1bGVy"
                "AFRBTEIAAAAPAAAATmF0dXJhbCBXb3JrcwBUUEUyAAAAEwAAAE1vb253YWtlIEVuc2VtYmxl"
                "AFRDT04AAAAJAAAAQW1iaWVudABUWUVSAAAABgAAADIwMjYAVFJDSwAAAAUAAAA0LzkAVFBP"
                "UwAAAAUAAAAyLzIAVFNTRQAAAA8AAABMYXZmNjIuMTIuMTAyAEFQSUMAAAEQAAAAaW1hZ2Uv"
                "anBlZwADQWxidW0gY292ZXIA/9j/4AAQSkZJRgABAgAAAQABAAD//gAQTGF2YzYyLjI4LjEw"
                "MgD/2wBDAAgEBAQEBAUFBQUFBQYGBgYGBgYGBgYGBgYHBwcICAgHBwcGBgcHCAgICAkJCQgI"
                "CAgJCQoKCgwMCwsODg4RERT/xABMAAEBAAAAAAAAAAAAAAAAAAAABAEBAQAAAAAAAAAAAAAA"
                "AAAAAAYQAQAAAAAAAAAAAAAAAAAAAAARAQAAAAAAAAAAAAAAAAAAAAD/wAARCABAAEADASIA"
                "AhEAAxEA/9oADAMBAAIRAxEAPwCIBNrIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB//2QAAAAAA"
                "AAAAAAD/+1AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABJbmZvAAAADwAAAAsA"
                "AAVIADs7Ozs7Ozs7O05OTk5OTk5OTmJiYmJiYmJiYnZ2dnZ2dnZ2domJiYmJiYmJiZ2dnZ2d"
                "nZ2dnbGxsbGxsbGxscTExMTExMTExNjY2NjY2NjY2Ozs7Ozs7Ozs7P///////////wAAAABM"
                "YXZjNjIuMjgAAAAAAAAAAAAAAAAkBC8AAAAAAAAFSNGQBj4AAAAAAAAAAAAAAAAAAAAA//sQ"
                "ZAAP8AAAaQAAAAgAAA0gAAABAAABpAAAACAAADSAAAAETEFNRTQuMFVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVTEFNRTQuMFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVX/+xBkIg/w"
                "AABpAAAACAAADSAAAAEAAAGkAAAAIAAANIAAAARVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVMQU1FNC4wVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVf/7EGRED/AAAGkA"
                "AAAIAAANIAAAAQAAAaQAAAAgAAA0gAAABFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVUxBTUU0LjBVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV//sQZGYP8AAAaQAAAAgA"
                "AA0gAAABAAABpAAAACAAADSAAAAEVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "TEFNRTQuMFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVX/+xBkiA/wAABpAAAACAAADSAA"
                "AAEAAAGkAAAAIAAANIAAAARVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVf/7EGSqD/AAAGkAAAAIAAANIAAAAQAA"
                "AaQAAAAgAAA0gAAABFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV//sQZMwP8AAAaQAAAAgAAA0gAAABAAABpAAA"
                "ACAAADSAAAAEVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVX/+xBk3Y/wAABpAAAACAAADSAAAAEAAAGkAAAAIAAA"
                "NIAAAARVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVf/7EGTdj/AAAGkAAAAIAAANIAAAAQAAAaQAAAAgAAA0gAAA"
                "BFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVV//sQZN2P8AAAaQAAAAgAAA0gAAABAAABpAAAACAAADSAAAAEVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVX/+xBk3Y/wAABpAAAACAAADSAAAAEAAAGkAAAAIAAANIAAAARVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVQ=="));
    fixtureStream.flush();
    return fixture;
}

inline juce::MemoryBlock id3 (int version, int encoding, const juce::StringArray& artists)
{
    juce::MemoryOutputStream text;
    text.writeByte (static_cast<char> (encoding));
    for (int index = 0; index < artists.size(); ++index)
    {
        const auto& artist = artists[index];
        if (encoding == 1 || encoding == 2)
        {
            if (encoding == 1) text.writeShort (static_cast<short> (0xfeff));
            const auto utf16 = artist.toUTF16();
            for (int unit = 0; unit < static_cast<int> (utf16.sizeInBytes() / 2) - 1; ++unit)
            {
                const auto code = static_cast<short> (utf16.getAddress()[unit]);
                if (encoding == 1) text.writeShort (code);
                else text.writeShortBigEndian (code);
            }
            text.writeShort (0);
        }
        else
        {
            if (encoding == 0)
                for (const auto character : artist) text.writeByte (static_cast<char> (character));
            else text.write (artist.toRawUTF8(), artist.getNumBytesAsUTF8());
            text.writeByte (0);
        }
    }
    const auto writeSize = [] (juce::MemoryOutputStream& out, size_t size, bool syncSafe)
    {
        for (int shift = 3; shift >= 0; --shift)
            out.writeByte (static_cast<char> ((size >> (shift * (syncSafe ? 7 : 8))) & (syncSafe ? 0x7f : 0xff)));
    };
    juce::MemoryOutputStream output;
    output.write ("ID3", 3);
    output.writeByte (static_cast<char> (version));
    output.writeShort (0);
    writeSize (output, 10 + text.getDataSize(), true);
    output.write ("TPE1", 4);
    writeSize (output, text.getDataSize(), version == 4);
    output.writeShort (0);
    output.write (text.getData(), text.getDataSize());
    return output.getMemoryBlock();
}

inline bool writeMp3 (const juce::File& file, int version, int encoding,
                      const juce::StringArray& artists)
{
    auto output = id3 (version, encoding, artists);
    const auto source = mp3();
    const auto* bytes = static_cast<const unsigned char*> (source.getData());
    size_t offset = 10;
    for (int index = 6; index < 10; ++index)
        offset += static_cast<size_t> (bytes[index]) << ((9 - index) * 7);
    output.append (bytes + offset, source.getSize() - offset);
    return file.replaceWithData (output.getData(), output.getSize());
}

inline juce::MemoryBlock vorbisComments (const juce::StringArray& artists, const juce::StringPairArray& metadata = {})
{
    juce::MemoryOutputStream output;
    output.writeInt (0);
    output.writeInt (artists.size() + metadata.size());
    for (const auto& artist : artists)
    {
        const auto comment = "ARTIST=" + artist;
        output.writeInt (static_cast<int> (comment.getNumBytesAsUTF8()));
        output.write (comment.toRawUTF8(), comment.getNumBytesAsUTF8());
    }
    for (int index = 0; index < metadata.size(); ++index)
    {
        const auto comment = metadata.getAllKeys()[index] + "=" + metadata.getAllValues()[index];
        output.writeInt (static_cast<int> (comment.getNumBytesAsUTF8()));
        output.write (comment.toRawUTF8(), comment.getNumBytesAsUTF8());
    }
    return output.getMemoryBlock();
}

inline juce::MemoryBlock oggPage (const juce::MemoryBlock& packet, int sequence,
                                  bool continued = false, bool complete = true)
{
    juce::MemoryOutputStream output;
    output.write ("OggS", 4);
    output.writeByte (0);
    output.writeByte (static_cast<char> ((sequence == 0 ? 2 : 0) | (continued ? 1 : 0)));
    output.writeInt64 (0);
    output.writeInt (42);
    output.writeInt (sequence);
    output.writeInt (0);
    output.writeByte (static_cast<char> (packet.getSize() / 255 + (complete ? 1 : 0)));
    for (size_t offset = 0; offset < packet.getSize() / 255; ++offset) output.writeByte (static_cast<char> (255));
    if (complete) output.writeByte (static_cast<char> (packet.getSize() % 255));
    output.write (packet.getData(), packet.getSize());
    return output.getMemoryBlock();
}

inline bool replaceVorbisArtists (const juce::File& file, bool flac, const juce::StringArray& artists,
                                   const juce::StringPairArray& metadata = {})
{
    juce::MemoryBlock original;
    if (! file.loadFileAsData (original)) return false;
    const auto* data = static_cast<const unsigned char*> (original.getData());
    const auto comments = vorbisComments (artists, metadata);
    juce::MemoryOutputStream output;
    if (flac)
    {
        output.write (data, 4);
        size_t offset = 4;
        bool last = false;
        while (! last && offset + 4 <= original.getSize())
        {
            const auto type = data[offset];
            const auto length = (static_cast<size_t> (data[offset + 1]) << 16)
                              | (static_cast<size_t> (data[offset + 2]) << 8) | data[offset + 3];
            if (offset + 4 + length > original.getSize()) return false;
            last = (type & 0x80) != 0;
            if ((type & 0x7f) != 4)
            {
                output.writeByte (static_cast<char> (type & 0x7f));
                output.write (data + offset + 1, 3 + length);
            }
            offset += 4 + length;
        }
        if (! last) return false;
        output.writeByte (static_cast<char> (0x84));
        for (int shift = 2; shift >= 0; --shift)
            output.writeByte (static_cast<char> ((comments.getSize() >> (shift * 8)) & 0xff));
        output.write (comments.getData(), comments.getSize());
        output.write (data + offset, original.getSize() - offset);
    }
    else
    {
        bool replaced = false;
        for (size_t offset = 0; offset + 27 <= original.getSize();)
        {
            const auto segmentCount = data[offset + 26];
            if (offset + 27 + segmentCount > original.getSize()) return false;
            const auto* segments = data + offset + 27;
            size_t payloadSize = 0;
            for (int index = 0; index < segmentCount; ++index) payloadSize += segments[index];
            const auto pageSize = 27 + segmentCount + payloadSize;
            if (offset + pageSize > original.getSize()) return false;
            const auto* payload = segments + segmentCount;
            if (! replaced && payloadSize >= 7 && std::memcmp (payload, "\3vorbis", 7) == 0)
            {
                size_t oldCommentSize = 0;
                int usedSegments = 0;
                do { oldCommentSize += segments[usedSegments++]; }
                while (usedSegments < segmentCount && segments[usedSegments - 1] == 255);
                if (segments[usedSegments - 1] == 255) return false;
                juce::MemoryBlock packet ("\3vorbis", 7);
                packet.append (comments.getData(), comments.getSize());
                const unsigned char framing = 1;
                packet.append (&framing, 1);
                juce::MemoryOutputStream page;
                page.write (data + offset, 26);
                const auto newSegments = packet.getSize() / 255 + 1;
                page.writeByte (static_cast<char> (static_cast<size_t> (segmentCount - usedSegments) + newSegments));
                for (size_t index = 1; index < newSegments; ++index) page.writeByte (static_cast<char> (255));
                page.writeByte (static_cast<char> (packet.getSize() % 255));
                page.write (segments + usedSegments, static_cast<size_t> (segmentCount - usedSegments));
                page.write (packet.getData(), packet.getSize());
                page.write (payload + oldCommentSize, payloadSize - oldCommentSize);
                auto block = page.getMemoryBlock();
                auto* bytes = static_cast<unsigned char*> (block.getData());
                std::memset (bytes + 22, 0, 4);
                uint32_t crc = 0;
                for (size_t index = 0; index < block.getSize(); ++index)
                {
                    crc ^= static_cast<uint32_t> (bytes[index]) << 24;
                    for (int bit = 0; bit < 8; ++bit)
                        crc = (crc << 1) ^ ((crc & 0x80000000u) != 0 ? 0x04c11db7u : 0u);
                }
                for (int index = 0; index < 4; ++index)
                    bytes[22 + index] = static_cast<unsigned char> ((crc >> (index * 8)) & 0xff);
                output.write (block.getData(), block.getSize());
                replaced = true;
            }
            else output.write (data + offset, pageSize);
            offset += pageSize;
        }
        if (! replaced) return false;
    }
    return file.replaceWithData (output.getData(), output.getDataSize());
}
}
