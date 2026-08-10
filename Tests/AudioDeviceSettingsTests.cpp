#include "audio/AudioEngine.h"
#include "app/PlayerController.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinSession.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <barrier>
#include <map>
#include <thread>

namespace
{
class FakeAudioIODevice final : public juce::AudioIODevice
{
public:
    FakeAudioIODevice() : AudioIODevice ("Fake CoreAudio", "CoreAudio")
    {
        activeOutputs.setRange (0, 2, true);
    }

    juce::StringArray getOutputChannelNames() override
    {
        juce::StringArray result;
        for (int channel = 1; channel <= 10; ++channel)
            result.add ("Output " + juce::String (channel));
        return result;
    }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override
    {
        return { 44100.0, 48000.0, 96000.0 };
    }
    juce::Array<int> getAvailableBufferSizes() override
    {
        return { 64, 128, 256, 512 };
    }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outputs,
                       double rate, int size) override
    {
        activeOutputs = outputs;
        sampleRate = rate;
        bufferSize = size;
        openState = true;
        return {};
    }
    void close() override { openState = false; }
    bool isOpen() override { return openState; }
    void start (juce::AudioIODeviceCallback* callback) override
    {
        streamingCallback = callback;
        playing = callback != nullptr;
    }
    void stop() override { streamingCallback = nullptr; playing = false; }
    bool isPlaying() override { return playing; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override
    {
        return activeOutputs;
    }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    void configure (double rate, int size, const juce::BigInteger& outputs)
    {
        sampleRate = rate;
        bufferSize = size;
        activeOutputs = outputs;
        openState = true;
    }

private:
    juce::AudioIODeviceCallback* streamingCallback = nullptr;
    juce::BigInteger activeOutputs;
    double sampleRate = 48000.0;
    int bufferSize = 256;
    bool openState = true;
    bool playing = false;
};

class FakeAudioDeviceBackend final : public IAudioDeviceBackend
{
public:
    struct Device
    {
        juce::Array<double> rates;
        juce::Array<int> buffers;
        int outputs = 0;
    };

    FakeAudioDeviceBackend()
    {
        devices.emplace ("Built-in Output",
                         Device { { 44100.0, 48000.0 }, { 128, 256, 512 }, 2 });
        devices.emplace ("Aggregate 10ch",
                         Device { { 48000.0, 96000.0 }, { 64, 128, 256 }, 10 });
        setup.outputDeviceName = "Built-in Output";
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
        ioDevice.configure (setup.sampleRate, setup.bufferSize,
                            setup.outputChannels);
    }

    juce::String initialiseWithDefaultDevices (int, int) override
    {
        ++initialiseCalls;
        return initialiseError;
    }

    juce::StringArray getOutputDeviceNames() const override
    {
        return { "Built-in Output", "Aggregate 10ch" };
    }

    AudioDeviceBackendState getCurrentDeviceState() const override
    {
        AudioDeviceBackendState state;
        const auto found = devices.find (setup.outputDeviceName);
        if (found == devices.end()) return state;
        state.open = true;
        state.name = setup.outputDeviceName;
        state.sampleRate = setup.sampleRate;
        state.sampleRates = found->second.rates;
        state.bufferSize = setup.bufferSize;
        state.bufferSizes = found->second.buffers;
        state.availableOutputChannels = found->second.outputs;
        return state;
    }

    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override
    {
        return setup;
    }

    juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup& requested,
        bool) override
    {
        ++setSetupCalls;
        requestedSetups.push_back (requested);
        if (nextSetupError.isNotEmpty())
        {
            auto error = nextSetupError;
            nextSetupError.clear();
            return error;
        }
        const auto found = devices.find (requested.outputDeviceName);
        if (found == devices.end()) return "Unknown CoreAudio device.";
        auto applied = requested;
        if (applied.sampleRate == 0.0)
            applied.sampleRate = found->second.rates.getFirst();
        if (applied.bufferSize == 0)
            applied.bufferSize = found->second.buffers.getFirst();
        if (! found->second.rates.contains (applied.sampleRate))
            return "Unsupported sample rate.";
        if (! found->second.buffers.contains (applied.bufferSize))
            return "Unsupported buffer size.";
        if (applied.outputChannels.countNumberOfSetBits()
            > found->second.outputs)
            return "Requested output channels are unavailable.";
        if (narrowOutputMaskOnSuccess)
        {
            applied.outputChannels.clear();
            applied.outputChannels.setRange (0, 2, true);
        }
        if (keepPreviousDeviceNameOnSuccess)
            applied.outputDeviceName = setup.outputDeviceName;
        setup = applied;
        ioDevice.configure (setup.sampleRate, setup.bufferSize,
                            setup.outputChannels);
        return {};
    }

    void addAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
        ++addCallbackCalls;
        attachedCallback.store (callback, std::memory_order_release);
        if (callback != nullptr) callback->audioDeviceAboutToStart (&ioDevice);
    }

    void removeAudioCallback (juce::AudioIODeviceCallback* callback) override
    {
        ++removeCallbackCalls;
        auto* expected = callback;
        if (attachedCallback.compare_exchange_strong (
                expected, nullptr, std::memory_order_acq_rel)
            && callback != nullptr)
            callback->audioDeviceStopped();
    }

    std::map<juce::String, Device> devices;
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    std::vector<juce::AudioDeviceManager::AudioDeviceSetup> requestedSetups;
    juce::String initialiseError;
    juce::String nextSetupError;
    FakeAudioIODevice ioDevice;
    std::atomic<juce::AudioIODeviceCallback*> attachedCallback { nullptr };
    bool narrowOutputMaskOnSuccess = false;
    bool keepPreviousDeviceNameOnSuccess = false;
    int initialiseCalls = 0;
    int setSetupCalls = 0;
    int addCallbackCalls = 0;
    int removeCallbackCalls = 0;
};

class AudioDeviceSettingsTests final : public juce::UnitTest
{
public:
    AudioDeviceSettingsTests()
        : UnitTest ("AudioDeviceSettings", "audio") {}

    void runTest() override
    {
        beginTest ("query exposes current CoreAudio capabilities without the backend");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            const auto view = engine.getDeviceSettings();
            expectEquals (view.currentDevice, juce::String ("Built-in Output"));
            expect (view.devices == juce::StringArray {
                "Built-in Output", "Aggregate 10ch" });
            expectWithinAbsoluteError (view.currentSampleRate, 48000.0, 0.01);
            expect (view.sampleRates == juce::Array<double> { 44100.0, 48000.0 });
            expectEquals (view.currentBufferSize, 256);
            expect (view.bufferSizes == juce::Array<int> { 128, 256, 512 });
            expectEquals (view.availableOutputChannels, 2);
            expect (fake->attachedCallback.load() != nullptr);
        }

        beginTest ("invalid values are rejected without interrupting the callback");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            const auto removals = fake->removeCallbackCalls;
            expect (engine.selectOutputDevice ("Missing Device").isNotEmpty());
            expect (engine.setSampleRate (88200.0).isNotEmpty());
            expect (engine.setBufferSize (1024).isNotEmpty());
            expectEquals (fake->removeCallbackCalls, removals);
            expect (fake->attachedCallback.load() != nullptr);
        }

        beginTest ("successful changes retain the selected multichannel layout");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            expect (engine.setOutputLayout (OutputLayout::sevenOneTwo).isEmpty());
            expectEquals (fake->setup.outputChannels.countNumberOfSetBits(), 10);
            expect (engine.setSampleRate (96000.0).isEmpty());
            expect (engine.setBufferSize (128).isEmpty());
            expectWithinAbsoluteError (fake->setup.sampleRate, 96000.0, 0.01);
            expectEquals (fake->setup.bufferSize, 128);
            expectEquals (fake->setup.outputChannels.countNumberOfSetBits(), 10);
            expect (fake->attachedCallback.load() != nullptr);
            expectEquals (fake->addCallbackCalls, fake->removeCallbackCalls + 1);
        }

        beginTest ("successful setup must preserve the exact requested device and channel map");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            const auto stereoSetup = fake->setup;

            fake->narrowOutputMaskOnSuccess = true;
            const auto narrowed = engine.setOutputLayout (OutputLayout::sevenOneTwo);
            expect (narrowed.contains ("did not match"), narrowed);
            expect (engine.getOutputLayout() == OutputLayout::stereo);
            expect (fake->setup == stereoSetup);
            fake->narrowOutputMaskOnSuccess = false;

            expect (engine.selectOutputDevice ("Built-in Output").isEmpty());
            const auto builtInSetup = fake->setup;
            fake->keepPreviousDeviceNameOnSuccess = true;
            const auto wrongDevice = engine.selectOutputDevice ("Aggregate 10ch");
            expect (wrongDevice.isNotEmpty(), wrongDevice);
            expect (fake->setup == builtInSetup);
        }

        beginTest ("rate and buffer changes preserve a non-contiguous aggregate route");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            expect (engine.setOutputLayout (OutputLayout::fiveOneTwo).isEmpty());
            fake->setup.outputChannels.clear();
            fake->setup.outputChannels.setRange (2, 8, true);
            fake->ioDevice.configure (fake->setup.sampleRate, fake->setup.bufferSize,
                                      fake->setup.outputChannels);
            const auto exactRoute = fake->setup.outputChannels;
            expect (engine.setSampleRate (96000.0).isEmpty());
            expect (fake->setup.outputChannels == exactRoute);
            expect (engine.setBufferSize (128).isEmpty());
            expect (fake->setup.outputChannels == exactRoute);
            expect (! fake->setup.useDefaultOutputChannels);
        }

        beginTest ("failed setup restores the complete previous setup and callback");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            expect (engine.setOutputLayout (OutputLayout::sevenOneTwo).isEmpty());
            const auto previous = fake->setup;

            const auto channelError = engine.selectOutputDevice ("Built-in Output");
            expect (channelError.contains ("CoreAudio"), channelError);
            expect (fake->setup == previous);
            expect (fake->attachedCallback.load() != nullptr);

            fake->nextSetupError = "CoreAudio transport failed.";
            const auto rateError = engine.setSampleRate (96000.0);
            expect (rateError.contains ("CoreAudio transport failed"), rateError);
            expect (fake->setup == previous);
            expect (fake->attachedCallback.load() != nullptr);
        }

        beginTest ("PlayerController exposes settings and records actionable status");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            PlayerController controller (std::move (backend));
            expectEquals (controller.getAudioDeviceSettings().currentDevice,
                          juce::String ("Built-in Output"));
            expect (controller.selectOutputDevice ("Aggregate 10ch").isEmpty());
            expect (controller.getLastStatusMessage().contains ("Aggregate 10ch"));
            expect (controller.setAudioSampleRate (96000.0).isEmpty());
            expect (controller.getLastStatusMessage().contains ("96000 Hz"));
            expect (controller.setAudioBufferSize (64).isEmpty());
            expect (controller.getLastStatusMessage().contains ("64 samples"));
            expect (controller.setAudioBufferSize (4096).isNotEmpty());
            expect (controller.getLastStatusMessage().contains ("does not support"));
            const auto view = controller.getViewState();
            expectWithinAbsoluteError (view.audioSampleRate, 96000.0, 0.01);
            expectEquals (view.audioBufferSize, 64);
        }

        beginTest ("PlayerController restores a saved position without autoplay");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("playback-restore-")
                                       .getSiblingFile ("playback-restore-"
                                                       + juce::Uuid().toString());
            expect (directory.createDirectory());
            const auto audioFile = directory.getChildFile ("resume.wav");
            juce::WavAudioFormat wav;
            auto output = std::unique_ptr<juce::OutputStream> (
                audioFile.createOutputStream());
            auto writer = wav.createWriterFor (
                output, juce::AudioFormatWriterOptions{}
                            .withSampleRate (8000.0)
                            .withNumChannels (2)
                            .withBitsPerSample (16));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> samples (2, 24000);
                samples.clear();
                expect (writer->writeFromAudioSampleBuffer (
                    samples, 0, samples.getNumSamples()));
                writer.reset();
            }

            PlayerAppState saved;
            TrackItem track { audioFile, "Resume", 3.0 };
            track.id = "resume-track";
            saved.playlist.addTrack (std::move (track));
            saved.playlist.selectTrack (0);
            saved.playbackPositionSeconds = 1.25;
            const auto stateFile = directory.getChildFile ("state.xml");
            juce::String stateError;
            expect (AppStateStore::writeAtomically (
                saved, stateFile, stateError), stateError);

            PlayerController controller (
                std::make_unique<FakeAudioDeviceBackend>());
            expect (controller.loadState (
                stateFile, directory.getChildFile ("plugins.xml")));
            const auto view = controller.getViewState();
            expectEquals (view.selectedTrackId,
                          juce::String ("resume-track"));
            expect (! view.isPlaying);
            expect (view.transportIntent == TransportIntent::paused);
            expectWithinAbsoluteError (view.positionSeconds, 1.25, 0.02);

            PlayerAppState captured;
            stateError.clear();
            expect (controller.captureStateSnapshot (captured, stateError),
                    stateError);
            expectWithinAbsoluteError (captured.playbackPositionSeconds,
                                       1.25, 0.02);
            expect (directory.deleteRecursively());
        }

        beginTest ("unavailable restored tracks retain their saved position");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getNonexistentChildFile (
                                           "unavailable-playback-restore-",
                                           juce::String());
            expect (directory.createDirectory());

            PlayerAppState saved;
            TrackItem track {
                directory.getChildFile ("missing.wav"), "Missing", 120.0
            };
            track.id = "missing-track";
            track.available = true;
            saved.playlist.addTrack (std::move (track));
            saved.playlist.selectTrack (0);
            saved.playbackPositionSeconds = 47.25;
            const auto stateFile = directory.getChildFile ("state.xml");
            juce::String stateError;
            expect (AppStateStore::writeAtomically (
                saved, stateFile, stateError), stateError);

            PlayerController controller (
                std::make_unique<FakeAudioDeviceBackend>());
            expect (controller.loadState (
                stateFile, directory.getChildFile ("plugins.xml")));
            expect (! controller.getViewState().selectedTrackAvailable);

            PlayerAppState captured;
            stateError.clear();
            expect (controller.captureStateSnapshot (captured, stateError),
                    stateError);
            expectWithinAbsoluteError (captured.playbackPositionSeconds,
                                       47.25, 0.001);
            expect (directory.deleteRecursively());
        }

        beginTest ("real ten-channel callback stays allocation-free and independent of skin churn");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            expect (engine.setOutputLayout (OutputLayout::sevenOneTwo).isEmpty());

            constexpr int samplesPerBlock = 64;
            constexpr int callbackTarget = 1000;
            const auto audioDirectory = juce::File::getSpecialLocation (
                    juce::File::tempDirectory)
                    .getNonexistentChildFile (
                        "moonwake-audio-device-stress", juce::String());
            expect (audioDirectory.createDirectory());
            const auto audioFile = audioDirectory.getNonexistentChildFile (
                "ten-channel", ".wav");
            juce::WavAudioFormat wav;
            auto output = std::unique_ptr<juce::OutputStream> (
                audioFile.createOutputStream());
            auto writer = wav.createWriterFor (
                output, juce::AudioFormatWriterOptions{}
                    .withSampleRate (48000.0)
                    .withNumChannels (10)
                    .withBitsPerSample (32)
                    .withSampleFormat (
                        juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (writer != nullptr);
            if (writer)
            {
                juce::AudioBuffer<float> source (10,
                                                 samplesPerBlock * callbackTarget);
                for (int channel = 0; channel < source.getNumChannels(); ++channel)
                    std::fill_n (source.getWritePointer (channel), source.getNumSamples(),
                                 0.04f * static_cast<float> (channel + 1));
                expect (writer->writeFromAudioSampleBuffer (
                    source, 0, source.getNumSamples()));
                writer.reset();
            }
            expect (engine.loadFile (audioFile));
            engine.play();

            SkinValidationReport report;
            const auto package = SkinBuiltInPackages::create (
                SkinBuiltInPackages::Package::foundation, report);
            const auto skin = package
                ? SkinCompiler::compile (*package, {}, report) : nullptr;
            expect (skin != nullptr, report.toText());

            std::atomic<int> callbackCount { 0 };
            std::atomic<float> renderedMagnitude { 0.0f };
            std::atomic<int> churnProgress { 0 };
            std::atomic<bool> callbackObservedDuringChurn { false };
            std::atomic<bool> churnFinished { false };
            std::barrier startBarrier (2);
            std::thread audioThread ([&]
            {
                std::array<std::array<float, samplesPerBlock>, 10> channels {};
                std::array<float*, 10> channelPointers {};
                for (size_t channel = 0; channel < channels.size(); ++channel)
                    channelPointers[channel] = channels[channel].data();
                auto* callback = fake->attachedCallback.load (
                    std::memory_order_acquire);
                startBarrier.arrive_and_wait();
#if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker (*this);
#endif
                float maximumRendered = 0.0f;
                do
                {
                    if (callback == nullptr) continue;
                    callback->audioDeviceIOCallbackWithContext (
                        nullptr, 0, channelPointers.data(), 10,
                        samplesPerBlock, {});
                    float magnitude = 0.0f;
                    for (const auto& channel : channels)
                        for (const auto sample : channel)
                            magnitude = juce::jmax (magnitude, std::abs (sample));
                    maximumRendered = juce::jmax (maximumRendered, magnitude);
                    callbackCount.fetch_add (1, std::memory_order_relaxed);
                    const auto observedProgress = churnProgress.load (
                        std::memory_order_acquire);
                    if (observedProgress > 0 && observedProgress < 250)
                        callbackObservedDuringChurn.store (
                            true, std::memory_order_release);
                    if ((callbackCount.load (std::memory_order_relaxed) & 127) == 0)
                        std::this_thread::yield();
                }
                while (! churnFinished.load (std::memory_order_acquire)
                       || callbackCount.load (std::memory_order_relaxed)
                              < callbackTarget);
                renderedMagnitude.store (maximumRendered,
                                         std::memory_order_release);
            });

            std::shared_ptr<SkinSession> activeSkin;
            startBarrier.arrive_and_wait();
            for (int iteration = 0; iteration < 250 && skin; ++iteration)
            {
                activeSkin = std::make_shared<SkinSession> (skin, iteration % 2 == 0);
                churnProgress.store (iteration + 1, std::memory_order_release);
                float meterSum = 0.0f;
                for (const auto meter : engine.getOutputPeakLevels())
                    meterSum += meter;
                expect (std::isfinite (meterSum));
                if (iteration == 124)
                {
                    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                                        + 5000.0;
                    while (! callbackObservedDuringChurn.load (
                               std::memory_order_acquire)
                           && juce::Time::getMillisecondCounterHiRes() < deadline)
                        std::this_thread::yield();
                }
            }
            churnFinished.store (true, std::memory_order_release);
            audioThread.join();
            expect (callbackCount.load() >= callbackTarget);
            expect (callbackObservedDuringChurn.load());
            expect (renderedMagnitude.load() > 0.0f);
            const auto enginePeaks = engine.getOutputPeakLevels();
            expect (std::any_of (enginePeaks.begin(), enginePeaks.end(),
                                 [] (float level) { return level > 0.0f; }));
            expect (activeSkin != nullptr
                    && activeSkin->health() == SkinSessionHealth::healthy);

            const auto audioSourceRoot = juce::File (MOONWAKE_SOURCE_DIR)
                                             .getChildFile ("Source/audio");
            for (const auto& file : audioSourceRoot.findChildFiles (
                     juce::File::findFiles, true))
                if (file.hasFileExtension ("h;hpp;cpp;mm"))
                    expect (! file.loadFileAsString().contains ("skin/"),
                            "Audio source depends on skin code: "
                                + file.getFullPathName());
            engine.unload();
            expect (audioFile.deleteFile());
            expect (audioDirectory.deleteRecursively());
        }
    }
};

AudioDeviceSettingsTests audioDeviceSettingsTests;
} // namespace
