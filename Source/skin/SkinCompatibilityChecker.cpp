#include "SkinCompatibilityChecker.h"

#include "SkinSchemaRegistry.h"

#include <regex>

namespace
{
bool isNumericIdentifier (const juce::String& value)
{
    return value.isNotEmpty() && value.containsOnly ("0123456789");
}

std::strong_ordering compareIdentifiers (const juce::String& left,
                                         const juce::String& right)
{
    const auto leftNumeric = isNumericIdentifier (left);
    const auto rightNumeric = isNumericIdentifier (right);
    if (leftNumeric != rightNumeric)
        return leftNumeric ? std::strong_ordering::less : std::strong_ordering::greater;

    if (leftNumeric)
    {
        if (left.length() != right.length())
            return left.length() < right.length() ? std::strong_ordering::less
                                                  : std::strong_ordering::greater;
    }

    const auto comparison = left.compare (right);
    if (comparison < 0) return std::strong_ordering::less;
    if (comparison > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}
} // namespace

std::optional<SkinSemVersion> SkinSemVersion::parse (juce::StringRef source)
{
    static const std::regex pattern (
        R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$)");
    const auto text = juce::String (source).toStdString();
    std::smatch match;
    if (! std::regex_match (text, match, pattern))
        return std::nullopt;

    SkinSemVersion result;
    result.major = match[1].str();
    result.minor = match[2].str();
    result.patch = match[3].str();
    result.prerelease = match[4].matched ? juce::String (match[4].str()) : juce::String();
    result.build = match[5].matched ? juce::String (match[5].str()) : juce::String();
    return result;
}

std::strong_ordering SkinSemVersion::operator<=> (const SkinSemVersion& other) const
{
    if (const auto comparison = compareIdentifiers (major, other.major);
        comparison != std::strong_ordering::equal)
        return comparison;
    if (const auto comparison = compareIdentifiers (minor, other.minor);
        comparison != std::strong_ordering::equal)
        return comparison;
    if (const auto comparison = compareIdentifiers (patch, other.patch);
        comparison != std::strong_ordering::equal)
        return comparison;

    if (prerelease.isEmpty() != other.prerelease.isEmpty())
        return prerelease.isEmpty() ? std::strong_ordering::greater
                                    : std::strong_ordering::less;
    if (prerelease.isEmpty())
        return std::strong_ordering::equal;

    const auto left = juce::StringArray::fromTokens (prerelease, ".", {});
    const auto right = juce::StringArray::fromTokens (other.prerelease, ".", {});
    const auto count = juce::jmin (left.size(), right.size());
    for (int i = 0; i < count; ++i)
        if (const auto comparison = compareIdentifiers (left[i], right[i]);
            comparison != std::strong_ordering::equal)
            return comparison;

    if (left.size() == right.size())
        return std::strong_ordering::equal;
    return left.size() < right.size() ? std::strong_ordering::less
                                      : std::strong_ordering::greater;
}

bool SkinSemVersion::operator== (const SkinSemVersion& other) const
{
    return (*this <=> other) == std::strong_ordering::equal;
}

bool SkinMigrationRegistry::hasExplicitPath (int, int)
{
    return false;
}

bool SkinCompatibilityChecker::checkManifestHeader (
    const juce::var& rawManifest, const SkinSemVersion& currentAppVersion,
    SkinValidationReport& report)
{
    if (! rawManifest.isObject())
    {
        report.addError ("MANIFEST002", "manifest.json", {},
                         "Manifest root must be an object.");
        return false;
    }

    const auto skinApiValue = rawManifest.getProperty ("skinApi", {});
    const auto minAppValue = rawManifest.getProperty ("minAppVersion", {});
    const auto capabilityValue = rawManifest.getProperty ("capabilities", {});
    if ((! skinApiValue.isInt() && ! skinApiValue.isInt64())
        || ! minAppValue.isString() || ! capabilityValue.isArray())
    {
        report.addError ("MANIFEST002", "manifest.json", {},
                         "Compatibility header fields have invalid JSON types.");
        return false;
    }

    const auto skinApi = static_cast<int> (skinApiValue);
    constexpr int currentSkinApi = 1;
    if (skinApi > currentSkinApi)
    {
        report.addError ("COMPAT001", "manifest.json", "/skinApi",
                         "Skin API is newer than this application.");
        return false;
    }
    if (skinApi < currentSkinApi
        && ! SkinMigrationRegistry::hasExplicitPath (skinApi, currentSkinApi))
    {
        report.addError ("COMPAT004", "manifest.json", "/skinApi",
                         "No explicit Skin API migration path is available.");
        return false;
    }

    const auto minimum = SkinSemVersion::parse (minAppValue.toString());
    if (! minimum)
    {
        report.addError ("MANIFEST004", "manifest.json", "/minAppVersion",
                         "minAppVersion is not SemVer 2.0.");
        return false;
    }
    if (*minimum > currentAppVersion)
    {
        report.addError ("COMPAT002", "manifest.json", "/minAppVersion",
                         "Skin requires a newer application version.");
        return false;
    }

    const auto* capabilities = capabilityValue.getArray();
    jassert (capabilities != nullptr);
    for (int i = 0; i < capabilities->size(); ++i)
    {
        const auto& value = capabilities->getReference (i);
        if (! value.isString())
        {
            report.addError ("MANIFEST002", "manifest.json",
                             "/capabilities/" + juce::String (i),
                             "Capability must be a string.");
            return false;
        }
        if (! SkinSchemaRegistry::knownCapabilities().contains (value.toString()))
        {
            report.addError ("COMPAT003", "manifest.json",
                             "/capabilities/" + juce::String (i),
                             "Capability is not recognised by this application.");
            return false;
        }
    }

    return true;
}
