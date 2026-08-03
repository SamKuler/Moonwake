#pragma once

#include "SkinValue.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

struct SkinResolvedFontFace
{
    SkinFontValue requested;
    juce::Typeface::Ptr typeface;
    juce::String family;
    juce::String style;
    std::vector<juce::String> fallbacks;
    int fallbackStyleFlags = juce::Font::plain;

    [[nodiscard]] bool matches (const SkinFontValue&) const;
};

namespace SkinFontResolver
{
[[nodiscard]] juce::String resolveGenericFamily (juce::StringRef);
[[nodiscard]] juce::String chooseClosestStyle (
    const juce::StringArray&, int weight, bool italic);
[[nodiscard]] SkinResolvedFontFace resolve (
    const SkinFontValue&, juce::Typeface::Ptr packageTypeface,
    const juce::StringArray& safeFamilies);
[[nodiscard]] juce::Font makeFont (const SkinResolvedFontFace&, float height);
[[nodiscard]] juce::Font makeFallbackFont (const SkinFontValue&, float height);
}
