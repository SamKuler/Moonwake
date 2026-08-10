#include "EffectGraph.h"

#include <algorithm>

EffectGraph::EffectGraph()
{
    graph.setPlayConfigDetails (currentChannelLayout.size(),
                                currentChannelLayout.size(),
                                currentSampleRate,
                                currentBlockSize);
    inputNode = graph.addNode (
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
        std::nullopt,
        juce::AudioProcessorGraph::UpdateKind::none);
    outputNode = graph.addNode (
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
        std::nullopt,
        juce::AudioProcessorGraph::UpdateKind::none);
    static_cast<void> (rebuildConnections());
}

void EffectGraph::prepareToPlay (double sampleRate,
                                 int blockSize,
                                 const juce::AudioChannelSet& channelLayout)
{
    if (prepared)
        graph.releaseResources();

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    juce::String ignoredError;
    static_cast<void> (setChannelLayout (channelLayout, ignoredError));
    graph.setPlayConfigDetails (currentChannelLayout.size(),
                                currentChannelLayout.size(),
                                currentSampleRate,
                                currentBlockSize);

    for (auto& slot : slots)
    {
        juce::String processorError;
        static_cast<void> (configureProcessor (*slot.node->getProcessor(), processorError));
    }

    static_cast<void> (rebuildConnections());
    graph.prepareToPlay (currentSampleRate, currentBlockSize);
    prepared = true;
}

void EffectGraph::releaseResources()
{
    if (prepared)
        graph.releaseResources();

    prepared = false;
}

void EffectGraph::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (stateCaptureRequested.load (std::memory_order_acquire))
        return;

    activeProcessCallbacks.fetch_add (1, std::memory_order_acq_rel);
    if (stateCaptureRequested.load (std::memory_order_acquire))
    {
        activeProcessCallbacks.fetch_sub (1, std::memory_order_release);
        return;
    }

    struct ActiveCallbackScope
    {
        std::atomic<int>& count;
        ~ActiveCallbackScope()
        {
            count.fetch_sub (1, std::memory_order_release);
        }
    } active { activeProcessCallbacks };
    graph.processBlock (buffer, midi);
}

bool EffectGraph::beginStateCapture (int timeoutMilliseconds)
{
    bool expected = false;
    if (! stateCaptureRequested.compare_exchange_strong (
            expected, true, std::memory_order_acq_rel))
        return false;

    const auto startedAt = juce::Time::getMillisecondCounterHiRes();
    while (activeProcessCallbacks.load (std::memory_order_acquire) != 0)
    {
        if (juce::Time::getMillisecondCounterHiRes() - startedAt
            >= juce::jmax (1, timeoutMilliseconds))
        {
            stateCaptureRequested.store (false, std::memory_order_release);
            return false;
        }
        juce::Thread::sleep (1);
    }
    return true;
}

void EffectGraph::endStateCapture() noexcept
{
    stateCaptureRequested.store (false, std::memory_order_release);
}

bool EffectGraph::addProcessor (int slotId,
                                std::unique_ptr<juce::AudioProcessor> processor,
                                juce::String& errorMessage)
{
    errorMessage.clear();
    if (slotId <= 0 || processor == nullptr || findSlot (slotId) != slots.end())
    {
        errorMessage = "Invalid or duplicate effect slot.";
        return false;
    }

    if (! configureProcessor (*processor, errorMessage))
        return false;

    auto node = graph.addNode (std::move (processor),
                               std::nullopt,
                               juce::AudioProcessorGraph::UpdateKind::none);
    if (node == nullptr)
    {
        errorMessage = "Could not add the effect processor to the audio graph.";
        return false;
    }

    slots.push_back ({ slotId, node });
    if (rebuildConnections())
        return true;

    slots.pop_back();
    graph.removeNode (node->nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    static_cast<void> (rebuildConnections());
    errorMessage = "The effect does not expose a compatible " + juce::String (currentChannelLayout.size())
        + "-channel audio path.";
    return false;
}

bool EffectGraph::removeProcessor (int slotId)
{
    const auto position = findSlot (slotId);
    if (position == slots.end())
        return false;

    const auto index = std::distance (slots.begin(), position);
    auto removedSlot = *position;
    slots.erase (position);
    if (! rebuildConnections())
    {
        slots.insert (slots.begin() + index,
                      std::move (removedSlot));
        static_cast<void> (rebuildConnections());
        return false;
    }

    graph.removeNode (removedSlot.node->nodeID,
                      juce::AudioProcessorGraph::UpdateKind::none);
    graph.rebuild();
    return true;
}

bool EffectGraph::clearProcessors()
{
    if (slots.empty())
        return true;

    auto removedSlots = slots;
    slots.clear();
    if (! rebuildConnections())
    {
        slots = std::move (removedSlots);
        static_cast<void> (rebuildConnections());
        return false;
    }

    for (const auto& removed : removedSlots)
        graph.removeNode (removed.node->nodeID,
                          juce::AudioProcessorGraph::UpdateKind::none);
    graph.rebuild();
    return true;
}

bool EffectGraph::moveProcessor (int fromIndex, int toIndex)
{
    if (! juce::isPositiveAndBelow (fromIndex, size())
        || ! juce::isPositiveAndBelow (toIndex, size()))
        return false;

    if (fromIndex == toIndex)
        return true;

    auto previousSlots = slots;
    auto slot = slots[static_cast<size_t> (fromIndex)];
    slots.erase (slots.begin() + fromIndex);
    slots.insert (slots.begin() + toIndex, std::move (slot));
    if (rebuildConnections())
        return true;

    slots = std::move (previousSlots);
    static_cast<void> (rebuildConnections());
    return false;
}

bool EffectGraph::setProcessorOrder (const std::vector<int>& orderedSlotIds)
{
    if (orderedSlotIds.size() != slots.size())
        return false;

    std::vector<SlotNode> reordered;
    reordered.reserve (slots.size());
    for (const auto slotId : orderedSlotIds)
    {
        const auto position = findSlot (slotId);
        if (position == slots.end()
            || std::find_if (reordered.begin(), reordered.end(), [slotId] (const auto& slot)
            {
                return slot.slotId == slotId;
            }) != reordered.end())
            return false;

        reordered.push_back (*position);
    }

    auto previousSlots = slots;
    slots = std::move (reordered);
    if (rebuildConnections())
        return true;

    slots = std::move (previousSlots);
    static_cast<void> (rebuildConnections());
    return false;
}

bool EffectGraph::setBypassed (int slotId, bool shouldBypass)
{
    const auto position = findSlot (slotId);
    if (position == slots.end())
        return false;

    position->node->setBypassed (shouldBypass);
    return true;
}

bool EffectGraph::setChannelLayout (const juce::AudioChannelSet& channelLayout,
                                    juce::String& errorMessage)
{
    errorMessage.clear();
    if (channelLayout.isDisabled() || channelLayout.size() <= 0)
    {
        errorMessage = "The requested output layout is empty.";
        return false;
    }

    std::vector<juce::AudioProcessor::BusesLayout> requestedLayouts;
    requestedLayouts.reserve (slots.size());
    for (const auto& slot : slots)
    {
        juce::AudioProcessor::BusesLayout requested;
        if (! createProcessorLayout (*slot.node->getProcessor(), channelLayout, requested)
            || ! slot.node->getProcessor()->checkBusesLayoutSupported (requested))
        {
            errorMessage = slot.node->getProcessor()->getName() + " does not support "
                + channelLayout.getDescription() + " (" + juce::String (channelLayout.size())
                + "-channel) processing.";
            return false;
        }

        requestedLayouts.push_back (std::move (requested));
    }

    const auto previousLayout = currentChannelLayout;
    std::vector<juce::AudioProcessor::BusesLayout> previousProcessorLayouts;
    previousProcessorLayouts.reserve (slots.size());
    for (const auto& slot : slots)
        previousProcessorLayouts.push_back (slot.node->getProcessor()->getBusesLayout());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].node->getProcessor()->setBusesLayout (requestedLayouts[i]))
            continue;

        for (size_t rollback = 0; rollback < i; ++rollback)
            static_cast<void> (slots[rollback].node->getProcessor()->setBusesLayout (
                previousProcessorLayouts[rollback]));

        errorMessage = slots[i].node->getProcessor()->getName()
            + " rejected the requested output layout.";
        return false;
    }

    currentChannelLayout = channelLayout;
    graph.setPlayConfigDetails (currentChannelLayout.size(),
                                currentChannelLayout.size(),
                                currentSampleRate,
                                currentBlockSize);
    static_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*> (inputNode->getProcessor())
        ->setParentGraph (&graph);
    static_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*> (outputNode->getProcessor())
        ->setParentGraph (&graph);
    if (! rebuildConnections())
    {
        for (size_t i = 0; i < slots.size(); ++i)
            static_cast<void> (slots[i].node->getProcessor()->setBusesLayout (previousProcessorLayouts[i]));
        currentChannelLayout = previousLayout;
        graph.setPlayConfigDetails (currentChannelLayout.size(),
                                    currentChannelLayout.size(),
                                    currentSampleRate,
                                    currentBlockSize);
        static_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*> (inputNode->getProcessor())
            ->setParentGraph (&graph);
        static_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*> (outputNode->getProcessor())
            ->setParentGraph (&graph);
        static_cast<void> (rebuildConnections());
        errorMessage = "Could not connect all channels in the requested effect graph.";
        return false;
    }

    return true;
}

int EffectGraph::size() const
{
    return static_cast<int> (slots.size());
}

int EffectGraph::getSlotIdAtIndex (int index) const
{
    return juce::isPositiveAndBelow (index, size()) ? slots[static_cast<size_t> (index)].slotId : 0;
}

juce::AudioProcessor* EffectGraph::getProcessor (int slotId) const
{
    const auto position = findSlot (slotId);
    return position == slots.end() ? nullptr : position->node->getProcessor();
}

bool EffectGraph::rebuildConnections()
{
    for (const auto& connection : graph.getConnections())
        graph.removeConnection (connection, juce::AudioProcessorGraph::UpdateKind::none);

    auto sourceNodeId = inputNode->nodeID;
    for (const auto& slot : slots)
    {
        for (int channel = 0; channel < currentChannelLayout.size(); ++channel)
            if (! graph.addConnection ({ { sourceNodeId, channel }, { slot.node->nodeID, channel } },
                                       juce::AudioProcessorGraph::UpdateKind::none))
                return false;

        sourceNodeId = slot.node->nodeID;
    }

    for (int channel = 0; channel < currentChannelLayout.size(); ++channel)
        if (! graph.addConnection ({ { sourceNodeId, channel }, { outputNode->nodeID, channel } },
                                   juce::AudioProcessorGraph::UpdateKind::none))
            return false;

    graph.rebuild();
    return true;
}

bool EffectGraph::configureProcessor (juce::AudioProcessor& processor, juce::String& errorMessage) const
{
    juce::AudioProcessor::BusesLayout layout;
    if (! createProcessorLayout (processor, currentChannelLayout, layout)
        || ! processor.checkBusesLayoutSupported (layout)
        || ! processor.setBusesLayout (layout))
    {
        errorMessage = processor.getName() + " cannot process the active "
            + currentChannelLayout.getDescription() + " ("
            + juce::String (currentChannelLayout.size()) + "-channel) layout.";
        return false;
    }

    processor.setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
    return true;
}

bool EffectGraph::createProcessorLayout (const juce::AudioProcessor& processor,
                                         const juce::AudioChannelSet& channelLayout,
                                         juce::AudioProcessor::BusesLayout& busesLayout) const
{
    busesLayout = processor.getBusesLayout();
    if (busesLayout.inputBuses.isEmpty() || busesLayout.outputBuses.isEmpty())
        return false;

    for (auto& bus : busesLayout.inputBuses)
        bus = juce::AudioChannelSet::disabled();
    for (auto& bus : busesLayout.outputBuses)
        bus = juce::AudioChannelSet::disabled();

    busesLayout.inputBuses.getReference (0) = channelLayout;
    busesLayout.outputBuses.getReference (0) = channelLayout;
    return true;
}

std::vector<EffectGraph::SlotNode>::iterator EffectGraph::findSlot (int slotId)
{
    return std::find_if (slots.begin(), slots.end(), [slotId] (const auto& slot)
    {
        return slot.slotId == slotId;
    });
}

std::vector<EffectGraph::SlotNode>::const_iterator EffectGraph::findSlot (int slotId) const
{
    return std::find_if (slots.cbegin(), slots.cend(), [slotId] (const auto& slot)
    {
        return slot.slotId == slotId;
    });
}
