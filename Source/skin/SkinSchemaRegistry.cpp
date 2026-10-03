#include "SkinSchemaRegistry.h"

#include "PlayerComponentCatalog.h"
#include "SkinRenderDefinition.h"
#include "SkinControlStyle.h"
#include "app/ProductIdentity.h"

#include <algorithm>

namespace
{
juce::String schemaIdentifier (SkinSchemaDocument document)
{
    switch (document)
    {
        case SkinSchemaDocument::manifest:
            return MoonwakeIdentity::skinManifestSchemaUri;
        case SkinSchemaDocument::tokens:
            return MoonwakeIdentity::skinTokensSchemaUri;
        case SkinSchemaDocument::componentIndex:
            return MoonwakeIdentity::skinComponentIndexSchemaUri;
        case SkinSchemaDocument::layout:
            return MoonwakeIdentity::skinLayoutSchemaUri;
        case SkinSchemaDocument::render:
            return MoonwakeIdentity::skinRenderSchemaUri;
    }

    jassertfalse;
    return {};
}

juce::String schemaTitle (juce::StringRef documentName)
{
    return juce::String (MoonwakeIdentity::skinApiName) + " "
        + juce::String (MoonwakeIdentity::skinApiVersion) + " " + documentName;
}

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

juce::var array (std::initializer_list<juce::var> values)
{
    juce::Array<juce::var> result;
    for (const auto& value : values)
        result.add (value);
    return juce::var (std::move (result));
}

juce::var stringSchema (int minLength, int maxLength, const juce::String& pattern = {})
{
    auto result = object();
    auto* value = result.getDynamicObject();
    value->setProperty ("type", "string");
    if (minLength >= 0)
        value->setProperty ("minLength", minLength);
    if (maxLength >= 0)
        value->setProperty ("maxLength", maxLength);
    if (pattern.isNotEmpty())
        value->setProperty ("pattern", pattern);
    return result;
}

juce::var reference (const juce::String& path)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("$ref", path);
    return result;
}

juce::var typedSchema (const juce::String& type)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("type", type);
    return result;
}

juce::var oneOf (std::initializer_list<juce::var> choices)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("oneOf", array (choices));
    return result;
}

juce::var closedObject (const juce::var& properties,
                        std::initializer_list<juce::var> required = {})
{
    auto result = typedSchema ("object");
    result.getDynamicObject()->setProperty ("additionalProperties", false);
    if (required.size() != 0)
        result.getDynamicObject()->setProperty ("required", array (required));
    result.getDynamicObject()->setProperty ("properties", properties);
    return result;
}

juce::var dynamicObject (const juce::var& valueSchema)
{
    auto result = typedSchema ("object");
    result.getDynamicObject()->setProperty ("additionalProperties", valueSchema);
    return result;
}

juce::var boundedNumber (const juce::String& type, double minimum, double maximum)
{
    auto result = typedSchema (type);
    result.getDynamicObject()->setProperty ("minimum", minimum);
    result.getDynamicObject()->setProperty ("maximum", maximum);
    return result;
}

juce::var enumSchema (std::initializer_list<juce::var> values)
{
    auto result = object();
    result.getDynamicObject()->setProperty ("enum", array (values));
    return result;
}

juce::var enumSchema (const std::vector<juce::String>& values)
{
    juce::Array<juce::var> items;
    for (const auto& value : values) items.add (value);
    auto result = object();
    result.getDynamicObject()->setProperty ("enum", juce::var (std::move (items)));
    return result;
}

juce::var arraySchema (const juce::var& item, int minimum = -1, int maximum = -1)
{
    auto result = typedSchema ("array");
    if (minimum >= 0) result.getDynamicObject()->setProperty ("minItems", minimum);
    if (maximum >= 0) result.getDynamicObject()->setProperty ("maxItems", maximum);
    result.getDynamicObject()->setProperty ("items", item);
    return result;
}

juce::var manifestSchema()
{
    const auto semverPattern = juce::String (
        R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$)");
    const auto pathPattern = juce::String (
        R"(^(?!/)(?!.*//)(?!.*(?:^|/)\.{1,2}(?:/|$))(?!.*\\).+$)");

    auto semver = stringSchema (1, 64, semverPattern);
    auto path = stringSchema (1, 512, pathPattern);

    auto authorProperties = object();
    authorProperties.getDynamicObject()->setProperty ("name", stringSchema (1, 80));
    authorProperties.getDynamicObject()->setProperty (
        "url", stringSchema (0, 2048, R"(^https?://)"));
    auto author = object();
    author.getDynamicObject()->setProperty ("type", "object");
    author.getDynamicObject()->setProperty ("additionalProperties", false);
    author.getDynamicObject()->setProperty ("required", array ({ "name" }));
    author.getDynamicObject()->setProperty ("properties", authorProperties);

    auto layoutProperties = object();
    for (const auto& name : { "studio", "listening", "effects", "settings" })
        layoutProperties.getDynamicObject()->setProperty (name, reference ("#/$defs/path"));
    auto layouts = object();
    layouts.getDynamicObject()->setProperty ("type", "object");
    layouts.getDynamicObject()->setProperty ("additionalProperties", false);
    layouts.getDynamicObject()->setProperty ("properties", layoutProperties);

    auto entrypointProperties = object();
    entrypointProperties.getDynamicObject()->setProperty ("tokens", reference ("#/$defs/path"));
    entrypointProperties.getDynamicObject()->setProperty ("components", reference ("#/$defs/path"));
    entrypointProperties.getDynamicObject()->setProperty ("layouts", layouts);
    auto entrypoints = object();
    entrypoints.getDynamicObject()->setProperty ("type", "object");
    entrypoints.getDynamicObject()->setProperty ("additionalProperties", false);
    entrypoints.getDynamicObject()->setProperty ("properties", entrypointProperties);

    auto capabilityItem = stringSchema (1, 80, R"(^[a-z][a-z0-9.-]*$)");
    auto capabilities = object();
    capabilities.getDynamicObject()->setProperty ("type", "array");
    capabilities.getDynamicObject()->setProperty ("minItems", 1);
    capabilities.getDynamicObject()->setProperty ("uniqueItems", true);
    capabilities.getDynamicObject()->setProperty ("items", capabilityItem);

    auto skinApi = object();
    skinApi.getDynamicObject()->setProperty ("const", 1);
    auto extends = object();
    extends.getDynamicObject()->setProperty ("const", "builtin.foundation");

    auto properties = object();
    properties.getDynamicObject()->setProperty ("skinApi", skinApi);
    properties.getDynamicObject()->setProperty (
        "id", stringSchema (1, 128, R"(^[a-z0-9]+([.-][a-z0-9]+)+$)"));
    properties.getDynamicObject()->setProperty ("name", stringSchema (1, 80));
    properties.getDynamicObject()->setProperty ("version", reference ("#/$defs/semver"));
    properties.getDynamicObject()->setProperty ("author", author);
    properties.getDynamicObject()->setProperty ("description", stringSchema (0, 500));
    properties.getDynamicObject()->setProperty ("license", stringSchema (1, 80));
    properties.getDynamicObject()->setProperty ("minAppVersion", reference ("#/$defs/semver"));
    properties.getDynamicObject()->setProperty ("extends", extends);
    properties.getDynamicObject()->setProperty ("capabilities", capabilities);
    properties.getDynamicObject()->setProperty ("entrypoints", entrypoints);

    auto definitions = object();
    definitions.getDynamicObject()->setProperty ("semver", semver);
    definitions.getDynamicObject()->setProperty ("path", path);

    auto result = object();
    auto* root = result.getDynamicObject();
    root->setProperty ("$schema", "https://json-schema.org/draft/2020-12/schema");
    root->setProperty ("$id", schemaIdentifier (SkinSchemaDocument::manifest));
    root->setProperty ("x-generatedBy", MoonwakeIdentity::skinSchemaGenerator);
    root->setProperty ("x-skinApi", MoonwakeIdentity::skinApiVersion);
    root->setProperty ("title", schemaTitle ("Manifest"));
    root->setProperty ("type", "object");
    root->setProperty ("additionalProperties", false);
    root->setProperty ("required", array ({ "skinApi", "id", "name", "version", "author",
                                             "minAppVersion", "capabilities", "entrypoints" }));
    root->setProperty ("properties", properties);
    root->setProperty ("$defs", definitions);
    return result;
}

juce::var tokensSchema()
{
    auto referenceProperties = object();
    referenceProperties.getDynamicObject()->setProperty (
        "$ref", stringSchema (1, 256,
            R"(^(colors|metrics|durations|fonts|gradients|strings)\.[A-Za-z0-9_.-]+$)"));
    auto tokenReference = closedObject (referenceProperties, { "$ref" });

    auto colour = stringSchema (0, -1, R"(^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$)");
    auto colourOrReference = oneOf ({ reference ("#/$defs/color"),
                                      reference ("#/$defs/reference") });
    auto numberOrReference = oneOf ({ typedSchema ("number"),
                                      reference ("#/$defs/reference") });
    auto path = stringSchema (1, 512,
        R"(^assets/(?!.*//)(?!.*(?:^|/)\.{1,2}(?:/|$))(?!.*\\)[^\u0000]+$)");

    auto familyItem = stringSchema (1, 128);
    auto families = typedSchema ("array");
    families.getDynamicObject()->setProperty ("minItems", 1);
    families.getDynamicObject()->setProperty ("maxItems", 8);
    families.getDynamicObject()->setProperty ("items", familyItem);
    auto weight = boundedNumber ("integer", 100, 900);
    weight.getDynamicObject()->setProperty ("multipleOf", 100);
    auto tracking = boundedNumber ("number", -10, 50);
    tracking.getDynamicObject()->setProperty ("default", 0);
    auto italic = typedSchema ("boolean");
    italic.getDynamicObject()->setProperty ("default", false);
    auto fontProperties = object();
    fontProperties.getDynamicObject()->setProperty ("source", reference ("#/$defs/path"));
    fontProperties.getDynamicObject()->setProperty ("families", families);
    fontProperties.getDynamicObject()->setProperty ("weight", weight);
    fontProperties.getDynamicObject()->setProperty ("italic", italic);
    fontProperties.getDynamicObject()->setProperty ("tracking", tracking);
    auto font = closedObject (fontProperties, { "families", "weight" });

    auto stopProperties = object();
    stopProperties.getDynamicObject()->setProperty ("position",
                                                     boundedNumber ("number", 0, 1));
    stopProperties.getDynamicObject()->setProperty ("color",
                                                     reference ("#/$defs/colorOrReference"));
    auto gradientStop = closedObject (stopProperties, { "position", "color" });

    auto gradientType = object();
    gradientType.getDynamicObject()->setProperty ("enum", array ({ "linear", "radial" }));
    auto angle = boundedNumber ("number", -3600, 3600);
    auto coordinate = boundedNumber ("number", 0, 1);
    auto radius = typedSchema ("number");
    radius.getDynamicObject()->setProperty ("exclusiveMinimum", 0);
    radius.getDynamicObject()->setProperty ("maximum", 2);
    auto stops = typedSchema ("array");
    stops.getDynamicObject()->setProperty ("minItems", 2);
    stops.getDynamicObject()->setProperty ("maxItems", 16);
    stops.getDynamicObject()->setProperty ("items", reference ("#/$defs/gradientStop"));
    auto gradientProperties = object();
    gradientProperties.getDynamicObject()->setProperty ("type", gradientType);
    gradientProperties.getDynamicObject()->setProperty ("angle", angle);
    gradientProperties.getDynamicObject()->setProperty ("centerX", coordinate);
    gradientProperties.getDynamicObject()->setProperty ("centerY", coordinate.clone());
    gradientProperties.getDynamicObject()->setProperty ("radius", radius);
    gradientProperties.getDynamicObject()->setProperty ("stops", stops);
    auto gradient = closedObject (gradientProperties, { "type", "stops" });

    const auto conditional = [] (const juce::String& type,
                                 std::initializer_list<juce::var> required)
    {
        auto typeConditionProperties = object();
        auto exactType = object();
        exactType.getDynamicObject()->setProperty ("const", type);
        typeConditionProperties.getDynamicObject()->setProperty ("type", exactType);
        auto condition = object();
        condition.getDynamicObject()->setProperty ("properties", typeConditionProperties);
        auto thenSchema = object();
        thenSchema.getDynamicObject()->setProperty ("required", array (required));
        auto branch = object();
        branch.getDynamicObject()->setProperty ("if", condition);
        branch.getDynamicObject()->setProperty ("then", thenSchema);
        return branch;
    };
    gradient.getDynamicObject()->setProperty (
        "allOf", array ({ conditional ("linear", { "angle" }),
                           conditional ("radial", { "centerX", "centerY", "radius" }) }));

    auto duration = boundedNumber ("integer", 0, 10000);
    auto fontOrReference = oneOf ({ reference ("#/$defs/font"),
                                    reference ("#/$defs/reference") });
    auto gradientOrReference = oneOf ({ reference ("#/$defs/gradient"),
                                        reference ("#/$defs/reference") });
    auto stringOrReference = oneOf ({ stringSchema (0, 256),
                                      reference ("#/$defs/reference") });

    auto properties = object();
    properties.getDynamicObject()->setProperty (
        "colors", dynamicObject (reference ("#/$defs/colorOrReference")));
    properties.getDynamicObject()->setProperty (
        "metrics", dynamicObject (reference ("#/$defs/numberOrReference")));
    properties.getDynamicObject()->setProperty (
        "durations", dynamicObject (oneOf ({ duration, reference ("#/$defs/reference") })));
    properties.getDynamicObject()->setProperty ("fonts", dynamicObject (fontOrReference));
    properties.getDynamicObject()->setProperty ("gradients", dynamicObject (gradientOrReference));
    properties.getDynamicObject()->setProperty ("strings", dynamicObject (stringOrReference));

    auto definitions = object();
    definitions.getDynamicObject()->setProperty ("reference", tokenReference);
    definitions.getDynamicObject()->setProperty ("color", colour);
    definitions.getDynamicObject()->setProperty ("colorOrReference", colourOrReference);
    definitions.getDynamicObject()->setProperty ("numberOrReference", numberOrReference);
    definitions.getDynamicObject()->setProperty ("path", path);
    definitions.getDynamicObject()->setProperty ("font", font);
    definitions.getDynamicObject()->setProperty ("gradientStop", gradientStop);
    definitions.getDynamicObject()->setProperty ("gradient", gradient);

    auto result = closedObject (properties);
    auto* root = result.getDynamicObject();
    root->setProperty ("$schema", "https://json-schema.org/draft/2020-12/schema");
    root->setProperty ("$id", schemaIdentifier (SkinSchemaDocument::tokens));
    root->setProperty ("x-generatedBy", MoonwakeIdentity::skinSchemaGenerator);
    root->setProperty ("x-skinApi", MoonwakeIdentity::skinApiVersion);
    root->setProperty ("title", schemaTitle ("Tokens"));
    root->setProperty ("$defs", definitions);
    return result;
}

juce::var layoutSchema()
{
    auto finiteNumber = boundedNumber ("number", -100000, 100000);
    auto positiveLength = boundedNumber ("number", 0, 100000);
    auto automatic = enumSchema ({ "auto" });
    automatic.getDynamicObject()->setProperty ("type", "string");
    auto percent = stringSchema (0, -1,
        R"(^(?:100(?:\.0+)?|(?:[0-9]|[1-9][0-9])(?:\.[0-9]+)?)%$)");
    auto length = oneOf ({ positiveLength, automatic, percent });
    auto fraction = stringSchema (0, -1,
        R"(^(?:[1-9][0-9]*(?:\.[0-9]+)?|0\.[0-9]*[1-9][0-9]*)fr$)");
    auto trackLength = oneOf ({ reference ("#/$defs/length"), fraction });

    auto insetProperties = object();
    for (const auto* side : { "top", "right", "bottom", "left" })
        insetProperties.getDynamicObject()->setProperty (side, positiveLength.clone());
    auto inset = oneOf ({ positiveLength.clone(), closedObject (insetProperties) });

    auto anchorProperties = object();
    for (const auto* side : { "top", "right", "bottom", "left" })
        anchorProperties.getDynamicObject()->setProperty (side, reference ("#/$defs/length"));
    anchorProperties.getDynamicObject()->setProperty ("centerX", finiteNumber.clone());
    anchorProperties.getDynamicObject()->setProperty ("centerY", finiteNumber.clone());
    auto anchors = closedObject (anchorProperties);

    auto common = object();
    common.getDynamicObject()->setProperty (
        "id", stringSchema (1, 128, R"(^[A-Za-z][A-Za-z0-9_.-]*$)"));
    common.getDynamicObject()->setProperty (
        "type", enumSchema ({ "row", "column", "grid", "stack", "scroll", "component", "visual" }));
    common.getDynamicObject()->setProperty ("width", reference ("#/$defs/length"));
    common.getDynamicObject()->setProperty ("height", reference ("#/$defs/length"));
    for (const auto* field : { "minWidth", "maxWidth", "minHeight", "maxHeight" })
        common.getDynamicObject()->setProperty (field, positiveLength.clone());
    common.getDynamicObject()->setProperty ("margin", reference ("#/$defs/inset"));
    common.getDynamicObject()->setProperty ("padding", reference ("#/$defs/inset"));
    common.getDynamicObject()->setProperty ("grow", boundedNumber ("number", 0, 1000));
    common.getDynamicObject()->setProperty ("shrink", boundedNumber ("number", 0, 1000));
    common.getDynamicObject()->setProperty (
        "alignSelf", enumSchema ({ "auto", "start", "center", "end", "stretch" }));
    auto aspect = typedSchema ("number");
    aspect.getDynamicObject()->setProperty ("exclusiveMinimum", 0);
    aspect.getDynamicObject()->setProperty ("maximum", 1000);
    common.getDynamicObject()->setProperty ("aspectRatio", aspect);
    common.getDynamicObject()->setProperty ("opacity", boundedNumber ("number", 0, 1));
    common.getDynamicObject()->setProperty ("clip", typedSchema ("boolean"));
    common.getDynamicObject()->setProperty ("zIndex", boundedNumber ("integer", -1000, 1000));
    common.getDynamicObject()->setProperty ("visible", typedSchema ("boolean"));
    common.getDynamicObject()->setProperty (
        "style", stringSchema (1, 128,
            R"(^[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z][A-Za-z0-9_.-]*$)"));
    common.getDynamicObject()->setProperty ("anchors", reference ("#/$defs/anchors"));
    for (const auto* field : { "column", "row", "columnSpan", "rowSpan" })
        common.getDynamicObject()->setProperty (field, boundedNumber ("integer", 1, 128));

    const auto nodeSchema = [&] (const juce::String& type,
                                 const juce::var& additions,
                                 std::initializer_list<juce::var> extraRequired)
    {
        auto properties = common.clone();
        properties.getDynamicObject()->setProperty ("type", [&]
        {
            auto exact = object();
            exact.getDynamicObject()->setProperty ("const", type);
            return exact;
        }());
        if (additions.isObject())
        {
            const auto& added = additions.getDynamicObject()->getProperties();
            for (int i = 0; i < added.size(); ++i)
                properties.getDynamicObject()->setProperty (added.getName (i), added.getValueAt (i));
        }
        juce::Array<juce::var> required { "id", "type" };
        for (const auto& field : extraRequired) required.add (field);
        auto result = closedObject (properties);
        result.getDynamicObject()->setProperty ("required", juce::var (std::move (required)));
        return result;
    };

    auto flexAdditions = object();
    flexAdditions.getDynamicObject()->setProperty ("gap", boundedNumber ("number", 0, 10000));
    flexAdditions.getDynamicObject()->setProperty (
        "align", enumSchema ({ "start", "center", "end", "stretch" }));
    flexAdditions.getDynamicObject()->setProperty (
        "justify", enumSchema ({ "start", "center", "end", "spaceBetween", "spaceAround" }));
    flexAdditions.getDynamicObject()->setProperty (
        "children", arraySchema (reference ("#/$defs/node"), -1, 1024));
    auto row = nodeSchema ("row", flexAdditions, { "children" });
    auto column = nodeSchema ("column", flexAdditions, { "children" });

    auto gridAdditions = object();
    gridAdditions.getDynamicObject()->setProperty (
        "columns", arraySchema (reference ("#/$defs/trackLength"), 1, 32));
    gridAdditions.getDynamicObject()->setProperty (
        "rows", arraySchema (reference ("#/$defs/trackLength"), 1, 32));
    gridAdditions.getDynamicObject()->setProperty ("columnGap",
                                                    boundedNumber ("number", 0, 10000));
    gridAdditions.getDynamicObject()->setProperty ("rowGap",
                                                    boundedNumber ("number", 0, 10000));
    gridAdditions.getDynamicObject()->setProperty (
        "children", arraySchema (reference ("#/$defs/node"), -1, 1024));
    auto grid = nodeSchema ("grid", gridAdditions, { "columns", "rows", "children" });

    auto stackAdditions = object();
    stackAdditions.getDynamicObject()->setProperty (
        "children", arraySchema (reference ("#/$defs/node"), -1, 1024));
    auto stack = nodeSchema ("stack", stackAdditions, { "children" });

    auto scrollAdditions = object();
    scrollAdditions.getDynamicObject()->setProperty (
        "direction", enumSchema ({ "vertical", "horizontal" }));
    scrollAdditions.getDynamicObject()->setProperty ("child", reference ("#/$defs/node"));
    auto scroll = nodeSchema ("scroll", scrollAdditions, { "direction", "child" });

    juce::Array<juce::var> componentIds;
    for (const auto& descriptor : PlayerComponentCatalog::descriptors())
        componentIds.add (descriptor.id);
    auto componentId = object();
    componentId.getDynamicObject()->setProperty ("enum", juce::var (std::move (componentIds)));
    auto componentAdditions = object();
    componentAdditions.getDynamicObject()->setProperty ("component",
                                                         reference ("#/$defs/componentId"));
    componentAdditions.getDynamicObject()->setProperty ("focusOrder",
                                                         boundedNumber ("integer", 1, 100000));
    std::vector<juce::String> presentations;
    for (const auto& descriptor : PlayerComponentCatalog::descriptors())
        for (const auto& presentation : descriptor.presentations)
            if (std::find (presentations.begin(), presentations.end(), presentation)
                == presentations.end())
                presentations.push_back (presentation);
    componentAdditions.getDynamicObject()->setProperty (
        "presentation", enumSchema (presentations));
    auto component = nodeSchema ("component", componentAdditions, { "component" });

    auto visualAdditions = object();
    auto pointerEvents = object();
    pointerEvents.getDynamicObject()->setProperty ("const", "none");
    visualAdditions.getDynamicObject()->setProperty ("pointerEvents", pointerEvents);
    auto visual = nodeSchema ("visual", visualAdditions, { "style" });

    auto node = oneOf ({ reference ("#/$defs/row"), reference ("#/$defs/column"),
                         reference ("#/$defs/grid"), reference ("#/$defs/stack"),
                         reference ("#/$defs/scroll"), reference ("#/$defs/component"),
                         reference ("#/$defs/visual") });

    auto conditionProperties = object();
    for (const auto* field : { "minWidth", "maxWidth", "minHeight", "maxHeight" })
        conditionProperties.getDynamicObject()->setProperty (field, positiveLength.clone());
    auto scale = typedSchema ("number");
    scale.getDynamicObject()->setProperty ("exclusiveMinimum", 0);
    scale.getDynamicObject()->setProperty ("maximum", 8);
    conditionProperties.getDynamicObject()->setProperty ("minScaleFactor", scale);
    conditionProperties.getDynamicObject()->setProperty ("maxScaleFactor", scale.clone());
    conditionProperties.getDynamicObject()->setProperty ("workspace", enumSchema ({ "studio", "listening" }));
    auto condition = closedObject (conditionProperties);
    condition.getDynamicObject()->setProperty ("minProperties", 1);
    auto breakpointProperties = object();
    auto ancillaryProperties = object();
    for (const auto& name : skinRenderTargetNames())
        if (const auto target = skinRenderTargetFromName (name);
            target && skinRenderIsAncillaryTarget (*target))
            ancillaryProperties.getDynamicObject()->setProperty (
                name, stringSchema (1, 128, R"(^[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z][A-Za-z0-9_.-]*$)"));
    const auto ancillaryStyles = closedObject (ancillaryProperties);
    breakpointProperties.getDynamicObject()->setProperty ("ancillaryStyles", ancillaryStyles);
    breakpointProperties.getDynamicObject()->setProperty ("when", condition);
    breakpointProperties.getDynamicObject()->setProperty ("root", reference ("#/$defs/node"));
    auto breakpoint = closedObject (breakpointProperties, { "when", "root" });

    auto properties = object();
    auto apiVersion = object();
    apiVersion.getDynamicObject()->setProperty ("const", 1);
    properties.getDynamicObject()->setProperty ("ancillaryStyles", ancillaryStyles);
    properties.getDynamicObject()->setProperty ("apiVersion", apiVersion);
    properties.getDynamicObject()->setProperty ("root", reference ("#/$defs/node"));
    properties.getDynamicObject()->setProperty (
        "breakpoints", arraySchema (reference ("#/$defs/breakpoint"), -1, 16));

    auto definitions = object();
    definitions.getDynamicObject()->setProperty ("finiteNumber", finiteNumber);
    definitions.getDynamicObject()->setProperty ("length", length);
    definitions.getDynamicObject()->setProperty ("trackLength", trackLength);
    definitions.getDynamicObject()->setProperty ("inset", inset);
    definitions.getDynamicObject()->setProperty ("anchors", anchors);
    definitions.getDynamicObject()->setProperty ("row", row);
    definitions.getDynamicObject()->setProperty ("column", column);
    definitions.getDynamicObject()->setProperty ("grid", grid);
    definitions.getDynamicObject()->setProperty ("stack", stack);
    definitions.getDynamicObject()->setProperty ("scroll", scroll);
    definitions.getDynamicObject()->setProperty ("component", component);
    definitions.getDynamicObject()->setProperty ("visual", visual);
    definitions.getDynamicObject()->setProperty ("node", node);
    definitions.getDynamicObject()->setProperty ("breakpoint", breakpoint);
    definitions.getDynamicObject()->setProperty ("componentId", componentId);

    auto result = closedObject (properties, { "apiVersion", "root" });
    auto* root = result.getDynamicObject();
    root->setProperty ("$schema", "https://json-schema.org/draft/2020-12/schema");
    root->setProperty ("$id", schemaIdentifier (SkinSchemaDocument::layout));
    root->setProperty ("x-generatedBy", MoonwakeIdentity::skinSchemaGenerator);
    root->setProperty ("x-skinApi", MoonwakeIdentity::skinApiVersion);
    root->setProperty ("title", schemaTitle ("Layout"));
    root->setProperty ("$defs", definitions);
    return result;
}

juce::var componentIndexSchema()
{
    auto path = stringSchema (1, 512,
        R"(^(?!/)(?!.*//)(?!.*(?:^|/)\.{1,2}(?:/|$))(?!.*\\)[^\u0000]+\.json$)");
    auto styleReference = stringSchema (1, 128,
        R"(^[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z][A-Za-z0-9_.-]*$)");

    auto slots = dynamicObject (reference ("#/$defs/styleReference"));
    slots.getDynamicObject()->setProperty ("maxProperties", 64);
    slots.getDynamicObject()->setProperty (
        "propertyNames", stringSchema (1, 128,
            R"(^[a-z][A-Za-z0-9]*(\.[a-z][A-Za-z0-9]*)+$)"));
    auto compositeProperties = object();
    compositeProperties.getDynamicObject()->setProperty ("renderer", reference ("#/$defs/path"));
    compositeProperties.getDynamicObject()->setProperty ("slots", slots);
    auto composite = closedObject (compositeProperties, { "renderer" });
    auto styleEntry = oneOf ({ reference ("#/$defs/path"), composite });

    auto styles = dynamicObject (reference ("#/$defs/styleEntry"));
    styles.getDynamicObject()->setProperty ("minProperties", 1);
    styles.getDynamicObject()->setProperty (
        "propertyNames", stringSchema (1, 80, R"(^[A-Za-z][A-Za-z0-9_.-]*$)"));

    auto definitions = object();
    definitions.getDynamicObject()->setProperty ("path", path);
    definitions.getDynamicObject()->setProperty ("styleReference", styleReference);
    definitions.getDynamicObject()->setProperty ("styleEntry", styleEntry);

    auto result = dynamicObject (styles);
    auto* root = result.getDynamicObject();
    root->setProperty ("$schema", "https://json-schema.org/draft/2020-12/schema");
    root->setProperty ("$id", schemaIdentifier (SkinSchemaDocument::componentIndex));
    root->setProperty ("x-generatedBy", MoonwakeIdentity::skinSchemaGenerator);
    root->setProperty ("x-skinApi", MoonwakeIdentity::skinApiVersion);
    root->setProperty ("title", schemaTitle ("Renderer Index"));
    root->setProperty ("minProperties", 1);
    root->setProperty ("propertyNames", enumSchema (skinRenderTargetNames()));
    root->setProperty ("$defs", definitions);
    return result;
}

juce::var renderSchema()
{
    const auto propertyEnum = [] (SkinPrimitive primitive, const char* path)
    {
        const auto* descriptor = SkinSchemaRegistry::renderPropertyDescriptor (primitive, path);
        jassert (descriptor != nullptr && ! descriptor->enumValues.empty());
        return enumSchema (descriptor != nullptr ? descriptor->enumValues
                                                  : std::vector<juce::String> {});
    };
    auto referenceProperties = object();
    referenceProperties.getDynamicObject()->setProperty (
        "$ref", stringSchema (1, 256,
            R"(^(colors|metrics|durations|fonts|gradients|strings)\.[A-Za-z0-9_.-]+$)"));
    auto tokenReference = closedObject (referenceProperties, { "$ref" });
    auto contextProperties = object();
    contextProperties.getDynamicObject()->setProperty (
        "$context", stringSchema (1, 256, R"(^[A-Za-z][A-Za-z0-9_.\[\]-]*$)"));
    auto context = closedObject (contextProperties, { "$context" });
    auto expressionProperties = object();
    expressionProperties.getDynamicObject()->setProperty ("$expr", stringSchema (1, 2048));
    auto expression = closedObject (expressionProperties, { "$expr" });
    auto dynamicValue = oneOf ({ typedSchema ("number"), typedSchema ("boolean"),
                                 stringSchema (0, 4096), reference ("#/$defs/reference"),
                                 reference ("#/$defs/context"),
                                 reference ("#/$defs/expression") });

    auto insetProperties = object();
    for (const auto* side : { "top", "right", "bottom", "left" })
        insetProperties.getDynamicObject()->setProperty (side, reference ("#/$defs/dynamicValue"));
    auto inset = oneOf ({ reference ("#/$defs/dynamicValue"), closedObject (insetProperties) });

    auto anchoredFrameProperties = object();
    anchoredFrameProperties.getDynamicObject()->setProperty (
        "anchor", propertyEnum (SkinPrimitive::rectangle, "frame.anchor"));
    anchoredFrameProperties.getDynamicObject()->setProperty ("inset", reference ("#/$defs/inset"));
    auto anchoredFrame = closedObject (anchoredFrameProperties, { "anchor" });
    auto explicitFrameProperties = object();
    for (const auto* field : { "x", "y", "width", "height" })
        explicitFrameProperties.getDynamicObject()->setProperty (
            field, reference ("#/$defs/dynamicValue"));
    auto explicitFrame = closedObject (explicitFrameProperties,
                                       { "x", "y", "width", "height" });
    auto frame = oneOf ({ anchoredFrame, explicitFrame });

    auto pointProperties = object();
    pointProperties.getDynamicObject()->setProperty ("x", reference ("#/$defs/dynamicValue"));
    pointProperties.getDynamicObject()->setProperty ("y", reference ("#/$defs/dynamicValue"));
    auto point = closedObject (pointProperties, { "x", "y" });

    auto strokeProperties = object();
    strokeProperties.getDynamicObject()->setProperty ("color", reference ("#/$defs/dynamicValue"));
    strokeProperties.getDynamicObject()->setProperty ("width", reference ("#/$defs/dynamicValue"));
    strokeProperties.getDynamicObject()->setProperty (
        "placement", propertyEnum (SkinPrimitive::rectangle, "stroke.placement"));
    strokeProperties.getDynamicObject()->setProperty (
        "cap", propertyEnum (SkinPrimitive::rectangle, "stroke.cap"));
    auto stroke = closedObject (strokeProperties, { "color", "width" });

    auto shadowProperties = object();
    for (const auto* field : { "color", "radius", "offsetX", "offsetY" })
        shadowProperties.getDynamicObject()->setProperty (
            field, reference ("#/$defs/dynamicValue"));
    auto shadow = closedObject (shadowProperties, { "color", "radius", "offsetX", "offsetY" });

    auto transformProperties = object();
    for (const auto* field : { "translateX", "translateY", "scaleX", "scaleY",
                              "rotation", "pivotX", "pivotY" })
        transformProperties.getDynamicObject()->setProperty (
            field, reference ("#/$defs/dynamicValue"));
    auto transform = closedObject (transformProperties);

    auto commonProperties = object();
    commonProperties.getDynamicObject()->setProperty (
        "id", stringSchema (1, 128, R"(^[A-Za-z][A-Za-z0-9_.-]*$)"));
    commonProperties.getDynamicObject()->setProperty ("type", enumSchema (skinPrimitiveNames()));
    commonProperties.getDynamicObject()->setProperty ("visible", reference ("#/$defs/dynamicValue"));
    commonProperties.getDynamicObject()->setProperty ("opacity", reference ("#/$defs/dynamicValue"));
    commonProperties.getDynamicObject()->setProperty ("transform", reference ("#/$defs/transform"));
    commonProperties.getDynamicObject()->setProperty ("clip", typedSchema ("boolean"));
    commonProperties.getDynamicObject()->setProperty ("shadow", reference ("#/$defs/shadow"));

    const auto makeLayer = [&] (const juce::String& type, const juce::var& additions,
                                std::initializer_list<juce::var> required)
    {
        auto properties = commonProperties.clone();
        auto exactType = object();
        exactType.getDynamicObject()->setProperty ("const", type);
        properties.getDynamicObject()->setProperty ("type", exactType);
        if (additions.isObject())
        {
            const auto& values = additions.getDynamicObject()->getProperties();
            for (int i = 0; i < values.size(); ++i)
                properties.getDynamicObject()->setProperty (values.getName (i),
                                                             values.getValueAt (i));
        }
        juce::Array<juce::var> requiredFields { "id", "type" };
        for (const auto& field : required) requiredFields.add (field);
        auto result = closedObject (properties);
        result.getDynamicObject()->setProperty ("required", juce::var (std::move (requiredFields)));
        return result;
    };

    auto shapeProperties = object();
    shapeProperties.getDynamicObject()->setProperty ("frame", reference ("#/$defs/frame"));
    shapeProperties.getDynamicObject()->setProperty ("fill", reference ("#/$defs/dynamicValue"));
    shapeProperties.getDynamicObject()->setProperty ("stroke", reference ("#/$defs/stroke"));
    auto rect = makeLayer ("rect", shapeProperties, { "frame" });
    auto roundedProperties = shapeProperties.clone();
    roundedProperties.getDynamicObject()->setProperty ("radius", reference ("#/$defs/dynamicValue"));
    auto roundedRect = makeLayer ("roundedRect", roundedProperties, { "frame", "radius" });
    auto ellipse = makeLayer ("ellipse", shapeProperties, { "frame" });

    auto lineProperties = object();
    lineProperties.getDynamicObject()->setProperty ("start", reference ("#/$defs/point"));
    lineProperties.getDynamicObject()->setProperty ("end", reference ("#/$defs/point"));
    lineProperties.getDynamicObject()->setProperty ("color", reference ("#/$defs/dynamicValue"));
    lineProperties.getDynamicObject()->setProperty ("width", reference ("#/$defs/dynamicValue"));
    lineProperties.getDynamicObject()->setProperty (
        "cap", propertyEnum (SkinPrimitive::line, "cap"));
    auto line = makeLayer ("line", lineProperties, { "start", "end", "color", "width" });

    auto arcProperties = object();
    arcProperties.getDynamicObject()->setProperty ("center", reference ("#/$defs/point"));
    for (const auto* field : { "radiusX", "radiusY", "startAngle", "endAngle" })
        arcProperties.getDynamicObject()->setProperty (field, reference ("#/$defs/dynamicValue"));
    arcProperties.getDynamicObject()->setProperty ("stroke", reference ("#/$defs/stroke"));
    auto arc = makeLayer ("arc", arcProperties,
                          { "center", "radiusX", "radiusY", "startAngle", "endAngle", "stroke" });

    auto pathProperties = object();
    const auto* pathDataDescriptor = SkinSchemaRegistry::renderPropertyDescriptor (
        SkinPrimitive::path, "data");
    jassert (pathDataDescriptor != nullptr);
    pathProperties.getDynamicObject()->setProperty (
        "data", oneOf ({ stringSchema (1, pathDataDescriptor != nullptr
                                               ? pathDataDescriptor->maximumUtf8Bytes : 0),
                          reference ("#/$defs/reference"),
                          reference ("#/$defs/context") }));
    pathProperties.getDynamicObject()->setProperty ("fill", reference ("#/$defs/dynamicValue"));
    pathProperties.getDynamicObject()->setProperty ("stroke", reference ("#/$defs/stroke"));
    auto path = makeLayer ("path", pathProperties, { "data" });

    auto textProperties = object();
    textProperties.getDynamicObject()->setProperty ("frame", reference ("#/$defs/frame"));
    for (const auto* field : { "text", "font", "fontSize", "color" })
        textProperties.getDynamicObject()->setProperty (field, reference ("#/$defs/dynamicValue"));
    textProperties.getDynamicObject()->setProperty (
        "align", oneOf ({ propertyEnum (SkinPrimitive::text, "align"),
                           reference ("#/$defs/context") }));
    const auto* maxLinesDescriptor = SkinSchemaRegistry::renderPropertyDescriptor (
        SkinPrimitive::text, "maxLines");
    jassert (maxLinesDescriptor != nullptr && maxLinesDescriptor->minimum
             && maxLinesDescriptor->maximum);
    textProperties.getDynamicObject()->setProperty (
        "maxLines", boundedNumber ("integer",
            maxLinesDescriptor != nullptr && maxLinesDescriptor->minimum
                ? *maxLinesDescriptor->minimum : 0.0,
            maxLinesDescriptor != nullptr && maxLinesDescriptor->maximum
                ? *maxLinesDescriptor->maximum : 0.0));
    textProperties.getDynamicObject()->setProperty (
        "overflow", propertyEnum (SkinPrimitive::text, "overflow"));
    auto text = makeLayer ("text", textProperties,
                           { "frame", "text", "font", "fontSize", "color" });

    auto assetProperties = object();
    assetProperties.getDynamicObject()->setProperty ("frame", reference ("#/$defs/frame"));
    const auto* sourceDescriptor = SkinSchemaRegistry::renderPropertyDescriptor (
        SkinPrimitive::image, "source");
    jassert (sourceDescriptor != nullptr);
    assetProperties.getDynamicObject()->setProperty (
        "source", oneOf ({
            stringSchema (1, sourceDescriptor != nullptr
                                 ? sourceDescriptor->maximumUtf8Bytes : 0,
                R"(^assets/(?!.*//)(?!.*(?:^|/)\.{1,2}(?:/|$))(?!.*\\)[^\u0000]+$)"),
            reference ("#/$defs/reference")
        }));
    assetProperties.getDynamicObject()->setProperty (
        "fit", propertyEnum (SkinPrimitive::image, "fit"));
    assetProperties.getDynamicObject()->setProperty ("tint", reference ("#/$defs/dynamicValue"));
    auto image = makeLayer ("image", assetProperties, { "frame", "source" });
    auto svg = makeLayer ("svg", assetProperties, { "frame", "source" });

    auto groupProperties = object();
    groupProperties.getDynamicObject()->setProperty (
        "children", arraySchema (reference ("#/$defs/layer"), -1,
                                 SkinRenderLimits::maximumChildrenPerGroup));
    auto group = makeLayer ("group", groupProperties, { "children" });
    auto nonRepeatGroupProperties = object();
    nonRepeatGroupProperties.getDynamicObject()->setProperty (
        "children", arraySchema (reference ("#/$defs/nonRepeatLayer"), -1,
                                 SkinRenderLimits::maximumChildrenPerGroup));
    auto nonRepeatGroup = makeLayer ("group", nonRepeatGroupProperties, { "children" });

    auto repeatProperties = object();
    std::vector<juce::String> repeatSources;
    int largestRepeat = 0;
    for (const auto& source : skinRepeatSourceDescriptors())
    {
        repeatSources.push_back (source.name);
        largestRepeat = std::max (largestRepeat, source.maximumItems);
    }
    repeatProperties.getDynamicObject()->setProperty (
        "source", enumSchema (repeatSources));
    repeatProperties.getDynamicObject()->setProperty (
        "limit", boundedNumber ("integer", 1, largestRepeat));
    // Nesting is a semantic rule so the compiler can emit stable RENDER008 rather than
    // collapsing it into a generic schema-alternative failure.
    repeatProperties.getDynamicObject()->setProperty ("template", reference ("#/$defs/layer"));
    auto repeat = makeLayer ("repeat", repeatProperties, { "source", "limit", "template" });

    auto nonRepeatLayer = oneOf ({ reference ("#/$defs/rect"),
                                   reference ("#/$defs/roundedRect"),
                                   reference ("#/$defs/ellipse"), reference ("#/$defs/line"),
                                   reference ("#/$defs/arc"), reference ("#/$defs/path"),
                                   reference ("#/$defs/text"), reference ("#/$defs/image"),
                                   reference ("#/$defs/svg"),
                                   reference ("#/$defs/nonRepeatGroup") });
    auto layer = oneOf ({ reference ("#/$defs/rect"), reference ("#/$defs/roundedRect"),
                          reference ("#/$defs/ellipse"), reference ("#/$defs/line"),
                          reference ("#/$defs/arc"), reference ("#/$defs/path"),
                          reference ("#/$defs/text"), reference ("#/$defs/image"),
                          reference ("#/$defs/svg"), reference ("#/$defs/group"),
                          reference ("#/$defs/repeat") });

    auto variant = dynamicObject (reference ("#/$defs/dynamicValue"));
    variant.getDynamicObject()->setProperty (
        "propertyNames", stringSchema (1, 256,
            R"(^[A-Za-z][A-Za-z0-9_.-]*\.[A-Za-z][A-Za-z0-9_.-]*$)"));

    auto transitionProperties = object();
    auto duration = oneOf ({ boundedNumber ("integer", 0, 10000),
                             reference ("#/$defs/reference") });
    transitionProperties.getDynamicObject()->setProperty ("durationMs", duration);
    transitionProperties.getDynamicObject()->setProperty ("delayMs", boundedNumber ("integer", 0, 10000));
    transitionProperties.getDynamicObject()->setProperty (
        "easing", enumSchema (skinAnimationEasingNames()));
    auto transitionProperty = stringSchema (1, 256,
        R"(^[A-Za-z][A-Za-z0-9_.-]*\.[A-Za-z][A-Za-z0-9_.-]*$)");
    auto transitionPropertyList = arraySchema (transitionProperty, 1, 128);
    transitionPropertyList.getDynamicObject()->setProperty ("uniqueItems", true);
    transitionProperties.getDynamicObject()->setProperty ("properties", transitionPropertyList);
    transitionProperties.getDynamicObject()->setProperty ("respectReduceMotion", typedSchema ("boolean"));
    auto transition = closedObject (transitionProperties, { "durationMs", "easing", "properties" });
    auto entryProperties = transitionProperties.clone();
    entryProperties.getDynamicObject()->setProperty ("from", reference ("#/$defs/variant"));
    auto reduceMotion = object();
    reduceMotion.getDynamicObject()->setProperty ("const", true);
    entryProperties.getDynamicObject()->setProperty ("respectReduceMotion", reduceMotion);
    auto entryTransition = closedObject (
        entryProperties, { "durationMs", "easing", "properties", "from" });

    auto variantsProperties = object();
    for (const auto& name : skinVariantNamesInMergeOrder())
        variantsProperties.getDynamicObject()->setProperty (name, reference ("#/$defs/variant"));
    auto variants = closedObject (variantsProperties);
    auto transitionsProperties = object();
    for (const auto& name : skinAnimationTriggerNames())
        transitionsProperties.getDynamicObject()->setProperty (
            name, reference (name.endsWith ("Enter") ? "#/$defs/entryTransition"
                                                       : "#/$defs/transition"));
    auto transitions = closedObject (transitionsProperties);

    auto rowLayoutProperties = object();
    for (const auto* field : { "rowHeight", "rowGap", "headerHeight" })
        rowLayoutProperties.getDynamicObject()->setProperty (field, reference ("#/$defs/dynamicValue"));
    rowLayoutProperties.getDynamicObject()->setProperty ("slots", dynamicObject (explicitFrame));
    auto rowBreakpointProperties = rowLayoutProperties.clone();
    auto rowWhenProperties = object();
    for (const auto* field : { "minWidth", "maxWidth" })
        rowWhenProperties.getDynamicObject()->setProperty (field, boundedNumber ("number", 0, 100000));
    auto rowWhen = closedObject (rowWhenProperties);
    rowWhen.getDynamicObject()->setProperty ("minProperties", 1);
    rowBreakpointProperties.getDynamicObject()->setProperty ("when", rowWhen);
    rowLayoutProperties.getDynamicObject()->setProperty (
        "breakpoints", arraySchema (closedObject (rowBreakpointProperties, { "when" }), -1, 16));
    const auto rowLayout = closedObject (rowLayoutProperties, { "rowHeight", "slots" });

    auto properties = object();
    auto apiVersion = object();
    apiVersion.getDynamicObject()->setProperty ("const", 1);
    properties.getDynamicObject()->setProperty ("apiVersion", apiVersion);
    properties.getDynamicObject()->setProperty ("target", enumSchema (skinRenderTargetNames()));
    properties.getDynamicObject()->setProperty (
        "layers", arraySchema (reference ("#/$defs/layer"), -1,
                               SkinRenderLimits::maximumRootLayers));
    properties.getDynamicObject()->setProperty ("variants", variants);
    properties.getDynamicObject()->setProperty ("transitions", transitions);
    properties.getDynamicObject()->setProperty ("rowLayout", rowLayout);
    properties.getDynamicObject()->setProperty (
        "contentClip", stringSchema (1, 128, R"(^[A-Za-z][A-Za-z0-9_.-]*$)"));
    properties.getDynamicObject()->setProperty ("controlStyle", typedSchema ("object"));

    juce::Array<juce::var> controlStyleConditions;
    for (const auto& targetName : skinRenderTargetNames())
    {
        const auto target = skinRenderTargetFromName (targetName);
        auto fields = object();
        juce::Array<juce::var> requiredFields;
        for (const auto& descriptor : skinControlStylePropertyDescriptors (*target))
        {
            const auto name = descriptor.path.upToFirstOccurrenceOf (".", false, false);
            if (fields.hasProperty (name)) continue;
            fields.getDynamicObject()->setProperty (name, descriptor.path.containsChar ('.')
                ? explicitFrame : reference ("#/$defs/dynamicValue"));
            requiredFields.add (name);
        }
        auto styleSchema = closedObject (fields);
        styleSchema.getDynamicObject()->setProperty ("minProperties", 1);
        styleSchema.getDynamicObject()->setProperty ("required", juce::var (requiredFields));
        auto targetValue = object();
        targetValue.getDynamicObject()->setProperty ("const", targetName);
        auto conditionProperties = object();
        conditionProperties.getDynamicObject()->setProperty ("target", targetValue);
        auto condition = object();
        condition.getDynamicObject()->setProperty ("properties", conditionProperties);
        auto consequentProperties = object();
        consequentProperties.getDynamicObject()->setProperty ("controlStyle", styleSchema);
        auto consequent = object();
        consequent.getDynamicObject()->setProperty ("properties", consequentProperties);
        auto branch = object();
        branch.getDynamicObject()->setProperty ("if", condition);
        branch.getDynamicObject()->setProperty ("then", consequent);
        controlStyleConditions.add (branch);
    }

    auto definitions = object();
    definitions.getDynamicObject()->setProperty ("reference", tokenReference);
    definitions.getDynamicObject()->setProperty ("context", context);
    definitions.getDynamicObject()->setProperty ("expression", expression);
    definitions.getDynamicObject()->setProperty ("dynamicValue", dynamicValue);
    definitions.getDynamicObject()->setProperty ("inset", inset);
    definitions.getDynamicObject()->setProperty ("frame", frame);
    definitions.getDynamicObject()->setProperty ("point", point);
    definitions.getDynamicObject()->setProperty ("stroke", stroke);
    definitions.getDynamicObject()->setProperty ("shadow", shadow);
    definitions.getDynamicObject()->setProperty ("transform", transform);
    definitions.getDynamicObject()->setProperty ("rect", rect);
    definitions.getDynamicObject()->setProperty ("roundedRect", roundedRect);
    definitions.getDynamicObject()->setProperty ("ellipse", ellipse);
    definitions.getDynamicObject()->setProperty ("line", line);
    definitions.getDynamicObject()->setProperty ("arc", arc);
    definitions.getDynamicObject()->setProperty ("path", path);
    definitions.getDynamicObject()->setProperty ("text", text);
    definitions.getDynamicObject()->setProperty ("image", image);
    definitions.getDynamicObject()->setProperty ("svg", svg);
    definitions.getDynamicObject()->setProperty ("group", group);
    definitions.getDynamicObject()->setProperty ("nonRepeatGroup", nonRepeatGroup);
    definitions.getDynamicObject()->setProperty ("nonRepeatLayer", nonRepeatLayer);
    definitions.getDynamicObject()->setProperty ("repeat", repeat);
    definitions.getDynamicObject()->setProperty ("layer", layer);
    definitions.getDynamicObject()->setProperty ("variant", variant);
    definitions.getDynamicObject()->setProperty ("transition", transition);
    definitions.getDynamicObject()->setProperty ("entryTransition", entryTransition);

    auto result = closedObject (properties, { "apiVersion", "target", "layers" });
    auto* root = result.getDynamicObject();
    root->setProperty ("$schema", "https://json-schema.org/draft/2020-12/schema");
    root->setProperty ("$id", schemaIdentifier (SkinSchemaDocument::render));
    root->setProperty ("x-generatedBy", MoonwakeIdentity::skinSchemaGenerator);
    root->setProperty ("x-skinApi", MoonwakeIdentity::skinApiVersion);
    root->setProperty ("title", schemaTitle ("Render Definition"));
    root->setProperty ("$defs", definitions);
    root->setProperty ("allOf", juce::var (controlStyleConditions));
    return result;
}

const SkinSchemaDescriptor& descriptorFor (SkinSchemaDocument document)
{
    static const SkinSchemaDescriptor manifest { SkinSchemaDocument::manifest, manifestSchema() };
    static const SkinSchemaDescriptor tokens { SkinSchemaDocument::tokens, tokensSchema() };
    static const SkinSchemaDescriptor componentIndex {
        SkinSchemaDocument::componentIndex, componentIndexSchema() };
    static const SkinSchemaDescriptor layout { SkinSchemaDocument::layout, layoutSchema() };
    static const SkinSchemaDescriptor render { SkinSchemaDocument::render, renderSchema() };

    switch (document)
    {
        case SkinSchemaDocument::manifest: return manifest;
        case SkinSchemaDocument::tokens: return tokens;
        case SkinSchemaDocument::componentIndex: return componentIndex;
        case SkinSchemaDocument::layout: return layout;
        case SkinSchemaDocument::render: return render;
    }

    return manifest;
}

juce::var propertiesForKind (const juce::var& schema, juce::StringRef kindRef)
{
    const juce::String kind (kindRef);
    if (kind == "root")
        return schema.getProperty ("properties", {});

    const auto rootProperties = schema.getProperty ("properties", {});
    if (! rootProperties.isObject())
        return {};

    if (kind == "author")
        return rootProperties.getProperty ("author", {}).getProperty ("properties", {});

    if (kind == "entrypoints")
        return rootProperties.getProperty ("entrypoints", {}).getProperty ("properties", {});

    if (kind == "layouts")
        return rootProperties.getProperty ("entrypoints", {})
            .getProperty ("properties", {})
            .getProperty ("layouts", {})
            .getProperty ("properties", {});

    return {};
}
} // namespace

bool SkinSchemaRegistry::isKnownField (SkinSchemaDocument document,
                                       juce::StringRef objectKind,
                                       juce::StringRef field)
{
    const auto properties = propertiesForKind (descriptorFor (document).schema, objectKind);
    return properties.isObject()
        && properties.getDynamicObject()->hasProperty (juce::Identifier (juce::String (field)));
}

const SkinSchemaDescriptor& SkinSchemaRegistry::descriptor (SkinSchemaDocument document)
{
    return descriptorFor (document);
}

juce::var SkinSchemaRegistry::jsonSchema (SkinSchemaDocument document)
{
    return descriptorFor (document).schema.clone();
}

std::map<SkinSchemaDocument, juce::String> SkinSchemaRegistry::serialisedSchemas()
{
    std::map<SkinSchemaDocument, juce::String> result;
    for (const auto document : { SkinSchemaDocument::manifest, SkinSchemaDocument::tokens,
                                 SkinSchemaDocument::componentIndex, SkinSchemaDocument::layout,
                                 SkinSchemaDocument::render })
        result.emplace (document, juce::JSON::toString (jsonSchema (document), true));
    return result;
}

const juce::StringArray& SkinSchemaRegistry::knownCapabilities()
{
    static const juce::StringArray capabilities { "tokens", "renderers", "layouts",
                                                   "assets", "animations" };
    return capabilities;
}

const SkinRenderPropertyDescriptor* SkinSchemaRegistry::renderPropertyDescriptor (
    SkinPrimitive primitive, juce::StringRef propertyPath)
{
    return skinRenderPropertyDescriptor (primitive, propertyPath);
}
