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
    startTimerHz (30);
}

EffectGraph::~EffectGraph()
{
    stopTimer();
    for (const auto& slot : slots)
        slot.node->getProcessor()->removeListener (this);
}

void EffectGraph::audioProcessorChanged (
    juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails& details)
{
    if (details.latencyChanged)
        latencyChanged.store (true, std::memory_order_release);
}

void EffectGraph::timerCallback()
{
    // Plug-ins may notify from their audio callback. Rebuild only on the message thread.
    if (latencyChanged.exchange (false, std::memory_order_acquire) && ! rebuildGraph())
        latencyChanged.store (true, std::memory_order_release);
}

void EffectGraph::prepareToPlay (double sampleRate,
                                 int blockSize,
                                 const juce::AudioChannelSet& channelLayout)
{
    if (prepared)
        graph.releaseResources();
    prepared = false;

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
        static_cast<void> (configureProcessor (*slot.node->getProcessor(), slot.scope, processorError));
    }

    static_cast<void> (rebuildConnections());
    graph.prepareToPlay (currentSampleRate, currentBlockSize);
    resetDryDelay();
    prepared = true;
}

void EffectGraph::releaseResources()
{
    if (prepared)
        graph.releaseResources();

    prepared = false;
    dryDelay.clear();
    dryDelayPosition = 0;
}

void EffectGraph::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                juce::AudioBuffer<float>* alignedDry)
{
    const auto copyDryInput = [&]
    {
        if (alignedDry != nullptr)
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                alignedDry->copyFrom (channel, 0, buffer, channel, 0, buffer.getNumSamples());
    };
    if (stateCaptureRequested.load (std::memory_order_acquire))
    {
        copyDryInput();
        return;
    }

    activeProcessCallbacks.fetch_add (1, std::memory_order_acq_rel);
    if (stateCaptureRequested.load (std::memory_order_acquire))
    {
        activeProcessCallbacks.fetch_sub (1, std::memory_order_release);
        copyDryInput();
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
    const auto latency = dryDelay.getNumSamples();
    if (latency == 0)
        copyDryInput();
    else
    {
        // The reference runs continuously so entering bypass has complete history.
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                if (alignedDry != nullptr)
                    alignedDry->setSample (channel, sample, dryDelay.getSample (channel, dryDelayPosition));
                dryDelay.setSample (channel, dryDelayPosition, buffer.getSample (channel, sample));
            }
            if (++dryDelayPosition == latency) dryDelayPosition = 0;
        }
    }
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

bool EffectGraph::clearTails (juce::String& errorMessage)
{
    errorMessage.clear();
    if (! prepared) return true;
    if (! beginStateCapture())
    {
        errorMessage = "Effects are busy. Try clearing tails again.";
        return false;
    }
    struct CaptureScope
    {
        EffectGraph& owner;
        ~CaptureScope() { owner.endStateCapture(); }
    } capture { *this };
    try
    {
        graph.reset();
        if (prepared)
        {
            // reset() leaves the graph's latency-compensation delays intact.
            // A fresh render sequence clears them without changing slot settings.
            graph.releaseResources();
            graph.prepareToPlay (currentSampleRate, currentBlockSize);
            resetDryDelay();
        }
        return true;
    }
    catch (...)
    {
        errorMessage = "A plug-in failed while clearing effect buffers.";
        return false;
    }
}

bool EffectGraph::addProcessor (int slotId,
                                std::unique_ptr<juce::AudioProcessor> processor,
                                juce::String& errorMessage,
                                EffectScope scope)
{
    errorMessage.clear();
    if (slotId <= 0 || processor == nullptr || findSlot (slotId) != slots.end())
    {
        errorMessage = "Invalid or duplicate effect slot.";
        return false;
    }

    if (! configureProcessor (*processor, scope, errorMessage))
        return false;

    auto node = graph.addNode (std::move (processor),
                               std::nullopt,
                               juce::AudioProcessorGraph::UpdateKind::none);
    if (node == nullptr)
    {
        errorMessage = "Could not add the effect processor to the audio graph.";
        return false;
    }

    node->getProcessor()->addListener (this);
    slots.push_back ({ slotId, node, std::move (scope) });
    if (rebuildConnections())
        return true;

    slots.pop_back();
    node->getProcessor()->removeListener (this);
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

    removedSlot.node->getProcessor()->removeListener (this);
    graph.removeNode (removedSlot.node->nodeID,
                      juce::AudioProcessorGraph::UpdateKind::none);
    static_cast<void> (rebuildGraph());
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
    {
        removed.node->getProcessor()->removeListener (this);
        graph.removeNode (removed.node->nodeID,
                          juce::AudioProcessorGraph::UpdateKind::none);
    }
    static_cast<void> (rebuildGraph());
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
        if (! validateEffectScope (slot.scope, channelLayout, errorMessage))
        {
            errorMessage = slot.node->getProcessor()->getName() + ": " + errorMessage;
            return false;
        }
        juce::AudioProcessor::BusesLayout requested;
        if (! createProcessorLayout (*slot.node->getProcessor(), channelLayout, slot.scope, requested)
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

    if (prepared)
        graph.releaseResources();

    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].node->getProcessor()->setBusesLayout (requestedLayouts[i]))
            continue;

        for (size_t rollback = 0; rollback <= i; ++rollback)
            static_cast<void> (slots[rollback].node->getProcessor()->setBusesLayout (
                previousProcessorLayouts[rollback]));

        errorMessage = slots[i].node->getProcessor()->getName()
            + " rejected the requested output layout.";
        if (prepared)
        {
            graph.prepareToPlay (currentSampleRate, currentBlockSize);
            resetDryDelay();
        }
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
        if (prepared)
        {
            graph.prepareToPlay (currentSampleRate, currentBlockSize);
            resetDryDelay();
        }
        return false;
    }

    if (prepared)
    {
        graph.prepareToPlay (currentSampleRate, currentBlockSize);
        resetDryDelay();
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

const EffectScope* EffectGraph::getProcessorScope (int slotId) const
{
    const auto position = findSlot (slotId);
    return position == slots.end() ? nullptr : &position->scope;
}

bool EffectGraph::setProcessorScope (int slotId, const EffectScope& scope, juce::String& errorMessage)
{
    errorMessage.clear();
    const auto position = findSlot (slotId);
    if (position == slots.end())
    {
        errorMessage = "The effect slot no longer exists.";
        return false;
    }
    if (! validateEffectScope (scope, currentChannelLayout, errorMessage))
        return false;
    if (position->scope == scope)
        return true;

    auto& processor = *position->node->getProcessor();
    juce::AudioProcessor::BusesLayout requested;
    if (! createProcessorLayout (processor, currentChannelLayout, scope, requested)
        || ! processor.checkBusesLayoutSupported (requested))
    {
        errorMessage = processor.getName() + " does not support the selected effect channel scope.";
        return false;
    }
    if (! beginStateCapture())
    {
        errorMessage = "Timed out while preparing the effect for a channel scope change.";
        return false;
    }
    struct CaptureScope
    {
        EffectGraph& owner;
        ~CaptureScope() { owner.endStateCapture(); }
    } capture { *this };

    const auto previousLayout = processor.getBusesLayout();
    const auto previousScope = position->scope;
    const auto layoutChanged = previousLayout != requested;
    if (prepared && layoutChanged)
        processor.releaseResources();
    if (! processor.setBusesLayout (requested))
    {
        static_cast<void> (processor.setBusesLayout (previousLayout));
        if (prepared && layoutChanged)
            processor.prepareToPlay (currentSampleRate, currentBlockSize);
        errorMessage = processor.getName() + " rejected the selected effect channel scope.";
        return false;
    }
    if (prepared && layoutChanged)
        processor.prepareToPlay (currentSampleRate, currentBlockSize);
    position->scope = scope;
    if (rebuildConnections())
        return true;

    if (prepared && layoutChanged)
        processor.releaseResources();
    static_cast<void> (processor.setBusesLayout (previousLayout));
    if (prepared && layoutChanged)
        processor.prepareToPlay (currentSampleRate, currentBlockSize);
    position->scope = previousScope;
    static_cast<void> (rebuildConnections());
    errorMessage = "Could not connect the selected effect channels.";
    return false;
}

bool EffectGraph::rebuildConnections()
{
    for (const auto& connection : graph.getConnections())
        graph.removeConnection (connection, juce::AudioProcessorGraph::UpdateKind::none);

    std::vector<juce::AudioProcessorGraph::NodeAndChannel> sources;
    for (int channel = 0; channel < currentChannelLayout.size(); ++channel)
        sources.push_back ({ inputNode->nodeID, channel });
    for (const auto& slot : slots)
    {
        const auto count = slot.scope.empty() ? currentChannelLayout.size()
                                              : static_cast<int> (slot.scope.size());
        for (int channel = 0; channel < count; ++channel)
        {
            const auto outputChannel = slot.scope.empty() ? channel
                : currentChannelLayout.getChannelIndexForType (slot.scope[static_cast<size_t> (channel)]);
            if (! graph.addConnection ({ sources[static_cast<size_t> (outputChannel)],
                                        { slot.node->nodeID, channel } },
                                       juce::AudioProcessorGraph::UpdateKind::none))
                return false;
            sources[static_cast<size_t> (outputChannel)] = { slot.node->nodeID, channel };
        }
    }

    for (int channel = 0; channel < currentChannelLayout.size(); ++channel)
        if (! graph.addConnection ({ sources[static_cast<size_t> (channel)], { outputNode->nodeID, channel } },
                                   juce::AudioProcessorGraph::UpdateKind::none))
            return false;

    return rebuildGraph();
}

bool EffectGraph::rebuildGraph()
{
    // Graph edits are serialized by the control thread; scope and state edits
    // may already own the capture gate when they rebuild connections.
    const auto ownsCapture = ! stateCaptureRequested.load (std::memory_order_acquire);
    if (ownsCapture && ! beginStateCapture()) return false;
    struct CaptureScope
    {
        EffectGraph& owner;
        bool acquired;
        ~CaptureScope() { if (acquired) owner.endStateCapture(); }
    } capture { *this, ownsCapture };
    graph.rebuild();
    if (prepared && (dryDelay.getNumChannels() != currentChannelLayout.size()
                     || dryDelay.getNumSamples() != juce::jmax (0, graph.getLatencySamples())))
        resetDryDelay();
    return true;
}

void EffectGraph::resetDryDelay()
{
    dryDelay.setSize (currentChannelLayout.size(), juce::jmax (0, graph.getLatencySamples()));
    dryDelay.clear();
    dryDelayPosition = 0;
}

bool EffectGraph::configureProcessor (juce::AudioProcessor& processor, const EffectScope& scope,
                                      juce::String& errorMessage) const
{
    if (! validateEffectScope (scope, currentChannelLayout, errorMessage))
        return false;
    juce::AudioProcessor::BusesLayout layout;
    if (! createProcessorLayout (processor, currentChannelLayout, scope, layout)
        || ! processor.checkBusesLayoutSupported (layout)
        || ! processor.setBusesLayout (layout))
    {
        const auto requested = scope.empty() ? currentChannelLayout.getDescription()
            : scope.size() == 1 ? juce::String ("mono") : juce::String ("stereo");
        errorMessage = processor.getName() + " cannot process the requested " + requested
            + " effect scope.";
        return false;
    }

    processor.setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
    return true;
}

bool EffectGraph::createProcessorLayout (const juce::AudioProcessor& processor,
                                         const juce::AudioChannelSet& channelLayout,
                                         const EffectScope& scope,
                                         juce::AudioProcessor::BusesLayout& busesLayout) const
{
    busesLayout = processor.getBusesLayout();
    if (busesLayout.inputBuses.isEmpty() || busesLayout.outputBuses.isEmpty())
        return false;

    for (auto& bus : busesLayout.inputBuses)
        bus = juce::AudioChannelSet::disabled();
    for (auto& bus : busesLayout.outputBuses)
        bus = juce::AudioChannelSet::disabled();

    const auto processingLayout = scope.empty() ? channelLayout
        : scope.size() == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    busesLayout.inputBuses.getReference (0) = processingLayout;
    busesLayout.outputBuses.getReference (0) = processingLayout;
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
