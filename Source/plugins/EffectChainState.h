#pragma once

#include <juce_core/juce_core.h>

#include <vector>

struct EffectSlotState
{
    EffectSlotState() = default;

    EffectSlotState (int idToUse,
                     juce::String pluginNameToUse,
                     juce::String pluginIdentifierToUse,
                     juce::String pluginFormatToUse = {})
        : id (idToUse),
          pluginName (std::move (pluginNameToUse)),
          pluginIdentifier (std::move (pluginIdentifierToUse)),
          pluginFormat (std::move (pluginFormatToUse))
    {
    }

    int id = 0;
    juce::String pluginName;
    juce::String pluginIdentifier;
    juce::String pluginFormat;
    bool bypassed = false;
    bool available = true;
    juce::String errorMessage;
    juce::MemoryBlock serializedState;
};

class EffectChainState
{
public:
    int addSlot (juce::String pluginName,
                 juce::String pluginIdentifier,
                 juce::String pluginFormat = {});
    int addSlotWithId (int slotId,
                       juce::String pluginName,
                       juce::String pluginFormat,
                       juce::String pluginIdentifier);
    bool removeSlot (int slotId);
    bool setBypassed (int slotId, bool shouldBeBypassed);
    bool setAvailability (int slotId, bool isAvailable, juce::String errorMessage = {});
    bool moveSlot (int fromIndex, int toIndex);

    [[nodiscard]] int size() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] const EffectSlotState* getSlot (int index) const;
    [[nodiscard]] EffectSlotState* getSlotById (int slotId);
    [[nodiscard]] const EffectSlotState* getSlotById (int slotId) const;
    [[nodiscard]] const std::vector<EffectSlotState>& getSlots() const;

private:
    std::vector<EffectSlotState> slots;
    int nextSlotId = 1;

    [[nodiscard]] bool isValidIndex (int index) const;
};
