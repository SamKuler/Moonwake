#include "ArtworkStore.h"

#include "ArtworkExtractor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{
struct EncodedImageDimensions
{
    std::uint64_t width = 0;
    std::uint64_t height = 0;
};

std::uint32_t readBigEndian32 (const std::uint8_t* bytes)
{
    return (static_cast<std::uint32_t> (bytes[0]) << 24)
         | (static_cast<std::uint32_t> (bytes[1]) << 16)
         | (static_cast<std::uint32_t> (bytes[2]) << 8)
         | static_cast<std::uint32_t> (bytes[3]);
}

std::optional<EncodedImageDimensions> inspectEncodedDimensions (
    const void* data, size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*> (data);
    constexpr std::uint8_t pngSignature[] {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a
    };
    if (size >= 24 && std::equal (std::begin (pngSignature),
                                  std::end (pngSignature), bytes)
        && readBigEndian32 (bytes + 8) == 13
        && std::equal (bytes + 12, bytes + 16,
                       reinterpret_cast<const std::uint8_t*> ("IHDR")))
        return EncodedImageDimensions { readBigEndian32 (bytes + 16),
                                        readBigEndian32 (bytes + 20) };

    if (size < 4 || bytes[0] != 0xff || bytes[1] != 0xd8)
        return std::nullopt;

    size_t offset = 2;
    while (offset < size)
    {
        while (offset < size && bytes[offset] == 0xff) ++offset;
        if (offset >= size) break;
        const auto marker = bytes[offset++];
        if (marker == 0x00 || marker == 0xd8
            || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (marker == 0xd9 || marker == 0xda || offset + 2 > size)
            break;

        const auto segmentLength = static_cast<size_t> (
            (static_cast<unsigned int> (bytes[offset]) << 8)
            | bytes[offset + 1]);
        if (segmentLength < 2 || segmentLength > size - offset)
            return std::nullopt;

        const auto isStartOfFrame = marker >= 0xc0 && marker <= 0xcf
                                 && marker != 0xc4 && marker != 0xc8
                                 && marker != 0xcc;
        if (isStartOfFrame)
        {
            if (segmentLength < 7) return std::nullopt;
            return EncodedImageDimensions {
                static_cast<std::uint64_t> (
                    (static_cast<unsigned int> (bytes[offset + 5]) << 8)
                    | bytes[offset + 6]),
                static_cast<std::uint64_t> (
                    (static_cast<unsigned int> (bytes[offset + 3]) << 8)
                    | bytes[offset + 4])
            };
        }
        offset += segmentLength;
    }
    return std::nullopt;
}

bool dimensionsAreSafe (const EncodedImageDimensions& dimensions,
                        juce::String& errorMessage)
{
    constexpr std::uint64_t maximumEncodedDimension = 16384;
    constexpr std::uint64_t maximumDecodedPixels = 32ULL * 1024ULL * 1024ULL;
    if (dimensions.width == 0 || dimensions.height == 0)
    {
        errorMessage = "Artwork has invalid dimensions.";
        return false;
    }
    if (dimensions.width > maximumEncodedDimension
        || dimensions.height > maximumEncodedDimension
        || dimensions.width > maximumDecodedPixels / dimensions.height)
    {
        errorMessage = "Artwork dimensions exceed the safe decoded-pixel limit.";
        return false;
    }
    return true;
}
} // namespace

ArtworkStore::ArtworkStore (juce::File rootDirectoryToUse)
    : rootDirectory (std::move (rootDirectoryToUse))
{
}

juce::String ArtworkStore::store (const juce::String& trackId,
                                  const juce::MemoryBlock& encodedImage,
                                  juce::String& errorMessage,
                                  EncodingCache* encodingCache) const
{
    errorMessage.clear();
    if (rootDirectory == juce::File() || encodedImage.getSize() == 0)
        return {};
    if (encodedImage.getSize() > ArtworkExtractor::maximumEncodedBytes)
    {
        errorMessage = "Artwork exceeds the 20 MiB encoded-size limit.";
        return {};
    }

    const auto reused = encodingCache != nullptr && encodingCache->source == encodedImage
        && ! encodingCache->png.isEmpty();
    juce::Image image;
    if (! reused)
    {
        const auto dimensions = inspectEncodedDimensions (
            encodedImage.getData(), encodedImage.getSize());
        if (! dimensions)
        {
            errorMessage = "Artwork is not a supported PNG or JPEG image.";
            return {};
        }
        if (! dimensionsAreSafe (*dimensions, errorMessage))
            return {};

        image = juce::ImageFileFormat::loadFrom (encodedImage.getData(),
                                                       encodedImage.getSize());
        if (! image.isValid())
        {
            errorMessage = "Artwork is not a supported PNG or JPEG image.";
            return {};
        }

        constexpr int maximumDimension = 1600;
        if (image.getWidth() > maximumDimension || image.getHeight() > maximumDimension)
        {
            const auto scale = juce::jmin (
                static_cast<double> (maximumDimension) / image.getWidth(),
                static_cast<double> (maximumDimension) / image.getHeight());
            image = image.rescaled (
                juce::jmax (1, static_cast<int> (std::round (image.getWidth() * scale))),
                juce::jmax (1, static_cast<int> (std::round (image.getHeight() * scale))),
                juce::Graphics::highResamplingQuality);
        }
    }

    if (! rootDirectory.isDirectory() && ! rootDirectory.createDirectory())
    {
        errorMessage = "Could not create artwork cache directory: "
                     + rootDirectory.getFullPathName();
        return {};
    }

    const auto key = safeKey (trackId);
    const auto target = resolve (key);
    juce::TemporaryFile temporary (target);
    auto output = std::unique_ptr<juce::FileOutputStream> (
        temporary.getFile().createOutputStream());
    juce::PNGImageFormat png;
    bool written = false;
    if (output != nullptr)
    {
        if (reused)
            written = output->write (encodingCache->png.getData(), encodingCache->png.getSize());
        else if (encodingCache != nullptr)
        {
            juce::MemoryOutputStream encoded;
            if (png.writeImageToStream (image, encoded))
            {
                encodingCache->source = encodedImage;
                encodingCache->png = encoded.getMemoryBlock();
                written = output->write (encodingCache->png.getData(), encodingCache->png.getSize());
            }
        }
        else written = png.writeImageToStream (image, *output);
    }
    if (! written)
    {
        errorMessage = "Could not encode artwork cache image.";
        return {};
    }
    output->flush();
    output.reset();
    if (! temporary.overwriteTargetFileWithTemporary())
    {
        errorMessage = "Could not atomically update artwork cache image.";
        return {};
    }
    return key;
}

juce::File ArtworkStore::resolve (const juce::String& cacheKey) const
{
    if (rootDirectory == juce::File() || cacheKey.isEmpty())
        return {};
    return rootDirectory.getChildFile (safeKey (cacheKey) + ".png");
}

bool ArtworkStore::isValid (const juce::String& cacheKey) const
{
    const auto file = resolve (cacheKey);
    if (! file.existsAsFile()
        || file.getSize() <= 0
        || file.getSize() > static_cast<juce::int64> (
               ArtworkExtractor::maximumEncodedBytes))
        return false;

    juce::MemoryBlock encoded;
    if (! file.loadFileAsData (encoded)) return false;
    juce::String ignoredError;
    const auto dimensions = inspectEncodedDimensions (
        encoded.getData(), encoded.getSize());
    if (! dimensions || ! dimensionsAreSafe (*dimensions, ignoredError))
        return false;
    return juce::ImageFileFormat::loadFrom (
               encoded.getData(), encoded.getSize()).isValid();
}

bool ArtworkStore::remove (const juce::String& cacheKey) const
{
    const auto file = resolve (cacheKey);
    return ! file.existsAsFile() || file.deleteFile();
}

const juce::File& ArtworkStore::root() const noexcept
{
    return rootDirectory;
}

juce::String ArtworkStore::safeKey (const juce::String& value)
{
    auto result = value.retainCharacters (
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    if (result.isEmpty())
        result = juce::String::toHexString (value.hashCode64());
    return result.substring (0, 128);
}
