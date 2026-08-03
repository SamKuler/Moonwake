#pragma once

#include "SkinDiagnostic.h"
#include "SkinSchemaValidator.h"
#include "SkinValue.h"

#include <map>
#include <optional>

class SkinTokenStore
{
public:
    static std::optional<SkinTokenStore> compile (const SkinValidatedDocument& child,
                                                  const SkinTokenStore* parent,
                                                  SkinValidationReport&,
                                                  juce::String origin = {});

    template <typename T>
    [[nodiscard]] const T* find (const juce::String& key) const
    {
        const auto found = values.find (key);
        return found == values.end() ? nullptr : std::get_if<T> (&found->second);
    }

    [[nodiscard]] bool contains (const juce::String& key) const;
    [[nodiscard]] juce::String originOf (const juce::String& key) const;

private:
    std::map<juce::String, SkinTokenValue> values;
    std::map<juce::String, juce::String> origins;
};
