#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/ChannelMappedAudioSource.h"
#include "audio/GaplessAudioSource.h"

#include <atomic>
#include <cmath>
#include <thread>

namespace
{
struct ReaderProbe
{
    std::atomic<juce::Thread::ThreadID> audioThread { nullptr };
    std::atomic_bool readOnAudioThread { false };
    std::atomic_bool blockReads { false };
    std::atomic_bool failReads { false };
    juce::int64 length = 200000;
    juce::int64 failFromFrame = std::numeric_limits<juce::int64>::max();
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
        lengthInSamples = probe.length;
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
        if (probe.failReads.load() || startSample >= probe.failFromFrame)
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

class ProbedAudioFormat final : public juce::AudioFormat
{
public:
    explicit ProbedAudioFormat (ReaderProbe& probeToUse)
        : AudioFormat ("Probe", ".probe"), probe (probeToUse) {}
    juce::Array<int> getPossibleSampleRates() override { return { 48000 }; }
    juce::Array<int> getPossibleBitDepths() override { return { 32 }; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    juce::AudioFormatReader* createReaderFor (juce::InputStream* stream, bool) override
    {
        auto reader = std::make_unique<ProbedAudioReader> (probe);
        reader->input = stream;
        return reader.release();
    }
    std::unique_ptr<juce::AudioFormatWriter> createWriterFor (
        std::unique_ptr<juce::OutputStream>&,
        const juce::AudioFormatWriterOptions&) override { return {}; }
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

        beginTest ("continuous native PCM is identical when split across resampled files");
        for (const auto rate : { 44100.0, 96000.0 })
            for (const auto& layout : { juce::AudioChannelSet::stereo(),
                                        juce::AudioChannelSet::create5point1(),
                                        juce::AudioChannelSet::create5point1point2(),
                                        juce::AudioChannelSet::create7point1point2() })
            {
                // Neither split is an integral device-frame duration at both rates.
                const auto whole = writeWave (rate, layout, 0, 12347, 0);
                const auto first = writeWave (rate, layout, 0, 4421, 0);
                const auto second = writeWave (rate, layout, 4421, 3199, 0);
                const auto third = writeWave (rate, layout, 7620, 4727, 0);
                const auto frames = static_cast<int> (std::llround (12347.0 * 48000.0 / rate));
                const auto reference = renderPlan ({ { { whole, 1 } }, -1 }, layout, frames);
                const auto split = renderPlan (
                    { { { first, 1 }, { second, 2 }, { third, 3 } }, -1 }, layout, frames);
                expectSameWaveform (reference, split, rate);
                for (const auto& file : { whole, first, second, third }) expect (file.deleteFile());
            }

        beginTest ("cached native loops preserve phase and cumulative fractional duration");
        for (const auto rate : { 44100.0, 96000.0 })
        {
            constexpr auto period = 113;
            constexpr auto repetitions = 200;
            const auto layout = juce::AudioChannelSet::create7point1point2();
            const auto whole = writeWave (rate, layout, 0, period * (repetitions + 1), period);
            const auto loop = writeWave (rate, layout, 0, period, period);
            const auto frames = static_cast<int> (std::llround (period * repetitions * 48000.0 / rate));
            const auto reference = renderPlan ({ { { whole, 1 } }, -1 }, layout, frames, false);
            const auto repeated = renderPlan ({ { { loop, 1 } }, 0 }, layout, frames, false);
            expectSameWaveform (reference, repeated, rate);
            expect (whole.deleteFile());
            expect (loop.deleteFile());
        }

        beginTest ("rate changes preserve DC and use one cumulative output clock");
        {
            const auto layout = juce::AudioChannelSet::stereo();
            AudioPlaybackPlan plan;
            long double duration = 0.0L;
            for (const auto rate : { 44100.0, 96000.0, 32000.0, 48000.0, 88200.0 })
            {
                constexpr auto length = 4319;
                plan.items.push_back ({ writeWave (rate, layout, 0, length, -1),
                                        static_cast<uint64_t> (plan.items.size() + 1) });
                duration += length * 48000.0L / rate;
            }
            const auto rendered = renderPlan (plan, layout, static_cast<int> (std::llround (duration)));
            float maximumError = 0.0f;
            for (int channel = 0; channel < layout.size(); ++channel)
                for (int frame = 128; frame < rendered.getNumSamples() - 32; ++frame)
                    maximumError = juce::jmax (maximumError,
                        std::abs (rendered.getSample (channel, frame) - 0.25f));
            expectWithinAbsoluteError (maximumError, 0.0f, 0.00001f);
            for (const auto& item : plan.items) expect (item.file.deleteFile());
        }

        beginTest ("mixed source rates retain fractional phase on the device clock");
        for (const auto rate : { 44100.0, 96000.0 })
        {
            const auto layout = juce::AudioChannelSet::stereo();
            constexpr auto length = 4319;
            const auto boundary = length * 48000.0 / rate;
            const auto first = writeWave (rate, layout, 0, length, -2);
            const auto second = writeWave (48000.0, layout, 0, length, -2, boundary);
            const auto frames = static_cast<int> (std::llround (boundary + length));
            const auto rendered = renderPlan ({ { { first, 1 }, { second, 2 } }, -1 }, layout, frames);
            float maximumError = 0.0f;
            for (int frame = static_cast<int> (std::ceil (boundary)) + 64; frame < frames - 32; ++frame)
                maximumError = juce::jmax (maximumError, std::abs (rendered.getSample (0, frame)
                    - static_cast<float> (0.05 + 0.00003 * frame)));
            expectWithinAbsoluteError (maximumError, 0.0f, 0.000001f);
            expect (first.deleteFile());
            expect (second.deleteFile());
        }

        beginTest ("seeking and unavailable items keep the remaining native stream continuous");
        for (const auto rate : { 44100.0, 96000.0 })
        {
            const auto layout = juce::AudioChannelSet::stereo();
            const auto whole = writeWave (rate, layout, 0, 9247, 0);
            const auto first = writeWave (rate, layout, 0, 4319, 0);
            const auto second = writeWave (rate, layout, 4319, 4928, 0);
            const auto missing = first.getSiblingFile ("unavailable-" + first.getFileName());
            constexpr auto seekFrame = 307;
            const auto frames = static_cast<int> (std::llround ((9247.0 - seekFrame) * 48000.0 / rate));
            const auto reference = renderPlan ({ { { whole, 1 } }, -1 }, layout, frames,
                                               true, seekFrame / rate);
            const auto split = renderPlan (
                { { { first, 1 }, { missing, 2 }, { second, 3 } }, -1 }, layout, frames,
                true, seekFrame / rate);
            expectSameWaveform (reference, split, rate);
            for (const auto& file : { whole, first, second }) expect (file.deleteFile());
        }

        beginTest ("gapless callbacks consume cached samples without waiting for disk reads");
        {
            ReaderProbe probe;
            juce::AudioFormatManager formats;
            formats.registerFormat (new ProbedAudioFormat (probe), true);
            juce::TimeSliceThread readAheadThread ("Read isolation test");
            expect (readAheadThread.startThread());
            GaplessAudioSource source (formats, readAheadThread);
            source.setDestinationLayout (juce::AudioChannelSet::create7point1point2());
            source.prepareToPlay (256, 48000.0);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("moonwake-reader-probe", ".probe");
            expect (file.replaceWithText ("probe"));
            expect (source.loadPlan ({ { { file, 1 } }, -1 }));
            probe.blockReads.store (true);
            juce::AudioBuffer<float> prefix (10, 256);
            source.getNextAudioBlock ({ &prefix, 0, 256 });
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
                    ProbedAudioReader::valueAt (channel, 511), 0.0001f);
            source.clear();
            expect (readAheadThread.stopThread (2000));
            expect (file.deleteFile());
        }

        beginTest ("worker decoding preserves positions, routing and read errors");
        {
            ReaderProbe probe;
            ChannelMappedAudioSource source (
                std::make_unique<ProbedAudioReader> (probe),
                juce::AudioChannelSet::create7point1point2());
            source.prepareToPlay (256, 48000.0);
            juce::AudioBuffer<float> output (10, 256);

            for (int block = 0; block < 300; ++block)
            {
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
            source.getNextAudioBlock ({ &output, 0, 256 });
            expect (source.hasReadError());
            expectWithinAbsoluteError (output.getMagnitude (0, 256), 0.0f, 0.0001f);
            expectEquals (source.getNextReadPosition(), juce::int64 { 1000 });
            probe.failReads.store (false);
            source.setNextReadPosition (1000);
            expect (! source.hasReadError());
            source.getNextAudioBlock ({ &output, 0, 256 });
            expectWithinAbsoluteError (output.getSample (9, 0),
                ProbedAudioReader::valueAt (9, 1000), 0.0001f);
        }

        beginTest ("gapless FIFO wraps and resumes after starvation without losing frames");
        {
            ReaderProbe probe;
            juce::AudioFormatManager formats;
            formats.registerFormat (new ProbedAudioFormat (probe), true);
            juce::TimeSliceThread thread ("Gapless FIFO test");
            expect (thread.startThread());
            GaplessAudioSource source (formats, thread);
            source.setDestinationLayout (juce::AudioChannelSet::create7point1point2());
            source.prepareToPlay (256, 48000.0);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("moonwake-fifo-probe", ".probe");
            expect (file.replaceWithText ("probe"));
            expect (source.loadPlan ({ { { file, 1 } }, -1 }));
            const auto generation = source.progress().generation;
            source.releaseResources();
            juce::AudioBuffer<float> output (10, 256);
            const auto renderAndVerify = [&]
            {
                const auto before = source.getNextReadPosition();
                {
#if JUCE_ENABLE_ALLOCATION_HOOKS
                    juce::UnitTestAllocationChecker allocationChecker (*this);
#endif
                    source.getNextAudioBlock ({ &output, 0, 256 });
                }
                const auto count = static_cast<int> (source.getNextReadPosition() - before);
                for (int channel = 0; channel < 10; ++channel)
                {
                    if (count > 0)
                        expectWithinAbsoluteError (output.getSample (channel, count - 1),
                            ProbedAudioReader::valueAt (channel, before + count - 1), 0.0001f);
                    if (count < 256)
                        expectWithinAbsoluteError (output.getSample (channel, count), 0.0f, 0.0001f);
                }
                return count;
            };
            bool starved = false;
            for (int block = 0; block < 300 && ! starved; ++block)
                starved = renderAndVerify() == 0;
            expect (starved);
            expect (! source.progress().finished);
            const auto stoppedAt = source.getNextReadPosition();
            expectEquals (renderAndVerify(), 0);
            expectEquals (source.getNextReadPosition(), stoppedAt);
            source.prepareToPlay (256, 48000.0);
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
            while (source.getNextReadPosition() < 100000
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                if (renderAndVerify() == 0) juce::Thread::sleep (1);
            expect (source.getNextReadPosition() >= 100000);
            expectEquals (source.progress().generation, generation);
            expectEquals (source.progress().sequence, uint64_t { 0 });
            source.clear();
            expect (thread.stopThread (2000));
            expect (file.deleteFile());
        }

        beginTest ("EOF snapshots identify the last consumed occurrence");
        {
            ReaderProbe probe;
            probe.length = 17;
            juce::AudioFormatManager formats;
            formats.registerFormat (new ProbedAudioFormat (probe), true);
            juce::TimeSliceThread thread ("Gapless snapshot test");
            expect (thread.startThread());
            GaplessAudioSource source (formats, thread);
            source.prepareToPlay (256, 48000.0);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("moonwake-snapshot-probe", ".probe");
            expect (file.replaceWithText ("probe"));
            std::atomic_bool stopObserver { false }, inconsistent { false };
            std::thread observer ([&]
            {
                while (! stopObserver.load())
                {
                    const auto progress = source.progress();
                    if (progress.finished && (progress.sequence != 1 || progress.token != 22))
                        inconsistent.store (true);
                }
            });
            juce::AudioBuffer<float> output (2, 34);
            for (int repetition = 0; repetition < 50; ++repetition)
            {
                expect (source.loadPlan ({ { { file, 11 }, { file, 22 } }, -1 }));
                source.getNextAudioBlock ({ &output, 0, 34 });
                const auto progress = source.progress();
                expect (progress.finished);
                expectEquals (progress.sequence, uint64_t { 1 });
            }
            stopObserver.store (true);
            observer.join();
            expect (! inconsistent.load());
            source.clear();
            expect (thread.stopThread (2000));
            expect (file.deleteFile());
        }

        beginTest ("gapless failures distinguish initial rejection from queued audio");
        {
            ReaderProbe probe;
            juce::AudioFormatManager formats;
            formats.registerFormat (new ProbedAudioFormat (probe), true);
            juce::TimeSliceThread thread ("Gapless failure test");
            expect (thread.startThread());
            GaplessAudioSource source (formats, thread);
            source.prepareToPlay (256, 48000.0);
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("moonwake-failure-probe", ".probe");
            expect (file.replaceWithText ("probe"));
            probe.failReads.store (true);
            expect (! source.loadPlan ({ { { file, 1 }, { file, 2 } }, -1 }));
            auto failures = source.drainPreparationFailures();
            expectEquals (static_cast<int> (failures.size()), 1);
            if (! failures.empty()) expect (! failures.front().hadBufferedAudio);
            probe.failReads.store (false);
            expect (source.loadPlan ({ { { file, 1 } }, -1 }));
            probe.failReads.store (true);
            juce::AudioBuffer<float> output (2, 256);
            source.getNextAudioBlock ({ &output, 0, 256 });
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (! source.progress().readError
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (1);
            expect (source.progress().readError);
            failures = source.drainPreparationFailures();
            expectEquals (static_cast<int> (failures.size()), 1);
            if (! failures.empty()) expect (failures.front().hadBufferedAudio);
            source.clear();
            expect (thread.stopThread (2000));
            expect (file.deleteFile());
        }

        beginTest ("future read failures report native prefixes retained for playback");
        {
            ReaderProbe probe;
            probe.length = 2048;
            probe.failFromFrame = 1024;
            const auto layout = juce::AudioChannelSet::stereo();
            const auto first = writeWave (48000.0, layout, 0, 97, -1);
            const auto last = writeWave (48000.0, layout, 0, 256, -1);
            const auto partial = first.getSiblingFile (first.getFileNameWithoutExtension() + ".probe");
            expect (partial.replaceWithText ("probe"));
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            formats.registerFormat (new ProbedAudioFormat (probe), false);
            juce::TimeSliceThread thread ("Partial future read test");
            expect (thread.startThread());
            GaplessAudioSource source (formats, thread);
            source.prepareToPlay (256, 48000.0);
            expect (source.loadPlan ({ { { first, 11 }, { partial, 22 }, { last, 33 } }, -1 }));
            const auto failures = source.drainPreparationFailures();
            expectEquals (static_cast<int> (failures.size()), 1);
            if (! failures.empty())
            {
                expectEquals (failures.front().sequence, uint64_t { 1 });
                expect (failures.front().hadBufferedAudio);
            }
            juce::AudioBuffer<float> output (2, 1377);
            source.getNextAudioBlock ({ &output, 0, 161 });
            expectEquals (source.progress().sequence, uint64_t { 1 });
            expectEquals (source.progress().token, uint64_t { 22 });
            source.getNextAudioBlock ({ &output, 161, 1216 });
            expect (source.progress().finished);
            expect (! source.progress().readError);
            expectEquals (source.progress().sequence, uint64_t { 2 });
            for (int frame = 0; frame < 1024; ++frame)
                expectWithinAbsoluteError (output.getSample (0, frame + 97),
                                           ProbedAudioReader::valueAt (0, frame), 0.000001f);
            source.clear();
            expect (thread.stopThread (2000));
            for (const auto& file : { first, partial, last }) expect (file.deleteFile());
        }

        beginTest ("gapless device rate and layout changes resume the consumed occurrence");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("moonwake-gapless-rate", ".wav");
            juce::WavAudioFormat wav;
            auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                .withSampleRate (48000.0).withNumChannels (10).withBitsPerSample (24));
            expect (writer != nullptr);
            if (writer)
            {
                juce::AudioBuffer<float> input (10, 2000);
                for (int channel = 0; channel < 10; ++channel)
                    for (int sample = 0; sample < input.getNumSamples(); ++sample)
                        input.setSample (channel, sample, 0.02f * static_cast<float> (channel + 1));
                expect (writer->writeFromAudioSampleBuffer (input, 0, input.getNumSamples()));
                writer.reset();
            }

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::TimeSliceThread thread ("Gapless configuration test");
            expect (thread.startThread());
            {
                GaplessAudioSource source (formats, thread);
                source.setDestinationLayout (juce::AudioChannelSet::create7point1point2());
                source.prepareToPlay (256, 48000.0);
                expect (source.loadPlan ({ { { file, 11 }, { file, 22 } }, 0 }));
                juce::AudioBuffer<float> output (10, 512);
                source.getNextAudioBlock ({ &output, 0, 512 });
                const auto original = source.progress();
                source.prepareToPlay (256, 96000.0);
                expectEquals (source.progress().generation, original.generation);
                expectEquals (source.progress().sequence, original.sequence);
                expectWithinAbsoluteError (source.progress().positionSeconds,
                                           original.positionSeconds, 0.000001);
                source.getNextAudioBlock ({ &output, 0, 512 });
                expectWithinAbsoluteError (source.progress().positionSeconds,
                                           original.positionSeconds + 512.0 / 96000.0, 0.000001);
                for (int channel = 0; channel < 10; ++channel)
                    expectWithinAbsoluteError (output.getSample (channel, 511),
                                               0.02f * static_cast<float> (channel + 1), 0.0001f);

                source.prepareToPlay (256, 48000.0);
                expect (source.seekToSeconds (0.04));
                source.getNextAudioBlock ({ &output, 0, 256 });
                expectEquals (source.progress().sequence, uint64_t { 1 });
                expectEquals (source.progress().token, uint64_t { 22 });
                expectWithinAbsoluteError (source.progress().positionSeconds, 176.0 / 48000.0, 0.000001);
                source.setDestinationLayout (juce::AudioChannelSet::stereo());
                source.setDestinationLayout (juce::AudioChannelSet::create7point1point2());
                expectEquals (source.progress().sequence, uint64_t { 1 });
                {
#if JUCE_ENABLE_ALLOCATION_HOOKS
                    juce::UnitTestAllocationChecker allocationChecker (*this);
#endif
                    source.getNextAudioBlock ({ &output, 0, 256 });
                }
                expectWithinAbsoluteError (output.getSample (9, 255), 0.2f, 0.0001f);
                source.clear();
            }
            expect (thread.stopThread (2000));
            expect (file.deleteFile());
        }
    }

private:
    juce::File writeWave (double rate, const juce::AudioChannelSet& layout,
                         int start, int length, int period, double timelineStart = 0.0)
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("moonwake-continuous-pcm", ".wav");
        juce::WavAudioFormat wav;
        auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
            .withSampleRate (rate).withChannelLayout (juce::AudioChannelSet::discreteChannels (layout.size()))
            .withBitsPerSample (32)
            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        expect (writer != nullptr, "Could not write " + file.getFullPathName());
        if (writer)
        {
            juce::AudioBuffer<float> input (layout.size(), length);
            for (int channel = 0; channel < layout.size(); ++channel)
                for (int frame = 0; frame < length; ++frame)
                {
                    const auto phase = period > 0
                        ? 2.0 * juce::MathConstants<double>::pi * (start + frame) / period
                        : (start + frame) * (0.031 + channel * 0.0037);
                    input.setSample (channel, frame, period == -1 ? 0.25f
                        : period == -2 ? static_cast<float> (0.05 + 0.00003
                            * (timelineStart + (start + frame) * 48000.0 / rate))
                        : static_cast<float> (0.15 + 0.16 * std::sin (phase)
                                                   + 0.07 * std::cos (phase * 3.0 + channel * 0.1)));
                }
            expect (writer->writeFromAudioSampleBuffer (input, 0, length));
        }
        return file;
    }

    juce::AudioBuffer<float> renderPlan (AudioPlaybackPlan plan,
        const juce::AudioChannelSet& layout, int frames, bool expectEnd = true,
        double startSeconds = 0.0)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::TimeSliceThread thread ("Continuous PCM regression");
        expect (thread.startThread());
        GaplessAudioSource source (formats, thread);
        source.setDestinationLayout (layout);
        source.prepareToPlay (256, 48000.0);
        const auto loaded = source.loadPlan (std::move (plan), startSeconds);
        expect (loaded);
        juce::AudioBuffer<float> rendered (layout.size(), frames);
        rendered.clear();
        juce::AudioBuffer<float> block (layout.size(), 1024);
        int position = 0, iteration = 0;
        const int sizes[] { 1, 17, 255, 1024 };
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (loaded && position < frames && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            const auto before = source.getNextReadPosition();
            const auto requested = juce::jmin (sizes[iteration++ % 4], frames - position);
            {
#if JUCE_ENABLE_ALLOCATION_HOOKS
                juce::UnitTestAllocationChecker allocationChecker (*this);
#endif
                source.getNextAudioBlock ({ &block, 0, requested });
            }
            const auto count = static_cast<int> (source.getNextReadPosition() - before);
            for (int channel = 0; channel < layout.size(); ++channel)
                rendered.copyFrom (channel, position, block, channel, 0, count);
            position += count;
            if (count == 0)
            {
                if (source.progress().finished || source.progress().readError) break;
                juce::Thread::sleep (1);
            }
        }
        expectEquals (position, frames);
        expect (! source.progress().readError);
        if (expectEnd && loaded)
        {
            while (! source.progress().finished && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (1);
            expect (source.progress().finished);
            expectEquals (source.getTotalLength(), static_cast<juce::int64> (frames));
        }
        source.clear();
        expect (thread.stopThread (2000));
        return rendered;
    }

    void expectSameWaveform (const juce::AudioBuffer<float>& reference,
                             const juce::AudioBuffer<float>& actual, double rate)
    {
        float maximumError = 0.0f;
        for (int channel = 0; channel < reference.getNumChannels(); ++channel)
            for (int frame = 0; frame < reference.getNumSamples(); ++frame)
                maximumError = juce::jmax (maximumError,
                    std::abs (reference.getSample (channel, frame) - actual.getSample (channel, frame)));
        expectWithinAbsoluteError (maximumError, 0.0f, 0.000001f,
            "Rate " + juce::String (rate) + ", channels " + juce::String (reference.getNumChannels()));
    }

};

ChannelMappedAudioSourceTests channelMappedAudioSourceTests;
} // namespace
