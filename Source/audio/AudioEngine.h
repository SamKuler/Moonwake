#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/ChannelMappedAudioSource.h"
#include "audio/EffectGraph.h"
#include "audio/GaplessAudioSource.h"
#include "audio/OutputLayout.h"
#include "audio/OutputChannelMix.h"
#include "audio/OutputMeter.h"

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

    // Freezes source consumption while the control thread edits playback state.
    // Effect processing remains active, including tails from previous input.
    class ScopedPlaybackEdit final
    {
    public:
        explicit ScopedPlaybackEdit (AudioEngine&);
        ~ScopedPlaybackEdit();
        ScopedPlaybackEdit (const ScopedPlaybackEdit&) = delete;
        ScopedPlaybackEdit& operator= (const ScopedPlaybackEdit&) = delete;

    private:
        AudioEngine& engine;
    };

    juce::String initialise();
    void shutdown();

    bool loadFile (const juce::File& file);
    bool loadPlaybackPlan (AudioPlaybackPlan plan, double startSeconds = 0.0);
    bool setUpcomingPlayback (std::vector<AudioPlaybackItem> items, int repeatFrom = -1);
    [[nodiscard]] AudioPlaybackProgress getPlaybackProgress() const;
    std::vector<AudioPreparationFailure> drainPreparationFailures();
    void unload();
    void play();
    void pause();
    void stop();
    bool seekToSeconds (double seconds);
    void setGain (double gain);
    [[nodiscard]] std::vector<OutputChannelMix> getOutputChannelMix() const;
    bool setOutputChannelMix (juce::AudioChannelSet::ChannelType, float gain, bool muted, bool solo);
    void resetOutputChannelMix();
    juce::String setOutputLayout (OutputLayout layout);
    [[nodiscard]] AudioDeviceSettingsView getDeviceSettings() const;
    juce::String selectOutputDevice (const juce::String& deviceName);
    juce::String setSampleRate (double sampleRate);
    juce::String setBufferSize (int bufferSize);
    bool addEffectPlugin (int slotId,
                          std::unique_ptr<juce::AudioPluginInstance> plugin,
                          juce::String& errorMessage,
                          EffectScope scope = {});
    bool removeEffectPlugin (int slotId);
    bool clearEffectPlugins();
    bool moveEffectPlugin (int fromIndex, int toIndex);
    bool setEffectPluginOrder (const std::vector<int>& orderedSlotIds);
    bool setEffectBypassed (int slotId, bool shouldBypass);
    bool setEffectScope (int slotId, const EffectScope&, juce::String& errorMessage);
    [[nodiscard]] int getEffectPluginCount() const;
    [[nodiscard]] juce::AudioPluginInstance* getEffectPlugin (int slotId) const;
    bool beginEffectStateCapture (int timeoutMilliseconds = 1000);
    void endEffectStateCapture() noexcept;

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool hasStreamFinished() const;
    [[nodiscard]] bool hasReadError() const;
    [[nodiscard]] double getCurrentPositionSeconds() const;
    [[nodiscard]] double getLengthInSeconds() const;
    [[nodiscard]] double getGain() const;
    [[nodiscard]] OutputLayout getOutputLayout() const;
    [[nodiscard]] int getAvailableOutputChannelCount() const;
    [[nodiscard]] juce::String getCurrentAudioDeviceName() const;
    [[nodiscard]] double getCurrentAudioSampleRate() const;
    [[nodiscard]] int getCurrentAudioBufferSize() const;
    [[nodiscard]] std::array<float, maximumOutputChannels> getOutputPeakLevels() const;
    [[nodiscard]] OutputMeterReadings getOutputMeterReadings() const;
    void resetOutputMeter() noexcept;

private:
    class EffectProcessingSource final : public juce::AudioSource
    {
    public:
        EffectProcessingSource (juce::AudioTransportSource& sourceToWrap,
                                std::array<std::atomic<float>, maximumOutputChannels>& outputPeakLevelsToUse,
                                std::atomic<float>& outputGainToUse,
                                OutputMeter& outputMeterToUse);

        void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

        bool addPlugin (int slotId,
                        std::unique_ptr<juce::AudioPluginInstance> plugin,
                        juce::String& errorMessage,
                        EffectScope scope);
        bool removePlugin (int slotId);
        bool clearPlugins();
        bool movePlugin (int fromIndex, int toIndex);
        bool setPluginOrder (const std::vector<int>& orderedSlotIds);
        bool setBypassed (int slotId, bool shouldBypass);
        bool setScope (int slotId, const EffectScope&, juce::String& errorMessage);
        bool setChannelLayout (const juce::AudioChannelSet& layout, juce::String& errorMessage);
        void setChannelGainTargets (const std::array<float, maximumOutputChannels>&) noexcept;
        [[nodiscard]] int getPluginCount() const;
        [[nodiscard]] juce::AudioPluginInstance* getPlugin (int slotId) const;
        bool beginStateCapture (int timeoutMilliseconds);
        void endStateCapture() noexcept;
        void beginPlaybackEdit();
        void endPlaybackEdit();

    private:
        juce::AudioTransportSource& transport;
        EffectGraph effectGraph;
        juce::MidiBuffer midiBuffer;
        juce::AudioChannelSet channelLayout = juce::AudioChannelSet::stereo();
        std::array<std::atomic<float>, maximumOutputChannels>& outputPeakLevels;
        std::atomic<float>& outputGain;
        OutputMeter& outputMeter;
        juce::SmoothedValue<float> smoothedGain;
        std::array<std::atomic<float>, maximumOutputChannels> channelGainTargets;
        std::array<juce::SmoothedValue<float>, maximumOutputChannels> smoothedChannelGains;
        std::atomic_bool playbackEditRequested { false };
        std::atomic<int> activeTransportCallbacks { 0 };
        unsigned playbackEditDepth = 0; // Control thread only.
    };

    std::unique_ptr<IAudioDeviceBackend> deviceBackend;
    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread readAheadThread { "Audio file read-ahead" };
    GaplessAudioSource playbackSource { formatManager, readAheadThread };
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::array<std::atomic<float>, maximumOutputChannels> outputPeakLevels {};
    std::atomic<float> outputGain { 0.8f };
    OutputMeter outputMeter;
    EffectProcessingSource effectProcessingSource;

    OutputLayout outputLayout = OutputLayout::stereo;
    std::vector<OutputChannelMix> outputChannelMix = defaultOutputChannelMix();
    bool initialised = false;
    void updateOutputChannelMixTargets();
};
