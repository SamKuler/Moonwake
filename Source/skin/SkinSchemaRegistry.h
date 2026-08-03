#pragma once

#include <juce_core/juce_core.h>

#include <map>

enum class SkinPrimitive;
struct SkinRenderPropertyDescriptor;

enum class SkinSchemaDocument
{
    manifest,
    tokens,
    componentIndex,
    layout,
    render
};

struct SkinSchemaDescriptor
{
    SkinSchemaDocument document = SkinSchemaDocument::manifest;
    juce::var schema;
};

class SkinSchemaRegistry
{
public:
    [[nodiscard]] static bool isKnownField (SkinSchemaDocument,
                                             juce::StringRef objectKind,
                                             juce::StringRef field);
    [[nodiscard]] static const SkinSchemaDescriptor& descriptor (SkinSchemaDocument);
    [[nodiscard]] static juce::var jsonSchema (SkinSchemaDocument);
    [[nodiscard]] static std::map<SkinSchemaDocument, juce::String> serialisedSchemas();
    [[nodiscard]] static const juce::StringArray& knownCapabilities();
    [[nodiscard]] static const SkinRenderPropertyDescriptor* renderPropertyDescriptor (
        SkinPrimitive, juce::StringRef propertyPath);
};
