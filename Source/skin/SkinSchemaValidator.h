#pragma once

#include "SkinDiagnostic.h"
#include "SkinSchemaRegistry.h"

#include <optional>

class SkinValidatedDocument
{
public:
    [[nodiscard]] SkinSchemaDocument kind() const;
    [[nodiscard]] const juce::var& value() const;

private:
    friend class SkinSchemaValidator;
    SkinValidatedDocument (SkinSchemaDocument kindIn, juce::var valueIn);

    SkinSchemaDocument documentKind;
    juce::var documentValue;
};

class SkinSchemaValidator
{
public:
    static std::optional<SkinValidatedDocument> validate (
        SkinSchemaDocument, const juce::var&, juce::String file,
        SkinValidationReport&);
};
