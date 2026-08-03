#pragma once

#include <juce_core/juce_core.h>

#include <vector>

enum class SkinDiagnosticSeverity
{
    warning,
    error
};

struct SkinDiagnostic
{
    SkinDiagnosticSeverity severity = SkinDiagnosticSeverity::error;
    juce::String code;
    juce::String file;
    juce::String pointer;
    juce::String message;
    juce::String suggestion;
};

class SkinValidationReport
{
public:
    void addError (juce::String code, juce::String file, juce::String pointer,
                   juce::String message, juce::String suggestion = {});
    void addWarning (juce::String code, juce::String file, juce::String pointer,
                     juce::String message, juce::String suggestion = {});

    [[nodiscard]] bool hasErrors() const;
    [[nodiscard]] bool hasCode (juce::StringRef code) const;
    [[nodiscard]] const std::vector<SkinDiagnostic>& getDiagnostics() const;
    [[nodiscard]] juce::String toText() const;

private:
    void add (SkinDiagnosticSeverity, juce::String code, juce::String file,
              juce::String pointer, juce::String message, juce::String suggestion);

    std::vector<SkinDiagnostic> diagnostics;
};

[[nodiscard]] const std::vector<juce::String>& skinDiagnosticCodeRegistry();
