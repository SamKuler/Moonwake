#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/ChannelRouter.h"

#include <atomic>

class ChannelMappedAudioSource final : public juce::PositionableAudioSource,
                                       private juce::TimeSliceClient
{
public:
    ChannelMappedAudioSource (std::unique_ptr<juce::AudioFormatReader> reader,
                              juce::AudioChannelSet destinationLayout,
                              juce::TimeSliceThread& readAheadThread);
    ~ChannelMappedAudioSource() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    // The transport must suspend its audio callback before changing the read position.
    void setNextReadPosition (juce::int64 newPosition) override;
    [[nodiscard]] juce::int64 getNextReadPosition() const override;
    [[nodiscard]] juce::int64 getTotalLength() const override;
    [[nodiscard]] bool isLooping() const override;
    void setLooping (bool shouldLoop) override;

    void setDestinationLayout (juce::AudioChannelSet layout);
    [[nodiscard]] const juce::AudioChannelSet& getSourceLayout() const;
    [[nodiscard]] bool hasReadError() const noexcept;

private:
    static juce::AudioChannelSet getUsableSourceLayout (juce::AudioFormatReader& reader);
    bool readNextChunk();
    void prefillAndStartReading();
    int useTimeSlice() override;

    juce::AudioChannelSet sourceLayout;
    juce::AudioChannelSet destinationLayout;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::TimeSliceThread& readAheadThread;
    static constexpr int readAheadCapacity = 65536;
    juce::AbstractFifo readAheadFifo { readAheadCapacity + 1 };
    juce::AudioBuffer<float> readAheadBuffer;
    juce::AudioBuffer<float> scratchBuffer;
    juce::int64 producerPosition = 0;
    std::atomic<juce::int64> consumedPosition { 0 };
    std::atomic_bool readFailed { false };
    bool looping = false;
    int prefillSamples = 8192;
};
