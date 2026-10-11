#include <juce_graphics/juce_graphics.h>

#include "library/ArtworkStore.h"

namespace
{
juce::MemoryBlock makeLargePng()
{
    juce::Image image (juce::Image::ARGB, 2000, 1000, true);
    { juce::Graphics graphics (image); graphics.fillAll (juce::Colours::darkorange); }
    juce::MemoryOutputStream output;
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, output))
        return {};
    return output.getMemoryBlock();
}

juce::MemoryBlock makeOversizedPngHeader()
{
    const unsigned char bytes[] {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        0x00, 0x01, 0x86, 0xa0, 0x00, 0x01, 0x86, 0xa0
    };
    return { bytes, sizeof (bytes) };
}

class ArtworkStoreTests final : public juce::UnitTest
{
public:
    ArtworkStoreTests() : UnitTest ("ArtworkStore", "library") {}

    void runTest() override
    {
        beginTest ("stores a bounded PNG atomically using a safe key");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-artwork-store", {}, true);
            ArtworkStore store (root);
            juce::String error;
            const auto key = store.store ("track/../../one", makeLargePng(), error);
            expect (error.isEmpty(), error);
            expect (key.isNotEmpty());
            expect (! key.containsAnyOf ("/\\."));
            const auto file = store.resolve (key);
            expect (file.existsAsFile());
            expect (store.isValid (key));
            const auto image = juce::ImageFileFormat::loadFrom (file);
            expect (image.isValid());
            expectEquals (image.getWidth(), 1600);
            expectEquals (image.getHeight(), 800);
            expect (store.remove (key));
            expect (! file.existsAsFile());
            expect (root.deleteRecursively());
        }

        beginTest ("rejects unsafe dimensions before decoding pixel data");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-artwork-pixel-budget", {}, true);
            ArtworkStore store (root);
            juce::String error;
            expect (store.store ("bomb", makeOversizedPngHeader(), error).isEmpty());
            expect (error.containsIgnoreCase ("dimension"), error);
            expect (! root.exists());
        }

        beginTest ("detects a corrupt cache entry");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-artwork-corrupt-cache", {}, true);
            ArtworkStore store (root);
            root.createDirectory();
            store.resolve ("track").replaceWithText ("corrupt");
            expect (! store.isValid ("track"));
            expect (root.deleteRecursively());
        }

        beginTest ("rejects corrupt artwork without leaving a cache file");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-artwork-invalid", {}, true);
            ArtworkStore store (root);
            juce::MemoryBlock invalid ("not an image", 12);
            juce::String error;
            expect (store.store ("track", invalid, error).isEmpty());
            expect (error.isNotEmpty());
            expect (! store.resolve ("track").existsAsFile());
            expect (store.remove ("track"));
            if (root.exists()) expect (root.deleteRecursively());
        }
    }
};

ArtworkStoreTests artworkStoreTests;
} // namespace
