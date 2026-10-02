#include "AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
class AudioDeviceManagerBackend final : public IAudioDeviceBackend
{
public:
    juce::String initialiseWithDefaultDevices (int inputChannels,
                                                int outputChannels) override
    {
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
            result.open = true;
            result.name = device->getName();
            result.sampleRate = device->getCurrentSampleRate();
            result.sampleRates = device->getAvailableSampleRates();
            result.bufferSize = device->getCurrentBufferSizeSamples();
            result.bufferSizes = device->getAvailableBufferSizes();
            result.availableOutputChannels = device->getOutputChannelNames().size();
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

private:
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
    return "CoreAudio rejected " + change + ": " + error
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
} // namespace

AudioEngine::AudioEngine()
    : AudioEngine (std::make_unique<AudioDeviceManagerBackend>())
{
}

AudioEngine::AudioEngine (std::unique_ptr<IAudioDeviceBackend> backend)
    : deviceBackend (std::move (backend)),
      effectProcessingSource (transportSource, outputPeakLevels, outputGain)
{
    jassert (deviceBackend != nullptr);
    formatManager.registerBasicFormats();
    sourcePlayer.setSource (&effectProcessingSource);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

juce::String AudioEngine::initialise()
{
    if (initialised)
        return {};

    if (! deviceBackend) return "Audio device backend is unavailable.";
    const auto error = deviceBackend->initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty())
        return error;

    sourcePlayer.setSource (&effectProcessingSource);
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
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;
    if (! readAheadThread.isThreadRunning() && ! readAheadThread.startThread())
        return false;

    const auto sampleRate = reader->sampleRate;
    auto newSource = std::make_unique<ChannelMappedAudioSource> (std::move (reader),
                                                                 toAudioChannelSet (outputLayout),
                                                                 readAheadThread);
    transportSource.stop();
    transportSource.setSource (newSource.get(), 0, nullptr, sampleRate,
                               getChannelCount (OutputLayout::sevenOneTwo));
    readerSource = std::move (newSource);
    return ! readerSource->hasReadError();
}

void AudioEngine::unload()
{
    transportSource.stop();
    transportSource.setSource (nullptr);
    readerSource.reset();
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
    if (initialised)
    {
        ScopedAudioCallbackPause callbackPause (*deviceBackend, sourcePlayer);
        transportSource.setPosition (juce::jmax (0.0, seconds));
        return ! hasReadError();
    }

    transportSource.setPosition (juce::jmax (0.0, seconds));
    return ! hasReadError();
}

void AudioEngine::setGain (double gainToUse)
{
    outputGain.store (static_cast<float> (juce::jlimit (0.0, 1.0, gainToUse)),
                      std::memory_order_relaxed);
}

juce::String AudioEngine::setOutputLayout (OutputLayout layout)
{
    if (layout == outputLayout)
        return {};

    const auto requestedChannelLayout = toAudioChannelSet (layout);
    const auto previousChannelLayout = toAudioChannelSet (outputLayout);
    if (! initialised)
    {
        juce::String graphError;
        if (! effectProcessingSource.setChannelLayout (requestedChannelLayout, graphError))
            return graphError;

        if (readerSource != nullptr)
            readerSource->setDestinationLayout (requestedChannelLayout);
        outputLayout = layout;
        return {};
    }

    const auto device = deviceBackend->getCurrentDeviceState();
    if (! device.open)
        return "No CoreAudio output device is active.";

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

    if (readerSource != nullptr)
        readerSource->setDestinationLayout (requestedChannelLayout);

    auto deviceError = deviceBackend->setAudioDeviceSetup (requestedSetup, true);
    if (deviceError.isEmpty()
        && deviceBackend->getAudioDeviceSetup() != requestedSetup)
        deviceError = "the applied audio device setup did not match the request";
    if (deviceError.isNotEmpty())
    {
        juce::String ignoredGraphError;
        static_cast<void> (effectProcessingSource.setChannelLayout (previousChannelLayout,
                                                                    ignoredGraphError));
        if (readerSource != nullptr)
            readerSource->setDestinationLayout (previousChannelLayout);
        const auto rollbackError = deviceBackend->setAudioDeviceSetup (previousSetup, true);
        return setupFailureMessage (requestedChannelLayout.getDescription(),
                                    deviceError, rollbackError);
    }

    outputLayout = layout;
    return {};
}

AudioDeviceSettingsView AudioEngine::getDeviceSettings() const
{
    AudioDeviceSettingsView view;
    if (! deviceBackend) return view;
    const auto state = deviceBackend->getCurrentDeviceState();
    view.currentDevice = state.name;
    view.devices = deviceBackend->getOutputDeviceNames();
    view.currentSampleRate = state.sampleRate;
    view.sampleRates = state.sampleRates;
    view.currentBufferSize = state.bufferSize;
    view.bufferSizes = state.bufferSizes;
    view.availableOutputChannels = state.availableOutputChannels;
    return view;
}

juce::String AudioEngine::selectOutputDevice (const juce::String& deviceName)
{
    if (! initialised || ! deviceBackend)
        return "CoreAudio is not initialised.";
    const auto devices = deviceBackend->getOutputDeviceNames();
    if (! devices.contains (deviceName))
        return "CoreAudio output device is not available: " + deviceName;

    const auto previous = deviceBackend->getAudioDeviceSetup();
    if (deviceBackend->getCurrentDeviceState().name == deviceName) return {};
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
            return {};
    }
    const auto rollbackError = deviceBackend->setAudioDeviceSetup (previous, true);
    return setupFailureMessage ("output device " + deviceName,
                                error.isNotEmpty()
                                    ? error
                                    : "the device does not expose enough output channels",
                                rollbackError);
}

juce::String AudioEngine::setSampleRate (double sampleRate)
{
    if (! initialised || ! deviceBackend)
        return "CoreAudio is not initialised.";
    const auto state = deviceBackend->getCurrentDeviceState();
    if (! state.open) return "No CoreAudio output device is active.";
    if (! std::isfinite (sampleRate)
        || ! containsSampleRate (state.sampleRates, sampleRate))
        return "The current CoreAudio device does not support "
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

juce::String AudioEngine::setBufferSize (int bufferSize)
{
    if (! initialised || ! deviceBackend)
        return "CoreAudio is not initialised.";
    const auto state = deviceBackend->getCurrentDeviceState();
    if (! state.open) return "No CoreAudio output device is active.";
    if (! state.bufferSizes.contains (bufferSize))
        return "The current CoreAudio device does not support a "
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
    return readerSource != nullptr && readerSource->hasReadError();
}

bool AudioEngine::hasStreamFinished() const
{
    return readerSource != nullptr && transportSource.hasStreamFinished();
}

double AudioEngine::getCurrentPositionSeconds() const
{
    return transportSource.getCurrentPosition();
}

double AudioEngine::getLengthInSeconds() const
{
    return transportSource.getLengthInSeconds();
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
    return deviceBackend
        ? deviceBackend->getCurrentDeviceState().availableOutputChannels : 0;
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

std::array<float, 10> AudioEngine::getOutputPeakLevels() const
{
    std::array<float, 10> result {};
    for (size_t channel = 0; channel < result.size(); ++channel)
        result[channel] = outputPeakLevels[channel].load (std::memory_order_relaxed);

    return result;
}

bool AudioEngine::addEffectPlugin (int slotId,
                                   std::unique_ptr<juce::AudioPluginInstance> plugin,
                                   juce::String& errorMessage)
{
    return effectProcessingSource.addPlugin (slotId, std::move (plugin), errorMessage);
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
    std::array<std::atomic<float>, 10>& outputPeakLevelsToUse,
    std::atomic<float>& outputGainToUse)
    : transport (sourceToWrap),
      outputPeakLevels (outputPeakLevelsToUse),
      outputGain (outputGainToUse)
{
    smoothedGain.setCurrentAndTargetValue (outputGain.load (std::memory_order_relaxed));
}

void AudioEngine::EffectProcessingSource::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
    effectGraph.prepareToPlay (sampleRate, samplesPerBlockExpected, channelLayout);
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (outputGain.load (std::memory_order_relaxed));
}

void AudioEngine::EffectProcessingSource::releaseResources()
{
    effectGraph.releaseResources();
    transport.releaseResources();
    for (auto& level : outputPeakLevels)
        level.store (0.0f, std::memory_order_relaxed);
}

void AudioEngine::EffectProcessingSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    transport.getNextAudioBlock (bufferToFill);

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
    effectGraph.processBlock (processBuffer, midiBuffer);

    const auto startGain = smoothedGain.getCurrentValue();
    smoothedGain.setTargetValue (outputGain.load (std::memory_order_relaxed));
    const auto endGain = smoothedGain.skip (bufferToFill.numSamples);
    processBuffer.applyGainRamp (0, bufferToFill.numSamples, startGain, endGain);

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

bool AudioEngine::EffectProcessingSource::addPlugin (int slotId,
                                                     std::unique_ptr<juce::AudioPluginInstance> plugin,
                                                     juce::String& errorMessage)
{
    return effectGraph.addProcessor (slotId, std::move (plugin), errorMessage);
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

bool AudioEngine::EffectProcessingSource::setChannelLayout (const juce::AudioChannelSet& layout,
                                                            juce::String& errorMessage)
{
    if (! effectGraph.setChannelLayout (layout, errorMessage))
        return false;

    channelLayout = layout;
    return true;
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
