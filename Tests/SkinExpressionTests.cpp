#include "skin/SkinExpression.h"

#include "skin/SkinSchemaValidator.h"

#include <juce_core/juce_core.h>

namespace
{
SkinTokenStore makeTokens()
{
    const auto raw = juce::JSON::parse (R"json({
        "colors": { "accent": "#FF000080" },
        "metrics": { "space": 8 },
        "strings": { "mode": "studio" },
        "gradients": {
            "surface": {
                "type": "linear",
                "angle": 0,
                "stops": [
                    { "position": 0, "color": "#00000040" },
                    { "position": 1, "color": "#00000000" }
                ]
            }
        }
    })json");
    SkinValidationReport report;
    const auto document = SkinSchemaValidator::validate (SkinSchemaDocument::tokens, raw,
                                                          "tokens.json", report);
    jassert (document.has_value());
    const auto tokens = document ? SkinTokenStore::compile (*document, nullptr, report)
                                 : std::nullopt;
    jassert (tokens.has_value());
    return *tokens;
}

std::optional<SkinExpression> compileExpression (
    juce::String source, const SkinTokenStore& tokens, SkinValidationReport& report,
    std::map<juce::String, SkinExpressionType> context = {})
{
    return SkinExpression::compile (std::move (source), context, tokens, report,
                                    "renderer.json", "/layers/0/value");
}

SkinExpressionContext contextWith (
    std::map<juce::String, SkinExpressionValue> sourceValues)
{
    return { [values = std::move (sourceValues)] (std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        const auto found = values.find (juce::String::fromUTF8 (path.data(),
                                                                static_cast<int> (path.size())));
        return found == values.end() ? std::nullopt
                                     : std::optional<SkinExpressionValue> (found->second);
    } };
}

class SkinExpressionTests final : public juce::UnitTest
{
public:
    SkinExpressionTests() : UnitTest ("SkinExpression") {}

    void runTest() override
    {
        const auto tokens = makeTokens();

        beginTest ("honours frozen precedence, unary operators and exact result types");
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                "1 + 2 * 3 == 7 and not false or false", tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                expect (expression->resultType() == SkinExpressionType::boolean);
                const auto value = expression->evaluate (contextWith ({}), &report);
                expect (value && std::get<bool> (*value));
            }
        }
        {
            SkinValidationReport report;
            const auto expression = compileExpression ("-(2 + 3) * 4", tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                expect (value && std::abs (std::get<double> (*value) + 20.0) < 0.0001);
            }
        }

        beginTest ("supports bounded numeric functions, strings, tokens and context");
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                "max(1, min(9, 4), abs(-7)) + clamp(20, 0, 10) + round(2.6) + lerp(0, 10, 0.5) + token.metrics.space",
                tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                expect (value && std::abs (std::get<double> (*value) - 33.0) < 0.0001);
            }
        }
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                R"expr(text.primary == 'line\n\'quoted\'' and token.strings.mode == 'studio')expr",
                tokens, report, { { "text.primary", SkinExpressionType::string } });
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (
                    contextWith ({ { "text.primary", juce::String ("line\n'quoted'") } }), &report);
                expect (value && std::get<bool> (*value));
            }
        }

        beginTest ("constructs alpha-last colours and mixes premultiplied linear sRGB");
        {
            SkinValidationReport report;
            const auto expression = compileExpression ("token.colors.accent", tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                const auto colour = value ? std::get<juce::Colour> (*value) : juce::Colour();
                expect (colour.getRed() == 0xff && colour.getGreen() == 0
                        && colour.getBlue() == 0 && colour.getAlpha() == 0x80);
            }
        }
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                "mix(color('#000000'), color('#FFFFFF'), 0.5)", tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                const auto colour = value ? std::get<juce::Colour> (*value) : juce::Colour();
                expectWithinAbsoluteError (static_cast<int> (colour.getRed()), 188, 1);
                expect (colour.getRed() == colour.getGreen()
                        && colour.getGreen() == colour.getBlue() && colour.getAlpha() == 255);
            }
        }
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                "alpha(mix(color('#FF0000FF'), color('#0000FF00'), 0.5), 0.25)",
                tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                const auto colour = value ? std::get<juce::Colour> (*value) : juce::Colour();
                expectWithinAbsoluteError (static_cast<int> (colour.getRed()), 255, 1);
                expectWithinAbsoluteError (static_cast<int> (colour.getBlue()), 0, 1);
                expectWithinAbsoluteError (static_cast<int> (colour.getAlpha()), 64, 1);
            }
        }


        beginTest ("rebinding updates numeric and color-string token constants");
        {
            const auto compileStore = [] (const juce::String& json,
                                          const SkinTokenStore* parent,
                                          SkinValidationReport& report)
                -> std::optional<SkinTokenStore>
            {
                const auto document = SkinSchemaValidator::validate (
                    SkinSchemaDocument::tokens, juce::JSON::parse (json),
                    "tokens.json", report);
                return document ? SkinTokenStore::compile (*document, parent, report)
                                : std::nullopt;
            };
            SkinValidationReport report;
            const auto parent = compileStore (
                R"json({"metrics":{"size":8},"strings":{"tint":"#FF0000FF"}})json",
                nullptr, report);
            auto number = parent
                ? compileExpression ("token.metrics.size * 2", *parent, report)
                : std::nullopt;
            auto colour = parent
                ? compileExpression ("color(token.strings.tint)", *parent, report)
                : std::nullopt;
            const auto child = parent ? compileStore (
                R"json({"metrics":{"size":12},"strings":{"tint":"#336699FF"}})json",
                &*parent, report) : std::nullopt;
            expect (number && colour && child, report.toText());
            if (number && colour && child)
            {
                expect (number->rebindTokens (*child, report));
                expect (colour->rebindTokens (*child, report));
                const auto numberValue = number->evaluate (contextWith ({}), &report);
                const auto colourValue = colour->evaluate (contextWith ({}), &report);
                expect (numberValue && std::get<double> (*numberValue) == 24.0);
                const auto reboundColour = colourValue
                    ? std::get<juce::Colour> (*colourValue) : juce::Colour();
                expect (reboundColour.getRed() == 0x33
                        && reboundColour.getGreen() == 0x66
                        && reboundColour.getBlue() == 0x99);
            }
        }

        beginTest ("and/or short-circuit and division by zero warns once while returning zero");
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                "false and (1 / 0 == 0) or true", tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                expect (value && std::get<bool> (*value));
                expect (! report.hasCode ("EXPR021"));
            }
        }
        {
            SkinValidationReport report;
            const auto expression = compileExpression ("1 / 0 + 2 / 0", tokens, report);
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (contextWith ({}), &report);
                expect (value && std::abs (std::get<double> (*value)) < 0.0001);
                expect (report.hasCode ("EXPR021"));
                int warningCount = 0;
                for (const auto& item : report.getDiagnostics())
                    if (item.code == "EXPR021") ++warningCount;
                expectEquals (warningCount, 1);
            }
        }

        beginTest ("non-finite runtime results return no property value and EXPR020");
        {
            SkinValidationReport report;
            const auto expression = compileExpression (
                "value.maximum * 2", tokens, report,
                { { "value.maximum", SkinExpressionType::number } });
            expect (expression.has_value(), report.toText());
            if (expression)
            {
                const auto value = expression->evaluate (
                    contextWith ({ { "value.maximum", std::numeric_limits<double>::max() } }),
                    &report);
                expect (! value);
                expect (report.hasCode ("EXPR020"));
            }
        }

        beginTest ("rejects unknown paths, functions, assignments and exact type mismatches");
        {
            SkinValidationReport report;
            expect (! compileExpression ("bounds.missing + 1", tokens, report,
                                         { { "bounds.width", SkinExpressionType::number } }));
            expect (report.hasCode ("EXPR003"));
        }
        {
            SkinValidationReport report;
            expect (! compileExpression ("mystery(1)", tokens, report));
            expect (report.hasCode ("EXPR004"));
        }
        {
            SkinValidationReport report;
            expect (! compileExpression ("value = 2", tokens, report,
                                         { { "value", SkinExpressionType::number } }));
            expect (report.hasCode ("EXPR001"));
        }
        {
            SkinValidationReport report;
            expect (! compileExpression ("1 + true", tokens, report));
            expect (report.hasCode ("EXPR006"));
        }
        {
            SkinValidationReport report;
            expect (! compileExpression ("token.gradients.surface == token.gradients.surface",
                                         tokens, report));
            expect (report.hasCode ("EXPR006"));
        }
        {
            SkinValidationReport report;
            expect (! compileExpression ("abs(1, 2)", tokens, report));
            expect (report.hasCode ("EXPR005"));
        }

        beginTest ("enforces the 256-token and 32-depth budgets");
        {
            juce::String source ("1");
            for (int i = 0; i < 130; ++i)
                source += " + 1";
            SkinValidationReport report;
            expect (! compileExpression (source, tokens, report));
            expect (report.hasCode ("EXPR007"));
        }
        {
            juce::String source ("1");
            for (int i = 0; i < 33; ++i)
                source = "(" + source + ")";
            SkinValidationReport report;
            expect (! compileExpression (source, tokens, report));
            expect (report.hasCode ("EXPR008"));
        }
    }
};

SkinExpressionTests skinExpressionTests;
} // namespace
