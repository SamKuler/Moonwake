#include "ChannelMappedAudioSource.h"

ChannelMappedAudioSource::ChannelMappedAudioSource (std::unique_ptr<juce::AudioFormatReader> readerToUse,
                                                    juce::AudioChannelSet destinationLayoutToUse)
    : sourceLayout (getUsableSourceLayout (*readerToUse)),
      destinationLayout (std::move (destinationLayoutToUse)),
      reader (std::move (readerToUse))
{
}

ChannelMappedAudioSource::ChannelMappedAudioSource (const ChannelMappedAudioSource& original,
                                                    juce::AudioChannelSet layout)
    : sourceLayout (original.sourceLayout), destinationLayout (std::move (layout)),
      reader (original.reader), nextReadPosition (original.nextReadPosition), looping (original.looping)
{
    prepareToPlay (original.scratchBuffer.getNumSamples(), reader->sampleRate);
}

void ChannelMappedAudioSource::prepareToPlay (int samplesPerBlockExpected, double)
{
    scratchBuffer.setSize (sourceLayout.size(),
                           juce::jmax (1, samplesPerBlockExpected),
                           false,
                           true,
                           false);
}

void ChannelMappedAudioSource::releaseResources()
{
}

void ChannelMappedAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
        return;

    bufferToFill.clearActiveBufferRegion();
    if (readFailed)
        return;
    auto samplesRemaining = bufferToFill.numSamples;
    auto destinationOffset = bufferToFill.startSample;
    while (samplesRemaining > 0)
    {
        if (looping && nextReadPosition >= getTotalLength())
            nextReadPosition = 0;
        const auto remaining = juce::jmax (juce::int64{}, getTotalLength() - nextReadPosition);
        const auto chunkSize = static_cast<int> (juce::jmin (remaining,
            static_cast<juce::int64> (juce::jmin (samplesRemaining, scratchBuffer.getNumSamples()))));
        if (chunkSize == 0)
            return;
        if (! reader->read (&scratchBuffer, 0, chunkSize, nextReadPosition, true, true))
        {
            readFailed = true;
            return;
        }
        ChannelRouter::route (scratchBuffer,
                              sourceLayout,
                              *bufferToFill.buffer,
                              destinationLayout,
                              destinationOffset,
                              chunkSize);
        nextReadPosition += chunkSize;
        destinationOffset += chunkSize;
        samplesRemaining -= chunkSize;
    }
}

void ChannelMappedAudioSource::setNextReadPosition (juce::int64 newPosition)
{
    nextReadPosition = juce::jmax (juce::int64{}, newPosition);
    readFailed = false;
}

juce::int64 ChannelMappedAudioSource::getNextReadPosition() const
{
    return looping && getTotalLength() > 0 ? nextReadPosition % getTotalLength() : nextReadPosition;
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
    looping = shouldLoop;
}

void ChannelMappedAudioSource::setDestinationLayout (juce::AudioChannelSet layout)
{
    destinationLayout = std::move (layout);
}

std::unique_ptr<ChannelMappedAudioSource> ChannelMappedAudioSource::forkForLayout (juce::AudioChannelSet layout) const
{
    return std::unique_ptr<ChannelMappedAudioSource> (new ChannelMappedAudioSource (*this, std::move (layout)));
}

const juce::AudioChannelSet& ChannelMappedAudioSource::getSourceLayout() const
{
    return sourceLayout;
}

bool ChannelMappedAudioSource::hasReadError() const noexcept
{
    return readFailed;
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
