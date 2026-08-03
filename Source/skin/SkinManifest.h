#pragma once

#include "SkinSchemaValidator.h"

#include <map>
#include <optional>

struct SkinManifest
{
    int skinApi = 0;
    juce::String id;
    juce::String name;
    juce::String version;
    juce::String authorName;
    juce::String authorUrl;
    juce::String description;
    juce::String license;
    juce::String minAppVersion;
    juce::String extends;
    juce::StringArray capabilities;
    juce::String tokensPath;
    juce::String componentsPath;
    std::map<juce::String, juce::String> layouts;

    static std::optional<SkinManifest> parse (const SkinValidatedDocument&,
                                              SkinValidationReport&);
};
