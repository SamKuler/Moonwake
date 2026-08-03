#include "SkinBuiltInPackages.h"

#include <MoonwakeSkinResources.h>

namespace
{
std::optional<std::vector<juce::String>> embeddedPaths (SkinValidationReport& report)
{
    if (MoonwakeSkinResources::namedResourceListSize < 2)
    {
        report.addError ("PKG008", "embedded-paths.txt", {},
                         "Built-in skin resources are incomplete.");
        return std::nullopt;
    }

    int size = 0;
    const auto* data = MoonwakeSkinResources::getNamedResource (
        MoonwakeSkinResources::namedResourceList[0], size);
    if (data == nullptr || size <= 0)
    {
        report.addError ("PKG008", "embedded-paths.txt", {},
                         "Built-in skin resource index is unavailable.");
        return std::nullopt;
    }

    auto lines = juce::StringArray::fromLines (juce::String::fromUTF8 (data, size));
    lines.removeEmptyStrings (true);
    if (lines.size() != MoonwakeSkinResources::namedResourceListSize - 1)
    {
        report.addError ("PKG008", "embedded-paths.txt", {},
                         "Built-in skin resource index does not match its payload.");
        return std::nullopt;
    }
    return std::vector<juce::String> (lines.begin(), lines.end());
}
} // namespace

std::optional<SkinPackage> SkinBuiltInPackages::create (
    Package requested, SkinValidationReport& report)
{
    const auto paths = embeddedPaths (report);
    if (! paths) return std::nullopt;

    const auto prefix = requested == Package::foundation ? juce::String ("Foundation/")
                                                         : juce::String ("Default/");
    std::vector<SkinPackage::EmbeddedFile> files;
    for (int i = 0; i < static_cast<int> (paths->size()); ++i)
    {
        const auto& indexedPath = (*paths)[static_cast<size_t> (i)];
        if (! indexedPath.startsWith (prefix)) continue;

        int size = 0;
        const auto* data = MoonwakeSkinResources::getNamedResource (
            MoonwakeSkinResources::namedResourceList[i + 1], size);
        if (data == nullptr || size < 0)
        {
            report.addError ("PKG008", indexedPath, {},
                             "Built-in skin payload is unavailable.");
            return std::nullopt;
        }
        files.push_back ({ indexedPath.substring (prefix.length()),
                           juce::MemoryBlock (data, static_cast<size_t> (size)) });
    }

    if (files.empty())
    {
        report.addError ("PKG008", prefix, {}, "Built-in skin package is empty.");
        return std::nullopt;
    }
    return SkinPackage::fromEmbedded (
        requested == Package::foundation ? "builtin.foundation" : "builtin.default",
        std::move (files), report);
}
