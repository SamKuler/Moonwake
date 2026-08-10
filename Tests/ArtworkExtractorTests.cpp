#include <juce_graphics/juce_graphics.h>

#include "library/ArtworkExtractor.h"

namespace
{
juce::MemoryBlock makePng (int width = 12, int height = 8)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    graphics.fillAll (juce::Colours::cornflowerblue);
    juce::MemoryOutputStream output;
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, output))
        return {};
    return output.getMemoryBlock();
}

class ArtworkExtractorTests final : public juce::UnitTest
{
public:
    ArtworkExtractorTests() : UnitTest ("ArtworkExtractor", "library") {}

    void runTest() override
    {
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
