#include <juce_core/juce_core.h>

#include "skin/SkinManifest.h"
#include "skin/SkinSchemaValidator.h"

namespace
{
juce::var parseJson (const juce::String& json)
{
    return juce::JSON::parse (json);
}

juce::String validManifest()
{
    return R"json({
        "skinApi": 1,
        "id": "com.example.complete",
        "name": "Complete",
        "version": "1.2.3-beta.1+build.7",
        "author": { "name": "Example", "url": "https://example.invalid" },
        "description": "Complete test skin",
        "license": "MIT",
        "minAppVersion": "1.0.0",
        "extends": "builtin.foundation",
        "capabilities": ["tokens", "renderers", "layouts", "animations"],
        "entrypoints": {
            "tokens": "tokens.json",
            "components": "components/index.json",
            "layouts": {
                "studio": "layouts/studio.json",
                "listening": "layouts/listening.json",
                "effects": "layouts/effects.json",
                "settings": "layouts/settings.json"
            }
        }
    })json";
}

class SkinManifestTests final : public juce::UnitTest
{
public:
    SkinManifestTests() : UnitTest ("SkinManifest", "skin") {}

    void runTest() override
    {
        beginTest ("validates and parses a complete manifest");
        SkinValidationReport report;
        const auto validated = SkinSchemaValidator::validate (
            SkinSchemaDocument::manifest, parseJson (validManifest()), "manifest.json", report);
        expect (validated.has_value(), report.toText());
        const auto manifest = validated ? SkinManifest::parse (*validated, report) : std::nullopt;
        expect (manifest.has_value(), report.toText());
        expect (! report.hasErrors(), report.toText());
        if (manifest)
        {
            expectEquals (manifest->id, juce::String ("com.example.complete"));
            expectEquals (manifest->authorName, juce::String ("Example"));
            expectEquals (manifest->layouts.size(), static_cast<size_t> (4));
            expectEquals (manifest->layouts.at ("effects"), juce::String ("layouts/effects.json"));
        }

        beginTest ("optional library routes parse without changing the legacy manifest shape");
        {
            auto raw = parseJson (validManifest());
            auto layouts = raw.getProperty ("entrypoints", {}).getProperty ("layouts", {});
            layouts.getDynamicObject()->setProperty ("library", "layouts/library.json");
            layouts.getDynamicObject()->setProperty ("collection", "layouts/collection.json");
            SkinValidationReport pageReport;
            const auto pageDocument = SkinSchemaValidator::validate (
                SkinSchemaDocument::manifest, raw, "manifest.json", pageReport);
            expect (pageDocument.has_value(), pageReport.toText());
            const auto pages = pageDocument ? SkinManifest::parse (*pageDocument, pageReport) : std::nullopt;
            expect (pages.has_value(), pageReport.toText());
            if (pages)
            {
                expectEquals (static_cast<int> (pages->layouts.size()), 6);
                expectEquals (pages->layouts.at ("library"), juce::String ("layouts/library.json"));
                expectEquals (pages->layouts.at ("collection"), juce::String ("layouts/collection.json"));
            }
        }

        beginTest ("rejects unknown fields and malformed identifiers");
        auto unknown = parseJson (validManifest());
        unknown.getDynamicObject()->setProperty ("capabilites", juce::Array<juce::var>());
        SkinValidationReport unknownReport;
        expect (! SkinSchemaValidator::validate (SkinSchemaDocument::manifest, unknown,
                                                 "manifest.json", unknownReport).has_value());
        expect (unknownReport.hasCode ("SCHEMA001"));

        auto invalidId = parseJson (validManifest());
        invalidId.getDynamicObject()->setProperty ("id", "Example Skin");
        SkinValidationReport idReport;
        expect (! SkinSchemaValidator::validate (SkinSchemaDocument::manifest, invalidId,
                                                 "manifest.json", idReport).has_value());
        expect (idReport.hasCode ("MANIFEST003"), idReport.toText());

        beginTest ("rejects invalid SemVer and unsafe entrypoint paths");
        auto invalidVersion = parseJson (validManifest());
        invalidVersion.getDynamicObject()->setProperty ("version", "1.2");
        SkinValidationReport versionReport;
        expect (! SkinSchemaValidator::validate (SkinSchemaDocument::manifest, invalidVersion,
                                                 "manifest.json", versionReport).has_value());

        auto invalidPath = parseJson (validManifest());
        invalidPath.getDynamicObject()->getProperty ("entrypoints").getDynamicObject()
            ->setProperty ("tokens", "../tokens.json");
        SkinValidationReport pathReport;
        expect (! SkinSchemaValidator::validate (SkinSchemaDocument::manifest, invalidPath,
                                                 "manifest.json", pathReport).has_value());

        beginTest ("schema and parser share registry field metadata");
        const auto schema = SkinSchemaRegistry::jsonSchema (SkinSchemaDocument::manifest);
        const auto properties = schema.getProperty ("properties", {});
        expect (properties.isObject());
        expect (properties.getDynamicObject()->hasProperty ("skinApi"));
        expect (properties.getDynamicObject()->hasProperty ("entrypoints"));
        expect (SkinSchemaRegistry::isKnownField (SkinSchemaDocument::manifest, "root", "entrypoints"));
        expect (! SkinSchemaRegistry::isKnownField (SkinSchemaDocument::manifest, "root", "entries"));
        expectEquals (schema.getProperty ("x-skinApi", {}).toString(), juce::String ("1"));
    }
};

SkinManifestTests skinManifestTests;
} // namespace
