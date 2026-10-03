#pragma once

#include "SkinExpression.h"
#include "SkinValue.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <variant>
#include <vector>

class SkinAssetCache;

enum class SkinRenderTarget
{
    label, textButton, toggleButton, comboBox, textEditor, linearSlider,
    rotarySlider, progressBar, scrollBar, popupMenu, tooltip, table, surface,
    artwork, effectChain, outputStage, channelMap, pluginList, skinList, channelMixer
};

enum class SkinPrimitive
{
    rectangle, roundedRectangle, ellipse, line, arc, path, text,
    image, svg, group, repeat
};

enum class SkinAnimationTrigger
{
    hover, pressed, selected, focused, disabled, value, workspaceEnter, previewEnter
};

enum class SkinAnimationEasing { linear, easeIn, easeOut, easeInOut, springGentle };

enum class SkinRenderValueType
{
    number, boolean, string, colour, font, gradient, colourOrGradient
};

using SkinCompiledRenderValue = std::variant<double, bool, juce::String, juce::Colour,
                                              SkinFontValue, SkinGradientValue>;
using SkinCompiledValueOrExpression = std::variant<SkinCompiledRenderValue, SkinExpression>;

struct SkinRenderPropertyDescriptor;

struct SkinRenderPropertyFallback
{
    enum class Strategy { literal, layerFrameCentreX, layerFrameCentreY };

    Strategy strategy = Strategy::literal;
    SkinCompiledRenderValue literal = 0.0;

    [[nodiscard]] SkinCompiledRenderValue resolve (
        juce::Rectangle<float> resolvedLayerFrame) const;
};

struct SkinRenderPropertyBinding
{
    SkinCompiledValueOrExpression value = SkinCompiledRenderValue (0.0);
    SkinRenderPropertyFallback runtimeFallback;
    const SkinRenderPropertyDescriptor* descriptor = nullptr;
    bool usesFallbackValue = false;
    std::shared_ptr<const juce::Path> compiledPath;
    juce::String tokenReference;
    int compiledPathCommandCount = 0;
    std::shared_ptr<const SkinAssetCache> assets;

    [[nodiscard]] SkinCompiledRenderValue resolve (
        const SkinExpressionContext&, juce::Rectangle<float> resolvedLayerFrame,
        SkinValidationReport* runtimeWarnings = nullptr) const;
};

enum class SkinRenderPropertySemantic { plain, assetPath, svgPath };

struct SkinRenderPropertyDescriptor
{
    juce::String path;
    SkinRenderValueType type = SkinRenderValueType::number;
    SkinRenderPropertyFallback fallback;
    bool expressionAllowed = true;
    bool interpolable = true;
    std::vector<juce::String> enumValues;
    SkinRenderPropertySemantic semantic = SkinRenderPropertySemantic::plain;
    bool integerOnly = false;
    std::optional<double> minimum;
    std::optional<double> maximum;
    int maximumUtf8Bytes = 0;
};

struct SkinRenderLimits
{
    static constexpr int maximumRootLayers = 1024;
    static constexpr int maximumChildrenPerGroup = 1024;
    static constexpr int maximumDeclaredNodes = 5000;
    static constexpr int maximumExpandedOperations = 5000;
    static constexpr int maximumLayerDepth = 32;
    static constexpr int maximumPathBytes = 65536;
    static constexpr int maximumPackagePathCommands = 250000;
};

struct SkinCompiledSvgPath
{
    std::shared_ptr<const juce::Path> path;
    int commandCount = 0;
};

enum class SkinSvgPathCompileError { none, invalidGrammar, commandBudgetExceeded };

struct SkinRepeatSourceDescriptor
{
    juce::String name;
    int maximumItems = 0;
    SkinRenderTarget target = SkinRenderTarget::surface;
};

struct SkinRenderLayer
{
    juce::String id;
    SkinPrimitive primitive = SkinPrimitive::rectangle;
    std::map<juce::String, SkinRenderPropertyBinding> properties;
    std::vector<SkinRenderLayer> children;
    juce::String repeatSource;
    int repeatLimit = 0;
    std::shared_ptr<const juce::Path> compiledPath;
};

struct SkinVariantPatch
{
    juce::String name;
    std::map<juce::String, SkinRenderPropertyBinding> properties;
};

struct SkinTransition
{
    int durationMs = 0;
    int delayMs = 0;
    SkinAnimationEasing easing = SkinAnimationEasing::linear;
    std::vector<juce::String> properties;
    std::map<juce::String, SkinRenderPropertyBinding> from;
    bool respectReduceMotion = true;
    juce::String durationTokenReference;
};

struct SkinRowSlotFrame
{
    SkinRenderPropertyBinding x, y, width, height;
};

struct SkinRowLayoutOverride
{
    std::optional<SkinRenderPropertyBinding> rowHeight, rowGap, headerHeight;
    std::map<juce::String, SkinRowSlotFrame> slots;
};

struct SkinRowLayoutDefinition
{
    struct Breakpoint
    {
        std::optional<float> minWidth, maxWidth;
        SkinRowLayoutOverride values;
    };
    SkinRowLayoutOverride base;
    std::vector<Breakpoint> breakpoints;
};

struct SkinControlStyleDefinition
{
    std::map<juce::String, SkinRenderPropertyBinding> properties;
};

struct SkinRenderProgram
{
    SkinRenderTarget target = SkinRenderTarget::surface;
    std::vector<SkinRenderLayer> baseLayers;
    std::array<SkinVariantPatch, 6> variants;
    std::map<SkinAnimationTrigger, SkinTransition> transitions;
    std::map<juce::String, SkinRenderValueType> propertyTypes;
    std::map<juce::String, const SkinRenderPropertyDescriptor*> propertyDescriptors;
    std::set<juce::String> contextPaths;
    int worstCaseOperations = 0;
    int pathCommandCount = 0;
    std::optional<SkinRowLayoutDefinition> rowLayout;
    std::optional<SkinControlStyleDefinition> controlStyle;
    juce::String contentClip;
};

struct SkinRenderStyleEntry
{
    SkinRenderProgram renderer;
    std::map<juce::String, juce::String> slotStyles;
    std::shared_ptr<const SkinAssetCache> assets;
};

[[nodiscard]] const std::vector<juce::String>& skinRenderTargetNames();
[[nodiscard]] std::optional<SkinRenderTarget> skinRenderTargetFromName (juce::StringRef);
[[nodiscard]] juce::String skinRenderTargetName (SkinRenderTarget);
[[nodiscard]] const std::vector<juce::String>& skinPrimitiveNames();
[[nodiscard]] std::optional<SkinPrimitive> skinPrimitiveFromName (juce::StringRef);
[[nodiscard]] const SkinRenderPropertyDescriptor* skinRenderPropertyDescriptor (
    SkinPrimitive, juce::StringRef propertyPath);
[[nodiscard]] const std::vector<SkinRenderPropertyDescriptor>& skinRenderPropertyDescriptors (
    SkinPrimitive);
[[nodiscard]] std::map<juce::String, SkinExpressionType> skinRenderContextForTarget (
    SkinRenderTarget, std::optional<juce::String> repeatSource = std::nullopt);
[[nodiscard]] std::map<juce::String, SkinExpressionType>
skinRenderContextForCompositeSlot (
    SkinRenderTarget ownerTarget, juce::StringRef slotName,
    SkinRenderTarget slotTarget,
    std::optional<juce::String> repeatSource = std::nullopt);
[[nodiscard]] std::map<juce::String, SkinExpressionType>
skinRenderContextForAnyCompositeSlot (
    SkinRenderTarget, std::optional<juce::String> repeatSource = std::nullopt);
[[nodiscard]] std::optional<juce::String> skinRenderSlotTarget (
    SkinRenderTarget owner, juce::StringRef slotName);
[[nodiscard]] bool skinRenderIsAncillaryTarget (SkinRenderTarget);
[[nodiscard]] bool skinRenderPropertyTypesCompatible (SkinRenderValueType expected,
                                                       SkinRenderValueType actual);
[[nodiscard]] const std::vector<SkinRepeatSourceDescriptor>& skinRepeatSourceDescriptors();
[[nodiscard]] const SkinRepeatSourceDescriptor* skinRepeatSourceDescriptor (juce::StringRef);
[[nodiscard]] const std::vector<juce::String>& skinAnimationTriggerNames();
[[nodiscard]] std::optional<SkinAnimationTrigger> skinAnimationTriggerFromName (juce::StringRef);
[[nodiscard]] const std::vector<juce::String>& skinAnimationEasingNames();
[[nodiscard]] std::optional<SkinAnimationEasing> skinAnimationEasingFromName (juce::StringRef);
[[nodiscard]] const std::vector<juce::String>& skinVariantNamesInMergeOrder();
[[nodiscard]] std::optional<SkinCompiledSvgPath> skinCompileSvgPathData (
    const juce::String& data,
    int maximumCommands = SkinRenderLimits::maximumPackagePathCommands,
    SkinSvgPathCompileError* error = nullptr);
