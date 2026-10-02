#pragma once

#include "SkinRenderer.h"

struct SkinResolvedRowLayout
{
    float rowHeight = 0.0f, rowGap = 0.0f, headerHeight = 0.0f;
    std::map<juce::String, juce::Rectangle<float>> slots;
};

[[nodiscard]] const SkinRenderPropertyDescriptor& skinRowLayoutMetricDescriptor (juce::StringRef);
[[nodiscard]] const SkinRenderPropertyDescriptor& skinRowLayoutFrameDescriptor (juce::StringRef);
[[nodiscard]] std::map<juce::String, SkinExpressionType>
    skinRowLayoutContextTypes (SkinRenderTarget, bool slotFrame);

[[nodiscard]] std::optional<SkinResolvedRowLayout> resolveSkinRowLayout (
    const SkinRenderProgram&, const SkinRenderContext&, SkinValidationReport&);
