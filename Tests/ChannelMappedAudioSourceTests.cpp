#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/ChannelMappedAudioSource.h"

#include <atomic>
#include <thread>

namespace
{
struct ReaderProbe
{
    std::atomic<juce::Thread::ThreadID> audioThread { nullptr };
    std::atomic_bool readOnAudioThread { false };
    std::atomic_bool blockReads { false };
    std::atomic_bool failReads { false };
    juce::WaitableEvent readEntered;
    juce::WaitableEvent allowRead;
};

class ProbedAudioReader final : public juce::AudioFormatReader
{
public:
    explicit ProbedAudioReader (ReaderProbe& probeToUse)
        : AudioFormatReader (nullptr, "Probe"), probe (probeToUse)
    {
        sampleRate = 48000.0;
        numChannels = 10;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = 200000;
    }

    static float valueAt (int channel, juce::int64 sample)
    {
        return static_cast<float> (channel + 1) * 0.04f
             + static_cast<float> (sample % 1024) / 4096.0f;
    }

    bool readSamples (int* const* channels, int channelCount, int destinationOffset,
                      juce::int64 startSample, int samples) override
    {
        if (juce::Thread::getCurrentThreadId() == probe.audioThread.load())
            probe.readOnAudioThread.store (true);
        if (probe.blockReads.load())
        {
            probe.readEntered.signal();
            probe.allowRead.wait (5000);
        }
        if (probe.failReads.load())
            return false;
        for (int channel = 0; channel < channelCount; ++channel)
            if (channels[channel] != nullptr)
                for (int sample = 0; sample < samples; ++sample)
                    reinterpret_cast<float*> (channels[channel])[destinationOffset + sample]
                        = valueAt (channel, startSample + sample);
        return true;
    }

private:
    ReaderProbe& probe;
};

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
                juce::TimeSliceThread readAheadThread ("Channel source test");
                ChannelMappedAudioSource source (std::move (reader), destinationLayout,
                                                 readAheadThread);
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

        beginTest ("audio callbacks consume cached samples without waiting for disk reads");
        {
            ReaderProbe probe;
            juce::TimeSliceThread readAheadThread ("Read isolation test");
            ChannelMappedAudioSource source (
                std::make_unique<ProbedAudioReader> (probe),
                juce::AudioChannelSet::create7point1point2(), readAheadThread);
            source.prepareToPlay (256, 48000.0);
            probe.blockReads.store (true);
            expect (readAheadThread.startThread());
            const auto producerBlocked = probe.readEntered.wait (2000);
            expect (producerBlocked);

            juce::WaitableEvent callbackFinished;
            juce::AudioBuffer<float> output (10, 256);
            std::thread callback ([&]
            {
                probe.audioThread.store (juce::Thread::getCurrentThreadId());
#if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker (*this);
#endif
                source.getNextAudioBlock ({ &output, 0, output.getNumSamples() });
                callbackFinished.signal();
            });
            const auto completedWhileReadBlocked = callbackFinished.wait (1000);
            probe.blockReads.store (false);
            probe.allowRead.signal();
            callback.join();
            expect (completedWhileReadBlocked);
            expect (! probe.readOnAudioThread.load());
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                expectWithinAbsoluteError (output.getSample (channel, 255),
                    ProbedAudioReader::valueAt (channel, 255), 0.0001f);
            source.releaseResources();
            expect (readAheadThread.stopThread (2000));
        }

        beginTest ("FIFO wrap, starvation recovery and seek preserve source positions");
        {
            ReaderProbe probe;
            juce::TimeSliceThread readAheadThread ("FIFO position test");
            ChannelMappedAudioSource source (
                std::make_unique<ProbedAudioReader> (probe),
                juce::AudioChannelSet::create7point1point2(), readAheadThread);
            source.prepareToPlay (256, 48000.0);
            juce::AudioBuffer<float> output (10, 256);

            for (int block = 0; block < 40; ++block)
                source.getNextAudioBlock ({ &output, 0, 256 });
            expectEquals (source.getNextReadPosition(), juce::int64 { 8192 });
            expectWithinAbsoluteError (output.getMagnitude (0, 256), 0.0f, 0.0001f);

            for (int block = 0; block < 300; ++block)
            {
                source.prepareToPlay (256, 48000.0);
                const auto position = source.getNextReadPosition();
                source.getNextAudioBlock ({ &output, 0, 256 });
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                    expectWithinAbsoluteError (output.getSample (channel, 255),
                        ProbedAudioReader::valueAt (channel, position + 255), 0.0001f);
                expectEquals (source.getNextReadPosition(), position + 256);
            }

            source.setNextReadPosition (130007);
            source.setDestinationLayout (juce::AudioChannelSet::stereo());
            juce::AudioBuffer<float> stereo (2, 256);
            source.getNextAudioBlock ({ &stereo, 0, 256 });
            expectWithinAbsoluteError (stereo.getSample (1, 0),
                ProbedAudioReader::valueAt (1, 130007), 0.0001f);
            source.setDestinationLayout (juce::AudioChannelSet::create7point1point2());
            source.getNextAudioBlock ({ &output, 0, 256 });
            expectWithinAbsoluteError (output.getSample (9, 0),
                ProbedAudioReader::valueAt (9, 130263), 0.0001f);

            source.setNextReadPosition (source.getTotalLength() - 7);
            source.getNextAudioBlock ({ &output, 0, 256 });
            expectEquals (source.getNextReadPosition(), source.getTotalLength());
            expectWithinAbsoluteError (output.getSample (9, 6),
                ProbedAudioReader::valueAt (9, source.getTotalLength() - 1), 0.0001f);
            expectWithinAbsoluteError (output.getSample (9, 7), 0.0f, 0.0001f);

            probe.failReads.store (true);
            source.setNextReadPosition (1000);
            expect (source.hasReadError());
            source.getNextAudioBlock ({ &output, 0, 256 });
            expectWithinAbsoluteError (output.getMagnitude (0, 256), 0.0f, 0.0001f);
            expectEquals (source.getNextReadPosition(), juce::int64 { 1000 });
            probe.failReads.store (false);
            source.setNextReadPosition (1000);
            expect (! source.hasReadError());
            source.getNextAudioBlock ({ &output, 0, 256 });
            expectWithinAbsoluteError (output.getSample (9, 0),
                ProbedAudioReader::valueAt (9, 1000), 0.0001f);
        }
    }
};

ChannelMappedAudioSourceTests channelMappedAudioSourceTests;
} // namespace
