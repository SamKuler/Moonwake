#pragma once

#include "SkinDiagnostic.h"
#include "SkinTokenStore.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

enum class SkinExpressionType { number, boolean, string, colour };
using SkinExpressionValue = std::variant<double, bool, juce::String, juce::Colour>;

struct SkinExpressionContext
{
    std::function<std::optional<SkinExpressionValue> (std::string_view)> lookup;
};

struct SkinExpressionProgram;

class SkinExpression
{
public:
    static std::optional<SkinExpression> compile (
        juce::String source,
        const std::map<juce::String, SkinExpressionType>& allowedContext,
        const SkinTokenStore& tokens,
        SkinValidationReport&,
        juce::String file,
        juce::String pointer);

    [[nodiscard]] SkinExpressionType resultType() const;
    [[nodiscard]] bool referencesContext (juce::StringRef path) const;
    [[nodiscard]] bool changesWhenBooleanContextChanges (juce::StringRef path) const;
    [[nodiscard]] std::optional<SkinExpressionValue> evaluateWithBooleanContext (
        juce::StringRef path, bool value) const;
    bool rebindTokens (const SkinTokenStore&, SkinValidationReport&);
    [[nodiscard]] std::optional<SkinExpressionValue> evaluate (
        const SkinExpressionContext&,
        SkinValidationReport* runtimeWarnings = nullptr) const;

private:
    explicit SkinExpression (std::shared_ptr<const SkinExpressionProgram>);
    std::shared_ptr<const SkinExpressionProgram> program;
};
