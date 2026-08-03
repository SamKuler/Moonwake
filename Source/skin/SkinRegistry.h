#pragma once

#include "SkinPackage.h"

#include <optional>
#include <vector>

struct SkinSelection
{
    juce::String id = "builtin.default";
    juce::String version;

    bool operator== (const SkinSelection&) const = default;
};

struct SkinRegistryEntry
{
    SkinSelection selection;
    bool builtIn = false;
    juce::File packageDirectory;
    juce::String sha256;
    juce::String name;
    juce::String author;
};

class SkinRegistry final
{
public:
    bool initialise (const juce::File& installRoot, SkinValidationReport&);
    bool refresh (SkinValidationReport&);

    [[nodiscard]] std::vector<SkinRegistryEntry> entries() const;
    [[nodiscard]] bool contains (const SkinSelection&) const;
    [[nodiscard]] std::optional<SkinSelection> resolve (
        const SkinSelection&) const;
    [[nodiscard]] std::optional<SkinPackage> packageFor (
        const SkinSelection&, const juce::File& privateSnapshotRoot,
        SkinValidationReport&) const;
    [[nodiscard]] const juce::File& root() const noexcept;

    [[nodiscard]] static juce::File defaultInstallRoot();

private:
    juce::File installDirectory;
    std::vector<SkinRegistryEntry> registered;
};
