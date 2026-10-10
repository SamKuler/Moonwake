#include "SkinDiagnostic.h"

#include <algorithm>

void SkinValidationReport::addError (juce::String code, juce::String file,
                                     juce::String pointer, juce::String message,
                                     juce::String suggestion)
{
    add (SkinDiagnosticSeverity::error, std::move (code), std::move (file),
         std::move (pointer), std::move (message), std::move (suggestion));
}

void SkinValidationReport::addWarning (juce::String code, juce::String file,
                                       juce::String pointer, juce::String message,
                                       juce::String suggestion)
{
    add (SkinDiagnosticSeverity::warning, std::move (code), std::move (file),
         std::move (pointer), std::move (message), std::move (suggestion));
}

bool SkinValidationReport::hasErrors() const
{
    return std::any_of (diagnostics.begin(), diagnostics.end(), [] (const auto& item)
    {
        return item.severity == SkinDiagnosticSeverity::error;
    });
}

bool SkinValidationReport::hasCode (juce::StringRef code) const
{
    return std::any_of (diagnostics.begin(), diagnostics.end(), [&] (const auto& item)
    {
        return item.code == code;
    });
}

const std::vector<SkinDiagnostic>& SkinValidationReport::getDiagnostics() const
{
    return diagnostics;
}

juce::String SkinValidationReport::toText() const
{
    juce::String result;

    for (const auto& item : diagnostics)
    {
        result << (item.severity == SkinDiagnosticSeverity::error ? "ERROR " : "WARNING ")
               << item.code << " " << item.file << "#" << item.pointer << "\n"
               << item.message << "\n";

        if (item.suggestion.isNotEmpty())
            result << "Suggestion: " << item.suggestion << "\n";
    }

    return result;
}

void SkinValidationReport::add (SkinDiagnosticSeverity severity, juce::String code,
                                juce::String file, juce::String pointer,
                                juce::String message, juce::String suggestion)
{
    diagnostics.push_back ({ severity, std::move (code), std::move (file),
                             std::move (pointer), std::move (message),
                             std::move (suggestion) });
}

const std::vector<juce::String>& skinDiagnosticCodeRegistry()
{
    static const std::vector<juce::String> codes {
        "PKG001", "PKG002", "PKG003", "PKG004", "PKG005", "PKG006",
        "PKG007", "PKG008", "PKG009", "PKG010", "PKG011", "PKG012",
        "MANIFEST001", "MANIFEST002", "MANIFEST003", "MANIFEST004",
        "MANIFEST005", "MANIFEST006", "COMPAT001", "COMPAT002", "COMPAT003",
        "COMPAT004", "SCHEMA001", "SCHEMA002", "SCHEMA003", "SCHEMA004",
        "SCHEMA005", "TOKEN001", "TOKEN002", "TOKEN003", "TOKEN004",
        "TOKEN005", "TOKEN006", "TOKEN007", "EXPR001", "EXPR002", "EXPR003",
        "EXPR004", "EXPR005", "EXPR006", "EXPR007", "EXPR008", "EXPR020",
        "EXPR021", "COMPONENT001", "COMPONENT002", "COMPONENT003",
        "COMPONENT004", "COMPONENT005", "COMPONENT006", "COMPONENT007",
        "LAYOUT001", "LAYOUT002", "LAYOUT003", "LAYOUT004", "LAYOUT005",
        "LAYOUT006", "LAYOUT007", "LAYOUT008", "LAYOUT009", "LAYOUT010",
        "LAYOUT011", "LAYOUT012", "LAYOUT013", "LAYOUT014", "LAYOUT015",
        "LAYOUT016", "LAYOUT017", "LAYOUT018", "LAYOUT019", "RENDER001", "RENDER002", "RENDER003",
        "RENDER004", "RENDER005", "RENDER006", "RENDER007", "RENDER008",
        "RENDER009", "RENDER010", "RENDER011", "RENDER012", "RENDER013",
        "RENDER014", "ASSET001", "ASSET002", "ASSET003", "ASSET004",
        "ASSET005", "ASSET006", "ASSET007", "ASSET008", "ASSET009",
        "ASSET010", "ASSET011", "RUNTIME001", "RUNTIME002", "RUNTIME003",
        "RUNTIME004", "RUNTIME005", "RUNTIME006", "RUNTIME007", "TOOL001",
        "TOOL002", "TOOL003", "TOOL004", "TOOL005", "TOOL006",
        "TOOL007", "TOOL008"
    };
    return codes;
}
