#include "audio/OutputMeter.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
constexpr double testRate = 48000.0;

bool awaitSeconds (OutputMeter& meter, double seconds)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        const auto value = meter.getReadings();
        if (value.measurementInterrupted) return false;
        if (value.measuredSeconds + 0.5 / testRate >= seconds) return true;
        juce::Thread::sleep (1);
    }
    return false;
}

bool feedTone (OutputMeter& meter, const juce::AudioChannelSet& layout,
               double seconds, float amplitude = 0.1f, int onlyChannel = -1,
               double frequency = 1000.0, double phase = 0.0,
               double rate = testRate)
{
    const auto readyDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! meter.getReadings().available)
    {
        if (juce::Time::getMillisecondCounterHiRes() >= readyDeadline) return false;
        juce::Thread::sleep (1);
    }
    const auto startSeconds = meter.getReadings().measuredSeconds;
    const auto totalFrames = static_cast<int> (std::llround (seconds * rate));
    juce::AudioBuffer<float> block (layout.size(), 4096);
    for (int start = 0; start < totalFrames;)
    {
        const auto frames = juce::jmin (block.getNumSamples(), totalFrames - start);
        block.clear();
        for (int channel = 0; channel < layout.size(); ++channel)
            if (onlyChannel < 0 || channel == onlyChannel)
                for (int i = 0; i < frames; ++i)
                    block.setSample (channel, i, amplitude * static_cast<float> (
                        std::sin (juce::MathConstants<double>::twoPi * frequency
                                  * static_cast<double> (start + i) / rate + phase)));
        meter.push (block, 0, frames, meter.beginBlock());
        start += frames;
        if (! awaitSeconds (meter, startSeconds + static_cast<double> (start) / rate)) return false;
    }
    return true;
}

class OutputMeterTests final : public juce::UnitTest
{
public:
    OutputMeterTests() : UnitTest ("OutputMeter", "audio") {}

    void runTest() override
    {
        beginTest ("known 1 kHz sine measures mono and stereo loudness, RMS and linear peaks");
        for (const auto& layout : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        {
            OutputMeter meter;
            meter.prepare (testRate, layout);
            expect (feedTone (meter, layout, 3.2));
            const auto reading = meter.getReadings();
            const auto expected = -23.0 + 10.0 * std::log10 (static_cast<double> (layout.size()));
            expect (reading.available);
            expectWithinAbsoluteError (reading.momentaryLufs, expected, 0.12);
            expectWithinAbsoluteError (reading.shortTermLufs, expected, 0.12);
            expectWithinAbsoluteError (reading.integratedLufs, expected, 0.12);
            expectWithinAbsoluteError (reading.samplePeak[0], 0.1f, 0.0001f);
            expectWithinAbsoluteError (reading.rms[0], 0.1f / std::sqrt (2.0f), 0.0001f);
            expect (reading.truePeak[0] >= reading.samplePeak[0]);
            expectWithinAbsoluteError (reading.measuredSeconds, 3.2, 0.0001);
        }

        beginTest ("momentary and short-term require complete windows; silent input has no finite LUFS");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::mono();
            meter.prepare (testRate, layout);
            expect (feedTone (meter, layout, 0.2));
            expect (std::isinf (meter.getReadings().momentaryLufs));
            expect (feedTone (meter, layout, 0.5));
            expect (std::isfinite (meter.getReadings().momentaryLufs));
            expect (std::isinf (meter.getReadings().shortTermLufs));
            meter.reset();
            expect (feedTone (meter, layout, 3.2, 0.0f));
            const auto reading = meter.getReadings();
            expect (std::isinf (reading.momentaryLufs) && reading.momentaryLufs < 0.0);
            expect (std::isinf (reading.shortTermLufs) && reading.shortTermLufs < 0.0);
            expect (std::isinf (reading.integratedLufs) && reading.integratedLufs < 0.0);
            expectEquals (reading.rms[0], 0.0f);
        }

        beginTest ("gated integrated loudness rejects a sustained very quiet section");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::mono();
            meter.prepare (testRate, layout);
            expect (feedTone (meter, layout, 3.2));
            const auto loud = meter.getReadings().integratedLufs;
            expect (feedTone (meter, layout, 3.2, 0.001f));
            const auto quiet = meter.getReadings();
            expectWithinAbsoluteError (quiet.momentaryLufs, -63.0, 0.12);
            expectWithinAbsoluteError (quiet.integratedLufs, loud, 0.25);
        }

        beginTest ("LFE is excluded from LUFS while all LFE level measurements remain active");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::create5point1();
            const auto lfe = layout.getChannelIndexForType (juce::AudioChannelSet::LFE);
            meter.prepare (testRate, layout);
            expect (feedTone (meter, layout, 3.2, 0.25f, lfe));
            const auto reading = meter.getReadings();
            expect (std::isinf (reading.integratedLufs));
            expect (std::isinf (reading.momentaryLufs));
            expectWithinAbsoluteError (reading.samplePeak[static_cast<size_t> (lfe)], 0.25f, 0.0001f);
            expectWithinAbsoluteError (reading.rms[static_cast<size_t> (lfe)], 0.25f / std::sqrt (2.0f), 0.0001f);
            expect (reading.maxTruePeak[static_cast<size_t> (lfe)] >= 0.25f);
        }

        beginTest ("all output layouts retain JUCE channel order including height and 16-channel output");
        for (const auto& description : outputLayouts())
        {
            const auto& layout = description.channels;
            OutputMeter meter;
            meter.prepare (testRate, layout);
            // The last channel reaches the meter independently of the first pair.
            const auto channel = layout.size() - 1;
            expect (feedTone (meter, layout, 0.6, 0.2f, channel), description.name);
            const auto reading = meter.getReadings();
            expectEquals (reading.channels, layout.size(), description.name);
            expectWithinAbsoluteError (reading.samplePeak[static_cast<size_t> (channel)], 0.2f, 0.0001f);
            if (channel > 0) expectEquals (reading.samplePeak[0], 0.0f);
            const auto type = layout.getTypeOfChannel (channel);
            if (type != juce::AudioChannelSet::LFE)
                expect (std::isfinite (reading.momentaryLufs), description.name);
        }

        beginTest ("side surround weighting and upper-channel weighting follow their EBU positions");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::create7point1point6();
            for (const auto type : { juce::AudioChannelSet::leftSurroundSide,
                                     juce::AudioChannelSet::topSideLeft,
                                     juce::AudioChannelSet::topRearRight })
            {
                meter.prepare (testRate, layout);
                expect (feedTone (meter, layout, 0.6, 0.1f, layout.getChannelIndexForType (type)));
                const auto weighting = type == juce::AudioChannelSet::leftSurroundSide
                                           ? 10.0 * std::log10 (1.41) : 0.0;
                expectWithinAbsoluteError (meter.getReadings().momentaryLufs, -23.0 + weighting, 0.12);
            }
        }

        beginTest ("inter-sample true peak exceeds sample peak across block boundaries at 48 and 96 kHz");
        for (const double rate : { 48000.0, 96000.0 })
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::mono();
            meter.prepare (rate, layout);
            expect (feedTone (meter, layout, 0.6, 0.9f, 0, rate / 4.0,
                              juce::MathConstants<double>::pi / 4.0, rate));
            const auto reading = meter.getReadings();
            expectWithinAbsoluteError (reading.samplePeak[0], 0.9f / std::sqrt (2.0f), 0.0001f);
            expect (reading.truePeak[0] > reading.samplePeak[0] * 1.3f);
            expect (reading.maxTruePeak[0] >= reading.truePeak[0]);
        }

        beginTest ("reset invalidates queued audio and a block rendered before the reset");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::mono();
            meter.prepare (testRate, layout);
            expect (feedTone (meter, layout, 0.6, 0.8f));
            const auto oldEpoch = meter.beginBlock();
            juce::AudioBuffer<float> oldBlock (1, 8192);
            for (int i = 0; i < oldBlock.getNumSamples(); ++i) oldBlock.setSample (0, i, 1.0f);
            meter.push (oldBlock, 0, oldBlock.getNumSamples(), oldEpoch);
            meter.reset();
            expectEquals (meter.getReadings().maxTruePeak[0], 0.0f);
            meter.push (oldBlock, 0, oldBlock.getNumSamples(), oldEpoch);
            expect (feedTone (meter, layout, 0.6, 0.1f));
            const auto reading = meter.getReadings();
            expectWithinAbsoluteError (reading.measuredSeconds, 0.6, 0.0001);
            expect (reading.maxTruePeak[0] < 0.12f);
            expectWithinAbsoluteError (reading.integratedLufs, -23.0, 0.12);
        }

        beginTest ("RMS tracks the last 300 ms and peak hold survives silence until reset");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::mono();
            meter.prepare (testRate, layout);
            expect (feedTone (meter, layout, 0.6, 0.4f));
            const auto peak = meter.getReadings().maxTruePeak[0];
            expect (feedTone (meter, layout, 0.6, 0.0f));
            const auto reading = meter.getReadings();
            expectWithinAbsoluteError (reading.rms[0], 0.0f, 0.000001f);
            expectWithinAbsoluteError (reading.samplePeak[0], 0.4f * std::pow (10.0f, -0.6f), 0.001f);
            expectEquals (reading.maxTruePeak[0], peak);
        }

        beginTest ("callback copies only the active region with no JUCE allocations and pads missing channels");
        {
            OutputMeter meter;
            meter.prepare (testRate, juce::AudioChannelSet::stereo());
            juce::AudioBuffer<float> block (1, 4800 + 32);
            for (int i = 0; i < block.getNumSamples(); ++i) block.setSample (0, i, 1.0f);
            block.clear (0, 16, 4800);
            {
                juce::UnitTestAllocationChecker allocationChecker (*this);
                meter.push (block, 16, 4800, meter.beginBlock());
            }
            expect (awaitSeconds (meter, 0.1));
            const auto reading = meter.getReadings();
            expectEquals (reading.samplePeak[0], 0.0f);
            expectEquals (reading.samplePeak[1], 0.0f);
        }

        beginTest ("bounded queue overflow exposes interruption and manual reset starts clean measurement");
        {
            OutputMeter meter;
            const auto layout = juce::AudioChannelSet::create9point1point6();
            meter.prepare (testRate, layout);
            juce::AudioBuffer<float> burst (layout.size(), 512 * 1024);
            burst.clear();
            meter.push (burst, 0, burst.getNumSamples(), meter.beginBlock());
            const auto interrupted = meter.getReadings();
            expect (interrupted.measurementInterrupted);
            expect (interrupted.droppedFrames > 0);
            meter.reset();
            expect (! meter.getReadings().measurementInterrupted);
            expectEquals (meter.getReadings().droppedFrames, uint64_t { 0 });
            expect (feedTone (meter, layout, 0.6, 0.1f, 0));
            expectWithinAbsoluteError (meter.getReadings().integratedLufs, -23.0, 0.12);
        }
    }
};

OutputMeterTests outputMeterTests;
} // namespace
