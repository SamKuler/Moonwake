#pragma once

#include "CompiledSkin.h"

class SkinCompiler
{
public:
    static std::shared_ptr<const CompiledSkin> compile (
        const SkinPackage&,
        std::shared_ptr<const CompiledSkin> foundation,
        SkinValidationReport&);
};
