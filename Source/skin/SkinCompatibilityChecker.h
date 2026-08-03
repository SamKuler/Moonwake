#pragma once

#include "SkinDiagnostic.h"

#include <compare>
#include <optional>

struct SkinSemVersion
{
    juce::String major;
    juce::String minor;
    juce::String patch;
    juce::String prerelease;
    juce::String build;

    static std::optional<SkinSemVersion> parse (juce::StringRef);
    std::strong_ordering operator<=> (const SkinSemVersion&) const;
    bool operator== (const SkinSemVersion&) const;
};

class SkinMigrationRegistry
{
public:
    [[nodiscard]] static bool hasExplicitPath (int fromSkinApi, int toSkinApi);
};

class SkinCompatibilityChecker
{
public:
    static bool checkManifestHeader (const juce::var& rawManifest,
                                     const SkinSemVersion& currentAppVersion,
                                     SkinValidationReport&);
};
