#include "EffectChainState.h"

#include <algorithm>

int EffectChainState::addSlot (juce::String pluginName,
                               juce::String pluginIdentifier,
                               juce::String pluginFormat)
{
    const auto id = nextSlotId++;

    slots.push_back ({ id,
                       std::move (pluginName),
                       std::move (pluginIdentifier),
                       std::move (pluginFormat) });
    return id;
}

int EffectChainState::addSlotWithId (int slotId,
                                     juce::String pluginName,
                                     juce::String pluginFormat,
                                     juce::String pluginIdentifier)
{
    if (slotId <= 0 || getSlotById (slotId) != nullptr)
        return 0;

    slots.push_back ({ slotId,
                       std::move (pluginName),
                       std::move (pluginIdentifier),
                       std::move (pluginFormat) });
    nextSlotId = juce::jmax (nextSlotId, slotId + 1);
    return slotId;
}

bool EffectChainState::removeSlot (int slotId)
{
    const auto position = std::find_if (slots.begin(), slots.end(), [slotId] (const auto& slot)
    {
        return slot.id == slotId;
    });

    if (position == slots.end())
        return false;

    slots.erase (position);
    return true;
}

bool EffectChainState::setBypassed (int slotId, bool shouldBeBypassed)
{
    if (auto* slot = getSlotById (slotId))
    {
        slot->bypassed = shouldBeBypassed;
        return true;
    }

    return false;
}

bool EffectChainState::setAvailability (int slotId, bool isAvailable, juce::String error)
{
    if (auto* slot = getSlotById (slotId))
    {
        slot->available = isAvailable;
        slot->errorMessage = isAvailable ? juce::String{} : std::move (error);
        return true;
    }

    return false;
}

bool EffectChainState::moveSlot (int fromIndex, int toIndex)
{
    if (! isValidIndex (fromIndex) || ! isValidIndex (toIndex))
        return false;

    if (fromIndex == toIndex)
        return true;

    auto slot = std::move (slots[static_cast<size_t> (fromIndex)]);
    slots.erase (slots.begin() + fromIndex);
    slots.insert (slots.begin() + toIndex, std::move (slot));
    return true;
}

int EffectChainState::size() const
{
    return static_cast<int> (slots.size());
}

bool EffectChainState::isEmpty() const
{
    return slots.empty();
}

const EffectSlotState* EffectChainState::getSlot (int index) const
{
    if (! isValidIndex (index))
        return nullptr;

    return &slots[static_cast<size_t> (index)];
}

EffectSlotState* EffectChainState::getSlotById (int slotId)
{
    const auto position = std::find_if (slots.begin(), slots.end(), [slotId] (const auto& slot)
    {
        return slot.id == slotId;
    });

    return position == slots.end() ? nullptr : &*position;
}

const EffectSlotState* EffectChainState::getSlotById (int slotId) const
{
    const auto position = std::find_if (slots.begin(), slots.end(), [slotId] (const auto& slot)
    {
        return slot.id == slotId;
    });

    return position == slots.end() ? nullptr : &*position;
}

const std::vector<EffectSlotState>& EffectChainState::getSlots() const
{
    return slots;
}

bool EffectChainState::isValidIndex (int index) const
{
    return index >= 0 && index < static_cast<int> (slots.size());
}
