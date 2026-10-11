#include <juce_graphics/juce_graphics.h>

#include "library/ArtworkExtractor.h"
#include "fixtures/TrackMetadataFixtures.h"
#include "fixtures/FlacArtwork.h"
#include "library/TrackMetadataReader.h"

namespace
{
juce::MemoryBlock makePng (int width = 12, int height = 8)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    { juce::Graphics graphics (image); graphics.fillAll (juce::Colours::cornflowerblue); }
    juce::MemoryOutputStream output;
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, output))
        return {};
    return output.getMemoryBlock();
}

void writeTagSize (juce::MemoryOutputStream& output, size_t size, bool syncSafe)
{
    for (int shift = 3; shift >= 0; --shift)
        output.writeByte (static_cast<char> ((size >> (shift * (syncSafe ? 7 : 8))) & (syncSafe ? 0x7f : 0xff)));
}

juce::MemoryBlock pictureFrame (const juce::MemoryBlock& image, int version, int encoding,
                                int type = 3, bool unsynchronised = false,
                                const char* mime = "image/png")
{
    juce::MemoryOutputStream payload;
    payload.writeByte (static_cast<char> (encoding));
    payload.writeString (mime);
    payload.writeByte (static_cast<char> (type));
    if (encoding == 1 || encoding == 2)
    {
        payload.writeShortBigEndian (static_cast<short> (0xfeff));
        payload.writeShort (0);
    }
    else payload.writeByte (0);
    payload.write (image.getData(), image.getSize());
    juce::MemoryOutputStream encoded;
    const auto* bytes = static_cast<const unsigned char*> (payload.getData());
    for (size_t index = 0; index < payload.getDataSize(); ++index)
    {
        encoded.writeByte (static_cast<char> (bytes[index]));
        if (unsynchronised && bytes[index] == 0xff) encoded.writeByte (0);
    }
    juce::MemoryOutputStream frame;
    frame.write ("APIC", 4);
    writeTagSize (frame, encoded.getDataSize(), version == 4);
    frame.writeByte (0);
    frame.writeByte (static_cast<char> (unsynchronised ? 2 : 0));
    frame.write (encoded.getData(), encoded.getDataSize());
    return frame.getMemoryBlock();
}

juce::MemoryBlock pictureTag (const juce::MemoryBlock& frames, int version)
{
    juce::MemoryOutputStream tag;
    tag.write ("ID3", 3);
    tag.writeByte (static_cast<char> (version));
    tag.writeShort (0);
    writeTagSize (tag, frames.getSize(), true);
    tag.write (frames.getData(), frames.getSize());
    return tag.getMemoryBlock();
}

class ArtworkExtractorTests final : public juce::UnitTest
{
public:
    ArtworkExtractorTests() : UnitTest ("ArtworkExtractor", "library") {}

    void runTest() override
    {
        beginTest ("reads bounded ID3 pictures without a platform metadata decoder");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-id3-artwork", {}, true);
            expect (directory.createDirectory());
            const auto audio = directory.getChildFile ("work.mp3");
            const auto png = makePng();
            for (const auto version : { 3, 4 })
                for (int encoding = 0; encoding <= (version == 3 ? 1 : 3); ++encoding)
                {
                    auto frames = pictureFrame (makePng (4, 4), version, 0, 4);
                    const auto front = pictureFrame (png, version, encoding);
                    frames.append (front.getData(), front.getSize());
                    const auto tag = pictureTag (frames, version);
                    expect (audio.replaceWithData (tag.getData(), tag.getSize()));
                    expect (ArtworkExtractor::find (audio) == png);
                }
            auto image = png;
            const unsigned char escaped[] { 0xff, 0xe0, 0xff, 0x00 };
            image.append (escaped, sizeof (escaped));
            const auto unsynchronised = pictureTag (pictureFrame (image, 4, 1, 3, true), 4);
            expect (audio.replaceWithData (unsynchronised.getData(), unsynchronised.getSize()));
            expect (ArtworkExtractor::find (audio) == image);
            const auto fixture = trackMetadataFixtures::mp3();
            expect (audio.replaceWithData (fixture.getData(), fixture.getSize()));
            const auto jpeg = ArtworkExtractor::find (audio);
            expectEquals (juce::ImageFileFormat::loadFrom (jpeg.getData(), jpeg.getSize()).getWidth(), 64);
            expect (directory.deleteRecursively());
        }

        beginTest ("malformed or unsupported ID3 pictures retain sidecar fallback");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-invalid-id3-artwork", {}, true);
            expect (directory.createDirectory());
            const auto audio = directory.getChildFile ("work.mp3");
            const auto png = makePng();
            const auto good = pictureTag (pictureFrame (png, 4, 0), 4);
            for (int corruption = 0; corruption < 6; ++corruption)
            {
                auto tag = good;
                auto* bytes = static_cast<unsigned char*> (tag.getData());
                if (corruption == 0) bytes[6] = 0x7f; // Declared tag exceeds the bounded reader.
                if (corruption == 1) bytes[14] = 0x7f; // Frame extends beyond the tag.
                if (corruption == 2) bytes[20] = 9; // Unknown text encoding.
                if (corruption == 3) bytes[19] = 8; // Compressed frame.
                if (corruption == 4) tag.setSize (tag.getSize() - 1); // Truncated payload.
                if (corruption == 5) tag = pictureTag (pictureFrame (png, 4, 0, 3, false, "-->"), 4);
                expect (audio.replaceWithData (tag.getData(), tag.getSize()));
                expect (ArtworkExtractor::find (audio).isEmpty(), juce::String (corruption));
            }
            expect (directory.getChildFile ("cover.png").replaceWithData (png.getData(), png.getSize()));
            expect (ArtworkExtractor::find (audio) == png);
            expect (directory.deleteRecursively());
        }

        beginTest ("reads FLAC picture blocks and prefers the front cover without sidecars");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-flac-artwork", {}, true);
            expect (directory.createDirectory());
            const auto audio = directory.getChildFile ("work.flac");
            const auto front = makePng();
            const auto back = makePng (4, 4);
            expect (flacArtworkFixture::write (audio, {
                flacArtworkFixture::picture (back, 4),
                flacArtworkFixture::picture (front, 3, "image/png", juce::String::fromUTF8 ("封面")) }));
            expect (ArtworkExtractor::find (audio) == front);
            const auto metadata = TrackMetadataReader().read (audio);
            expect (metadata.ok, metadata.error);
            expect (metadata.artworkData == front);
            expect (TrackMetadataReader().read (audio, false).artworkData.isEmpty());
            expect (flacArtworkFixture::write (audio, { flacArtworkFixture::picture (back, 4) }));
            expect (ArtworkExtractor::find (audio) == back);
            const auto mp3 = trackMetadataFixtures::mp3();
            const auto mp3File = directory.getChildFile ("work.mp3");
            expect (mp3File.replaceWithData (mp3.getData(), mp3.getSize()));
            const auto jpeg = ArtworkExtractor::find (mp3File);
            expect (flacArtworkFixture::write (audio, { flacArtworkFixture::picture (jpeg, 3, "image/jpeg") }));
            expect (ArtworkExtractor::find (audio) == jpeg);
            expect (directory.deleteRecursively());
        }

        beginTest ("FLAC picture lengths are bounded and linked or malformed pictures retain fallback");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-invalid-flac-artwork", {}, true);
            expect (directory.createDirectory());
            const auto audio = directory.getChildFile ("work.flac");
            const auto png = makePng();
            for (int corruption = 0; corruption < 7; ++corruption)
            {
                auto picture = flacArtworkFixture::picture (png);
                auto* bytes = static_cast<unsigned char*> (picture.getData());
                if (corruption == 0) bytes[4] = 0xff; // MIME length beyond the block.
                if (corruption == 1) bytes[17] = 0xff; // Description length beyond the block.
                if (corruption == 2) bytes[37] = 0xff; // Image length beyond the block.
                if (corruption == 3) picture.setSize (picture.getSize() - 1);
                if (corruption == 4) picture = flacArtworkFixture::picture (png, 3, "-->");
                if (corruption == 5) picture = flacArtworkFixture::picture (png, 3, "image/gif");
                if (corruption == 6) picture = flacArtworkFixture::picture ({});
                expect (flacArtworkFixture::write (audio, { picture }));
                expect (ArtworkExtractor::find (audio).isEmpty(), juce::String (corruption));
                expect (flacArtworkFixture::write (audio, { picture, flacArtworkFixture::picture (png) }));
                expect (ArtworkExtractor::find (audio) == png);
            }
            expect (flacArtworkFixture::write (audio, { flacArtworkFixture::picture (png) }));
            juce::MemoryBlock truncated;
            expect (audio.loadFileAsData (truncated));
            truncated.setSize (50); // Declared picture block extends past EOF.
            expect (audio.replaceWithData (truncated.getData(), truncated.getSize()));
            expect (ArtworkExtractor::find (audio).isEmpty());
            expect (directory.getChildFile ("cover.png").replaceWithData (png.getData(), png.getSize()));
            expect (ArtworkExtractor::find (audio) == png);
            expect (directory.deleteRecursively());
        }

        beginTest ("prefers embedded Base64 artwork");
        {
            const auto directory = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-artwork-extractor", {}, true);
            expect (directory.createDirectory());
            const auto audio = directory.getChildFile ("work.flac");
            expect (audio.replaceWithText ("fixture"));
            const auto sidecar = makePng (4, 4);
            expect (directory.getChildFile ("cover.png").replaceWithData (
                sidecar.getData(), sidecar.getSize()));

            juce::StringPairArray metadata;
            metadata.set ("METADATA_BLOCK_PICTURE", makePng().toBase64Encoding());
            const auto result = ArtworkExtractor::find (audio, metadata);
            const auto decoded = juce::ImageFileFormat::loadFrom (
                result.getData(), result.getSize());
            expect (decoded.isValid());
            expectEquals (decoded.getWidth(), 12);
            expect (directory.deleteRecursively());
        }

        beginTest ("finds same-name and conventional sidecar artwork deterministically");
        {
            const auto directory = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-artwork-sidecar", {}, true);
            expect (directory.createDirectory());
            const auto audio = directory.getChildFile ("piece.wav");
            expect (audio.replaceWithText ("fixture"));
            const auto sameName = makePng (9, 7);
            const auto cover = makePng (20, 20);
            expect (directory.getChildFile ("piece.png").replaceWithData (
                sameName.getData(), sameName.getSize()));
            expect (directory.getChildFile ("cover.png").replaceWithData (
                cover.getData(), cover.getSize()));

            const auto result = ArtworkExtractor::find (audio);
            const auto decoded = juce::ImageFileFormat::loadFrom (
                result.getData(), result.getSize());
            expectEquals (decoded.getWidth(), 9);
            expect (directory.deleteRecursively());
        }
    }
};

ArtworkExtractorTests artworkExtractorTests;
} // namespace
