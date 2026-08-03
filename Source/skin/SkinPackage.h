#pragma once

#include "SkinDiagnostic.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <vector>

class SkinPackage
{
public:
    struct EmbeddedFile
    {
        juce::String path;
        juce::MemoryBlock bytes;
    };

    static std::optional<SkinPackage> captureSnapshot (
        const juce::File& source, const juce::File& privateTemporaryRoot,
        SkinValidationReport&);

    static std::optional<SkinPackage> fromEmbedded (
        juce::String packageId, std::vector<EmbeddedFile>, SkinValidationReport&);

    [[nodiscard]] bool isArchive() const;
    [[nodiscard]] juce::String readText (const juce::String& relativePath,
                                         SkinValidationReport&) const;
    [[nodiscard]] juce::MemoryBlock readBinary (const juce::String& relativePath,
                                                SkinValidationReport&) const;
    [[nodiscard]] bool contains (const juce::String& relativePath) const;
    [[nodiscard]] juce::StringArray listFiles() const;
    [[nodiscard]] const juce::File& snapshotRoot() const;
    [[nodiscard]] juce::String sha256() const;
    [[nodiscard]] bool verifySnapshotDigest (SkinValidationReport&) const;

    static bool isSafeRelativePath (const juce::String&);

private:
    struct State;
    explicit SkinPackage (std::shared_ptr<State>);

    std::shared_ptr<State> state;
};
