#pragma once

#include <juce_graphics/juce_graphics.h>

class ArtworkStore
{
public:
    explicit ArtworkStore (juce::File rootDirectory);

    [[nodiscard]] juce::String store (const juce::String& trackId,
                                      const juce::MemoryBlock& encodedImage,
                                      juce::String& errorMessage) const;
    [[nodiscard]] juce::File resolve (const juce::String& cacheKey) const;
    [[nodiscard]] bool isValid (const juce::String& cacheKey) const;
    [[nodiscard]] bool remove (const juce::String& cacheKey) const;
    [[nodiscard]] const juce::File& root() const noexcept;

private:
    [[nodiscard]] static juce::String safeKey (const juce::String&);

    juce::File rootDirectory;
};
