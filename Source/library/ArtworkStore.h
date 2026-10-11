#pragma once

#include <juce_graphics/juce_graphics.h>

class ArtworkStore
{
public:
    // Optional, worker-owned single-image cache for consecutive tracks sharing a cover.
    // Each track still receives its own atomically written, independently owned file.
    class EncodingCache
    {
        friend class ArtworkStore;
        juce::MemoryBlock source, png;
    };

    explicit ArtworkStore (juce::File rootDirectory);

    [[nodiscard]] juce::String store (const juce::String& trackId,
                                      const juce::MemoryBlock& encodedImage,
                                      juce::String& errorMessage,
                                      EncodingCache* encodingCache = nullptr) const;
    [[nodiscard]] juce::File resolve (const juce::String& cacheKey) const;
    [[nodiscard]] bool isValid (const juce::String& cacheKey) const;
    [[nodiscard]] bool remove (const juce::String& cacheKey) const;
    [[nodiscard]] const juce::File& root() const noexcept;

private:
    [[nodiscard]] static juce::String safeKey (const juce::String&);

    juce::File rootDirectory;
};
