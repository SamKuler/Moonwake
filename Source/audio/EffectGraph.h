#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

class EffectGraph
{
public:
    EffectGraph();

    void prepareToPlay (double sampleRate,
                        int blockSize,
                        const juce::AudioChannelSet& channelLayout);
    void releaseResources();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    bool addProcessor (int slotId,
                       std::unique_ptr<juce::AudioProcessor> processor,
                       juce::String& errorMessage);
    bool removeProcessor (int slotId);
    bool moveProcessor (int fromIndex, int toIndex);
    bool setProcessorOrder (const std::vector<int>& orderedSlotIds);
    bool setBypassed (int slotId, bool shouldBypass);
    bool setChannelLayout (const juce::AudioChannelSet& channelLayout, juce::String& errorMessage);

    [[nodiscard]] int size() const;
    [[nodiscard]] int getSlotIdAtIndex (int index) const;
    [[nodiscard]] juce::AudioProcessor* getProcessor (int slotId) const;

private:
    struct SlotNode
    {
        int slotId = 0;
        juce::AudioProcessorGraph::Node::Ptr node;
    };

    bool rebuildConnections();
    bool configureProcessor (juce::AudioProcessor& processor, juce::String& errorMessage) const;
    bool createProcessorLayout (const juce::AudioProcessor& processor,
                                const juce::AudioChannelSet& channelLayout,
                                juce::AudioProcessor::BusesLayout& busesLayout) const;
    [[nodiscard]] std::vector<SlotNode>::iterator findSlot (int slotId);
    [[nodiscard]] std::vector<SlotNode>::const_iterator findSlot (int slotId) const;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    std::vector<SlotNode> slots;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    juce::AudioChannelSet currentChannelLayout = juce::AudioChannelSet::stereo();
    bool prepared = false;
};
