#include <juce_core/juce_core.h>

#include "skin/SkinCompatibilityChecker.h"

namespace
{
juce::var header (int skinApi, const juce::String& minAppVersion,
                  std::initializer_list<const char*> capabilities)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("skinApi", skinApi);
    object->setProperty ("minAppVersion", minAppVersion);
    juce::Array<juce::var> values;
    for (const auto* capability : capabilities)
        values.add (capability);
    object->setProperty ("capabilities", values);
    return juce::var (object);
}

class SkinCompatibilityTests final : public juce::UnitTest
{
public:
    SkinCompatibilityTests() : UnitTest ("SkinCompatibility", "skin") {}

    void runTest() override
    {
        beginTest ("parses the current application SemVer");
        const auto current = SkinSemVersion::parse ("1.2.3");
        expect (current.has_value());
        expect (SkinSemVersion::parse ("1.2.3-beta.1+build.7").has_value());
        expect (! SkinSemVersion::parse ("1x2x3"));
        expect (! SkinSemVersion::parse ("01.2.3"));
        expect (! SkinSemVersion::parse ("1.2.3-01"));
        expect (SkinSemVersion::parse ("123456789012345678901234567890.2.3").has_value());

        beginTest ("accepts current API known capabilities and equal app version");
        SkinValidationReport ok;
        expect (current && SkinCompatibilityChecker::checkManifestHeader (
            header (1, "1.2.3", { "tokens", "renderers", "layouts", "assets", "animations" }),
            *current, ok));
        expect (! ok.hasErrors());

        beginTest ("rejects future and unmigratable old APIs");
        SkinValidationReport future;
        expect (current && ! SkinCompatibilityChecker::checkManifestHeader (
            header (2, "1.0.0", { "tokens" }), *current, future));
        expect (future.hasCode ("COMPAT001"));
        SkinValidationReport old;
        expect (current && ! SkinCompatibilityChecker::checkManifestHeader (
            header (0, "1.0.0", { "tokens" }), *current, old));
        expect (old.hasCode ("COMPAT004"));

        beginTest ("enforces SemVer precedence and unknown capabilities");
        const auto prerelease = SkinSemVersion::parse ("1.2.3-beta.2");
        const auto release = SkinSemVersion::parse ("1.2.3");
        expect (prerelease && release && *prerelease < *release);
        const auto beta2 = SkinSemVersion::parse ("1.2.3-beta.2");
        const auto beta11 = SkinSemVersion::parse ("1.2.3-beta.11");
        expect (beta2 && beta11 && *beta2 < *beta11);
        SkinValidationReport tooNew;
        expect (current && ! SkinCompatibilityChecker::checkManifestHeader (
            header (1, "1.2.4", { "tokens" }), *current, tooNew));
        expect (tooNew.hasCode ("COMPAT002"));
        SkinValidationReport unknown;
        expect (current && ! SkinCompatibilityChecker::checkManifestHeader (
            header (1, "1.0.0", { "tokens", "scripts" }), *current, unknown));
        expect (unknown.hasCode ("COMPAT003"));

        beginTest ("rejects malformed compatibility header types deterministically");
        auto malformed = header (1, "1.0.0", { "tokens" });
        malformed.getDynamicObject()->setProperty ("skinApi", "1");
        SkinValidationReport malformedReport;
        expect (current && ! SkinCompatibilityChecker::checkManifestHeader (
            malformed, *current, malformedReport));
        expect (malformedReport.hasCode ("MANIFEST002"));
        expect (! malformedReport.hasCode ("COMPAT003"));
    }
};

SkinCompatibilityTests skinCompatibilityTests;
} // namespace
