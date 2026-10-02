#pragma once

#include "audio/EffectScope.h"
#include <juce_gui_basics/juce_gui_basics.h>

inline juce::String effectScopeLabel (const EffectScope& scope)
{
    if (scope.empty()) return "All channels";
    juce::StringArray labels;
    for (const auto channel : scope)
        labels.add (juce::AudioChannelSet::getAbbreviatedChannelTypeName (channel));
    return labels.joinIntoString (" / ");
}

struct EffectScopeMenu
{
    explicit EffectScopeMenu (const juce::AudioChannelSet& layout)
    {
        menu.addItem (1, "All channels");
        scopes.emplace_back();
        const auto channels = layout.getChannelTypes();
        juce::PopupMenu mono, pairs;
        for (int first = 0; first < channels.size(); ++first)
        {
            scopes.push_back ({ channels[first] });
            mono.addItem (static_cast<int> (scopes.size()), effectScopeLabel (scopes.back()));
            juce::PopupMenu seconds;
            for (int second = first + 1; second < channels.size(); ++second)
            {
                scopes.push_back ({ channels[first], channels[second] });
                seconds.addItem (static_cast<int> (scopes.size()), effectScopeLabel (scopes.back()));
            }
            if (seconds.getNumItems() > 0)
                pairs.addSubMenu (juce::AudioChannelSet::getAbbreviatedChannelTypeName (channels[first]), seconds);
        }
        menu.addSubMenu ("One channel", mono);
        if (pairs.getNumItems() > 0) menu.addSubMenu ("Channel pair", pairs);
    }

    int idFor (const EffectScope& scope) const
    {
        const auto found = std::find (scopes.begin(), scopes.end(), scope);
        return found == scopes.end() ? 0 : static_cast<int> (std::distance (scopes.begin(), found)) + 1;
    }
    juce::PopupMenu menu;
    std::vector<EffectScope> scopes;
};
