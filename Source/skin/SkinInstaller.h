#pragma once

#include "SkinManifest.h"
#include "SkinPackage.h"
#include "SkinRegistry.h"

#include <functional>
#include <optional>
#include <vector>

class SkinInstaller final
{
public:
    using FileWriter = std::function<bool (const juce::File&,
                                           const juce::MemoryBlock&)>;

    explicit SkinInstaller (juce::File installRoot, FileWriter = {});

    bool recover (SkinValidationReport&);
    bool install (const SkinPackage&, const SkinManifest&,
                  std::optional<SkinSelection> protectedSelection,
                  SkinValidationReport&);
    bool remove (const SkinSelection&,
                 std::optional<SkinSelection> protectedSelection,
                 SkinValidationReport&);

private:
    struct StagedRemoval
    {
        juce::File version;
        juce::File stagedVersion;
        juce::File digest;
        juce::File stagedDigest;
        bool digestWasMoved = false;
    };

    bool recoverIdRoot (const juce::File&, SkinValidationReport&);
    bool stagePruneVersions (const SkinManifest&,
                             std::optional<SkinSelection>,
                             std::vector<StagedRemoval>&,
                             juce::File& transactionMarker,
                             SkinValidationReport&);
    static void rollbackPrune (std::vector<StagedRemoval>&,
                               const juce::File& transactionMarker);
    static void finalisePrune (std::vector<StagedRemoval>&,
                               const juce::File& transactionMarker,
                               SkinValidationReport&);

    juce::File installDirectory;
    FileWriter writeFile;
};
