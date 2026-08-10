#pragma once

#include <juce_core/juce_core.h>

class ArtworkExtractor
{
public:
    static constexpr size_t maximumEncodedBytes = 20u * 1024u * 1024u;

    [[nodiscard]] static juce::MemoryBlock find (
        const juce::File& audioFile,
        const juce::StringPairArray& metadata = {});
};
