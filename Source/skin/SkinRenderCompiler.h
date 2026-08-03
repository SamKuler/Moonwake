#pragma once

#include "PlayerComponentCatalog.h"
#include "SkinPackage.h"
#include "SkinRenderDefinition.h"
#include "SkinTokenStore.h"

class SkinRenderCompiler
{
public:
    static std::optional<SkinRenderProgram> compileProgram (
        const SkinValidatedDocument&, const SkinTokenStore&, SkinValidationReport&);

    static std::map<juce::String, SkinRenderStyleEntry> compileIndex (
        const SkinValidatedDocument&, const SkinPackage&, const SkinTokenStore&,
        const PlayerComponentCatalog&, SkinValidationReport&);

    static bool rebindProgramTokens (SkinRenderProgram&, const SkinTokenStore&,
                                     SkinValidationReport&);
};
