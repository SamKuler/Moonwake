#include "audio/GaplessAudioSource.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace
{
class DelayedQueueWav final : public juce::WavAudioFormat
{
public:
    std::atomic_bool delay { false }, openedFuture { false };
    juce::AudioFormatReader* createReaderFor (juce::InputStream* input, bool deleteOnFailure) override
    {
        if (auto* stream = dynamic_cast<juce::FileInputStream*> (input);
            delay.load() && stream && stream->getFile().getFileName() == "C.wav")
        {
            openedFuture.store (true);
            juce::Thread::sleep (80);
        }
        return juce::WavAudioFormat::createReaderFor (input, deleteOnFailure);
    }
};

class QueuePublicationTests final : public juce::UnitTest
{
public:
    QueuePublicationTests() : UnitTest ("Queue publication", "audio") {}

    void runTest() override
    {
        beginTest ("create queue publication signals");
        const auto root = juce::File::getCurrentWorkingDirectory().getChildFile ("build/queue-publication-tests")
            .getNonexistentChildFile ("signals", {});
        const auto created = root.createDirectory();
        expect (created.wasOk(), created.getErrorMessage());
        const auto write = [&] (const juce::String& name, int frames, double rate, float value, bool waveform = false)
        {
            const auto file = root.getChildFile (name);
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            juce::WavAudioFormat format;
            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                .withSampleRate (rate).withNumChannels (2).withBitsPerSample (32)
                .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            expect (writer != nullptr);
            if (writer)
            {
                juce::AudioBuffer<float> samples (2, frames);
                for (int channel = 0; channel < 2; ++channel)
                    for (int frame = 0; frame < frames; ++frame)
                        samples.setSample (channel, frame, value + (waveform ? static_cast<float> (
                            0.1 * std::sin (frame * (0.071 + channel * 0.013))
                            + 0.03 * std::cos (frame * 0.173)) : 0.0f));
                expect (writer->writeFromAudioSampleBuffer (samples, 0, frames));
            }
            return file;
        };
        const auto future = write ("future.wav", 48000, 48000.0, 0.7f);
        for (const auto rate : { 44100.0, 48000.0 })
        {
            const auto current = write ("wave-" + juce::String (rate) + ".wav", static_cast<int> (rate * 4), rate, 0.3f, true);
            for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                                       OutputLayout::sevenOneTwo, OutputLayout::nineOneSix })
            {
                beginTest ("repeated edits preserve each waveform sample at " + juce::String (rate) + " Hz / " + toString (layout));
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                juce::TimeSliceThread worker ("Queue waveform read-ahead");
                worker.startThread();
                GaplessAudioSource baseline (formats, worker), edited (formats, worker);
                for (auto* source : { &baseline, &edited })
                {
                    source->setDestinationLayout (toAudioChannelSet (layout));
                    source->prepareToPlay (256, 48000.0);
                    expect (source->loadPlan ({ { { current, 11 }, { future, 22 } }, -1 }));
                }
                juce::AudioBuffer<float> reference (getChannelCount (layout), 256), actual (getChannelCount (layout), 256);
                float maximumError = 0.0f;
                for (int block = 0; block < 100; ++block)
                {
                    if (block % 8 == 3)
                    {
                        const auto before = edited.progress();
                        expect (edited.setUpcoming ({ { future, 22 } }, (block / 8) % 2 ? 0 : -1, before));
                        const auto after = edited.progress();
                        expect (after.generation > before.generation);
                        expectEquals (after.token, uint64_t { 11 });
                        expectWithinAbsoluteError (after.positionSeconds, before.positionSeconds, 0.000000001);
                    }
                    baseline.getNextAudioBlock ({ &reference, 0, 256 });
                    {
                        juce::UnitTestAllocationChecker noAllocations (*this);
                        edited.getNextAudioBlock ({ &actual, 0, 256 });
                    }
                    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
                        for (int frame = 0; frame < 256; ++frame)
                            maximumError = std::max (maximumError, std::abs (
                                reference.getSample (channel, frame) - actual.getSample (channel, frame)));
                    if (block % 8 == 0) juce::Thread::sleep (1);
                }
                expectWithinAbsoluteError (maximumError, 0.0f, 0.000001f);
                expect (baseline.seekToSeconds (0.25));
                expect (edited.seekToSeconds (0.25));
                baseline.getNextAudioBlock ({ &reference, 0, 256 });
                edited.getNextAudioBlock ({ &actual, 0, 256 });
                for (int frame = 0; frame < 256; ++frame)
                    expectWithinAbsoluteError (actual.getSample (0, frame), reference.getSample (0, frame), 0.000001f);
                baseline.releaseResources();
                edited.releaseResources();
            }
        }

        beginTest ("publication rejects a stale current track while its replacement is prepared");
        {
            const auto a = write ("A.wav", 8192, 48000.0, 0.2f);
            const auto b = write ("B.wav", 48000, 48000.0, 0.4f);
            const auto c = write ("C.wav", 48000, 48000.0, 0.8f);
            juce::AudioFormatManager formats;
            auto* wav = new DelayedQueueWav;
            formats.registerFormat (wav, true);
            juce::TimeSliceThread worker ("Queue publication read-ahead");
            worker.startThread();
            GaplessAudioSource source (formats, worker);
            source.prepareToPlay (128, 48000.0);
            expect (source.loadPlan ({ { { a, 101 }, { b, 202 } }, -1 }));
            expect (worker.stopThread (2000));
            expect (worker.startThread());
            juce::AudioBuffer<float> block (2, 128);
            for (int n = 0; n < 56; ++n) source.getNextAudioBlock ({ &block, 0, 128 });
            const auto expected = source.progress();
            expectEquals (expected.token, uint64_t { 101 });
            wav->delay.store (true);
            std::atomic<int> wrongFuture { 0 }, silent { 0 };
            std::thread consumer ([&]
            {
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
                while (! wav->openedFuture.load() && juce::Time::getMillisecondCounterHiRes() < deadline)
                    std::this_thread::yield();
                juce::AudioBuffer<float> audio (2, 128);
                for (int n = 0; n < 64; ++n)
                {
                    source.getNextAudioBlock ({ &audio, 0, 128 });
                    for (int i = 0; i < 128; ++i)
                    {
                        const auto value = audio.getSample (0, i);
                        if (value > 0.5f) ++wrongFuture;
                        if (std::abs (value) < 0.1f) ++silent;
                    }
                    std::this_thread::sleep_for (std::chrono::microseconds (2667));
                }
            });
            const auto accepted = source.setUpcoming ({ { c, 303 } }, -1, expected);
            consumer.join();
            expect (! accepted);
            expect (wav->openedFuture.load());
            expectEquals (source.progress().token, uint64_t { 202 });
            expectEquals (wrongFuture.load(), 0);
            expectEquals (silent.load(), 0);
            source.getNextAudioBlock ({ &block, 0, 128 });
            expectWithinAbsoluteError (block.getSample (0, 0), 0.4f, 0.000001f);
            wav->delay.store (false);
            expect (source.setUpcoming ({ { c, 303 } }, -1, source.progress()));
            expectEquals (source.progress().token, uint64_t { 202 });
            source.releaseResources();
        }
        beginTest ("future replacement retains the renderer's half-frame boundaries");
        {
            struct RateCase { int frames; double sourceRate, deviceRate; };
            const auto oldFuture = write ("fraction-B.wav", 48000, 48000.0, 0.4f);
            const auto newFuture = write ("fraction-C.wav", 48000, 48000.0, 0.7f);
            for (const auto test : { RateCase { 27, 96000.0, 48000.0 }, RateCase { 49, 96000.0, 48000.0 },
                                     RateCase { 1840, 48000.0, 44100.0 } })
            {
                const auto current = write ("fraction-" + juce::String (test.frames) + ".wav",
                                            test.frames, test.sourceRate, 0.2f, true);
                const auto end = static_cast<int> (std::llround (
                    static_cast<long double> (test.frames) * test.deviceRate / test.sourceRate));
                for (const auto updates : { 0, 1, 3 })
                {
                    juce::AudioFormatManager formats;
                    formats.registerBasicFormats();
                    juce::TimeSliceThread worker ("Half-frame queue boundary");
                    expect (worker.startThread());
                    GaplessAudioSource baseline (formats, worker), edited (formats, worker);
                    for (auto* source : { &baseline, &edited })
                    {
                        source->prepareToPlay (128, test.deviceRate);
                        expect (source->loadPlan ({ { { current, 1 }, { oldFuture, 2 } }, -1 }));
                    }
                    expect (worker.stopThread (2000));
                    for (int update = 0; update < updates; ++update)
                        expect (edited.setUpcoming ({ { update % 2 == 0 ? newFuture : oldFuture,
                                                        update % 2 == 0 ? uint64_t { 3 } : uint64_t { 2 } } },
                                                    -1, edited.progress()));
                    juce::AudioBuffer<float> reference (2, end), actual (2, end);
                    baseline.getNextAudioBlock ({ &reference, 0, end });
                    edited.getNextAudioBlock ({ &actual, 0, end });
                    const auto label = juce::String (test.frames) + " native frames, " + juce::String (updates) + " updates";
                    expectEquals (baseline.progress().token, uint64_t { 1 }, label);
                    expectEquals (edited.progress().token, uint64_t { 1 }, label);
                    float maximumError = 0.0f;
                    for (int channel = 0; channel < 2; ++channel)
                        for (int frame = 0; frame < end; ++frame)
                            maximumError = std::max (maximumError, std::abs (
                                reference.getSample (channel, frame) - actual.getSample (channel, frame)));
                    expectWithinAbsoluteError (maximumError, 0.0f, 1.0e-7f, label);
                    baseline.getNextAudioBlock ({ &reference, 0, 1 });
                    edited.getNextAudioBlock ({ &actual, 0, 1 });
                    expectEquals (baseline.progress().token, uint64_t { 2 }, label);
                    expectEquals (edited.progress().token, updates == 0 ? uint64_t { 2 } : uint64_t { 3 }, label);
                    baseline.releaseResources();
                    edited.releaseResources();
                }
            }
        }

        beginTest ("paused future replacement preserves the tail beyond a full PCM FIFO");
        {
            const auto tailDirectory = juce::File::getCurrentWorkingDirectory().getChildFile ("build/queue-publication-tests")
                .getNonexistentChildFile ("paused-tail", {});
            expect (tailDirectory.createDirectory());
            const auto valueAt = [] (int kind, int frame)
            {
                return (kind == 0 ? 0.125f : kind == 1 ? -0.375f : 0.625f)
                    + static_cast<float> (frame) / 1048576.0f;
            };
            const auto writeSignal = [&] (const juce::File& file, int kind, int frames)
            {
                juce::WavAudioFormat format;
                auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
                auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions()
                    .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (32)
                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                if (! writer) return false;
                juce::AudioBuffer<float> samples (2, frames);
                for (int channel = 0; channel < 2; ++channel)
                    for (int frame = 0; frame < frames; ++frame)
                        samples.setSample (channel, frame, valueAt (kind, frame));
                return writer->writeFromAudioSampleBuffer (samples, 0, frames);
            };
            const auto oldFuture = tailDirectory.getChildFile ("old-future.wav");
            const auto newFuture = tailDirectory.getChildFile ("new-future.wav");
            expect (writeSignal (oldFuture, 1, 4096));
            expect (writeSignal (newFuture, 2, 4096));
            for (const auto frames : { 65537, 65538 })
            {
                const auto current = tailDirectory.getChildFile ("current-" + juce::String (frames) + ".wav");
                expect (writeSignal (current, 0, frames));
                for (const int futureUpdates : { 0, 1, 3 })
                {
                    juce::AudioFormatManager formats;
                    formats.registerBasicFormats();
                    juce::TimeSliceThread worker ("Paused tail test");
                    expect (worker.startThread());
                    GaplessAudioSource source (formats, worker);
                    source.prepareToPlay (256, 48000.0);
                    expect (source.loadPlan ({ { { current, 1 }, { oldFuture, 2 } }, -1 }));
                    // Join the initial fill while no callback has consumed its PCM.
                    expect (worker.stopThread (2000));
                    for (int change = 0; change < futureUpdates; ++change)
                        expect (source.setUpcoming ({ { change % 2 == 0 ? newFuture : oldFuture,
                                                       change % 2 == 0 ? uint64_t { 3 } : uint64_t { 2 } } },
                                                    -1, source.progress()));
                    expect (worker.startThread());
                    juce::AudioBuffer<float> output (2, 256);
                    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
                    int verified = 0;
                    while (verified < frames + 64 && juce::Time::getMillisecondCounterHiRes() < deadline)
                    {
                        const auto before = source.getNextReadPosition();
                        source.getNextAudioBlock ({ &output, 0, 256 });
                        const auto count = static_cast<int> (source.getNextReadPosition() - before);
                        for (int sample = 0; sample < count && verified < frames + 64; ++sample, ++verified)
                        {
                            const auto expected = verified < frames ? valueAt (0, verified)
                                : valueAt (futureUpdates > 0 ? 2 : 1, verified - frames);
                            for (int channel = 0; channel < 2; ++channel)
                                expectWithinAbsoluteError (output.getSample (channel, sample), expected, 1.0e-7f,
                                    "A length " + juce::String (frames) + ", output frame " + juce::String (verified));
                        }
                        if (count == 0) juce::Thread::sleep (1);
                    }
                    expectEquals (verified, frames + 64);
                    source.releaseResources();
                    expect (worker.stopThread (2000));
                }
            }
            expect (tailDirectory.deleteRecursively());
        }

        expect (root.deleteRecursively());
    }
};
QueuePublicationTests queuePublicationTests;
}
