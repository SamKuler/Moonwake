#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/AudioEngine.h"
#include "app/PlayerController.h"

#include <algorithm>
#include <limits>
#include <thread>

namespace
{
class OfflineAudioDeviceBackend final : public IAudioDeviceBackend,
                                        private juce::AudioIODevice
{
public:
    OfflineAudioDeviceBackend() : AudioIODevice ("Offline 16ch", "Test")
    {
        setup.outputDeviceName = getName();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
    }

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
        setup = requested;
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

class DelayProbePlugin final : public juce::AudioPluginInstance
{
public:
    DelayProbePlugin()
        : AudioPluginInstance (BusesProperties()
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}
    int prepares = 0, releases = 0;
    const juce::String getName() const override { return "Delay probe"; }
    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = getName();
    }
    void prepareToPlay (double, int) override
    {
        ++prepares;
        delay.setSize (getTotalNumOutputChannels(), 256);
        delay.clear();
        position = 0;
    }
    void releaseResources() override { ++releases; delay.clear(); }
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
