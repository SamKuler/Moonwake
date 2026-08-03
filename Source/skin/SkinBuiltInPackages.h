#pragma once

#include "SkinPackage.h"

class SkinBuiltInPackages
{
public:
    enum class Package { foundation, defaultSkin };

    static std::optional<SkinPackage> create (Package, SkinValidationReport&);
};
