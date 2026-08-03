#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/ChannelMappedAudioSource.h"

namespace
{
class ChannelMappedAudioSourceTests final : public juce::UnitTest
{
public:
    ChannelMappedAudioSourceTests()
        : UnitTest ("ChannelMappedAudioSource", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("reads a native 5.1 WAV into matching 7.1.2 speaker roles");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("channel-source-tests");
            expect (directory.createDirectory());
            const auto file = directory.getNonexistentChildFile ("native-5-1", ".wav");
            juce::WavAudioFormat format;
            const auto sourceLayout = juce::AudioChannelSet::create5point1();
            auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
            auto writer = format.createWriterFor (output,
                                                   juce::AudioFormatWriterOptions{}
                    .withSampleRate (48000.0)
                    .withChannelLayout (sourceLayout)
                    .withBitsPerSample (32)
                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (writer != nullptr);

            if (writer != nullptr)
            {
                juce::AudioBuffer<float> sourceBuffer (sourceLayout.size(), 32);
                for (int channel = 0; channel < sourceBuffer.getNumChannels(); ++channel)
                    for (int sample = 0; sample < sourceBuffer.getNumSamples(); ++sample)
                        sourceBuffer.setSample (channel, sample, 0.1f * static_cast<float> (channel + 1));
                expect (writer->writeFromAudioSampleBuffer (sourceBuffer, 0, sourceBuffer.getNumSamples()));
                writer.reset();
            }

            std::unique_ptr<juce::AudioFormatReader> reader (
                format.createReaderFor (file.createInputStream().release(), true));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                const auto destinationLayout = juce::AudioChannelSet::create7point1point2();
                ChannelMappedAudioSource source (std::move (reader), destinationLayout);
                source.prepareToPlay (16, 48000.0);
                juce::AudioBuffer<float> destination (destinationLayout.size(), 16);
                source.getNextAudioBlock ({ &destination, 0, 16 });

                for (int channel = 0; channel < sourceLayout.size(); ++channel)
                {
                    const auto type = sourceLayout.getTypeOfChannel (channel);
                    auto destinationChannel = destinationLayout.getChannelIndexForType (type);
                    if (destinationChannel < 0 && type == juce::AudioChannelSet::leftSurround)
                        destinationChannel = destinationLayout.getChannelIndexForType (
                            juce::AudioChannelSet::leftSurroundSide);
                    if (destinationChannel < 0 && type == juce::AudioChannelSet::rightSurround)
                        destinationChannel = destinationLayout.getChannelIndexForType (
                            juce::AudioChannelSet::rightSurroundSide);

                    expect (destinationChannel >= 0);
                    if (destinationChannel >= 0)
                        expectWithinAbsoluteError (destination.getSample (destinationChannel, 0),
                                                   0.1f * static_cast<float> (channel + 1),
                                                   0.0001f);
                }

                const auto rearLeft = destinationLayout.getChannelIndexForType (
                    juce::AudioChannelSet::leftSurroundRear);
                const auto heightLeft = destinationLayout.getChannelIndexForType (
                    juce::AudioChannelSet::topSideLeft);
                expectWithinAbsoluteError (destination.getSample (rearLeft, 0), 0.0f, 0.0001f);
                expectWithinAbsoluteError (destination.getSample (heightLeft, 0), 0.0f, 0.0001f);
            }

            expect (file.deleteFile());
        }
    }
};

ChannelMappedAudioSourceTests channelMappedAudioSourceTests;
} // namespace
