#include "ArtworkExtractor.h"

#include <array>

namespace
{
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
