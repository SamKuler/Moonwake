#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "audio/EffectGraph.h"

namespace
{
class TestEffect final : public juce::AudioProcessor
{
public:
    explicit TestEffect (float multiplierToUse, bool supportsSurroundToUse = false)
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          multiplier (multiplierToUse),
          supportsSurround (supportsSurroundToUse)
    {
    }

    const juce::String getName() const override { return "Test Effect"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (multiplier);
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
    void getStateInformation (juce::MemoryBlock& destination) override
    {
        destination.replaceAll (&multiplier, sizeof (multiplier));
    }
    void setStateInformation (const void* data, int size) override
    {
        if (size == static_cast<int> (sizeof (multiplier)))
            std::memcpy (&multiplier, data, sizeof (multiplier));
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
            && (supportsSurround
                || layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo());
    }

private:
    float multiplier = 1.0f;
    bool supportsSurround = false;
};

class EffectGraphTests final : public juce::UnitTest
{
public:
    EffectGraphTests()
        : UnitTest ("EffectGraph", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("processes, bypasses, moves, and removes stable slots");
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::stereo());
            juce::String error;
            expect (graph.addProcessor (10, std::make_unique<TestEffect> (2.0f), error), error);
            expect (graph.addProcessor (20, std::make_unique<TestEffect> (0.5f), error), error);
            expectEquals (graph.size(), 2);
            expectEquals (graph.getSlotIdAtIndex (0), 10);
            expect (graph.getProcessor (20) != nullptr);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            buffer.addFrom (0, 0, std::vector<float> (64, 1.0f).data(), 64);
            buffer.addFrom (1, 0, std::vector<float> (64, 1.0f).data(), 64);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 0.0001f);

            expect (graph.setBypassed (10, true));
            buffer.clear();
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample (channel, sample, 1.0f);
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 0.0001f);

            expect (graph.moveProcessor (1, 0));
            expectEquals (graph.getSlotIdAtIndex (0), 20);
            expect (graph.setProcessorOrder ({ 10, 20 }));
            expectEquals (graph.getSlotIdAtIndex (0), 10);
            expect (! graph.setProcessorOrder ({ 20, 20 }));
            expect (graph.removeProcessor (10));
            expectEquals (graph.size(), 1);
            expect (graph.getProcessor (10) == nullptr);
        }

        beginTest ("clears the complete effect chain in one graph update");
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::stereo());
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (2.0f), error), error);
            expect (graph.addProcessor (2, std::make_unique<TestEffect> (3.0f), error), error);
            expect (graph.clearProcessors());
            expectEquals (graph.size(), 0);
            expect (graph.clearProcessors());

            juce::AudioBuffer<float> buffer (2, 64);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample (channel, sample, 1.0f);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 0.0001f);
        }

        beginTest ("state capture bypasses effects without blocking the audio path");
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::stereo());
            juce::String error;
            expect (graph.addProcessor (
                1, std::make_unique<TestEffect> (2.0f), error), error);
            expect (graph.beginStateCapture());

            juce::AudioBuffer<float> buffer (2, 64);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample (channel, sample, 1.0f);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 0.0001f);

            graph.endStateCapture();
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 2.0f, 0.0001f);
        }

        beginTest ("rejects a stereo-only effect in an active 5.1 graph");
        {
            EffectGraph graph;
            juce::String error;
            expect (graph.setChannelLayout (juce::AudioChannelSet::create5point1(), error), error);
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::create5point1());
            expect (! graph.addProcessor (1, std::make_unique<TestEffect> (1.0f), error));
            expect (error.containsIgnoreCase ("5.1") || error.containsIgnoreCase ("6-channel"));
            expectEquals (graph.size(), 0);
        }

        beginTest ("processes every channel through a compatible 5.1 effect");
        {
            EffectGraph graph;
            juce::String error;
            const auto layout = juce::AudioChannelSet::create5point1();
            expect (graph.setChannelLayout (layout, error), error);
            graph.prepareToPlay (48000.0, 64, layout);
            expect (graph.addProcessor (7, std::make_unique<TestEffect> (2.0f, true), error), error);

            juce::AudioBuffer<float> buffer (layout.size(), 64);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    buffer.setSample (channel, sample, static_cast<float> (channel + 1));
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0),
                                           static_cast<float> ((channel + 1) * 2),
                                           0.0001f);
        }
    }
};

EffectGraphTests effectGraphTests;
} // namespace
