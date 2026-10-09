#pragma once

#include "audio/ListeningOutputConfiguration.h"
#include "plugins/EffectChainState.h"

struct EffectChainPreset
{
    juce::String id = juce::Uuid().toString();
    juce::String name;
    juce::int64 createdAtMs = juce::Time::currentTimeMillis();
    juce::int64 updatedAtMs = createdAtMs;
    EffectChainState chain;
};

struct ListeningPreset
{
    juce::String id = juce::Uuid().toString();
    juce::String name;
    juce::int64 createdAtMs = juce::Time::currentTimeMillis();
    juce::int64 updatedAtMs = createdAtMs;
    ListeningOutputConfiguration output;
    EffectChainState chain;
};

juce::Result validateEffectChainSnapshot (const EffectChainState&);
juce::Result validateListeningOutputConfiguration (const ListeningOutputConfiguration&);
juce::Result validatePreset (const EffectChainPreset&);
juce::Result validatePreset (const ListeningPreset&);

class ListeningPresetLibrary
{
public:
    const std::vector<EffectChainPreset>& getEffectChainPresets() const { return effectChains; }
    const std::vector<ListeningPreset>& getListeningPresets() const { return listening; }
    const EffectChainPreset* findEffectChain (const juce::String& id) const;
    const ListeningPreset* findListening (const juce::String& id) const;
    juce::Result put (EffectChainPreset);
    juce::Result put (ListeningPreset);
    juce::Result renameEffectChain (const juce::String& id, const juce::String& name);
    juce::Result renameListening (const juce::String& id, const juce::String& name);
    juce::Result duplicateEffectChain (const juce::String& id, const juce::String& name, juce::String& newId);
    juce::Result duplicateListening (const juce::String& id, const juce::String& name, juce::String& newId);
    juce::Result removeEffectChain (const juce::String& id);
    juce::Result removeListening (const juce::String& id);

private:
    std::vector<EffectChainPreset> effectChains;
    std::vector<ListeningPreset> listening;
};
