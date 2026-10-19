#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/ChannelMappedAudioSource.h"
#include "audio/EffectGraph.h"
#include "audio/GaplessAudioSource.h"
#include "audio/OutputLayout.h"
#include "audio/OutputChannelMix.h"
#include "audio/OutputMeter.h"
#include "audio/LoudnessComparison.h"
#include "audio/OutputCalibrationProcessor.h"
#include "audio/ListeningOutputConfiguration.h"
#include "audio/AudioOutputRoute.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

struct AudioDeviceOutputChannel
{
    int deviceChannelIndex = -1; // Zero-based index in the device's physical output list.
    juce::String name;
};

struct AudioDeviceSettingsView
{
    juce::String currentDevice;
    juce::String currentDeviceIdentifier;
    juce::StringArray devices;
    double currentSampleRate = 0.0;
    juce::Array<double> sampleRates;
    int currentBufferSize = 0;
    juce::Array<int> bufferSizes;
    int availableOutputChannels = 0;
    // Ordered exactly like the packed output buffers supplied to the audio callback.
    std::vector<AudioDeviceOutputChannel> activeOutputChannels;
    std::vector<AudioDeviceOutputChannel> outputChannels;
    // Logical speaker order -> zero-based physical output, or -1 when disconnected.
    std::vector<int> outputRouting;
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
    juce::StringArray outputChannelNames {};
    juce::BigInteger activeOutputChannels {};
    juce::String identifier {};
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
    virtual juce::String openDeviceConfiguration()
    {
        return "Device configuration is unavailable for this audio backend.";
    }
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
    bool setUpcomingPlayback (std::vector<AudioPlaybackItem> items, int repeatFrom = -1,
                              std::optional<AudioPlaybackProgress> expected = {});
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
    [[nodiscard]] std::vector<OutputChannelCalibration> getOutputCalibration() const;
    juce::String setOutputCalibration (juce::AudioChannelSet::ChannelType, float trimDb,
                                       double delayMs, bool inverted);
    void resetOutputCalibration();
    [[nodiscard]] juce::String validateCalibrationTestSignal (const CalibrationTestSignal&) const;
    juce::String setCalibrationTestSignal (CalibrationTestSignal);
    void stopCalibrationTestSignal() noexcept;
    [[nodiscard]] CalibrationTestSignal getCalibrationTestSignal() const;

    struct PreparedEffect
    {
        int slotId = 0;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        EffectScope scope;
        bool bypassed = false;
    };
    juce::String replaceEffectPlugins (std::vector<PreparedEffect>);
    juce::String applyListeningConfiguration (const ListeningOutputConfiguration&,
                                              std::vector<PreparedEffect>);
    juce::String setOutputLayout (OutputLayout layout,
                                 const std::vector<AudioOutputRoute>& savedRoutes = {},
                                 juce::String* routingWarning = nullptr);
    [[nodiscard]] AudioDeviceSettingsView getDeviceSettings() const;
    [[nodiscard]] uint64_t getDeviceConfigurationRevision() const noexcept;
    juce::String takeOutputError();
    juce::String selectOutputDevice (const juce::String& deviceName,
                                    const std::vector<AudioOutputRoute>& savedRoutes = {},
                                    juce::String* routingWarning = nullptr);
    juce::String setSampleRate (double sampleRate);
    juce::String setBufferSize (int bufferSize);
    juce::String setOutputRouting (const std::vector<int>& physicalOutputs);
    juce::String openDeviceConfiguration();
    bool addEffectPlugin (int slotId,
                          std::unique_ptr<juce::AudioPluginInstance> plugin,
                          juce::String& errorMessage,
                          EffectScope scope = {});
    bool removeEffectPlugin (int slotId);
    bool clearEffectPlugins();
    bool moveEffectPlugin (int fromIndex, int toIndex);
    bool setEffectPluginOrder (const std::vector<int>& orderedSlotIds);
    bool setEffectBypassed (int slotId, bool shouldBypass);
    void setAllEffectsBypassed (bool) noexcept;
    void setLoudnessComparisonEnabled (bool);
    void resetLoudnessComparison() noexcept;
    [[nodiscard]] LoudnessComparisonStatus getLoudnessComparisonStatus();
    [[nodiscard]] bool areAllEffectsBypassed() const noexcept;
    bool clearEffectTails (juce::String& errorMessage);
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
    class DeviceOutputSource final : public juce::AudioSource
    {
    public:
        explicit DeviceOutputSource (juce::AudioSource& source) : logicalSource (source) {}
        void setRouting (const std::vector<int>&);
        void setActiveOutputs (const juce::BigInteger&);
        [[nodiscard]] int getLogicalChannelCount() const noexcept { return static_cast<int> (routes.size()); }
        void prepareToPlay (int, double) override;
        void releaseResources() override;
        void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

    private:
        juce::AudioSource& logicalSource;
        juce::AudioBuffer<float> logicalBuffer;
        std::vector<int> routes { 0, 1 }; // Changed only with the device callback detached.
        std::array<int, maximumOutputChannels> packedDestinations {};
    };

    class RoutedSourcePlayer final : public juce::AudioSourcePlayer
    {
    public:
        RoutedSourcePlayer (DeviceOutputSource& source, std::atomic<uint64_t>& revision)
            : outputSource (source), deviceRevision (revision) {}
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void beginConfiguration() noexcept { retainingResources = true; }
        void endConfiguration() noexcept { retainingResources = false; }
        juce::String prepareOutput (double sampleRate, int blockSize, bool force = false);
        void isolateOutput() noexcept;
        void invalidatePreparedOutput() noexcept { prepared = false; isolateOutput(); }
        void resetDeviceState() noexcept;
        [[nodiscard]] bool isOutputIsolated() const noexcept { return isolated; }
        bool takePreparationFailure() noexcept { return preparationFailurePending.exchange (false); }
    private:
        DeviceOutputSource& outputSource;
        std::atomic<uint64_t>& deviceRevision;
        std::atomic_bool renderingEnabled { false };
        std::atomic_bool preparationFailurePending { false };
        std::atomic_bool retainingResources { false }, isolated { false };
        bool prepared = false;
        double preparedSampleRate = 0.0;
        int preparedBlockSize = 0, preparedChannels = 0;
        [[nodiscard]] bool matchesPreparedOutput (double sampleRate, int blockSize) const noexcept;
    };

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
        void setAllBypassed (bool value) noexcept { allBypassed.store (value, std::memory_order_relaxed); }
        [[nodiscard]] bool areAllBypassed() const noexcept { return allBypassed.load (std::memory_order_relaxed); }
        bool clearTails (juce::String& errorMessage) { return effectGraph->clearTails (errorMessage); }
        bool setScope (int slotId, const EffectScope&, juce::String& errorMessage);
        bool setChannelLayout (const juce::AudioChannelSet& layout, juce::String& errorMessage);
        std::unique_ptr<EffectGraph> replaceGraph (std::unique_ptr<EffectGraph>,
            const juce::AudioChannelSet&, double sampleRate, int blockSize);
        LoudnessComparison comparison;
        OutputCalibrationProcessor calibration;
        void setChannelGainTargets (const std::array<float, maximumOutputChannels>&) noexcept;
        [[nodiscard]] int getPluginCount() const;
        [[nodiscard]] juce::AudioPluginInstance* getPlugin (int slotId) const;
        bool beginStateCapture (int timeoutMilliseconds);
        void endStateCapture() noexcept;
        void beginPlaybackEdit();
        void endPlaybackEdit();

    private:
        juce::AudioTransportSource& transport;
        std::unique_ptr<EffectGraph> effectGraph = std::make_unique<EffectGraph>();
        double stagedSampleRate = 0.0;
        int stagedBlockSize = 0;
        juce::MidiBuffer midiBuffer;
        juce::AudioChannelSet channelLayout = juce::AudioChannelSet::stereo();
        std::array<std::atomic<float>, maximumOutputChannels>& outputPeakLevels;
        std::atomic<float>& outputGain;
        OutputMeter& outputMeter;
        juce::SmoothedValue<float> smoothedGain;
        std::atomic_bool allBypassed { false };
        juce::SmoothedValue<float> dryMatchGain { 1.0f }, wetMatchGain { 1.0f };
        juce::SmoothedValue<float> effectWetMix { 1.0f };
        juce::AudioBuffer<float> dryBuffer;
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
    juce::AudioTransportSource transportSource;
    std::array<std::atomic<float>, maximumOutputChannels> outputPeakLevels {};
    std::atomic<float> outputGain { 0.8f };
    OutputMeter outputMeter;
    EffectProcessingSource effectProcessingSource;
    DeviceOutputSource deviceOutputSource;
    std::atomic<uint64_t> deviceConfigurationRevision { 0 };
    RoutedSourcePlayer sourcePlayer;

    OutputLayout outputLayout = OutputLayout::stereo;
    std::vector<OutputChannelMix> outputChannelMix = defaultOutputChannelMix();
    std::vector<OutputChannelCalibration> outputCalibration = defaultOutputCalibration();
    CalibrationTestSignal calibrationTestSignal;
    std::vector<int> outputRouting { 0, 1 };
    bool initialised = false;
    void resetPlaybackMeasurements() noexcept;
    void updateOutputChannelMixTargets();
    void updateOutputCalibrationTargets();
    std::unique_ptr<EffectGraph> prepareEffectGraph (std::vector<PreparedEffect>,
        const juce::AudioChannelSet&, double sampleRate, int bufferSize, juce::String& error);
    // These configuration steps require the device callback to be detached.
    juce::String applyOutputRouting (const std::vector<int>&, const juce::BigInteger&);
    juce::String restoreOutputRouting (const std::vector<AudioOutputRoute>&);
};
