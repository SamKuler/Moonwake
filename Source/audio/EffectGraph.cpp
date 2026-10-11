#include "EffectGraph.h"

#include <algorithm>
#include <stdexcept>

class EffectGraph::ScopedEdit
{
public:
    explicit ScopedEdit (EffectGraph& graphToUse) : owner (graphToUse)
    {
        acquired = ! owner.stateCaptureRequested.load (std::memory_order_acquire);
        ready = ! acquired || owner.beginStateCapture();
    }
    ~ScopedEdit() { if (acquired && ready) owner.endStateCapture(); }
    explicit operator bool() const noexcept { return ready; }
private:
    EffectGraph& owner;
    bool acquired = false, ready = false;
};

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
    const ScopedEdit edit (*this);
    if (! edit) throw std::runtime_error ("Timed out while preparing the effect graph.");
    processingReady.store (false, std::memory_order_release);
    prepared = false;
    // JUCE records a node as prepared before calling the plug-in. Clear those
    // records even when an earlier prepare attempt failed part-way through.
    graph.releaseResources();

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    juce::String error;
    if (! setChannelLayout (channelLayout, error))
        throw std::runtime_error (error.toStdString());
    graph.setPlayConfigDetails (currentChannelLayout.size(), currentChannelLayout.size(),
                               currentSampleRate, currentBlockSize);
    for (auto& slot : slots)
        if (! configureProcessor (*slot.node->getProcessor(), slot.scope, error))
            throw std::runtime_error (error.toStdString());
    if (! rebuildConnections())
        throw std::runtime_error ("Could not rebuild the effect graph.");
    graph.prepareToPlay (currentSampleRate, currentBlockSize);
    resetDryDelay();
    prepared = true;
    processingReady.store (true, std::memory_order_release);
}

void EffectGraph::releaseResources()
{
    processingReady.store (false, std::memory_order_release);
    if (std::exchange (prepared, false))
        graph.releaseResources();
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
    if (stateCaptureRequested.load (std::memory_order_acquire)
        || ! processingReady.load (std::memory_order_acquire))
    {
        copyDryInput();
        return;
    }

    activeProcessCallbacks.fetch_add (1, std::memory_order_acq_rel);
    if (stateCaptureRequested.load (std::memory_order_acquire)
        || ! processingReady.load (std::memory_order_acquire))
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
        processingReady.store (false, std::memory_order_release);
        graph.reset();
        if (prepared)
        {
            // reset() leaves the graph's latency-compensation delays intact.
            // A fresh render sequence clears them without changing slot settings.
            graph.releaseResources();
            graph.prepareToPlay (currentSampleRate, currentBlockSize);
            resetDryDelay();
        }
        processingReady.store (prepared, std::memory_order_release);
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

    const ScopedEdit edit (*this);
    if (! edit)
    {
        errorMessage = "Timed out while preparing the effect graph for insertion.";
        return false;
    }
    try
    {
        if (! configureProcessor (*processor, scope, errorMessage)) return false;
    }
    catch (...)
    {
        errorMessage = "The plug-in failed while configuring its audio channels.";
        return false;
    }

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
    const auto restored = rebuildConnections();
    if (! restored) processingReady.store (false, std::memory_order_release);
    errorMessage = "The plug-in failed while preparing its " + juce::String (currentChannelLayout.size())
        + "-channel audio path.";
    if (! restored) errorMessage += " The previous effect chain could not be restored; effects are suspended.";
    return false;
}

bool EffectGraph::removeProcessor (int slotId)
{
    const ScopedEdit edit (*this);
    if (! edit) return false;
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
        if (! rebuildConnections()) processingReady.store (false, std::memory_order_release);
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
    const ScopedEdit edit (*this);
    if (! edit) return false;
    if (slots.empty())
        return true;

    auto removedSlots = slots;
    slots.clear();
    if (! rebuildConnections())
    {
        slots = std::move (removedSlots);
        if (! rebuildConnections()) processingReady.store (false, std::memory_order_release);
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
    const ScopedEdit edit (*this);
    if (! edit) return false;
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
    if (! rebuildConnections()) processingReady.store (false, std::memory_order_release);
    return false;
}

bool EffectGraph::setProcessorOrder (const std::vector<int>& orderedSlotIds)
{
    const ScopedEdit edit (*this);
    if (! edit) return false;
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
    if (! rebuildConnections()) processingReady.store (false, std::memory_order_release);
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

    if (channelLayout == currentChannelLayout) return true;
    const ScopedEdit edit (*this);
    if (! edit)
    {
        errorMessage = "Timed out while preparing the effect graph for a layout change.";
        return false;
    }
    std::vector<juce::AudioProcessor::BusesLayout> requestedLayouts, previousLayouts;
    requestedLayouts.reserve (slots.size());
    previousLayouts.reserve (slots.size());
    try
    {
        for (const auto& slot : slots)
        {
            auto& processor = *slot.node->getProcessor();
            if (! validateEffectScope (slot.scope, channelLayout, errorMessage)) return false;
            juce::AudioProcessor::BusesLayout requested;
            if (! createProcessorLayout (processor, channelLayout, slot.scope, requested)
                || ! processor.checkBusesLayoutSupported (requested))
            {
                errorMessage = processor.getName() + " does not support "
                    + channelLayout.getDescription() + " (" + juce::String (channelLayout.size())
                    + "-channel) processing.";
                return false;
            }
            requestedLayouts.push_back (std::move (requested));
            previousLayouts.push_back (processor.getBusesLayout());
        }
    }
    catch (...)
    {
        errorMessage = "A plug-in failed while checking the requested output layout.";
        return false;
    }

    const auto previousLayout = currentChannelLayout;
    const auto configureGraph = [&] (const juce::AudioChannelSet& layout)
    {
        currentChannelLayout = layout;
        graph.setPlayConfigDetails (layout.size(), layout.size(), currentSampleRate, currentBlockSize);
        static_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*> (inputNode->getProcessor())
            ->setParentGraph (&graph);
        static_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*> (outputNode->getProcessor())
            ->setParentGraph (&graph);
        if (! rebuildConnections()) throw std::runtime_error ("Could not rebuild the effect graph.");
        if (prepared) graph.prepareToPlay (currentSampleRate, currentBlockSize);
        resetDryDelay();
        processingReady.store (prepared, std::memory_order_release);
    };
    try
    {
        processingReady.store (false, std::memory_order_release);
        if (prepared) graph.releaseResources();
        for (size_t i = 0; i < slots.size(); ++i)
            if (! slots[i].node->getProcessor()->setBusesLayout (requestedLayouts[i]))
                throw std::runtime_error ("A plug-in rejected the requested output layout.");
        configureGraph (channelLayout);
        return true;
    }
    catch (...)
    {
        errorMessage = "A plug-in failed while applying the requested output layout.";
    }
    try
    {
        if (prepared) graph.releaseResources();
        for (size_t i = 0; i < slots.size(); ++i)
            if (! slots[i].node->getProcessor()->setBusesLayout (previousLayouts[i]))
                throw std::runtime_error ("Could not restore plug-in layout.");
        configureGraph (previousLayout);
    }
    catch (...)
    {
        currentChannelLayout = previousLayout;
        processingReady.store (false, std::memory_order_release);
        errorMessage += " The previous effect configuration could not be prepared; effects are suspended.";
    }
    return false;
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

    const ScopedEdit edit (*this);
    if (! edit)
    {
        errorMessage = "Timed out while preparing the effect for a channel scope change.";
        return false;
    }
    auto& processor = *position->node->getProcessor();
    const auto previousLayout = processor.getBusesLayout();
    const auto previousScope = position->scope;
    juce::AudioProcessor::BusesLayout requested;
    try
    {
        if (! createProcessorLayout (processor, currentChannelLayout, scope, requested)
            || ! processor.checkBusesLayoutSupported (requested))
        {
            errorMessage = processor.getName() + " does not support the selected effect channel scope.";
            return false;
        }
    }
    catch (...)
    {
        errorMessage = "The plug-in failed while checking the selected effect channel scope.";
        return false;
    }
    const auto layoutChanged = previousLayout != requested;
    const auto applyLayout = [&] (const juce::AudioProcessor::BusesLayout& layout)
    {
        if (prepared && layoutChanged) processor.releaseResources();
        if (! processor.setBusesLayout (layout))
            throw std::runtime_error ("The plug-in rejected the effect channel scope.");
        if (prepared && layoutChanged) processor.prepareToPlay (currentSampleRate, currentBlockSize);
    };
    try
    {
        applyLayout (requested);
        position->scope = scope;
        if (rebuildConnections()) return true;
    }
    catch (...) {}
    errorMessage = "The plug-in failed while applying the selected effect channel scope.";
    position->scope = previousScope;
    try
    {
        applyLayout (previousLayout);
        if (! rebuildConnections()) throw std::runtime_error ("Could not restore effect connections.");
    }
    catch (...)
    {
        processingReady.store (false, std::memory_order_release);
        errorMessage += " The previous effect configuration could not be prepared; effects are suspended.";
    }
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
    const ScopedEdit edit (*this);
    if (! edit) return false;
    try
    {
        graph.rebuild();
        if (prepared && (dryDelay.getNumChannels() != currentChannelLayout.size()
                         || dryDelay.getNumSamples() != juce::jmax (0, graph.getLatencySamples())))
            resetDryDelay();
        return true;
    }
    catch (...)
    {
        return false;
    }
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
