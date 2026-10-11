#include "SkinValue.h"

namespace
{
std::optional<uint8_t> parseByte (const juce::String& value, int offset)
{
    const auto high = juce::CharacterFunctions::getHexDigitValue (value[offset]);
    const auto low = juce::CharacterFunctions::getHexDigitValue (value[offset + 1]);
    if (high < 0 || low < 0)
        return std::nullopt;
    return static_cast<uint8_t> ((high << 4) | low);
}
} // namespace

std::optional<juce::Colour> parseSkinCssColour (juce::StringRef source)
{
    const juce::String value (source);
    if ((value.length() != 7 && value.length() != 9) || ! value.startsWithChar ('#'))
        return std::nullopt;

    const auto red = parseByte (value, 1);
    const auto green = parseByte (value, 3);
    const auto blue = parseByte (value, 5);
    const auto alpha = value.length() == 9 ? parseByte (value, 7)
                                           : std::optional<uint8_t> (uint8_t { 0xff });
    if (! red || ! green || ! blue || ! alpha)
        return std::nullopt;
    return juce::Colour (*red, *green, *blue, *alpha);
}
