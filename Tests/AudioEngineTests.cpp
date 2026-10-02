#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/AudioEngine.h"
#include "app/PlayerController.h"

#include <algorithm>

namespace
{
class OfflineAudioDeviceBackend final : public IAudioDeviceBackend,
                                        private juce::AudioIODevice
{
public:
    OfflineAudioDeviceBackend() : AudioIODevice ("Offline 10ch", "Test")
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
                 setup.bufferSize, { setup.bufferSize }, 10 };
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

private:
    juce::StringArray getOutputChannelNames() override
    {
        return { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };
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

class AudioEngineTests final : public juce::UnitTest
{
public:
    AudioEngineTests()
        : UnitTest ("AudioEngine", "audio")
    {
    }

    void runTest() override
    {
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

        beginTest ("short tracks finish between UI polls and repeat from their actual EOF");
        {
            auto backend = std::make_unique<OfflineAudioDeviceBackend>();
            auto* offlineDevice = backend.get();
            PlayerController controller (std::move (backend));
            controller.addFiles ({ file });
            controller.setRepeatMode (RepeatMode::one);
            expect (! controller.getViewState().playbackFinished);
            expect (controller.playOrPause());

            for (int repetition = 0; repetition < 3; ++repetition)
            {
                juce::AudioBuffer<float> rendered (2, 256);
                for (int block = 0; block < 24; ++block)
                    offlineDevice->render (rendered);

                const auto state = controller.getViewState();
                expect (! state.isPlaying);
                expect (state.playbackFinished);
                expect (state.transportIntent == TransportIntent::playing);
                expect (controller.handlePlaybackFinished());
                expect (! controller.getViewState().playbackFinished);
            }

            juce::AudioBuffer<float> rendered (2, 256);
            for (int block = 0; block < 24; ++block)
                offlineDevice->render (rendered);
            controller.setRepeatMode (RepeatMode::off);
            expect (! controller.handlePlaybackFinished());
            expect (controller.getViewState().transportIntent == TransportIntent::stopped);
        }

        expect (file.deleteFile());

        for (const auto sourceSampleRate : { 48000.0, 44100.0 })
        {
            beginTest ("all output channels carry their source signal at "
                       + juce::String (sourceSampleRate, 0) + " Hz");
            const auto multichannelFile = directory.getNonexistentChildFile (
                "discrete-ten-channel", ".wav");
            auto multichannelOutput = std::unique_ptr<juce::OutputStream> (
                multichannelFile.createOutputStream());
            auto multichannelWriter = format.createWriterFor (
                multichannelOutput, juce::AudioFormatWriterOptions{}
                    .withSampleRate (sourceSampleRate)
                    .withChannelLayout (juce::AudioChannelSet::discreteChannels (10))
                    .withBitsPerSample (32)
                    .withSampleFormat (
                        juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (multichannelWriter != nullptr);
            if (multichannelWriter == nullptr)
                continue;

            juce::AudioBuffer<float> signal (10, 48000);
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
                                       OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
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
                                       OutputLayout::stereo, OutputLayout::sevenOneTwo })
            {
                expect (multichannelEngine.setOutputLayout (layout).isEmpty());
                verifyOutput (layout);
            }
            finishPlayback();
            multichannelEngine.unload();
            expect (multichannelFile.deleteFile());
        }
    }
};

AudioEngineTests audioEngineTests;
} // namespace
