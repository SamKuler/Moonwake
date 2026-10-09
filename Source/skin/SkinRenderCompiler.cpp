#include "SkinRenderCompiler.h"
#include "SkinRowLayout.h"
#include "SkinControlStyle.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
juce::String escapePointer (juce::String value)
{
    return value.replace ("~", "~0").replace ("/", "~1");
}

SkinRenderValueType valueType (const SkinCompiledRenderValue& value)
{
    if (std::holds_alternative<double> (value)) return SkinRenderValueType::number;
    if (std::holds_alternative<bool> (value)) return SkinRenderValueType::boolean;
    if (std::holds_alternative<juce::String> (value)) return SkinRenderValueType::string;
    if (std::holds_alternative<juce::Colour> (value)) return SkinRenderValueType::colour;
    if (std::holds_alternative<SkinFontValue> (value)) return SkinRenderValueType::font;
    return SkinRenderValueType::gradient;
}

SkinRenderValueType valueType (SkinExpressionType type)
{
    switch (type)
    {
        case SkinExpressionType::number: return SkinRenderValueType::number;
        case SkinExpressionType::boolean: return SkinRenderValueType::boolean;
        case SkinExpressionType::string: return SkinRenderValueType::string;
        case SkinExpressionType::colour: return SkinRenderValueType::colour;
    }
    return SkinRenderValueType::string;
}

std::optional<SkinCompiledRenderValue> tokenValue (
    const SkinTokenStore& tokens, const juce::String& path)
{
    if (const auto* colour = tokens.find<juce::Colour> (path)) return *colour;
    if (const auto* number = tokens.find<double> (path)) return *number;
    if (const auto* duration = tokens.find<int64_t> (path))
        return static_cast<double> (*duration);
    if (const auto* string = tokens.find<juce::String> (path)) return *string;
    if (const auto* font = tokens.find<SkinFontValue> (path)) return *font;
    if (const auto* gradient = tokens.find<SkinGradientValue> (path)) return *gradient;
    return std::nullopt;
}

bool isEntryTrigger (SkinAnimationTrigger trigger)
{
    return trigger == SkinAnimationTrigger::workspaceEnter
        || trigger == SkinAnimationTrigger::previewEnter;
}

struct IndexedLayer
{
    SkinPrimitive primitive = SkinPrimitive::rectangle;
    std::optional<juce::String> repeatSource;
    std::set<juce::String> availableProperties;
};

class ProgramCompiler
{
public:
    ProgramCompiler (const SkinTokenStore& tokensIn, SkinValidationReport& reportIn,
                     juce::String fileIn, bool compositeSlotContextsIn = false)
        : tokens (tokensIn), report (reportIn), file (std::move (fileIn)),
          compositeSlotContexts (compositeSlotContextsIn)
    {
    }

    std::optional<SkinRenderProgram> compile (const SkinValidatedDocument& document)
    {
        if (document.kind() != SkinSchemaDocument::render)
        {
            fail ("RENDER004", {}, "Render compiler requires a validated render document.");
            return std::nullopt;
        }

        const auto& root = document.value();
        const auto parsedTarget = skinRenderTargetFromName (
            root.getProperty ("target", {}).toString());
        if (! parsedTarget)
        {
            fail ("RENDER001", "/target", "Renderer target is unknown.");
            return std::nullopt;
        }
        target = *parsedTarget;

        SkinRenderProgram result;
        result.target = target;
        const auto* layers = root.getProperty ("layers", {}).getArray();
        if (layers == nullptr)
        {
            fail ("RENDER004", "/layers", "Renderer layers must be an array.");
            return std::nullopt;
        }
        if (layers->size() > SkinRenderLimits::maximumRootLayers)
        {
            fail ("RENDER010", "/layers", "Renderer has more than 1024 root layers.");
            return std::nullopt;
        }

        for (int i = 0; i < layers->size(); ++i)
        {
            const auto pointer = "/layers/" + juce::String (i);
            auto layer = compileLayer (layers->getReference (i), pointer, 1, false,
                                       std::nullopt);
            if (! layer) return std::nullopt;
            result.baseLayers.push_back (std::move (*layer));
        }

        worstCaseOperations = 0;
        for (const auto& layer : result.baseLayers)
            worstCaseOperations += operationCount (layer);

        if (declaredNodes > SkinRenderLimits::maximumDeclaredNodes
            || worstCaseOperations > SkinRenderLimits::maximumExpandedOperations)
        {
            fail ("RENDER010", "/layers",
                  "Render program exceeds the declared-node or expanded-operation budget.");
            return std::nullopt;
        }
        if (pathCommands > SkinRenderLimits::maximumPackagePathCommands)
        {
            fail ("RENDER013", "/layers", "Path command budget exceeds 250000.");
            return std::nullopt;
        }

        result.variants = compileVariants (root.getProperty ("variants", {}));
        if (report.hasErrors()) return std::nullopt;
        result.transitions = compileTransitions (root.getProperty ("transitions", {}));
        if (report.hasErrors()) return std::nullopt;
        if (root.hasProperty ("contentClip"))
        {
            result.contentClip = root.getProperty ("contentClip", {}).toString();
            const auto layer = std::find_if (result.baseLayers.begin(), result.baseLayers.end(), [&] (const auto& candidate)
            { return candidate.id == result.contentClip; });
            if ((target != SkinRenderTarget::table && target != SkinRenderTarget::channelMap
                 && target != SkinRenderTarget::pluginList && target != SkinRenderTarget::skinList
                 && target != SkinRenderTarget::directoryList)
                || layer == result.baseLayers.end()
                || (layer->primitive != SkinPrimitive::rectangle
                    && layer->primitive != SkinPrimitive::roundedRectangle
                    && layer->primitive != SkinPrimitive::ellipse))
            {
                fail ("RENDER004", "/contentClip",
                      "List contentClip must reference a root rect, roundedRect or ellipse layer.");
                return std::nullopt;
            }
        }
        if (root.hasProperty ("rowLayout"))
        {
            const auto renderContextPaths = usedContextPaths;
            result.rowLayout = compileRowLayout (root.getProperty ("rowLayout", {}));
            usedContextPaths = renderContextPaths;
            if (! result.rowLayout) return std::nullopt;
        }
        if (root.hasProperty ("controlStyle"))
        {
            const auto renderContextPaths = usedContextPaths;
            result.controlStyle = compileControlStyle (root.getProperty ("controlStyle", {}));
            usedContextPaths = renderContextPaths;
            if (! result.controlStyle) return std::nullopt;
        }
        if ((target == SkinRenderTarget::directoryList || target == SkinRenderTarget::smartPlaylistEditor
             || target == SkinRenderTarget::smartPlaylistRuleList) && ! result.rowLayout)
        {
            fail ("RENDER004", "/rowLayout", "Music directory and smart playlist renderers require rowLayout.");
            return std::nullopt;
        }
        if (target == SkinRenderTarget::playlistGrid && (! result.rowLayout || ! result.controlStyle))
        {
            fail ("RENDER004", {}, "Playlist grid renderers require controlStyle and rowLayout.");
            return std::nullopt;
        }
        if (pathCommands > SkinRenderLimits::maximumPackagePathCommands)
        {
            fail ("RENDER013", "/layers", "Path command budget exceeds 250000.");
            return std::nullopt;
        }
        result.propertyTypes = propertyTypes;
        result.propertyDescriptors = propertyDescriptors;
        result.contextPaths = usedContextPaths;
        result.worstCaseOperations = worstCaseOperations;
        result.pathCommandCount = pathCommands;
        return result;
    }

private:
    std::optional<SkinControlStyleDefinition> compileControlStyle (const juce::var& raw)
    {
        const auto& descriptors = skinControlStylePropertyDescriptors (target);
        if (descriptors.empty())
        {
            fail ("RENDER004", "/controlStyle", "This renderer target does not have a control style.");
            return std::nullopt;
        }
        SkinControlStyleDefinition result;
        const auto contexts = skinRenderContextForTarget (target);
        const std::map<juce::String, SkinExpressionType> globalMetricContexts;
        for (const auto& descriptor : descriptors)
        {
            const auto group = descriptor.path.upToFirstOccurrenceOf (".", false, false);
            auto value = raw.getProperty (group, {});
            if (descriptor.path.containsChar ('.'))
                value = value.getProperty (descriptor.path.fromFirstOccurrenceOf (".", false, false), {});
            const auto binding = compileValue (value, descriptor,
                "/controlStyle/" + descriptor.path.replaceCharacter ('.', '/'),
                target == SkinRenderTarget::scrollBar && descriptor.path == "thickness"
                    ? globalMetricContexts : contexts);
            if (! binding) return std::nullopt;
            result.properties.emplace (descriptor.path, *binding);
        }
        return result;
    }

    std::optional<SkinRowLayoutOverride> compileRowLayoutValues (
        const juce::var& raw, const juce::String& pointer)
    {
        SkinRowLayoutOverride result;
        const auto contexts = skinRowLayoutContextTypes (target, false);
        for (const auto& [name, destination] : {
                 std::pair { "rowHeight", &result.rowHeight },
                 std::pair { "rowGap", &result.rowGap },
                 std::pair { "headerHeight", &result.headerHeight } })
            if (raw.hasProperty (name))
            {
                *destination = compileValue (raw.getProperty (name, {}),
                    skinRowLayoutMetricDescriptor (name), pointer + "/" + name, contexts);
                if (! *destination) return std::nullopt;
            }
        const auto slots = raw.getProperty ("slots", {});
        if (const auto* object = slots.getDynamicObject())
        {
            const auto slotContexts = skinRowLayoutContextTypes (target, true);
            const auto& properties = object->getProperties();
            for (int index = 0; index < properties.size(); ++index)
            {
                const auto name = properties.getName (index).toString();
                const auto slotPointer = pointer + "/slots/" + escapePointer (name);
                if (! skinRenderSlotTarget (target, name))
                {
                    fail ("RENDER004", slotPointer, "Row layout slot is unavailable for this target.");
                    return std::nullopt;
                }
                const auto rawFrame = properties.getValueAt (index);
                SkinRowSlotFrame frame;
                for (const auto& [field, destination] : {
                         std::pair { "x", &frame.x }, std::pair { "y", &frame.y },
                         std::pair { "width", &frame.width }, std::pair { "height", &frame.height } })
                {
                    const auto& descriptor = skinRowLayoutFrameDescriptor (field);
                    const auto binding = compileValue (rawFrame.getProperty (field, {}),
                        descriptor, slotPointer + "/" + field, slotContexts);
                    if (! binding) return std::nullopt;
                    *destination = *binding;
                }
                result.slots.emplace (name, std::move (frame));
            }
        }
        return result;
    }

    std::optional<SkinRowLayoutDefinition> compileRowLayout (const juce::var& raw)
    {
        if (target != SkinRenderTarget::table && target != SkinRenderTarget::effectChain
            && target != SkinRenderTarget::channelMap && target != SkinRenderTarget::pluginList
            && target != SkinRenderTarget::channelMixer
            && target != SkinRenderTarget::skinList && target != SkinRenderTarget::popupMenu
            && target != SkinRenderTarget::playlistGrid && target != SkinRenderTarget::directoryList
            && target != SkinRenderTarget::smartPlaylistEditor && target != SkinRenderTarget::smartPlaylistRuleList)
        {
            fail ("RENDER004", "/rowLayout", "This renderer target does not have a row layout.");
            return std::nullopt;
        }
        SkinRowLayoutDefinition result;
        const auto base = compileRowLayoutValues (raw, "/rowLayout");
        if (! base) return std::nullopt;
        result.base = *base;
        const auto breakpointValues = raw.getProperty ("breakpoints", {});
        if (const auto* breakpoints = breakpointValues.getArray())
            for (int index = 0; index < breakpoints->size(); ++index)
            {
                const auto& entry = breakpoints->getReference (index);
                const auto pointer = "/rowLayout/breakpoints/" + juce::String (index);
                const auto when = entry.getProperty ("when", {});
                SkinRowLayoutDefinition::Breakpoint breakpoint;
                if (when.hasProperty ("minWidth"))
                    breakpoint.minWidth = static_cast<float> (static_cast<double> (when.getProperty ("minWidth", {})));
                if (when.hasProperty ("maxWidth"))
                    breakpoint.maxWidth = static_cast<float> (static_cast<double> (when.getProperty ("maxWidth", {})));
                if (breakpoint.minWidth && breakpoint.maxWidth && *breakpoint.minWidth > *breakpoint.maxWidth)
                {
                    fail ("RENDER004", pointer + "/when", "Row layout breakpoint width range is reversed.");
                    return std::nullopt;
                }
                const auto values = compileRowLayoutValues (entry, pointer);
                if (! values) return std::nullopt;
                breakpoint.values = *values;
                result.breakpoints.push_back (std::move (breakpoint));
            }
        return result;
    }

    std::optional<SkinRenderLayer> compileLayer (
        const juce::var& raw, const juce::String& pointer, int depth,
        bool nestedBelowRepeat, std::optional<juce::String> activeRepeatSource)
    {
        if (! raw.isObject() || raw.isArray())
        {
            fail ("RENDER004", pointer, "Layer must be an object.");
            return std::nullopt;
        }
        if (++declaredNodes > SkinRenderLimits::maximumDeclaredNodes
            || depth > SkinRenderLimits::maximumLayerDepth)
        {
            fail ("RENDER010", pointer, "Render program exceeds its compile budget.");
            return std::nullopt;
        }

        const auto id = raw.getProperty ("id", {}).toString();
        if (id.isEmpty())
        {
            fail ("RENDER004", pointer + "/id", "Layer ID is required.");
            return std::nullopt;
        }
        if (! layerIds.insert (id).second)
        {
            fail ("RENDER003", pointer + "/id", "Layer ID is duplicated: " + id + ".");
            return std::nullopt;
        }

        const auto parsedPrimitive = skinPrimitiveFromName (raw.getProperty ("type", {}).toString());
        if (! parsedPrimitive)
        {
            fail ("RENDER002", pointer + "/type", "Layer primitive is unknown.");
            return std::nullopt;
        }
        if (nestedBelowRepeat && *parsedPrimitive == SkinPrimitive::repeat)
        {
            fail ("RENDER008", pointer, "repeat.template cannot contain another repeat.");
            return std::nullopt;
        }

        SkinRenderLayer layer;
        layer.id = id;
        layer.primitive = *parsedPrimitive;
        validateTopLevelFields (raw, layer.primitive, pointer);
        if (report.hasErrors()) return std::nullopt;

        layerIndex.emplace (id, IndexedLayer { layer.primitive, activeRepeatSource, {} });
        compileCommon (raw, layer, pointer, activeRepeatSource);
        if (report.hasErrors()) return std::nullopt;

        switch (layer.primitive)
        {
            case SkinPrimitive::rectangle:
            case SkinPrimitive::ellipse:
                if (! compileFrame (raw, layer, pointer, activeRepeatSource)
                    || ! compileShape (raw, layer, pointer, activeRepeatSource))
                    return std::nullopt;
                break;
            case SkinPrimitive::roundedRectangle:
                if (! compileFrame (raw, layer, pointer, activeRepeatSource)
                    || ! compileShape (raw, layer, pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "radius", pointer, activeRepeatSource))
                    return std::nullopt;
                break;
            case SkinPrimitive::line:
                if (! compilePoint (raw, layer, "start", pointer, activeRepeatSource)
                    || ! compilePoint (raw, layer, "end", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "color", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "width", pointer, activeRepeatSource))
                    return std::nullopt;
                compileOptionalEnum (raw, layer, "cap", pointer, activeRepeatSource);
                break;
            case SkinPrimitive::arc:
                if (! compilePoint (raw, layer, "center", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "radiusX", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "radiusY", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "startAngle", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "endAngle", pointer, activeRepeatSource)
                    || ! compileStroke (raw, layer, pointer, activeRepeatSource, true))
                    return std::nullopt;
                break;
            case SkinPrimitive::path:
                if (! compilePath (raw, layer, pointer)
                    || ! compileOptional (raw, layer, "fill", pointer, activeRepeatSource)
                    || ! compileStroke (raw, layer, pointer, activeRepeatSource, false))
                    return std::nullopt;
                break;
            case SkinPrimitive::text:
                if (! compileFrame (raw, layer, pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "text", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "font", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "fontSize", pointer, activeRepeatSource)
                    || ! compileRequired (raw, layer, "color", pointer, activeRepeatSource))
                    return std::nullopt;
                compileOptionalEnum (raw, layer, "align", pointer, activeRepeatSource);
                if (raw.getDynamicObject()->hasProperty ("maxLines"))
                    compileBinding (raw.getProperty ("maxLines", {}), layer, "maxLines",
                                    pointer + "/maxLines", activeRepeatSource);
                else
                    setFallback (layer, "maxLines");
                compileOptionalEnum (raw, layer, "overflow", pointer, activeRepeatSource);
                break;
            case SkinPrimitive::image:
            case SkinPrimitive::svg:
                if (! compileFrame (raw, layer, pointer, activeRepeatSource)
                    || ! compileAsset (raw, layer, pointer, activeRepeatSource))
                    return std::nullopt;
                break;
            case SkinPrimitive::group:
                if (! compileChildren (raw, layer, pointer, depth, nestedBelowRepeat,
                                      activeRepeatSource))
                    return std::nullopt;
                break;
            case SkinPrimitive::repeat:
                if (! compileRepeat (raw, layer, pointer, depth))
                    return std::nullopt;
                break;
        }

        const auto operations = operationCount (layer);
        if (operations > SkinRenderLimits::maximumExpandedOperations)
        {
            fail ("RENDER010", pointer, "Worst-case expanded renderer exceeds 5000 operations.");
            return std::nullopt;
        }
        return layer;
    }

    void validateTopLevelFields (const juce::var& raw, SkinPrimitive primitive,
                                 const juce::String& pointer)
    {
        std::set<juce::String> allowed { "id", "type" };
        for (const auto& property : skinRenderPropertyDescriptors (primitive))
            allowed.insert (property.path.upToFirstOccurrenceOf (".", false, false));
        if (primitive == SkinPrimitive::group) allowed.insert ("children");
        if (primitive == SkinPrimitive::repeat)
        {
            allowed.insert ("source");
            allowed.insert ("limit");
            allowed.insert ("template");
        }
        const auto& properties = raw.getDynamicObject()->getProperties();
        for (int i = 0; i < properties.size(); ++i)
        {
            const auto name = properties.getName (i).toString();
            if (! allowed.contains (name))
                fail ("RENDER004", pointer + "/" + escapePointer (name),
                      "Property is not valid for this layer primitive.");
        }
    }

    void compileCommon (const juce::var& raw, SkinRenderLayer& layer,
                        const juce::String& pointer,
                        const std::optional<juce::String>& repeatSource)
    {
        compileOptional (raw, layer, "visible", pointer, repeatSource);
        compileOptional (raw, layer, "opacity", pointer, repeatSource);
        if (! raw.getDynamicObject()->hasProperty ("visible")) setFallback (layer, "visible");
        if (! raw.getDynamicObject()->hasProperty ("opacity")) setFallback (layer, "opacity");

        const auto transform = raw.getProperty ("transform", {});
        for (const auto* name : { "translateX", "translateY", "scaleX", "scaleY",
                                  "rotation", "pivotX", "pivotY" })
        {
            const auto path = "transform." + juce::String (name);
            if (transform.isObject() && transform.getDynamicObject()->hasProperty (name))
                compileBinding (transform.getProperty (name, {}), layer, path,
                                pointer + "/transform/" + name, repeatSource);
            else
                setFallback (layer, path);
        }
        validateNestedFields (transform,
                              { "translateX", "translateY", "scaleX", "scaleY", "rotation",
                                "pivotX", "pivotY" }, pointer + "/transform");

        if (raw.getDynamicObject()->hasProperty ("clip"))
            compileOptional (raw, layer, "clip", pointer, repeatSource);
        else
            setFallback (layer, "clip");

        const auto shadow = raw.getProperty ("shadow", {});
        if (! shadow.isVoid())
        {
            validateNestedFields (shadow, { "color", "radius", "offsetX", "offsetY" },
                                  pointer + "/shadow");
            for (const auto* field : { "color", "radius", "offsetX", "offsetY" })
            {
                if (! shadow.isObject() || ! shadow.getDynamicObject()->hasProperty (field))
                {
                    fail ("RENDER004", pointer + "/shadow/" + field,
                          "Shadow requires color, radius, offsetX and offsetY.");
                    continue;
                }
                compileBinding (shadow.getProperty (field, {}), layer,
                                "shadow." + juce::String (field),
                                pointer + "/shadow/" + field, repeatSource);
            }
        }
        else
        {
            for (const auto* field : { "color", "radius", "offsetX", "offsetY" })
                setFallback (layer, "shadow." + juce::String (field));
        }
    }

    bool compileFrame (const juce::var& raw, SkinRenderLayer& layer,
                       const juce::String& pointer,
                       const std::optional<juce::String>& repeatSource)
    {
        const auto frame = raw.getProperty ("frame", {});
        if (! frame.isObject() || frame.isArray())
        {
            fail ("RENDER004", pointer + "/frame", "Layer frame is required.");
            return false;
        }
        const auto hasAnchor = frame.getDynamicObject()->hasProperty ("anchor");
        const auto hasExplicit = frame.getDynamicObject()->hasProperty ("x")
                              || frame.getDynamicObject()->hasProperty ("y")
                              || frame.getDynamicObject()->hasProperty ("width")
                              || frame.getDynamicObject()->hasProperty ("height");
        if (hasAnchor == hasExplicit)
        {
            fail ("RENDER004", pointer + "/frame",
                  "Frame must use exactly one of anchor or x/y/width/height.");
            return false;
        }
        if (hasAnchor)
        {
            validateNestedFields (frame, { "anchor", "inset" }, pointer + "/frame");
            if (! compileBinding (frame.getProperty ("anchor", {}), layer, "frame.anchor",
                                  pointer + "/frame/anchor", repeatSource))
                return false;
            const auto inset = frame.getProperty ("inset", {});
            if (inset.isVoid())
            {
                for (const auto* side : { "top", "right", "bottom", "left" })
                    setFallback (layer, "frame.inset." + juce::String (side));
            }
            else if (inset.isObject() && ! inset.isArray())
            {
                validateNestedFields (inset, { "top", "right", "bottom", "left" },
                                      pointer + "/frame/inset");
                for (const auto* side : { "top", "right", "bottom", "left" })
                    if (inset.getDynamicObject()->hasProperty (side))
                        compileBinding (inset.getProperty (side, {}), layer,
                                        "frame.inset." + juce::String (side),
                                        pointer + "/frame/inset/" + side, repeatSource);
                    else
                        setFallback (layer, "frame.inset." + juce::String (side));
            }
            else
            {
                for (const auto* side : { "top", "right", "bottom", "left" })
                    compileBinding (inset, layer, "frame.inset." + juce::String (side),
                                    pointer + "/frame/inset", repeatSource);
            }
            return ! report.hasErrors();
        }

        validateNestedFields (frame, { "x", "y", "width", "height" }, pointer + "/frame");
        for (const auto* field : { "x", "y", "width", "height" })
        {
            if (! frame.getDynamicObject()->hasProperty (field))
            {
                fail ("RENDER004", pointer + "/frame/" + field,
                      "Explicit frame requires x, y, width and height.");
                return false;
            }
            compileBinding (frame.getProperty (field, {}), layer,
                            "frame." + juce::String (field),
                            pointer + "/frame/" + field, repeatSource);
        }
        return ! report.hasErrors();
    }

    bool compileShape (const juce::var& raw, SkinRenderLayer& layer,
                       const juce::String& pointer,
                       const std::optional<juce::String>& repeatSource)
    {
        if (! compileOptional (raw, layer, "fill", pointer, repeatSource)) return false;
        if (! raw.getDynamicObject()->hasProperty ("fill")) setFallback (layer, "fill");
        return compileStroke (raw, layer, pointer, repeatSource, false);
    }

    bool compileStroke (const juce::var& raw, SkinRenderLayer& layer,
                        const juce::String& pointer,
                        const std::optional<juce::String>& repeatSource, bool required)
    {
        const auto stroke = raw.getProperty ("stroke", {});
        if (stroke.isVoid())
        {
            if (required)
            {
                fail ("RENDER004", pointer + "/stroke", "Stroke is required for this primitive.");
                return false;
            }
            for (const auto* field : { "color", "width", "placement", "cap" })
                setFallback (layer, "stroke." + juce::String (field));
            return true;
        }
        if (! stroke.isObject() || stroke.isArray())
        {
            fail ("RENDER004", pointer + "/stroke", "Stroke must be an object.");
            return false;
        }
        validateNestedFields (stroke, { "color", "width", "placement", "cap" },
                              pointer + "/stroke");
        for (const auto* field : { "color", "width" })
        {
            if (! stroke.getDynamicObject()->hasProperty (field))
            {
                fail ("RENDER004", pointer + "/stroke/" + field,
                      "Stroke requires color and width.");
                return false;
            }
            compileBinding (stroke.getProperty (field, {}), layer,
                            "stroke." + juce::String (field),
                            pointer + "/stroke/" + field, repeatSource);
        }
        compileNestedEnum (stroke, layer, "stroke.placement", "placement",
                           pointer + "/stroke", repeatSource);
        compileNestedEnum (stroke, layer, "stroke.cap", "cap",
                           pointer + "/stroke", repeatSource);
        return ! report.hasErrors();
    }

    bool compilePoint (const juce::var& raw, SkinRenderLayer& layer,
                       const juce::String& name, const juce::String& pointer,
                       const std::optional<juce::String>& repeatSource)
    {
        const auto point = raw.getProperty (name, {});
        if (! point.isObject() || point.isArray())
        {
            fail ("RENDER004", pointer + "/" + name, "Point must be an object.");
            return false;
        }
        validateNestedFields (point, { "x", "y" }, pointer + "/" + name);
        for (const auto* coordinate : { "x", "y" })
        {
            if (! point.getDynamicObject()->hasProperty (coordinate))
            {
                fail ("RENDER004", pointer + "/" + name + "/" + coordinate,
                      "Point requires x and y.");
                return false;
            }
            compileBinding (point.getProperty (coordinate, {}), layer,
                            name + "." + coordinate,
                            pointer + "/" + name + "/" + coordinate, repeatSource);
        }
        return ! report.hasErrors();
    }

    bool compilePath (const juce::var& raw, SkinRenderLayer& layer,
                      const juce::String& pointer)
    {
        if (! raw.getDynamicObject()->hasProperty ("data"))
        {
            fail ("RENDER013", pointer + "/data", "SVG path data is required.");
            return false;
        }
        if (! compileBinding (raw.getProperty ("data", {}), layer, "data",
                              pointer + "/data", std::nullopt))
            return false;
        const auto& binding = layer.properties.at ("data");
        layer.compiledPath = binding.compiledPath;
        return layer.compiledPath != nullptr
            || std::holds_alternative<SkinExpression> (binding.value);
    }

    bool compileAsset (const juce::var& raw, SkinRenderLayer& layer,
                       const juce::String& pointer,
                       const std::optional<juce::String>& repeatSource)
    {
        if (! compileRequired (raw, layer, "source", pointer, repeatSource))
            return false;
        compileOptionalEnum (raw, layer, "fit", pointer, repeatSource);
        if (! raw.getDynamicObject()->hasProperty ("fit")) setFallback (layer, "fit");
        if (! compileOptional (raw, layer, "tint", pointer, repeatSource)) return false;
        if (! raw.getDynamicObject()->hasProperty ("tint")) setFallback (layer, "tint");
        return ! report.hasErrors();
    }

    bool compileChildren (const juce::var& raw, SkinRenderLayer& layer,
                          const juce::String& pointer, int depth, bool nestedBelowRepeat,
                          const std::optional<juce::String>& repeatSource)
    {
        const auto* children = raw.getProperty ("children", {}).getArray();
        if (children == nullptr
            || children->size() > SkinRenderLimits::maximumChildrenPerGroup)
        {
            fail ("RENDER004", pointer + "/children", "Group children must be an array of at most 1024 layers.");
            return false;
        }
        for (int i = 0; i < children->size(); ++i)
        {
            auto child = compileLayer (children->getReference (i),
                                       pointer + "/children/" + juce::String (i), depth + 1,
                                       nestedBelowRepeat, repeatSource);
            if (! child) return false;
            layer.children.push_back (std::move (*child));
        }
        return true;
    }

    bool compileRepeat (const juce::var& raw, SkinRenderLayer& layer,
                        const juce::String& pointer, int depth)
    {
        const auto source = raw.getProperty ("source", {}).toString();
        const auto* sourceDescriptor = skinRepeatSourceDescriptor (source);
        if (sourceDescriptor == nullptr)
        {
            fail ("RENDER009", pointer + "/source", "Repeat source is not whitelisted.");
            return false;
        }
        const auto limit = static_cast<int> (raw.getProperty ("limit", 0));
        if (target != sourceDescriptor->target || limit < 1
            || limit > sourceDescriptor->maximumItems)
        {
            fail ("RENDER009", pointer,
                  "Repeat source is unavailable for this target or limit exceeds its bound.");
            return false;
        }
        const auto templateValue = raw.getProperty ("template", {});
        auto item = compileLayer (templateValue, pointer + "/template", depth + 1, true, source);
        if (! item) return false;
        layer.repeatSource = source;
        layer.repeatLimit = limit;
        layer.children.push_back (std::move (*item));
        return true;
    }

    bool compileRequired (const juce::var& raw, SkinRenderLayer& layer,
                          const juce::String& name, const juce::String& pointer,
                          const std::optional<juce::String>& repeatSource)
    {
        if (! raw.getDynamicObject()->hasProperty (name))
        {
            fail ("RENDER004", pointer + "/" + name, "Required layer property is missing.");
            return false;
        }
        return compileBinding (raw.getProperty (name, {}), layer, name,
                               pointer + "/" + name, repeatSource);
    }

    bool compileOptional (const juce::var& raw, SkinRenderLayer& layer,
                          const juce::String& name, const juce::String& pointer,
                          const std::optional<juce::String>& repeatSource)
    {
        if (! raw.getDynamicObject()->hasProperty (name)) return true;
        return compileBinding (raw.getProperty (name, {}), layer, name,
                               pointer + "/" + name, repeatSource);
    }

    void compileOptionalEnum (const juce::var& raw, SkinRenderLayer& layer,
                              const juce::String& name,
                              const juce::String& pointer,
                              const std::optional<juce::String>& repeatSource)
    {
        if (! raw.getDynamicObject()->hasProperty (name))
        {
            setFallback (layer, name);
            return;
        }
        compileBinding (raw.getProperty (name, {}), layer, name,
                        pointer + "/" + name, repeatSource);
    }

    void compileNestedEnum (const juce::var& raw, SkinRenderLayer& layer,
                            const juce::String& path, const juce::String& name,
                            const juce::String& pointer,
                            const std::optional<juce::String>& repeatSource)
    {
        if (! raw.getDynamicObject()->hasProperty (name))
        {
            setFallback (layer, path);
            return;
        }
        compileBinding (raw.getProperty (name, {}), layer, path,
                        pointer + "/" + name, repeatSource);
    }

    bool compileBinding (const juce::var& raw, SkinRenderLayer& layer,
                         const juce::String& propertyPath, const juce::String& pointer,
                         const std::optional<juce::String>& repeatSource)
    {
        const auto* property = SkinSchemaRegistry::renderPropertyDescriptor (
            layer.primitive, propertyPath);
        if (property == nullptr)
        {
            fail ("RENDER004", pointer, "Property is unavailable for this primitive.");
            return false;
        }
        const auto contexts = compositeSlotContexts
            ? skinRenderContextForAnyCompositeSlot (target, repeatSource)
            : skinRenderContextForTarget (target, repeatSource);
        const auto binding = compileValue (raw, *property, pointer, contexts);
        if (! binding) return false;
        layer.properties.insert_or_assign (propertyPath, *binding);
        layerIndex[layer.id].availableProperties.insert (propertyPath);
        propertyTypes.insert_or_assign (layer.id + "." + propertyPath, property->type);
        propertyDescriptors.insert_or_assign (layer.id + "." + propertyPath, property);
        return true;
    }

    std::optional<SkinRenderPropertyBinding> compileValue (
        const juce::var& raw, const SkinRenderPropertyDescriptor& property,
        const juce::String& pointer,
        const std::map<juce::String, SkinExpressionType>& contexts)
    {
        if (raw.isObject() && ! raw.isArray())
        {
            const auto* object = raw.getDynamicObject();
            if (object->hasProperty ("$ref"))
            {
                auto binding = compileTokenReference (raw.getProperty ("$ref", {}).toString(),
                                                      property, pointer);
                return binding ? finaliseLiteralBinding (std::move (*binding), property, pointer)
                               : std::nullopt;
            }
            if (object->hasProperty ("$context") || object->hasProperty ("$expr"))
            {
                if (! property.expressionAllowed)
                {
                    fail ("RENDER004", pointer, "This property does not accept expressions.");
                    return std::nullopt;
                }
                const auto isContext = object->hasProperty ("$context");
                const auto source = raw.getProperty (isContext ? "$context" : "$expr", {}).toString();
                if (property.semantic == SkinRenderPropertySemantic::svgPath
                    && (! isContext || source != "icon.path"))
                {
                    fail ("RENDER004", pointer,
                          "Dynamic SVG path data is restricted to icon.path.");
                    return std::nullopt;
                }
                if (isContext && ! contexts.contains (source))
                {
                    fail ("RENDER005", pointer, "Context path is unavailable for this target.");
                    return std::nullopt;
                }
                const auto expression = SkinExpression::compile (
                    source, contexts, tokens, report, file, pointer);
                if (! expression) return std::nullopt;
                for (const auto& [contextPath, ignoredType] : contexts)
                {
                    juce::ignoreUnused (ignoredType);
                    if (expression->referencesContext (contextPath))
                        usedContextPaths.insert (contextPath);
                }
                const auto actual = valueType (expression->resultType());
                if (! skinRenderPropertyTypesCompatible (property.type, actual))
                {
                    fail ("RENDER005", pointer, "Expression result type is incompatible with property.");
                    return std::nullopt;
                }
                return SkinRenderPropertyBinding {
                    *expression, property.fallback, &property, false, {}, {}, 0, {}
                };
            }
            fail ("RENDER004", pointer, "Dynamic value object must contain exactly one binding key.");
            return std::nullopt;
        }

        std::optional<SkinCompiledRenderValue> literalValue;
        switch (property.type)
        {
            case SkinRenderValueType::number:
                if (raw.isInt() || raw.isInt64() || raw.isDouble())
                {
                    const auto number = static_cast<double> (raw);
                    if (std::isfinite (number)) literalValue = number;
                }
                break;
            case SkinRenderValueType::boolean:
                if (raw.isBool()) literalValue = static_cast<bool> (raw);
                break;
            case SkinRenderValueType::string:
                if (raw.isString()) literalValue = raw.toString();
                break;
            case SkinRenderValueType::colour:
            case SkinRenderValueType::colourOrGradient:
                if (raw.isString())
                    if (const auto colour = parseSkinCssColour (raw.toString()))
                        literalValue = *colour;
                break;
            case SkinRenderValueType::font:
            case SkinRenderValueType::gradient:
                break;
        }
        if (! literalValue)
        {
            fail ("RENDER004", pointer, "Literal value type is incompatible with property.");
            return std::nullopt;
        }
        return finaliseLiteralBinding (
            SkinRenderPropertyBinding {
                *literalValue, property.fallback, &property, false, {}, {}, 0, {} },
            property, pointer);
    }

    std::optional<SkinRenderPropertyBinding> compileTokenReference (
        const juce::String& path, const SkinRenderPropertyDescriptor& property,
        const juce::String& pointer)
    {
        const auto value = tokenValue (tokens, path);
        if (! value || ! skinRenderPropertyTypesCompatible (property.type, valueType (*value)))
        {
            fail ("RENDER006", pointer, "Token does not exist or has an incompatible type.");
            return std::nullopt;
        }
        auto binding = SkinRenderPropertyBinding {
            *value, property.fallback, &property, false, {}, {}, 0, {}
        };
        binding.tokenReference = path;
        return binding;
    }

    std::optional<SkinRenderPropertyBinding> finaliseLiteralBinding (
        SkinRenderPropertyBinding binding, const SkinRenderPropertyDescriptor& property,
        const juce::String& pointer)
    {
        const auto* value = std::get_if<SkinCompiledRenderValue> (&binding.value);
        jassert (value != nullptr);
        jassert (binding.descriptor == &property);
        if (value == nullptr) return std::nullopt;

        if (! property.enumValues.empty())
        {
            const auto* text = std::get_if<juce::String> (value);
            if (text == nullptr
                || std::find (property.enumValues.begin(), property.enumValues.end(), *text)
                       == property.enumValues.end())
            {
                fail ("RENDER004", pointer, "String is not an accepted enum value.");
                return std::nullopt;
            }
        }

        if (property.semantic == SkinRenderPropertySemantic::assetPath)
        {
            const auto* source = std::get_if<juce::String> (value);
            if (source == nullptr || ! source->startsWith ("assets/")
                || ! SkinPackage::isSafeRelativePath (*source)
                || (property.maximumUtf8Bytes > 0
                    && source->getNumBytesAsUTF8()
                        > static_cast<size_t> (property.maximumUtf8Bytes)))
            {
                fail ("RENDER007", pointer, "Asset path is not package-local and safe.");
                return std::nullopt;
            }
        }
        else if (property.semantic == SkinRenderPropertySemantic::svgPath)
        {
            const auto* data = std::get_if<juce::String> (value);
            if (data == nullptr || data->isEmpty()
                || (property.maximumUtf8Bytes > 0
                    && data->getNumBytesAsUTF8()
                        > static_cast<size_t> (property.maximumUtf8Bytes)))
            {
                fail ("RENDER013", pointer,
                      "SVG path data is empty or over the byte budget.");
                return std::nullopt;
            }
            const auto parsed = skinCompileSvgPathData (*data);
            if (! parsed)
            {
                fail ("RENDER013", pointer, "SVG path grammar or numeric data is invalid.");
                return std::nullopt;
            }
            pathCommands += parsed->commandCount;
            binding.compiledPath = parsed->path;
            binding.compiledPathCommandCount = parsed->commandCount;
        }
        if (property.type == SkinRenderValueType::number
            && (property.integerOnly || property.minimum || property.maximum))
        {
            const auto* number = std::get_if<double> (value);
            if (number == nullptr
                || (property.integerOnly && *number != std::round (*number))
                || (property.minimum && *number < *property.minimum)
                || (property.maximum && *number > *property.maximum))
            {
                fail ("RENDER004", pointer,
                      "Numeric value violates its descriptor range or integer constraint.");
                return std::nullopt;
            }
        }
        return binding;
    }

    void setFallback (SkinRenderLayer& layer, const juce::String& propertyPath)
    {
        const auto* descriptor = SkinSchemaRegistry::renderPropertyDescriptor (
            layer.primitive, propertyPath);
        jassert (descriptor != nullptr);
        if (descriptor == nullptr) return;
        layer.properties.insert_or_assign (
            propertyPath,
            SkinRenderPropertyBinding { descriptor->fallback.literal, descriptor->fallback,
                                        descriptor, true, {}, {}, 0, {} });
        layerIndex[layer.id].availableProperties.insert (propertyPath);
        propertyTypes.insert_or_assign (layer.id + "." + propertyPath, descriptor->type);
        propertyDescriptors.insert_or_assign (layer.id + "." + propertyPath, descriptor);
    }

    void validateNestedFields (const juce::var& value,
                               std::initializer_list<const char*> allowed,
                               const juce::String& pointer)
    {
        if (value.isVoid()) return;
        if (! value.isObject() || value.isArray())
        {
            fail ("RENDER004", pointer, "Property must be an object.");
            return;
        }
        const auto& properties = value.getDynamicObject()->getProperties();
        for (int i = 0; i < properties.size(); ++i)
        {
            const auto name = properties.getName (i).toString();
            if (std::none_of (allowed.begin(), allowed.end(), [&] (const char* item)
                             { return name == item; }))
                fail ("RENDER004", pointer + "/" + escapePointer (name),
                      "Unknown field in a closed render object.");
        }
    }

    std::array<SkinVariantPatch, 6> compileVariants (const juce::var& raw)
    {
        std::array<SkinVariantPatch, 6> result;
        const auto& names = skinVariantNamesInMergeOrder();
        jassert (names.size() == result.size());
        for (size_t i = 0; i < names.size(); ++i)
        {
            result[i].name = names[i];
            if (! raw.isObject() || ! raw.getDynamicObject()->hasProperty (names[i])) continue;
            const auto patch = raw.getProperty (names[i], {});
            if (! patch.isObject() || patch.isArray())
            {
                fail ("RENDER012", "/variants/" + names[i], "Variant patch must be an object.");
                continue;
            }
            compilePatch (patch, result[i].properties,
                          "/variants/" + names[i], "RENDER012");
        }
        return result;
    }

    void compilePatch (const juce::var& patch,
                       std::map<juce::String, SkinRenderPropertyBinding>& destination,
                       const juce::String& pointer, const juce::String& errorCode)
    {
        const auto& values = patch.getDynamicObject()->getProperties();
        for (int i = 0; i < values.size(); ++i)
        {
            const auto fullPath = values.getName (i).toString();
            const auto resolved = resolvePropertyPath (fullPath);
            if (! resolved)
            {
                fail (errorCode, pointer + "/" + escapePointer (fullPath),
                      "Patch references an unknown layer or property.");
                continue;
            }
            const auto& [layerId, propertyPath] = *resolved;
            const auto indexed = layerIndex.find (layerId);
            const auto* property = SkinSchemaRegistry::renderPropertyDescriptor (
                indexed->second.primitive, propertyPath);
            jassert (property != nullptr);
            const auto contexts = compositeSlotContexts
                ? skinRenderContextForAnyCompositeSlot (
                    target, indexed->second.repeatSource)
                : skinRenderContextForTarget (
                    target, indexed->second.repeatSource);
            const auto binding = compileValue (
                values.getValueAt (i), *property,
                pointer + "/" + escapePointer (fullPath),
                contexts);
            if (binding) destination.insert_or_assign (fullPath, *binding);
        }
    }

    std::optional<std::pair<juce::String, juce::String>> resolvePropertyPath (
        const juce::String& fullPath) const
    {
        const IndexedLayer* bestLayer = nullptr;
        juce::String bestId;
        for (const auto& [id, layer] : layerIndex)
            if (fullPath.startsWith (id + ".") && id.length() > bestId.length())
            {
                bestId = id;
                bestLayer = &layer;
            }
        if (bestLayer == nullptr) return std::nullopt;
        const auto propertyPath = fullPath.substring (bestId.length() + 1);
        if (SkinSchemaRegistry::renderPropertyDescriptor (
                bestLayer->primitive, propertyPath) == nullptr
            || ! bestLayer->availableProperties.contains (propertyPath))
            return std::nullopt;
        return std::pair (bestId, propertyPath);
    }

    std::map<SkinAnimationTrigger, SkinTransition> compileTransitions (const juce::var& raw)
    {
        std::map<SkinAnimationTrigger, SkinTransition> result;
        if (raw.isVoid()) return result;
        if (! raw.isObject() || raw.isArray())
        {
            fail ("RENDER011", "/transitions", "Transitions must be an object.");
            return result;
        }
        const auto& values = raw.getDynamicObject()->getProperties();
        for (int i = 0; i < values.size(); ++i)
        {
            const auto name = values.getName (i).toString();
            const auto trigger = skinAnimationTriggerFromName (name);
            if (! trigger)
            {
                fail ("RENDER011", "/transitions/" + escapePointer (name),
                      "Transition trigger is unknown.");
                continue;
            }
            const auto transitionValue = values.getValueAt (i);
            auto transition = compileTransition (*trigger, transitionValue,
                                                 "/transitions/" + name);
            if (transition) result.emplace (*trigger, std::move (*transition));
        }
        return result;
    }

    std::optional<SkinTransition> compileTransition (
        SkinAnimationTrigger trigger, const juce::var& raw, const juce::String& pointer)
    {
        if (! raw.isObject() || raw.isArray())
        {
            fail ("RENDER011", pointer, "Transition must be an object.");
            return std::nullopt;
        }
        validateNestedFields (raw,
                              { "durationMs", "delayMs", "easing", "properties", "from",
                                "respectReduceMotion" }, pointer);
        SkinTransition result;
        const auto duration = raw.getProperty ("durationMs", {});
        if (duration.isObject() && duration.getDynamicObject()->hasProperty ("$ref"))
        {
            const auto token = duration.getProperty ("$ref", {}).toString();
            const auto* value = tokens.find<int64_t> (token);
            if (value == nullptr || *value < 0 || *value > 10000)
            {
                fail ("RENDER011", pointer + "/durationMs", "Duration token is invalid.");
                return std::nullopt;
            }
            result.durationMs = static_cast<int> (*value);
            result.durationTokenReference = token;
        }
        else if (duration.isInt() || duration.isInt64())
        {
            result.durationMs = static_cast<int> (duration);
            if (result.durationMs < 0 || result.durationMs > 10000)
            {
                fail ("RENDER011", pointer + "/durationMs", "Duration is outside 0 through 10000 ms.");
                return std::nullopt;
            }
        }
        else
        {
            fail ("RENDER011", pointer + "/durationMs", "Transition duration is required.");
            return std::nullopt;
        }
        result.delayMs = static_cast<int> (raw.getProperty ("delayMs", 0));
        if (result.delayMs < 0 || result.delayMs > 10000)
        {
            fail ("RENDER011", pointer + "/delayMs", "Delay is outside 0 through 10000 ms.");
            return std::nullopt;
        }
        const auto easing = skinAnimationEasingFromName (
            raw.getProperty ("easing", {}).toString());
        if (! easing)
        {
            fail ("RENDER011", pointer + "/easing", "Transition easing is unknown.");
            return std::nullopt;
        }
        result.easing = *easing;
        result.respectReduceMotion = static_cast<bool> (
            raw.getProperty ("respectReduceMotion", true));
        if (isEntryTrigger (trigger) && ! result.respectReduceMotion)
        {
            fail ("RENDER011", pointer + "/respectReduceMotion",
                  "Entry transitions must respect Reduce Motion.");
            return std::nullopt;
        }

        const auto* properties = raw.getProperty ("properties", {}).getArray();
        if (properties == nullptr || properties->isEmpty() || properties->size() > 128)
        {
            fail ("RENDER011", pointer + "/properties", "Transition properties are required.");
            return std::nullopt;
        }
        std::set<juce::String> unique;
        for (const auto& value : *properties)
        {
            const auto propertyPath = value.toString();
            if (! unique.insert (propertyPath).second || ! resolvePropertyPath (propertyPath))
            {
                fail ("RENDER011", pointer + "/properties",
                      "Transition property is duplicated or unknown.");
                return std::nullopt;
            }
            const auto type = propertyTypes.find (propertyPath);
            if (type == propertyTypes.end())
            {
                fail ("RENDER011", pointer + "/properties", "Transition property is unavailable.");
                return std::nullopt;
            }
            const auto descriptor = propertyDescriptors.find (propertyPath);
            if (descriptor == propertyDescriptors.end() || descriptor->second == nullptr)
            {
                fail ("RENDER011", pointer + "/properties",
                      "Transition property has no compiled descriptor.");
                return std::nullopt;
            }
            if (*easing == SkinAnimationEasing::springGentle
                && (type->second != SkinRenderValueType::number
                    || ! descriptor->second->interpolable))
            {
                fail ("RENDER011", pointer + "/easing",
                      "springGentle only accepts numeric properties.");
                return std::nullopt;
            }
            result.properties.push_back (propertyPath);
        }

        const auto from = raw.getProperty ("from", {});
        if (isEntryTrigger (trigger))
        {
            if (! from.isObject() || from.isArray())
            {
                fail ("RENDER011", pointer + "/from", "Entry transition requires a from patch.");
                return std::nullopt;
            }
            compilePatch (from, result.from, pointer + "/from", "RENDER011");
            if (result.from.size() != result.properties.size())
            {
                fail ("RENDER011", pointer + "/from",
                      "Entry transition from keys must equal its properties list.");
                return std::nullopt;
            }
            for (const auto& path : result.properties)
                if (! result.from.contains (path))
                {
                    fail ("RENDER011", pointer + "/from",
                          "Entry transition from keys must equal its properties list.");
                    return std::nullopt;
                }
        }
        else if (! from.isVoid())
        {
            fail ("RENDER011", pointer + "/from", "Computed transitions cannot provide from.");
            return std::nullopt;
        }
        return result;
    }

    static int operationCount (const SkinRenderLayer& layer)
    {
        if (layer.primitive == SkinPrimitive::repeat)
            return 1 + layer.repeatLimit * operationCount (layer.children.front());
        int result = 1;
        for (const auto& child : layer.children) result += operationCount (child);
        return result;
    }

    void fail (juce::String code, juce::String pointer, juce::String message)
    {
        report.addError (std::move (code), file, std::move (pointer), std::move (message),
                         "Follow the Skin API 1 render contract.");
    }

    const SkinTokenStore& tokens;
    SkinValidationReport& report;
    juce::String file;
    bool compositeSlotContexts = false;
    SkinRenderTarget target = SkinRenderTarget::surface;
    std::set<juce::String> layerIds;
    std::map<juce::String, IndexedLayer> layerIndex;
    std::map<juce::String, SkinRenderValueType> propertyTypes;
    std::map<juce::String, const SkinRenderPropertyDescriptor*> propertyDescriptors;
    std::set<juce::String> usedContextPaths;
    int declaredNodes = 0;
    int worstCaseOperations = 0;
    int pathCommands = 0;
};

struct PendingStyle
{
    juce::String fullName;
    SkinRenderTarget target = SkinRenderTarget::surface;
    juce::String rendererPath;
    std::map<juce::String, juce::String> slots;
    std::optional<SkinValidatedDocument> rendererDocument;
};

bool parseJson (const juce::String& text, juce::var& value, SkinValidationReport& report,
                const juce::String& file)
{
    const auto result = juce::JSON::parse (text, value);
    if (result.failed())
    {
        report.addError ("SCHEMA002", file, {}, "Renderer JSON could not be parsed: "
                         + result.getErrorMessage());
        return false;
    }
    return true;
}

} // namespace

std::optional<SkinRenderProgram> SkinRenderCompiler::compileProgram (
    const SkinValidatedDocument& document, const SkinTokenStore& tokens,
    SkinValidationReport& report)
{
    return ProgramCompiler (tokens, report, "renderer.json").compile (document);
}

namespace
{
bool rebindTokenBinding (SkinRenderPropertyBinding& binding,
                         const SkinTokenStore& tokens,
                         SkinValidationReport& report)
{
    if (auto* expression = std::get_if<SkinExpression> (&binding.value))
        if (! expression->rebindTokens (tokens, report)) return false;
    if (binding.tokenReference.isEmpty()) return true;
    const auto value = tokenValue (tokens, binding.tokenReference);
    const auto* descriptor = binding.descriptor;
    if (! value || descriptor == nullptr
        || ! skinRenderPropertyTypesCompatible (descriptor->type, valueType (*value)))
    {
        report.addError ("RENDER006", "components/index.json", binding.tokenReference,
                         "Inherited renderer token override has an incompatible type.");
        return false;
    }

    if (! descriptor->enumValues.empty())
    {
        const auto* text = std::get_if<juce::String> (&*value);
        if (text == nullptr
            || std::find (descriptor->enumValues.begin(), descriptor->enumValues.end(), *text)
                == descriptor->enumValues.end())
        {
            report.addError ("RENDER004", "components/index.json", binding.tokenReference,
                             "Inherited renderer token override is outside its enum.");
            return false;
        }
    }

    if (descriptor->semantic == SkinRenderPropertySemantic::assetPath)
    {
        const auto* path = std::get_if<juce::String> (&*value);
        if (path == nullptr || ! path->startsWith ("assets/")
            || ! SkinPackage::isSafeRelativePath (*path)
            || (descriptor->maximumUtf8Bytes > 0
                && path->getNumBytesAsUTF8()
                    > static_cast<size_t> (descriptor->maximumUtf8Bytes)))
        {
            report.addError ("RENDER007", "components/index.json", binding.tokenReference,
                             "Inherited renderer asset token is not package-local and safe.");
            return false;
        }
    }
    else if (descriptor->semantic == SkinRenderPropertySemantic::svgPath)
    {
        const auto* data = std::get_if<juce::String> (&*value);
        SkinSvgPathCompileError error = SkinSvgPathCompileError::none;
        const auto parsed = data != nullptr
            ? skinCompileSvgPathData (*data, SkinRenderLimits::maximumPackagePathCommands,
                                      &error)
            : std::nullopt;
        if (! parsed)
        {
            report.addError ("RENDER013", "components/index.json", binding.tokenReference,
                             "Inherited renderer SVG-path token is invalid.");
            return false;
        }
        binding.compiledPath = parsed->path;
        binding.compiledPathCommandCount = parsed->commandCount;
    }

    if (descriptor->type == SkinRenderValueType::number
        && (descriptor->integerOnly || descriptor->minimum || descriptor->maximum))
    {
        const auto* number = std::get_if<double> (&*value);
        if (number == nullptr
            || (descriptor->integerOnly && *number != std::round (*number))
            || (descriptor->minimum && *number < *descriptor->minimum)
            || (descriptor->maximum && *number > *descriptor->maximum))
        {
            report.addError ("RENDER004", "components/index.json", binding.tokenReference,
                             "Inherited renderer numeric token violates its property range.");
            return false;
        }
    }
    binding.value = *value;
    return true;
}

bool rebindLayerTokens (SkinRenderLayer& layer, const SkinTokenStore& tokens,
                        SkinValidationReport& report, int& pathCommands)
{
    for (auto& [ignored, binding] : layer.properties)
    {
        juce::ignoreUnused (ignored);
        if (! rebindTokenBinding (binding, tokens, report)) return false;
        if (binding.compiledPathCommandCount > 0)
        {
            if (pathCommands > SkinRenderLimits::maximumPackagePathCommands
                             - binding.compiledPathCommandCount)
            {
                report.addError ("RENDER013", "components/index.json", {},
                                 "Inherited renderer path-command budget exceeds 250000.");
                return false;
            }
            pathCommands += binding.compiledPathCommandCount;
        }
    }
    if (layer.primitive == SkinPrimitive::path)
        if (const auto found = layer.properties.find ("data");
            found != layer.properties.end())
            layer.compiledPath = found->second.compiledPath;
    for (auto& child : layer.children)
        if (! rebindLayerTokens (child, tokens, report, pathCommands)) return false;
    return true;
}
} // namespace

bool SkinRenderCompiler::rebindProgramTokens (
    SkinRenderProgram& program, const SkinTokenStore& tokens,
    SkinValidationReport& report)
{
    if (program.controlStyle)
        for (auto& [name, binding] : program.controlStyle->properties)
        {
            juce::ignoreUnused (name);
            if (! rebindTokenBinding (binding, tokens, report)) return false;
        }
    if (program.rowLayout)
    {
        const auto rebind = [&] (SkinRowLayoutOverride& values)
        {
            for (auto* binding : { &values.rowHeight, &values.rowGap, &values.headerHeight })
                if (*binding && ! rebindTokenBinding (**binding, tokens, report)) return false;
            for (auto& [name, frame] : values.slots)
            {
                juce::ignoreUnused (name);
                for (auto* binding : { &frame.x, &frame.y, &frame.width, &frame.height })
                    if (! rebindTokenBinding (*binding, tokens, report)) return false;
            }
            return true;
        };
        if (! rebind (program.rowLayout->base)) return false;
        for (auto& breakpoint : program.rowLayout->breakpoints)
            if (! rebind (breakpoint.values)) return false;
    }
    int pathCommands = 0;
    for (auto& layer : program.baseLayers)
        if (! rebindLayerTokens (layer, tokens, report, pathCommands)) return false;
    for (auto& variant : program.variants)
        for (auto& [ignored, binding] : variant.properties)
        {
            juce::ignoreUnused (ignored);
            if (! rebindTokenBinding (binding, tokens, report)) return false;
            if (binding.compiledPathCommandCount > 0)
            {
                if (pathCommands > SkinRenderLimits::maximumPackagePathCommands
                                 - binding.compiledPathCommandCount)
                {
                    report.addError ("RENDER013", "components/index.json", {},
                                     "Inherited renderer path-command budget exceeds 250000.");
                    return false;
                }
                pathCommands += binding.compiledPathCommandCount;
            }
        }
    for (auto& [ignored, transition] : program.transitions)
    {
        juce::ignoreUnused (ignored);
        if (transition.durationTokenReference.isNotEmpty())
        {
            const auto* duration = tokens.find<int64_t> (
                transition.durationTokenReference);
            if (duration == nullptr || *duration < 0 || *duration > 10000)
            {
                report.addError ("RENDER011", "components/index.json",
                                 transition.durationTokenReference,
                                 "Inherited renderer duration override is invalid.");
                return false;
            }
            transition.durationMs = static_cast<int> (*duration);
        }
        for (auto& [ignoredPath, binding] : transition.from)
        {
            juce::ignoreUnused (ignoredPath);
            if (! rebindTokenBinding (binding, tokens, report)) return false;
            if (binding.compiledPathCommandCount > 0)
            {
                if (pathCommands > SkinRenderLimits::maximumPackagePathCommands
                                 - binding.compiledPathCommandCount)
                {
                    report.addError ("RENDER013", "components/index.json", {},
                                     "Inherited renderer path-command budget exceeds 250000.");
                    return false;
                }
                pathCommands += binding.compiledPathCommandCount;
            }
        }
    }
    program.pathCommandCount = pathCommands;
    return ! report.hasErrors();
}

std::map<juce::String, SkinRenderStyleEntry> SkinRenderCompiler::compileIndex (
    const SkinValidatedDocument& index, const SkinPackage& package,
    const SkinTokenStore& tokens, const PlayerComponentCatalog& catalog,
    SkinValidationReport& report)
{
    juce::ignoreUnused (catalog);
    std::map<juce::String, SkinRenderStyleEntry> result;
    if (index.kind() != SkinSchemaDocument::componentIndex)
    {
        report.addError ("RENDER004", "components/index.json", {},
                         "Renderer index compiler requires a validated component-index document.");
        return result;
    }

    std::vector<PendingStyle> pending;
    const auto& groups = index.value().getDynamicObject()->getProperties();
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const auto targetName = groups.getName (groupIndex).toString();
        const auto target = skinRenderTargetFromName (targetName);
        if (! target)
        {
            report.addError ("RENDER001", "components/index.json", "/" + targetName,
                             "Renderer index target is unknown.");
            continue;
        }
        const auto styles = groups.getValueAt (groupIndex);
        if (! styles.isObject() || styles.isArray()
            || styles.getDynamicObject()->getProperties().isEmpty())
        {
            report.addError ("RENDER004", "components/index.json", "/" + targetName,
                             "Renderer target must contain at least one style.");
            continue;
        }
        const auto& styleProperties = styles.getDynamicObject()->getProperties();
        for (int styleIndex = 0; styleIndex < styleProperties.size(); ++styleIndex)
        {
            const auto styleName = styleProperties.getName (styleIndex).toString();
            const auto pointer = "/" + targetName + "/" + escapePointer (styleName);
            if (styleName.isEmpty() || styleName.length() > 80)
            {
                report.addError ("RENDER004", "components/index.json", pointer,
                                 "Style name is invalid for this renderer target.");
                continue;
            }
            PendingStyle item;
            item.fullName = targetName + "." + styleName;
            item.target = *target;
            const auto entry = styleProperties.getValueAt (styleIndex);
            if (entry.isString())
                item.rendererPath = entry.toString();
            else if (entry.isObject() && ! entry.isArray())
            {
                item.rendererPath = entry.getProperty ("renderer", {}).toString();
                const auto slots = entry.getProperty ("slots", {});
                if (! slots.isVoid() && (! slots.isObject() || slots.isArray()))
                {
                    report.addError ("RENDER004", "components/index.json", pointer + "/slots",
                                     "Slots must be an object.");
                    continue;
                }
                if (slots.isObject())
                {
                    const auto& slotProperties = slots.getDynamicObject()->getProperties();
                    for (int slotIndex = 0; slotIndex < slotProperties.size(); ++slotIndex)
                    {
                        const auto slotName = slotProperties.getName (slotIndex).toString();
                        const auto styleReference = slotProperties.getValueAt (slotIndex).toString();
                        const auto expectedTarget = skinRenderSlotTarget (*target, slotName);
                        const auto referencedTarget = styleReference.upToFirstOccurrenceOf (".", false, false);
                        if (! expectedTarget || styleReference == referencedTarget
                            || *expectedTarget != referencedTarget
                            || ! skinRenderTargetFromName (referencedTarget))
                        {
                            report.addError ("RENDER004", "components/index.json",
                                             pointer + "/slots/" + escapePointer (slotName),
                                             "Slot name or referenced renderer target is invalid.");
                            continue;
                        }
                        item.slots.emplace (slotName, styleReference);
                    }
                }
            }
            else
            {
                report.addError ("RENDER004", "components/index.json", pointer,
                                 "Style entry must be a renderer path or composite entry.");
                continue;
            }
            if (! SkinPackage::isSafeRelativePath (item.rendererPath)
                || ! item.rendererPath.endsWithIgnoreCase (".json"))
            {
                report.addError ("RENDER007", "components/index.json", pointer,
                                 "Renderer path is not a safe package-local JSON path.");
                continue;
            }
            pending.push_back (std::move (item));
        }
    }
    if (report.hasErrors()) return result;

    std::map<juce::String, SkinValidatedDocument> validatedByPath;
    for (auto& style : pending)
    {
        if (const auto found = validatedByPath.find (style.rendererPath);
            found != validatedByPath.end())
        {
            style.rendererDocument = found->second;
            continue;
        }
        const auto beforeRead = report.getDiagnostics().size();
        const auto text = package.readText (style.rendererPath, report);
        if (report.getDiagnostics().size() != beforeRead) continue;
        juce::var raw;
        if (! parseJson (text, raw, report, style.rendererPath)) continue;
        const auto validated = SkinSchemaValidator::validate (
            SkinSchemaDocument::render, raw, style.rendererPath, report);
        if (! validated) continue;
        validatedByPath.emplace (style.rendererPath, *validated);
        style.rendererDocument = *validated;
    }
    if (report.hasErrors()) return result;

    int totalPathCommands = 0;
    std::map<juce::String, SkinRenderProgram> compiledByPath;
    for (auto& style : pending)
    {
        SkinRenderProgram* program = nullptr;
        if (const auto found = compiledByPath.find (style.rendererPath);
            found != compiledByPath.end())
        {
            program = &found->second;
        }
        else
        {
            ProgramCompiler compiler (tokens, report, style.rendererPath, true);
            auto compiled = compiler.compile (*style.rendererDocument);
            if (! compiled) continue;
            totalPathCommands += compiled->pathCommandCount;
            if (totalPathCommands > SkinRenderLimits::maximumPackagePathCommands)
            {
                report.addError ("RENDER013", style.rendererPath, "/layers",
                                 "Package renderer path-command budget exceeds 250000.");
                continue;
            }
            program = &compiledByPath.emplace (style.rendererPath,
                                               std::move (*compiled)).first->second;
        }
        if (program->target != style.target)
        {
            report.addError ("RENDER001", style.rendererPath, "/target",
                             "Renderer target does not match its index target.");
            continue;
        }
        result.emplace (style.fullName,
                        SkinRenderStyleEntry { *program, std::move (style.slots), {} });
    }
    if (report.hasErrors()) result.clear();
    return result;
}
