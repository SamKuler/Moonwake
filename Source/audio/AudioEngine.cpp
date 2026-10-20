#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <exception>
#include <set>

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

bool sameOutputDeviceState (const AudioDeviceBackendState& actual, const AudioDeviceBackendState& expected)
{
    const auto identity = [] (const auto& state) { return state.identifier.isNotEmpty() ? state.identifier : state.name; };
    return actual.open && identity (actual) == identity (expected)
        && actual.activeOutputChannels == expected.activeOutputChannels
        && std::abs (actual.sampleRate - expected.sampleRate) < 0.01
        && actual.bufferSize == expected.bufferSize;
}

template <typename Player>
class ScopedAudioCallbackPause final
{
public:
    ScopedAudioCallbackPause (IAudioDeviceBackend& backendToUse,
                              Player& callbackToUse)
        : backend (backendToUse), callback (callbackToUse),
          previousSetup (backend.getAudioDeviceSetup()), previousState (backend.getCurrentDeviceState()),
          wasIsolated (callback.isOutputIsolated())
    {
        callback.beginConfiguration();
        backend.removeAudioCallback (&callback);
    }

    ~ScopedAudioCallbackPause()
    {
        backend.addAudioCallback (&callback);
        callback.endConfiguration();
    }

    juce::Result prepareOutput (bool force = false)
    {
        const auto state = backend.getCurrentDeviceState();
        if (! state.open) return juce::Result::fail ("No audio output device is active.");
        const auto error = callback.prepareOutput (state.sampleRate, state.bufferSize, force);
        preparationFailed = error.isNotEmpty();
        return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
    }

    juce::String recoverPreparation()
    {
        return preparationFailed && ! wasIsolated ? prepareOutput().getErrorMessage() : juce::String {};
    }

    juce::String restoreDevice()
    {
        auto error = backend.setAudioDeviceSetup (previousSetup, true);
        const auto restored = backend.getCurrentDeviceState();
        if (error.isEmpty() && (backend.getAudioDeviceSetup() != previousSetup
            || ! sameOutputDeviceState (restored, previousState)))
            error = "the previous device setup could not be restored";
        if (error.isNotEmpty())
        {
            callback.isolateOutput();
            error += ". Outputs are disconnected; select an available device and output routing";
        }
        return error;
    }

private:
    IAudioDeviceBackend& backend;
    Player& callback;
    juce::AudioDeviceManager::AudioDeviceSetup previousSetup;
    AudioDeviceBackendState previousState;
    bool wasIsolated = false, preparationFailed = false;
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
      effectProcessingSource (transportSource, playbackSource, outputPeakLevels, outputGain, outputMeter),
      deviceOutputSource (effectProcessingSource), sourcePlayer (deviceOutputSource, deviceConfigurationRevision)
{
    jassert (deviceBackend != nullptr);
    formatManager.registerBasicFormats();
    updateOutputCalibrationTargets();
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
    stopCalibrationTestSignal();
    if (initialised)
    {
        deviceBackend->removeAudioCallback (&sourcePlayer);
        initialised = false;
    }

    unload();
    readAheadThread.stopThread (1000);
    sourcePlayer.setSource (nullptr);
    sourcePlayer.resetDeviceState();
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
    if (loaded) resetPlaybackMeasurements();
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
    resetPlaybackMeasurements();
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
    if (sought) resetPlaybackMeasurements();
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
    for (const auto& setting : outputCalibration)
        if (const auto index = channels.getChannelIndexForType (setting.channel); index >= 0)
            targets[static_cast<size_t> (index)] *= juce::Decibels::decibelsToGain (setting.trimDb);
    for (int channel = 0; channel < channels.size(); ++channel)
        if (static_cast<size_t> (channel) >= outputRouting.size() || outputRouting[static_cast<size_t> (channel)] < 0)
            targets[static_cast<size_t> (channel)] = 0.0f;
    effectProcessingSource.comparison.setChannelWeights (targets);
}

std::vector<OutputChannelCalibration> AudioEngine::getOutputCalibration() const
{
    return outputCalibration;
}

juce::String AudioEngine::setOutputCalibration (juce::AudioChannelSet::ChannelType channel,
                                                float trimDb, double delayMs, bool inverted)
{
    const OutputChannelCalibration requested { channel, trimDb, delayMs, inverted };
    if (! isValidOutputCalibration (requested))
        return "Calibration requires a supported speaker, -24 to +12 dB trim and 0 to 200 ms delay.";
    const auto found = std::find_if (outputCalibration.begin(), outputCalibration.end(),
        [channel] (const auto& setting) { return setting.channel == channel; });
    *found = requested;
    updateOutputCalibrationTargets();
    return {};
}

void AudioEngine::resetOutputCalibration()
{
    for (auto& setting : outputCalibration) setting = { setting.channel };
    updateOutputCalibrationTargets();
}

void AudioEngine::updateOutputCalibrationTargets()
{
    effectProcessingSource.calibration.setCalibration (toAudioChannelSet (outputLayout), outputCalibration);
    updateOutputChannelMixTargets();
}

juce::String AudioEngine::validateCalibrationTestSignal (const CalibrationTestSignal& signal) const
{
    if (signal.kind == CalibrationTestSignal::Kind::off) return {};
    if (! initialised)
        return "No audio output device is active.";
    const auto device = deviceBackend->getCurrentDeviceState();
    if (! device.open)
        return "No audio output device is active.";
    if (sourcePlayer.isOutputIsolated())
        return "Restore an audio output device before starting a calibration test.";
    if ((signal.kind != CalibrationTestSignal::Kind::pinkNoise && signal.kind != CalibrationTestSignal::Kind::sine)
        || toAudioChannelSet (outputLayout).getChannelIndexForType (signal.channel) < 0
        || ! std::isfinite (signal.levelDbFS) || signal.levelDbFS < -60.0f || signal.levelDbFS > -12.0f
        || ! std::isfinite (signal.frequencyHz) || signal.frequencyHz < 20.0
        || signal.frequencyHz > juce::jmin (20000.0, device.sampleRate * 0.45))
        return "Choose an active speaker, a -60 to -12 dBFS test level and a supported test frequency.";
    const auto logicalChannel = toAudioChannelSet (outputLayout).getChannelIndexForType (signal.channel);
    const auto physicalOutput = outputRouting[static_cast<size_t> (logicalChannel)];
    if (physicalOutput < 0 || physicalOutput >= device.availableOutputChannels
        || ! device.activeOutputChannels[physicalOutput])
        return "This speaker has no physical output. Connect it to an output port in Settings before starting a test.";
    return {};
}

juce::String AudioEngine::setCalibrationTestSignal (CalibrationTestSignal signal)
{
    if (const auto error = validateCalibrationTestSignal (signal); error.isNotEmpty()) return error;
    if (signal.kind == CalibrationTestSignal::Kind::off)
    {
        stopCalibrationTestSignal();
        return {};
    }
    if ((signal.channel == juce::AudioChannelSet::LFE || signal.channel == juce::AudioChannelSet::LFE2)
        && signal.frequencyHz > 120.0)
        signal.frequencyHz = 60.0;
    if (effectProcessingSource.calibration.isTestSignalActive()
        && signal.kind == calibrationTestSignal.kind && signal.channel == calibrationTestSignal.channel
        && juce::exactlyEqual (signal.levelDbFS, calibrationTestSignal.levelDbFS)
        && juce::exactlyEqual (signal.frequencyHz, calibrationTestSignal.frequencyHz))
        return {};
    calibrationTestSignal = signal;
    effectProcessingSource.calibration.setTestSignal (signal);
    return {};
}

void AudioEngine::stopCalibrationTestSignal() noexcept
{
    calibrationTestSignal.kind = CalibrationTestSignal::Kind::off;
    effectProcessingSource.calibration.stopTestSignal();
}

CalibrationTestSignal AudioEngine::getCalibrationTestSignal() const
{
    auto result = calibrationTestSignal;
    if (! effectProcessingSource.calibration.isTestSignalActive()) result.kind = CalibrationTestSignal::Kind::off;
    return result;
}

std::unique_ptr<EffectGraph> AudioEngine::prepareEffectGraph (
    std::vector<PreparedEffect> effects, const juce::AudioChannelSet& layout,
    double sampleRate, int bufferSize, juce::String& error)
{
    try
    {
        auto graph = std::make_unique<EffectGraph>();
        if (! graph->setChannelLayout (layout, error)) return {};
        graph->prepareToPlay (sampleRate > 0.0 ? sampleRate : 48000.0,
                              bufferSize > 0 ? bufferSize : 512, layout);
        for (auto& effect : effects)
        {
            if (! graph->addProcessor (effect.slotId, std::move (effect.plugin), error, effect.scope))
                return {};
            static_cast<void> (graph->setBypassed (effect.slotId, effect.bypassed));
        }
        return graph;
    }
    catch (const std::exception& exception)
    {
        error = "Could not prepare the effect chain: " + juce::String (exception.what());
    }
    catch (...)
    {
        error = "Could not prepare the effect chain.";
    }
    return {};
}

juce::String AudioEngine::replaceEffectPlugins (std::vector<PreparedEffect> effects)
{
    if (initialised && sourcePlayer.isOutputIsolated())
        return "Restore an audio output device before replacing the effect chain.";
    juce::String error;
    const auto layout = toAudioChannelSet (outputLayout);
    auto graph = prepareEffectGraph (std::move (effects), layout,
                                    getCurrentAudioSampleRate(), getCurrentAudioBufferSize(), error);
    if (graph == nullptr) return error;
    std::unique_ptr<EffectGraph> previousGraph;
    if (initialised)
    {
        stopCalibrationTestSignal();
        ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
        previousGraph = effectProcessingSource.replaceGraph (std::move (graph), layout,
            getCurrentAudioSampleRate(), getCurrentAudioBufferSize());
    }
    else
        previousGraph = effectProcessingSource.replaceGraph (std::move (graph), layout, 0.0, 0);
    resetPlaybackMeasurements();
    return {};
}

juce::String AudioEngine::applyListeningConfiguration (const ListeningOutputConfiguration& configuration,
                                                       std::vector<PreparedEffect> effects)
{
    if (! initialised || ! deviceBackend) return "Audio is not initialised.";
    if (std::none_of (outputLayouts().begin(), outputLayouts().end(), [&] (const auto& layout)
        { return layout.layout == configuration.layout; })) return "The requested output layout is unavailable.";
    if (! std::isfinite (configuration.sampleRate) || configuration.sampleRate < 0.0 || configuration.bufferSize < 0)
        return "The listening configuration contains invalid device timing.";
    const auto layout = toAudioChannelSet (configuration.layout);
    if (configuration.routing.size() != static_cast<size_t> (layout.size()))
        return "Output routing must contain one destination for every logical speaker.";
    auto nextMix = defaultOutputChannelMix();
    auto nextCalibration = defaultOutputCalibration();
    std::set<juce::AudioChannelSet::ChannelType> mixSpeakers, calibrationSpeakers;
    for (const auto& mix : configuration.channelMix)
    {
        if (! isValidOutputChannelMix (mix) || ! mixSpeakers.insert (mix.channel).second)
            return "The listening configuration contains invalid channel mix settings.";
        *std::find_if (nextMix.begin(), nextMix.end(), [&] (const auto& value) { return value.channel == mix.channel; }) = mix;
    }
    for (const auto& setting : configuration.calibration)
    {
        if (! isValidOutputCalibration (setting) || ! calibrationSpeakers.insert (setting.channel).second)
            return "The listening configuration contains invalid speaker calibration.";
        *std::find_if (nextCalibration.begin(), nextCalibration.end(), [&] (const auto& value)
            { return value.channel == setting.channel; }) = setting;
    }
    const auto before = deviceBackend->getCurrentDeviceState();
    if (! before.open) return "No audio output device is active.";
    const auto beforeIdentifier = before.identifier.isNotEmpty() ? before.identifier : before.name;
    const auto deviceName = configuration.deviceIdentifier == beforeIdentifier ? before.name : configuration.deviceName;
    if (! deviceBackend->getOutputDeviceNames().contains (deviceName))
        return "Audio output device is not available: " + deviceName;
    const auto previousSetup = deviceBackend->getAudioDeviceSetup();
    auto requested = previousSetup;
    requested.outputDeviceName = deviceName;
    requested.sampleRate = configuration.sampleRate;
    requested.bufferSize = configuration.bufferSize;
    requested.useDefaultOutputChannels = false;
    requested.outputChannels.clear();
    for (const auto port : configuration.routing)
    {
        if (port < -1 || (deviceName == before.name && port >= before.availableOutputChannels))
            return "Output routing refers to an unavailable physical output.";
        if (port >= 0)
        {
            if (requested.outputChannels[port]) return "Each physical output can be assigned to only one logical speaker.";
            requested.outputChannels.setBit (port);
        }
    }
    if (requested.outputChannels.isZero()) requested.outputChannels.setBit (0);
    juce::String error;
    auto graph = prepareEffectGraph (std::move (effects), layout,
        requested.sampleRate > 0.0 ? requested.sampleRate : before.sampleRate,
        requested.bufferSize > 0 ? requested.bufferSize : before.bufferSize, error);
    if (graph == nullptr) return error;

    std::unique_ptr<EffectGraph> previousGraph;
    const auto previousLayout = outputLayout;
    const auto previousMix = outputChannelMix;
    const auto previousCalibration = outputCalibration;
    stopCalibrationTestSignal();
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    error = deviceBackend->setAudioDeviceSetup (requested, true);
    const auto applied = deviceBackend->getCurrentDeviceState();
    const auto appliedSetup = deviceBackend->getAudioDeviceSetup();
    const auto identifier = applied.identifier.isNotEmpty() ? applied.identifier : applied.name;
    juce::BigInteger actualMask;
    if (error.isEmpty()) error = outputRoutingMask (configuration.routing, layout.size(), applied, actualMask);
    if (error.isEmpty() && (! applied.open || applied.name != deviceName
        || (configuration.deviceIdentifier.isNotEmpty() && identifier != configuration.deviceIdentifier)
        || appliedSetup.outputDeviceName != deviceName || ! hasExactOutputRouting (appliedSetup, requested)
        || applied.activeOutputChannels != actualMask || applied.sampleRate <= 0.0 || applied.bufferSize <= 0
        || (configuration.sampleRate > 0.0 && std::abs (applied.sampleRate - configuration.sampleRate) >= 0.01)
        || (configuration.bufferSize > 0 && applied.bufferSize != configuration.bufferSize)))
        error = "the applied audio device configuration did not match the listening configuration";
    if (error.isEmpty())
    {
        try { graph->prepareToPlay (applied.sampleRate, applied.bufferSize, layout); }
        catch (...) { error = "the effect chain could not prepare for the applied audio device"; }
    }
    if (error.isEmpty())
        error = playbackSource.reconfigureOutput (layout, applied.sampleRate, [&]
        {
            previousGraph = effectProcessingSource.replaceGraph (std::move (graph), layout,
                                                                  applied.sampleRate, applied.bufferSize);
            deviceOutputSource.setRouting (configuration.routing);
            outputLayout = configuration.layout;
            outputChannelMix = nextMix;
            outputCalibration = nextCalibration;
            updateOutputCalibrationTargets();
            return callbackPause.prepareOutput (true);
        }).getErrorMessage();
    if (error.isNotEmpty())
    {
        if (previousGraph != nullptr)
        {
            graph = effectProcessingSource.replaceGraph (std::move (previousGraph), toAudioChannelSet (previousLayout),
                                                         before.sampleRate, before.bufferSize);
            outputLayout = previousLayout;
            outputChannelMix = previousMix;
            outputCalibration = previousCalibration;
            updateOutputCalibrationTargets();
            deviceOutputSource.setRouting (outputRouting);
            updateOutputChannelMixTargets();
        }
        auto rollbackError = callbackPause.restoreDevice();
        if (rollbackError.isEmpty()) rollbackError = callbackPause.recoverPreparation();
        if (rollbackError.isNotEmpty())
        {
            outputRouting.assign (static_cast<size_t> (getChannelCount (outputLayout)), -1);
            deviceOutputSource.setRouting (outputRouting);
            updateOutputChannelMixTargets();
        }
        return setupFailureMessage ("the listening configuration", error, rollbackError);
    }
    outputRouting = configuration.routing;
    deviceOutputSource.setRouting (outputRouting);
    updateOutputChannelMixTargets();
    resetPlaybackMeasurements();
    return {};
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
            stopCalibrationTestSignal();
            ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
            const auto warning = restoreOutputRouting (savedRoutes);
            if (routingWarning != nullptr) *routingWarning = warning;
        }
        return {};
    }

    const auto requestedChannelLayout = toAudioChannelSet (layout);
    const auto previousOutputLayout = outputLayout;
    const auto previousChannelLayout = toAudioChannelSet (outputLayout);
    if (! initialised)
    {
        juce::String graphError;
        if (! effectProcessingSource.setChannelLayout (requestedChannelLayout, graphError))
            return graphError;

        playbackSource.setDestinationLayout (requestedChannelLayout);
        outputLayout = layout;
        updateOutputCalibrationTargets();
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

    stopCalibrationTestSignal();
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);

    auto deviceError = deviceBackend->setAudioDeviceSetup (requestedSetup, true);
    if (deviceError.isEmpty()
        && deviceBackend->getAudioDeviceSetup() != requestedSetup)
        deviceError = "the applied audio device setup did not match the request";
    bool graphChanged = false;
    if (deviceError.isEmpty())
        deviceError = playbackSource.reconfigureOutput (requestedChannelLayout,
            deviceBackend->getCurrentDeviceState().sampleRate, [&]
        {
            juce::String graphError;
            try
            {
                if (! effectProcessingSource.setChannelLayout (requestedChannelLayout, graphError))
                    return juce::Result::fail (graphError);
                graphChanged = true;
                deviceOutputSource.setRouting (sequentialRouting (requestedChannelLayout.size()));
                outputLayout = layout;
                updateOutputCalibrationTargets();
                return callbackPause.prepareOutput (true);
            }
            catch (...)
            {
                graphChanged = true;
                sourcePlayer.invalidatePreparedOutput();
                return juce::Result::fail ("The effect chain could not prepare the requested output layout.");
            }
        }).getErrorMessage();
    if (deviceError.isNotEmpty())
    {
        outputLayout = previousOutputLayout;
        updateOutputCalibrationTargets();
        deviceOutputSource.setRouting (outputRouting);
        updateOutputChannelMixTargets();
        const auto rollbackError = callbackPause.restoreDevice();
        if (graphChanged)
        {
            juce::String graphError;
            try
            {
                if (! effectProcessingSource.setChannelLayout (previousChannelLayout, graphError)
                    || (rollbackError.isEmpty() && callbackPause.prepareOutput().failed()))
                    sourcePlayer.invalidatePreparedOutput();
            }
            catch (...) { sourcePlayer.invalidatePreparedOutput(); }
        }
        return setupFailureMessage (requestedChannelLayout.getDescription(),
                                    deviceError, rollbackError);
    }

    outputLayout = layout;
    updateOutputCalibrationTargets();
    outputRouting = sequentialRouting (requestedChannelLayout.size());
    deviceOutputSource.setRouting (outputRouting);
    updateOutputChannelMixTargets();
    const auto warning = restoreOutputRouting (savedRoutes);
    if (routingWarning != nullptr) *routingWarning = warning;
    resetPlaybackMeasurements();
    return {};
}

uint64_t AudioEngine::getDeviceConfigurationRevision() const noexcept
{
    return deviceConfigurationRevision.load (std::memory_order_relaxed);
}

juce::String AudioEngine::takeOutputError()
{
    return sourcePlayer.takePreparationFailure()
        ? "The audio chain could not prepare after an audio device change. Outputs are disconnected; select an available audio device."
        : juce::String {};
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
    if (deviceBackend->getCurrentDeviceState().name == deviceName && ! sourcePlayer.isOutputIsolated())
    {
        if (! savedRoutes.empty())
        {
            stopCalibrationTestSignal();
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

    stopCalibrationTestSignal();
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    auto error = deviceBackend->setAudioDeviceSetup (requested, true);
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
            error = playbackSource.reconfigureOutput (toAudioChannelSet (outputLayout), applied.sampleRate, [&]
            {
                deviceOutputSource.setRouting (sequentialRouting (getChannelCount (outputLayout)));
                return callbackPause.prepareOutput();
            }).getErrorMessage();
            if (error.isEmpty())
            {
                outputRouting = sequentialRouting (getChannelCount (outputLayout));
                deviceOutputSource.setRouting (outputRouting);
                updateOutputChannelMixTargets();
                const auto warning = restoreOutputRouting (savedRoutes);
                if (routingWarning != nullptr) *routingWarning = warning;
                resetPlaybackMeasurements();
                return {};
            }
        }
    }
    deviceOutputSource.setRouting (outputRouting);
    updateOutputChannelMixTargets();
    auto rollbackError = callbackPause.restoreDevice();
    if (rollbackError.isEmpty()) rollbackError = callbackPause.recoverPreparation();
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
    stopCalibrationTestSignal();
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    return applyOutputRouting (physicalOutputs, mask);
}

juce::String AudioEngine::applyOutputRouting (const std::vector<int>& physicalOutputs,
                                             const juce::BigInteger& mask)
{
    const auto previous = deviceBackend->getAudioDeviceSetup();
    const auto previousState = deviceBackend->getCurrentDeviceState();
    auto expectedState = previousState;
    expectedState.activeOutputChannels = mask;
    auto requested = previous;
    requested.useDefaultOutputChannels = false;
    requested.outputChannels = mask;
    auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty()
        && (deviceBackend->getAudioDeviceSetup() != requested
            || ! sameOutputDeviceState (deviceBackend->getCurrentDeviceState(), expectedState)))
        error = "the applied output routing did not match the request";
    if (error.isNotEmpty())
    {
        auto rollbackError = deviceBackend->setAudioDeviceSetup (previous, true);
        if (rollbackError.isEmpty() && (deviceBackend->getAudioDeviceSetup() != previous
            || ! sameOutputDeviceState (deviceBackend->getCurrentDeviceState(), previousState)))
            rollbackError = "the previous output setup could not be restored";
        if (rollbackError.isNotEmpty())
        {
            sourcePlayer.isolateOutput();
            rollbackError += ". Outputs are disconnected; restore an audio output device";
        }
        return setupFailureMessage ("output routing", error, rollbackError);
    }
    outputRouting = physicalOutputs;
    deviceOutputSource.setRouting (outputRouting);
    updateOutputChannelMixTargets();
    resetPlaybackMeasurements();
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
    updateOutputChannelMixTargets();
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
    if (std::abs (state.sampleRate - sampleRate) < 0.01 && ! sourcePlayer.isOutputIsolated()) return {};

    const auto previous = deviceBackend->getAudioDeviceSetup();
    auto requested = previous;
    requested.sampleRate = sampleRate;
    stopCalibrationTestSignal();
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty()
        && deviceBackend->getAudioDeviceSetup() == requested
        && std::abs (deviceBackend->getCurrentDeviceState().sampleRate
                     - sampleRate) < 0.01)
    {
        error = playbackSource.reconfigureOutput (toAudioChannelSet (outputLayout), sampleRate,
            [&] { return callbackPause.prepareOutput(); }).getErrorMessage();
        if (error.isEmpty()) { resetPlaybackMeasurements(); return {}; }
    }
    auto rollbackError = callbackPause.restoreDevice();
    if (rollbackError.isEmpty()) rollbackError = callbackPause.recoverPreparation();
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
    deviceRevision.fetch_add (1, std::memory_order_relaxed);
    outputSource.setActiveOutputs (device->getActiveOutputChannels());
    if (retainingResources || isolated)
    {
        // A temporary callback detachment must not reinitialise the retained DSP
        // or decode queues. Only an explicit, successful device edit leaves isolation.
        if (! matchesPreparedOutput (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()))
            isolateOutput();
        renderingEnabled.store (! isolated, std::memory_order_release);
        return;
    }
    if (prepareOutput (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples()).isNotEmpty())
        preparationFailurePending.store (true, std::memory_order_release);
    renderingEnabled.store (! isolated && prepared, std::memory_order_release);
}

void AudioEngine::RoutedSourcePlayer::audioDeviceStopped()
{
    deviceRevision.fetch_add (1, std::memory_order_relaxed);
    renderingEnabled.store (false, std::memory_order_release);
    if (retainingResources || isolated) return;
    prepared = false;
    juce::AudioSourcePlayer::audioDeviceStopped();
}

bool AudioEngine::RoutedSourcePlayer::matchesPreparedOutput (double sampleRate, int blockSize) const noexcept
{
    return prepared && std::abs (sampleRate - preparedSampleRate) < 0.01
        && blockSize == preparedBlockSize && outputSource.getLogicalChannelCount() == preparedChannels;
}

juce::String AudioEngine::RoutedSourcePlayer::prepareOutput (double sampleRate, int blockSize, bool force)
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0 || blockSize <= 0)
    {
        isolateOutput();
        return "The audio device has no valid sample rate or buffer size.";
    }
    if (! force && matchesPreparedOutput (sampleRate, blockSize))
    {
        isolated = false;
        preparationFailurePending.store (false, std::memory_order_release);
        return {};
    }
    prepared = false;
    try
    {
        juce::AudioSourcePlayer::prepareToPlay (sampleRate, blockSize);
        preparedSampleRate = sampleRate;
        preparedBlockSize = blockSize;
        preparedChannels = outputSource.getLogicalChannelCount();
        prepared = true;
        isolated = false;
        preparationFailurePending.store (false, std::memory_order_release);
        return {};
    }
    catch (...)
    {
        isolateOutput();
        return "The audio chain could not prepare for the device. Outputs are disconnected; restore an audio output device.";
    }
}

void AudioEngine::RoutedSourcePlayer::isolateOutput() noexcept
{
    isolated = true;
    renderingEnabled.store (false, std::memory_order_release);
}

void AudioEngine::RoutedSourcePlayer::resetDeviceState() noexcept
{
    prepared = false;
    isolated = false;
    renderingEnabled.store (false, std::memory_order_release);
    preparationFailurePending.store (false, std::memory_order_release);
}

void AudioEngine::RoutedSourcePlayer::audioDeviceIOCallbackWithContext (
    const float* const* inputs, int inputCount, float* const* outputs, int outputCount, int samples,
    const juce::AudioIODeviceCallbackContext& context)
{
    if (renderingEnabled.load (std::memory_order_acquire))
        juce::AudioSourcePlayer::audioDeviceIOCallbackWithContext (inputs, inputCount, outputs, outputCount, samples, context);
    else
        for (int channel = 0; channel < outputCount; ++channel)
            if (outputs[channel] != nullptr) juce::FloatVectorOperations::clear (outputs[channel], samples);
}

juce::String AudioEngine::setBufferSize (int bufferSize)
{
    if (! initialised || ! deviceBackend)
        return "Audio is not initialised.";
    const auto state = deviceBackend->getCurrentDeviceState();
    if (! state.open) return "No audio output device is active.";
    if (sourcePlayer.isOutputIsolated())
        return "Restore an audio output device or sample rate before changing the buffer size.";
    if (! state.bufferSizes.contains (bufferSize))
        return "The current audio device does not support a "
            + juce::String (bufferSize) + "-sample buffer.";
    if (state.bufferSize == bufferSize) return {};

    const auto previous = deviceBackend->getAudioDeviceSetup();
    auto requested = previous;
    requested.bufferSize = bufferSize;
    stopCalibrationTestSignal();
    ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
    auto error = deviceBackend->setAudioDeviceSetup (requested, true);
    if (error.isEmpty()
        && deviceBackend->getAudioDeviceSetup() == requested
        && deviceBackend->getCurrentDeviceState().bufferSize == bufferSize
        && std::abs (deviceBackend->getCurrentDeviceState().sampleRate - state.sampleRate) < 0.01)
    {
        error = callbackPause.prepareOutput().getErrorMessage();
        if (error.isEmpty()) { resetPlaybackMeasurements(); return {}; }
    }
    auto rollbackError = callbackPause.restoreDevice();
    if (rollbackError.isEmpty()) rollbackError = callbackPause.recoverPreparation();
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

void AudioEngine::resetPlaybackMeasurements() noexcept
{
    effectProcessingSource.comparison.reset();
    outputMeter.reset();
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
    if (! effectProcessingSource.clearTails (errorMessage)) return false;
    resetPlaybackMeasurements();
    return true;
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
    GaplessAudioSource& playbackToUse,
    std::array<std::atomic<float>, maximumOutputChannels>& outputPeakLevelsToUse,
    std::atomic<float>& outputGainToUse,
    OutputMeter& outputMeterToUse)
    : transport (sourceToWrap),
      playback (playbackToUse),
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
    calibration.stopTestSignal();
    calibration.prepare (sampleRate);
    if (std::abs (stagedSampleRate - sampleRate) >= 0.01 || stagedBlockSize != samplesPerBlockExpected)
        effectGraph->prepareToPlay (sampleRate, samplesPerBlockExpected, channelLayout);
    stagedSampleRate = 0.0;
    stagedBlockSize = 0;
    dryBuffer.setSize (maximumOutputChannels, samplesPerBlockExpected);
    effectWetMix.reset (sampleRate, 0.02);
    effectWetMix.setCurrentAndTargetValue (areAllBypassed() ? 0.0f : 1.0f);
    outputMeter.prepare (sampleRate, channelLayout);
    comparison.prepare (sampleRate, channelLayout);
    dryMatchGain.reset (sampleRate, 0.02); wetMatchGain.reset (sampleRate, 0.02);
    dryMatchGain.setCurrentAndTargetValue (1.0f); wetMatchGain.setCurrentAndTargetValue (1.0f);
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (outputGain.load (std::memory_order_relaxed));
    for (size_t channel = 0; channel < smoothedChannelGains.size(); ++channel)
    {
        smoothedChannelGains[channel].reset (sampleRate, 0.02);
        smoothedChannelGains[channel].setCurrentAndTargetValue (
            channelGainTargets[channel].load (std::memory_order_relaxed));
    }
    // Source preparation publishes replacement PCM, so commit it only after
    // the output processors have prepared successfully.
    transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void AudioEngine::EffectProcessingSource::releaseResources()
{
    calibration.stopTestSignal();
    effectGraph->releaseResources();
    stagedSampleRate = 0.0;
    stagedBlockSize = 0;
    transport.releaseResources();
    comparison.release();
    outputMeter.release();
    for (auto& level : outputPeakLevels)
        level.store (0.0f, std::memory_order_relaxed);
}

void AudioEngine::EffectProcessingSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    const auto meterEpoch = outputMeter.beginBlock();
    const auto comparisonBlock = comparison.beginBlock();
    bool blockPlaying = false;
    bufferToFill.clearActiveBufferRegion();
    if (! calibration.isTestSignalActive() && ! playbackEditRequested.load())
    {
        activeTransportCallbacks.fetch_add (1);
        if (! playbackEditRequested.load())
        {
            lastTransportPositionSeconds = playback.progress().positionSeconds;
            blockPlaying = transport.isPlaying();
            transport.getNextAudioBlock (bufferToFill);
            if (blockPlaying) lastTransportPositionSeconds = playback.getLastBlockStartPositionSeconds();
        }
        activeTransportCallbacks.fetch_sub (1);
    }

    effectGraph->setTransportPosition (lastTransportPositionSeconds, blockPlaying);
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
    const auto compare = comparison.isEnabled();
    effectGraph->processBlock (processBuffer, midiBuffer, blendDry || compare ? &dryBuffer : nullptr);
    if (compare && transport.isPlaying() && ! calibration.isTestSignalActive()
        && ! playbackEditRequested.load (std::memory_order_relaxed))
        comparison.push (dryBuffer, processBuffer, bufferToFill.numSamples, comparisonBlock);
    dryMatchGain.setTargetValue (comparison.dryGain());
    wetMatchGain.setTargetValue (comparison.wetGain());
    if (effectWetMix.isSmoothing() || dryMatchGain.isSmoothing() || wetMatchGain.isSmoothing()
        || compare || blendDry)
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
        {
            const auto wet = effectWetMix.getNextValue();
            const auto dryGain = dryMatchGain.getNextValue(), wetGain = wetMatchGain.getNextValue();
            for (int channel = 0; channel < channelCount; ++channel)
                processBuffer.setSample (channel, sample,
                    wet * wetGain * processBuffer.getSample (channel, sample)
                    + (wet < 1.0f ? (1.0f - wet) * dryGain * dryBuffer.getSample (channel, sample) : 0.0f));
        }

    for (int channel = 0; channel < juce::jmin (channelCount, maximumOutputChannels); ++channel)
    {
        auto& gain = smoothedChannelGains[static_cast<size_t> (channel)];
        gain.setTargetValue (channelGainTargets[static_cast<size_t> (channel)].load (std::memory_order_relaxed));
        gain.applyGain (processBuffer.getWritePointer (channel), bufferToFill.numSamples);
    }
    static_cast<void> (calibration.renderTestSignal (processBuffer));
    calibration.process (processBuffer);

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
    return effectGraph->addProcessor (slotId, std::move (plugin), errorMessage, std::move (scope));
}

bool AudioEngine::EffectProcessingSource::removePlugin (int slotId)
{
    return effectGraph->removeProcessor (slotId);
}

bool AudioEngine::EffectProcessingSource::clearPlugins()
{
    return effectGraph->clearProcessors();
}

bool AudioEngine::EffectProcessingSource::movePlugin (int fromIndex, int toIndex)
{
    return effectGraph->moveProcessor (fromIndex, toIndex);
}

bool AudioEngine::EffectProcessingSource::setPluginOrder (const std::vector<int>& orderedSlotIds)
{
    return effectGraph->setProcessorOrder (orderedSlotIds);
}

bool AudioEngine::EffectProcessingSource::setBypassed (int slotId, bool shouldBypass)
{
    return effectGraph->setBypassed (slotId, shouldBypass);
}

bool AudioEngine::EffectProcessingSource::setScope (int slotId, const EffectScope& scope, juce::String& errorMessage)
{
    return effectGraph->setProcessorScope (slotId, scope, errorMessage);
}

bool AudioEngine::EffectProcessingSource::setChannelLayout (const juce::AudioChannelSet& layout,
                                                            juce::String& errorMessage)
{
    if (! effectGraph->setChannelLayout (layout, errorMessage))
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
    return effectGraph->size();
}

juce::AudioPluginInstance* AudioEngine::EffectProcessingSource::getPlugin (int slotId) const
{
    return dynamic_cast<juce::AudioPluginInstance*> (effectGraph->getProcessor (slotId));
}

bool AudioEngine::EffectProcessingSource::beginStateCapture (
    int timeoutMilliseconds)
{
    return effectGraph->beginStateCapture (timeoutMilliseconds);
}

void AudioEngine::EffectProcessingSource::endStateCapture() noexcept
{
    effectGraph->endStateCapture();
}

std::unique_ptr<EffectGraph> AudioEngine::EffectProcessingSource::replaceGraph (
    std::unique_ptr<EffectGraph> replacement, const juce::AudioChannelSet& layout,
    double sampleRate, int blockSize)
{
    effectGraph.swap (replacement);
    channelLayout = layout;
    stagedSampleRate = sampleRate;
    stagedBlockSize = blockSize;
    return replacement;
}

void AudioEngine::setLoudnessComparisonEnabled (bool enabled)
{
    effectProcessingSource.comparison.setEnabled (enabled);
}

void AudioEngine::resetLoudnessComparison() noexcept { effectProcessingSource.comparison.reset(); }
LoudnessComparisonStatus AudioEngine::getLoudnessComparisonStatus()
{
    return effectProcessingSource.comparison.update();
}
