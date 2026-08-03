#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/TrackMetadataReader.h"

namespace
{
juce::File getMetadataTestDirectory()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
                               .getChildFile ("build")
                               .getChildFile ("track-metadata-tests");
    static_cast<void> (directory.createDirectory());
    return directory;
}

class TrackMetadataReaderTests final : public juce::UnitTest
{
public:
    TrackMetadataReaderTests()
        : UnitTest ("TrackMetadataReader", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("reads duration sample rate and channel count from a WAV file");
        {
            const auto wavFile = getMetadataTestDirectory().getNonexistentChildFile ("one-second-stereo", ".wav");
            auto output = std::unique_ptr<juce::OutputStream> (wavFile.createOutputStream());
            expect (output != nullptr, "Could not create WAV output at " + wavFile.getFullPathName());
            juce::WavAudioFormat wavFormat;
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate (44100.0)
                                     .withNumChannels (2)
                                     .withBitsPerSample (16);
            auto writer = wavFormat.createWriterFor (output, options);
            expect (writer != nullptr);

            if (writer != nullptr)
            {
                juce::AudioBuffer<float> samples (2, 44100);
                samples.clear();
                expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
                writer.reset();
            }

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (wavFile);
            expect (result.ok);
            expectWithinAbsoluteError (result.track.durationSeconds, 1.0, 0.001);
            expectWithinAbsoluteError (result.track.sampleRate, 44100.0, 0.001);
            expectEquals (result.track.channelCount, 2);
            expectEquals (result.track.title, wavFile.getFileNameWithoutExtension());
            expect (result.track.available);
            expect (result.track.id.isNotEmpty());

            expect (wavFile.deleteFile());
        }

        beginTest ("rejects files that no registered audio format can read");
        {
            const auto textFile = getMetadataTestDirectory().getNonexistentChildFile ("not-audio", ".txt");
            expect (textFile.replaceWithText ("not audio"));

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (textFile);
            expect (! result.ok);
            expect (result.error.isNotEmpty());
            expectEquals (result.track.file.getFullPathName(), textFile.getFullPathName());
            expect (! result.track.available);

            expect (textFile.deleteFile());
        }

        beginTest ("reports missing files as unavailable");
        {
            const auto missingFile = getMetadataTestDirectory().getNonexistentChildFile ("missing", ".wav");

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (missingFile);
            expect (! result.ok);
            expect (! result.track.available);
            expect (result.error.containsIgnoreCase ("not found"));
        }
    }
};

TrackMetadataReaderTests trackMetadataReaderTests;
} // namespace
