#include "SkinRenderDefinition.h"

#include "PlayerComponentCatalog.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{
SkinRenderPropertyFallback literal (SkinCompiledRenderValue value)
{
    return { SkinRenderPropertyFallback::Strategy::literal, std::move (value) };
}

SkinRenderPropertyDescriptor descriptor (
    juce::String path, SkinRenderValueType type, SkinCompiledRenderValue fallback,
    bool expressionAllowed = true, bool interpolable = true)
{
    SkinRenderPropertyDescriptor result;
    result.path = std::move (path);
    result.type = type;
    result.fallback = literal (std::move (fallback));
    result.expressionAllowed = expressionAllowed;
    result.interpolable = interpolable;
    return result;
}

SkinRenderPropertyDescriptor enumDescriptor (
    juce::String path, juce::String fallback,
    std::initializer_list<const char*> accepted,
    bool expressionAllowed = false)
{
    auto result = descriptor (std::move (path), SkinRenderValueType::string,
                              std::move (fallback), expressionAllowed, false);
    for (const auto* value : accepted) result.enumValues.emplace_back (value);
    return result;
}

SkinRenderPropertyDescriptor pivotDescriptor (const char* path, bool x)
{
    auto result = descriptor (path, SkinRenderValueType::number, 0.0);
    result.fallback.strategy = x ? SkinRenderPropertyFallback::Strategy::layerFrameCentreX
                                 : SkinRenderPropertyFallback::Strategy::layerFrameCentreY;
    return result;
}

SkinRenderPropertyDescriptor boundedIntegerDescriptor (
    juce::String path, double fallback, double minimum, double maximum)
{
    auto result = descriptor (std::move (path), SkinRenderValueType::number,
                              fallback, false, false);
    result.integerOnly = true;
    result.minimum = minimum;
    result.maximum = maximum;
    return result;
}

SkinRenderPropertyDescriptor constrainedStringDescriptor (
    juce::String path, SkinRenderPropertySemantic semantic, int maximumUtf8Bytes,
    bool expressionAllowed = false)
{
    auto result = descriptor (std::move (path), SkinRenderValueType::string,
                              juce::String(), expressionAllowed, false);
    result.semantic = semantic;
    result.maximumUtf8Bytes = maximumUtf8Bytes;
    return result;
}

std::vector<SkinRenderPropertyDescriptor> commonDescriptors()
{
    return {
        descriptor ("visible", SkinRenderValueType::boolean, true),
        descriptor ("opacity", SkinRenderValueType::number, 1.0),
        descriptor ("transform.translateX", SkinRenderValueType::number, 0.0),
        descriptor ("transform.translateY", SkinRenderValueType::number, 0.0),
        descriptor ("transform.scaleX", SkinRenderValueType::number, 1.0),
        descriptor ("transform.scaleY", SkinRenderValueType::number, 1.0),
        descriptor ("transform.rotation", SkinRenderValueType::number, 0.0),
        pivotDescriptor ("transform.pivotX", true),
        pivotDescriptor ("transform.pivotY", false),
        descriptor ("clip", SkinRenderValueType::boolean, false, false, false),
        descriptor ("shadow.color", SkinRenderValueType::colour,
                    juce::Colour::fromRGBA (0, 0, 0, 0)),
        descriptor ("shadow.radius", SkinRenderValueType::number, 0.0),
        descriptor ("shadow.offsetX", SkinRenderValueType::number, 0.0),
        descriptor ("shadow.offsetY", SkinRenderValueType::number, 0.0)
    };
}

void append (std::vector<SkinRenderPropertyDescriptor>& destination,
             std::initializer_list<SkinRenderPropertyDescriptor> values)
{
    destination.insert (destination.end(), values.begin(), values.end());
}

void appendFrame (std::vector<SkinRenderPropertyDescriptor>& result)
{
    append (result, {
        enumDescriptor ("frame.anchor", "bounds", { "bounds", "contentBounds" }),
        descriptor ("frame.x", SkinRenderValueType::number, 0.0),
        descriptor ("frame.y", SkinRenderValueType::number, 0.0),
        descriptor ("frame.width", SkinRenderValueType::number, 0.0),
        descriptor ("frame.height", SkinRenderValueType::number, 0.0),
        descriptor ("frame.inset.top", SkinRenderValueType::number, 0.0),
        descriptor ("frame.inset.right", SkinRenderValueType::number, 0.0),
        descriptor ("frame.inset.bottom", SkinRenderValueType::number, 0.0),
        descriptor ("frame.inset.left", SkinRenderValueType::number, 0.0)
    });
}

void appendStroke (std::vector<SkinRenderPropertyDescriptor>& result)
{
    append (result, {
        descriptor ("stroke.color", SkinRenderValueType::colour,
                    juce::Colour::fromRGBA (0, 0, 0, 0)),
        descriptor ("stroke.width", SkinRenderValueType::number, 0.0),
        enumDescriptor ("stroke.placement", "center", { "inside", "center", "outside" }),
        enumDescriptor ("stroke.cap", "butt", { "butt", "square", "round" })
    });
}

std::vector<SkinRenderPropertyDescriptor> descriptorsFor (SkinPrimitive primitive)
{
    auto result = commonDescriptors();
    const auto transparent = juce::Colour::fromRGBA (0, 0, 0, 0);

    switch (primitive)
    {
        case SkinPrimitive::rectangle:
        case SkinPrimitive::ellipse:
            appendFrame (result);
            append (result, { descriptor ("fill", SkinRenderValueType::colourOrGradient,
                                          transparent) });
            appendStroke (result);
            break;
        case SkinPrimitive::roundedRectangle:
            appendFrame (result);
            append (result, {
                descriptor ("radius", SkinRenderValueType::number, 0.0),
                descriptor ("fill", SkinRenderValueType::colourOrGradient, transparent)
            });
            appendStroke (result);
            break;
        case SkinPrimitive::line:
            append (result, {
                descriptor ("start.x", SkinRenderValueType::number, 0.0),
                descriptor ("start.y", SkinRenderValueType::number, 0.0),
                descriptor ("end.x", SkinRenderValueType::number, 0.0),
                descriptor ("end.y", SkinRenderValueType::number, 0.0),
                descriptor ("color", SkinRenderValueType::colour, transparent),
                descriptor ("width", SkinRenderValueType::number, 0.0),
                enumDescriptor ("cap", "butt", { "butt", "square", "round" })
            });
            break;
        case SkinPrimitive::arc:
            append (result, {
                descriptor ("center.x", SkinRenderValueType::number, 0.0),
                descriptor ("center.y", SkinRenderValueType::number, 0.0),
                descriptor ("radiusX", SkinRenderValueType::number, 0.0),
                descriptor ("radiusY", SkinRenderValueType::number, 0.0),
                descriptor ("startAngle", SkinRenderValueType::number, 0.0),
                descriptor ("endAngle", SkinRenderValueType::number, 0.0)
            });
            appendStroke (result);
            break;
        case SkinPrimitive::path:
            append (result, {
                constrainedStringDescriptor ("data", SkinRenderPropertySemantic::svgPath,
                                             SkinRenderLimits::maximumPathBytes, true),
                descriptor ("fill", SkinRenderValueType::colourOrGradient, transparent)
            });
            appendStroke (result);
            break;
        case SkinPrimitive::text:
            appendFrame (result);
            append (result, {
                descriptor ("text", SkinRenderValueType::string, juce::String(), true, false),
                descriptor ("font", SkinRenderValueType::font, SkinFontValue(), false, false),
                descriptor ("fontSize", SkinRenderValueType::number, 12.0),
                descriptor ("color", SkinRenderValueType::colour, transparent),
                enumDescriptor ("align", "left",
                                { "topLeft", "top", "topRight", "left", "center", "right",
                                  "bottomLeft", "bottom", "bottomRight" }, true),
                boundedIntegerDescriptor ("maxLines", 1.0, 1.0, 32.0),
                enumDescriptor ("overflow", "clip", { "clip", "ellipsis", "shrink" })
            });
            break;
        case SkinPrimitive::image:
        case SkinPrimitive::svg:
            appendFrame (result);
            append (result, {
                constrainedStringDescriptor ("source", SkinRenderPropertySemantic::assetPath, 512),
                enumDescriptor ("fit", "contain", { "contain", "cover", "fill", "center" }),
                descriptor ("tint", SkinRenderValueType::colour,
                            juce::Colour::fromRGBA (255, 255, 255, 255))
            });
            break;
        case SkinPrimitive::group:
        case SkinPrimitive::repeat:
            break;
    }
    return result;
}

const std::array<std::vector<SkinRenderPropertyDescriptor>, 11>& allDescriptors()
{
    static const std::array<std::vector<SkinRenderPropertyDescriptor>, 11> values {
        descriptorsFor (SkinPrimitive::rectangle),
        descriptorsFor (SkinPrimitive::roundedRectangle),
        descriptorsFor (SkinPrimitive::ellipse),
        descriptorsFor (SkinPrimitive::line),
        descriptorsFor (SkinPrimitive::arc),
        descriptorsFor (SkinPrimitive::path),
        descriptorsFor (SkinPrimitive::text),
        descriptorsFor (SkinPrimitive::image),
        descriptorsFor (SkinPrimitive::svg),
        descriptorsFor (SkinPrimitive::group),
        descriptorsFor (SkinPrimitive::repeat)
    };
    return values;
}

void addContext (std::map<juce::String, SkinExpressionType>& result,
                 std::initializer_list<std::pair<const char*, SkinExpressionType>> values)
{
    for (const auto& [path, type] : values)
        result.emplace (path, type);
}

SkinCompiledRenderValue expressionValueToRenderValue (const SkinExpressionValue& value)
{
    return std::visit ([] (const auto& item) -> SkinCompiledRenderValue { return item; }, value);
}
} // namespace

SkinCompiledRenderValue SkinRenderPropertyFallback::resolve (
    juce::Rectangle<float> resolvedLayerFrame) const
{
    if (strategy == Strategy::layerFrameCentreX)
        return static_cast<double> (resolvedLayerFrame.getCentreX());
    if (strategy == Strategy::layerFrameCentreY)
        return static_cast<double> (resolvedLayerFrame.getCentreY());
    return literal;
}

SkinCompiledRenderValue SkinRenderPropertyBinding::resolve (
    const SkinExpressionContext& context, juce::Rectangle<float> resolvedLayerFrame,
    SkinValidationReport* runtimeWarnings) const
{
    if (usesFallbackValue)
        return runtimeFallback.resolve (resolvedLayerFrame);
    if (const auto* literalValue = std::get_if<SkinCompiledRenderValue> (&value))
        return *literalValue;
    if (const auto evaluated = std::get<SkinExpression> (value).evaluate (context, runtimeWarnings))
        return expressionValueToRenderValue (*evaluated);
    return runtimeFallback.resolve (resolvedLayerFrame);
}

const std::vector<juce::String>& skinRenderTargetNames()
{
    static const std::vector<juce::String> names {
        "label", "textButton", "toggleButton", "comboBox", "textEditor",
        "linearSlider", "rotarySlider", "progressBar", "scrollBar", "popupMenu",
        "tooltip", "table", "surface", "artwork", "effectChain", "outputStage",
        "channelMap", "pluginList", "skinList"
    };
    return names;
}

std::optional<SkinRenderTarget> skinRenderTargetFromName (juce::StringRef nameRef)
{
    const juce::String name (nameRef);
    const auto& names = skinRenderTargetNames();
    const auto found = std::find (names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<SkinRenderTarget> (std::distance (names.begin(), found));
}

juce::String skinRenderTargetName (SkinRenderTarget target)
{
    return skinRenderTargetNames()[static_cast<size_t> (target)];
}

const std::vector<juce::String>& skinPrimitiveNames()
{
    static const std::vector<juce::String> names {
        "rect", "roundedRect", "ellipse", "line", "arc", "path", "text",
        "image", "svg", "group", "repeat"
    };
    return names;
}

std::optional<SkinPrimitive> skinPrimitiveFromName (juce::StringRef nameRef)
{
    const juce::String name (nameRef);
    const auto& names = skinPrimitiveNames();
    const auto found = std::find (names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<SkinPrimitive> (std::distance (names.begin(), found));
}

const SkinRenderPropertyDescriptor* skinRenderPropertyDescriptor (
    SkinPrimitive primitive, juce::StringRef propertyPath)
{
    const auto& values = skinRenderPropertyDescriptors (primitive);
    const auto found = std::find_if (values.begin(), values.end(), [&] (const auto& value)
    {
        return value.path == propertyPath;
    });
    return found == values.end() ? nullptr : &*found;
}

const std::vector<SkinRenderPropertyDescriptor>& skinRenderPropertyDescriptors (
    SkinPrimitive primitive)
{
    return allDescriptors()[static_cast<size_t> (primitive)];
}

std::map<juce::String, SkinExpressionType> skinRenderContextForTarget (
    SkinRenderTarget target, std::optional<juce::String> repeatSource)
{
    using Type = SkinExpressionType;
    std::map<juce::String, Type> result;
    addContext (result, {
        { "bounds.x", Type::number }, { "bounds.y", Type::number },
        { "bounds.width", Type::number }, { "bounds.height", Type::number },
        { "contentBounds.x", Type::number }, { "contentBounds.y", Type::number },
        { "contentBounds.width", Type::number }, { "contentBounds.height", Type::number },
        { "scaleFactor", Type::number }, { "state.enabled", Type::boolean },
        { "state.hover", Type::boolean }, { "state.pressed", Type::boolean },
        { "state.focused", Type::boolean }, { "state.selected", Type::boolean },
        { "state.dragging", Type::boolean }
    });

    const auto addPrimaryText = [&]
    {
        addContext (result, { { "text.primary", Type::string },
                              { "text.align", Type::string } });
    };
    const auto addAllText = [&]
    {
        addPrimaryText();
        addContext (result, { { "text.secondary", Type::string } });
    };
    const auto addValue = [&]
    {
        addContext (result, { { "value.normalized", Type::number },
                              { "value.minimum", Type::number },
                              { "value.maximum", Type::number },
                              { "value.display", Type::string } });
    };

    switch (target)
    {
        case SkinRenderTarget::label:
            addAllText();
            break;
        case SkinRenderTarget::textButton:
            addPrimaryText();
            addContext (result, { { "icon.name", Type::string },
                                  { "icon.path", Type::string } });
            break;
        case SkinRenderTarget::toggleButton:
        case SkinRenderTarget::comboBox:
        case SkinRenderTarget::textEditor:
        case SkinRenderTarget::tooltip:
            addPrimaryText();
            break;
        case SkinRenderTarget::linearSlider:
        case SkinRenderTarget::rotarySlider:
            addValue();
            break;
        case SkinRenderTarget::progressBar:
            addContext (result, { { "value.normalized", Type::number },
                                  { "value.display", Type::string } });
            break;
        case SkinRenderTarget::scrollBar:
            addContext (result, {
                { "scroll.orientation", Type::string }, { "scroll.thumbStart", Type::number },
                { "scroll.thumbSize", Type::number }, { "scroll.part", Type::string }
            });
            break;
        case SkinRenderTarget::popupMenu:
            addAllText();
            addContext (result, { { "menu.part", Type::string },
                                  { "menu.itemIndex", Type::number },
                                  { "icon.name", Type::string } });
            break;
        case SkinRenderTarget::table:
            addContext (result, { { "table.rowCount", Type::number },
                                  { "table.columnCount", Type::number } });
            break;
        case SkinRenderTarget::artwork:
            addContext (result, { { "artwork.hasImage", Type::boolean },
                                  { "artwork.aspectRatio", Type::number } });
            break;
        case SkinRenderTarget::effectChain:
            addContext (result, { { "chain.slotCount", Type::number } });
            break;
        case SkinRenderTarget::outputStage:
            addAllText();
            addContext (result, { { "meter.channelCount", Type::number },
                                  { "output.layout", Type::string } });
            for (int channel = 0; channel < 10; ++channel)
            {
                const auto prefix = "meter.channels[" + juce::String (channel) + "].";
                result.emplace (prefix + "index", Type::number);
                result.emplace (prefix + "label", Type::string);
                result.emplace (prefix + "peak", Type::number);
                result.emplace (prefix + "isLfe", Type::boolean);
            }
            break;
        case SkinRenderTarget::channelMap:
        case SkinRenderTarget::pluginList:
        case SkinRenderTarget::skinList:
            break;
        case SkinRenderTarget::surface:
            break;
    }

    if (repeatSource == "meter.channels")
        addContext (result, { { "item.index", Type::number }, { "item.label", Type::string },
                              { "item.peak", Type::number }, { "item.isLfe", Type::boolean } });
    else if (repeatSource == "table.visibleColumns")
        addContext (result, { { "item.index", Type::number }, { "item.label", Type::string } });
    else if (repeatSource == "output.channelLabels")
        addContext (result, { { "item.index", Type::number }, { "item.label", Type::string } });

    return result;
}

std::map<juce::String, SkinExpressionType> skinRenderContextForCompositeSlot (
    SkinRenderTarget ownerTarget, juce::StringRef slotNameRef,
    SkinRenderTarget slotTarget, std::optional<juce::String> repeatSource)
{
    auto result = skinRenderContextForTarget (slotTarget, repeatSource);
    const juce::String slotName (slotNameRef);
    using Type = SkinExpressionType;
    switch (ownerTarget)
    {
        case SkinRenderTarget::table:
            if (slotName == "row.surface" || slotName.startsWith ("cell."))
                addContext (result, {
                    { "row.index", Type::number },
                    { "row.selected", Type::boolean },
                    { "row.available", Type::boolean },
                    { "row.title", Type::string },
                    { "row.location", Type::string },
                    { "row.duration", Type::string }
                });
            if (slotName.startsWith ("cell."))
                addContext (result, { { "column.id", Type::string } });
            break;
        case SkinRenderTarget::artwork:
            addContext (result, {
                { "artwork.hasImage", Type::boolean },
                { "artwork.aspectRatio", Type::number }
            });
            break;
        case SkinRenderTarget::effectChain:
            addContext (result, {
                { "chain.slotCount", Type::number },
                { "slot.index", Type::number },
                { "slot.bypassed", Type::boolean },
                { "slot.hasError", Type::boolean },
                { "slot.name", Type::string }
            });
            break;
        case SkinRenderTarget::channelMap:
            addContext (result, {
                { "channel.index", Type::number },
                { "channel.sourceLabel", Type::string },
                { "channel.deviceLabel", Type::string },
                { "channel.connected", Type::boolean }
            });
            break;
        case SkinRenderTarget::pluginList:
            addContext (result, {
                { "plugin.index", Type::number },
                { "plugin.name", Type::string },
                { "plugin.manufacturer", Type::string },
                { "plugin.format", Type::string },
                { "plugin.enabled", Type::boolean }
            });
            break;
        case SkinRenderTarget::skinList:
            addContext (result, {
                { "skin.index", Type::number },
                { "skin.id", Type::string },
                { "skin.name", Type::string },
                { "skin.author", Type::string },
                { "skin.version", Type::string },
                { "skin.builtin", Type::boolean },
                { "skin.active", Type::boolean },
                { "skin.previewed", Type::boolean }
            });
            break;
        case SkinRenderTarget::label:
        case SkinRenderTarget::textButton:
            if (slotName.startsWith ("icon."))
                addContext (result, { { "icon.name", Type::string },
                                      { "icon.path", Type::string } });
            break;
        case SkinRenderTarget::toggleButton:
        case SkinRenderTarget::comboBox:
        case SkinRenderTarget::textEditor:
        case SkinRenderTarget::linearSlider:
        case SkinRenderTarget::rotarySlider:
        case SkinRenderTarget::progressBar:
        case SkinRenderTarget::scrollBar:
        case SkinRenderTarget::popupMenu:
        case SkinRenderTarget::tooltip:
        case SkinRenderTarget::surface:
        case SkinRenderTarget::outputStage:
            break;
    }
    return result;
}

std::map<juce::String, SkinExpressionType> skinRenderContextForAnyCompositeSlot (
    SkinRenderTarget slotTarget, std::optional<juce::String> repeatSource)
{
    auto result = skinRenderContextForTarget (slotTarget, repeatSource);
    const auto slotTargetName = skinRenderTargetName (slotTarget);
    for (const auto& descriptor : PlayerComponentCatalog::descriptors())
    {
        const auto owner = skinRenderTargetFromName (
            descriptor.defaultRendererTarget);
        if (! owner) continue;
        for (const auto& slot : descriptor.slots)
        {
            if (slot.rendererTarget != slotTargetName) continue;
            const auto context = skinRenderContextForCompositeSlot (
                *owner, slot.name, slotTarget, repeatSource);
            result.insert (context.begin(), context.end());
        }
    }
    return result;
}

std::optional<juce::String> skinRenderSlotTarget (
    SkinRenderTarget owner, juce::StringRef slotNameRef)
{
    const juce::String slotName (slotNameRef);
    const auto ownerName = skinRenderTargetName (owner);
    for (const auto& component : PlayerComponentCatalog::descriptors())
    {
        bool usesTarget = component.defaultRendererTarget == ownerName;
        for (const auto& [presentation, target] : component.rendererTargetByPresentation)
        {
            juce::ignoreUnused (presentation);
            usesTarget = usesTarget || target == ownerName;
        }
        if (! usesTarget) continue;
        for (const auto& slot : component.slots)
            if (slot.name == slotName)
                return slot.rendererTarget;
    }
    return std::nullopt;
}

bool skinRenderIsAncillaryTarget (SkinRenderTarget target)
{
    return target == SkinRenderTarget::scrollBar || target == SkinRenderTarget::popupMenu
        || target == SkinRenderTarget::tooltip;
}

bool skinRenderPropertyTypesCompatible (SkinRenderValueType expected,
                                        SkinRenderValueType actual)
{
    if (expected == actual) return true;
    return expected == SkinRenderValueType::colourOrGradient
        && (actual == SkinRenderValueType::colour || actual == SkinRenderValueType::gradient);
}

const std::vector<SkinRepeatSourceDescriptor>& skinRepeatSourceDescriptors()
{
    static const std::vector<SkinRepeatSourceDescriptor> values {
        { "meter.channels", 10, SkinRenderTarget::outputStage },
        { "table.visibleColumns", 3, SkinRenderTarget::table },
        { "output.channelLabels", 10, SkinRenderTarget::outputStage }
    };
    return values;
}

const SkinRepeatSourceDescriptor* skinRepeatSourceDescriptor (juce::StringRef nameRef)
{
    const juce::String name (nameRef);
    const auto& values = skinRepeatSourceDescriptors();
    const auto found = std::find_if (values.begin(), values.end(), [&] (const auto& value)
    {
        return value.name == name;
    });
    return found == values.end() ? nullptr : &*found;
}

const std::vector<juce::String>& skinAnimationTriggerNames()
{
    static const std::vector<juce::String> names {
        "hover", "pressed", "selected", "focused", "disabled", "value",
        "workspaceEnter", "previewEnter"
    };
    return names;
}

std::optional<SkinAnimationTrigger> skinAnimationTriggerFromName (juce::StringRef nameRef)
{
    const juce::String name (nameRef);
    const auto& names = skinAnimationTriggerNames();
    const auto found = std::find (names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<SkinAnimationTrigger> (std::distance (names.begin(), found));
}

const std::vector<juce::String>& skinAnimationEasingNames()
{
    static const std::vector<juce::String> names {
        "linear", "easeIn", "easeOut", "easeInOut", "springGentle"
    };
    return names;
}

std::optional<SkinAnimationEasing> skinAnimationEasingFromName (juce::StringRef nameRef)
{
    const juce::String name (nameRef);
    const auto& names = skinAnimationEasingNames();
    const auto found = std::find (names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<SkinAnimationEasing> (std::distance (names.begin(), found));
}

const std::vector<juce::String>& skinVariantNamesInMergeOrder()
{
    static const std::vector<juce::String> names {
        "selected", "hover", "pressed", "dragging", "focused", "disabled"
    };
    return names;
}

std::optional<SkinCompiledSvgPath> skinCompileSvgPathData (
    const juce::String& data, int maximumCommands, SkinSvgPathCompileError* error)
{
    if (error != nullptr) *error = SkinSvgPathCompileError::none;
    const auto fail = [&] (SkinSvgPathCompileError reason)
        -> std::optional<SkinCompiledSvgPath>
    {
        if (error != nullptr) *error = reason;
        return std::nullopt;
    };
    if (maximumCommands < 1)
        return fail (SkinSvgPathCompileError::commandBudgetExceeded);

    struct Token
    {
        enum class Type { end, command, number, invalid } type = Type::invalid;
        char command = 0;
        double number = 0.0;
    };

    class Lexer
    {
    public:
        explicit Lexer (const juce::String& source)
            : storage (source.toRawUTF8()), cursor (storage), end (storage + source.getNumBytesAsUTF8()) {}

        Token next()
        {
            int commaCount = 0;
            while (cursor < end && (*cursor == ' ' || *cursor == '\t'
                                    || *cursor == '\r' || *cursor == '\n'
                                    || *cursor == ','))
            {
                if (*cursor++ == ',') ++commaCount;
            }
            if (commaCount > 1 || (commaCount == 1 && ! previousWasNumber))
                return { Token::Type::invalid };
            if (cursor == end)
                return commaCount == 0 ? Token { Token::Type::end }
                                       : Token { Token::Type::invalid };

            const auto isCommand = [] (char value)
            {
                switch (value)
                {
                    case 'M': case 'm': case 'L': case 'l': case 'H': case 'h':
                    case 'V': case 'v': case 'C': case 'c': case 'S': case 's':
                    case 'Q': case 'q': case 'T': case 't': case 'A': case 'a':
                    case 'Z': case 'z': return true;
                    default: return false;
                }
            };
            if (isCommand (*cursor))
            {
                if (commaCount != 0) return { Token::Type::invalid };
                previousWasNumber = false;
                return { Token::Type::command, *cursor++ };
            }

            const auto start = cursor;
            if (*cursor == '+' || *cursor == '-') ++cursor;
            int digits = 0;
            while (cursor < end && std::isdigit (static_cast<unsigned char> (*cursor)))
            {
                ++cursor;
                ++digits;
            }
            if (cursor < end && *cursor == '.')
            {
                ++cursor;
                while (cursor < end && std::isdigit (static_cast<unsigned char> (*cursor)))
                {
                    ++cursor;
                    ++digits;
                }
            }
            if (digits == 0) return { Token::Type::invalid };
            if (cursor < end && (*cursor == 'e' || *cursor == 'E'))
            {
                ++cursor;
                if (cursor < end && (*cursor == '+' || *cursor == '-')) ++cursor;
                int exponentDigits = 0;
                while (cursor < end && std::isdigit (static_cast<unsigned char> (*cursor)))
                {
                    ++cursor;
                    ++exponentDigits;
                }
                if (exponentDigits == 0) return { Token::Type::invalid };
            }

            const auto length = static_cast<size_t> (cursor - start);
            if (length == 0 || length > 64) return { Token::Type::invalid };
            char numberText[65] {};
            std::copy (start, cursor, numberText);
            char* parsedEnd = nullptr;
            const auto value = std::strtod (numberText, &parsedEnd);
            if (parsedEnd != numberText + length || ! std::isfinite (value))
                return { Token::Type::invalid };
            previousWasNumber = true;
            return { Token::Type::number, 0, value };
        }

    private:
        const char* storage = nullptr;
        const char* cursor = nullptr;
        const char* end = nullptr;
        bool previousWasNumber = false;
    };

    const auto parameterCount = [] (char command)
    {
        switch (static_cast<char> (std::toupper (static_cast<unsigned char> (command))))
        {
            case 'M': case 'L': case 'T': return 2;
            case 'H': case 'V': return 1;
            case 'C': return 6;
            case 'S': case 'Q': return 4;
            case 'A': return 7;
            case 'Z': return 0;
            default: return -1;
        }
    };

    Lexer lexer (data);
    auto token = lexer.next();
    if (token.type != Token::Type::command
        || std::toupper (static_cast<unsigned char> (token.command)) != 'M')
        return fail (SkinSvgPathCompileError::invalidGrammar);

    int commandCount = 0;
    while (token.type != Token::Type::end)
    {
        if (token.type != Token::Type::command)
            return fail (SkinSvgPathCompileError::invalidGrammar);
        const auto command = token.command;
        const auto required = parameterCount (command);
        if (required < 0) return fail (SkinSvgPathCompileError::invalidGrammar);

        int numbersInGroup = 0;
        int completedGroups = 0;
        std::array<double, 7> group {};
        token = lexer.next();
        while (token.type == Token::Type::number)
        {
            if (required == 0) return fail (SkinSvgPathCompileError::invalidGrammar);
            group[static_cast<size_t> (numbersInGroup++)] = token.number;
            if (numbersInGroup == required)
            {
                if (std::toupper (static_cast<unsigned char> (command)) == 'A')
                {
                    if (group[0] < 0.0 || group[1] < 0.0
                        || (group[3] != 0.0 && group[3] != 1.0)
                        || (group[4] != 0.0 && group[4] != 1.0))
                        return fail (SkinSvgPathCompileError::invalidGrammar);
                }
                numbersInGroup = 0;
                ++completedGroups;
                if (++commandCount > maximumCommands)
                    return fail (SkinSvgPathCompileError::commandBudgetExceeded);
            }
            token = lexer.next();
        }
        if (token.type == Token::Type::invalid || numbersInGroup != 0
            || (required > 0 && completedGroups == 0))
            return fail (SkinSvgPathCompileError::invalidGrammar);
        if (required == 0 && ++commandCount > maximumCommands)
            return fail (SkinSvgPathCompileError::commandBudgetExceeded);
    }

    return SkinCompiledSvgPath {
        std::make_shared<const juce::Path> (juce::Drawable::parseSVGPath (data)),
        commandCount
    };
}
