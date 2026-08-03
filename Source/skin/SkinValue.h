#pragma once

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <variant>
#include <vector>

struct SkinFontValue
{
    juce::String source;
    juce::StringArray families;
    int weight = 400;
    bool italic = false;
    float tracking = 0.0f;
};

struct SkinGradientStop
{
    float position = 0.0f;
    juce::Colour colour;
};

struct SkinGradientValue
{
    enum class Type { linear, radial };

    Type type = Type::linear;
    float angle = 0.0f;
    float centreX = 0.5f;
    float centreY = 0.5f;
    float radius = 1.0f;
    std::vector<SkinGradientStop> stops;
};

using SkinTokenValue = std::variant<juce::Colour, double, int64_t, juce::String,
                                    SkinFontValue, SkinGradientValue>;

[[nodiscard]] std::optional<juce::Colour> parseSkinCssColour (juce::StringRef);
