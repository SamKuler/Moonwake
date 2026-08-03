#include "SkinFontResolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
struct StyleTraits
{
    int weight = 400;
    bool italic = false;
};

juce::String normaliseStyleName (juce::String value)
{
    return value.trim().toLowerCase().removeCharacters (" _-");
}

StyleTraits classifyStyle (const juce::String& style)
{
    const auto normalised = normaliseStyleName (style);
    StyleTraits result;
    result.italic = normalised.contains ("italic")
                 || normalised.contains ("oblique");

    if (normalised.contains ("hairline") || normalised.contains ("thin"))
        result.weight = 100;
    else if (normalised.contains ("extralight")
             || normalised.contains ("ultralight"))
        result.weight = 200;
    else if (normalised.contains ("light"))
        result.weight = 300;
    else if (normalised.contains ("semibold")
             || normalised.contains ("demibold"))
        result.weight = 600;
    else if (normalised.contains ("extrabold")
             || normalised.contains ("ultrabold"))
        result.weight = 800;
    else if (normalised.contains ("black") || normalised.contains ("heavy"))
        result.weight = 900;
    else if (normalised.contains ("bold"))
        result.weight = 700;
    else if (normalised.contains ("medium"))
        result.weight = 500;

    return result;
}

int styleFlagsFor (const SkinFontValue& value)
{
    return (value.weight >= 600 ? juce::Font::bold : juce::Font::plain)
         | (value.italic ? juce::Font::italic : juce::Font::plain);
}

std::vector<juce::String> resolveFallbacks (
    const juce::StringArray& families, int firstIndex)
{
    std::vector<juce::String> result;
    for (int index = firstIndex; index < families.size(); ++index)
        result.push_back (SkinFontResolver::resolveGenericFamily (families[index]));
    return result;
}

juce::Font makeFontFromOptions (juce::FontOptions options,
                                const SkinResolvedFontFace& face)
{
    options = options.withFallbacks (face.fallbacks)
                     .withFallbackEnabled()
                     .withKerningFactor (face.requested.tracking);
    auto font = juce::Font (options);
    (void) font.getTypefacePtr();
    return font;
}
} // namespace

bool SkinResolvedFontFace::matches (const SkinFontValue& value) const
{
    if (requested.source != value.source
        || requested.weight != value.weight
        || requested.italic != value.italic
        || std::abs (requested.tracking - value.tracking)
               > std::numeric_limits<float>::epsilon()
        || requested.families.size() != value.families.size())
        return false;

    for (int index = 0; index < requested.families.size(); ++index)
        if (requested.families[index] != value.families[index])
            return false;
    return true;
}

namespace SkinFontResolver
{
juce::String resolveGenericFamily (juce::StringRef familyRef)
{
    const auto family = juce::String (familyRef).trim();
    if (family.equalsIgnoreCase ("sans-serif"))
        return juce::Font::getDefaultSansSerifFontName();
    if (family.equalsIgnoreCase ("serif"))
        return juce::Font::getDefaultSerifFontName();
    if (family.equalsIgnoreCase ("monospace"))
        return juce::Font::getDefaultMonospacedFontName();
    return family;
}

juce::String chooseClosestStyle (const juce::StringArray& styles,
                                 int weight, bool italic)
{
    if (styles.isEmpty()) return {};

    const auto requestedWeight = std::clamp (weight, 1, 1000);
    auto bestScore = std::numeric_limits<int>::max();
    juce::String bestStyle;

    for (const auto& style : styles)
    {
        const auto traits = classifyStyle (style);
        const auto score = std::abs (traits.weight - requestedWeight)
                         + (traits.italic == italic ? 0 : 10000);
        if (score < bestScore)
        {
            bestScore = score;
            bestStyle = style;
        }
    }
    return bestStyle;
}

SkinResolvedFontFace resolve (const SkinFontValue& value,
                              juce::Typeface::Ptr packageTypeface,
                              const juce::StringArray& safeFamilies)
{
    SkinResolvedFontFace result;
    result.requested = value;
    result.fallbackStyleFlags = styleFlagsFor (value);

    if (packageTypeface != nullptr)
    {
        result.typeface = std::move (packageTypeface);
        result.family = result.typeface->getName();
        result.style = result.typeface->getStyle();
        result.fallbacks = resolveFallbacks (safeFamilies, 0);
        return result;
    }

    if (safeFamilies.isEmpty())
        result.family = juce::Font::getDefaultSansSerifFontName();
    else
        result.family = resolveGenericFamily (safeFamilies[0]);
    result.fallbacks = resolveFallbacks (safeFamilies, 1);

    auto probe = juce::Font (juce::FontOptions (
        result.family, 12.0f, result.fallbackStyleFlags));
    result.style = chooseClosestStyle (
        probe.getAvailableStyles(), value.weight, value.italic);

    const auto options = result.style.isNotEmpty()
        ? juce::FontOptions (result.family, result.style, 12.0f)
        : juce::FontOptions (result.family, 12.0f, result.fallbackStyleFlags);
    auto resolvedFont = juce::Font (options.withFallbacks (result.fallbacks)
                                           .withFallbackEnabled());
    result.typeface = resolvedFont.getTypefacePtr();
    return result;
}

juce::Font makeFont (const SkinResolvedFontFace& face, float height)
{
    if (face.typeface != nullptr)
        return makeFontFromOptions (
            juce::FontOptions (face.typeface).withHeight (height), face);
    if (face.style.isNotEmpty())
        return makeFontFromOptions (
            juce::FontOptions (face.family, face.style, height), face);
    return makeFontFromOptions (
        juce::FontOptions (face.family, height, face.fallbackStyleFlags), face);
}

juce::Font makeFallbackFont (const SkinFontValue& value, float height)
{
    SkinResolvedFontFace face;
    face.requested = value;
    face.fallbackStyleFlags = styleFlagsFor (value);
    if (value.families.isEmpty())
        face.family = juce::Font::getDefaultSansSerifFontName();
    else
        face.family = resolveGenericFamily (value.families[0]);
    face.fallbacks = resolveFallbacks (value.families, 1);
    return makeFontFromOptions (
        juce::FontOptions (face.family, height, face.fallbackStyleFlags), face);
}
} // namespace SkinFontResolver
