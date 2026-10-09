#include "SkinRowLayout.h"

#include "PlayerComponentCatalog.h"

#include <cmath>
#include <tuple>

const SkinRenderPropertyDescriptor& skinRowLayoutMetricDescriptor (juce::StringRef name)
{
    static const auto descriptors = []
    {
        std::map<juce::String, SkinRenderPropertyDescriptor> result;
        for (const auto* key : { "rowHeight", "rowGap", "headerHeight" })
        {
            SkinRenderPropertyDescriptor property;
            property.path = key;
            property.type = SkinRenderValueType::number;
            property.minimum = juce::String (key) == "rowHeight" ? 1.0 : 0.0;
            property.maximum = 100000.0;
            property.fallback.literal = *property.minimum;
            result.emplace (key, std::move (property));
        }
        return result;
    }();
    return descriptors.at (juce::String (name));
}

const SkinRenderPropertyDescriptor& skinRowLayoutFrameDescriptor (juce::StringRef name)
{
    static const auto descriptors = []
    {
        std::map<juce::String, SkinRenderPropertyDescriptor> result;
        for (const auto* key : { "x", "y", "width", "height" })
        {
            auto property = *skinRenderPropertyDescriptor (
                SkinPrimitive::rectangle, "frame." + juce::String (key));
            property.minimum = juce::String (key) == "x" || juce::String (key) == "y" ? -100000.0 : 0.0;
            property.maximum = 100000.0;
            result.emplace (key, std::move (property));
        }
        return result;
    }();
    return descriptors.at (juce::String (name));
}

std::map<juce::String, SkinExpressionType> skinRowLayoutContextTypes (
    SkinRenderTarget target, bool slotFrame)
{
    auto contexts = skinRenderContextForTarget (target);
    contexts.emplace ("table.rowCount", SkinExpressionType::number);
    contexts.emplace ("table.columnCount", SkinExpressionType::number);
    contexts.emplace ("container.width", SkinExpressionType::number);
    contexts.emplace ("container.height", SkinExpressionType::number);
    if (target == SkinRenderTarget::table)
        contexts.emplace ("table.presentation", SkinExpressionType::string);
    if (slotFrame)
        for (const auto* field : { "layout.rowHeight", "layout.rowGap", "layout.headerHeight" })
            contexts.emplace (field, SkinExpressionType::number);
    return contexts;
}

namespace
{
bool interactiveSlotTarget (const juce::String& target)
{
    return target == "textButton" || target == "toggleButton" || target == "linearSlider"
        || target == "rotarySlider" || target == "comboBox" || target == "textEditor"
        || target == "smartPlaylistRuleList";
}

SkinExpressionContext expressionContextFor (const SkinRenderContext& context,
                                           const SkinResolvedRowLayout* row = nullptr,
                                           std::optional<juce::Rectangle<float>> container = {})
{
    return { [&context, row, containerBounds = container.value_or (context.bounds)]
        (std::string_view path) -> std::optional<SkinExpressionValue>
    {
        const auto rectangleValue = [&] (std::string_view prefix, juce::Rectangle<float> bounds)
            -> std::optional<SkinExpressionValue>
        {
            if (! path.starts_with (prefix)) return std::nullopt;
            const auto field = path.substr (prefix.size());
            if (field == "x") return static_cast<double> (bounds.getX());
            if (field == "y") return static_cast<double> (bounds.getY());
            if (field == "width") return static_cast<double> (bounds.getWidth());
            if (field == "height") return static_cast<double> (bounds.getHeight());
            return std::nullopt;
        };
        if (const auto value = rectangleValue ("bounds.", context.bounds)) return value;
        if (const auto value = rectangleValue ("contentBounds.", context.contentBounds)) return value;
        if (path == "container.width") return static_cast<double> (containerBounds.getWidth());
        if (path == "container.height") return static_cast<double> (containerBounds.getHeight());
        if (path == "scaleFactor") return static_cast<double> (context.scaleFactor);
        if (path == "table.rowCount") return static_cast<double> (context.tableRowCount);
        if (path == "table.columnCount") return static_cast<double> (context.tableColumnCount);
        if (path == "chain.slotCount")
        {
            if (context.extraLookup)
                if (const auto value = context.extraLookup (path)) return value;
            return static_cast<double> (context.repeatItemCount);
        }
        if (path == "menu.part") return context.menuPart;
        if (path == "menu.itemIndex") return static_cast<double> (context.menuItemIndex);
        if (path == "state.enabled") return context.state.enabled;
        if (path == "state.hover") return context.state.hover;
        if (path == "state.pressed") return context.state.pressed;
        if (path == "state.focused") return context.state.focused;
        if (path == "state.selected") return context.state.selected;
        if (path == "state.dragging") return context.state.dragging;
        if (path == "value.normalized") return context.normalisedValue;
        if (path == "value.minimum") return context.minimum;
        if (path == "value.maximum") return context.maximum;
        if (path == "value.display") return context.displayValue;
        if (path == "text.primary") return context.primaryText;
        if (path == "text.secondary") return context.secondaryText;
        if (path == "text.align") return context.textAlignment;
        if (path == "icon.name") return context.iconName;
        if (path == "icon.path") return context.iconPathData;
        if (row != nullptr)
        {
            if (path == "layout.rowHeight") return static_cast<double> (row->rowHeight);
            if (path == "layout.rowGap") return static_cast<double> (row->rowGap);
            if (path == "layout.headerHeight") return static_cast<double> (row->headerHeight);
        }
        return context.extraLookup ? context.extraLookup (path) : std::nullopt;
    } };
}

std::optional<float> resolveNumber (const SkinRenderPropertyBinding& binding,
                                    const SkinExpressionContext& context,
                                    SkinValidationReport& report, const juce::String& pointer)
{
    std::optional<double> number;
    if (const auto* literal = std::get_if<SkinCompiledRenderValue> (&binding.value))
    {
        if (const auto* value = std::get_if<double> (literal)) number = *value;
    }
    else if (const auto value = std::get<SkinExpression> (binding.value).evaluate (context, &report))
    {
        if (const auto* result = std::get_if<double> (&*value)) number = *result;
    }
    const auto* descriptor = binding.descriptor;
    if (! number || ! std::isfinite (static_cast<float> (*number))
        || (descriptor != nullptr && descriptor->minimum && *number < *descriptor->minimum)
        || (descriptor != nullptr && descriptor->maximum && *number > *descriptor->maximum))
    {
        report.addError ("RENDER004", "rowLayout", pointer,
                         "Row layout value must be finite and within its numeric range.");
        return std::nullopt;
    }
    return static_cast<float> (*number);
}
} // namespace

std::optional<SkinResolvedRowLayout> resolveSkinRowLayout (
    const SkinRenderProgram& program, const SkinRenderContext& context, SkinValidationReport& report)
{
    if (! program.rowLayout) return std::nullopt;
    auto values = program.rowLayout->base;
    for (const auto& breakpoint : program.rowLayout->breakpoints)
    {
        if ((breakpoint.minWidth && context.bounds.getWidth() < *breakpoint.minWidth)
            || (breakpoint.maxWidth && context.bounds.getWidth() > *breakpoint.maxWidth)) continue;
        if (breakpoint.values.rowHeight) values.rowHeight = breakpoint.values.rowHeight;
        if (breakpoint.values.rowGap) values.rowGap = breakpoint.values.rowGap;
        if (breakpoint.values.headerHeight) values.headerHeight = breakpoint.values.headerHeight;
        for (const auto& [name, frame] : breakpoint.values.slots) values.slots.insert_or_assign (name, frame);
    }
    if (program.target == SkinRenderTarget::effectChain || program.target == SkinRenderTarget::channelMixer
        || program.target == SkinRenderTarget::playlistGrid
        || program.target == SkinRenderTarget::smartPlaylistEditor
        || program.target == SkinRenderTarget::smartPlaylistRuleList)
        for (const auto& component : PlayerComponentCatalog::descriptors())
            if (component.defaultRendererTarget == skinRenderTargetName (program.target))
                for (const auto& slot : component.slots)
                    if ((interactiveSlotTarget (slot.rendererTarget) || program.target == SkinRenderTarget::playlistGrid
                         || program.target == SkinRenderTarget::smartPlaylistEditor
                         || program.target == SkinRenderTarget::smartPlaylistRuleList)
                        && ! values.slots.contains (slot.name))
                    {
                        report.addError ("RENDER004", "rowLayout", "/slots/" + slot.name,
                                         "Row layout must place every interactive slot and all playlist card and smart editor slots.");
                        return std::nullopt;
                    }
    SkinResolvedRowLayout result;
    const auto metrics = expressionContextFor (context);
    const auto height = resolveNumber (*values.rowHeight, metrics, report, "/rowHeight");
    if (! height) return std::nullopt;
    result.rowHeight = *height;
    for (const auto& [name, binding, destination] : {
             std::tuple { "rowGap", &values.rowGap, &result.rowGap },
             std::tuple { "headerHeight", &values.headerHeight, &result.headerHeight } })
        if (*binding)
        {
            const auto value = resolveNumber (**binding, metrics, report, "/" + juce::String (name));
            if (! value) return std::nullopt;
            *destination = *value;
        }
    auto rowContext = context;
    rowContext.bounds = { 0.0f, 0.0f, context.bounds.getWidth(), result.rowHeight };
    rowContext.contentBounds = rowContext.bounds;
    const auto frameContext = expressionContextFor (rowContext, &result, context.bounds);
    for (const auto& [name, frame] : values.slots)
    {
        const auto pointer = "/slots/" + name;
        const auto x = resolveNumber (frame.x, frameContext, report, pointer + "/x");
        const auto y = resolveNumber (frame.y, frameContext, report, pointer + "/y");
        const auto width = resolveNumber (frame.width, frameContext, report, pointer + "/width");
        const auto heightValue = resolveNumber (frame.height, frameContext, report, pointer + "/height");
        if (! x || ! y || ! width || ! heightValue) return std::nullopt;
        const auto target = skinRenderSlotTarget (program.target, name).value_or (juce::String{});
        const juce::Rectangle<float> slotBounds (*x, *y, *width, *heightValue);
        if (interactiveSlotTarget (target))
        {
            const auto minimum = PlayerComponentCatalog::dimensionsForTarget (target);
            if (*width < minimum.minimumWidth || *heightValue < minimum.minimumHeight)
            {
                report.addError ("RENDER004", "rowLayout", pointer,
                                 "Interactive row slot is smaller than its catalog minimum size.");
                return std::nullopt;
            }
            if (! rowContext.bounds.toNearestInt().contains (slotBounds.getSmallestIntegerContainer()))
            {
                report.addError ("RENDER004", "rowLayout", pointer,
                                 "Interactive row slot must fit inside its row's native bounds.");
                return std::nullopt;
            }
        }
        result.slots.emplace (name, slotBounds);
    }
    return result;
}
