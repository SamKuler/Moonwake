#pragma once

#include "SkinDiagnostic.h"
#include "SkinLayoutDefinition.h"

#include <juce_graphics/juce_graphics.h>

struct CompiledSkin;

struct SkinResolvedNode
{
    SkinLayoutNodeType type = SkinLayoutNodeType::visual;
    juce::String nodeId, componentId, style, presentation;
    juce::Rectangle<float> bounds;
    juce::Rectangle<float> clipBounds;
    juce::Rectangle<float> scrollViewportBounds;
    juce::Rectangle<float> scrollContentBounds;
    float opacity = 1.0f;
    int focusOrder = 0, zIndex = 0;
    bool visible = true, scrollVertical = true;
    std::vector<SkinResolvedNode> children;
};

struct SkinResolvedLayout
{
    SkinResolvedNode root;
    std::map<juce::String, juce::String> ancillaryStyles;
    float scrollBarWidth = 8.0f;
    [[nodiscard]] const SkinResolvedNode* component (const juce::String&) const;
};

class SkinLayoutEngine
{
public:
    static constexpr int scrollBarThickness = 8;

    static std::optional<SkinResolvedLayout> resolve (
        const SkinLayoutDefinition&,
        juce::Rectangle<float> bounds,
        float scaleFactor,
        SkinValidationReport&,
        std::optional<WorkspaceMode> workspace = {}, const CompiledSkin* skin = nullptr);
};
