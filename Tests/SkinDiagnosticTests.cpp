#include <juce_core/juce_core.h>

#include "skin/SkinDiagnostic.h"
#include "ui/SkinFallbackDiagnostic.h"

namespace
{
class SkinDiagnosticTests final : public juce::UnitTest
{
public:
    SkinDiagnosticTests() : UnitTest ("SkinDiagnostic", "skin") {}

    void runTest() override
    {
        beginTest ("preserves structured diagnostics and deterministic text");
        SkinValidationReport report;
        report.addError ("MANIFEST001", "manifest.json", "/skinApi",
                         "skinApi must be 1.", "Set skinApi to 1.");
        report.addWarning ("EXPR021", "components/knob.json", "/layers/1/endAngle",
                           "Division by zero.", "Avoid a zero denominator.");

        expect (report.hasErrors());
        expectEquals (static_cast<int> (report.getDiagnostics().size()), 2);
        expect (report.getDiagnostics()[0].severity == SkinDiagnosticSeverity::error);
        expectEquals (report.getDiagnostics()[0].code, juce::String ("MANIFEST001"));
        expect (report.toText().contains ("ERROR MANIFEST001 manifest.json#/skinApi"));
        expect (report.toText().contains ("Suggestion: Set skinApi to 1."));
        expect (report.toText().indexOf ("MANIFEST001") < report.toText().indexOf ("EXPR021"));

        beginTest ("publishes unique stable error codes");
        const auto& codes = skinDiagnosticCodeRegistry();
        juce::StringArray unique;
        for (const auto& code : codes)
            unique.addIfNotAlreadyThere (code);
        expectEquals (unique.size(), static_cast<int> (codes.size()));
        expect (unique.contains ("PKG001"));
        expect (unique.contains ("COMPAT004"));
        expect (unique.contains ("LAYOUT017"));
        expect (unique.contains ("RUNTIME007"));
        expect (unique.contains ("TOOL004"));
        expect (unique.contains ("TOOL005"));
        expect (unique.contains ("TOOL008"));

        beginTest ("startup skin failures remain visible after fallback activation");
        SkinValidationReport healthy;
        expectEquals (SkinFallbackDiagnostic::makeStartupStatusText (
                          healthy, "Ready"),
                      juce::String ("Ready"));

        SkinValidationReport fallback;
        fallback.addError ("RUNTIME005", "remembered.skin", {},
                           "The remembered skin is unavailable.");
        const auto status = SkinFallbackDiagnostic::makeStartupStatusText (
            fallback, "Ready");
        expect (status.containsIgnoreCase ("fallback"), status);
        expect (status.contains ("RUNTIME005"), status);
        expect (status.containsIgnoreCase ("remembered skin"), status);
        expect (! status.containsWholeWord ("Ready"), status);
    }
};

SkinDiagnosticTests skinDiagnosticTests;
} // namespace
