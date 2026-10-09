#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/AudioEngine.h"
#include "app/PlayerController.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace
{
class OfflineAudioDeviceBackend final : public IAudioDeviceBackend,
                                        private juce::AudioIODevice
{
public:
    explicit OfflineAudioDeviceBackend (int bufferSize = 256) : AudioIODevice ("Offline 16ch", "Test")
    {
        setup.outputDeviceName = getName();
        setup.sampleRate = 48000.0;
        setup.bufferSize = bufferSize;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
    }

    juce::String initialiseWithDefaultDevices (int, int) override { return {}; }
    juce::StringArray getOutputDeviceNames() const override { return { getName(), "Offline alternate" }; }
    AudioDeviceBackendState getCurrentDeviceState() const override
    {
        return { true, setup.outputDeviceName, setup.sampleRate, { 48000.0, 96000.0 },
                 setup.bufferSize, { setup.bufferSize }, 16, {}, setup.outputChannels };
    }
    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override
    {
        return setup;
    }
    juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup& requested, bool) override
    {
        if (setupFailuresRemaining > 0)
        {
            --setupFailuresRemaining;
            if (mutateOnSetupFailure && (! leaveRollbackFailureUnchanged || setupFailuresRemaining > 0)) setup = requested;
            return "Injected device failure";
        }
        setup = requested;
        if (setup.sampleRate == 0.0)
            setup.sampleRate = setup.outputDeviceName == "Offline alternate" ? 96000.0 : 48000.0;
        if (setup.bufferSize == 0) setup.bufferSize = 256;
        return {};
    }
    void addAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
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

    int setupFailuresRemaining = 0;
    bool mutateOnSetupFailure = false;
    bool leaveRollbackFailureUnchanged = false;

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

    void restartAt (double sampleRate)
    {
        attachedCallback->audioDeviceStopped();
        setup.sampleRate = sampleRate;
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

class DelayProbePlugin final : public juce::AudioPluginInstance
{
public:
    explicit DelayProbePlugin (int latencySamples = 256, bool reportsLatency = false)
        : AudioPluginInstance (BusesProperties()
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          delaySamples (latencySamples)
    {
        if (reportsLatency) setLatencySamples (latencySamples);
    }
    int prepares = 0, releases = 0;
    double rejectedSampleRate = 0.0;
    std::function<void()> onPrepare;
    const juce::String getName() const override { return "Delay probe"; }
    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = getName();
    }
    void prepareToPlay (double rate, int) override
    {
        if (juce::exactlyEqual (rate, rejectedSampleRate)) throw std::runtime_error ("Unsupported probe sample rate");
        if (onPrepare) onPrepare();
        ++prepares;
        delay.setSize (getTotalNumOutputChannels(), delaySamples);
        delay.clear();
        position = 0;
    }
    void releaseResources() override { ++releases; delay.clear(); }
    void reset() override { delay.clear(); position = 0; }
    void changeLatency (int samples)
    {
        delaySamples = samples;
        delay.setSize (getTotalNumOutputChannels(), delaySamples);
        reset();
        setLatencySamples (samples);
    }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto input = buffer.getSample (channel, sample);
                buffer.setSample (channel, sample, delay.getSample (channel, position));
                delay.setSample (channel, position, input);
            }
            position = (position + 1) % delay.getNumSamples();
        }
    }
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == layout.getMainOutputChannelSet();
    }
    double getTailLengthSeconds() const override { return 256.0 / 48000.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
private:
    juce::AudioBuffer<float> delay;
    int delaySamples = 256;
    int position = 0;
};

class MixProbePlugin final : public juce::AudioPluginInstance
{
public:
    MixProbePlugin() : AudioPluginInstance (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Channel mix probe"; }
    void fillInPluginDescription (juce::PluginDescription& description) const override { description.name = getName(); }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            std::fill_n (buffer.getWritePointer (channel), buffer.getNumSamples(), 0.1f * static_cast<float> (channel + 1));
    }
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == layout.getMainOutputChannelSet();
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

class AudioOutputTransactionTests final : public juce::UnitTest
{
public:
    AudioOutputTransactionTests() : UnitTest ("AudioOutputTransactions", "audio") {}
    void runTest() override
    {
        for (const auto pluginFails : { false, true })
        {
            beginTest (pluginFails ? "failed staged effect preparation preserves the old delay history"
                                   : "a rejected device change preserves the old delay history");
            juce::TemporaryFile file (juce::File::getCurrentWorkingDirectory().getChildFile ("output-transaction.wav"));
            writeAudio (file.getFile(), true);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            juce::String error;
            expect (engine.addEffectPlugin (1, std::make_unique<DelayProbePlugin> (512), error));
            expect (engine.loadFile (file.getFile()));
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            auto configuration = alternateConfiguration();
            configuration.sampleRate = 0.0;
            std::vector<AudioEngine::PreparedEffect> replacement;
            if (pluginFails)
            {
                auto plugin = std::make_unique<DelayProbePlugin>();
                plugin->rejectedSampleRate = 96000.0;
                replacement.push_back ({ 2, std::move (plugin), {}, false });
            }
            else device->setupFailuresRemaining = 1;
            expect (engine.applyListeningConfiguration (configuration, std::move (replacement)).isNotEmpty());
            expect (engine.getEffectPlugin (1) != nullptr);
            expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
            device->render (block);
            device->render (block);
            expectWithinAbsoluteError (block.getMagnitude (0, 256), 0.4f, 0.0001f);
        }

        for (const auto& operation : { juce::String ("layout"), juce::String ("rate"), juce::String ("device") })
        {
            beginTest ("a rejected manual " + operation + " change preserves delayed audio");
            juce::TemporaryFile file (juce::File::getCurrentWorkingDirectory().getChildFile ("manual-transaction.wav"));
            writeAudio (file.getFile(), true);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            juce::String error;
            expect (engine.addEffectPlugin (1, std::make_unique<DelayProbePlugin> (512), error));
            expect (engine.loadFile (file.getFile()));
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            device->setupFailuresRemaining = 1;
            error = operation == "layout" ? engine.setOutputLayout (OutputLayout::fiveOne)
                : operation == "rate" ? engine.setSampleRate (96000.0) : engine.selectOutputDevice ("Offline alternate");
            expect (error.isNotEmpty());
            device->render (block); device->render (block);
            expectWithinAbsoluteError (block.getMagnitude (0, 256), 0.4f, 0.0001f);
        }

        beginTest ("only external device preparation failures publish a single output error");
        {
            juce::TemporaryFile file (juce::File::getCurrentWorkingDirectory().getChildFile ("output-error.wav"));
            writeAudio (file.getFile(), false);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            auto plugin = std::make_unique<DelayProbePlugin> (1);
            plugin->rejectedSampleRate = 96000.0;
            juce::String error;
            expect (engine.addEffectPlugin (1, std::move (plugin), error));
            expect (engine.loadFile (file.getFile()));
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            const auto before = engine.getCurrentPositionSeconds();
            expect (engine.setSampleRate (96000.0).isNotEmpty());
            expect (engine.takeOutputError().isEmpty());
            expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
            expectEquals (engine.getCurrentPositionSeconds(), before);
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.0001f);
            device->restartAt (96000.0);
            expect (engine.takeOutputError().isNotEmpty());
            expect (engine.takeOutputError().isEmpty());
            device->render (block);
            expectEquals (block.getMagnitude (0, 256), 0.0f);
            expect (engine.selectOutputDevice ("Offline 16ch").isEmpty());
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.0001f);
            device->restartAt (96000.0);
            expect (engine.selectOutputDevice ("Offline 16ch").isEmpty());
            expect (engine.takeOutputError().isEmpty());
        }

        beginTest ("successful routing starts a new measurement while rejected routing retains the old one");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            juce::String error;
            expect (engine.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error));
            juce::AudioBuffer<float> block (2, 256);
            for (int pass = 0; pass < 16; ++pass) device->render (block);
            auto before = engine.getOutputMeterReadings();
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            while (before.measuredSeconds <= 0.0 && juce::Time::getMillisecondCounterHiRes() < deadline)
            {
                juce::Thread::sleep (1);
                before = engine.getOutputMeterReadings();
            }
            expect (before.measuredSeconds > 0.0);
            device->setupFailuresRemaining = 1;
            expect (engine.setOutputRouting ({ 1, 0 }).isNotEmpty());
            expect (engine.getOutputMeterReadings().measuredSeconds >= before.measuredSeconds);
            expect (engine.setOutputRouting ({ 1, 0 }).isEmpty());
            expectEquals (engine.getOutputMeterReadings().measuredSeconds, 0.0);
            expectEquals (engine.getOutputMeterReadings().maxTruePeak[0], 0.0f);
        }

        beginTest ("failed hardware rollback isolates retained audio until the original device timing returns");
        {
            juce::TemporaryFile original (juce::File::getCurrentWorkingDirectory().getChildFile ("output-transaction-original.wav"));
            juce::TemporaryFile moved (juce::File::getCurrentWorkingDirectory().getChildFile ("output-transaction-moved.wav"));
            writeAudio (original.getFile(), false);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.loadPlaybackPlan ({ { { original.getFile(), 73 } }, -1 }));
            juce::Thread::sleep (100);
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            const auto before = engine.getPlaybackProgress();
            expect (original.getFile().moveFileTo (moved.getFile()));
            device->setupFailuresRemaining = 2;
            device->mutateOnSetupFailure = true;
            device->leaveRollbackFailureUnchanged = true;
            expect (engine.applyListeningConfiguration (alternateConfiguration(), {}).isNotEmpty());
            expectEquals (engine.getCurrentAudioSampleRate(), 96000.0);
            expect (engine.setOutputRouting ({ 0, 1 }).isEmpty());
            device->restartAt (96000.0);
            device->render (block);
            expectEquals (block.getMagnitude (0, 256), 0.0f);
            expect (! engine.hasReadError());
            expect (engine.isPlaying());
            expectEquals (engine.getPlaybackProgress().token, before.token);
            expectEquals (engine.getCurrentPositionSeconds(), before.positionSeconds);
            expect (engine.selectOutputDevice ("Offline 16ch").isEmpty());
            device->render (block);
            expect (engine.isPlaying());
            expect (! engine.hasReadError());
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.0001f);
        }

        for (const auto& [previousLayout, requestedLayout, previousChannels, requestedChannels]
             : { std::tuple { OutputLayout::stereo, OutputLayout::fiveOne, 2, 6 },
                 std::tuple { OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo, 8, 10 } })
        {
            beginTest ("failed " + juce::String (previousChannels) + " to " + juce::String (requestedChannels)
                + " channel preparation restores host buses even when hardware rollback fails");
            juce::TemporaryFile file (juce::File::getCurrentWorkingDirectory().getChildFile ("layout-rollback.wav"));
            writeAudio (file.getFile(), false, 16);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            int requestedPreparations = 0;
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.setOutputLayout (previousLayout).isEmpty());
            auto plugin = std::make_unique<DelayProbePlugin> (1);
            auto* probe = plugin.get();
            juce::String error;
            expect (engine.addEffectPlugin (1, std::move (plugin), error));
            expect (engine.loadFile (file.getFile()));
            engine.play();
            juce::AudioBuffer<float> block (previousChannels, 256);
            device->render (block);
            for (int channel = 0; channel < previousChannels; ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 128), 0.08f, 0.0001f);
            const auto before = engine.getPlaybackProgress();
            probe->onPrepare = [&]
            {
                if (probe->getTotalNumOutputChannels() == requestedChannels && ++requestedPreparations == 2)
                {
                    device->setupFailuresRemaining = 1;
                    throw std::runtime_error ("Injected output preparation failure");
                }
            };

            error = engine.setOutputLayout (requestedLayout);
            expect (error.contains ("Outputs are disconnected"), error);
            expect (engine.getOutputLayout() == previousLayout);
            expectEquals (probe->getTotalNumInputChannels(), previousChannels);
            expectEquals (probe->getTotalNumOutputChannels(), previousChannels);
            expectEquals (device->getAudioDeviceSetup().outputChannels.countNumberOfSetBits(), requestedChannels);
            device->render (block);
            expectEquals (block.getMagnitude (0, block.getNumSamples()), 0.0f);
            expectEquals (engine.getCurrentPositionSeconds(), before.positionSeconds);
            expect (engine.isPlaying());
            expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine,
                juce::AudioChannelSet::left, -30.0f, 1000.0 }).isNotEmpty());
            expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);

            expect (engine.selectOutputDevice ("Offline 16ch").isEmpty());
            expect (engine.getOutputLayout() == previousLayout);
            expectEquals (probe->getTotalNumInputChannels(), previousChannels);
            expectEquals (probe->getTotalNumOutputChannels(), previousChannels);
            expectEquals (device->getAudioDeviceSetup().outputChannels.countNumberOfSetBits(), previousChannels);
            device->render (block);
            for (int channel = 0; channel < previousChannels; ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 128), 0.08f, 0.0001f);
            expect (engine.getCurrentPositionSeconds() > before.positionSeconds);
            expect (engine.isPlaying());
            expect (! engine.hasReadError());
        }

        beginTest ("failed external rate preparation preserves queued audio until the previous rate is restored");
        {
            juce::TemporaryFile original (juce::File::getCurrentWorkingDirectory().getChildFile ("external-rate-source.wav"));
            juce::TemporaryFile moved (juce::File::getCurrentWorkingDirectory().getChildFile ("external-rate-moved.wav"));
            writeAudio (original.getFile(), false);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.loadPlaybackPlan ({ { { original.getFile(), 84 } }, -1 }));
            juce::Thread::sleep (100);
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            const auto before = engine.getPlaybackProgress();
            expect (original.getFile().moveFileTo (moved.getFile()));

            device->restartAt (96000.0);
            expect (engine.takeOutputError().isNotEmpty());
            expect (engine.takeOutputError().isEmpty());
            expect (! engine.hasReadError());
            expect (engine.drainPreparationFailures().empty());
            expect (engine.isPlaying());
            expectEquals (engine.getPlaybackProgress().token, before.token);
            expectEquals (engine.getCurrentPositionSeconds(), before.positionSeconds);
            device->render (block);
            expectEquals (block.getMagnitude (0, block.getNumSamples()), 0.0f);
            expectEquals (engine.getCurrentPositionSeconds(), before.positionSeconds);

            expect (engine.setSampleRate (48000.0).isEmpty());
            device->render (block);
            expect (engine.isPlaying());
            expect (! engine.hasReadError());
            expect (engine.takeOutputError().isEmpty());
            expectEquals (engine.getPlaybackProgress().token, before.token);
            expect (engine.getCurrentPositionSeconds() > before.positionSeconds);
            for (int channel = 0; channel < 2; ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 128), 0.08f, 0.0001f);
        }

        beginTest ("an external plug-in preparation failure retains PCM for recovery after the source file moves");
        {
            juce::TemporaryFile original (juce::File::getCurrentWorkingDirectory().getChildFile ("external-plugin-source.wav"));
            juce::TemporaryFile moved (juce::File::getCurrentWorkingDirectory().getChildFile ("external-plugin-moved.wav"));
            writeAudio (original.getFile(), false);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            auto plugin = std::make_unique<DelayProbePlugin> (1);
            plugin->rejectedSampleRate = 96000.0;
            juce::String error;
            expect (engine.addEffectPlugin (1, std::move (plugin), error), error);
            expect (engine.loadPlaybackPlan ({ { { original.getFile(), 85 } }, -1 }));
            juce::Thread::sleep (100);
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            const auto before = engine.getPlaybackProgress();
            device->restartAt (96000.0);
            expect (engine.takeOutputError().isNotEmpty());
            juce::Thread::sleep (100);
            expect (original.getFile().moveFileTo (moved.getFile()));
            const auto restored = engine.setSampleRate (48000.0);
            expect (restored.isEmpty(), restored);
            expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
            expectEquals (engine.getPlaybackProgress().token, before.token);
            expectEquals (engine.getCurrentPositionSeconds(), before.positionSeconds);
            expect (engine.isPlaying());
            expect (! engine.hasReadError());
            expect (engine.drainPreparationFailures().empty());
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.0001f);
            expect (engine.getCurrentPositionSeconds() > before.positionSeconds);
        }

        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
            for (const auto blockSize : { 128, 512 })
            {
                beginTest ("resuming music during test fade preserves every frame in " + toString (layout)
                    + " with " + juce::String (blockSize) + " sample blocks");
                juce::TemporaryFile file (juce::File::getCurrentWorkingDirectory().getChildFile ("test-resume.wav"));
                writeAudio (file.getFile(), false, 16);
                auto backend = std::make_unique<OfflineAudioDeviceBackend> (blockSize);
                auto* device = backend.get();
                AudioEngine engine (std::move (backend));
                expect (engine.initialise().isEmpty());
                expect (engine.setOutputLayout (layout).isEmpty());
                expect (engine.loadFile (file.getFile()));
                expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine,
                    juce::AudioChannelSet::left, -30.0f, 1000.0 }).isEmpty());
                juce::AudioBuffer<float> block (getChannelCount (layout), blockSize);
                for (int pass = 0; pass < 10; ++pass) device->render (block);
                engine.stopCalibrationTestSignal();
                engine.play();
                for (int pass = 0; pass < 2; ++pass)
                {
                    device->render (block);
                    for (int channel = 1; channel < block.getNumChannels(); ++channel)
                        for (int sample = 0; sample < blockSize; ++sample)
                            expectWithinAbsoluteError (block.getSample (channel, sample), 0.08f, 0.0001f);
                }
                expectWithinAbsoluteError (engine.getCurrentPositionSeconds(), 2.0 * blockSize / 48000.0, 0.000001);
            }

        for (const auto target : { OutputLayout::lcr, OutputLayout::twoOne })
            for (const auto listeningPreset : { false, true })
            {
                beginTest ("same-count layout changes refresh speaker loudness mapping for " + toString (target)
                    + (listeningPreset ? " through a listening configuration" : " through device settings"));
                auto backend = std::make_unique<OfflineAudioDeviceBackend>();
                auto* device = backend.get();
                AudioEngine engine (std::move (backend));
                expect (engine.initialise().isEmpty());
                expect (engine.setOutputLayout (target == OutputLayout::lcr ? OutputLayout::twoOne : OutputLayout::lcr).isEmpty());
                if (listeningPreset)
                {
                    auto configuration = alternateConfiguration();
                    configuration.deviceIdentifier = configuration.deviceName = "Offline 16ch";
                    configuration.sampleRate = 48000.0;
                    configuration.layout = target;
                    configuration.routing = { 0, 1, 2 };
                    expect (engine.applyListeningConfiguration (configuration, {}).isEmpty());
                }
                else expect (engine.setOutputLayout (target).isEmpty());
                expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine,
                    target == OutputLayout::lcr ? juce::AudioChannelSet::centre : juce::AudioChannelSet::LFE,
                    -30.0f, 1000.0 }).isEmpty());
                juce::AudioBuffer<float> block (3, 256);
                for (int pass = 0; pass < 120; ++pass)
                {
                    device->render (block);
                    juce::Thread::sleep (1);
                }
                auto readings = engine.getOutputMeterReadings();
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
                while (readings.measuredSeconds < 0.6 && juce::Time::getMillisecondCounterHiRes() < deadline)
                {
                    juce::Thread::sleep (1);
                    readings = engine.getOutputMeterReadings();
                }
                expect (readings.available && readings.measuredSeconds >= 0.6);
                expect (block.getMagnitude (2, 0, 256) > 0.02f);
                expect (target == OutputLayout::lcr ? std::isfinite (readings.momentaryLufs)
                                                    : std::isinf (readings.momentaryLufs) && readings.momentaryLufs < 0.0);
            }

        beginTest ("ordinary external device restarts prepare new timing and keep playback audible");
        {
            juce::TemporaryFile file (juce::File::getCurrentWorkingDirectory().getChildFile ("output-transaction.wav"));
            writeAudio (file.getFile(), false);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.loadFile (file.getFile()));
            engine.play();
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            const auto before = engine.getCurrentPositionSeconds();
            device->restartAt (96000.0);
            expectWithinAbsoluteError (engine.getCurrentPositionSeconds(), before, 0.00003);
            device->render (block);
            expect (engine.isPlaying());
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.0001f);
        }

        for (const auto channel : { juce::AudioChannelSet::left, juce::AudioChannelSet::LFE })
        {
            beginTest ("resubmitting an active calibration signal preserves its phase and delayed history for speaker "
                       + juce::String (static_cast<int> (channel)));
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto controlBackend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            auto* controlDevice = controlBackend.get();
            AudioEngine engine (std::move (backend)), control (std::move (controlBackend));
            const CalibrationTestSignal signal { CalibrationTestSignal::Kind::sine, channel, -30.0f, 1000.0 };
            for (auto* candidate : { &engine, &control })
            {
                expect (candidate->initialise().isEmpty());
                expect (candidate->setOutputLayout (OutputLayout::fiveOne).isEmpty());
                expect (candidate->setOutputCalibration (channel, 0.0f, 20.0, false).isEmpty());
                expect (candidate->setCalibrationTestSignal (signal).isEmpty());
            }
            juce::AudioBuffer<float> actual (6, 256), expected (6, 256);
            for (int block = 0; block < 20; ++block) { device->render (actual); controlDevice->render (expected); }
            expect (engine.setCalibrationTestSignal (signal).isEmpty());
            device->render (actual); controlDevice->render (expected);
            for (int speaker = 0; speaker < 6; ++speaker)
                expect (std::equal (actual.getReadPointer (speaker), actual.getReadPointer (speaker) + 256,
                                   expected.getReadPointer (speaker)));
            device->restartAt (48000.0);
            expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
            expect (engine.setCalibrationTestSignal (signal).isEmpty());
            for (int block = 0; block < 20; ++block) device->render (actual);
            expect (actual.getMagnitude (0, 256) > 0.001f);
        }
    }
private:
    ListeningOutputConfiguration alternateConfiguration() const
    {
        ListeningOutputConfiguration configuration;
        configuration.deviceIdentifier = configuration.deviceName = "Offline alternate";
        configuration.layout = OutputLayout::stereo;
        configuration.sampleRate = 96000.0;
        configuration.bufferSize = 256;
        configuration.routing = { 0, 1 };
        return configuration;
    }
    void writeAudio (const juce::File& file, bool impulse, int channels = 2)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
            .withSampleRate (48000.0)
            .withChannelLayout (channels == 2 ? juce::AudioChannelSet::stereo()
                                             : juce::AudioChannelSet::discreteChannels (channels))
            .withBitsPerSample (16));
        expect (writer != nullptr);
        if (writer == nullptr) return;
        juce::AudioBuffer<float> samples (channels, 8192);
        samples.clear();
        for (int channel = 0; channel < channels; ++channel)
            if (impulse) samples.setSample (channel, 0, 0.5f);
            else std::fill_n (samples.getWritePointer (channel), samples.getNumSamples(), 0.1f);
        expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
    }
} audioOutputTransactionTests;

class CalibrationEngineTests final : public juce::UnitTest
{
public:
    CalibrationEngineTests() : UnitTest ("CalibrationEngine", "audio") {}
    void runTest() override
    {
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                   OutputLayout::nineOneSix })
        {
            beginTest ("calibration test bypasses effects mute solo and follows physical routing in " + toString (layout));
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.setOutputLayout (layout).isEmpty());
            juce::String error;
            expect (engine.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error), error);
            const auto speakers = toAudioChannelSet (layout);
            const auto testSpeaker = speakers.getTypeOfChannel (speakers.size() - 1);
            std::vector<int> routing;
            for (int channel = 0; channel < speakers.size(); ++channel)
            {
                routing.push_back (15 - channel);
                expect (engine.setOutputChannelMix (speakers.getTypeOfChannel (channel), 0.0f, true, channel == 0));
            }
            expect (engine.setOutputRouting (routing).isEmpty());
            expect (engine.setOutputCalibration (testSpeaker, -6.0206f, 1.0, true).isEmpty());
            engine.setGain (0.4);
            expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine, testSpeaker, -30.0f, 1000.0 }).isEmpty());
            juce::AudioBuffer<float> physical (16, 256);
            for (int block = 0; block < 8; ++block) device->renderPhysicalOutputs (physical);
            {
                juce::UnitTestAllocationChecker allocations (*this);
                device->renderPhysicalOutputs (physical);
            }
            for (int port = 0; port < 16; ++port)
                expectWithinAbsoluteError (physical.getMagnitude (port, 0, 256),
                    port == routing.back() ? 0.00632455f : 0.0f, 0.00001f);
            expect (engine.getOutputPeakLevels()[static_cast<size_t> (speakers.size() - 1)] > 0.006f);
            expect (! engine.isPlaying());
            std::swap (routing[0], routing[1]);
            expect (engine.setOutputRouting (routing).isEmpty());
            expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
            expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine, testSpeaker, -30.0f, 1000.0 }).isEmpty());
            expect (engine.setOutputLayout (OutputLayout::mono).isEmpty());
            expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
            expect (engine.setOutputLayout (layout).isEmpty());
            const auto calibration = engine.getOutputCalibration();
            const auto saved = std::find_if (calibration.begin(), calibration.end(), [testSpeaker] (const auto& value)
                { return value.channel == testSpeaker; });
            expect (saved != calibration.end() && saved->inverted);
            expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine, juce::AudioChannelSet::unknown, -30.0f, 1000.0 }).isNotEmpty());
            expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine, testSpeaker, 0.0f, 1000.0 }).isNotEmpty());
        }

        beginTest ("a disconnected speaker cannot start a calibration test");
        {
            AudioEngine engine (std::make_unique<OfflineAudioDeviceBackend>());
            expect (engine.initialise().isEmpty());
            expect (engine.setOutputRouting ({ -1, 1 }).isEmpty());
            const CalibrationTestSignal signal { CalibrationTestSignal::Kind::sine,
                juce::AudioChannelSet::left, -30.0f, 1000.0 };
            expect (engine.validateCalibrationTestSignal (signal).containsIgnoreCase ("physical output"));
            expect (engine.setCalibrationTestSignal (signal).isNotEmpty());
            expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
        }

        beginTest ("a physical port removed during a device restart cannot start a calibration test");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            device->disconnectPhysicalPort (0);
            const CalibrationTestSignal signal { CalibrationTestSignal::Kind::sine,
                juce::AudioChannelSet::left, -30.0f, 1000.0 };
            expect (engine.validateCalibrationTestSignal (signal).containsIgnoreCase ("physical output"));
            expect (engine.setCalibrationTestSignal (signal).isNotEmpty());
            expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
        }

        beginTest ("effect replacement rejects an incompatible staged chain without removing the audible graph");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            juce::String error;
            auto oldPlugin = std::make_unique<MixProbePlugin>();
            auto* oldIdentity = oldPlugin.get();
            expect (engine.addEffectPlugin (1, std::move (oldPlugin), error), error);
            std::vector<AudioEngine::PreparedEffect> requested;
            requested.push_back ({ 2, std::make_unique<MixProbePlugin>(), {}, false });
            requested.push_back ({ 3, std::make_unique<MixProbePlugin>(), { juce::AudioChannelSet::LFE }, false });
            expect (engine.replaceEffectPlugins (std::move (requested)).isNotEmpty());
            expect (engine.getEffectPlugin (1) == oldIdentity);
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.00001f);
            expect (engine.replaceEffectPlugins ({}).isEmpty());
            expectEquals (engine.getEffectPluginCount(), 0);
            device->render (block);
            expectEquals (block.getMagnitude (0, 0, 256), 0.0f);
        }

        beginTest ("listening configuration commits actual routing calibration and chain together");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            engine.setGain (0.5);
            ListeningOutputConfiguration configuration;
            configuration.deviceIdentifier = configuration.deviceName = "Offline alternate";
            configuration.layout = OutputLayout::fiveOne;
            configuration.sampleRate = 96000.0;
            configuration.bufferSize = 256;
            configuration.routing = { 15, 14, 13, 12, 11, 10 };
            configuration.calibration = { { juce::AudioChannelSet::left, 6.0206f, 1.0, true } };
            configuration.channelMix = { { juce::AudioChannelSet::right, 1.0f, true, false } };
            std::vector<AudioEngine::PreparedEffect> requested;
            requested.push_back ({ 8, std::make_unique<MixProbePlugin>(), {}, false });
            expect (engine.applyListeningConfiguration (configuration, std::move (requested)).isEmpty());
            expect (engine.getOutputLayout() == OutputLayout::fiveOne);
            expectEquals (engine.getCurrentAudioSampleRate(), 96000.0);
            expectEquals (engine.getCurrentAudioDeviceName(), juce::String ("Offline alternate"));
            expectEquals (engine.getGain(), 0.5);
            expect (engine.getEffectPlugin (8) != nullptr);
            juce::AudioBuffer<float> block (16, 256);
            for (int pass = 0; pass < 3; ++pass) device->renderPhysicalOutputs (block);
            expectWithinAbsoluteError (block.getSample (15, 128), -0.1f, 0.00001f);
            expectEquals (block.getMagnitude (14, 0, 256), 0.0f);
            expectWithinAbsoluteError (block.getSample (10, 128), 0.3f, 0.00001f);
        }

        beginTest ("listening device and layout changes preserve playback identity position intent and master gain");
        {
            juce::TemporaryFile temporary (juce::File::getCurrentWorkingDirectory().getChildFile ("calibration-source.wav"));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = temporary.getFile().createOutputStream();
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> input (2, 48000);
                for (int channel = 0; channel < 2; ++channel)
                    std::fill_n (input.getWritePointer (channel), input.getNumSamples(), 0.1f);
                expect (writer->writeFromAudioSampleBuffer (input, 0, input.getNumSamples()));
                writer.reset();
                auto backend = std::make_unique<OfflineAudioDeviceBackend>();
                auto* device = backend.get();
                AudioEngine engine (std::move (backend));
                expect (engine.initialise().isEmpty());
                expect (engine.loadPlaybackPlan ({ { { temporary.getFile(), 41 } }, 0 }, 0.25));
                engine.setGain (0.37);
                engine.play();
                const auto before = engine.getPlaybackProgress();
                ListeningOutputConfiguration configuration;
                configuration.deviceIdentifier = configuration.deviceName = "Offline alternate";
                configuration.layout = OutputLayout::fiveOne;
                configuration.sampleRate = 96000.0;
                configuration.bufferSize = 256;
                configuration.routing = { 0, 1, 2, 3, 4, 5 };
                expect (engine.applyListeningConfiguration (configuration, {}).isEmpty());
                const auto after = engine.getPlaybackProgress();
                expect (engine.isPlaying());
                expectEquals (after.token, before.token);
                expectEquals (after.sequence, before.sequence);
                expectWithinAbsoluteError (after.positionSeconds, before.positionSeconds, 0.00003);
                expectWithinAbsoluteError (engine.getGain(), 0.37, 0.000001);

                beginTest ("calibration monitoring lets old effect tails decay without feeding them to speakers");
                juce::String error;
                expect (engine.addEffectPlugin (1, std::make_unique<DelayProbePlugin>(), error), error);
                juce::AudioBuffer<float> block (6, 256);
                device->render (block);
                engine.pause();
                expect (engine.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine,
                    juce::AudioChannelSet::left, -30.0f, 1000.0 }).isEmpty());
                for (int pass = 0; pass < 8; ++pass) device->render (block);
                engine.stopCalibrationTestSignal();
                for (int pass = 0; pass < 3; ++pass) device->render (block);
                expectEquals (block.getMagnitude (0, 0, 256), 0.0f);
            }
        }

        beginTest ("moving an open audio file preserves its reader through listening layout and rate changes");
        {
            juce::TemporaryFile original (juce::File::getCurrentWorkingDirectory().getChildFile ("calibration-open.wav"));
            juce::TemporaryFile moved (juce::File::getCurrentWorkingDirectory().getChildFile ("calibration-moved.wav"));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = original.getFile().createOutputStream();
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> input (2, 480000);
                for (int channel = 0; channel < 2; ++channel)
                    std::fill_n (input.getWritePointer (channel), input.getNumSamples(), 0.1f);
                expect (writer->writeFromAudioSampleBuffer (input, 0, input.getNumSamples()));
                writer.reset();
                auto backend = std::make_unique<OfflineAudioDeviceBackend>();
                auto* device = backend.get();
                AudioEngine engine (std::move (backend));
                expect (engine.initialise().isEmpty());
                expect (engine.loadPlaybackPlan ({ { { original.getFile(), 99 } }, -1 }, 0.25));
                expect (original.getFile().moveFileTo (moved.getFile()));
                engine.play();
                for (const auto layout : { OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                    OutputLayout::sevenOneTwo, OutputLayout::nineOneSix, OutputLayout::stereo })
                {
                    const auto before = engine.getPlaybackProgress();
                    ListeningOutputConfiguration configuration;
                    configuration.deviceIdentifier = configuration.deviceName = "Offline alternate";
                    configuration.layout = layout;
                    configuration.sampleRate = layout == OutputLayout::stereo ? 48000.0 : 96000.0;
                    configuration.bufferSize = 256;
                    for (int channel = 0; channel < getChannelCount (layout); ++channel) configuration.routing.push_back (channel);
                    const auto error = engine.applyListeningConfiguration (configuration, {});
                    expect (error.isEmpty(), error);
                    expect (! engine.hasReadError());
                    expect (engine.isPlaying());
                    const auto after = engine.getPlaybackProgress();
                    expectEquals (after.token, before.token);
                    expectWithinAbsoluteError (after.positionSeconds, before.positionSeconds, 0.00003);
                    juce::AudioBuffer<float> block (getChannelCount (layout), 256);
                    device->render (block);
                    expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.001f);
                }
            }
        }

        beginTest ("failed source reconfiguration restores cached audio and preserves transport intent and position");
        {
            juce::TemporaryFile original (juce::File::getCurrentWorkingDirectory().getChildFile ("calibration-cached.wav"));
            juce::TemporaryFile moved (juce::File::getCurrentWorkingDirectory().getChildFile ("calibration-cached-moved.wav"));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = original.getFile().createOutputStream();
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> input (2, 1024);
                for (int channel = 0; channel < 2; ++channel)
                    std::fill_n (input.getWritePointer (channel), input.getNumSamples(), 0.1f);
                expect (writer->writeFromAudioSampleBuffer (input, 0, input.getNumSamples()));
                writer.reset();
                auto backend = std::make_unique<OfflineAudioDeviceBackend>();
                auto* device = backend.get();
                AudioEngine engine (std::move (backend));
                expect (engine.initialise().isEmpty());
                expect (engine.loadPlaybackPlan ({ { { original.getFile(), 99 } }, -1 }));
                expect (engine.seekToSeconds (0.01));
                engine.play();
                const auto before = engine.getPlaybackProgress();
                expect (original.getFile().moveFileTo (moved.getFile()));
                juce::String error;
                expect (engine.addEffectPlugin (1, std::make_unique<DelayProbePlugin> (1), error), error);
                const auto* originalPlugin = engine.getEffectPlugin (1);
                ListeningOutputConfiguration configuration;
                configuration.deviceIdentifier = configuration.deviceName = "Offline alternate";
                configuration.layout = OutputLayout::fiveOne;
                configuration.sampleRate = 96000.0;
                configuration.bufferSize = 256;
                configuration.routing = { 0, 1, 2, 3, 4, 5 };
                const auto failure = engine.applyListeningConfiguration (configuration, {});
                expect (failure.isNotEmpty());
                expect (failure.containsIgnoreCase ("source"));
                expect (engine.getEffectPlugin (1) == originalPlugin);
                expect (engine.getOutputLayout() == OutputLayout::stereo);
                expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
                expect (! engine.hasReadError());
                expect (engine.isPlaying());
                expect (engine.drainPreparationFailures().empty());
                expectEquals (engine.getPlaybackProgress().token, before.token);
                expectWithinAbsoluteError (engine.getCurrentPositionSeconds(), before.positionSeconds, 0.00003);
                juce::AudioBuffer<float> block (2, 256);
                device->render (block);
                expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.001f);
            }
        }

        beginTest ("failed listening device apply retains old calibration graph and audio after verified rollback");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            juce::String error;
            expect (engine.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error), error);
            const auto* oldPlugin = engine.getEffectPlugin (1);
            ListeningOutputConfiguration configuration;
            configuration.deviceIdentifier = configuration.deviceName = "Offline 16ch";
            configuration.layout = OutputLayout::fiveOne;
            configuration.sampleRate = 96000.0;
            configuration.bufferSize = 256;
            configuration.routing = { 15, 14, 13, 12, 11, 10 };
            configuration.calibration = { { juce::AudioChannelSet::left, -24.0f, 20.0, true } };
            configuration.deviceIdentifier = "disconnected-device-identity";
            expect (engine.applyListeningConfiguration (configuration, {}).isNotEmpty());
            expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
            expect (engine.getEffectPlugin (1) == oldPlugin);
            configuration.deviceIdentifier = "Offline 16ch";
            device->setupFailuresRemaining = 1;
            device->mutateOnSetupFailure = true;
            expect (engine.applyListeningConfiguration (configuration, {}).isNotEmpty());
            expect (engine.getOutputLayout() == OutputLayout::stereo);
            expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
            expect (engine.getEffectPlugin (1) == oldPlugin);
            expect (engine.getDeviceSettings().outputRouting == std::vector<int> ({ 0, 1 }));
            juce::AudioBuffer<float> block (2, 256);
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 128), 0.08f, 0.00001f);
            device->setupFailuresRemaining = 2;
            device->mutateOnSetupFailure = false;
            const auto rollbackError = engine.applyListeningConfiguration (configuration, {});
            expect (rollbackError.contains ("Outputs are disconnected"));
            expect (engine.getEffectPlugin (1) == oldPlugin);
            expect (engine.getDeviceSettings().outputRouting == std::vector<int> ({ -1, -1 }));
            device->render (block);
            expectEquals (block.getMagnitude (0, 0, 256), 0.0f);
        }

        for (const auto& operation : { "listening preset", "output layout", "sample rate", "output device" })
            checkBufferedEngineRollback (operation);

    }

private:
    void checkBufferedEngineRollback (const juce::String& operation)
    {
        beginTest ("a failed " + operation + " change retains queued playback and device configuration");
        const auto directory = juce::File::getCurrentWorkingDirectory();
        juce::TemporaryFile original (directory.getChildFile ("buffered-engine-source.wav"));
        juce::TemporaryFile moved (directory.getChildFile ("buffered-engine-moved.wav"));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = original.getFile().createOutputStream();
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
            .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
        expect (writer != nullptr);
        if (writer == nullptr) return;
        juce::AudioBuffer<float> input (2, 8192);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < input.getNumSamples(); ++sample)
                input.setSample (channel, sample, 0.1f + 0.02f * std::sin (static_cast<float> (sample) * 0.01f));
        expect (writer->writeFromAudioSampleBuffer (input, 0, input.getNumSamples()));
        writer.reset();
        auto backend = std::make_unique<OfflineAudioDeviceBackend>();
        auto controlBackend = std::make_unique<OfflineAudioDeviceBackend>();
        auto* device = backend.get();
        auto* controlDevice = controlBackend.get();
        AudioEngine engine (std::move (backend)), control (std::move (controlBackend));
        for (auto* candidate : { &engine, &control })
        {
            expect (candidate->initialise().isEmpty());
            expect (candidate->loadPlaybackPlan ({ { { original.getFile(), 41 } }, -1 }));
            candidate->play();
        }
        juce::AudioBuffer<float> actual (2, 256), expected (2, 256);
        device->render (actual);
        controlDevice->render (expected);
        const auto before = engine.getPlaybackProgress();
        expect (original.getFile().moveFileTo (moved.getFile()));
        juce::String error;
        if (operation == "output layout") error = engine.setOutputLayout (OutputLayout::fiveOne);
        else if (operation == "sample rate") error = engine.setSampleRate (96000.0);
        else if (operation == "output device") error = engine.selectOutputDevice ("Offline alternate");
        else
        {
            ListeningOutputConfiguration configuration;
            configuration.deviceIdentifier = configuration.deviceName = "Offline alternate";
            configuration.layout = OutputLayout::fiveOne;
            configuration.sampleRate = 96000.0;
            configuration.bufferSize = 256;
            configuration.routing = { 0, 1, 2, 3, 4, 5 };
            error = engine.applyListeningConfiguration (configuration, {});
        }
        expect (error.containsIgnoreCase ("source"), error);
        expectEquals (engine.getCurrentAudioDeviceName(), juce::String ("Offline 16ch"));
        expectEquals (engine.getCurrentAudioSampleRate(), 48000.0);
        expect (engine.getOutputLayout() == OutputLayout::stereo);
        expect (engine.isPlaying());
        expect (! engine.hasReadError());
        expectEquals (engine.getPlaybackProgress().positionSeconds, before.positionSeconds);
        expect (engine.drainPreparationFailures().empty());
        for (int frame = 256; frame < 8192; frame += 256)
        {
            device->render (actual);
            controlDevice->render (expected);
            for (int channel = 0; channel < 2; ++channel)
                expect (std::equal (actual.getReadPointer (channel), actual.getReadPointer (channel) + 256,
                                    expected.getReadPointer (channel)), "Buffered audio differs from uninterrupted playback");
        }
    }
} calibrationEngineTests;

class AudioEngineTests final : public juce::UnitTest
{
public:
    AudioEngineTests()
        : UnitTest ("AudioEngine", "audio")
    {
    }

    void runTest() override
    {
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
        {
            beginTest ("physical routing follows logical effects and mixer in " + toString (layout));
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            expect (playback.initialise().isEmpty());
            expect (playback.setOutputLayout (layout).isEmpty());
            juce::String error;
            expect (playback.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error), error);
            const auto count = getChannelCount (layout);
            std::vector<int> route;
            for (int channel = 0; channel < count; ++channel)
                route.push_back (15 - channel);
            route[1] = -1;
            expect (playback.setOutputRouting (route).isEmpty());
            const auto channels = toAudioChannelSet (layout);
            expect (playback.setOutputChannelMix (channels.getTypeOfChannel (0), 0.5f, false, false));
            juce::AudioBuffer<float> packed (count - 1, 256);
            for (int block = 0; block < 5; ++block) device->render (packed);
            {
                juce::UnitTestAllocationChecker allocationChecker (*this);
                device->render (packed);
            }
            int packedIndex = 0;
            for (int logical = count - 1; logical >= 0; --logical)
            {
                if (logical == 1) continue;
                expectWithinAbsoluteError (packed.getSample (packedIndex++, 128),
                    0.08f * static_cast<float> (logical + 1) * (logical == 0 ? 0.5f : 1.0f), 0.00001f);
            }
            expect (playback.getDeviceSettings().outputRouting == route);
            juce::AudioBuffer<float> physicalOutputs (16, 256);
            {
                juce::UnitTestAllocationChecker allocationChecker (*this);
                device->renderPhysicalOutputs (physicalOutputs);
            }
            for (int physical = 0; physical < 16; ++physical)
            {
                const auto mapped = std::find (route.begin(), route.end(), physical);
                const auto logical = static_cast<int> (std::distance (route.begin(), mapped));
                const auto expected = mapped == route.end() ? 0.0f
                    : 0.08f * static_cast<float> (logical + 1) * (logical == 0 ? 0.5f : 1.0f);
                expectWithinAbsoluteError (physicalOutputs.getSample (physical, 128), expected, 0.00001f);
            }
            if (count > 2)
            {
                device->disconnectPhysicalPort (15);
                device->renderPhysicalOutputs (physicalOutputs);
                expectWithinAbsoluteError (physicalOutputs.getSample (15, 128), 0.0f, 0.00001f);
                expectWithinAbsoluteError (physicalOutputs.getSample (13, 128), 0.24f, 0.00001f);
            }
            expect (playback.setOutputRouting (std::vector<int> (static_cast<size_t> (count), -1)).isEmpty());
            juce::AudioBuffer<float> silent (1, 256);
            {
                juce::UnitTestAllocationChecker allocationChecker (*this);
                device->render (silent);
            }
            expectWithinAbsoluteError (silent.getMagnitude (0, 256), 0.0f, 0.00001f);
            expect (playback.getOutputPeakLevels()[0] > 0.0f,
                    "The logical bus remains active with every physical route disconnected.");
        }

        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                                   OutputLayout::sevenOneTwo, OutputLayout::sevenOneFour,
                                   OutputLayout::sevenOneSix, OutputLayout::nineOneSix })
        {
            beginTest ("channel gain, mute and multi-solo follow effects and feed the output meter in " + toString (layout));
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            playback.setGain (0.5);
            expect (playback.initialise().isEmpty());
            expect (playback.setOutputLayout (layout).isEmpty());
            juce::String error;
            expect (playback.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error), error);
            const auto channels = toAudioChannelSet (layout);
            juce::AudioBuffer<float> block (channels.size(), 256);
            for (int index = 0; index < 4; ++index) device->render (block);
            expect (playback.setOutputChannelMix (channels.getTypeOfChannel (0), 2.0f, false, false));
            expect (playback.setOutputChannelMix (channels.getTypeOfChannel (1), 0.5f, true, false));
            {
                juce::UnitTestAllocationChecker allocations (*this);
                device->render (block);
            }
            expectWithinAbsoluteError (block.getSample (0, 0), 0.05f * (1.0f + 1.0f / 960.0f), 0.00001f);
            expectWithinAbsoluteError (block.getSample (0, 255), 0.05f * (1.0f + 256.0f / 960.0f), 0.00001f);
            for (int index = 0; index < 4; ++index) device->render (block);
            for (int channel = 0; channel < channels.size(); ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 255),
                    channel == 0 ? 0.1f : channel == 1 ? 0.0f : 0.05f * static_cast<float> (channel + 1), 0.00001f);
            playback.resetOutputMeter();
            for (int index = 0; index < 64; ++index)
            {
                device->render (block);
                juce::Thread::sleep (1);
            }
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (playback.getOutputMeterReadings().measuredSeconds < 64.0 * 256.0 / 48000.0
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (1);
            const auto readings = playback.getOutputMeterReadings();
            expect (readings.available && ! readings.measurementInterrupted);
            for (int channel = 0; channel < channels.size(); ++channel)
            {
                expectWithinAbsoluteError (readings.samplePeak[static_cast<size_t> (channel)], block.getSample (channel, 255), 0.00001f);
                expectWithinAbsoluteError (readings.rms[static_cast<size_t> (channel)], block.getSample (channel, 255), 0.00001f);
            }
            expect (playback.setOutputChannelMix (channels.getTypeOfChannel (0), 2.0f, false, true));
            expect (playback.setOutputChannelMix (channels.getTypeOfChannel (1), 0.5f, false, true));
            for (int index = 0; index < 4; ++index) device->render (block);
            for (int channel = 0; channel < channels.size(); ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 255), channel == 0 ? 0.1f : channel == 1 ? 0.05f : 0.0f, 0.00001f);
            expect (playback.setOutputChannelMix (channels.getTypeOfChannel (0), 2.0f, true, true));
            for (int index = 0; index < 4; ++index) device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 255), 0.0f, 0.00001f, "mute takes precedence over solo");
            expectWithinAbsoluteError (block.getSample (1, 255), 0.05f, 0.00001f);
            playback.resetOutputChannelMix();
            for (int index = 0; index < 4; ++index) device->render (block);
            for (int channel = 0; channel < channels.size(); ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 255), 0.05f * static_cast<float> (channel + 1), 0.00001f);
        }

        beginTest ("absent solo identities do not silence the current layout and return with their saved mix");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            playback.setGain (0.5);
            expect (playback.initialise().isEmpty());
            juce::String error;
            expect (playback.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error), error);
            const auto height = juce::AudioChannelSet::topSideLeft;
            expect (playback.setOutputChannelMix (height, 0.25f, false, true));
            juce::AudioBuffer<float> stereo (2, 256);
            for (int index = 0; index < 4; ++index) device->render (stereo);
            expectWithinAbsoluteError (stereo.getSample (0, 255), 0.05f, 0.00001f);
            expectWithinAbsoluteError (stereo.getSample (1, 255), 0.1f, 0.00001f);
            const auto before = playback.getOutputChannelMix();
            expect (! playback.setOutputChannelMix (juce::AudioChannelSet::unknown, 1.0f, false, false));
            expect (! playback.setOutputChannelMix (static_cast<juce::AudioChannelSet::ChannelType> (999), 1.0f, false, false));
            for (const auto gain : { -0.01f, 2.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
                expect (! playback.setOutputChannelMix (height, gain, false, false));
            expect (playback.getOutputChannelMix() == before);
            expect (playback.setOutputLayout (OutputLayout::nineOneSix).isEmpty());
            juce::AudioBuffer<float> immersive (16, 256);
            for (int index = 0; index < 4; ++index) device->render (immersive);
            const auto channels = toAudioChannelSet (OutputLayout::nineOneSix);
            for (int channel = 0; channel < 16; ++channel)
                expectWithinAbsoluteError (immersive.getSample (channel, 255),
                    channels.getTypeOfChannel (channel) == height ? 0.0125f * static_cast<float> (channel + 1) : 0.0f, 0.00001f);
            expect (playback.setOutputLayout (OutputLayout::stereo).isEmpty());
            device->render (stereo);
            expectWithinAbsoluteError (stereo.getSample (0, 0), 0.05f, 0.00001f, "layout prepare must start with that layout's mapped mix");
            expect (playback.getOutputChannelMix() == before);
        }

        beginTest ("controller mix edits persist speaker identities and increment revisions only on changes");
        {
            PlayerController controller (std::make_unique<OfflineAudioDeviceBackend>());
            const auto firstRevision = controller.getStateRevision();
            expect (controller.setOutputChannelMix (juce::AudioChannelSet::left, 1.0f, false, false));
            controller.resetOutputChannelMix();
            expectEquals (controller.getStateRevision(), firstRevision);
            expect (controller.setOutputChannelMix (juce::AudioChannelSet::left, 0.5f, true, false));
            expectEquals (controller.getStateRevision(), firstRevision + 1);
            expect (controller.setOutputChannelMix (juce::AudioChannelSet::topSideLeft, 1.5f, false, true));
            expectEquals (controller.getStateRevision(), firstRevision + 2);
            PlayerAppState snapshot;
            juce::String error;
            expect (controller.captureStateSnapshot (snapshot, error), error);
            StateRestoreResult saved;
            saved.state = AppStateStore::fromValueTree (AppStateStore::toValueTree (snapshot));
            PlayerController restored (std::make_unique<OfflineAudioDeviceBackend>());
            expect (restored.applyPreparedState (std::move (saved)));
            expect (restored.getOutputChannelMix() == controller.getOutputChannelMix());
            expect (restored.setOutputLayout (OutputLayout::nineOneSix).isEmpty());
            expect (restored.getOutputChannelMix() == snapshot.outputChannelMix);
            const auto revision = restored.getStateRevision();
            restored.resetOutputChannelMix();
            expectEquals (restored.getStateRevision(), revision + 1);
            expect (restored.getOutputChannelMix() == defaultOutputChannelMix());
            restored.resetOutputChannelMix();
            expectEquals (restored.getStateRevision(), revision + 1);
        }

        beginTest ("unload detaches the reader and clears transport duration");
        const auto directory = juce::File::getCurrentWorkingDirectory()
                                   .getChildFile ("build")
                                   .getChildFile ("audio-engine-tests");
        expect (directory.createDirectory());
        const auto file = directory.getNonexistentChildFile ("unload", ".wav");

        juce::WavAudioFormat format;
        auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
        auto writer = format.createWriterFor (output,
                                               juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (48000.0)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
        expect (writer != nullptr);
        if (writer != nullptr)
        {
            juce::AudioBuffer<float> samples (2, 4800);
            samples.clear();
            expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
            writer.reset();
        }

        AudioEngine engine;
        expect (engine.loadFile (file));
        expect (engine.getLengthInSeconds() > 0.09);
        engine.seekToSeconds (0.05);
        engine.unload();
        expectWithinAbsoluteError (engine.getLengthInSeconds(), 0.0, 0.000001);
        expectWithinAbsoluteError (engine.getCurrentPositionSeconds(), 0.0, 0.000001);
        for (const auto level : engine.getOutputPeakLevels())
            expectWithinAbsoluteError (level, 0.0f, 0.000001f);

        beginTest ("short tracks repeat through audio boundaries between UI polls");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* offlineDevice = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ file });
            controller.setRepeatMode (RepeatMode::one);
            expect (controller.playOrPause());
            juce::AudioBuffer<float> rendered (2, 256);
            for (int block = 0; block < 70; ++block)
            {
                offlineDevice->render (rendered);
                juce::Thread::sleep (1);
            }
            expect (controller.isPlaying());
            bool preparationFailed = true;
            expect (controller.synchronizePlayback (true, &preparationFailed));
            expect (! preparationFailed, "Normal repeat boundaries are not preparation failures.");
            expect (controller.getCurrentPositionSeconds() < 0.1);
            controller.setRepeatMode (RepeatMode::off);
            for (int block = 0; block < 24; ++block)
            {
                offlineDevice->render (rendered);
                juce::Thread::sleep (1);
            }
            preparationFailed = true;
            expect (controller.synchronizePlayback (true, &preparationFailed));
            expect (! preparationFailed, "Normal queue completion is not a preparation failure.");
            expect (controller.getViewState().transportIntent == TransportIntent::stopped);
        }

        beginTest ("controller seeking reports failure without misreporting an unchanged valid position");
        {
            PlayerController controller (std::make_unique<OfflineAudioDeviceBackend>());
            expect (! controller.seekToSeconds (0.05));
            controller.addFiles ({ file });
            expect (controller.selectTrack (0));
            expect (controller.seekToSeconds (0.0));
            expect (controller.seekToRatio (0.5));
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), 0.05, 0.00001);
            expect (controller.seekToSeconds (0.05));
            expect (! controller.seekToSeconds (std::numeric_limits<double>::quiet_NaN()));
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), 0.05, 0.00001);
            controller.clearPlaylist();
            expect (! controller.seekToRatio (0.5));
        }

        beginTest ("controller reports future preparation failures once, independently of playback changes");
        {
            const auto removed = directory.getNonexistentChildFile ("future-unavailable", ".wav");
            expect (file.copyFileTo (removed));
            PlayerController controller (std::make_unique<OfflineAudioDeviceBackend>());
            controller.addFiles ({ file, removed });
            expect (removed.deleteFile());
            expect (controller.playOrPause());
            bool preparationFailed = false;
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (! preparationFailed && juce::Time::getMillisecondCounterHiRes() < deadline)
            {
                controller.synchronizePlayback (true, &preparationFailed);
                if (! preparationFailed) juce::Thread::sleep (1);
            }
            expect (preparationFailed);
            expect (! controller.getPlaylist().getTrack (1)->available);
            expect (controller.getLastStatusMessage().contains ("Skipped unavailable audio"));
            preparationFailed = true;
            controller.synchronizePlayback (true, &preparationFailed);
            expect (! preparationFailed, "A consumed failure must not redisplay on every UI poll.");
            controller.clearPlaylist();
        }

        beginTest ("controller skips missing manual candidates and records actual playback history");
        {
            const auto second = directory.getNonexistentChildFile ("navigation-second", ".wav");
            const auto third = directory.getNonexistentChildFile ("navigation-third", ".wav");
            expect (file.copyFileTo (second));
            expect (file.copyFileTo (third));
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* offlineDevice = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ file, second, third });
            controller.setRepeatMode (RepeatMode::one);
            expect (controller.playOrPause());
            expect (controller.nextTrack());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 1);
            expect (controller.previousTrack());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 0);
            expect (second.deleteFile());
            expect (controller.nextTrack());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 2);
            expect (! controller.getPlaylist().getTrack (1)->available);
            expect (controller.previousTrack());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 0);
            expect (file.copyFileTo (second));
            expect (controller.selectTrack (1));
            expect (controller.getPlaylist().getTrack (1)->available);
            controller.setRepeatMode (RepeatMode::off);
            juce::AudioBuffer<float> rendered (2, 256);
            for (int block = 0; block < 48; ++block)
            {
                offlineDevice->render (rendered);
                juce::Thread::sleep (1);
            }
            expect (controller.synchronizePlayback());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 2);
            controller.clearPlaylist();
            expect (second.deleteFile());
            expect (third.deleteFile());
        }

        beginTest ("an entirely unreadable repeated queue stops after bounded candidate attempts");
        {
            PlayerController controller (std::make_unique<OfflineAudioDeviceBackend>());
            controller.addFiles ({ file });
            controller.setRepeatMode (RepeatMode::all);
            const auto moved = file.getSiblingFile ("temporarily-moved.wav");
            expect (file.moveFileTo (moved));
            expect (! controller.playOrPause());
            expect (! controller.isPlaying());
            expect (! controller.getPlaylist().getTrack (0)->available);
            expect (moved.moveFileTo (file));
            expect (controller.playOrPause());
            controller.clearPlaylist();
        }

        expect (file.deleteFile());

        for (const auto sourceSampleRate : { 48000.0, 44100.0 })
        {
            beginTest ("all output channels carry their source signal at "
                       + juce::String (sourceSampleRate, 0) + " Hz");
            const auto multichannelFile = directory.getNonexistentChildFile (
                "discrete-sixteen-channel", ".wav");
            auto multichannelOutput = std::unique_ptr<juce::OutputStream> (
                multichannelFile.createOutputStream());
            auto multichannelWriter = format.createWriterFor (
                multichannelOutput, juce::AudioFormatWriterOptions{}
                    .withSampleRate (sourceSampleRate)
                    .withChannelLayout (juce::AudioChannelSet::discreteChannels (16))
                    .withBitsPerSample (32)
                    .withSampleFormat (
                        juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (multichannelWriter != nullptr);
            if (multichannelWriter == nullptr)
                continue;

            juce::AudioBuffer<float> signal (16, 48000);
            for (int channel = 0; channel < signal.getNumChannels(); ++channel)
                std::fill_n (signal.getWritePointer (channel), signal.getNumSamples(),
                             0.04f * static_cast<float> (channel + 1));
            expect (multichannelWriter->writeFromAudioSampleBuffer (
                signal, 0, signal.getNumSamples()));
            multichannelWriter.reset();

            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* offlineDevice = backend.get();
            AudioEngine multichannelEngine (std::move (backend));
            expect (multichannelEngine.initialise().isEmpty());

            const auto verifyOutput = [&] (OutputLayout layout)
            {
                expectEquals (offlineDevice->getAudioDeviceSetup().outputChannels.countNumberOfSetBits(),
                              getChannelCount (layout), "The backend must enable the requested physical outputs.");
                juce::AudioBuffer<float> rendered (getChannelCount (layout), 256);
                for (int block = 0; block < 8; ++block)
                    offlineDevice->render (rendered);

                for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
                    expectWithinAbsoluteError (
                        rendered.getSample (channel, rendered.getNumSamples() - 1),
                        0.04f * static_cast<float> (channel + 1)
                            * static_cast<float> (multichannelEngine.getGain()),
                        0.0001f,
                        toString (layout) + " output channel " + juce::String (channel + 1));
            };
            const auto finishPlayback = [&]
            {
                multichannelEngine.seekToSeconds (multichannelEngine.getLengthInSeconds());
                juce::AudioBuffer<float> rendered (
                    getChannelCount (multichannelEngine.getOutputLayout()), 256);
                offlineDevice->render (rendered);
            };

            for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                       OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                   OutputLayout::sevenOneFour, OutputLayout::sevenOneSix,
                                   OutputLayout::nineOneFour, OutputLayout::nineOneSix })
            {
                expect (multichannelEngine.setOutputLayout (layout).isEmpty());
                expect (multichannelEngine.loadFile (multichannelFile));
                multichannelEngine.play();
                verifyOutput (layout);
                finishPlayback();
            }

            beginTest ("loaded audio retains all channels across output layout changes at "
                       + juce::String (sourceSampleRate, 0) + " Hz");
            expect (multichannelEngine.setOutputLayout (OutputLayout::stereo).isEmpty());
            expect (multichannelEngine.loadFile (multichannelFile));
            multichannelEngine.play();
            for (const auto layout : { OutputLayout::stereo, OutputLayout::sevenOneTwo,
                                       OutputLayout::sevenOneFour, OutputLayout::sevenOneSix,
                                       OutputLayout::nineOneFour, OutputLayout::nineOneSix,
                                       OutputLayout::stereo, OutputLayout::nineOneSix })
            {
                expect (multichannelEngine.setOutputLayout (layout).isEmpty());
                verifyOutput (layout);
            }
            finishPlayback();
            multichannelEngine.unload();
            expect (multichannelFile.deleteFile());
        }

        const auto writeSignal = [&] (const juce::String& name, int samples,
                                      double rate, float level, int channels = 16)
        {
            const auto signalFile = directory.getNonexistentChildFile (name, ".wav");
            auto stream = std::unique_ptr<juce::OutputStream> (signalFile.createOutputStream());
            auto signalWriter = format.createWriterFor (
                stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate (rate)
                    .withChannelLayout (channels == 2 ? juce::AudioChannelSet::stereo()
                                                    : juce::AudioChannelSet::discreteChannels (channels))
                    .withBitsPerSample (32)
                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (signalWriter != nullptr);
            if (signalWriter)
            {
                juce::AudioBuffer<float> signal (channels, samples);
                for (int channel = 0; channel < channels; ++channel)
                    std::fill_n (signal.getWritePointer (channel), samples,
                                 level * static_cast<float> (channel + 1));
                expect (signalWriter->writeFromAudioSampleBuffer (signal, 0, samples));
            }
            return signalFile;
        };
        const auto first = writeSignal ("gapless-first", 97, 48000.0, 0.02f);
        const auto second = writeSignal ("gapless-second", 159, 48000.0, 0.04f);
        const auto third = writeSignal ("gapless-third", 2000, 48000.0, 0.06f);
        const auto shortLoop = writeSignal ("gapless-loop", 17, 48000.0, 0.02f);
        const auto differentRate = writeSignal ("gapless-rate", 147, 44100.0, 0.04f);

        beginTest ("global monitoring bypass preserves multichannel source, slot bypass and transport while clear tails remains independent");
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
        {
            const auto channels = getChannelCount (layout);
            const auto source = writeSignal ("bypass-" + juce::String (channels), 48000, 48000.0, 0.02f, channels);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            expect (playback.initialise().isEmpty());
            expect (playback.setOutputLayout (layout).isEmpty());
            expect (playback.loadFile (source));
            juce::String error;
            playback.play();
            juce::AudioBuffer<float> block (channels, 1024);
            device->render (block);
            // The source may not contain every speaker in the selected output layout.
            std::vector<float> dry;
            for (int channel = 0; channel < channels; ++channel) dry.push_back (block.getSample (channel, 1023));
            expect (playback.addEffectPlugin (1, std::make_unique<MixProbePlugin>(), error), error);
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 1023), 0.08f, 0.0001f);
            const auto position = playback.getCurrentPositionSeconds();
            playback.setAllEffectsBypassed (true);
            {
                juce::UnitTestAllocationChecker allocations (*this);
                device->render (block);
            }
            expect (playback.areAllEffectsBypassed());
            expect (block.getSample (0, 0) < 0.08f && block.getSample (0, 0) > 0.016f,
                    "The monitoring switch fades rather than jumping.");
            for (int channel = 0; channel < channels; ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 1023), dry[static_cast<size_t> (channel)], 0.0001f);
            expect (playback.getCurrentPositionSeconds() > position && playback.isPlaying());
            expect (playback.clearEffectTails (error), error);
            expect (playback.areAllEffectsBypassed());
            expect (playback.setEffectBypassed (1, true));
            playback.setAllEffectsBypassed (false);
            device->render (block);
            for (int channel = 0; channel < channels; ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 1023), dry[static_cast<size_t> (channel)], 0.0001f);
            expect (playback.setEffectBypassed (1, false));
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 1023), 0.08f, 0.0001f);
            playback.setAllEffectsBypassed (true);
            expect (playback.clearEffectPlugins());
            expect (playback.addEffectPlugin (2, std::make_unique<MixProbePlugin>(), error), error);
            device->render (block);
            expectWithinAbsoluteError (block.getSample (0, 1023), 0.016f, 0.0001f,
                                       "New effects respect the active master bypass.");
            playback.seekToSeconds (playback.getLengthInSeconds());
            device->render (block);
            expect (! playback.isPlaying());
            expect (playback.clearEffectTails (error), error);
            device->render (block);
            expectEquals (block.getMagnitude (0, 1024), 0.0f);
            playback.unload();
            expect (source.deleteFile());
        }

        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                   OutputLayout::nineOneSix })
        {
            beginTest ("monitoring bypass aligns dry audio with scoped graph latency in " + toString (layout));
            const auto channels = getChannelCount (layout);
            const auto source = directory.getNonexistentChildFile ("latency-bypass-" + toString (layout), ".wav");
            const auto sampleAt = [] (int channel, int sample)
            {
                return 0.02f * static_cast<float> (channel + 1)
                    * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * sample / 128.0));
            };
            {
                auto stream = std::unique_ptr<juce::OutputStream> (source.createOutputStream());
                auto signalWriter = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate (48000.0).withChannelLayout (juce::AudioChannelSet::discreteChannels (16))
                    .withBitsPerSample (32)
                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                expect (signalWriter != nullptr, "Could not create the discrete multichannel latency fixture.");
                if (signalWriter)
                {
                    juce::AudioBuffer<float> signal (16, 48000);
                    for (int channel = 0; channel < signal.getNumChannels(); ++channel)
                        for (int sample = 0; sample < signal.getNumSamples(); ++sample)
                            signal.setSample (channel, sample, sampleAt (channel, sample));
                    expect (signalWriter->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples()));
                }
            }
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            expect (playback.initialise().isEmpty());
            expect (playback.setOutputLayout (layout).isEmpty());
            playback.setGain (1.0);
            expect (playback.loadFile (source), "Could not load the discrete multichannel latency fixture.");
            auto plugin = std::make_unique<DelayProbePlugin> (64, true);
            auto* probe = plugin.get();
            juce::String error;
            expect (playback.addEffectPlugin (1, std::move (plugin), error,
                                              { juce::AudioChannelSet::left }), error);
            playback.play();
            juce::AudioBuffer<float> block (channels, 256);
            int renderedSamples = 0;
            const auto render = [&]
            {
                juce::UnitTestAllocationChecker allocations (*this);
                device->render (block);
                renderedSamples += block.getNumSamples();
            };
            const auto expectAligned = [&] (int blocks, int latency, int clearedPrefix = 0)
            {
                auto maximumError = 0.0f;
                for (int index = 0; index < blocks; ++index)
                {
                    const auto start = renderedSamples;
                    render();
                    for (int channel = 0; channel < channels; ++channel)
                        for (int sample = 0; sample < block.getNumSamples(); ++sample)
                        {
                            const auto expected = index * block.getNumSamples() + sample < clearedPrefix
                                ? 0.0f : sampleAt (channel, start + sample - latency);
                            maximumError = std::max (maximumError, std::abs (block.getSample (channel, sample) - expected));
                        }
                }
                expectWithinAbsoluteError (maximumError, 0.0f, 0.00001f);
            };
            for (int index = 0; index < 8; ++index) render();
            expectAligned (1, 64);
            for (const auto bypassed : { true, false })
            {
                playback.setAllEffectsBypassed (bypassed);
                expectAligned (5, 64);
                if (bypassed)
                {
                    expect (playback.setEffectPluginOrder ({ 1 }));
                    expectAligned (1, 64);
                }
            }

            probe->changeLatency (160);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
            for (int index = 0; index < 4; ++index) render();
            expectAligned (1, 160);
            playback.setAllEffectsBypassed (true);
            expectAligned (5, 160);
            expect (playback.clearEffectTails (error), error);
            expectAligned (1, 160, 160);
            playback.setAllEffectsBypassed (false);
            expectAligned (5, 160);
            playback.unload();
            expect (source.deleteFile());
        }

        beginTest ("effect monitoring state survives workspace switches and is absent from saved sessions");
        {
            PlayerController controller (std::make_unique<OfflineAudioDeviceBackend>());
            controller.setAllEffectsBypassed (true);
            controller.setWorkspaceMode (WorkspaceMode::studio);
            expect (controller.getViewState().allEffectsBypassed);
            controller.setWorkspaceMode (WorkspaceMode::listening);
            expect (controller.getViewState().allEffectsBypassed);
            PlayerAppState snapshot;
            juce::String error;
            expect (controller.captureStateSnapshot (snapshot, error), error);
            StateRestoreResult restored;
            restored.state = std::move (snapshot);
            expect (controller.applyPreparedState (std::move (restored)));
            expect (! controller.getViewState().allEffectsBypassed);
        }
        const auto missing = directory.getNonexistentChildFile ("missing", ".wav");

        beginTest ("output meter observes rendered gain and resets on explicit playback discontinuities");
        {
            const auto meterFile = writeSignal ("meter-output", 96000, 48000.0, 0.05f);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            expect (playback.initialise().isEmpty());
            playback.setGain (0.5);
            expect (playback.loadFile (meterFile));
            playback.play();
            juce::AudioBuffer<float> block (2, 256);
            for (int index = 0; index < 72; ++index)
            {
                device->render (block);
                juce::Thread::sleep (1);
            }
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (playback.getOutputMeterReadings().measuredSeconds < 72.0 * 256.0 / 48000.0
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (1);
            const auto reading = playback.getOutputMeterReadings();
            expect (reading.available);
            expect (! reading.measurementInterrupted);
            expectWithinAbsoluteError (reading.samplePeak[0], block.getMagnitude (0, 0, 256), 0.0001f);
            expectWithinAbsoluteError (reading.samplePeak[0], 0.025f, 0.0001f);
            expectWithinAbsoluteError (reading.rms[1], 0.05f, 0.0001f);
            playback.resetOutputMeter();
            expectEquals (playback.getOutputMeterReadings().maxTruePeak[0], 0.0f);
            device->render (block);
            expect (playback.seekToSeconds (0.5));
            expectEquals (playback.getOutputMeterReadings().measuredSeconds, 0.0);
            expectEquals (playback.getOutputMeterReadings().maxTruePeak[0], 0.0f);
            playback.unload();
            expectEquals (playback.getOutputMeterReadings().samplePeak[0], 0.0f);
            expect (meterFile.deleteFile());
        }

        beginTest ("stereo playback clears every unsupported speaker after large-layout playback");
        {
            const auto stereoFile = writeSignal ("stereo-upmix", 48000, 48000.0, 0.125f, 2);
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            expect (playback.initialise().isEmpty());
            for (const auto layout : { OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                                       OutputLayout::sevenOneTwo, OutputLayout::sevenOneFour,
                                       OutputLayout::sevenOneSix, OutputLayout::nineOneFour,
                                       OutputLayout::nineOneSix })
            {
                expect (playback.setOutputLayout (layout).isEmpty());
                expect (playback.loadFile (third));
                playback.play();
                juce::AudioBuffer<float> rendered (getChannelCount (layout), 256);
                device->render (rendered);
                expect (rendered.getSample (rendered.getNumChannels() - 1, 255) > 0.0f);
                expect (playback.loadFile (stereoFile));
                playback.play();
                for (int block = 0; block < 8; ++block) device->render (rendered);
                for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
                    for (int frame = 0; frame < rendered.getNumSamples(); ++frame)
                        expectWithinAbsoluteError (rendered.getSample (channel, frame),
                            channel < 2 ? 0.125f * static_cast<float> (channel + 1) * static_cast<float> (playback.getGain()) : 0.0f,
                            0.0001f, toString (layout) + " speaker " + juce::String (channel + 1));
            }
            playback.unload();
            expect (stereoFile.deleteFile());
        }

        beginTest ("future queue edits preserve effect resources and buffered tails on every bus");
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                   OutputLayout::sevenOneFour, OutputLayout::sevenOneSix,
                                   OutputLayout::nineOneFour, OutputLayout::nineOneSix })
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine playback (std::move (backend));
            expect (playback.initialise().isEmpty());
            expect (playback.setOutputLayout (layout).isEmpty());
            expect (playback.loadPlaybackPlan ({ { { third, 1 } }, -1 }));
            auto plugin = std::make_unique<DelayProbePlugin>();
            auto* probe = plugin.get();
            juce::String error;
            expect (playback.addEffectPlugin (1, std::move (plugin), error), error);
            playback.play();
            juce::AudioBuffer<float> block (getChannelCount (layout), 256);
            device->render (block);
            const auto prepares = probe->prepares;
            const auto releases = probe->releases;
            const auto position = playback.getCurrentPositionSeconds();
            {
                const AudioEngine::ScopedPlaybackEdit edit (playback);
                expect (playback.setUpcomingPlayback ({ { first, 2 } }, -1));
                device->render (block);
                expectWithinAbsoluteError (playback.getCurrentPositionSeconds(), position, 0.000001);
                for (int channel = 0; channel < block.getNumChannels(); ++channel)
                    expectWithinAbsoluteError (block.getSample (channel, 255),
                        0.048f * static_cast<float> (channel + 1), 0.0001f);
            }
            expectEquals (probe->prepares, prepares);
            expectEquals (probe->releases, releases);
            playback.seekToSeconds (playback.getLengthInSeconds());
            device->render (block);
        }

        beginTest ("removing the actual current track reconciles its final pause callback");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ shortLoop, second });
            expect (controller.selectTrack (1));
            controller.setRepeatMode (RepeatMode::all);
            expect (controller.playOrPause());
            juce::AudioBuffer<float> prefix (2, 175);
            device->render (prefix); // The audio is one frame before A returns to B.
            controller.synchronizePlayback();
            const auto visibleSelection = controller.getPlaylist().getSelectedIndex();
            std::atomic_bool observedPause { false };
            std::thread finalCallback ([&]
            {
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
                while (controller.isPlaying()
                       && juce::Time::getMillisecondCounterHiRes() < deadline)
                    juce::Thread::sleep (1);
                observedPause.store (! controller.isPlaying());
                juce::AudioBuffer<float> fade (2, 64);
                device->render (fade);
            });
            expect (controller.removeTrack (visibleSelection));
            finalCallback.join();
            expect (observedPause.load());
            expect (controller.isPlaying());
            expectWithinAbsoluteError (controller.getLengthInSeconds(), 159.0 / 48000.0, 0.000001);
            expectEquals (controller.getPlaylist().size(), 1);
            juce::AudioBuffer<float> resumed (2, 64);
            device->render (resumed);
            expectWithinAbsoluteError (resumed.getSample (0, 63), 0.032f, 0.0001f);
            controller.clearPlaylist();
        }

        beginTest ("explicit pause at EOF never reloads or restarts the finished track");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ first });
            expect (controller.setPlaying (true));
            juce::AudioBuffer<float> block (2, 128);
            device->render (block);
            expect (controller.setPlaying (false));
            expect (! controller.isPlaying());
            expect (controller.getViewState().transportIntent == TransportIntent::stopped);
            expect (controller.setPlaying (false));
            expect (! controller.isPlaying());
            expect (controller.setPlaying (true));
            expect (controller.isPlaying());
            controller.clearPlaylist();
        }

        beginTest ("append after audio EOF leaves one-click playback and coherent transport intent");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ first });
            expect (controller.playOrPause());
            juce::AudioBuffer<float> block (2, 128);
            device->render (block);
            controller.addFiles ({ second });
            expect (! controller.isPlaying());
            expect (controller.getViewState().transportIntent == TransportIntent::stopped);
            expect (controller.playOrPause());
            expect (controller.isPlaying());
            expect (controller.getViewState().transportIntent == TransportIntent::playing);
            controller.clearPlaylist();
        }

        beginTest ("continuous playback replays repeated forward-history occurrences");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ first, second, third });
            controller.setShuffleEnabled (true);
            expect (controller.selectTrack (0));
            expect (controller.selectTrack (1));
            expect (controller.selectTrack (0));
            expect (controller.selectTrack (2));
            expect (controller.previousTrack());
            expect (controller.previousTrack());
            expect (controller.previousTrack());
            expect (controller.playOrPause());
            juce::AudioBuffer<float> firstPair (2, 256);
            device->render (firstPair);
            juce::AudioBuffer<float> repeated (2, 32);
            device->render (repeated);
            expect (controller.synchronizePlayback());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 0);
            expectWithinAbsoluteError (repeated.getSample (0, 31), 0.016f, 0.0001f);
            controller.clearPlaylist();
        }

        beginTest ("controller reconciles every short-track boundary before a single UI poll");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ first, second, third, shortLoop, differentRate });
            controller.setShuffleEnabled (true);
            const auto initialId = controller.getPlaylist().getSelectedTrack()->id;
            expect (controller.playOrPause());
            juce::AudioBuffer<float> rendered (2, 256);
            for (int block = 0; block < 16; ++block)
            {
                device->render (rendered);
                juce::Thread::sleep (1);
            }
            expect (controller.synchronizePlayback());
            const auto history = controller.getPlaylist().navigationCandidates (
                PlaylistNavigation::previous);
            expectEquals (static_cast<int> (history.size()), 4);
            if (! history.empty()) expectEquals (history.back(), initialId);
            expect (controller.getViewState().transportIntent == TransportIntent::stopped);
        }

        beginTest ("queue edits reconcile an audio transition that the UI has not observed");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ first, second, third });
            expect (controller.playOrPause());
            juce::AudioBuffer<float> partial (2, 128);
            device->render (partial);
            controller.addFiles ({ shortLoop });
            expectEquals (controller.getPlaylist().getSelectedIndex(), 1);
            expectWithinAbsoluteError (controller.getCurrentPositionSeconds(),
                                       31.0 / 48000.0, 0.00001);
            juce::AudioBuffer<float> rendered (2, 256);
            for (int block = 0; block < 12; ++block)
            {
                device->render (rendered);
                juce::Thread::sleep (1);
            }
            expect (controller.synchronizePlayback());
            expectEquals (controller.getPlaylist().getSelectedIndex(), 3);
        }

        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                   OutputLayout::sevenOneFour, OutputLayout::sevenOneSix,
                                   OutputLayout::nineOneFour, OutputLayout::nineOneSix })
        {
            beginTest ("track boundaries fill the same callback in " + toString (layout));
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine gapless (std::move (backend));
            expect (gapless.initialise().isEmpty());
            expect (gapless.setOutputLayout (layout).isEmpty());
            expect (gapless.loadPlaybackPlan ({ { { first, 11 }, { second, 22 } }, -1 }));
            gapless.play();
            juce::AudioBuffer<float> rendered (getChannelCount (layout), 256);
            {
#if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker (*this);
#endif
                device->render (rendered);
            }
            for (int channel = 0; channel < rendered.getNumChannels(); ++channel)
                for (int sample = 0; sample < 256; ++sample)
                    expectWithinAbsoluteError (rendered.getSample (channel, sample),
                        (sample < 97 ? 0.02f : 0.04f)
                            * static_cast<float> (channel + 1) * 0.8f, 0.0001f);
            const auto progress = gapless.getPlaybackProgress();
            expectEquals (progress.sequence, uint64_t { 1 });
            expectEquals (progress.token, uint64_t { 22 });
            expectWithinAbsoluteError (progress.positionSeconds, 159.0 / 48000.0, 0.000001);
            expect (progress.finished);
        }

        beginTest ("repeat one advances many occurrences without UI polling");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine gapless (std::move (backend));
            expect (gapless.initialise().isEmpty());
            expect (gapless.loadPlaybackPlan ({ { { shortLoop, 7 } }, 0 }));
            gapless.play();
            juce::AudioBuffer<float> rendered (2, 256);
            for (int block = 0; block < 8; ++block)
            {
                device->render (rendered);
                for (int sample = 0; sample < 256; ++sample)
                    expectWithinAbsoluteError (rendered.getSample (0, sample), 0.016f, 0.0001f);
            }
            const auto progress = gapless.getPlaybackProgress();
            expectEquals (progress.sequence, uint64_t { 120 });
            expectEquals (progress.token, uint64_t { 7 });
            expectWithinAbsoluteError (progress.positionSeconds, 8.0 / 48000.0, 0.000001);
            expect (! progress.finished);
            expect (gapless.setUpcomingPlayback ({}, -1));
            device->render (rendered);
            expect (gapless.hasStreamFinished());
        }

        beginTest ("resampling preserves the exact output boundary and seek occurrence");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine gapless (std::move (backend));
            expect (gapless.initialise().isEmpty());
            expect (gapless.loadPlaybackPlan (
                { { { first, 11 }, { differentRate, 22 }, { third, 33 } }, -1 }));
            gapless.play();
            juce::AudioBuffer<float> rendered (2, 256);
            device->render (rendered);
            for (int sample = 97; sample < 256; ++sample)
                expect (rendered.getSample (0, sample) > 0.0f);
            const auto beforeSeek = gapless.getPlaybackProgress();
            expectEquals (beforeSeek.sequence, uint64_t { 1 });
            expectWithinAbsoluteError (beforeSeek.positionSeconds, 159.0 / 48000.0, 0.000001);
            expect (gapless.seekToSeconds (0.0));
            const auto afterSeek = gapless.getPlaybackProgress();
            expectEquals (afterSeek.generation, beforeSeek.generation);
            expectEquals (afterSeek.sequence, beforeSeek.sequence);
            device->render (rendered);
            expectWithinAbsoluteError (rendered.getSample (0, 160), 0.048f, 0.0001f);
            expectEquals (gapless.getPlaybackProgress().sequence, uint64_t { 2 });
            expect (gapless.setUpcomingPlayback ({}, -1));
            gapless.seekToSeconds (gapless.getLengthInSeconds());
            device->render (rendered);
        }

        beginTest ("upcoming replacement invalidates prepared audio and preserves current position");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine gapless (std::move (backend));
            expect (gapless.initialise().isEmpty());
            expect (gapless.loadPlaybackPlan ({ { { first, 11 }, { second, 22 } }, -1 }));
            gapless.play();
            juce::AudioBuffer<float> prefix (2, 64);
            device->render (prefix);
            const auto generation = gapless.getPlaybackProgress().generation;
            expect (gapless.setUpcomingPlayback ({ { third, 33 } }, -1));
            expect (gapless.getPlaybackProgress().generation > generation);
            expectWithinAbsoluteError (gapless.getCurrentPositionSeconds(), 64.0 / 48000.0, 0.000001);
            expect (gapless.setOutputLayout (OutputLayout::sevenOneTwo).isEmpty());
            expect (gapless.setOutputLayout (OutputLayout::nineOneSix).isEmpty());
            juce::AudioBuffer<float> rendered (16, 256);
            device->render (rendered);
            for (int channel = 0; channel < 16; ++channel)
            {
                expectWithinAbsoluteError (rendered.getSample (channel, 32),
                                           0.02f * static_cast<float> (channel + 1) * 0.8f, 0.0001f);
                expectWithinAbsoluteError (rendered.getSample (channel, 33),
                                           0.06f * static_cast<float> (channel + 1) * 0.8f, 0.0001f);
            }
            expectEquals (gapless.getPlaybackProgress().token, uint64_t { 33 });
            gapless.seekToSeconds (gapless.getLengthInSeconds());
            device->render (rendered);
        }

        beginTest ("failed future items keep sequence slots and an unavailable repeat suffix ends");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* device = backend.get();
            AudioEngine gapless (std::move (backend));
            expect (gapless.initialise().isEmpty());
            expect (! gapless.loadPlaybackPlan ({ { { missing, 9 }, { first, 11 } }, -1 }));
            expect (! gapless.isPlaying());
            const auto initialFailures = gapless.drainPreparationFailures();
            expectEquals (static_cast<int> (initialFailures.size()), 1);
            expect (gapless.loadPlaybackPlan (
                { { { first, 11 }, { missing, 9 }, { second, 22 } }, -1 }));
            gapless.play();
            juce::AudioBuffer<float> rendered (2, 256);
            device->render (rendered);
            expectWithinAbsoluteError (rendered.getSample (0, 97), 0.032f, 0.0001f);
            expectEquals (gapless.getPlaybackProgress().sequence, uint64_t { 2 });
            const auto failures = gapless.drainPreparationFailures();
            expectEquals (static_cast<int> (failures.size()), 1);
            if (! failures.empty())
            {
                expectEquals (failures.front().sequence, uint64_t { 1 });
                expectEquals (failures.front().token, uint64_t { 9 });
            }
            expect (gapless.loadPlaybackPlan ({ { { first, 11 }, { missing, 9 } }, 1 }));
            gapless.play();
            device->render (rendered);
            expect (gapless.hasStreamFinished());
            expectEquals (static_cast<int> (gapless.drainPreparationFailures().size()), 1);
            device->render (rendered);
            expect (gapless.drainPreparationFailures().empty());
        }
        beginTest ("live queue edits preserve dry output across every required device bus");
        {
            const auto current = writeSignal ("continuous-current", 192000, 48000.0, 0.25f, 2);
            const auto future = writeSignal ("continuous-future", 192000, 48000.0, 0.25f, 2);
            const auto appended = writeSignal ("continuous-appended", 192000, 48000.0, 0.25f, 2);
            for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                                       OutputLayout::sevenOneTwo, OutputLayout::nineOneSix })
            {
                auto backend = std::make_unique<OfflineAudioDeviceBackend>();
                auto* device = backend.get();
                PlayerController controller (std::move (backend));
                expect (controller.setOutputLayout (layout).isEmpty());
                controller.addFiles ({ current, future });
                expect (controller.setPlaying (true));
                std::atomic_bool running { true }, observing { true };
                std::atomic<int> callbacks { 0 }, changedSamples { 0 };
                std::thread audio ([&]
                {
                    juce::AudioBuffer<float> block (getChannelCount (layout), 256);
                    while (running.load())
                    {
                        device->render (block);
                        if (observing.load())
                            for (int frame = 0; frame < 256; ++frame)
                                if (std::abs (block.getSample (0, frame) - 0.2f) > 0.000001f)
                                    ++changedSamples;
                        ++callbacks;
                        juce::Thread::sleep (5);
                    }
                });
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
                while (callbacks.load() < 2 && juce::Time::getMillisecondCounterHiRes() < deadline)
                    juce::Thread::sleep (1);
                controller.addFiles ({ appended });
                controller.setShuffleEnabled (true);
                controller.setRepeatMode (RepeatMode::all);
                expect (controller.moveTrack (1, 2));
                expect (controller.removeTrack (2));
                for (int edit = 0; edit < 12; ++edit)
                {
                    controller.setShuffleEnabled (edit % 2 == 0);
                    controller.setRepeatMode (edit % 2 == 0 ? RepeatMode::off : RepeatMode::all);
                    juce::Thread::sleep (2);
                }
                observing.store (false);
                expect (controller.setPlaying (false));
                running.store (false);
                audio.join();
                expect (callbacks.load() >= 5, toString (layout));
                expectEquals (changedSamples.load(), 0, toString (layout));
                expectEquals (controller.getPlaylist().getSelectedTrack()->file.getFullPathName(), current.getFullPathName());
            }
            for (const auto& signalFile : { current, future, appended }) expect (signalFile.deleteFile());
        }
        for (const auto& signalFile : { first, second, third, shortLoop, differentRate })
            expect (signalFile.deleteFile());
    }
};

AudioEngineTests audioEngineTests;
} // namespace
