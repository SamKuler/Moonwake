#pragma once

#include "audio/AudioEngine.h"

class LibraryAudioBackend final : public IAudioDeviceBackend,
                                        private juce::AudioIODevice
{
public:
    LibraryAudioBackend() : AudioIODevice ("Offline 16ch", "Test")
    {
        setup.outputDeviceName = getName();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
    }

    int setupChanges = 0, callbackAttachments = 0;
    juce::String initialiseWithDefaultDevices (int, int) override { return {}; }
    juce::StringArray getOutputDeviceNames() const override { return { getName() }; }
    AudioDeviceBackendState getCurrentDeviceState() const override
    {
        return { true, getName(), setup.sampleRate, { setup.sampleRate },
                 setup.bufferSize, { setup.bufferSize }, 16, {}, setup.outputChannels };
    }
    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override
    {
        return setup;
    }
    juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup& requested, bool) override
    {
        ++setupChanges;
        setup = requested;
        return {};
    }
    void addAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
        ++callbackAttachments;
        attachedCallback = callback;
        callback->audioDeviceAboutToStart (this);
    }
    void removeAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
        if (attachedCallback == callback)
        {
            attachedCallback = nullptr;
            callback->audioDeviceStopped();
        }
    }

    void render (juce::AudioBuffer<float>& output)
    {
        output.clear();
        attachedCallback->audioDeviceIOCallbackWithContext (
            nullptr, 0, output.getArrayOfWritePointers(), output.getNumChannels(),
            output.getNumSamples(), {});
    }

    void renderPhysicalOutputs (juce::AudioBuffer<float>& output)
    {
        output.clear();
        std::array<float*, 16> packed {};
        int active = 0;
        for (auto port = setup.outputChannels.findNextSetBit (0); port >= 0;
             port = setup.outputChannels.findNextSetBit (port + 1))
            packed[static_cast<size_t> (active++)] = output.getWritePointer (port);
        attachedCallback->audioDeviceIOCallbackWithContext (
            nullptr, 0, packed.data(), active, output.getNumSamples(), {});
    }

    void disconnectPhysicalPort (int port)
    {
        attachedCallback->audioDeviceStopped();
        setup.outputChannels.clearBit (port);
        attachedCallback->audioDeviceAboutToStart (this);
    }

private:
    juce::StringArray getOutputChannelNames() override
    {
        return { "1", "2", "3", "4", "5", "6", "7", "8",
                 "9", "10", "11", "12", "13", "14", "15", "16" };
    }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { setup.sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { setup.bufferSize }; }
    int getDefaultBufferSize() override { return setup.bufferSize; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&,
                       double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return attachedCallback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return setup.bufferSize; }
    double getCurrentSampleRate() override { return setup.sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return setup.outputChannels; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    juce::AudioIODeviceCallback* attachedCallback = nullptr;
};
