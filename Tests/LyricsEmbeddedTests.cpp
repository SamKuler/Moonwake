#include "audio/TrackArtistTags.h"
#include "ArtistMetadataFixtures.h"

namespace
{
juce::MemoryBlock lyricId3 (int version, int encoding, const juce::String& value)
{
    const auto encoded = artistMetadataFixtures::id3 (version, encoding, { value });
    const auto* bytes = static_cast<const unsigned char*> (encoded.getData());
    juce::MemoryOutputStream payload;
    payload.writeByte (static_cast<char> (encoding));
    payload.write ("eng", 3);
    if (encoding == 1) payload.writeShort (static_cast<short> (0xfeff));
    if (encoding == 1 || encoding == 2) payload.writeShort (0);
    else payload.writeByte (0);
    payload.write (bytes + 21, encoded.getSize() - 21);
    juce::MemoryOutputStream result;
    result.write ("ID3", 3);
    result.writeByte (static_cast<char> (version));
    result.writeShort (0);
    const auto size = payload.getDataSize();
    for (int shift = 3; shift >= 0; --shift)
        result.writeByte (static_cast<char> (((size + 10) >> (shift * 7)) & 0x7f));
    result.write ("USLT", 4);
    for (int shift = 3; shift >= 0; --shift)
        result.writeByte (static_cast<char> ((size >> (shift * (version == 4 ? 7 : 8))) & (version == 4 ? 0x7f : 0xff)));
    result.writeShort (0);
    result.write (payload.getData(), payload.getDataSize());
    return result.getMemoryBlock();
}
}

class LyricsEmbeddedTests final : public juce::UnitTest
{
public:
    LyricsEmbeddedTests() : UnitTest ("LyricsEmbedded", "lyrics") {}
    void runTest() override
    {
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build/lyrics-embedded-tests");
        static_cast<void> (directory.createDirectory());
        const auto file = directory.getNonexistentChildFile ("moonwake-embedded-lyrics", ".tag");
        beginTest ("ID3 USLT preserves line breaks slashes and Unicode in versioned encodings");
        const auto expected = juce::String::fromUTF8 ("[00:01]NÉON/HEART\n[00:02]Second");
        for (const auto version : { 3, 4 })
            for (const auto encoding : { 0, 1, 2, 3 })
            {
                if (version == 3 && encoding > 1) continue;
                const auto tag = lyricId3 (version, encoding, expected);
                expect (file.replaceWithData (tag.getData(), tag.getSize()));
                expectEquals (readTrackLyricsTag (file), expected);
                expect (readTrackArtistTags (file).isEmpty());
            }

        beginTest ("FLAC and Ogg Vorbis lyric comments share bounded metadata parsing");
        for (const auto flac : { true, false })
            for (const auto field : { "LYRICS=", "UNSYNCEDLYRICS=" })
            {
                juce::MemoryOutputStream comments;
                comments.writeInt (0);
                comments.writeInt (2);
                for (const auto& value : juce::StringArray { "ARTIST=One/Two", juce::String (field) + expected })
                {
                    comments.writeInt (static_cast<int> (value.getNumBytesAsUTF8()));
                    comments.write (value.toRawUTF8(), value.getNumBytesAsUTF8());
                }
                juce::MemoryOutputStream output;
                if (flac)
                {
                    output.write ("fLaC", 4);
                    output.writeByte (static_cast<char> (0x84));
                    for (int shift = 2; shift >= 0; --shift)
                        output.writeByte (static_cast<char> ((comments.getDataSize() >> (shift * 8)) & 0xff));
                    output.write (comments.getData(), comments.getDataSize());
                }
                else
                {
                    const auto first = artistMetadataFixtures::oggPage (juce::MemoryBlock ("\1vorbis", 7), 0);
                    output.write (first.getData(), first.getSize());
                    juce::MemoryBlock packet ("\3vorbis", 7);
                    packet.append (comments.getData(), comments.getDataSize());
                    const auto second = artistMetadataFixtures::oggPage (packet, 1);
                    output.write (second.getData(), second.getSize());
                }
                expect (file.replaceWithData (output.getData(), output.getDataSize()));
                expectEquals (readTrackLyricsTag (file), expected);
                expect (readTrackArtistTags (file) == juce::StringArray { "One/Two" });
            }

        beginTest ("Truncated and oversized ID3 frames return no lyric content");
        auto truncated = lyricId3 (4, 3, expected);
        expect (file.replaceWithData (truncated.getData(), truncated.getSize() - 2));
        expect (readTrackLyricsTag (file).isEmpty());
        auto* bytes = static_cast<unsigned char*> (truncated.getData());
        bytes[6] = 0x7f;
        expect (file.replaceWithData (truncated.getData(), truncated.getSize()));
        expect (readTrackLyricsTag (file).isEmpty());
        expect (file.deleteFile());
    }
};
static LyricsEmbeddedTests lyricsEmbeddedTests;
