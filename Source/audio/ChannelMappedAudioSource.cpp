#include "ChannelMappedAudioSource.h"

ChannelMappedAudioSource::ChannelMappedAudioSource (std::unique_ptr<juce::AudioFormatReader> reader,
                                                    juce::AudioChannelSet destinationLayoutToUse)
    : sourceLayout (getUsableSourceLayout (*reader)),
      destinationLayout (std::move (destinationLayoutToUse)),
      readerSource (reader.release(), true)
{
}

void ChannelMappedAudioSource::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    scratchBuffer.setSize (sourceLayout.size(),
                           juce::jmax (1, samplesPerBlockExpected),
                           false,
                           true,
                           false);
    readerSource.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void ChannelMappedAudioSource::releaseResources()
{
    readerSource.releaseResources();
}

void ChannelMappedAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
        return;

    auto samplesRemaining = bufferToFill.numSamples;
    auto destinationOffset = bufferToFill.startSample;
    while (samplesRemaining > 0)
    {
        const auto chunkSize = juce::jmin (samplesRemaining, scratchBuffer.getNumSamples());
        if (chunkSize <= 0)
        {
            bufferToFill.clearActiveBufferRegion();
            return;
        }

        scratchBuffer.clear();
        readerSource.getNextAudioBlock ({ &scratchBuffer, 0, chunkSize });
        ChannelRouter::route (scratchBuffer,
                              sourceLayout,
                              *bufferToFill.buffer,
                              destinationLayout,
                              destinationOffset,
                              chunkSize);
        destinationOffset += chunkSize;
        samplesRemaining -= chunkSize;
    }
}

void ChannelMappedAudioSource::setNextReadPosition (juce::int64 newPosition)
{
    readerSource.setNextReadPosition (newPosition);
}

juce::int64 ChannelMappedAudioSource::getNextReadPosition() const
{
    return readerSource.getNextReadPosition();
}

juce::int64 ChannelMappedAudioSource::getTotalLength() const
{
    return readerSource.getTotalLength();
}

bool ChannelMappedAudioSource::isLooping() const
{
    return readerSource.isLooping();
}

void ChannelMappedAudioSource::setLooping (bool shouldLoop)
{
    readerSource.setLooping (shouldLoop);
}

void ChannelMappedAudioSource::setDestinationLayout (juce::AudioChannelSet layout)
{
    destinationLayout = std::move (layout);
}

const juce::AudioChannelSet& ChannelMappedAudioSource::getSourceLayout() const
{
    return sourceLayout;
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
