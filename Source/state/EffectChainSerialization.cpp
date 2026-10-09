#include "EffectChainSerialization.h"

namespace
{
const juce::Identifier effectChainType { "EffectChain" };
const juce::Identifier slotType { "Slot" };
const juce::Identifier idProperty { "id" };
const juce::Identifier pluginNameProperty { "pluginName" };
const juce::Identifier pluginIdentifierProperty { "pluginIdentifier" };
const juce::Identifier pluginFormatProperty { "pluginFormat" };
const juce::Identifier bypassedProperty { "bypassed" };
const juce::Identifier availablePropertyForSlot { "available" };
const juce::Identifier errorMessageProperty { "errorMessage" };
const juce::Identifier serializedStateProperty { "serializedState" };
}

juce::ValueTree EffectChainSerialization::toValueTree (const EffectChainState& chain)
{
    juce::ValueTree chainTree (effectChainType);
    for (const auto& slot : chain.getSlots())
    {
        juce::ValueTree slotTree (slotType);
        slotTree.setProperty (idProperty, slot.id, nullptr);
        slotTree.setProperty (pluginNameProperty, slot.pluginName, nullptr);
        slotTree.setProperty (pluginIdentifierProperty, slot.pluginIdentifier, nullptr);
        slotTree.setProperty (pluginFormatProperty, slot.pluginFormat, nullptr);
        slotTree.setProperty (bypassedProperty, slot.bypassed, nullptr);
        slotTree.setProperty (availablePropertyForSlot, slot.available, nullptr);
        slotTree.setProperty (errorMessageProperty, slot.errorMessage, nullptr);
        if (! slot.scope.empty())
        {
            juce::ValueTree scopeTree ("Scope");
            for (const auto channel : slot.scope)
            {
                juce::ValueTree channelTree ("Channel");
                channelTree.setProperty ("type", static_cast<int> (channel), nullptr);
                scopeTree.appendChild (channelTree, nullptr);
            }
            slotTree.appendChild (scopeTree, nullptr);
        }
        if (slot.serializedState.getSize() > 0)
            slotTree.setProperty (serializedStateProperty, slot.serializedState.toBase64Encoding(), nullptr);
        chainTree.appendChild (slotTree, nullptr);
    }

    return chainTree;
}

EffectChainState EffectChainSerialization::fromValueTree (const juce::ValueTree& chainTree)
{
    EffectChainState chain;
    if (chainTree.isValid())
    {
        for (int i = 0; i < chainTree.getNumChildren(); ++i)
        {
            const auto slotTree = chainTree.getChild (i);
            if (! slotTree.hasType (slotType))
                continue;

            const auto savedId = static_cast<int> (slotTree.getProperty (idProperty, 0));
            const auto pluginName = slotTree.getProperty (pluginNameProperty).toString();
            const auto pluginIdentifier = slotTree.getProperty (pluginIdentifierProperty).toString();
            const auto pluginFormat = slotTree.getProperty (pluginFormatProperty).toString();
            EffectScope scope;
            const auto scopeTree = slotTree.getChildWithName ("Scope");
            for (const auto channel : scopeTree)
                scope.push_back (channel.hasType ("Channel")
                    ? static_cast<juce::AudioChannelSet::ChannelType> (static_cast<int> (channel.getProperty ("type", 0)))
                    : juce::AudioChannelSet::unknown);
            const auto id = savedId > 0
                                ? chain.addSlotWithId (savedId,
                                                        pluginName,
                                                        pluginFormat,
                                                        pluginIdentifier, scope)
                                : chain.addSlot (pluginName,
                                                  pluginIdentifier,
                                                  pluginFormat, scope);

            if (id == 0)
                continue;

            chain.setBypassed (id, static_cast<bool> (slotTree.getProperty (bypassedProperty, false)));
            chain.setAvailability (id,
                                   static_cast<bool> (slotTree.getProperty (availablePropertyForSlot, true)),
                                   slotTree.getProperty (errorMessageProperty).toString());
            if (auto* slot = chain.getSlotById (id))
                slot->serializedState.fromBase64Encoding (slotTree.getProperty (serializedStateProperty).toString());
        }
    }

    return chain;
}
