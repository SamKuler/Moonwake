#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/ChannelMappedAudioSource.h"
#include "audio/EffectGraph.h"
#include "audio/OutputLayout.h"

#include <array>
#include <atomic>
#include <memory>

struct AudioDeviceSettingsView
{
    juce::String currentDevice;
    juce::StringArray devices;
    double currentSampleRate = 0.0;
    juce::Array<double> sampleRates;
    int currentBufferSize = 0;
    juce::Array<int> bufferSizes;
    int availableOutputChannels = 0;
};

struct AudioDeviceBackendState
{
    bool open = false;
    juce::String name;
    double sampleRate = 0.0;
    juce::Array<double> sampleRates;
    int bufferSize = 0;
    juce::Array<int> bufferSizes;
    int availableOutputChannels = 0;
};

class IAudioDeviceBackend
{
public:
    virtual ~IAudioDeviceBackend() = default;
    virtual juce::String initialiseWithDefaultDevices (int inputChannels,
                                                        int outputChannels) = 0;
    [[nodiscard]] virtual juce::StringArray getOutputDeviceNames() const = 0;
    [[nodiscard]] virtual AudioDeviceBackendState getCurrentDeviceState() const = 0;
    [[nodiscard]] virtual juce::AudioDeviceManager::AudioDeviceSetup
        getAudioDeviceSetup() const = 0;
    virtual juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup&, bool chosen) = 0;
    virtual void addAudioCallback (juce::AudioIODeviceCallback*) = 0;
    virtual void removeAudioCallback (juce::AudioIODeviceCallback*) = 0;
};

class AudioEngine
{
public:
    AudioEngine();
    explicit AudioEngine (std::unique_ptr<IAudioDeviceBackend>);
    ~AudioEngine();

    juce::String initialise();
    void shutdown();

    bool loadFile (const juce::File& file);
    void unload();
    void play();
    void pause();
    void stop();
    void seekToSeconds (double seconds);
    void setGain (double gain);
    juce::String setOutputLayout (OutputLayout layout);
    [[nodiscard]] AudioDeviceSettingsView getDeviceSettings() const;
    juce::String selectOutputDevice (const juce::String& deviceName);
    juce::String setSampleRate (double sampleRate);
    juce::String setBufferSize (int bufferSize);
    bool addEffectPlugin (int slotId,
                          std::unique_ptr<juce::AudioPluginInstance> plugin,
                          juce::String& errorMessage);
    bool removeEffectPlugin (int slotId);
    bool clearEffectPlugins();
    bool moveEffectPlugin (int fromIndex, int toIndex);
    bool setEffectPluginOrder (const std::vector<int>& orderedSlotIds);
    bool setEffectBypassed (int slotId, bool shouldBypass);
    [[nodiscard]] int getEffectPluginCount() const;
    [[nodiscard]] juce::AudioPluginInstance* getEffectPlugin (int slotId) const;
    bool beginEffectStateCapture (int timeoutMilliseconds = 1000);
    void endEffectStateCapture() noexcept;

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getLengthInSeconds() const;
    [[nodiscard]] double getGain() const;
    [[nodiscard]] OutputLayout getOutputLayout() const;
    [[nodiscard]] int getAvailableOutputChannelCount() const;
    [[nodiscard]] juce::String getCurrentAudioDeviceName() const;
    [[nodiscard]] double getCurrentAudioSampleRate() const;
    [[nodiscard]] int getCurrentAudioBufferSize() const;
    [[nodiscard]] std::array<float, 10> getOutputPeakLevels() const;

private:
    class EffectProcessingSource final : public juce::AudioSource
    {
    public:
        EffectProcessingSource (juce::AudioTransportSource& sourceToWrap,
                                std::array<std::atomic<float>, 10>& outputPeakLevelsToUse,
                                std::atomic<float>& outputGainToUse);

        void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

        bool addPlugin (int slotId,
                        std::unique_ptr<juce::AudioPluginInstance> plugin,
                        juce::String& errorMessage);
        bool removePlugin (int slotId);
        bool clearPlugins();
        bool movePlugin (int fromIndex, int toIndex);
        bool setPluginOrder (const std::vector<int>& orderedSlotIds);
        bool setBypassed (int slotId, bool shouldBypass);
        bool setChannelLayout (const juce::AudioChannelSet& layout, juce::String& errorMessage);
        [[nodiscard]] int getPluginCount() const;
        [[nodiscard]] juce::AudioPluginInstance* getPlugin (int slotId) const;
        bool beginStateCapture (int timeoutMilliseconds);
        void endStateCapture() noexcept;

    private:
        juce::AudioTransportSource& transport;
        EffectGraph effectGraph;
        juce::MidiBuffer midiBuffer;
        juce::AudioChannelSet channelLayout = juce::AudioChannelSet::stereo();
        std::array<std::atomic<float>, 10>& outputPeakLevels;
        std::atomic<float>& outputGain;
        juce::SmoothedValue<float> smoothedGain;
    };

    std::unique_ptr<IAudioDeviceBackend> deviceBackend;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::array<std::atomic<float>, 10> outputPeakLevels {};
    std::atomic<float> outputGain { 0.8f };
    EffectProcessingSource effectProcessingSource;
    std::unique_ptr<ChannelMappedAudioSource> readerSource;

    OutputLayout outputLayout = OutputLayout::stereo;
    bool initialised = false;
};
