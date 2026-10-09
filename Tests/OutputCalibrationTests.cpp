#include "audio/OutputCalibrationProcessor.h"

#include <juce_core/juce_core.h>
#include <cmath>

namespace
{
class OutputCalibrationTests final : public juce::UnitTest
{
public:
    OutputCalibrationTests() : UnitTest ("OutputCalibration", "audio") {}
    void runTest() override
    {
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                   OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                   OutputLayout::nineOneSix })
        {
            beginTest ("calibration applies independent speaker gain polarity and delay in " + toString (layout));
            const auto speakers = toAudioChannelSet (layout);
            auto settings = defaultOutputCalibration();
            for (auto& setting : settings)
                if (const auto index = speakers.getChannelIndexForType (setting.channel); index >= 0)
                {
                    setting.trimDb = index % 2 == 0 ? -6.0206f : 6.0206f;
                    setting.delayMs = static_cast<double> (index);
                    setting.inverted = index % 2 != 0;
                }
            OutputCalibrationProcessor processor;
            processor.setCalibration (speakers, settings);
            processor.prepare (48000.0);
            juce::AudioBuffer<float> block (speakers.size(), 1024);
            block.clear();
            for (int channel = 0; channel < speakers.size(); ++channel)
                block.setSample (channel, 0, 0.25f);
            {
                juce::UnitTestAllocationChecker allocations (*this);
                processor.process (block);
            }
            for (int channel = 0; channel < speakers.size(); ++channel)
                for (int sample = 0; sample < block.getNumSamples(); ++sample)
                    expectWithinAbsoluteError (block.getSample (channel, sample),
                        sample == channel * 48 ? (channel % 2 == 0 ? 0.125f : -0.5f) : 0.0f,
                        0.00001f);
        }

        beginTest ("fractional delay splits an impulse across the adjacent samples");
        {
            OutputCalibrationProcessor processor;
            processor.setCalibration (juce::AudioChannelSet::stereo(),
                { { juce::AudioChannelSet::left, 0.0f, 0.01, false } });
            processor.prepare (48000.0);
            juce::AudioBuffer<float> block (2, 256);
            block.clear();
            block.setSample (0, 0, 1.0f);
            processor.process (block);
            expectWithinAbsoluteError (block.getSample (0, 0), 0.52f, 0.00001f);
            expectWithinAbsoluteError (block.getSample (0, 1), 0.48f, 0.00001f);
            expectEquals (block.getMagnitude (0, 2, 254), 0.0f);
        }

        beginTest ("repeated test stop preserves maximum music delay across callback blocks without repeats");
        {
            OutputCalibrationProcessor processor;
            processor.setCalibration (juce::AudioChannelSet::stereo(),
                { { juce::AudioChannelSet::left, 0.0f, 200.0, false } });
            processor.prepare (48000.0);
            juce::AudioBuffer<float> block (2, 256);
            for (int callback = 0; callback < 80; ++callback)
            {
                block.clear();
                if (callback == 0) block.setSample (0, 0, 1.0f);
                {
                    juce::UnitTestAllocationChecker allocations (*this);
                    processor.stopTestSignal();
                    static_cast<void> (processor.renderTestSignal (block));
                    processor.process (block);
                }
                for (int sample = 0; sample < 256; ++sample)
                    expectEquals (block.getSample (0, sample), callback * 256 + sample == 9600 ? 1.0f : 0.0f);
            }
        }

        beginTest ("changing trim and polarity ramps through zero without a discontinuity");
        {
            OutputCalibrationProcessor processor;
            auto settings = defaultOutputCalibration();
            processor.setCalibration (juce::AudioChannelSet::stereo(), settings);
            processor.prepare (48000.0);
            for (auto& setting : settings)
                if (setting.channel == juce::AudioChannelSet::left) setting.inverted = true;
            processor.setCalibration (juce::AudioChannelSet::stereo(), settings);
            juce::AudioBuffer<float> block (2, 1024);
            for (int channel = 0; channel < 2; ++channel)
                std::fill_n (block.getWritePointer (channel), 1024, 0.25f);
            processor.process (block);
            expect (block.getSample (0, 0) > 0.24f);
            expectWithinAbsoluteError (block.getSample (0, 1023), -0.25f, 0.00001f);
            for (int sample = 1; sample < 1024; ++sample)
                expect (std::abs (block.getSample (0, sample) - block.getSample (0, sample - 1)) < 0.001f);
        }

        beginTest ("pink noise is bounded and isolates the selected speaker including LFE");
        {
            OutputCalibrationProcessor processor;
            processor.setCalibration (juce::AudioChannelSet::create5point1(), {});
            processor.prepare (48000.0);
            juce::AudioBuffer<float> block (6, 256);
            for (const auto speaker : { juce::AudioChannelSet::left, juce::AudioChannelSet::LFE })
            {
                processor.setTestSignal ({ CalibrationTestSignal::Kind::pinkNoise, speaker, -30.0f, 1000.0 });
                for (int callback = 0; callback < 10; ++callback)
                {
                    juce::UnitTestAllocationChecker allocations (*this);
                    expect (processor.renderTestSignal (block));
                    processor.process (block);
                }
                for (int channel = 0; channel < 6; ++channel)
                {
                    const auto selected = channel == (speaker == juce::AudioChannelSet::left ? 0 : 3);
                    const auto peak = block.getMagnitude (channel, 0, 256);
                    expect (selected ? peak > 0.0001f && peak < 0.032f : peak == 0.0f);
                }
            }
        }

        beginTest ("stopped test signal fades over resumed music without clearing its input");
        {
            OutputCalibrationProcessor processor;
            processor.prepare (48000.0);
            processor.setTestSignal ({ CalibrationTestSignal::Kind::sine,
                juce::AudioChannelSet::left, -30.0f, 1000.0 });
            juce::AudioBuffer<float> block (2, 512);
            for (int pass = 0; pass < 4; ++pass)
            {
                expect (processor.renderTestSignal (block));
                processor.process (block);
            }
            const auto lastTestSample = block.getSample (0, 511);
            processor.stopTestSignal();
            for (int channel = 0; channel < 2; ++channel)
                std::fill_n (block.getWritePointer (channel), 512, 0.1f);
            {
                juce::UnitTestAllocationChecker allocations (*this);
                static_cast<void> (processor.renderTestSignal (block));
                processor.process (block);
            }
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto fade = sample < 240 ? lastTestSample * static_cast<float> (240 - sample) / 240.0f : 0.0f;
                expectWithinAbsoluteError (block.getSample (0, sample), 0.1f + fade, 0.000001f);
                expectEquals (block.getSample (1, sample), 0.1f);
            }
        }

        beginTest ("test signal isolates one speaker and stopping discards delayed test history");
        {
            OutputCalibrationProcessor processor;
            auto settings = defaultOutputCalibration();
            for (auto& setting : settings)
                if (setting.channel == juce::AudioChannelSet::left) setting.delayMs = 200.0;
            processor.setCalibration (juce::AudioChannelSet::stereo(), settings);
            processor.prepare (48000.0);
            processor.setTestSignal ({ CalibrationTestSignal::Kind::sine, juce::AudioChannelSet::left, -30.0f, 1000.0 });
            juce::AudioBuffer<float> block (2, 256);
            for (int iteration = 0; iteration < 45; ++iteration)
            {
                block.clear();
                expect (processor.renderTestSignal (block));
                processor.process (block);
            }
            expect (block.getMagnitude (0, 0, 256) > 0.02f);
            expectEquals (block.getMagnitude (1, 0, 256), 0.0f);
            processor.stopTestSignal();
            for (int iteration = 0; iteration < 45; ++iteration)
            {
                block.clear();
                {
                    juce::UnitTestAllocationChecker allocations (*this);
                    static_cast<void> (processor.renderTestSignal (block));
                    processor.process (block);
                }
                expectEquals (block.getMagnitude (0, iteration == 0 ? 240 : 0, iteration == 0 ? 16 : 256), 0.0f);
            }
        }
    }
} outputCalibrationTests;
}
