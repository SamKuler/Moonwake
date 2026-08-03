#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/ChannelRouter.h"

class ChannelMappedAudioSource final : public juce::PositionableAudioSource
{
public:
    ChannelMappedAudioSource (std::unique_ptr<juce::AudioFormatReader> reader,
                              juce::AudioChannelSet destinationLayout);

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void setNextReadPosition (juce::int64 newPosition) override;
    [[nodiscard]] juce::int64 getNextReadPosition() const override;
    [[nodiscard]] juce::int64 getTotalLength() const override;
    [[nodiscard]] bool isLooping() const override;
    void setLooping (bool shouldLoop) override;

    void setDestinationLayout (juce::AudioChannelSet layout);
    [[nodiscard]] const juce::AudioChannelSet& getSourceLayout() const;

private:
    static juce::AudioChannelSet getUsableSourceLayout (juce::AudioFormatReader& reader);

    juce::AudioChannelSet sourceLayout;
    juce::AudioChannelSet destinationLayout;
    juce::AudioFormatReaderSource readerSource;
    juce::AudioBuffer<float> scratchBuffer;
};
