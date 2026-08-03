#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/AudioEngine.h"

namespace
{
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

        expect (file.deleteFile());
    }
};

AudioEngineTests audioEngineTests;
} // namespace
