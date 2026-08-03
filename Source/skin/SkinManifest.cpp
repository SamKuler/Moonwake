#include "SkinManifest.h"

std::optional<SkinManifest> SkinManifest::parse (const SkinValidatedDocument& document,
                                                 SkinValidationReport& report)
{
    if (document.kind() != SkinSchemaDocument::manifest || ! document.value().isObject())
    {
        report.addError ("MANIFEST002", "manifest.json", {},
                         "Manifest parser requires a validated manifest document.");
        return std::nullopt;
    }

    const auto& root = document.value();
    SkinManifest result;
    result.skinApi = static_cast<int> (root.getProperty ("skinApi", 0));
    result.id = root.getProperty ("id", {}).toString();
    result.name = root.getProperty ("name", {}).toString();
    result.version = root.getProperty ("version", {}).toString();
    result.description = root.getProperty ("description", {}).toString();
    result.license = root.getProperty ("license", {}).toString();
    result.minAppVersion = root.getProperty ("minAppVersion", {}).toString();
    result.extends = root.getProperty ("extends", {}).toString();

    const auto author = root.getProperty ("author", {});
    result.authorName = author.getProperty ("name", {}).toString();
    result.authorUrl = author.getProperty ("url", {}).toString();

    if (const auto* capabilities = root.getProperty ("capabilities", {}).getArray())
        for (const auto& capability : *capabilities)
            result.capabilities.add (capability.toString());

    const auto entrypoints = root.getProperty ("entrypoints", {});
    result.tokensPath = entrypoints.getProperty ("tokens", {}).toString();
    result.componentsPath = entrypoints.getProperty ("components", {}).toString();
    const auto layouts = entrypoints.getProperty ("layouts", {});
    if (layouts.isObject())
        for (const auto& name : { "studio", "listening", "effects", "settings" })
            if (layouts.getDynamicObject()->hasProperty (name))
                result.layouts.emplace (name, layouts.getProperty (name, {}).toString());

    return result;
}
