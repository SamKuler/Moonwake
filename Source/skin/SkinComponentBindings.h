#pragma once

#include "PlayerComponentCatalog.h"
#include "SkinDiagnostic.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

class SkinComponentBindings
{
public:
    static const juce::Identifier componentIdProperty;
    static const juce::Identifier iconNameProperty;
    static const juce::Identifier iconPresentationProperty;

    bool bind (const juce::String& id, juce::Component&, SkinValidationReport&);
    [[nodiscard]] juce::Component* find (const juce::String& id) const;
    [[nodiscard]] juce::StringArray ids() const;

private:
    std::map<juce::String, juce::Component*> bindings;
};
