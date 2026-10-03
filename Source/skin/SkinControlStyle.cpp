#include "SkinControlStyle.h"

#include <cmath>

const std::vector<SkinRenderPropertyDescriptor>&
skinControlStylePropertyDescriptors (SkinRenderTarget target)
{
    static const auto descriptors = []
    {
        std::map<SkinRenderTarget, std::vector<SkinRenderPropertyDescriptor>> result;
        const auto number = [&] (SkinRenderTarget owner, juce::String path, double minimum)
        {
            SkinRenderPropertyDescriptor descriptor;
            descriptor.path = std::move (path);
            descriptor.type = SkinRenderValueType::number;
            descriptor.minimum = minimum;
            descriptor.maximum = 100000.0;
            descriptor.fallback.literal = minimum;
            result[owner].push_back (std::move (descriptor));
        };
        const auto frame = [&] (SkinRenderTarget owner, const juce::String& name)
        {
            for (const auto* field : { "x", "y", "width", "height" })
                number (owner, name + "." + field,
                        juce::String (field) == "x" || juce::String (field) == "y" ? -100000.0 : 0.0);
        };
        frame (SkinRenderTarget::textButton, "iconBounds");
        for (const auto owner : { SkinRenderTarget::linearSlider, SkinRenderTarget::rotarySlider })
        {
            frame (owner, "trackBounds");
            number (owner, "thumbRadius", 0.0);
        }
        number (SkinRenderTarget::tooltip, "maxWidth", 1.0);
        number (SkinRenderTarget::tooltip, "offsetX", 0.0);
        number (SkinRenderTarget::tooltip, "offsetY", 0.0);
        number (SkinRenderTarget::scrollBar, "thickness", 1.0);
        number (SkinRenderTarget::scrollBar, "minThumbLength", 1.0);
        number (SkinRenderTarget::playlistGrid, "minItemWidth", 120.0);
        number (SkinRenderTarget::playlistGrid, "itemGap", 0.0);
        for (const auto* field : { "selectionColour", "selectedTextColour", "caretColour" })
        {
            SkinRenderPropertyDescriptor descriptor;
            descriptor.path = field;
            descriptor.type = SkinRenderValueType::colour;
            descriptor.fallback.literal = juce::Colours::transparentBlack;
            result[SkinRenderTarget::textEditor].push_back (std::move (descriptor));
        }
        return result;
    }();
    static const std::vector<SkinRenderPropertyDescriptor> empty;
    const auto found = descriptors.find (target);
    return found != descriptors.end() ? found->second : empty;
}

std::optional<SkinResolvedControlStyle> resolveSkinControlStyle (
    const SkinRenderProgram& program, const SkinRenderContext& context, SkinValidationReport& report)
{
    if (! program.controlStyle) return std::nullopt;
    const SkinExpressionContext expressions { [&] (std::string_view path) -> std::optional<SkinExpressionValue>
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
        if (path == "scaleFactor") return static_cast<double> (context.scaleFactor);
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
        if (path == "scroll.orientation") return context.scrollOrientation;
        if (path == "scroll.part") return context.scrollPart;
        if (path == "scroll.thumbStart") return context.scrollThumbStart;
        if (path == "scroll.thumbSize") return context.scrollThumbSize;
        return context.extraLookup ? context.extraLookup (path) : std::nullopt;
    } };
    std::map<juce::String, SkinCompiledRenderValue> values;
    for (const auto& [name, binding] : program.controlStyle->properties)
    {
        const auto before = report.getDiagnostics().size();
        const auto value = binding.resolve (expressions, context.bounds, &report);
        const auto* descriptor = binding.descriptor;
        const auto* number = std::get_if<double> (&value);
        const auto valid = descriptor != nullptr && (descriptor->type == SkinRenderValueType::colour
            ? std::holds_alternative<juce::Colour> (value)
            : number != nullptr && std::isfinite (*number)
                && (! descriptor->minimum || *number >= *descriptor->minimum)
                && (! descriptor->maximum || *number <= *descriptor->maximum));
        if (! valid || before != report.getDiagnostics().size())
        {
            report.addError ("RENDER004", "controlStyle", "/" + name,
                             "Control style value must match its type and numeric range.");
            return std::nullopt;
        }
        values.emplace (name, value);
    }
    SkinResolvedControlStyle result;
    for (const auto& [name, destination] : {
             std::pair { "iconBounds", &result.iconBounds },
             std::pair { "trackBounds", &result.trackBounds } })
        if (values.contains (juce::String (name) + ".x"))
        {
            const auto number = [&] (const char* field)
            { return static_cast<float> (std::get<double> (values.at (juce::String (name) + "." + field))); };
            *destination = juce::Rectangle<float> (number ("x"), number ("y"), number ("width"), number ("height"));
        }
    for (const auto& [name, destination] : {
             std::pair { "thumbRadius", &result.thumbRadius }, std::pair { "maxWidth", &result.maxWidth },
             std::pair { "offsetX", &result.offsetX }, std::pair { "offsetY", &result.offsetY },
             std::pair { "thickness", &result.thickness }, std::pair { "minThumbLength", &result.minThumbLength },
             std::pair { "minItemWidth", &result.minItemWidth }, std::pair { "itemGap", &result.itemGap } })
        if (const auto found = values.find (name); found != values.end())
            *destination = static_cast<float> (std::get<double> (found->second));
    for (const auto& [name, destination] : {
             std::pair { "selectionColour", &result.selectionColour },
             std::pair { "selectedTextColour", &result.selectedTextColour },
             std::pair { "caretColour", &result.caretColour } })
        if (const auto found = values.find (name); found != values.end())
            *destination = std::get<juce::Colour> (found->second);
    return result;
}
