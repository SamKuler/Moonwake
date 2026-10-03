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
        juce::StringArray outputChannelNames {};
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
        state.open = ioDevice.isOpen();
        state.name = setup.outputDeviceName;
        state.sampleRate = setup.sampleRate;
        state.sampleRates = found->second.rates;
        state.bufferSize = setup.bufferSize;
        state.bufferSizes = found->second.buffers;
        state.availableOutputChannels = found->second.outputs;
        state.outputChannelNames = found->second.outputChannelNames;
        state.activeOutputChannels = ioDevice.getActiveOutputChannels();
        state.identifier = "test-device:" + state.name;
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
            narrowOutputMaskOnSuccess = false;
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
        if (callback != nullptr && renderOnAttach)
        {
            juce::AudioBuffer<float> output (devices.at (setup.outputDeviceName).outputs, 64);
            output.clear();
            std::array<float*, maximumOutputChannels> packed {};
            int count = 0;
            const auto active = ioDevice.getActiveOutputChannels();
            for (auto port = active.findNextSetBit (0); port >= 0; port = active.findNextSetBit (port + 1))
                packed[static_cast<size_t> (count++)] = output.getWritePointer (port);
            callback->audioDeviceIOCallbackWithContext (nullptr, 0, packed.data(), count, 64, {});
            attachOutputSnapshots.push_back (std::move (output));
        }
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

    juce::String openDeviceConfiguration() override
    {
        ++configurationCalls;
        return configurationError;
    }

    std::map<juce::String, Device> devices;
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    std::vector<juce::AudioDeviceManager::AudioDeviceSetup> requestedSetups;
    juce::String initialiseError;
    juce::String nextSetupError;
    mutable FakeAudioIODevice ioDevice;
    std::atomic<juce::AudioIODeviceCallback*> attachedCallback { nullptr };
    bool narrowOutputMaskOnSuccess = false;
    bool keepPreviousDeviceNameOnSuccess = false;
    int initialiseCalls = 0;
    int setSetupCalls = 0;
    int addCallbackCalls = 0;
    int removeCallbackCalls = 0;
    int configurationCalls = 0;
    juce::String configurationError;
    bool renderOnAttach = false;
    std::vector<juce::AudioBuffer<float>> attachOutputSnapshots;
};

class AudioDeviceSettingsTests final : public juce::UnitTest
{
public:
    AudioDeviceSettingsTests()
        : UnitTest ("AudioDeviceSettings", "audio") {}

    void runTest() override
    {
        beginTest ("sparse routing validates before interrupting audio and rolls back rejected masks");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            const auto removals = fake->removeCallbackCalls;
            for (const auto& invalid : std::vector<std::vector<int>> { { 0 }, { 2, 2 }, { -2, 1 }, { 1, 10 } })
                expect (engine.setOutputRouting (invalid).isNotEmpty());
            expectEquals (fake->removeCallbackCalls, removals);
            expect (engine.setOutputRouting ({ 7, 2 }).isEmpty());
            const auto expectedSetup = fake->setup;
            expect (expectedSetup.outputChannels[2] && expectedSetup.outputChannels[7]);
            expectEquals (expectedSetup.outputChannels.countNumberOfSetBits(), 2);
            expectEquals (engine.getDeviceSettings().currentDeviceIdentifier,
                          juce::String ("test-device:Aggregate 10ch"));
            expectEquals (static_cast<int> (engine.getDeviceSettings().outputChannels.size()), 10);
            fake->nextSetupError = "Device busy";
            expect (engine.setOutputRouting ({ 3, -1 }).contains ("Device busy"));
            expect (fake->setup == expectedSetup);
            expect (engine.getDeviceSettings().outputRouting == std::vector<int> { 7, 2 });
            fake->narrowOutputMaskOnSuccess = true;
            expect (engine.setOutputRouting ({ 8, 9 }).contains ("did not match"));
            expect (fake->setup == expectedSetup);
            expect (engine.getDeviceSettings().outputRouting == std::vector<int> { 7, 2 });
            fake->narrowOutputMaskOnSuccess = false;
            expect (engine.setOutputRouting ({ 3, -1 }).isEmpty());
            fake->devices.at ("Aggregate 10ch").outputs = 2;
            const auto beforeUnavailableRoute = fake->removeCallbackCalls;
            expect (engine.setOutputRouting ({ 3, -1 }).contains ("unavailable"));
            expectEquals (fake->removeCallbackCalls, beforeUnavailableRoute);
            expect (engine.selectOutputDevice ("Built-in Output").isEmpty());
            expect (engine.getDeviceSettings().outputRouting == std::vector<int> { 0, 1 });
        }

        beginTest ("device configuration delegates to the backend without changing audio setup");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            const auto before = fake->setup;
            expect (engine.openDeviceConfiguration().isEmpty());
            expectEquals (fake->configurationCalls, 1);
            fake->configurationError = "Configuration application could not be opened.";
            expect (engine.openDeviceConfiguration() == fake->configurationError);
            expect (fake->setup == before);
            expectEquals (fake->removeCallbackCalls, 0);
        }

        beginTest ("output labels follow actual active physical ports in callback order");
        {
            auto backend = std::make_unique<FakeAudioDeviceBackend>();
            auto* fake = backend.get();
            fake->devices.at ("Aggregate 10ch").outputChannelNames = {
                "Monitor L", "Monitor R", "Headphones L", "Headphones R",
                "ADAT 1", "ADAT 2", "ADAT 3", "ADAT 4", "S/PDIF L", "S/PDIF R" };
            AudioEngine engine (std::move (backend));
            expect (engine.initialise().isEmpty());
            expect (engine.selectOutputDevice ("Aggregate 10ch").isEmpty());
            juce::BigInteger actualOutputs;
            actualOutputs.setBit (2);
            actualOutputs.setBit (7);
            fake->ioDevice.configure (48000.0, 256, actualOutputs);
            const auto view = engine.getDeviceSettings();
            expectEquals (view.availableOutputChannels, 10);
            expectEquals (static_cast<int> (view.activeOutputChannels.size()), 2);
            if (view.activeOutputChannels.size() == 2)
            {
                expectEquals (view.activeOutputChannels[0].deviceChannelIndex, 2);
                expectEquals (view.activeOutputChannels[0].name, juce::String ("Headphones L"));
                expectEquals (view.activeOutputChannels[1].deviceChannelIndex, 7);
                expectEquals (view.activeOutputChannels[1].name, juce::String ("ADAT 4"));
            }
            expect (fake->setup.outputChannels != actualOutputs,
                    "The device's active mask must take precedence over the requested setup.");

            fake->devices.at ("Aggregate 10ch").outputChannelNames = { "Monitor L", "Monitor R", "  " };
            const auto unnamed = engine.getDeviceSettings();
            expectEquals (static_cast<int> (unnamed.activeOutputChannels.size()), 2);
            if (unnamed.activeOutputChannels.size() == 2)
            {
                expectEquals (unnamed.activeOutputChannels[0].name, juce::String ("Output 3"));
                expectEquals (unnamed.activeOutputChannels[1].name, juce::String ("Output 8"));
            }

            fake->ioDevice.close();
            const auto disconnected = engine.getDeviceSettings();
            expectEquals (fake->getCurrentDeviceState().availableOutputChannels, 10);
            expectEquals (disconnected.availableOutputChannels, 0);
            expectEquals (engine.getAvailableOutputChannelCount(), 0);
            expect (disconnected.activeOutputChannels.empty(),
                    "A closed device must not report its retained channel mask as connected outputs.");
        }

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
            expect (channelError.contains ("audio device"), channelError);
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

        beginTest ("physical routes restore by device identity and logical layout with unavailable-port fallback");
        {
            const auto root = juce::File::getCurrentWorkingDirectory()
                .getChildFile ("build").getNonexistentChildFile ("device-routing", {});
            expect (root.createDirectory());
            const auto stateFile = root.getChildFile ("state.xml");
            const auto pluginFile = root.getChildFile ("plugins.xml");
            {
                PlayerController writer (std::make_unique<FakeAudioDeviceBackend>());
                expect (writer.selectOutputDevice ("Aggregate 10ch").isEmpty());
                expect (writer.setOutputRouting ({ 7, 2 }).isEmpty());
                expect (writer.selectOutputDevice ("Built-in Output").isEmpty());
                expect (writer.setOutputRouting ({ 1, 0 }).isEmpty());
                expect (writer.selectOutputDevice ("Aggregate 10ch").isEmpty());
                expect (writer.getAudioDeviceSettings().outputRouting == std::vector<int> { 7, 2 });
                expect (writer.setOutputLayout (OutputLayout::fiveOne).isEmpty());
                expect (writer.setOutputRouting ({ 9, 8, 7, 6, 5, -1 }).isEmpty());
                expect (writer.setOutputLayout (OutputLayout::stereo).isEmpty());
                expect (writer.getAudioDeviceSettings().outputRouting == std::vector<int> { 7, 2 });
                expect (writer.saveState (stateFile, pluginFile));
            }
            PlayerController reader (std::make_unique<FakeAudioDeviceBackend>());
            expect (reader.loadState (stateFile, pluginFile));
            expect (reader.getAudioDeviceSettings().outputRouting == std::vector<int> { 7, 2 });
            expect (reader.selectOutputDevice ("Built-in Output").isEmpty());
            expect (reader.getAudioDeviceSettings().outputRouting == std::vector<int> { 1, 0 });
            auto changed = std::make_unique<FakeAudioDeviceBackend>();
            changed->devices.at ("Aggregate 10ch").outputs = 6;
            PlayerController fallback (std::move (changed));
            expect (fallback.loadState (stateFile, pluginFile));
            expect (fallback.getLastStatusMessage().contains ("Could not restore output routing"));
            expect (fallback.getAudioDeviceSettings().outputRouting == std::vector<int> { -1, -1 });
            expect (root.deleteRecursively());
        }

        beginTest ("saved routing is committed before the first callback of a device or layout change");
        {
            const auto root = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                .getNonexistentChildFile ("atomic-output-routing", {});
            expect (root.createDirectory());
            const auto file = root.getChildFile ("signal.wav");
            juce::WavAudioFormat wav;
            auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> signal (2, 96000);
                for (int channel = 0; channel < 2; ++channel)
                    std::fill_n (signal.getWritePointer (channel), signal.getNumSamples(), 0.25f);
                expect (writer->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples()));
                writer.reset();
                auto backend = std::make_unique<FakeAudioDeviceBackend>();
                auto* fake = backend.get();
                PlayerController controller (std::move (backend));
                expect (controller.selectOutputDevice ("Aggregate 10ch").isEmpty());
                expect (controller.setOutputRouting ({ 7, 2 }).isEmpty());
                for (const auto layout : { OutputLayout::fiveOne, OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
                {
                    expect (controller.setOutputLayout (layout).isEmpty());
                    std::vector<int> routes;
                    for (int channel = 0; channel < getChannelCount (layout); ++channel)
                        routes.push_back (9 - channel);
                    expect (controller.setOutputRouting (routes).isEmpty());
                }
                expect (controller.setOutputLayout (OutputLayout::stereo).isEmpty());
                expect (controller.selectOutputDevice ("Built-in Output").isEmpty());
                controller.addFiles ({ file });
                expect (controller.setPlaying (true));
                fake->renderOnAttach = true;
                const auto expectFirstOutput = [&] (std::vector<int> audiblePorts)
                {
                    expectEquals (static_cast<int> (fake->attachOutputSnapshots.size()), 1,
                                  "A change must resume the callback only after applying its saved route.");
                    for (const auto& output : fake->attachOutputSnapshots)
                        for (int port = 0; port < output.getNumChannels(); ++port)
                        {
                            const auto peak = output.getMagnitude (port, 0, output.getNumSamples());
                            if (std::find (audiblePorts.begin(), audiblePorts.end(), port) != audiblePorts.end())
                                expect (peak > 0.1f, "Saved port " + juce::String (port) + " must receive playback immediately.");
                            else
                                expectWithinAbsoluteError (peak, 0.0f, 0.00001f,
                                                           "Unexpected signal on physical port " + juce::String (port));
                        }
                    fake->attachOutputSnapshots.clear();
                };
                expect (controller.selectOutputDevice ("Aggregate 10ch").isEmpty());
                expectFirstOutput ({ 7, 2 });
                for (const auto layout : { OutputLayout::fiveOne, OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
                {
                    expect (controller.setOutputLayout (layout).isEmpty());
                    expectFirstOutput ({ 9, 8 });
                }

                fake->renderOnAttach = false;
                expect (controller.setOutputLayout (OutputLayout::stereo).isEmpty());
                expect (controller.selectOutputDevice ("Built-in Output").isEmpty());
                fake->devices.at ("Aggregate 10ch").outputs = 6;
                fake->renderOnAttach = true;
                const auto beforeDevice = controller.getStateRevision();
                expect (controller.selectOutputDevice ("Aggregate 10ch").contains ("Outputs are disconnected"));
                expectFirstOutput ({});
                expect (controller.getStateRevision() > beforeDevice);
                const auto beforeLayout = controller.getStateRevision();
                expect (controller.setOutputLayout (OutputLayout::fiveOne).contains ("Outputs are disconnected"));
                expectFirstOutput ({});
                expect (controller.getStateRevision() > beforeLayout);
                expect (controller.getAudioDeviceSettings().outputRouting == std::vector<int> (6, -1));
                PlayerAppState captured;
                juce::String error;
                expect (controller.captureStateSnapshot (captured, error), error);
                expectEquals (captured.audioDevice.outputDeviceName, juce::String ("Aggregate 10ch"));
                expect (captured.outputLayout == OutputLayout::fiveOne);
                const auto saved = std::find_if (captured.audioDevice.outputRoutes.begin(), captured.audioDevice.outputRoutes.end(),
                    [] (const auto& route) { return route.deviceIdentifier == "test-device:Aggregate 10ch"
                                                && route.layout == OutputLayout::fiveOne; });
                expect (saved != captured.audioDevice.outputRoutes.end());
                if (saved != captured.audioDevice.outputRoutes.end())
                    expect (saved->channels == std::vector<int> { 9, 8, 7, 6, 5, 4 }, "Keep saved intent after a temporary device capability change.");
                fake->renderOnAttach = false;
            }
            expect (root.deleteRecursively());
        }

        beginTest ("device preferences survive saving and a new controller");
        {
            const auto root = juce::File::getCurrentWorkingDirectory()
                .getChildFile ("build").getNonexistentChildFile ("device-preferences", {});
            expect (root.createDirectory());
            const auto stateFile = root.getChildFile ("state.xml");
            const auto pluginFile = root.getChildFile ("plugins.xml");
            {
                PlayerController writer (std::make_unique<FakeAudioDeviceBackend>());
                const auto revision = writer.getStateRevision();
                expect (writer.selectOutputDevice ("Aggregate 10ch").isEmpty());
                expect (writer.getStateRevision() > revision);
                expect (writer.setAudioSampleRate (96000.0).isEmpty());
                expect (writer.setAudioBufferSize (128).isEmpty());
                expect (writer.setOutputLayout (OutputLayout::sevenOneTwo).isEmpty());
                expect (writer.saveState (stateFile, pluginFile));
            }
            PlayerController reader (std::make_unique<FakeAudioDeviceBackend>());
            expect (reader.loadState (stateFile, pluginFile));
            const auto settings = reader.getAudioDeviceSettings();
            expectEquals (settings.currentDevice, juce::String ("Aggregate 10ch"));
            expectWithinAbsoluteError (settings.currentSampleRate, 96000.0, 0.01);
            expectEquals (settings.currentBufferSize, 128);
            expect (reader.getViewState().outputLayout == OutputLayout::sevenOneTwo);

            auto missingBackend = std::make_unique<FakeAudioDeviceBackend>();
            missingBackend->devices.erase ("Aggregate 10ch");
            PlayerController fallback (std::move (missingBackend));
            expect (fallback.loadState (stateFile, pluginFile));
            expectEquals (fallback.getAudioDeviceSettings().currentDevice,
                          juce::String ("Built-in Output"));
            expect (fallback.getLastStatusMessage().contains ("Aggregate 10ch"));
            PlayerAppState captured;
            juce::String error;
            expect (fallback.captureStateSnapshot (captured, error), error);
            expect (captured.outputLayout == OutputLayout::sevenOneTwo);
            const auto device = AppStateStore::toValueTree (captured)
                .getChildWithName ("AudioDevice");
            expectEquals (device.getProperty ("name").toString(),
                          juce::String ("Aggregate 10ch"));
            expect (root.deleteRecursively());
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
            const auto audioDirectory = juce::File::getCurrentWorkingDirectory()
                    .getChildFile ("build")
                    .getNonexistentChildFile (
                        "moonwake-audio-device-stress", juce::String());
            const auto directoryResult = audioDirectory.createDirectory();
            expect (directoryResult.wasOk(), directoryResult.getErrorMessage());
            if (directoryResult.failed()) return;
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
