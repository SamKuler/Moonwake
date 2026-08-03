#pragma once

#include "SkinDiagnostic.h"
#include "SkinRenderDefinition.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>

class SkinAnimationEngine;
class SkinAssetCache;

struct SkinControlState
{
    bool enabled = true;
    bool hover = false;
    bool pressed = false;
    bool focused = false;
    bool selected = false;
    bool dragging = false;
};

struct SkinRenderContext
{
    juce::Rectangle<float> bounds;
    juce::Rectangle<float> contentBounds;
    float scaleFactor = 1.0f;
    SkinControlState state;
    double normalisedValue = 0.0;
    double minimum = 0.0;
    double maximum = 1.0;
    juce::String primaryText;
    juce::String secondaryText;
    juce::String textAlignment { "left" };
    juce::String displayValue;
    juce::String iconName;
    juce::String iconPathData;
    std::shared_ptr<const juce::Path> iconPath;
    std::array<float, 10> meterPeaks {};
    juce::StringArray meterLabels;
    std::array<bool, 10> meterIsLfe {};
    int repeatItemCount = 0;
    juce::StringArray repeatLabels;
    juce::String scrollOrientation;
    juce::String scrollPart;
    juce::String menuPart;
    double scrollThumbStart = 0.0;
    double scrollThumbSize = 0.0;
    int menuItemIndex = -1;
    int tableRowCount = 0;
    int tableColumnCount = 0;
    std::optional<SkinAnimationTrigger> entryTrigger;
    uint64_t entrySequence = 0;
    std::function<std::optional<SkinExpressionValue> (std::string_view)> extraLookup;
};

enum class SkinRenderStatus { ok, recoverableFault, fatalFault };

class SkinRuntimeFaultSink
{
public:
    virtual ~SkinRuntimeFaultSink() = default;
    virtual void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) = 0;
};

class SkinRenderer final
{
public:
    explicit SkinRenderer (std::shared_ptr<const SkinAssetCache> assets = {});

    void setAssetCache (std::shared_ptr<const SkinAssetCache>);

    SkinRenderStatus draw (juce::Graphics&, const SkinRenderProgram&,
                           const SkinRenderContext&, bool requiredComponent,
                           SkinRuntimeFaultSink&) const;

    SkinRenderStatus draw (juce::Graphics&, const SkinRenderProgram&,
                           const SkinRenderContext&, bool requiredComponent,
                           SkinRuntimeFaultSink&, SkinAnimationEngine*,
                           const juce::String& animationKey) const;

private:
    std::shared_ptr<const SkinAssetCache> assetCache;
};
