#include "PlayerController.h"

#include <algorithm>

namespace
{
const char* operationPending = "Wait for the effect chain operation to finish.";
}

bool PlayerController::isEffectPresetModified() const
{
    return activeEffectPresetId.isNotEmpty()
        && effectPresetBaseline != std::make_pair (effectChainRevision, pluginStateRevision.load (std::memory_order_acquire));
}

bool PlayerController::isListeningPresetModified() const
{
    return activeListeningPresetId.isNotEmpty()
        && listeningPresetBaseline != std::make_tuple (effectChainRevision,
            pluginStateRevision.load (std::memory_order_acquire), outputConfigurationRevision, audioEngine.getDeviceConfigurationRevision());
}

ListeningOutputConfiguration PlayerController::captureListeningOutput() const
{
    const auto device = audioEngine.getDeviceSettings();
    return { device.currentDeviceIdentifier, device.currentDevice, audioEngine.getOutputLayout(),
             device.currentSampleRate, device.currentBufferSize, device.outputRouting,
             audioEngine.getOutputChannelMix(), audioEngine.getOutputCalibration() };
}

PlayerController::PresetRevision PlayerController::capturePresetRevision() const
{
    return { effectChainRevision, pluginStateRevision.load (std::memory_order_acquire),
             outputConfigurationRevision, audioEngine.getDeviceConfigurationRevision() };
}

void PlayerController::markPresetActive (PresetKind kind, const juce::String& id,
                                        const PresetRevision& revision)
{
    if (kind == PresetKind::effectChain)
    {
        activeEffectPresetId = id;
        effectPresetBaseline = { revision.effects, revision.parameters };
    }
    else
    {
        activeListeningPresetId = id;
        listeningPresetBaseline = { revision.effects, revision.parameters, revision.output, revision.device };
    }
}

juce::Result PlayerController::persistPresetLibrary (ListeningPresetLibrary candidate)
{
    if (restoringEffects) return juce::Result::fail (operationPending);
    juce::String error;
    if (! ListeningPresetStore::writeAtomically (candidate, presetFile, error))
    {
        lastStatusMessage = error;
        return juce::Result::fail (error);
    }
    presetLibrary = std::move (candidate);
    return juce::Result::ok();
}

juce::Result PlayerController::savePreset (PresetKind kind, const juce::String& name,
                                          const juce::String& overwriteId)
{
    if (restoringEffects) return juce::Result::fail (operationPending);
    if (name.trim().isEmpty()) return juce::Result::fail ("Enter a preset name.");
    const auto snapshotRevision = capturePresetRevision();
    EffectChainState snapshot;
    juce::String error;
    if (! captureEffectChainSnapshot (snapshot, error, true)) return juce::Result::fail (error);
    auto candidate = presetLibrary;
    const auto now = juce::Time::currentTimeMillis();
    juce::String savedId;
    juce::Result result = juce::Result::ok();
    if (kind == PresetKind::effectChain)
    {
        EffectChainPreset preset;
        if (overwriteId.isNotEmpty())
        {
            const auto* existing = candidate.findEffectChain (overwriteId);
            if (existing == nullptr) return juce::Result::fail ("Preset was not found.");
            preset = *existing;
        }
        preset.name = name;
        preset.updatedAtMs = juce::jmax (now, preset.createdAtMs);
        preset.chain = std::move (snapshot);
        savedId = preset.id;
        result = candidate.put (std::move (preset));
    }
    else
    {
        ListeningPreset preset;
        if (overwriteId.isNotEmpty())
        {
            const auto* existing = candidate.findListening (overwriteId);
            if (existing == nullptr) return juce::Result::fail ("Preset was not found.");
            preset = *existing;
        }
        preset.name = name;
        preset.updatedAtMs = juce::jmax (now, preset.createdAtMs);
        preset.output = captureListeningOutput();
        preset.chain = std::move (snapshot);
        savedId = preset.id;
        result = candidate.put (std::move (preset));
    }
    if (result.failed()) return result;
    result = persistPresetLibrary (std::move (candidate));
    if (result.wasOk())
    {
        markPresetActive (kind, savedId, snapshotRevision);
        lastStatusMessage = "Preset saved.";
    }
    return result;
}

juce::Result PlayerController::renamePreset (PresetKind kind, const juce::String& id, const juce::String& name)
{
    auto candidate = presetLibrary;
    const auto changed = kind == PresetKind::effectChain ? candidate.renameEffectChain (id, name)
                                                       : candidate.renameListening (id, name);
    return changed.failed() ? changed : persistPresetLibrary (std::move (candidate));
}

juce::Result PlayerController::duplicatePreset (PresetKind kind, const juce::String& id, const juce::String& name)
{
    auto candidate = presetLibrary;
    juce::String newId;
    const auto changed = kind == PresetKind::effectChain ? candidate.duplicateEffectChain (id, name, newId)
                                                       : candidate.duplicateListening (id, name, newId);
    return changed.failed() ? changed : persistPresetLibrary (std::move (candidate));
}

juce::Result PlayerController::deletePreset (PresetKind kind, const juce::String& id)
{
    auto candidate = presetLibrary;
    const auto removed = kind == PresetKind::effectChain ? candidate.removeEffectChain (id) : candidate.removeListening (id);
    if (removed.failed()) return removed;
    const auto saved = persistPresetLibrary (std::move (candidate));
    if (saved.wasOk())
    {
        auto& active = kind == PresetKind::effectChain ? activeEffectPresetId : activeListeningPresetId;
        if (active == id) active.clear();
    }
    return saved;
}

juce::Result PlayerController::importPreset (const juce::File& file)
{
    ImportedListeningPreset imported;
    juce::String error;
    if (! ListeningPresetStore::importPreset (file, imported, error)) return juce::Result::fail (error);
    auto candidate = presetLibrary;
    const auto result = std::visit ([&] (auto& preset)
    {
        preset.id = juce::Uuid().toString();
        preset.createdAtMs = preset.updatedAtMs = juce::Time::currentTimeMillis();
        return candidate.put (std::move (preset));
    }, imported);
    return result.failed() ? result : persistPresetLibrary (std::move (candidate));
}

juce::Result PlayerController::exportPreset (PresetKind kind, const juce::String& id, const juce::File& file)
{
    juce::String error;
    bool written = false;
    if (kind == PresetKind::effectChain)
    {
        const auto* preset = presetLibrary.findEffectChain (id);
        if (preset == nullptr) return juce::Result::fail ("Preset was not found.");
        written = ListeningPresetStore::exportPreset (*preset, file, error);
    }
    else
    {
        const auto* preset = presetLibrary.findListening (id);
        if (preset == nullptr) return juce::Result::fail ("Preset was not found.");
        written = ListeningPresetStore::exportPreset (*preset, file, error);
    }
    return written ? juce::Result::ok() : juce::Result::fail (error);
}

struct PlayerController::PendingPresetApply
{
    PresetKind kind = PresetKind::effectChain;
    juce::String id;
    EffectChainState chain;
    std::optional<ListeningOutputConfiguration> output;
    std::vector<juce::PluginDescription> descriptions;
    std::vector<AudioEngine::PreparedEffect> prepared;
    uint64_t generation = 0, effectRevision = 0, parameterRevision = 0, outputRevision = 0, deviceRevision = 0;
    PluginInsertionCallback completed;
    std::function<void()> beforeCommit;
};

juce::Result PlayerController::applyPreset (PresetKind kind, const juce::String& id,
                                           PluginInsertionCallback completed, std::function<void()> beforeCommit)
{
    if (restoringEffects) return juce::Result::fail (operationPending);
    auto pending = std::make_shared<PendingPresetApply>();
    pending->kind = kind;
    pending->id = id;
    if (kind == PresetKind::effectChain)
    {
        const auto* preset = presetLibrary.findEffectChain (id);
        if (preset == nullptr) return juce::Result::fail ("Preset was not found.");
        pending->chain = preset->chain;
    }
    else
    {
        const auto* preset = presetLibrary.findListening (id);
        if (preset == nullptr) return juce::Result::fail ("Preset was not found.");
        pending->chain = preset->chain;
        pending->output = preset->output;
    }
    const auto layout = toAudioChannelSet (pending->output ? pending->output->layout : audioEngine.getOutputLayout());
    const auto formats = pluginManager.getRegisteredFormatNames();
    for (const auto& slot : pending->chain.getSlots())
    {
        juce::String error;
        if (! validateEffectScope (slot.scope, layout, error)) return juce::Result::fail (slot.pluginName + ": " + error);
        const auto description = pluginManager.findPluginByIdentifier (slot.pluginIdentifier);
        if (description == nullptr)
            return juce::Result::fail (slot.pluginName + " is not installed or needs to be rescanned.");
        if (! formats.contains (description->pluginFormatName)
            || (slot.pluginFormat.isNotEmpty() && slot.pluginFormat != description->pluginFormatName))
            return juce::Result::fail (slot.pluginName + " uses an unavailable plug-in format.");
        pending->descriptions.push_back (*description);
    }
    pending->completed = std::move (completed);
    pending->beforeCommit = std::move (beforeCommit);
    pending->effectRevision = effectChainRevision;
    pending->parameterRevision = pluginStateRevision.load (std::memory_order_acquire);
    pending->outputRevision = outputConfigurationRevision;
    pending->deviceRevision = audioEngine.getDeviceConfigurationRevision();
    pending->generation = ++effectRestoreGeneration;
    restoringEffects = true;
    stopCalibrationTestSignal();
    const auto alive = lifetimeFlag;
    if (! juce::MessageManager::callAsync ([this, alive, pending]
        { if (alive->load()) prepareNextPresetEffect (pending, 0); }))
    {
        restoringEffects = false;
        return juce::Result::fail ("Could not schedule the preset operation.");
    }
    return juce::Result::ok();
}

void PlayerController::finishPresetApply (const std::shared_ptr<PendingPresetApply>& pending, juce::String error)
{
    const auto current = pending->generation == effectRestoreGeneration;
    if (current) restoringEffects = false;
    const auto success = error.isEmpty();
    const auto message = success ? juce::String ("Preset applied.") : std::move (error);
    if (current) lastStatusMessage = message;
    if (pending->completed) pending->completed (success, message);
}

void PlayerController::prepareNextPresetEffect (std::shared_ptr<PendingPresetApply> pending, size_t index)
{
    if (pending->generation != effectRestoreGeneration)
    {
        finishPresetApply (pending, "The session changed before the preset was ready.");
        return;
    }
    if (pending->effectRevision != effectChainRevision
        || pending->parameterRevision != pluginStateRevision.load (std::memory_order_acquire)
        || pending->outputRevision != outputConfigurationRevision
        || pending->deviceRevision != audioEngine.getDeviceConfigurationRevision())
    {
        finishPresetApply (pending, "Effect or output settings changed before the preset was ready.");
        return;
    }
    if (index == pending->descriptions.size())
    {
        juce::String failure;
        try
        {
            if (pending->beforeCommit) pending->beforeCommit();
            const AudioEngine::ScopedPlaybackEdit playbackEdit (audioEngine);
            failure = pending->output
                ? audioEngine.applyListeningConfiguration (*pending->output, std::move (pending->prepared))
                : audioEngine.replaceEffectPlugins (std::move (pending->prepared));
            if (failure.isEmpty())
            {
                effectChain = std::move (pending->chain);
                for (const auto& slot : effectChain.getSlots())
                {
                    if (auto* plugin = audioEngine.getEffectPlugin (slot.id)) plugin->addListener (this);
                    effectChain.setAvailability (slot.id, true);
                }
                ++effectChainRevision;
                const auto revision = pluginStateRevision.fetch_add (1, std::memory_order_acq_rel) + 1;
                capturedPluginStateRevision = revision;
                if (pending->output)
                {
                    preferredOutputLayout = audioEngine.getOutputLayout();
                    rememberAudioDeviceSettings();
                    const auto device = audioEngine.getDeviceSettings();
                    auto& routes = audioDevicePreferences.outputRoutes;
                    const auto found = std::find_if (routes.begin(), routes.end(), [&] (const auto& route)
                        { return route.deviceIdentifier == device.currentDeviceIdentifier && route.layout == preferredOutputLayout; });
                    AudioOutputRoute route { device.currentDeviceIdentifier, preferredOutputLayout, device.outputRouting };
                    if (found == routes.end()) routes.push_back (std::move (route));
                    else *found = std::move (route);
                    ++outputConfigurationRevision;
                }
                ++stateRevision;
                markPresetActive (pending->kind, pending->id, capturePresetRevision());
            }
        }
        catch (...)
        {
            failure = "A plug-in failed while applying the preset.";
        }
        finishPresetApply (pending, std::move (failure));
        return;
    }
    const auto alive = lifetimeFlag;
    const auto currentRate = audioEngine.getCurrentAudioSampleRate();
    const auto currentSize = audioEngine.getCurrentAudioBufferSize();
    const auto rate = pending->output && pending->output->sampleRate > 0.0
        ? pending->output->sampleRate : currentRate > 0.0 ? currentRate : 44100.0;
    const auto size = pending->output && pending->output->bufferSize > 0
        ? pending->output->bufferSize : currentSize > 0 ? currentSize : 512;
    pluginManager.createPluginInstanceAsync (pending->descriptions[index], rate, size,
        [this, alive, pending, index] (std::unique_ptr<juce::AudioPluginInstance> plugin,
                                                          const juce::String& creationError) mutable
        {
            if (! alive->load()) return;
            if (pending->generation != effectRestoreGeneration)
            { finishPresetApply (pending, "The session changed before the preset was ready."); return; }
            if (plugin == nullptr)
            { finishPresetApply (pending, creationError.isNotEmpty() ? creationError : "A preset plug-in could not be created."); return; }
            const auto* slot = pending->chain.getSlot (static_cast<int> (index));
            try
            {
                if (slot->serializedState.getSize() > 0)
                    plugin->setStateInformation (slot->serializedState.getData(), static_cast<int> (slot->serializedState.getSize()));
                pending->prepared.push_back ({ slot->id, std::move (plugin), slot->scope, slot->bypassed });
            }
            catch (...)
            { finishPresetApply (pending, "A plug-in failed while restoring preset state."); return; }
            prepareNextPresetEffect (std::move (pending), index + 1);
        });
}

juce::Result PlayerController::setOutputCalibration (const OutputChannelCalibration& calibration)
{
    if (restoringEffects) return juce::Result::fail (operationPending);
    const auto current = audioEngine.getOutputCalibration();
    if (std::find (current.begin(), current.end(), calibration) != current.end()) return juce::Result::ok();
    const auto error = audioEngine.setOutputCalibration (calibration.channel, calibration.trimDb,
                                                        calibration.delayMs, calibration.inverted);
    if (error.isNotEmpty()) return juce::Result::fail (error);
    ++outputConfigurationRevision;
    ++stateRevision;
    return juce::Result::ok();
}

void PlayerController::resetOutputCalibration()
{
    if (restoringEffects) { lastStatusMessage = operationPending; return; }
    const auto current = audioEngine.getOutputCalibration();
    if (std::none_of (current.begin(), current.end(), [] (const auto& value)
        { return value != OutputChannelCalibration { value.channel }; })) return;
    audioEngine.resetOutputCalibration();
    ++outputConfigurationRevision;
    ++stateRevision;
}

juce::Result PlayerController::setCalibrationTestSignal (CalibrationTestSignal signal)
{
    if (signal.kind == CalibrationTestSignal::Kind::off) { stopCalibrationTestSignal(); return juce::Result::ok(); }
    if (restoringEffects) return juce::Result::fail (operationPending);
    const auto validation = audioEngine.validateCalibrationTestSignal (signal);
    if (validation.isNotEmpty()) return juce::Result::fail (validation);
    if (! setPlaying (false)) return juce::Result::fail (lastStatusMessage);
    const auto error = audioEngine.setCalibrationTestSignal (signal);
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
}

void PlayerController::stopCalibrationTestSignal()
{
    audioEngine.stopCalibrationTestSignal();
}
