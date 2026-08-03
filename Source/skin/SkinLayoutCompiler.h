#pragma once

#include "PlayerComponentCatalog.h"
#include "SkinDiagnostic.h"
#include "SkinLayoutDefinition.h"
#include "SkinSchemaValidator.h"

class SkinLayoutCompiler
{
public:
    static std::optional<SkinLayoutDefinition> compile (
        const SkinValidatedDocument&,
        SkinLayoutEntry,
        SkinValidationReport&);
};
