#include "ListeningPresets.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
constexpr size_t maximumPresetsPerKind = 512;
constexpr size_t maximumSlotStateBytes = 16 * 1024 * 1024;

bool supportedChannel (juce::AudioChannelSet::ChannelType channel)
{
    return isValidOutputChannelMix ({ channel });
}

template <typename Preset>
juce::Result validateIdentity (const Preset& preset)
{
    const auto uuid = juce::Uuid (preset.id);
    if (uuid.isNull() || uuid.toString() != preset.id)
        return juce::Result::fail ("Preset ID must be a canonical UUID.");
    if (preset.name.trim().isEmpty() || preset.name.length() > 256)
        return juce::Result::fail ("Preset name must contain 1 to 256 characters.");
    if (preset.createdAtMs < 0 || preset.updatedAtMs < preset.createdAtMs)
        return juce::Result::fail ("Preset timestamps are invalid.");
    return validateEffectChainSnapshot (preset.chain);
}

template <typename Preset>
const Preset* find (const std::vector<Preset>& values, const juce::String& id)
{
    const auto it = std::find_if (values.begin(), values.end(), [&] (const auto& value) { return value.id == id; });
    return it == values.end() ? nullptr : &*it;
}

template <typename Preset>
juce::Result putPreset (std::vector<Preset>& values, Preset preset)
{
    preset.name = preset.name.trim();
    const auto validation = validatePreset (preset);
    if (validation.failed()) return validation;
    const auto it = std::find_if (values.begin(), values.end(), [&] (const auto& value) { return value.id == preset.id; });
    if (it != values.end()) *it = std::move (preset);
    else if (values.size() < maximumPresetsPerKind) values.push_back (std::move (preset));
    else return juce::Result::fail ("The preset library is full.");
    return juce::Result::ok();
}

template <typename Preset>
juce::Result renamePreset (std::vector<Preset>& values, const juce::String& id, const juce::String& name)
{
    const auto* existing = find (values, id);
    if (existing == nullptr) return juce::Result::fail ("Preset was not found.");
    auto copy = *existing;
    copy.name = name;
    copy.updatedAtMs = juce::jmax (copy.updatedAtMs, juce::Time::currentTimeMillis());
    return putPreset (values, std::move (copy));
}

template <typename Preset>
juce::Result duplicatePreset (std::vector<Preset>& values, const juce::String& id,
                              const juce::String& name, juce::String& newId)
{
    const auto* existing = find (values, id);
    if (existing == nullptr) return juce::Result::fail ("Preset was not found.");
    auto copy = *existing;
    copy.id = juce::Uuid().toString();
    copy.name = name;
    copy.createdAtMs = copy.updatedAtMs = juce::Time::currentTimeMillis();
    const auto result = putPreset (values, copy);
    if (result.wasOk()) newId = copy.id;
    return result;
}

template <typename Preset>
juce::Result removePreset (std::vector<Preset>& values, const juce::String& id)
{
    const auto count = std::erase_if (values, [&] (const auto& value) { return value.id == id; });
    return count > 0 ? juce::Result::ok() : juce::Result::fail ("Preset was not found.");
}
}

juce::Result validateEffectChainSnapshot (const EffectChainState& chain)
{
    if (chain.size() > 128) return juce::Result::fail ("A preset supports at most 128 effect slots.");
    std::set<int> ids;
    for (const auto& slot : chain.getSlots())
    {
        if (slot.id <= 0 || slot.id == std::numeric_limits<int>::max() || ! ids.insert (slot.id).second)
            return juce::Result::fail ("Effect slot IDs must be positive and unique.");
        if (slot.pluginName.trim().isEmpty() || slot.pluginName.length() > 1024
            || slot.pluginIdentifier.trim().isEmpty() || slot.pluginIdentifier.length() > 8192
            || slot.errorMessage.length() > 8192)
            return juce::Result::fail ("Effect plug-in identity is missing or too long.");
        if (slot.pluginFormat != "VST3" && slot.pluginFormat != "AudioUnit" && slot.pluginFormat.isNotEmpty())
            return juce::Result::fail ("Effect plug-in format is unsupported.");
        if (slot.serializedState.getSize() > maximumSlotStateBytes)
            return juce::Result::fail ("An effect state exceeds the 16 MiB preset limit.");
        std::set<int> channels;
        if (slot.scope.size() > 2) return juce::Result::fail ("Effect scope must select the full bus, one speaker or a pair.");
        for (const auto channel : slot.scope)
            if (! supportedChannel (channel) || ! channels.insert (static_cast<int> (channel)).second)
                return juce::Result::fail ("Effect scope contains an unknown or repeated speaker.");
    }
    return juce::Result::ok();
}

juce::Result validateListeningOutputConfiguration (const ListeningOutputConfiguration& output)
{
    if (std::none_of (outputLayouts().begin(), outputLayouts().end(), [&] (const auto& value) { return value.layout == output.layout; }))
        return juce::Result::fail ("Output layout is unsupported.");
    if (output.deviceIdentifier.length() > 8192 || output.deviceName.length() > 1024
        || ! std::isfinite (output.sampleRate) || output.sampleRate < 0.0 || output.sampleRate > 768000.0
        || output.bufferSize < 0 || output.bufferSize > 65536)
        return juce::Result::fail ("Output device settings are invalid.");
    std::set<int> ports;
    if (! output.routing.empty() && output.routing.size() != static_cast<size_t> (getChannelCount (output.layout)))
        return juce::Result::fail ("Output routing must include every logical speaker.");
    for (const auto port : output.routing)
        if (port < -1 || port > 4095 || (port >= 0 && ! ports.insert (port).second))
            return juce::Result::fail ("Output routing contains an invalid or repeated physical port.");
    std::set<int> mixChannels, calibrationChannels;
    for (const auto& mix : output.channelMix)
        if (! isValidOutputChannelMix (mix) || ! mixChannels.insert (static_cast<int> (mix.channel)).second)
            return juce::Result::fail ("Channel mix contains an invalid or repeated speaker.");
    for (const auto& calibration : output.calibration)
        if (! isValidOutputCalibration (calibration)
            || ! calibrationChannels.insert (static_cast<int> (calibration.channel)).second)
            return juce::Result::fail ("Output calibration contains an invalid or repeated speaker.");
    return juce::Result::ok();
}

juce::Result validatePreset (const EffectChainPreset& preset) { return validateIdentity (preset); }
juce::Result validatePreset (const ListeningPreset& preset)
{
    const auto identity = validateIdentity (preset);
    return identity.failed() ? identity : validateListeningOutputConfiguration (preset.output);
}

const EffectChainPreset* ListeningPresetLibrary::findEffectChain (const juce::String& id) const { return find (effectChains, id); }
const ListeningPreset* ListeningPresetLibrary::findListening (const juce::String& id) const { return find (listening, id); }
juce::Result ListeningPresetLibrary::put (EffectChainPreset preset) { return putPreset (effectChains, std::move (preset)); }
juce::Result ListeningPresetLibrary::put (ListeningPreset preset) { return putPreset (listening, std::move (preset)); }
juce::Result ListeningPresetLibrary::renameEffectChain (const juce::String& id, const juce::String& name) { return renamePreset (effectChains, id, name); }
juce::Result ListeningPresetLibrary::renameListening (const juce::String& id, const juce::String& name) { return renamePreset (listening, id, name); }
juce::Result ListeningPresetLibrary::duplicateEffectChain (const juce::String& id, const juce::String& name, juce::String& newId) { return duplicatePreset (effectChains, id, name, newId); }
juce::Result ListeningPresetLibrary::duplicateListening (const juce::String& id, const juce::String& name, juce::String& newId) { return duplicatePreset (listening, id, name, newId); }
juce::Result ListeningPresetLibrary::removeEffectChain (const juce::String& id) { return removePreset (effectChains, id); }
juce::Result ListeningPresetLibrary::removeListening (const juce::String& id) { return removePreset (listening, id); }
