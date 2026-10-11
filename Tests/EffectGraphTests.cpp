#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "audio/EffectGraph.h"

#include <thread>
#include <stdexcept>

namespace
{
class TestEffect final : public juce::AudioProcessor
{
public:
    explicit TestEffect (float multiplierToUse, bool supportsSurroundToUse = false,
                         float offsetToUse = 0.0f, bool distinguishChannelsToUse = false,
                         int latencyToUse = 0)
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          multiplier (multiplierToUse),
          supportsSurround (supportsSurroundToUse), offset (offsetToUse),
          distinguishChannels (distinguishChannelsToUse), latency (latencyToUse)
    {
        setLatencySamples (latency);
    }

    const juce::String getName() const override { return "Test Effect"; }
    void prepareToPlay (double, int) override
    {
        if (throwAllPreparations || throwPreparingChannels == getTotalNumInputChannels())
            throw std::runtime_error ("fixture preparation failure");
        preparedChannels = getTotalNumInputChannels();
        delay.setSize (preparedChannels, juce::jmax (1, latency));
        delay.clear();
        delayPosition = 0;
    }
    void releaseResources() override {}
    void reset() override { delay.clear(); delayPosition = 0; }
    void changeLatency (int samples)
    {
        latency = samples;
        prepareToPlay (0.0, 0);
        setLatencySamples (samples);
    }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        processingLayoutMatched = processingLayoutMatched && preparedChannels == getTotalNumInputChannels();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto input = buffer.getSample (channel, sample)
                    * multiplier * (distinguishChannels ? static_cast<float> (channel + 1) : 1.0f) + offset;
                if (latency == 0)
                    buffer.setSample (channel, sample, input);
                else
                {
                    const auto position = (delayPosition + sample) % latency;
                    buffer.setSample (channel, sample, delay.getSample (channel, position));
                    delay.setSample (channel, position, input);
                }
            }
        if (latency != 0)
            delayPosition = (delayPosition + buffer.getNumSamples()) % latency;
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

    bool canApplyBusesLayout (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet().size() != rejectedChannelCount
            && isBusesLayoutSupported (layouts);
    }
    int rejectedChannelCount = 0;
    int throwPreparingChannels = 0;
    bool throwAllPreparations = false;
    bool processingLayoutMatched = true;

private:
    float multiplier = 1.0f;
    bool supportsSurround = false;
    float offset = 0.0f;
    bool distinguishChannels = false;
    int preparedChannels = 0;
    int latency = 0;
    int delayPosition = 0;
    juce::AudioBuffer<float> delay;
};

juce::AudioBuffer<float> signal (int channels)
{
    juce::AudioBuffer<float> buffer (channels, 64);
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < 64; ++sample)
            buffer.setSample (channel, sample, static_cast<float> (channel + 1));
    return buffer;
}

class EffectGraphTests final : public juce::UnitTest
{
public:
    EffectGraphTests()
        : UnitTest ("EffectGraph", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("failed plug-in preparation rolls back insertion and preserves the live chain");
        for (const auto& layout : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::create5point1(),
                                  juce::AudioChannelSet::create5point1point2(), juce::AudioChannelSet::create7point1point2() })
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, layout);
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (2.0f, true), error), error);
            auto failing = std::make_unique<TestEffect> (0.5f, true);
            failing->throwPreparingChannels = layout.size();
            expect (! graph.addProcessor (2, std::move (failing), error));
            expect (error.isNotEmpty());
            expectEquals (graph.size(), 1);
            expect (graph.getProcessor (2) == nullptr);
            juce::MidiBuffer midi;
            auto buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0), 2.0f * (channel + 1), 0.0001f);
            expect (graph.addProcessor (2, std::make_unique<TestEffect> (0.5f, true), error), error);
            expect (graph.moveProcessor (0, 1));
            expect (graph.setProcessorOrder ({ 1, 2 }));
            expect (graph.removeProcessor (2));
            expect (graph.clearProcessors());
        }

        beginTest ("failed scope and output preparation restore processor layout and routing");
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::stereo());
            auto effect = std::make_unique<TestEffect> (2.0f, true);
            auto* live = effect.get();
            juce::String error;
            expect (graph.addProcessor (1, std::move (effect), error), error);
            live->throwPreparingChannels = 1;
            expect (! graph.setProcessorScope (1, { juce::AudioChannelSet::left }, error));
            expect (error.isNotEmpty());
            expect (graph.getProcessorScope (1)->empty());
            expectEquals (live->getTotalNumInputChannels(), 2);
            live->throwPreparingChannels = 6;
            expect (! graph.setChannelLayout (juce::AudioChannelSet::create5point1(), error));
            expect (error.isNotEmpty());
            expectEquals (live->getTotalNumInputChannels(), 2);
            auto buffer = signal (2);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 2.0f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 0), 4.0f, 0.0001f);
            expect (live->processingLayoutMatched);
        }

        beginTest ("a failed scope rollback suspends processing until preparation recovers");
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::stereo());
            auto effect = std::make_unique<TestEffect> (2.0f, true);
            auto* live = effect.get();
            juce::String error;
            expect (graph.addProcessor (1, std::move (effect), error), error);
            live->throwAllPreparations = true;
            expect (! graph.setProcessorScope (1, { juce::AudioChannelSet::left }, error));
            expect (error.contains ("suspended"));
            expect (graph.getProcessorScope (1)->empty());
            auto buffer = signal (2);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 0.0001f);
            live->throwAllPreparations = false;
            expect (graph.clearTails (error), error);
            buffer = signal (2);
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 2.0f, 0.0001f);
        }

        beginTest ("failed reprepare suspends processing until the device configuration recovers");
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::stereo());
            auto effect = std::make_unique<TestEffect> (2.0f, true);
            auto* live = effect.get();
            juce::String error;
            expect (graph.addProcessor (1, std::move (effect), error), error);
            live->throwPreparingChannels = 2;
            bool threw = false;
            try { graph.prepareToPlay (44100.0, 64, juce::AudioChannelSet::stereo()); }
            catch (...) { threw = true; }
            expect (threw);
            auto buffer = signal (2);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 0.0001f);
            live->throwPreparingChannels = 0;
            graph.prepareToPlay (44100.0, 64, juce::AudioChannelSet::stereo());
            buffer = signal (2);
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 2.0f, 0.0001f);
        }

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

        beginTest ("clearing tails resets scoped effects and compensation buffers without changing bypass or parameters");
        for (const auto& layout : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::create5point1(),
                                   juce::AudioChannelSet::create5point1point2(), juce::AudioChannelSet::create7point1point2() })
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, layout);
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (2.0f, true, 0.0f, false, 128),
                                        error, { juce::AudioChannelSet::left }), error);
            expect (graph.addProcessor (2, std::make_unique<TestEffect> (3.0f, true), error), error);
            expect (graph.setBypassed (2, true));
            juce::MemoryBlock before;
            graph.getProcessor (1)->getStateInformation (before);
            juce::MidiBuffer midi;
            auto block = signal (layout.size());
            for (int i = 0; i < 3; ++i) { block = signal (layout.size()); graph.processBlock (block, midi); }
            block.clear();
            graph.processBlock (block, midi);
            for (int channel = 0; channel < layout.size(); ++channel)
                expect (block.getMagnitude (channel, 0, 64) > 0.0f, "The fixture contains buffered audio before reset.");
            expect (graph.clearTails (error), error);
            for (int i = 0; i < 4; ++i)
            {
                block.clear(); graph.processBlock (block, midi);
                expectEquals (block.getMagnitude (0, 64), 0.0f);
            }
            juce::MemoryBlock after;
            graph.getProcessor (1)->getStateInformation (after);
            expect (before == after);
            expectEquals (graph.size(), 2);
            expect (*graph.getProcessorScope (1) == EffectScope { juce::AudioChannelSet::left });
            for (int i = 0; i < 3; ++i) { block = signal (layout.size()); graph.processBlock (block, midi); }
            for (int channel = 0; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (block.getSample (channel, 0),
                    static_cast<float> (channel + 1) * (channel == 0 ? 2.0f : 1.0f), 0.0001f);
            expect (graph.beginStateCapture());
            expect (! graph.clearTails (error));
            expect (error.isNotEmpty());
            graph.endStateCapture();
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

        beginTest ("explicit stereo scopes preserve ordered channels and all other channels through 16 outputs");
        for (const auto& layout : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::create5point1(),
                                  juce::AudioChannelSet::create5point1point2(), juce::AudioChannelSet::create7point1point2(),
                                  juce::AudioChannelSet::create9point1point6() })
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, layout);
            const EffectScope scope { layout.getTypeOfChannel (layout.size() - 1), layout.getTypeOfChannel (0) };
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (2.0f, false, 0.0f, true), error, scope), error);
            expect (graph.getProcessorScope (1) && *graph.getProcessorScope (1) == scope);
            auto* plugin = graph.getProcessor (1);
            expect (plugin && plugin->getBusesLayout().getMainInputChannelSet() == juce::AudioChannelSet::stereo());
            auto buffer = signal (layout.size());
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0), static_cast<float> (channel + 1)
                    * (channel == 0 ? 4.0f : channel == layout.size() - 1 ? 2.0f : 1.0f), 0.0001f);
            expect (graph.setBypassed (1, true));
            buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0), static_cast<float> (channel + 1), 0.0001f);
        }

        beginTest ("overlapping mono and stereo scopes retain chain order and complete-bus processing");
        {
            EffectGraph graph;
            const auto layout = juce::AudioChannelSet::create7point1point2();
            graph.prepareToPlay (48000.0, 64, layout);
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (2.0f, false, 1.0f), error,
                                        { juce::AudioChannelSet::left, juce::AudioChannelSet::right }), error);
            expect (graph.addProcessor (2, std::make_unique<TestEffect> (3.0f, true), error), error);
            expect (graph.addProcessor (3, std::make_unique<TestEffect> (0.5f, true), error,
                                        { juce::AudioChannelSet::right }), error);
            expectEquals (graph.getProcessor (3)->getTotalNumInputChannels(), 1);
            juce::MidiBuffer midi;
            auto buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 9.0f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 0), 7.5f, 0.0001f);
            for (int channel = 2; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0), static_cast<float> ((channel + 1) * 3), 0.0001f);
            expect (graph.moveProcessor (1, 0));
            buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 7.0f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 0), 6.5f, 0.0001f);
            expect (graph.removeProcessor (1));
            buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 3.0f, 0.0001f);
            expectWithinAbsoluteError (buffer.getSample (1, 0), 3.0f, 0.0001f);
        }

        beginTest ("scope changes reprepare native buses and rejected scopes leave routing intact");
        {
            EffectGraph graph;
            const auto layout = juce::AudioChannelSet::create5point1();
            graph.prepareToPlay (48000.0, 64, layout);
            juce::String error;
            auto plugin = std::make_unique<TestEffect> (2.0f, true);
            auto* effect = plugin.get();
            expect (graph.addProcessor (1, std::move (plugin), error), error);
            const EffectScope centre { juce::AudioChannelSet::centre };
            expect (graph.setProcessorScope (1, centre, error), error);
            expectEquals (effect->getTotalNumInputChannels(), 1);
            auto buffer = signal (layout.size());
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            expect (effect->processingLayoutMatched);
            for (int channel = 0; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0), static_cast<float> (channel + 1)
                    * (layout.getTypeOfChannel (channel) == juce::AudioChannelSet::centre ? 2.0f : 1.0f), 0.0001f);
            effect->rejectedChannelCount = 2; // Check succeeds; applying the new bus layout fails.
            for (const EffectScope& bad : { EffectScope { juce::AudioChannelSet::left, juce::AudioChannelSet::right },
                                          EffectScope { juce::AudioChannelSet::left, juce::AudioChannelSet::left },
                                          EffectScope { juce::AudioChannelSet::topFrontLeft },
                                          EffectScope { juce::AudioChannelSet::left, juce::AudioChannelSet::right, juce::AudioChannelSet::centre } })
            {
                expect (! graph.setProcessorScope (1, bad, error));
                expect (error.isNotEmpty());
                expect (*graph.getProcessorScope (1) == centre);
                expectEquals (effect->getTotalNumInputChannels(), 1);
            }
            buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (2, 0), 6.0f, 0.0001f);
            expect (effect->processingLayoutMatched);
            expect (! graph.setChannelLayout (juce::AudioChannelSet::stereo(), error));
            expect (*graph.getProcessorScope (1) == centre);
            effect->rejectedChannelCount = 0;
            expect (graph.setProcessorScope (1, {}, error), error);
            expectEquals (effect->getTotalNumInputChannels(), 6);
            buffer = signal (layout.size());
            graph.processBlock (buffer, midi);
            expect (effect->processingLayoutMatched);
            for (int channel = 0; channel < layout.size(); ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 0), static_cast<float> ((channel + 1) * 2), 0.0001f);
        }

        beginTest ("layout changes retain semantic channel identity and roll back all buses on rejection");
        {
            EffectGraph graph;
            const auto original = juce::AudioChannelSet::create7point1point2();
            graph.prepareToPlay (48000.0, 64, original);
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (2.0f, true), error), error);
            auto refusing = std::make_unique<TestEffect> (3.0f, true);
            auto* refusingPointer = refusing.get();
            expect (graph.addProcessor (2, std::move (refusing), error), error);
            const EffectScope sides { juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::rightSurroundSide };
            expect (graph.addProcessor (3, std::make_unique<TestEffect> (0.5f), error, sides), error);
            refusingPointer->rejectedChannelCount = 16;
            expect (! graph.setChannelLayout (juce::AudioChannelSet::create9point1point6(), error));
            expectEquals (graph.getProcessor (1)->getTotalNumInputChannels(), 10);
            expectEquals (graph.getProcessor (2)->getTotalNumInputChannels(), 10);
            refusingPointer->rejectedChannelCount = 0;
            const auto expanded = juce::AudioChannelSet::create9point1point6();
            expect (graph.setChannelLayout (expanded, error), error);
            auto buffer = signal (expanded.size());
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < expanded.size(); ++channel)
            {
                const auto type = expanded.getTypeOfChannel (channel);
                const auto scoped = type == sides[0] || type == sides[1];
                expectWithinAbsoluteError (buffer.getSample (channel, 0), static_cast<float> (channel + 1)
                    * (scoped ? 3.0f : 6.0f), 0.0001f);
            }
            expect (refusingPointer->processingLayoutMatched);
        }

        beginTest ("JUCE delay compensation aligns scoped processors and untouched channels across 16 outputs");
        {
            EffectGraph graph;
            const auto layout = juce::AudioChannelSet::create9point1point6();
            graph.prepareToPlay (48000.0, 64, layout);
            juce::String error;
            expect (graph.addProcessor (1, std::make_unique<TestEffect> (1.0f, false, 0.0f, false, 7), error,
                                        { layout.getTypeOfChannel (15), layout.getTypeOfChannel (0) }), error);
            const auto expectAlignedImpulse = [&] (int latency)
            {
                juce::AudioBuffer<float> buffer (layout.size(), 64);
                buffer.clear();
                for (int channel = 0; channel < layout.size(); ++channel)
                    buffer.setSample (channel, 0, static_cast<float> (channel + 1));
                juce::MidiBuffer midi;
                graph.processBlock (buffer, midi);
                for (int channel = 0; channel < layout.size(); ++channel)
                    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                        expectWithinAbsoluteError (buffer.getSample (channel, sample),
                            sample == latency ? static_cast<float> (channel + 1) : 0.0f, 0.0001f,
                            "channel " + juce::String (channel) + " sample " + juce::String (sample));
            };
            expectAlignedImpulse (7);
            expect (graph.addProcessor (2, std::make_unique<TestEffect> (1.0f, true, 0.0f, false, 11), error,
                                        { layout.getTypeOfChannel (2) }), error);
            expectAlignedImpulse (11);
            expect (graph.addProcessor (3, std::make_unique<TestEffect> (1.0f, true, 0.0f, false, 5), error), error);
            expectAlignedImpulse (16);
            expect (graph.setProcessorScope (1, { layout.getTypeOfChannel (2), layout.getTypeOfChannel (3) }, error), error);
            expectAlignedImpulse (23);
        }

        beginTest ("latency notifications rebuild scoped compensation on the message thread");
        for (const auto& layout : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::create5point1(),
                                  juce::AudioChannelSet::create5point1point2(), juce::AudioChannelSet::create7point1point2(),
                                  juce::AudioChannelSet::create9point1point6() })
        {
            EffectGraph graph;
            graph.prepareToPlay (48000.0, 64, layout);
            auto plugin = std::make_unique<TestEffect> (1.0f, true, 0.0f, false, 7);
            auto* effect = plugin.get();
            juce::String error;
            expect (graph.addProcessor (1, std::move (plugin), error,
                                        { layout.getTypeOfChannel (0) }), error);
            std::thread change ([&] { effect->changeLatency (11); });
            change.join();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            juce::AudioBuffer<float> buffer (layout.size(), 64);
            buffer.clear();
            for (int channel = 0; channel < layout.size(); ++channel)
                buffer.setSample (channel, 0, 1.0f);
            juce::MidiBuffer midi;
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < layout.size(); ++channel)
                for (int sample = 0; sample < 64; ++sample)
                    expectWithinAbsoluteError (buffer.getSample (channel, sample),
                        sample == 11 ? 1.0f : 0.0f, 0.0001f);
            expect (graph.removeProcessor (1));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
    }
};

EffectGraphTests effectGraphTests;
} // namespace
