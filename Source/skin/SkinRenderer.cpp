#include "SkinRenderer.h"

#include "SkinAnimationEngine.h"
#include "SkinAssetCache.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
constexpr int maximumShadowLayers = 8;
constexpr int64_t maximumShadowPixelsPerFrame = 16000000ll;
constexpr float maximumRenderMagnitude = 10000000.0f;

double meterPeakDecibels (double peak)
{
    return peak > 0.001 ? 20.0 * std::log10 (peak) : -60.0;
}

struct RepeatItem
{
    int index = 0;
    juce::String label;
    double peak = 0.0;
    double peakDb = -60.0;
    double rmsDb = -60.0;
    double truePeakDb = -60.0;
    bool isLfe = false;
};

struct RenderFailure final : std::runtime_error
{
    RenderFailure (juce::String pointerIn, juce::String messageIn)
        : std::runtime_error (messageIn.toStdString()), pointer (std::move (pointerIn)),
          message (std::move (messageIn)) {}

    juce::String pointer;
    juce::String message;
};

template <typename T>
T valueOr (const SkinCompiledRenderValue& value, T fallback)
{
    if (const auto* result = std::get_if<T> (&value)) return *result;
    return fallback;
}

float numberValue (const SkinCompiledRenderValue& value, float fallback = 0.0f)
{
    const auto number = valueOr<double> (value, fallback);
    const auto converted = static_cast<float> (number);
    if (! std::isfinite (number) || ! std::isfinite (converted)
        || std::abs (converted) > maximumRenderMagnitude)
        throw RenderFailure ({}, "Numeric render value is outside the safe graphics range.");
    return converted;
}

class ScopedTransparencyLayer final
{
public:
    ScopedTransparencyLayer (juce::Graphics& graphicsToUse, float opacity)
        : graphics (opacity < 1.0f ? &graphicsToUse : nullptr)
    {
        if (graphics != nullptr) graphics->beginTransparencyLayer (opacity);
    }

    ~ScopedTransparencyLayer()
    {
        if (graphics != nullptr) graphics->endTransparencyLayer();
    }

private:
    juce::Graphics* graphics = nullptr;
};

juce::Justification justificationFrom (const juce::String& value)
{
    if (value == "topLeft") return juce::Justification::topLeft;
    if (value == "top") return juce::Justification::centredTop;
    if (value == "topRight") return juce::Justification::topRight;
    if (value == "left") return juce::Justification::centredLeft;
    if (value == "center") return juce::Justification::centred;
    if (value == "right") return juce::Justification::centredRight;
    if (value == "bottomLeft") return juce::Justification::bottomLeft;
    if (value == "bottom") return juce::Justification::centredBottom;
    if (value == "bottomRight") return juce::Justification::bottomRight;
    return juce::Justification::centredLeft;
}

juce::PathStrokeType::EndCapStyle capFrom (const juce::String& value)
{
    if (value == "round") return juce::PathStrokeType::rounded;
    if (value == "square") return juce::PathStrokeType::square;
    return juce::PathStrokeType::butt;
}

juce::Rectangle<float> fittedRectangle (juce::Rectangle<float> destination,
                                        float sourceWidth, float sourceHeight,
                                        const juce::String& fit)
{
    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || fit == "fill")
        return destination;
    if (fit == "center")
        return { destination.getCentreX() - sourceWidth * 0.5f,
                 destination.getCentreY() - sourceHeight * 0.5f,
                 sourceWidth, sourceHeight };
    const auto scaleX = destination.getWidth() / sourceWidth;
    const auto scaleY = destination.getHeight() / sourceHeight;
    const auto scale = fit == "cover" ? std::max (scaleX, scaleY)
                                       : std::min (scaleX, scaleY);
    const auto width = sourceWidth * scale;
    const auto height = sourceHeight * scale;
    return { destination.getCentreX() - width * 0.5f,
             destination.getCentreY() - height * 0.5f, width, height };
}

juce::Image tintedImage (const juce::Image& source, juce::Colour tint)
{
    if (tint == juce::Colours::white) return source;
    juce::Image result (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
    const auto toLinear = [] (float value)
    {
        return value <= 0.04045f ? value / 12.92f
                                 : std::pow ((value + 0.055f) / 1.055f, 2.4f);
    };
    const auto toSrgb = [] (float value)
    {
        value = juce::jlimit (0.0f, 1.0f, value);
        return value <= 0.0031308f ? value * 12.92f
                                  : 1.055f * std::pow (value, 1.0f / 2.4f) - 0.055f;
    };
    for (int y = 0; y < source.getHeight(); ++y)
        for (int x = 0; x < source.getWidth(); ++x)
        {
            const auto pixel = source.getPixelAt (x, y);
            result.setPixelAt (x, y, juce::Colour::fromFloatRGBA (
                toSrgb (toLinear (pixel.getFloatRed()) * toLinear (tint.getFloatRed())),
                toSrgb (toLinear (pixel.getFloatGreen()) * toLinear (tint.getFloatGreen())),
                toSrgb (toLinear (pixel.getFloatBlue()) * toLinear (tint.getFloatBlue())),
                pixel.getFloatAlpha() * tint.getFloatAlpha()));
        }
    return result;
}

class RenderPass
{
public:
    RenderPass (juce::Graphics& graphicsIn, const SkinRenderProgram& programIn,
                const SkinRenderContext& contextIn,
                std::shared_ptr<const SkinAssetCache> assetsIn,
                SkinAnimationEngine* animationsIn, juce::String animationKeyIn)
        : graphics (graphicsIn), program (programIn), context (contextIn),
          assets (std::move (assetsIn)),
          animations (programIn.transitions.empty() ? nullptr : animationsIn),
          animationKey (std::move (animationKeyIn))
    {
        const std::array activeVariants {
            context.state.selected, context.state.hover, context.state.pressed,
            context.state.dragging, context.state.focused, ! context.state.enabled
        };
        for (size_t i = 0; i < activeVariants.size(); ++i)
            if (activeVariants[i])
                for (const auto& [path, binding] : program.variants[i].properties)
                    variantBindings.insert_or_assign (path, &binding);
    }

    void draw()
    {
        const auto finiteRectangle = [] (juce::Rectangle<float> rectangle)
        {
            return std::isfinite (rectangle.getX()) && std::isfinite (rectangle.getY())
                && std::isfinite (rectangle.getWidth()) && std::isfinite (rectangle.getHeight());
        };
        if (! finiteRectangle (context.bounds) || ! finiteRectangle (context.contentBounds)
            || ! std::isfinite (context.scaleFactor) || context.scaleFactor <= 0.0f
            || context.scaleFactor > 64.0f
            || ! std::isfinite (context.normalisedValue)
            || ! std::isfinite (context.minimum) || ! std::isfinite (context.maximum))
            throw RenderFailure ({}, "Render context contains non-finite geometry or values.");

        if (program.transitions.empty())
        {
            for (const auto& layer : program.baseLayers)
                drawLayer (layer, std::nullopt, false);
            return;
        }

        for (const auto& layer : program.baseLayers)
            collectValues (layer, std::nullopt, false);

        if (animations != nullptr && animationKey.isNotEmpty())
        {
            SkinAnimationInput input;
            input.state = context.state;
            input.normalisedValue = context.normalisedValue;
            input.entryTrigger = context.entryTrigger;
            input.entrySequence = context.entrySequence;
            if (context.entryTrigger)
            {
                const auto transition = program.transitions.find (*context.entryTrigger);
                if (transition != program.transitions.end())
                    for (const auto& [path, binding] : transition->second.from)
                    {
                        input.entryFromValues[path] = binding.resolve (
                            expressionContext (std::nullopt), frameForPropertyPath (path),
                            &runtimeWarnings);
                        if (binding.assets != nullptr)
                            input.entryFromResources[path] = binding.assets;
                    }
            }
            displayedValues = animations->resolve (
                animationKey, program, input, computedValues,
                computedResources, displayedResources);
        }
        else
        {
            displayedValues = computedValues;
            displayedResources = computedResources;
        }

        for (const auto& layer : program.baseLayers)
            drawLayer (layer, std::nullopt, false);
    }

    [[nodiscard]] const SkinValidationReport& warnings() const noexcept
    {
        return runtimeWarnings;
    }

private:
    juce::Rectangle<float> frameForPropertyPath (const juce::String& path) const
    {
        const juce::Rectangle<float>* result = nullptr;
        int longestId = -1;
        for (const auto& [id, frame] : frames)
            if (path.startsWith (id + ".") && id.length() > longestId)
            {
                longestId = id.length();
                result = &frame;
            }
        return result != nullptr ? *result : context.bounds;
    }

    SkinExpressionContext expressionContext (std::optional<RepeatItem> repeatItemValue) const
    {
        return { [this, item = std::move (repeatItemValue)] (std::string_view path)
            -> std::optional<SkinExpressionValue>
        {
            const auto number = [] (double value) -> std::optional<SkinExpressionValue>
            { return SkinExpressionValue (value); };
            const auto boolean = [] (bool value) -> std::optional<SkinExpressionValue>
            { return SkinExpressionValue (value); };
            const auto string = [] (juce::String value) -> std::optional<SkinExpressionValue>
            { return SkinExpressionValue (std::move (value)); };

            if (path == "bounds.x") return number (context.bounds.getX());
            if (path == "bounds.y") return number (context.bounds.getY());
            if (path == "bounds.width") return number (context.bounds.getWidth());
            if (path == "bounds.height") return number (context.bounds.getHeight());
            if (path == "contentBounds.x") return number (context.contentBounds.getX());
            if (path == "contentBounds.y") return number (context.contentBounds.getY());
            if (path == "contentBounds.width") return number (context.contentBounds.getWidth());
            if (path == "contentBounds.height") return number (context.contentBounds.getHeight());
            if (path == "scaleFactor") return number (context.scaleFactor);
            if (path == "state.enabled") return boolean (context.state.enabled);
            if (path == "state.hover") return boolean (context.state.hover);
            if (path == "state.pressed") return boolean (context.state.pressed);
            if (path == "state.focused") return boolean (context.state.focused);
            if (path == "state.selected") return boolean (context.state.selected);
            if (path == "state.dragging") return boolean (context.state.dragging);
            if (path == "value.normalized") return number (context.normalisedValue);
            if (path == "value.minimum") return number (context.minimum);
            if (path == "value.maximum") return number (context.maximum);
            if (path == "value.display") return string (context.displayValue);
            if (path == "text.primary") return string (context.primaryText);
            if (path == "text.secondary") return string (context.secondaryText);
            if (path == "text.align") return string (context.textAlignment);
            if (path == "icon.name") return string (context.iconName);
            if (path == "icon.path") return string (context.iconPathData);
            if (path == "scroll.orientation") return string (context.scrollOrientation);
            if (path == "scroll.part") return string (context.scrollPart);
            if (path == "scroll.thumbStart") return number (context.scrollThumbStart);
            if (path == "scroll.thumbSize") return number (context.scrollThumbSize);
            if (path == "menu.part") return string (context.menuPart);
            if (path == "menu.itemIndex") return number (context.menuItemIndex);
            if (path == "table.rowCount") return number (context.tableRowCount);
            if (path == "table.columnCount") return number (context.tableColumnCount);
            if (path == "meter.channelCount") return number (context.meterLabels.size());

            if (item)
            {
                if (path == "item.index") return number (item->index);
                if (path == "item.label") return string (item->label);
                if (path == "item.peak") return number (item->peak);
                if (path == "item.peakDb") return number (item->peakDb);
                if (path == "item.rmsDb") return number (item->rmsDb);
                if (path == "item.truePeakDb") return number (item->truePeakDb);
                if (path == "item.isLfe") return boolean (item->isLfe);
            }

            if (path.starts_with ("meter.channels["))
            {
                for (int channel = 0; channel < maximumOutputChannels; ++channel)
                {
                    const auto prefix = "meter.channels[" + std::to_string (channel) + "].";
                    if (! path.starts_with (prefix)) continue;
                    const auto field = path.substr (prefix.size());
                    if (field == "index") return number (channel);
                    if (field == "label")
                        return string (channel < context.meterLabels.size()
                                           ? context.meterLabels[channel] : juce::String());
                    const auto index = static_cast<size_t> (channel);
                    if (field == "peak") return number (context.meterPeaks[index]);
                    if (field == "peakDb") return number (meterPeakDecibels (context.meterPeaks[index]));
                    if (field == "rmsDb") return number (meterPeakDecibels (context.meterRms[index]));
                    if (field == "truePeakDb") return number (meterPeakDecibels (context.meterTruePeaks[index]));
                    if (field == "isLfe") return boolean (context.meterIsLfe[index]);
                }
            }

            return context.extraLookup ? context.extraLookup (path) : std::nullopt;
        } };
    }

    const SkinRenderPropertyBinding* bindingFor (const SkinRenderLayer& layer,
                                                  const juce::String& property) const
    {
        const auto patch = variantBindings.find (layer.id + "." + property);
        if (patch != variantBindings.end()) return patch->second;
        const auto base = layer.properties.find (property);
        return base == layer.properties.end() ? nullptr : &base->second;
    }

    std::shared_ptr<const SkinAssetCache> assetCacheFor (
        const SkinRenderLayer& layer, const juce::String& property,
        bool insideRepeat) const
    {
        const auto* resources = insideRepeat ? repeatDisplayedResources
                                             : &displayedResources;
        if (resources != nullptr)
            if (const auto displayed = resources->find (
                    layer.id + "." + property);
                displayed != resources->end())
                return displayed->second;
        const auto* binding = bindingFor (layer, property);
        return binding != nullptr && binding->assets != nullptr
            ? binding->assets : assets;
    }

    SkinCompiledRenderValue resolveBinding (const SkinRenderLayer& layer,
                                             const juce::String& property,
                                             juce::Rectangle<float> frame,
                                             std::optional<RepeatItem> item) const
    {
        const auto* binding = bindingFor (layer, property);
        if (binding == nullptr) return 0.0;
        return binding->resolve (expressionContext (std::move (item)), frame,
                                 &runtimeWarnings);
    }

    juce::Rectangle<float> resolveFrame (const SkinRenderLayer& layer,
                                         std::optional<RepeatItem> item,
                                         bool useDisplayed) const
    {
        const auto fetch = [&] (const juce::String& property,
                                SkinCompiledRenderValue fallback)
        {
            if ((useDisplayed && ! item) || (item && repeatDisplayedValues != nullptr))
            {
                const auto& values = item ? *repeatDisplayedValues : displayedValues;
                const auto found = values.find (layer.id + "." + property);
                if (found != values.end()) return found->second;
            }
            const auto* binding = bindingFor (layer, property);
            return binding != nullptr
                ? binding->resolve (expressionContext (item), context.bounds, &runtimeWarnings)
                : fallback;
        };

        if (bindingFor (layer, "frame.anchor") != nullptr)
        {
            auto frame = valueOr<juce::String> (
                             fetch ("frame.anchor", juce::String ("bounds")), "bounds")
                             == "contentBounds" ? context.contentBounds : context.bounds;
            const auto top = numberValue (fetch ("frame.inset.top", 0.0));
            const auto right = numberValue (fetch ("frame.inset.right", 0.0));
            const auto bottom = numberValue (fetch ("frame.inset.bottom", 0.0));
            const auto left = numberValue (fetch ("frame.inset.left", 0.0));
            frame.setBounds (frame.getX() + left, frame.getY() + top,
                             std::max (0.0f, frame.getWidth() - left - right),
                             std::max (0.0f, frame.getHeight() - top - bottom));
            return frame;
        }

        if (bindingFor (layer, "frame.x") != nullptr)
            return { numberValue (fetch ("frame.x", 0.0)),
                     numberValue (fetch ("frame.y", 0.0)),
                     std::max (0.0f, numberValue (fetch ("frame.width", 0.0))),
                     std::max (0.0f, numberValue (fetch ("frame.height", 0.0))) };
        return context.bounds;
    }

    void collectValues (const SkinRenderLayer& layer, std::optional<RepeatItem> item,
                        bool insideRepeat)
    {
        if (insideRepeat) return;
        const auto frame = resolveFrame (layer, item, false);
        frames[layer.id] = frame;
        for (const auto& [property, binding] : layer.properties)
        {
            const auto* selected = bindingFor (layer, property);
            const auto* selectedBinding = selected != nullptr ? selected : &binding;
            const auto path = layer.id + "." + property;
            computedValues[path] = selectedBinding->resolve (
                expressionContext (item), frame, &runtimeWarnings);
            if (selectedBinding->assets != nullptr)
                computedResources[path] = selectedBinding->assets;
        }
        for (const auto& child : layer.children)
            collectValues (child, item, layer.primitive == SkinPrimitive::repeat);
    }

    void collectRepeatValues (
        const SkinRenderLayer& layer, const RepeatItem& item,
        SkinAnimationPropertyMap& values,
        SkinAnimationResourceMap& resources,
        std::map<juce::String, juce::Rectangle<float>>& itemFrames)
    {
        const auto frame = resolveFrame (layer, item, false);
        itemFrames[layer.id] = frame;
        for (const auto& [propertyName, binding] : layer.properties)
        {
            const auto* selected = bindingFor (layer, propertyName);
            const auto* selectedBinding = selected != nullptr ? selected : &binding;
            const auto path = layer.id + "." + propertyName;
            values[path] = selectedBinding->resolve (
                expressionContext (item), frame, &runtimeWarnings);
            if (selectedBinding->assets != nullptr)
                resources[path] = selectedBinding->assets;
        }
        for (const auto& child : layer.children)
            collectRepeatValues (child, item, values, resources, itemFrames);
    }

    SkinCompiledRenderValue property (const SkinRenderLayer& layer,
                                      const juce::String& name,
                                      juce::Rectangle<float> frame,
                                      std::optional<RepeatItem> item,
                                      bool insideRepeat) const
    {
        if (! insideRepeat && ! item)
        {
            const auto displayed = displayedValues.find (layer.id + "." + name);
            if (displayed != displayedValues.end()) return displayed->second;
        }
        if (insideRepeat && repeatDisplayedValues != nullptr)
        {
            const auto displayed = repeatDisplayedValues->find (layer.id + "." + name);
            if (displayed != repeatDisplayedValues->end()) return displayed->second;
        }
        return resolveBinding (layer, name, frame, std::move (item));
    }

    juce::AffineTransform layerTransform (const SkinRenderLayer& layer,
                                          juce::Rectangle<float> frame,
                                          std::optional<RepeatItem> item,
                                          bool insideRepeat) const
    {
        const auto get = [&] (const char* name, float fallback)
        {
            return numberValue (property (layer, name, frame, item, insideRepeat), fallback);
        };
        const auto pivotX = get ("transform.pivotX", frame.getCentreX());
        const auto pivotY = get ("transform.pivotY", frame.getCentreY());
        const auto scaleX = get ("transform.scaleX", 1.0f);
        const auto scaleY = get ("transform.scaleY", 1.0f);
        const auto radians = juce::degreesToRadians (get ("transform.rotation", 0.0f));
        const auto translateX = get ("transform.translateX", 0.0f);
        const auto translateY = get ("transform.translateY", 0.0f);
        const auto result = juce::AffineTransform::translation (-pivotX, -pivotY)
            .scaled (scaleX, scaleY)
            .rotated (radians)
            .translated (pivotX + translateX, pivotY + translateY);
        for (const auto value : { result.mat00, result.mat01, result.mat02,
                                  result.mat10, result.mat11, result.mat12 })
            if (! std::isfinite (value) || std::abs (value) > maximumRenderMagnitude)
                throw RenderFailure (layer.id + ".transform",
                                     "Layer transform is outside the safe graphics range.");
        return result;
    }

    void setFill (const SkinCompiledRenderValue& value,
                  juce::Rectangle<float> frame)
    {
        if (const auto* colour = std::get_if<juce::Colour> (&value))
        {
            graphics.setColour (*colour);
            return;
        }
        const auto* gradient = std::get_if<SkinGradientValue> (&value);
        if (gradient == nullptr || gradient->stops.empty())
        {
            graphics.setColour (juce::Colours::transparentBlack);
            return;
        }
        const auto safeGradientNumber = [] (double number)
        {
            return std::isfinite (number)
                && std::abs (number) <= static_cast<double> (maximumRenderMagnitude);
        };
        if (! safeGradientNumber (gradient->angle)
            || ! safeGradientNumber (gradient->centreX)
            || ! safeGradientNumber (gradient->centreY)
            || ! safeGradientNumber (gradient->radius))
            throw RenderFailure ({}, "Gradient geometry is outside the safe graphics range.");
        for (const auto& stop : gradient->stops)
            if (! safeGradientNumber (stop.position))
                throw RenderFailure ({}, "Gradient stop is outside the safe graphics range.");
        juce::ColourGradient result;
        if (gradient->type == SkinGradientValue::Type::linear)
        {
            const auto radians = juce::degreesToRadians (gradient->angle);
            const juce::Point<float> direction { std::cos (radians), std::sin (radians) };
            const auto halfWidth = frame.getWidth() * 0.5f;
            const auto halfHeight = frame.getHeight() * 0.5f;
            const auto xLimit = std::abs (direction.x) < 1.0e-6f
                              ? std::numeric_limits<float>::max()
                              : halfWidth / std::abs (direction.x);
            const auto yLimit = std::abs (direction.y) < 1.0e-6f
                              ? std::numeric_limits<float>::max()
                              : halfHeight / std::abs (direction.y);
            const auto halfLength = std::min (xLimit, yLimit);
            const auto start = frame.getCentre() - direction * halfLength;
            const auto end = frame.getCentre() + direction * halfLength;
            result = juce::ColourGradient (gradient->stops.front().colour, start,
                                           gradient->stops.back().colour, end, false);
        }
        else
        {
            const auto centre = juce::Point<float> (
                frame.getX() + frame.getWidth() * gradient->centreX,
                frame.getY() + frame.getHeight() * gradient->centreY);
            const auto radius = std::min (frame.getWidth(), frame.getHeight())
                              * 0.5f * gradient->radius;
            result = juce::ColourGradient (gradient->stops.front().colour, centre,
                                           gradient->stops.back().colour,
                                           centre.translated (radius, 0.0f), true);
        }
        result.clearColours();
        for (const auto& stop : gradient->stops)
            result.addColour (stop.position, stop.colour);
        graphics.setGradientFill (result);
    }

    juce::Path shapePath (const SkinRenderLayer& layer,
                          juce::Rectangle<float> frame,
                          std::optional<RepeatItem> item,
                          bool insideRepeat) const
    {
        juce::Path path;
        switch (layer.primitive)
        {
            case SkinPrimitive::rectangle: path.addRectangle (frame); break;
            case SkinPrimitive::roundedRectangle:
                path.addRoundedRectangle (
                    frame, std::max (0.0f, numberValue (
                        property (layer, "radius", frame, item, insideRepeat))));
                break;
            case SkinPrimitive::ellipse: path.addEllipse (frame); break;
            case SkinPrimitive::line:
                path.startNewSubPath (
                    numberValue (property (layer, "start.x", frame, item, insideRepeat)),
                    numberValue (property (layer, "start.y", frame, item, insideRepeat)));
                path.lineTo (
                    numberValue (property (layer, "end.x", frame, item, insideRepeat)),
                    numberValue (property (layer, "end.y", frame, item, insideRepeat)));
                break;
            case SkinPrimitive::arc:
                path.addCentredArc (
                    numberValue (property (layer, "center.x", frame, item, insideRepeat)),
                    numberValue (property (layer, "center.y", frame, item, insideRepeat)),
                    std::max (0.0f, numberValue (
                        property (layer, "radiusX", frame, item, insideRepeat))),
                    std::max (0.0f, numberValue (
                        property (layer, "radiusY", frame, item, insideRepeat))),
                    0.0f,
                    juce::degreesToRadians (numberValue (
                        property (layer, "startAngle", frame, item, insideRepeat)))
                        + juce::MathConstants<float>::halfPi,
                    juce::degreesToRadians (numberValue (
                        property (layer, "endAngle", frame, item, insideRepeat)))
                        + juce::MathConstants<float>::halfPi,
                    true);
                break;
            case SkinPrimitive::path:
            {
                auto compiled = layer.compiledPath;
                const auto displayedData = valueOr<juce::String> (
                    property (layer, "data", frame, item, insideRepeat), {});
                const auto selectPath = [&] (const SkinRenderPropertyBinding& binding)
                {
                    if (binding.compiledPath == nullptr) return;
                    if (const auto* literal = std::get_if<SkinCompiledRenderValue> (&binding.value))
                        if (const auto* text = std::get_if<juce::String> (literal))
                            if (*text == displayedData) compiled = binding.compiledPath;
                };
                const auto base = layer.properties.find ("data");
                if (base != layer.properties.end()) selectPath (base->second);
                for (const auto& patch : program.variants)
                {
                    const auto found = patch.properties.find (layer.id + ".data");
                    if (found != patch.properties.end()) selectPath (found->second);
                }
                for (const auto& [trigger, transition] : program.transitions)
                {
                    juce::ignoreUnused (trigger);
                    const auto found = transition.from.find (layer.id + ".data");
                    if (found != transition.from.end()) selectPath (found->second);
                }
                if (compiled == nullptr && displayedData.isNotEmpty())
                {
                    if (context.iconPath != nullptr
                        && displayedData == context.iconPathData)
                        compiled = context.iconPath;
                }
                if (compiled == nullptr && displayedData.isNotEmpty())
                {
                    const auto parsed = skinCompileSvgPathData (
                        displayedData, 256);
                    if (! parsed)
                        throw RenderFailure (layer.id + ".data",
                                             "Dynamic semantic icon path is invalid.");
                    compiled = parsed->path;
                }
                if (compiled != nullptr) path = *compiled;
                break;
            }
            case SkinPrimitive::text:
            case SkinPrimitive::image:
            case SkinPrimitive::svg:
            case SkinPrimitive::group:
            case SkinPrimitive::repeat:
                break;
        }
        return path;
    }

    void drawShadow (const SkinRenderLayer& layer, juce::Rectangle<float> frame,
                     const juce::Path& path, std::optional<RepeatItem> item,
                     bool insideRepeat)
    {
        const auto colour = valueOr<juce::Colour> (
            property (layer, "shadow.color", frame, item, insideRepeat),
            juce::Colours::transparentBlack);
        const auto radius = std::max (0.0f, numberValue (
            property (layer, "shadow.radius", frame, item, insideRepeat)));
        if (colour.isTransparent() || radius <= 0.0f) return;

        shadowLayerIds.insert (layer.id);
        if (shadowLayerIds.size() > maximumShadowLayers)
            throw RenderFailure (layer.id + ".shadow", "Renderer exceeds eight shadow layers.");
        auto shadowBounds = path.isEmpty() ? frame : path.getBounds();
        if (! transformStack.empty())
            shadowBounds = shadowBounds.transformedBy (transformStack.back());
        const auto scale = std::max (0.01, static_cast<double> (context.scaleFactor));
        const auto shadowWidth = std::ceil (
            (static_cast<double> (shadowBounds.getWidth()) + radius * 2.0) * scale);
        const auto shadowHeight = std::ceil (
            (static_cast<double> (shadowBounds.getHeight()) + radius * 2.0) * scale);
        if (! std::isfinite (shadowWidth) || ! std::isfinite (shadowHeight)
            || shadowWidth > maximumShadowPixelsPerFrame
            || shadowHeight > maximumShadowPixelsPerFrame
            || shadowWidth * shadowHeight
                > static_cast<double> (maximumShadowPixelsPerFrame - shadowPixels))
            throw RenderFailure (layer.id + ".shadow",
                                 "Renderer exceeds the per-frame shadow pixel budget.");
        shadowPixels += static_cast<int64_t> (shadowWidth * shadowHeight);
        const auto offsetX = juce::roundToInt (numberValue (
            property (layer, "shadow.offsetX", frame, item, insideRepeat)));
        const auto offsetY = juce::roundToInt (numberValue (
            property (layer, "shadow.offsetY", frame, item, insideRepeat)));
        juce::DropShadow (colour, juce::roundToInt (radius), { offsetX, offsetY })
            .drawForPath (graphics, path.isEmpty() ? juce::Path() : path);
    }

    void drawShape (const SkinRenderLayer& layer, juce::Rectangle<float> frame,
                    std::optional<RepeatItem> item, bool insideRepeat)
    {
        auto path = shapePath (layer, frame, item, insideRepeat);
        drawShadow (layer, frame, path, item, insideRepeat);
        if (layer.primitive != SkinPrimitive::line && layer.primitive != SkinPrimitive::arc)
        {
            setFill (property (layer, "fill", frame, item, insideRepeat), frame);
            graphics.fillPath (path);
        }

        juce::Colour strokeColour;
        float strokeWidth = 0.0f;
        juce::String placement = "center";
        juce::String cap = "butt";
        if (layer.primitive == SkinPrimitive::line)
        {
            strokeColour = valueOr<juce::Colour> (
                property (layer, "color", frame, item, insideRepeat),
                juce::Colours::transparentBlack);
            strokeWidth = numberValue (property (layer, "width", frame, item, insideRepeat));
            cap = valueOr<juce::String> (
                property (layer, "cap", frame, item, insideRepeat), "butt");
        }
        else
        {
            strokeColour = valueOr<juce::Colour> (
                property (layer, "stroke.color", frame, item, insideRepeat),
                juce::Colours::transparentBlack);
            strokeWidth = numberValue (
                property (layer, "stroke.width", frame, item, insideRepeat));
            placement = valueOr<juce::String> (
                property (layer, "stroke.placement", frame, item, insideRepeat), "center");
            cap = valueOr<juce::String> (
                property (layer, "stroke.cap", frame, item, insideRepeat), "butt");
        }
        if (strokeWidth <= 0.0f || strokeColour.isTransparent()) return;

        if ((layer.primitive == SkinPrimitive::rectangle
             || layer.primitive == SkinPrimitive::roundedRectangle
             || layer.primitive == SkinPrimitive::ellipse)
            && placement != "center")
        {
            auto strokeFrame = frame;
            const auto amount = strokeWidth * 0.5f * (placement == "inside" ? 1.0f : -1.0f);
            strokeFrame = strokeFrame.reduced (amount);
            path = shapePath (layer, strokeFrame, item, insideRepeat);
        }
        graphics.setColour (strokeColour);
        graphics.strokePath (path, juce::PathStrokeType (
            strokeWidth, juce::PathStrokeType::curved, capFrom (cap)));
    }

    void drawText (const SkinRenderLayer& layer, juce::Rectangle<float> frame,
                   std::optional<RepeatItem> item, bool insideRepeat)
    {
        const auto text = valueOr<juce::String> (
            property (layer, "text", frame, item, insideRepeat), {});
        const auto fontValue = valueOr<SkinFontValue> (
            property (layer, "font", frame, item, insideRepeat), SkinFontValue());
        const auto fontSize = std::max (1.0f, numberValue (
            property (layer, "fontSize", frame, item, insideRepeat), 12.0f));
        const auto fontAssets = assetCacheFor (layer, "font", insideRepeat);
        graphics.setFont (fontAssets != nullptr
                              ? fontAssets->makeFont (fontValue, fontSize)
                              : juce::Font (juce::FontOptions (fontSize)));
        graphics.setColour (valueOr<juce::Colour> (
            property (layer, "color", frame, item, insideRepeat),
            juce::Colours::transparentBlack));
        const auto alignment = justificationFrom (valueOr<juce::String> (
            property (layer, "align", frame, item, insideRepeat), "left"));
        const auto maxLines = juce::jlimit (1, 32, juce::roundToInt (numberValue (
            property (layer, "maxLines", frame, item, insideRepeat), 1.0f)));
        const auto overflow = valueOr<juce::String> (
            property (layer, "overflow", frame, item, insideRepeat), "clip");
        if (frame.isEmpty() || text.isEmpty()) return;
        graphics.reduceClipRegion (frame.getSmallestIntegerContainer());
        if (overflow == "clip" && maxLines == 1)
        {
            graphics.drawText (text, frame, alignment, false);
            return;
        }
        if (overflow == "shrink")
        {
            auto fittedText = text;
            if (text.containsAnyOf ("\r\n"))
            {
                const juce::detail::ShapedText paragraphs (text,
                    juce::detail::ShapedText::Options {}
                        .withFont (graphics.getCurrentFont()).withWordWrapWidth (frame.getWidth())
                        .withTrailingWhitespacesShouldFit (false));
                const auto& lines = paragraphs.getLineTextRanges().getRanges();
                if (lines.size() > static_cast<size_t> (maxLines))
                    fittedText = text.substring (0,
                        static_cast<int> (lines[static_cast<size_t> (maxLines - 1)].getEnd())).trimEnd();
            }
            graphics.drawFittedText (fittedText, frame.toNearestInt(), alignment, maxLines, 0.2f);
            return;
        }

        const auto font = graphics.getCurrentFont();
        const auto visibleLines = juce::jmin (maxLines, juce::jmax (1,
            static_cast<int> (std::floor (frame.getHeight() / font.getHeight()))));
        auto options = juce::detail::ShapedText::Options {}
            .withFont (font).withWordWrapWidth (frame.getWidth()).withHeight (frame.getHeight())
            .withJustification (alignment).withMaxNumLines (visibleLines)
            .withTrailingWhitespacesShouldFit (false);
        if (overflow == "ellipsis") options = options.withEllipsis();
        juce::detail::ShapedText shaped (text, options);
        const auto& ranges = shaped.getLineTextRanges().getRanges();
        if (overflow == "ellipsis" && ! ranges.empty())
        {
            const auto end = static_cast<int> (ranges.back().getEnd());
            if (text.substring (end).trim().isNotEmpty())
                shaped = juce::detail::ShapedText (
                    text.substring (0, end).trimEnd() + juce::String::charToString (0x2026), options);
        }
        shaped.draw (graphics, juce::AffineTransform::translation (frame.getPosition()));
    }

    void drawAsset (const SkinRenderLayer& layer, juce::Rectangle<float> frame,
                    std::optional<RepeatItem> item, bool insideRepeat)
    {
        const auto source = valueOr<juce::String> (
            property (layer, "source", frame, item, insideRepeat), {});
        const auto fit = valueOr<juce::String> (
            property (layer, "fit", frame, item, insideRepeat), "contain");
        const auto tint = valueOr<juce::Colour> (
            property (layer, "tint", frame, item, insideRepeat), juce::Colours::white);
        const auto sourceAssets = assetCacheFor (layer, "source", insideRepeat);
        if (sourceAssets == nullptr)
            throw RenderFailure (layer.id + ".source", "Renderer has no asset cache.");

        juce::Graphics::ScopedSaveState state (graphics);
        graphics.reduceClipRegion (frame.toNearestInt());
        if (layer.primitive == SkinPrimitive::image)
        {
            const auto* image = sourceAssets->findImage (source);
            if (image == nullptr)
                throw RenderFailure (layer.id + ".source", "Bitmap asset is unavailable.");
            const auto tinted = tintedImage (*image, tint);
            const auto destination = fittedRectangle (
                frame, static_cast<float> (image->getWidth()),
                static_cast<float> (image->getHeight()), fit);
            graphics.drawImage (tinted, destination);
            return;
        }

        const auto drawable = sourceAssets->findSvg (source);
        if (drawable == nullptr)
            throw RenderFailure (layer.id + ".source", "SVG asset is unavailable.");
        const auto sourceBounds = drawable->getDrawableBounds();
        const auto destination = fittedRectangle (
            frame, sourceBounds.getWidth(), sourceBounds.getHeight(), fit);
        if (tint == juce::Colours::white)
        {
            drawable->drawWithin (graphics, destination, juce::RectanglePlacement::stretchToFit,
                                  1.0f);
            return;
        }
        const auto rasterScale = std::max (0.01f, context.scaleFactor);
        const auto rasterWidth = std::ceil (
            std::max (1.0f, destination.getWidth() * rasterScale));
        const auto rasterHeight = std::ceil (
            std::max (1.0f, destination.getHeight() * rasterScale));
        if (! std::isfinite (rasterWidth) || ! std::isfinite (rasterHeight)
            || rasterWidth > maximumShadowPixelsPerFrame
            || rasterHeight > maximumShadowPixelsPerFrame
            || rasterWidth * rasterHeight > maximumShadowPixelsPerFrame)
            throw RenderFailure (layer.id + ".source",
                                 "Tinted SVG raster exceeds the offscreen pixel budget.");
        const auto width = static_cast<int> (rasterWidth);
        const auto height = static_cast<int> (rasterHeight);
        juce::Image raster (juce::Image::ARGB, width, height, true);
        juce::Graphics rasterGraphics (raster);
        drawable->drawWithin (rasterGraphics, raster.getBounds().toFloat(),
                              juce::RectanglePlacement::stretchToFit, 1.0f);
        graphics.drawImage (tintedImage (raster, tint), destination);
    }

    int repeatCount (const SkinRenderLayer& layer) const
    {
        if (layer.repeatSource == "meter.channels")
            return std::min (layer.repeatLimit, context.meterLabels.size());
        if (layer.repeatSource == "output.channelLabels")
            return std::min (layer.repeatLimit,
                             context.repeatItemCount > 0 ? context.repeatItemCount
                                                         : context.meterLabels.size());
        if (layer.repeatSource == "table.visibleColumns")
            return std::min (layer.repeatLimit, context.repeatItemCount);
        return 0;
    }

    RepeatItem repeatItem (const SkinRenderLayer& layer, int index) const
    {
        RepeatItem item;
        item.index = index;
        if (layer.repeatSource == "meter.channels")
        {
            if (index < context.meterLabels.size()) item.label = context.meterLabels[index];
            item.peak = context.meterPeaks[static_cast<size_t> (index)];
            item.peakDb = meterPeakDecibels (item.peak);
            item.rmsDb = meterPeakDecibels (context.meterRms[static_cast<size_t> (index)]);
            item.truePeakDb = meterPeakDecibels (context.meterTruePeaks[static_cast<size_t> (index)]);
            item.isLfe = context.meterIsLfe[static_cast<size_t> (index)];
        }
        else if (index < context.repeatLabels.size())
            item.label = context.repeatLabels[index];
        else if (index < context.meterLabels.size())
            item.label = context.meterLabels[index];
        return item;
    }

    void drawLayer (const SkinRenderLayer& layer, std::optional<RepeatItem> item,
                    bool insideRepeat)
    {
        if (! valueOr<bool> (property (layer, "visible", context.bounds, item, insideRepeat), true))
            return;
        const auto frame = resolveFrame (layer, item, insideRepeat || (! item));
        const auto opacity = juce::jlimit (0.0f, 1.0f, numberValue (
            property (layer, "opacity", frame, item, insideRepeat), 1.0f));
        if (opacity <= 0.0f) return;

        juce::Graphics::ScopedSaveState state (graphics);
        const auto localTransform = layerTransform (layer, frame, item, insideRepeat);
        const auto cumulativeTransform = transformStack.empty()
            ? localTransform : localTransform.followedBy (transformStack.back());
        transformStack.push_back (cumulativeTransform);
        graphics.addTransform (localTransform);
        const auto clip = valueOr<bool> (
            property (layer, "clip", frame, item, insideRepeat), false);
        if (clip) graphics.reduceClipRegion (frame.toNearestInt());
        ScopedTransparencyLayer transparency (graphics, opacity);

        if (layer.primitive == SkinPrimitive::text
            || layer.primitive == SkinPrimitive::image
            || layer.primitive == SkinPrimitive::svg
            || layer.primitive == SkinPrimitive::group
            || layer.primitive == SkinPrimitive::repeat)
        {
            juce::Path shadowPath;
            shadowPath.addRectangle (frame);
            drawShadow (layer, frame, shadowPath, item, insideRepeat);
        }

        switch (layer.primitive)
        {
            case SkinPrimitive::rectangle:
            case SkinPrimitive::roundedRectangle:
            case SkinPrimitive::ellipse:
            case SkinPrimitive::line:
            case SkinPrimitive::arc:
            case SkinPrimitive::path:
                drawShape (layer, frame, item, insideRepeat);
                break;
            case SkinPrimitive::text:
                drawText (layer, frame, item, insideRepeat);
                break;
            case SkinPrimitive::image:
            case SkinPrimitive::svg:
                drawAsset (layer, frame, item, insideRepeat);
                break;
            case SkinPrimitive::group:
                for (const auto& child : layer.children)
                    drawLayer (child, item, insideRepeat);
                break;
            case SkinPrimitive::repeat:
                if (! layer.children.empty())
                    for (int index = 0; index < repeatCount (layer); ++index)
                    {
                        const auto itemValue = repeatItem (layer, index);
                        if (animations == nullptr || animationKey.isEmpty())
                        {
                            drawLayer (layer.children.front(), itemValue, true);
                            continue;
                        }

                        SkinAnimationPropertyMap itemComputed;
                        SkinAnimationResourceMap itemResources;
                        std::map<juce::String, juce::Rectangle<float>> itemFrames;
                        collectRepeatValues (
                            layer.children.front(), itemValue, itemComputed,
                            itemResources, itemFrames);
                        SkinAnimationInput input;
                        input.state = context.state;
                        input.normalisedValue = context.normalisedValue;
                        input.entryTrigger = context.entryTrigger;
                        input.entrySequence = context.entrySequence;
                        if (context.entryTrigger)
                        {
                            const auto transition = program.transitions.find (*context.entryTrigger);
                            if (transition != program.transitions.end())
                                for (const auto& [path, binding] : transition->second.from)
                                {
                                    const juce::Rectangle<float>* itemFrame = nullptr;
                                    int longestId = -1;
                                    for (const auto& [id, candidate] : itemFrames)
                                        if (path.startsWith (id + ".") && id.length() > longestId)
                                        {
                                            longestId = id.length();
                                            itemFrame = &candidate;
                                        }
                                    input.entryFromValues[path] = binding.resolve (
                                        expressionContext (itemValue),
                                        itemFrame != nullptr ? *itemFrame : context.bounds,
                                        &runtimeWarnings);
                                    if (binding.assets != nullptr)
                                        input.entryFromResources[path] = binding.assets;
                                }
                        }
                        SkinAnimationResourceMap itemDisplayedResources;
                        auto itemDisplayed = animations->resolve (
                            animationKey + ":repeat:" + layer.id + ":" + juce::String (index),
                            program, input, itemComputed, itemResources,
                            itemDisplayedResources);
                        const auto* previousDisplayed = repeatDisplayedValues;
                        const auto* previousResources = repeatDisplayedResources;
                        repeatDisplayedValues = &itemDisplayed;
                        repeatDisplayedResources = &itemDisplayedResources;
                        drawLayer (layer.children.front(), itemValue, true);
                        repeatDisplayedValues = previousDisplayed;
                        repeatDisplayedResources = previousResources;
                    }
                break;
        }

        transformStack.pop_back();
    }

    juce::Graphics& graphics;
    const SkinRenderProgram& program;
    const SkinRenderContext& context;
    std::shared_ptr<const SkinAssetCache> assets;
    SkinAnimationEngine* animations = nullptr;
    juce::String animationKey;
    std::map<juce::String, const SkinRenderPropertyBinding*> variantBindings;
    SkinAnimationPropertyMap computedValues;
    SkinAnimationPropertyMap displayedValues;
    SkinAnimationResourceMap computedResources;
    SkinAnimationResourceMap displayedResources;
    std::map<juce::String, juce::Rectangle<float>> frames;
    mutable SkinValidationReport runtimeWarnings;
    std::set<juce::String> shadowLayerIds;
    int64_t shadowPixels = 0;
    std::vector<juce::AffineTransform> transformStack;
    const SkinAnimationPropertyMap* repeatDisplayedValues = nullptr;
    const SkinAnimationResourceMap* repeatDisplayedResources = nullptr;
};
} // namespace

SkinRenderer::SkinRenderer (std::shared_ptr<const SkinAssetCache> assets)
    : assetCache (std::move (assets))
{
}

void SkinRenderer::setAssetCache (std::shared_ptr<const SkinAssetCache> assets)
{
    assetCache = std::move (assets);
}

SkinRenderStatus SkinRenderer::draw (
    juce::Graphics& graphics, const SkinRenderProgram& program,
    const SkinRenderContext& context, bool requiredComponent,
    SkinRuntimeFaultSink& sink) const
{
    return draw (graphics, program, context, requiredComponent, sink, nullptr, {});
}

SkinRenderStatus SkinRenderer::draw (
    juce::Graphics& graphics, const SkinRenderProgram& program,
    const SkinRenderContext& context, bool requiredComponent,
    SkinRuntimeFaultSink& sink, SkinAnimationEngine* animations,
    const juce::String& animationKey) const
{
    try
    {
        RenderPass pass (graphics, program, context, assetCache, animations, animationKey);
        pass.draw();
        for (const auto& warning : pass.warnings().getDiagnostics())
            sink.reportRenderFault (warning, SkinRenderStatus::ok);
        return SkinRenderStatus::ok;
    }
    catch (const RenderFailure& failure)
    {
        const auto status = requiredComponent ? SkinRenderStatus::fatalFault
                                              : SkinRenderStatus::recoverableFault;
        SkinDiagnostic diagnostic;
        diagnostic.severity = requiredComponent ? SkinDiagnosticSeverity::error
                                                 : SkinDiagnosticSeverity::warning;
        diagnostic.code = requiredComponent ? "RUNTIME002" : "RUNTIME001";
        diagnostic.file = skinRenderTargetName (program.target);
        diagnostic.pointer = failure.pointer;
        diagnostic.message = failure.message;
        diagnostic.suggestion = requiredComponent
            ? "Restore builtin.foundation or select another skin."
            : "The optional visual layer was skipped.";
        sink.reportRenderFault (diagnostic, status);
        return status;
    }
    catch (...)
    {
        const auto status = requiredComponent ? SkinRenderStatus::fatalFault
                                              : SkinRenderStatus::recoverableFault;
        SkinDiagnostic diagnostic;
        diagnostic.severity = requiredComponent ? SkinDiagnosticSeverity::error
                                                 : SkinDiagnosticSeverity::warning;
        diagnostic.code = requiredComponent ? "RUNTIME002" : "RUNTIME001";
        diagnostic.file = skinRenderTargetName (program.target);
        diagnostic.message = "Unexpected renderer failure was contained inside paint.";
        sink.reportRenderFault (diagnostic, status);
        return status;
    }
}
