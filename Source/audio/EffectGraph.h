#pragma once

#include "EffectScope.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

class EffectGraph : private juce::AudioProcessorListener,
                    private juce::Timer
{
public:
    EffectGraph();
    ~EffectGraph() override;

    void prepareToPlay (double sampleRate,
                        int blockSize,
                        const juce::AudioChannelSet& channelLayout);
    void releaseResources();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    bool beginStateCapture (int timeoutMilliseconds = 1000);
    void endStateCapture() noexcept;

    bool addProcessor (int slotId,
                       std::unique_ptr<juce::AudioProcessor> processor,
                       juce::String& errorMessage,
                       EffectScope scope = {});
    bool removeProcessor (int slotId);
    bool clearProcessors();
    bool moveProcessor (int fromIndex, int toIndex);
    bool setProcessorOrder (const std::vector<int>& orderedSlotIds);
    bool setBypassed (int slotId, bool shouldBypass);
    bool setProcessorScope (int slotId, const EffectScope&, juce::String& errorMessage);
    bool setChannelLayout (const juce::AudioChannelSet& channelLayout, juce::String& errorMessage);

    [[nodiscard]] int size() const;
    [[nodiscard]] int getSlotIdAtIndex (int index) const;
    [[nodiscard]] juce::AudioProcessor* getProcessor (int slotId) const;
    [[nodiscard]] const EffectScope* getProcessorScope (int slotId) const;

private:
    struct SlotNode
    {
        int slotId = 0;
        juce::AudioProcessorGraph::Node::Ptr node;
        EffectScope scope;
    };

    bool rebuildConnections();
    bool configureProcessor (juce::AudioProcessor& processor, const EffectScope&,
                             juce::String& errorMessage) const;
    bool createProcessorLayout (const juce::AudioProcessor& processor,
                                const juce::AudioChannelSet& channelLayout,
                                const EffectScope&,
                                juce::AudioProcessor::BusesLayout& busesLayout) const;
    [[nodiscard]] std::vector<SlotNode>::iterator findSlot (int slotId);
    [[nodiscard]] std::vector<SlotNode>::const_iterator findSlot (int slotId) const;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*,
                                const juce::AudioProcessorListener::ChangeDetails&) override;
    void timerCallback() override;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    std::vector<SlotNode> slots;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    juce::AudioChannelSet currentChannelLayout = juce::AudioChannelSet::stereo();
    bool prepared = false;
    std::atomic_bool stateCaptureRequested { false };
    std::atomic<int> activeProcessCallbacks { 0 };
    std::atomic_bool latencyChanged { false };
};
