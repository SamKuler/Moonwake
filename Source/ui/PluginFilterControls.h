#pragma once

#include "plugins/PluginFilter.h"
#include "skin/SkinComponentBindings.h"

#include <functional>

class PluginFilterControls
{
public:
    PluginFilterControls();
    void setPlugins (const juce::Array<juce::PluginDescription>&);
    [[nodiscard]] PluginFilter filter() const;
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, const juce::String& prefix, SkinValidationReport&);
    void clearHiddenFilters();

    juce::TextEditor search;
    juce::ComboBox type, manufacturer, format;
    std::function<void()> onChange;
};
