#pragma once
#include "plugins/EffectChainState.h"
#include <juce_data_structures/juce_data_structures.h>

namespace EffectChainSerialization
{
juce::ValueTree toValueTree (const EffectChainState&);
// Supports legacy app-state slots without IDs or explicit plug-in formats.
EffectChainState fromValueTree (const juce::ValueTree&);
}
