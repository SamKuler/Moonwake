#include "PluginFilter.h"

juce::StringArray PluginFilter::types (const juce::PluginDescription& plugin)
{
    auto result = juce::StringArray::fromTokens (plugin.category, "|", {});
    result.trim();
    result.removeEmptyStrings();
    for (auto& type : result)
        if (type == "Fx") type = "Effect";
    if (plugin.isInstrument) result.addIfNotAlreadyThere ("Instrument", true);
    if (result.isEmpty()) result.add ("Uncategorised");
    result.removeDuplicates (true);
    return result;
}

juce::String PluginFilter::manufacturerName (const juce::PluginDescription& plugin)
{
    const auto name = plugin.manufacturerName.trim();
    return name.isEmpty() ? juce::String ("Unknown manufacturer") : name;
}

bool PluginFilter::matches (const juce::PluginDescription& plugin) const
{
    const auto categories = types (plugin);
    if (type.isNotEmpty() && ! categories.contains (type, true)) return false;
    if (manufacturer.isNotEmpty() && ! manufacturerName (plugin).equalsIgnoreCase (manufacturer)) return false;
    if (format.isNotEmpty() && plugin.pluginFormatName != format) return false;
    const auto text = plugin.name + " " + manufacturerName (plugin) + " "
        + plugin.category + " " + categories.joinIntoString (" ") + " " + plugin.pluginFormatName
        + (plugin.pluginFormatName == "AudioUnit" ? " AU" : "");
    for (const auto& term : juce::StringArray::fromTokens (search.trim(), true))
        if (! text.containsIgnoreCase (term)) return false;
    return true;
}
