#include "skin/SkinTokenStore.h"

#include "skin/SkinSchemaRegistry.h"
#include "skin/SkinSchemaValidator.h"

#include <juce_core/juce_core.h>

#include <concepts>

namespace
{
template <typename Document>
concept TokenCompileInput = requires (const Document& document,
                                      SkinValidationReport& report)
{
    SkinTokenStore::compile (document, nullptr, report);
};

static_assert (TokenCompileInput<SkinValidatedDocument>);
static_assert (! TokenCompileInput<juce::var>);

std::optional<SkinValidatedDocument> validateTokens (juce::StringRef json,
                                                      SkinValidationReport& report)
{
    const auto parsed = juce::JSON::parse (juce::String (json));
    return SkinSchemaValidator::validate (SkinSchemaDocument::tokens, parsed,
                                          "tokens.json", report);
}

std::optional<SkinTokenStore> compileTokens (juce::StringRef json,
                                             const SkinTokenStore* parent,
                                             SkinValidationReport& report,
                                             juce::String origin = {})
{
    const auto document = validateTokens (json, report);
    if (! document)
        return std::nullopt;
    return SkinTokenStore::compile (*document, parent, report, std::move (origin));
}

class SkinTokenStoreTests final : public juce::UnitTest
{
public:
    SkinTokenStoreTests() : UnitTest ("SkinTokenStore") {}

    void runTest() override
    {
        beginTest ("compiles every supported token type and CSS alpha-last colours");
        {
            SkinValidationReport report;
            auto store = compileTokens (R"json({
                "colors": {
                    "opaque": "#123456",
                    "translucent": "#11223344",
                    "alias": { "$ref": "colors.translucent" }
                },
                "metrics": { "space": 12.5 },
                "durations": { "fast": 90 },
                "fonts": {
                    "body": {
                        "source": "assets/fonts/Body.ttf",
                        "families": ["Body", "sans-serif"],
                        "weight": 600,
                        "italic": true,
                        "tracking": 0.25
                    }
                },
                "gradients": {
                    "linear": {
                        "type": "linear",
                        "angle": 135,
                        "stops": [
                            { "position": 0, "color": "#000000" },
                            { "position": 1, "color": { "$ref": "colors.opaque" } }
                        ]
                    },
                    "radial": {
                        "type": "radial",
                        "centerX": 0.25,
                        "centerY": 0.75,
                        "radius": 1.5,
                        "stops": [
                            { "position": 0, "color": "#FFFFFFFF" },
                            { "position": 1, "color": "#00000000" }
                        ]
                    }
                },
                "strings": { "appearance": "light" }
            })json", nullptr, report);
            expect (store.has_value(), report.toText());
            if (store)
            {
                const auto* opaque = store->find<juce::Colour> ("colors.opaque");
                const auto* translucent = store->find<juce::Colour> ("colors.translucent");
                const auto* alias = store->find<juce::Colour> ("colors.alias");
                expect (opaque != nullptr && opaque->getRed() == 0x12
                        && opaque->getGreen() == 0x34 && opaque->getBlue() == 0x56
                        && opaque->getAlpha() == 0xff);
                expect (translucent != nullptr && translucent->getRed() == 0x11
                        && translucent->getGreen() == 0x22 && translucent->getBlue() == 0x33
                        && translucent->getAlpha() == 0x44);
                expect (alias != nullptr && translucent != nullptr && *alias == *translucent);

                const auto* metric = store->find<double> ("metrics.space");
                const auto* duration = store->find<int64_t> ("durations.fast");
                expect (metric != nullptr && std::abs (*metric - 12.5) < 0.0001);
                expect (duration != nullptr && *duration == 90);

                const auto* font = store->find<SkinFontValue> ("fonts.body");
                expect (font != nullptr && font->source == "assets/fonts/Body.ttf"
                        && font->families == juce::StringArray ({ "Body", "sans-serif" })
                        && font->weight == 600 && font->italic
                        && std::abs (font->tracking - 0.25f) < 0.0001f);

                const auto* linear = store->find<SkinGradientValue> ("gradients.linear");
                expect (linear != nullptr && linear->type == SkinGradientValue::Type::linear
                        && linear->angle == 135.0f && linear->stops.size() == 2
                        && linear->stops[0].position == 0.0f
                        && linear->stops[1].position == 1.0f);
                const auto* radial = store->find<SkinGradientValue> ("gradients.radial");
                expect (radial != nullptr && radial->type == SkinGradientValue::Type::radial
                        && radial->centreX == 0.25f && radial->centreY == 0.75f
                        && radial->radius == 1.5f);

                const auto* string = store->find<juce::String> ("strings.appearance");
                expect (string != nullptr && *string == "light");
            }
        }

        beginTest ("child declarations override while parent values remain available");
        {
            SkinValidationReport report;
            auto parent = compileTokens (R"json({
                "colors": { "base": "#102030", "unchanged": "#ABCDEF" },
                "metrics": { "space": 8 }
            })json", nullptr, report);
            expect (parent.has_value(), report.toText());
            auto child = parent ? compileTokens (R"json({
                "colors": {
                    "base": "#405060",
                    "fromParent": { "$ref": "colors.unchanged" }
                }
            })json", &*parent, report) : std::nullopt;
            expect (child.has_value(), report.toText());
            if (child)
            {
                expect (child->contains ("colors.base"));
                expect (child->contains ("colors.unchanged"));
                expect (child->contains ("metrics.space"));
                const auto* inherited = child->find<juce::Colour> ("colors.unchanged");
                const auto* referenced = child->find<juce::Colour> ("colors.fromParent");
                expect (inherited != nullptr && referenced != nullptr && *inherited == *referenced);
                expect (child->find<double> ("colors.base") == nullptr);
            }
        }

        beginTest ("token aliases retain the resource package that declared their value");
        {
            SkinValidationReport report;
            auto parent = compileTokens (R"json({
                "strings": { "asset": "assets/parent.svg" },
                "fonts": {
                  "body": {
                    "source": "assets/parent.ttf",
                    "families": ["sans-serif"], "weight": 400
                  }
                }
            })json", nullptr, report, "parent@1.0.0");
            auto child = parent ? compileTokens (R"json({
                "strings": {
                  "assetAlias": { "$ref": "strings.asset" },
                  "localAsset": "assets/child.svg"
                },
                "fonts": {
                  "bodyAlias": { "$ref": "fonts.body" },
                  "localBody": {
                    "source": "assets/child.ttf",
                    "families": ["sans-serif"], "weight": 400
                  }
                }
            })json", &*parent, report, "child@1.0.0") : std::nullopt;
            expect (child.has_value(), report.toText());
            if (child)
            {
                expectEquals (child->originOf ("strings.assetAlias"),
                              juce::String ("parent@1.0.0"));
                expectEquals (child->originOf ("fonts.bodyAlias"),
                              juce::String ("parent@1.0.0"));
                expectEquals (child->originOf ("strings.localAsset"),
                              juce::String ("child@1.0.0"));
                expectEquals (child->originOf ("fonts.localBody"),
                              juce::String ("child@1.0.0"));
            }
        }

        beginTest ("reports missing, cross-type and cyclic references deterministically");
        {
            SkinValidationReport report;
            expect (! compileTokens (R"json({
                "colors": { "missing": { "$ref": "colors.nope" } }
            })json", nullptr, report));
            expect (report.hasCode ("TOKEN003"));
        }
        {
            SkinValidationReport report;
            expect (! compileTokens (R"json({
                "colors": { "accent": "#112233" },
                "metrics": { "wrong": { "$ref": "colors.accent" } }
            })json", nullptr, report));
            expect (report.hasCode ("TOKEN005"));
        }
        {
            SkinValidationReport report;
            expect (! compileTokens (R"json({
                "colors": {
                    "a": { "$ref": "colors.b" },
                    "b": { "$ref": "colors.c" },
                    "c": { "$ref": "colors.a" }
                }
            })json", nullptr, report));
            expect (report.hasCode ("TOKEN004"));
        }

        beginTest ("schema and semantic compiler enforce gradient and font contracts");
        {
            SkinValidationReport report;
            expect (! validateTokens (R"json({
                "gradients": {
                    "bad": {
                        "type": "radial", "centerX": 0.5, "centerY": 0.5,
                        "stops": [
                            { "position": 0, "color": "#000000" },
                            { "position": 1, "color": "#FFFFFF" }
                        ]
                    }
                }
            })json", report));
            expect (report.hasCode ("SCHEMA004") || report.hasCode ("SCHEMA005"));
        }
        {
            SkinValidationReport report;
            expect (! validateTokens (R"json({
                "gradients": {
                    "bad": {
                        "type": "radial", "centerX": 0.5, "centerY": 0.5,
                        "radius": 0,
                        "stops": [
                            { "position": 0, "color": "#000000" },
                            { "position": 1, "color": "#FFFFFF" }
                        ]
                    }
                }
            })json", report));
            expect (report.hasCode ("SCHEMA004") || report.hasCode ("SCHEMA005"));
        }
        {
            SkinValidationReport report;
            expect (! compileTokens (R"json({
                "gradients": {
                    "bad": {
                        "type": "linear", "angle": 0,
                        "stops": [
                            { "position": 0.8, "color": "#000000" },
                            { "position": 0.2, "color": "#FFFFFF" }
                        ]
                    }
                }
            })json", nullptr, report));
            expect (report.hasCode ("TOKEN006"));
        }
        {
            SkinValidationReport report;
            expect (! validateTokens (R"json({
                "fonts": {
                    "bad": { "source": "fonts/nope.ttf", "families": ["Body"], "weight": 400 }
                }
            })json", report));
            expect (report.hasCode ("SCHEMA004") || report.hasCode ("SCHEMA005"));
        }

        beginTest ("tokens schema is generated and consumed from the shared registry");
        {
            const auto schema = SkinSchemaRegistry::jsonSchema (SkinSchemaDocument::tokens);
            expectEquals (schema.getProperty ("x-generatedBy", {}).toString(),
                          juce::String ("Moonwake SkinSchemaRegistry"));
            expectEquals (schema.getProperty ("$id", {}).toString(),
                          juce::String ("urn:uuid:c864e7d5-12f1-4c4e-90d9-efdcc26a3016"));
            expect (SkinSchemaRegistry::isKnownField (SkinSchemaDocument::tokens,
                                                      "root", "gradients"));
            SkinValidationReport report;
            expect (! validateTokens (R"json({ "unknownGroup": {} })json", report));
            expect (report.hasCode ("SCHEMA001"));
        }

        beginTest ("non-finite numeric values are rejected before compilation");
        {
            auto root = juce::var (new juce::DynamicObject());
            auto metrics = juce::var (new juce::DynamicObject());
            metrics.getDynamicObject()->setProperty ("infinite",
                                                       std::numeric_limits<double>::infinity());
            root.getDynamicObject()->setProperty ("metrics", metrics);
            SkinValidationReport report;
            expect (! SkinSchemaValidator::validate (SkinSchemaDocument::tokens, root,
                                                     "tokens.json", report));
            expect (report.hasCode ("SCHEMA004") || report.hasCode ("SCHEMA005"));
        }
    }
};

SkinTokenStoreTests skinTokenStoreTests;
} // namespace
