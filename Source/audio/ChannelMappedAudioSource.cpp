#include "ChannelMappedAudioSource.h"

ChannelMappedAudioSource::ChannelMappedAudioSource (std::unique_ptr<juce::AudioFormatReader> readerToUse,
                                                    juce::AudioChannelSet destinationLayoutToUse,
                                                    juce::TimeSliceThread& threadToUse)
    : sourceLayout (getUsableSourceLayout (*readerToUse)),
      destinationLayout (std::move (destinationLayoutToUse)),
      reader (std::move (readerToUse)),
      readAheadThread (threadToUse),
      readAheadBuffer (sourceLayout.size(), readAheadCapacity + 1)
{
}

ChannelMappedAudioSource::~ChannelMappedAudioSource()
{
    readAheadThread.removeTimeSliceClient (this);
}

void ChannelMappedAudioSource::prepareToPlay (int samplesPerBlockExpected, double)
{
    readAheadThread.removeTimeSliceClient (this);
    scratchBuffer.setSize (sourceLayout.size(),
                           juce::jmax (1, samplesPerBlockExpected),
                           false,
                           true,
                           false);
    prefillSamples = juce::jmin (readAheadCapacity,
                                juce::jmax (8192, samplesPerBlockExpected * 2));
    prefillAndStartReading();
}

void ChannelMappedAudioSource::releaseResources()
{
    readAheadThread.removeTimeSliceClient (this);
}

void ChannelMappedAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
        return;

    bufferToFill.clearActiveBufferRegion();
    auto samplesRemaining = bufferToFill.numSamples;
    auto destinationOffset = bufferToFill.startSample;
    while (samplesRemaining > 0)
    {
        const auto read = readAheadFifo.read (
            juce::jmin (samplesRemaining, scratchBuffer.getNumSamples()));
        const auto chunkSize = read.blockSize1 + read.blockSize2;
        if (chunkSize == 0)
            return;

        for (int channel = 0; channel < sourceLayout.size(); ++channel)
        {
            scratchBuffer.copyFrom (channel, 0,
                readAheadBuffer.getReadPointer (channel, read.startIndex1), read.blockSize1);
            scratchBuffer.copyFrom (channel, read.blockSize1,
                readAheadBuffer.getReadPointer (channel, read.startIndex2), read.blockSize2);
        }
        ChannelRouter::route (scratchBuffer,
                              sourceLayout,
                              *bufferToFill.buffer,
                              destinationLayout,
                              destinationOffset,
                              chunkSize);
        consumedPosition.fetch_add (chunkSize, std::memory_order_release);
        destinationOffset += chunkSize;
        samplesRemaining -= chunkSize;
    }
}

void ChannelMappedAudioSource::setNextReadPosition (juce::int64 newPosition)
{
    readAheadThread.removeTimeSliceClient (this);
    readAheadFifo.reset();
    consumedPosition.store (juce::jmax (juce::int64{}, newPosition),
                            std::memory_order_release);
    producerPosition = getNextReadPosition();
    readFailed.store (false, std::memory_order_release);
    prefillAndStartReading();
}

juce::int64 ChannelMappedAudioSource::getNextReadPosition() const
{
    const auto position = consumedPosition.load (std::memory_order_acquire);
    return looping && getTotalLength() > 0 ? position % getTotalLength() : position;
}

juce::int64 ChannelMappedAudioSource::getTotalLength() const
{
    return reader->lengthInSamples;
}

bool ChannelMappedAudioSource::isLooping() const
{
    return looping;
}

void ChannelMappedAudioSource::setLooping (bool shouldLoop)
{
    if (looping == shouldLoop)
        return;
    readAheadThread.removeTimeSliceClient (this);
    const auto position = getNextReadPosition();
    looping = shouldLoop;
    setNextReadPosition (position);
}

void ChannelMappedAudioSource::setDestinationLayout (juce::AudioChannelSet layout)
{
    destinationLayout = std::move (layout);
}

const juce::AudioChannelSet& ChannelMappedAudioSource::getSourceLayout() const
{
    return sourceLayout;
}

bool ChannelMappedAudioSource::hasReadError() const noexcept
{
    return readFailed.load (std::memory_order_acquire);
}

bool ChannelMappedAudioSource::readNextChunk()
{
    if (hasReadError() || getTotalLength() <= 0)
        return false;
    if (producerPosition >= getTotalLength())
    {
        if (! looping)
            return false;
        producerPosition = 0;
    }

    const auto samples = static_cast<int> (juce::jmin (
        juce::int64 { 2048 }, getTotalLength() - producerPosition));
    int start1, size1, start2, size2;
    readAheadFifo.prepareToWrite (samples, start1, size1, start2, size2);
    const auto readSection = [this] (int start, int size)
    {
        if (size == 0)
            return true;
        if (! reader->read (&readAheadBuffer, start, size, producerPosition, true, true))
        {
            readFailed.store (true, std::memory_order_release);
            return false;
        }
        producerPosition += size;
        return true;
    };

    int written = 0;
    if (readSection (start1, size1))
    {
        written = size1;
        if (readSection (start2, size2))
            written += size2;
    }
    readAheadFifo.finishedWrite (written);
    return written > 0;
}

void ChannelMappedAudioSource::prefillAndStartReading()
{
    while (readAheadFifo.getNumReady() < prefillSamples && readNextChunk()) {}
    if (! hasReadError() && (looping || producerPosition < getTotalLength()))
        readAheadThread.addTimeSliceClient (this);
}

int ChannelMappedAudioSource::useTimeSlice()
{
    const auto read = readNextChunk();
    if (hasReadError() || (! looping && producerPosition >= getTotalLength()))
        return -1;
    return read ? 1 : 5;
}

juce::AudioChannelSet ChannelMappedAudioSource::getUsableSourceLayout (juce::AudioFormatReader& reader)
{
    auto layout = reader.getChannelLayout();
    const auto channelCount = static_cast<int> (reader.numChannels);
    if (layout.size() == channelCount && ! layout.isDisabled())
        return layout;

    if (channelCount == 1)
        return juce::AudioChannelSet::mono();

    if (channelCount == 2)
        return juce::AudioChannelSet::stereo();

    return juce::AudioChannelSet::discreteChannels (channelCount);
}
