#pragma once

#include "SkinRenderer.h"

struct SkinResolvedControlStyle
{
    std::optional<juce::Rectangle<float>> iconBounds, trackBounds;
    std::optional<float> thumbRadius, maxWidth, offsetX, offsetY, thickness, minThumbLength;
    std::optional<juce::Colour> selectionColour, selectedTextColour, caretColour;
};

[[nodiscard]] const std::vector<SkinRenderPropertyDescriptor>&
    skinControlStylePropertyDescriptors (SkinRenderTarget);
[[nodiscard]] std::optional<SkinResolvedControlStyle> resolveSkinControlStyle (
    const SkinRenderProgram&, const SkinRenderContext&, SkinValidationReport&);
