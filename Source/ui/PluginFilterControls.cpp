#include "PluginFilterControls.h"

#include <array>

namespace
{
juce::String selectedFilter (const juce::ComboBox& box)
{
    return box.getSelectedId() > 1 ? box.getText() : juce::String {};
}

void populate (juce::ComboBox& box, juce::StringArray values, const juce::String& all)
{
    const auto previous = selectedFilter (box);
    values.removeDuplicates (true);
    values.sort (true);
    box.clear (juce::dontSendNotification);
    box.addItem (all, 1);
    for (int i = 0; i < values.size(); ++i) box.addItem (values[i], i + 2);
    const auto index = values.indexOf (previous, true);
    box.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
}
}

PluginFilterControls::PluginFilterControls()
{
    search.setTitle ("Search plug-ins");
    search.setTextToShowWhenEmpty ("Search plug-ins", juce::Colours::transparentBlack);
    type.setTitle ("Filter plug-ins by type");
    type.setTooltip ("Types reported by the plug-in");
    manufacturer.setTitle ("Filter plug-ins by manufacturer");
    format.setTitle ("Filter plug-ins by format");
    search.onTextChange = [this] { if (onChange) onChange(); };
    for (auto* box : { &type, &manufacturer, &format })
        box->onChange = [this] { if (onChange) onChange(); };
    setPlugins ({});
}

void PluginFilterControls::setPlugins (const juce::Array<juce::PluginDescription>& plugins)
{
    juce::StringArray types, manufacturers, formats;
    for (const auto& plugin : plugins)
    {
        types.addArray (PluginFilter::types (plugin));
        manufacturers.add (PluginFilter::manufacturerName (plugin));
        formats.add (plugin.pluginFormatName == "AudioUnit" ? "AU" : plugin.pluginFormatName);
    }
    populate (type, types, "All types");
    populate (manufacturer, manufacturers, "All manufacturers");
    populate (format, formats, "All formats");
}

PluginFilter PluginFilterControls::filter() const
{
    auto selectedFormat = selectedFilter (format);
    if (selectedFormat == "AU") selectedFormat = "AudioUnit";
    return { search.getText(), selectedFilter (type), selectedFilter (manufacturer), selectedFormat };
}

void PluginFilterControls::addTo (juce::Component& parent)
{
    for (auto* component : std::array<juce::Component*, 4> { &search, &type, &manufacturer, &format })
        parent.addAndMakeVisible (component);
}

void PluginFilterControls::bind (SkinComponentBindings& bindings, const juce::String& prefix,
                                SkinValidationReport& report)
{
    static_cast<void> (bindings.bind (prefix + ".filter", search, report));
    static_cast<void> (bindings.bind (prefix + ".typeFilter", type, report));
    static_cast<void> (bindings.bind (prefix + ".manufacturerFilter", manufacturer, report));
    static_cast<void> (bindings.bind (prefix + ".formatFilter", format, report));
}

void PluginFilterControls::clearHiddenFilters()
{
    bool changed = false;
    if (! search.isVisible() && search.getText().isNotEmpty())
    {
        search.setText ({}, false);
        changed = true;
    }
    for (auto* box : { &type, &manufacturer, &format })
        if (! box->isVisible() && box->getSelectedId() != 1)
        {
            box->setSelectedId (1, juce::dontSendNotification);
            changed = true;
        }
    if (changed && onChange) onChange();
}
