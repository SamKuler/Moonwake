#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#if JUCE_MAC
#include <CoreAudio/CoreAudio.h>
#endif

namespace
{
#if JUCE_MAC
juce::String coreAudioString (AudioDeviceID device, AudioObjectPropertySelector selector)
{
    AudioObjectPropertyAddress address { selector, kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain };
    CFStringRef value = nullptr;
    UInt32 size = sizeof (value);
    if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, &value) != noErr
        || value == nullptr)
        return {};
    const auto text = juce::String::fromCFString (value);
    CFRelease (value);
    return text;
}

AudioDeviceID coreAudioOutputDevice (const juce::String& displayName)
{
    AudioObjectPropertyAddress address { kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
        return kAudioObjectUnknown;
    std::vector<AudioDeviceID> devices (size / sizeof (AudioDeviceID));
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
        return kAudioObjectUnknown;
    juce::StringArray names;
    std::vector<AudioDeviceID> outputs;
    for (const auto device : devices)
    {
        AudioObjectPropertyAddress streams { kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain };
        UInt32 streamSize = 0;
        if (AudioObjectGetPropertyDataSize (device, &streams, 0, nullptr, &streamSize) != noErr)
            continue;
        juce::HeapBlock<char> storage (streamSize);
        if (AudioObjectGetPropertyData (device, &streams, 0, nullptr, &streamSize, storage.getData()) != noErr)
            continue;
        const auto* buffers = reinterpret_cast<const AudioBufferList*> (storage.getData());
        unsigned channelCount = 0;
        for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index)
            channelCount += buffers->mBuffers[index].mNumberChannels;
        if (channelCount == 0) continue;
        const auto name = coreAudioString (device, kAudioDevicePropertyDeviceNameCFString);
        if (name.isEmpty()) continue;
        names.add (name);
        outputs.push_back (device);
    }
    // JUCE disambiguates duplicate CoreAudio names in the same device order.
    names.appendNumbersToDuplicates (false, true);
    const auto index = names.indexOf (displayName);
    return index >= 0 ? outputs[static_cast<size_t> (index)] : kAudioObjectUnknown;
}

bool openConfigurationBundle (const juce::String& identifier)
{
    if (identifier.isEmpty()) return false;
    juce::ChildProcess launcher;
    return launcher.start (juce::StringArray { "/usr/bin/open", "-b", identifier }, 0)
        && launcher.waitForProcessToFinish (3000) && launcher.getExitCode() == 0;
}
#endif

class AudioDeviceManagerBackend final : public IAudioDeviceBackend
{
public:
    juce::String initialiseWithDefaultDevices (int inputChannels,
                                                int outputChannels) override
    {
#if JUCE_MAC
        nativeDevice = kAudioObjectUnknown;
#endif
        return manager.initialiseWithDefaultDevices (inputChannels, outputChannels);
    }

    juce::StringArray getOutputDeviceNames() const override
    {
        if (auto* type = manager.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            return type->getDeviceNames (false);
        }
        return {};
    }

    AudioDeviceBackendState getCurrentDeviceState() const override
    {
        AudioDeviceBackendState result;
        if (auto* device = manager.getCurrentAudioDevice())
        {
            result.open = device->isOpen();
            result.name = device->getName();
            result.sampleRate = device->getCurrentSampleRate();
            result.sampleRates = device->getAvailableSampleRates();
            result.bufferSize = device->getCurrentBufferSizeSamples();
            result.bufferSizes = device->getAvailableBufferSizes();
            result.outputChannelNames = device->getOutputChannelNames();
            result.availableOutputChannels = result.outputChannelNames.size();
            result.activeOutputChannels = device->getActiveOutputChannels();
#if JUCE_MAC
            const auto deviceId = currentNativeDevice (result.name);
            if (deviceId != kAudioObjectUnknown)
                result.identifier = coreAudioString (deviceId, kAudioDevicePropertyDeviceUID);
#endif
        }
        return result;
    }

    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override
    {
        return manager.getAudioDeviceSetup();
    }

    juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup& setup,
        bool chosen) override
    {
#if JUCE_MAC
        nativeDevice = kAudioObjectUnknown;
#endif
        return manager.setAudioDeviceSetup (setup, chosen);
    }

    void addAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
        manager.addAudioCallback (callback);
    }

    void removeAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
        manager.removeAudioCallback (callback);
    }

    juce::String openDeviceConfiguration() override
    {
#if JUCE_MAC
        juce::String application;
        if (auto* device = manager.getCurrentAudioDevice())
            if (const auto deviceId = currentNativeDevice (device->getName());
                deviceId != kAudioObjectUnknown)
                application = coreAudioString (deviceId, kAudioDevicePropertyConfigurationApplication);
        if (openConfigurationBundle (application)) return {};
        if (application != "com.apple.audio.AudioMIDISetup"
            && openConfigurationBundle ("com.apple.audio.AudioMIDISetup")) return {};
        return "Could not open the device configuration application or Audio MIDI Setup.";
#elif JUCE_WINDOWS
        if (auto* device = manager.getCurrentAudioDevice(); device != nullptr && device->hasControlPanel())
            if (device->showControlPanel()) return {};
        return juce::Process::openDocument ("ms-settings:sound", {})
            ? juce::String{} : "Could not open Windows sound settings.";
#else
        return IAudioDeviceBackend::openDeviceConfiguration();
#endif
    }

private:
#if JUCE_MAC
    AudioDeviceID currentNativeDevice (const juce::String& name) const
    {
        if (nativeDeviceName != name || nativeDevice == kAudioObjectUnknown
            || coreAudioString (nativeDevice, kAudioDevicePropertyDeviceUID).isEmpty())
        {
            nativeDeviceName = name;
            nativeDevice = coreAudioOutputDevice (name);
        }
        return nativeDevice;
    }
    mutable juce::String nativeDeviceName;
    mutable AudioDeviceID nativeDevice = kAudioObjectUnknown;
#endif
    mutable juce::AudioDeviceManager manager;
};

class ScopedAudioCallbackPause final
{
public:
    ScopedAudioCallbackPause (IAudioDeviceBackend& backendToUse,
                              juce::AudioIODeviceCallback& callbackToUse)
        : backend (backendToUse), callback (callbackToUse)
    {
        backend.removeAudioCallback (&callback);
    }

    ~ScopedAudioCallbackPause()
    {
        backend.addAudioCallback (&callback);
    }

private:
    IAudioDeviceBackend& backend;
    juce::AudioIODeviceCallback& callback;
};

bool containsSampleRate (const juce::Array<double>& rates, double requested)
{
    return std::any_of (rates.begin(), rates.end(), [requested] (double available)
    {
        return std::abs (available - requested) < 0.01;
    });
}

juce::String setupFailureMessage (const juce::String& change,
                                  const juce::String& error,
                                  const juce::String& rollbackError)
{
    return "The audio device rejected " + change + ": " + error
        + (rollbackError.isNotEmpty()
               ? " Previous device setup also reported: " + rollbackError
               : juce::String{});
}

bool hasExactOutputRouting (
    const juce::AudioDeviceManager::AudioDeviceSetup& applied,
    const juce::AudioDeviceManager::AudioDeviceSetup& requested)
{
    return applied.useDefaultOutputChannels == requested.useDefaultOutputChannels
        && applied.outputChannels == requested.outputChannels;
}

std::vector<int> sequentialRouting (int channels)
{
    std::vector<int> result (static_cast<size_t> (channels));
    std::iota (result.begin(), result.end(), 0);
    return result;
}

juce::String outputRoutingMask (const std::vector<int>& routing, int logicalChannels,
                               const AudioDeviceBackendState& state, juce::BigInteger& mask)
{
    if (! state.open) return "No audio output device is active.";
    if (routing.size() != static_cast<size_t> (logicalChannels))
        return "Output routing must contain one destination for every logical speaker.";
    for (const auto port : routing)
    {
        if (port < -1 || port >= state.availableOutputChannels)
            return "Output routing refers to an unavailable physical output.";
        if (port < 0) continue;
        if (mask[port]) return "Each physical output can be assigned to only one logical speaker.";
        mask.setBit (port);
    }
    if (state.availableOutputChannels == 0)
        return "The current audio device has no physical output ports.";
    // An active silent output keeps the device clock running when all speakers are disconnected.
    if (mask.isZero()) mask.setBit (0);
    return {};
}
} // namespace

AudioEngine::AudioEngine()
    : AudioEngine (std::make_unique<AudioDeviceManagerBackend>())
{
}

AudioEngine::AudioEngine (std::unique_ptr<IAudioDeviceBackend> backend)
    : deviceBackend (std::move (backend)),
      effectProcessingSource (transportSource, outputPeakLevels, outputGain, outputMeter),
      deviceOutputSource (effectProcessingSource), sourcePlayer (deviceOutputSource)
{
    jassert (deviceBackend != nullptr);
    formatManager.registerBasicFormats();
    updateOutputChannelMixTargets();
    transportSource.setSource (&playbackSource);
    sourcePlayer.setSource (&deviceOutputSource);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

AudioEngine::ScopedPlaybackEdit::ScopedPlaybackEdit (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    engine.effectProcessingSource.beginPlaybackEdit();
}

AudioEngine::ScopedPlaybackEdit::~ScopedPlaybackEdit()
{
    engine.effectProcessingSource.endPlaybackEdit();
}

juce::String AudioEngine::initialise()
{
    if (initialised)
        return {};

    if (! deviceBackend) return "Audio device backend is unavailable.";
    const auto error = deviceBackend->initialiseWithDefaultDevices (0, getChannelCount (outputLayout));
    if (error.isNotEmpty())
        return error;

    sourcePlayer.setSource (&deviceOutputSource);
    deviceBackend->addAudioCallback (&sourcePlayer);
    initialised = true;
    return {};
}

void AudioEngine::shutdown()
{
    if (initialised)
    {
        deviceBackend->removeAudioCallback (&sourcePlayer);
        initialised = false;
    }

    unload();
    readAheadThread.stopThread (1000);
    sourcePlayer.setSource (nullptr);
}

bool AudioEngine::loadFile (const juce::File& file)
{
    return loadPlaybackPlan ({ { { file, 0 } }, -1 });
}

bool AudioEngine::loadPlaybackPlan (AudioPlaybackPlan plan, double startSeconds)
{
    if (! readAheadThread.isThreadRunning() && ! readAheadThread.startThread())
        return false;
    transportSource.stop();
    const ScopedPlaybackEdit edit (*this);
    const auto loaded = playbackSource.loadPlan (std::move (plan), startSeconds);
    if (loaded) outputMeter.reset();
    return loaded;
}

bool AudioEngine::setUpcomingPlayback (std::vector<AudioPlaybackItem> items, int repeatFrom,
                                       std::optional<AudioPlaybackProgress> expected)
{
    return playbackSource.setUpcoming (std::move (items), repeatFrom, expected);
}

AudioPlaybackProgress AudioEngine::getPlaybackProgress() const
{
    return playbackSource.progress();
}

std::vector<AudioPreparationFailure> AudioEngine::drainPreparationFailures()
{
    return playbackSource.drainPreparationFailures();
}

void AudioEngine::unload()
{
    transportSource.stop();
    const ScopedPlaybackEdit edit (*this);
    playbackSource.clear();
    outputMeter.reset();
    for (auto& level : outputPeakLevels)
        level.store (0.0f, std::memory_order_relaxed);
}

void AudioEngine::play()
{
    transportSource.start();
}

void AudioEngine::pause()
{
    transportSource.stop();
}

void AudioEngine::stop()
{
    transportSource.stop();
    static_cast<void> (seekToSeconds (0.0));
}

bool AudioEngine::seekToSeconds (double seconds)
{
    const ScopedPlaybackEdit edit (*this);
    const auto sought = playbackSource.seekToSeconds (juce::jmax (0.0, seconds));
    if (sought) outputMeter.reset();
    return sought;
}

void AudioEngine::setGain (double gainToUse)
{
    outputGain.store (static_cast<float> (juce::jlimit (0.0, 1.0, gainToUse)),
                      std::memory_order_relaxed);
}

std::vector<OutputChannelMix> AudioEngine::getOutputChannelMix() const
{
    return outputChannelMix;
}

bool AudioEngine::setOutputChannelMix (juce::AudioChannelSet::ChannelType channel,
                                      float gain, bool muted, bool solo)
{
    const OutputChannelMix requested { channel, gain, muted, solo };
    if (! isValidOutputChannelMix (requested))
        return false;
    const auto found = std::find_if (outputChannelMix.begin(), outputChannelMix.end(),
        [channel] (const auto& mix) { return mix.channel == channel; });
    jassert (found != outputChannelMix.end());
    if (*found == requested)
        return true;
    *found = requested;
    updateOutputChannelMixTargets();
    return true;
}

void AudioEngine::resetOutputChannelMix()
{
    for (auto& mix : outputChannelMix)
        mix = { mix.channel };
    updateOutputChannelMixTargets();
}

void AudioEngine::updateOutputChannelMixTargets()
{
    const auto channels = toAudioChannelSet (outputLayout);
    const auto hasSolo = std::any_of (outputChannelMix.begin(), outputChannelMix.end(),
        [&] (const auto& mix) { return mix.solo && channels.getChannelIndexForType (mix.channel) >= 0; });
    std::array<float, maximumOutputChannels> targets;
    targets.fill (1.0f);
    for (const auto& mix : outputChannelMix)
        if (const auto index = channels.getChannelIndexForType (mix.channel); index >= 0)
            targets[static_cast<size_t> (index)] = mix.muted || (hasSolo && ! mix.solo) ? 0.0f : mix.gain;
    effectProcessingSource.setChannelGainTargets (targets);
}

juce::String AudioEngine::setOutputLayout (OutputLayout layout,
                                          const std::vector<AudioOutputRoute>& savedRoutes,
                                          juce::String* routingWarning)
{
    if (routingWarning != nullptr) routingWarning->clear();
    if (layout == outputLayout)
    {
        if (initialised && ! savedRoutes.empty())
        {
            ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
            const auto warning = restoreOutputRouting (savedRoutes);
            if (routingWarning != nullptr) *routingWarning = warning;
        }
        return {};
    }

    const auto requestedChannelLayout = toAudioChannelSet (layout);
    const auto previousChannelLayout = toAudioChannelSet (outputLayout);
    if (! initialised)
    {
        juce::String graphError;
        if (! effectProcessingSource.setChannelLayout (requestedChannelLayout, graphError))
            return graphError;

        playbackSource.setDestinationLayout (requestedChannelLayout);
        outputLayout = layout;
        outputRouting = sequentialRouting (requestedChannelLayout.size());
        deviceOutputSource.setRouting (outputRouting);
        updateOutputChannelMixTargets();
        return {};
    }

    const auto device = deviceBackend->getCurrentDeviceState();
    if (! device.open)
        return "No audio output device is active.";

    const auto availableChannels = device.availableOutputChannels;
    if (availableChannels < requestedChannelLayout.size())
        return "The current output device, " + device.name + ", exposes "
            + juce::String (availableChannels) + " output channel"
            + (availableChannels == 1 ? juce::String{} : "s") + "; "
            + requestedChannelLayout.getDescription() + " requires "
            + juce::String (requestedChannelLayout.size()) + ".";

    const auto previousSetup = deviceBackend->getAudioDeviceSetup();
    auto requestedSetup = previousSetup;
    requestedSetup.useDefaultOutputChannels = false;
    requestedSetup.outputChannels.clear();
    requestedSetup.outputChannels.setRange (0, requestedChannelLayout.size(), true);

    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);

    juce::String graphError;
    if (! effectProcessingSource.setChannelLayout (requestedChannelLayout, graphError))
    {
        return graphError;
    }

    playbackSource.setDestinationLayout (requestedChannelLayout);

    auto deviceError = deviceBackend->setAudioDeviceSetup (requestedSetup, true);
    if (deviceError.isEmpty()
        && deviceBackend->getAudioDeviceSetup() != requestedSetup)
        deviceError = "the applied audio device setup did not match the request";
    if (deviceError.isNotEmpty())
    {
        juce::String ignoredGraphError;
        static_cast<void> (effectProcessingSource.setChannelLayout (previousChannelLayout,
                                                                    ignoredGraphError));
        playbackSource.setDestinationLayout (previousChannelLayout);
        const auto rollbackError = deviceBackend->setAudioDeviceSetup (previousSetup, true);
        return setupFailureMessage (requestedChannelLayout.getDescription(),
                                    deviceError, rollbackError);
    }

    outputLayout = layout;
    outputRouting = sequentialRouting (requestedChannelLayout.size());
    deviceOutputSource.setRouting (outputRouting);
    updateOutputChannelMixTargets();
    const auto warning = restoreOutputRouting (savedRoutes);
    if (routingWarning != nullptr) *routingWarning = warning;
    return {};
}

AudioDeviceSettingsView AudioEngine::getDeviceSettings() const
{
    AudioDeviceSettingsView view;
    if (! deviceBackend) return view;
    const auto state = deviceBackend->getCurrentDeviceState();
    view.currentDevice = state.name;
    view.currentDeviceIdentifier = state.identifier.isNotEmpty() ? state.identifier : state.name;
    view.devices = deviceBackend->getOutputDeviceNames();
    view.currentSampleRate = state.sampleRate;
    view.sampleRates = state.sampleRates;
    view.currentBufferSize = state.bufferSize;
    view.bufferSizes = state.bufferSizes;
    view.availableOutputChannels = state.open ? state.availableOutputChannels : 0;
    view.outputRouting = outputRouting;
    for (int channel = 0; channel < view.availableOutputChannels; ++channel)
    {
        auto name = channel < state.outputChannelNames.size()
                        ? state.outputChannelNames[channel].trim() : juce::String {};
        if (name.isEmpty()) name = "Output " + juce::String (channel + 1);
        view.outputChannels.push_back ({ channel, std::move (name) });
    }
    if (state.open)
        for (auto channel = state.activeOutputChannels.findNextSetBit (0); channel >= 0;
             channel = state.activeOutputChannels.findNextSetBit (channel + 1))
        {
            auto name = channel < state.outputChannelNames.size()
                            ? state.outputChannelNames[channel].trim() : juce::String {};
            if (name.isEmpty()) name = "Output " + juce::String (channel + 1);
            view.activeOutputChannels.push_back ({ channel, std::move (name) });
        }
    return view;
}

juce::String AudioEngine::selectOutputDevice (const juce::String& deviceName,
                                             const std::vector<AudioOutputRoute>& savedRoutes,
                                             juce::String* routingWarning)
{
    if (routingWarning != nullptr) routingWarning->clear();
    if (! initialised || ! deviceBackend)
        return "Audio is not initialised.";
    const auto devices = deviceBackend->getOutputDeviceNames();
    if (! devices.contains (deviceName))
        return "Audio output device is not available: " + deviceName;

    const auto previous = deviceBackend->getAudioDeviceSetup();
    if (deviceBackend->getCurrentDeviceState().name == deviceName)
    {
        if (! savedRoutes.empty())
        {
            ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
            const auto warning = restoreOutputRouting (savedRoutes);
            if (routingWarning != nullptr) *routingWarning = warning;
        }
        return {};
    }
    auto requested = previous;
    requested.outputDeviceName = deviceName;
    requested.sampleRate = 0.0;
    requested.bufferSize = 0;
    requested.useDefaultOutputChannels = false;
    requested.outputChannels.clear();
    requested.outputChannels.setRange (0, getChannelCount (outputLayout), true);

    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    const auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty())
    {
        const auto applied = deviceBackend->getCurrentDeviceState();
        const auto appliedSetup = deviceBackend->getAudioDeviceSetup();
        if (applied.open
            && applied.name == deviceName
            && appliedSetup.outputDeviceName == deviceName
            && hasExactOutputRouting (appliedSetup, requested)
            && applied.availableOutputChannels >= getChannelCount (outputLayout))
        {
            outputRouting = sequentialRouting (getChannelCount (outputLayout));
            deviceOutputSource.setRouting (outputRouting);
            const auto warning = restoreOutputRouting (savedRoutes);
            if (routingWarning != nullptr) *routingWarning = warning;
            return {};
        }
    }
    const auto rollbackError = deviceBackend->setAudioDeviceSetup (previous, true);
    return setupFailureMessage ("output device " + deviceName,
                                error.isNotEmpty()
                                    ? error
                                    : "the device does not expose enough output channels",
                                rollbackError);
}

juce::String AudioEngine::setOutputRouting (const std::vector<int>& physicalOutputs)
{
    if (! initialised || ! deviceBackend)
        return "Audio is not initialised.";
    const auto state = deviceBackend->getCurrentDeviceState();
    juce::BigInteger mask;
    if (const auto error = outputRoutingMask (physicalOutputs, getChannelCount (outputLayout), state, mask);
        error.isNotEmpty()) return error;
    const auto previous = deviceBackend->getAudioDeviceSetup();
    auto requested = previous;
    requested.useDefaultOutputChannels = false;
    requested.outputChannels = mask;
    if (physicalOutputs == outputRouting && previous == requested
        && state.activeOutputChannels == mask)
        return {};
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    return applyOutputRouting (physicalOutputs, mask);
}

juce::String AudioEngine::applyOutputRouting (const std::vector<int>& physicalOutputs,
                                             const juce::BigInteger& mask)
{
    const auto previous = deviceBackend->getAudioDeviceSetup();
    auto requested = previous;
    requested.useDefaultOutputChannels = false;
    requested.outputChannels = mask;
    auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty()
        && (deviceBackend->getAudioDeviceSetup() != requested
            || deviceBackend->getCurrentDeviceState().activeOutputChannels != mask))
        error = "the applied output routing did not match the request";
    if (error.isNotEmpty())
    {
        auto rollbackError = deviceBackend->setAudioDeviceSetup (previous, true);
        if (rollbackError.isEmpty() && deviceBackend->getAudioDeviceSetup() != previous)
            rollbackError = "the previous output setup could not be restored";
        return setupFailureMessage ("output routing", error, rollbackError);
    }
    outputRouting = physicalOutputs;
    deviceOutputSource.setRouting (outputRouting);
    return {};
}

juce::String AudioEngine::restoreOutputRouting (const std::vector<AudioOutputRoute>& savedRoutes)
{
    if (savedRoutes.empty()) return {};
    const auto state = deviceBackend->getCurrentDeviceState();
    const auto identifier = state.identifier.isNotEmpty() ? state.identifier : state.name;
    const auto found = std::find_if (savedRoutes.begin(), savedRoutes.end(), [&] (const auto& route)
    {
        return route.deviceIdentifier == identifier && route.layout == outputLayout;
    });
    if (found == savedRoutes.end()) return {};
    juce::BigInteger mask;
    auto error = outputRoutingMask (found->channels, getChannelCount (outputLayout), state, mask);
    if (error.isEmpty()) error = applyOutputRouting (found->channels, mask);
    if (error.isEmpty()) return {};

    const std::vector<int> silent (static_cast<size_t> (getChannelCount (outputLayout)), -1);
    juce::BigInteger silentMask;
    silentMask.setBit (0);
    const auto fallbackError = state.open && state.availableOutputChannels > 0
        ? applyOutputRouting (silent, silentMask) : juce::String {};
    // Even a failed hardware rollback must not expose default output ports.
    outputRouting = silent;
    deviceOutputSource.setRouting (outputRouting);
    return "Could not restore output routing. Outputs are disconnected; choose available ports in Settings. "
        + error + (fallbackError.isNotEmpty() ? " " + fallbackError : juce::String {});
}

juce::String AudioEngine::openDeviceConfiguration()
{
    return deviceBackend ? deviceBackend->openDeviceConfiguration()
                         : "Audio device backend is unavailable.";
}

juce::String AudioEngine::setSampleRate (double sampleRate)
{
    if (! initialised || ! deviceBackend)
        return "Audio is not initialised.";
    const auto state = deviceBackend->getCurrentDeviceState();
    if (! state.open) return "No audio output device is active.";
    if (! std::isfinite (sampleRate)
        || ! containsSampleRate (state.sampleRates, sampleRate))
        return "The current audio device does not support "
            + juce::String (sampleRate, 0) + " Hz.";
    if (std::abs (state.sampleRate - sampleRate) < 0.01) return {};

    const auto previous = deviceBackend->getAudioDeviceSetup();
    auto requested = previous;
    requested.sampleRate = sampleRate;
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    const auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty()
        && deviceBackend->getAudioDeviceSetup() == requested
        && std::abs (deviceBackend->getCurrentDeviceState().sampleRate
                     - sampleRate) < 0.01)
        return {};
    const auto rollbackError = deviceBackend->setAudioDeviceSetup (previous, true);
    return setupFailureMessage (juce::String (sampleRate, 0) + " Hz sample rate",
                                error.isNotEmpty()
                                    ? error : "the applied rate did not match",
                                rollbackError);
}

void AudioEngine::DeviceOutputSource::setRouting (const std::vector<int>& mapping)
{
    routes = mapping;
}

void AudioEngine::DeviceOutputSource::setActiveOutputs (const juce::BigInteger& activeOutputs)
{
    packedDestinations.fill (-1);
    for (size_t logical = 0; logical < routes.size(); ++logical)
    {
        const auto physical = routes[logical];
        if (physical < 0 || ! activeOutputs[physical]) continue;
        auto packed = 0;
        for (auto port = activeOutputs.findNextSetBit (0); port >= 0 && port < physical;
             port = activeOutputs.findNextSetBit (port + 1))
            ++packed;
        packedDestinations[logical] = packed;
    }
}

void AudioEngine::DeviceOutputSource::prepareToPlay (int blockSize, double sampleRate)
{
    logicalBuffer.setSize (static_cast<int> (routes.size()), juce::jmax (1, blockSize));
    logicalSource.prepareToPlay (blockSize, sampleRate);
}

void AudioEngine::DeviceOutputSource::releaseResources()
{
    logicalSource.releaseResources();
}

void AudioEngine::DeviceOutputSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& output)
{
    if (output.buffer == nullptr || output.numSamples <= 0) return;
    output.clearActiveBufferRegion();
    for (auto offset = 0; offset < output.numSamples;)
    {
        const auto samples = juce::jmin (logicalBuffer.getNumSamples(), output.numSamples - offset);
        logicalSource.getNextAudioBlock ({ &logicalBuffer, 0, samples });
        for (int logical = 0; logical < logicalBuffer.getNumChannels(); ++logical)
            if (const auto packed = packedDestinations[static_cast<size_t> (logical)];
                packed >= 0 && packed < output.buffer->getNumChannels())
                output.buffer->copyFrom (packed, output.startSample + offset,
                                         logicalBuffer, logical, 0, samples);
        offset += samples;
    }
}

void AudioEngine::RoutedSourcePlayer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    outputSource.setActiveOutputs (device->getActiveOutputChannels());
    juce::AudioSourcePlayer::audioDeviceAboutToStart (device);
}

juce::String AudioEngine::setBufferSize (int bufferSize)
{
    if (! initialised || ! deviceBackend)
        return "Audio is not initialised.";
    const auto state = deviceBackend->getCurrentDeviceState();
    if (! state.open) return "No audio output device is active.";
    if (! state.bufferSizes.contains (bufferSize))
        return "The current audio device does not support a "
            + juce::String (bufferSize) + "-sample buffer.";
    if (state.bufferSize == bufferSize) return {};

    const auto previous = deviceBackend->getAudioDeviceSetup();
    auto requested = previous;
    requested.bufferSize = bufferSize;
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    const auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty()
        && deviceBackend->getAudioDeviceSetup() == requested
        && deviceBackend->getCurrentDeviceState().bufferSize == bufferSize)
        return {};
    const auto rollbackError = deviceBackend->setAudioDeviceSetup (previous, true);
    return setupFailureMessage (juce::String (bufferSize) + "-sample buffer",
                                error.isNotEmpty()
                                    ? error : "the applied buffer size did not match",
                                rollbackError);
}

bool AudioEngine::isPlaying() const
{
    return transportSource.isPlaying();
}

bool AudioEngine::hasReadError() const
{
    return playbackSource.progress().readError;
}

bool AudioEngine::hasStreamFinished() const
{
    return playbackSource.progress().finished;
}

double AudioEngine::getCurrentPositionSeconds() const
{
    return playbackSource.progress().positionSeconds;
}

double AudioEngine::getLengthInSeconds() const
{
    return playbackSource.progress().lengthSeconds;
}

double AudioEngine::getGain() const
{
    return static_cast<double> (outputGain.load (std::memory_order_relaxed));
}

OutputLayout AudioEngine::getOutputLayout() const
{
    return outputLayout;
}

int AudioEngine::getAvailableOutputChannelCount() const
{
    if (! deviceBackend) return 0;
    const auto state = deviceBackend->getCurrentDeviceState();
    return state.open ? state.availableOutputChannels : 0;
}

juce::String AudioEngine::getCurrentAudioDeviceName() const
{
    return deviceBackend ? deviceBackend->getCurrentDeviceState().name
                         : juce::String{};
}

double AudioEngine::getCurrentAudioSampleRate() const
{
    return deviceBackend ? deviceBackend->getCurrentDeviceState().sampleRate : 0.0;
}

int AudioEngine::getCurrentAudioBufferSize() const
{
    return deviceBackend ? deviceBackend->getCurrentDeviceState().bufferSize : 0;
}

std::array<float, maximumOutputChannels> AudioEngine::getOutputPeakLevels() const
{
    std::array<float, maximumOutputChannels> result {};
    for (size_t channel = 0; channel < result.size(); ++channel)
        result[channel] = outputPeakLevels[channel].load (std::memory_order_relaxed);

    return result;
}

OutputMeterReadings AudioEngine::getOutputMeterReadings() const
{
    return outputMeter.getReadings();
}

void AudioEngine::resetOutputMeter() noexcept
{
    outputMeter.reset();
}

bool AudioEngine::addEffectPlugin (int slotId,
                                   std::unique_ptr<juce::AudioPluginInstance> plugin,
                                   juce::String& errorMessage,
                                   EffectScope scope)
{
    return effectProcessingSource.addPlugin (slotId, std::move (plugin), errorMessage, std::move (scope));
}

bool AudioEngine::removeEffectPlugin (int slotId)
{
    return effectProcessingSource.removePlugin (slotId);
}

bool AudioEngine::clearEffectPlugins()
{
    return effectProcessingSource.clearPlugins();
}

bool AudioEngine::moveEffectPlugin (int fromIndex, int toIndex)
{
    return effectProcessingSource.movePlugin (fromIndex, toIndex);
}

bool AudioEngine::setEffectPluginOrder (const std::vector<int>& orderedSlotIds)
{
    return effectProcessingSource.setPluginOrder (orderedSlotIds);
}

bool AudioEngine::setEffectBypassed (int slotId, bool shouldBypass)
{
    return effectProcessingSource.setBypassed (slotId, shouldBypass);
}

void AudioEngine::setAllEffectsBypassed (bool value) noexcept
{
    effectProcessingSource.setAllBypassed (value);
}

bool AudioEngine::areAllEffectsBypassed() const noexcept
{
    return effectProcessingSource.areAllBypassed();
}

bool AudioEngine::clearEffectTails (juce::String& errorMessage)
{
    return effectProcessingSource.clearTails (errorMessage);
}

bool AudioEngine::setEffectScope (int slotId, const EffectScope& scope, juce::String& errorMessage)
{
    return effectProcessingSource.setScope (slotId, scope, errorMessage);
}

int AudioEngine::getEffectPluginCount() const
{
    return effectProcessingSource.getPluginCount();
}

juce::AudioPluginInstance* AudioEngine::getEffectPlugin (int slotId) const
{
    return effectProcessingSource.getPlugin (slotId);
}

bool AudioEngine::beginEffectStateCapture (int timeoutMilliseconds)
{
    return effectProcessingSource.beginStateCapture (timeoutMilliseconds);
}

void AudioEngine::endEffectStateCapture() noexcept
{
    effectProcessingSource.endStateCapture();
}

AudioEngine::EffectProcessingSource::EffectProcessingSource (
    juce::AudioTransportSource& sourceToWrap,
    std::array<std::atomic<float>, maximumOutputChannels>& outputPeakLevelsToUse,
    std::atomic<float>& outputGainToUse,
    OutputMeter& outputMeterToUse)
    : transport (sourceToWrap),
      outputPeakLevels (outputPeakLevelsToUse),
      outputGain (outputGainToUse),
      outputMeter (outputMeterToUse)
{
    smoothedGain.setCurrentAndTargetValue (outputGain.load (std::memory_order_relaxed));
    for (size_t channel = 0; channel < channelGainTargets.size(); ++channel)
    {
        channelGainTargets[channel].store (1.0f, std::memory_order_relaxed);
        smoothedChannelGains[channel].setCurrentAndTargetValue (1.0f);
    }
}

void AudioEngine::EffectProcessingSource::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
    effectGraph.prepareToPlay (sampleRate, samplesPerBlockExpected, channelLayout);
    dryBuffer.setSize (maximumOutputChannels, samplesPerBlockExpected);
    effectWetMix.reset (sampleRate, 0.02);
    effectWetMix.setCurrentAndTargetValue (areAllBypassed() ? 0.0f : 1.0f);
    outputMeter.prepare (sampleRate, channelLayout);
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (outputGain.load (std::memory_order_relaxed));
    for (size_t channel = 0; channel < smoothedChannelGains.size(); ++channel)
    {
        smoothedChannelGains[channel].reset (sampleRate, 0.02);
        smoothedChannelGains[channel].setCurrentAndTargetValue (
            channelGainTargets[channel].load (std::memory_order_relaxed));
    }
}

void AudioEngine::EffectProcessingSource::releaseResources()
{
    effectGraph.releaseResources();
    transport.releaseResources();
    outputMeter.release();
    for (auto& level : outputPeakLevels)
        level.store (0.0f, std::memory_order_relaxed);
}

void AudioEngine::EffectProcessingSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    const auto meterEpoch = outputMeter.beginBlock();
    bufferToFill.clearActiveBufferRegion();
    if (! playbackEditRequested.load())
    {
        activeTransportCallbacks.fetch_add (1);
        if (! playbackEditRequested.load())
            transport.getNextAudioBlock (bufferToFill);
        activeTransportCallbacks.fetch_sub (1);
    }

    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
        return;

    std::array<float*, 64> channelPointers {};
    const auto channelCount = juce::jmin (bufferToFill.buffer->getNumChannels(),
                                         static_cast<int> (channelPointers.size()));

    for (int channel = 0; channel < channelCount; ++channel)
        channelPointers[static_cast<size_t> (channel)] = bufferToFill.buffer->getWritePointer (channel,
                                                                                              bufferToFill.startSample);

    juce::AudioBuffer<float> processBuffer (channelPointers.data(),
                                            channelCount,
                                            bufferToFill.numSamples);
    midiBuffer.clear();
    effectWetMix.setTargetValue (areAllBypassed() ? 0.0f : 1.0f);
    const auto blendDry = effectWetMix.isSmoothing() || effectWetMix.getTargetValue() == 0.0f;
    // Keep processing during monitoring bypass so delay/reverb state does not freeze.
    effectGraph.processBlock (processBuffer, midiBuffer, blendDry ? &dryBuffer : nullptr);
    if (effectWetMix.isSmoothing())
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
        {
            const auto wet = effectWetMix.getNextValue();
            for (int channel = 0; channel < channelCount; ++channel)
                processBuffer.setSample (channel, sample,
                    wet * processBuffer.getSample (channel, sample)
                    + (1.0f - wet) * dryBuffer.getSample (channel, sample));
        }
    else if (blendDry)
        for (int channel = 0; channel < channelCount; ++channel)
            processBuffer.copyFrom (channel, 0, dryBuffer, channel, 0, bufferToFill.numSamples);

    for (int channel = 0; channel < juce::jmin (channelCount, maximumOutputChannels); ++channel)
    {
        auto& gain = smoothedChannelGains[static_cast<size_t> (channel)];
        gain.setTargetValue (channelGainTargets[static_cast<size_t> (channel)].load (std::memory_order_relaxed));
        gain.applyGain (processBuffer.getWritePointer (channel), bufferToFill.numSamples);
    }

    const auto startGain = smoothedGain.getCurrentValue();
    smoothedGain.setTargetValue (outputGain.load (std::memory_order_relaxed));
    const auto endGain = smoothedGain.skip (bufferToFill.numSamples);
    processBuffer.applyGainRamp (0, bufferToFill.numSamples, startGain, endGain);
    outputMeter.push (processBuffer, 0, bufferToFill.numSamples, meterEpoch);

    for (size_t channel = 0; channel < outputPeakLevels.size(); ++channel)
    {
        const auto previous = outputPeakLevels[channel].load (std::memory_order_relaxed);
        const auto current = channel < static_cast<size_t> (channelCount)
                                 ? processBuffer.getMagnitude (static_cast<int> (channel), 0,
                                                               bufferToFill.numSamples)
                                 : 0.0f;
        outputPeakLevels[channel].store (juce::jmax (current, previous * 0.88f),
                                         std::memory_order_relaxed);
    }
}

void AudioEngine::EffectProcessingSource::beginPlaybackEdit()
{
    if (playbackEditDepth++ > 0) return;
    playbackEditRequested.store (true);
    // The second callback-side check closes the race with entering this gate.
    // Only source consumption is awaited; plug-ins continue processing normally.
    while (activeTransportCallbacks.load() != 0)
        juce::Thread::sleep (1);
}

void AudioEngine::EffectProcessingSource::endPlaybackEdit()
{
    jassert (playbackEditDepth > 0);
    if (--playbackEditDepth == 0)
        playbackEditRequested.store (false);
}

bool AudioEngine::EffectProcessingSource::addPlugin (int slotId,
                                                     std::unique_ptr<juce::AudioPluginInstance> plugin,
                                                     juce::String& errorMessage,
                                                     EffectScope scope)
{
    return effectGraph.addProcessor (slotId, std::move (plugin), errorMessage, std::move (scope));
}

bool AudioEngine::EffectProcessingSource::removePlugin (int slotId)
{
    return effectGraph.removeProcessor (slotId);
}

bool AudioEngine::EffectProcessingSource::clearPlugins()
{
    return effectGraph.clearProcessors();
}

bool AudioEngine::EffectProcessingSource::movePlugin (int fromIndex, int toIndex)
{
    return effectGraph.moveProcessor (fromIndex, toIndex);
}

bool AudioEngine::EffectProcessingSource::setPluginOrder (const std::vector<int>& orderedSlotIds)
{
    return effectGraph.setProcessorOrder (orderedSlotIds);
}

bool AudioEngine::EffectProcessingSource::setBypassed (int slotId, bool shouldBypass)
{
    return effectGraph.setBypassed (slotId, shouldBypass);
}

bool AudioEngine::EffectProcessingSource::setScope (int slotId, const EffectScope& scope, juce::String& errorMessage)
{
    return effectGraph.setProcessorScope (slotId, scope, errorMessage);
}

bool AudioEngine::EffectProcessingSource::setChannelLayout (const juce::AudioChannelSet& layout,
                                                            juce::String& errorMessage)
{
    if (! effectGraph.setChannelLayout (layout, errorMessage))
        return false;

    channelLayout = layout;
    return true;
}

void AudioEngine::EffectProcessingSource::setChannelGainTargets (
    const std::array<float, maximumOutputChannels>& targets) noexcept
{
    for (size_t channel = 0; channel < targets.size(); ++channel)
        channelGainTargets[channel].store (targets[channel], std::memory_order_relaxed);
}

int AudioEngine::EffectProcessingSource::getPluginCount() const
{
    return effectGraph.size();
}

juce::AudioPluginInstance* AudioEngine::EffectProcessingSource::getPlugin (int slotId) const
{
    return dynamic_cast<juce::AudioPluginInstance*> (effectGraph.getProcessor (slotId));
}

bool AudioEngine::EffectProcessingSource::beginStateCapture (
    int timeoutMilliseconds)
{
    return effectGraph.beginStateCapture (timeoutMilliseconds);
}

void AudioEngine::EffectProcessingSource::endStateCapture() noexcept
{
    effectGraph.endStateCapture();
}
