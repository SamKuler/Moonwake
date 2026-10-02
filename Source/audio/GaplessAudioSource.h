#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/ChannelMappedAudioSource.h"

#include <atomic>
#include <memory>
#include <limits>
#include <mutex>
#include <vector>

struct AudioPlaybackItem
{
    juce::File file;
    uint64_t token = 0;
};

struct AudioPlaybackPlan
{
    std::vector<AudioPlaybackItem> items;
    int repeatFrom = -1;
};

struct AudioPlaybackProgress
{
    uint64_t generation = 0;
    uint64_t sequence = 0;
    uint64_t token = 0;
    double positionSeconds = 0.0;
    double lengthSeconds = 0.0;
    bool finished = false;
    bool readError = false;
};

struct AudioPreparationFailure
{
    uint64_t generation = 0;
    uint64_t sequence = 0;
    uint64_t token = 0;
    juce::String error;
    bool hadBufferedAudio = false;
};

class GaplessAudioSource final : public juce::PositionableAudioSource,
                                 private juce::TimeSliceClient
{
public:
    GaplessAudioSource (juce::AudioFormatManager&, juce::TimeSliceThread&);
    ~GaplessAudioSource() override;

    // Control operations require the owning transport's callback to be suspended.
    bool loadPlan (AudioPlaybackPlan, double startSeconds = 0.0);
    bool setUpcoming (std::vector<AudioPlaybackItem>, int repeatFrom);
    bool seekToSeconds (double);
    void clear();
    void setDestinationLayout (juce::AudioChannelSet);
    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] AudioPlaybackProgress progress() const noexcept;
    std::vector<AudioPreparationFailure> drainPreparationFailures();

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void setNextReadPosition (juce::int64) override;
    [[nodiscard]] juce::int64 getNextReadPosition() const override;
    [[nodiscard]] juce::int64 getTotalLength() const override;
    [[nodiscard]] bool isLooping() const override { return false; }

private:
    struct Decoder;
    struct CachedTrack;
    struct NativeStream;
    struct Boundary
    {
        juce::int64 streamOffset = 0;
        double startFrame = 0.0;
        uint64_t sequence = 0;
        uint64_t token = 0;
        double lengthSeconds = 0.0;
    };

    int useTimeSlice() override;
    bool prepareDecoder (uint64_t sequence, double startSeconds);
    bool produceChunk();
    bool resetAt (uint64_t sequence, double startSeconds);
    void stopPreparing();
    void publish (const Boundary&, juce::int64 streamPosition) noexcept;
    void recordFailure (uint64_t sequence, const juce::String&, bool hadBufferedAudio = false);
    [[nodiscard]] int indexForSequence (uint64_t sequence) const noexcept;
    [[nodiscard]] bool repeatCycleUnavailable() const;

    juce::AudioFormatManager& formats;
    juce::TimeSliceThread& readAheadThread;
    AudioPlaybackPlan plan;
    std::vector<bool> unavailable;
    std::vector<std::unique_ptr<CachedTrack>> cachedTracks;
    int cachedFrames = 0;
    juce::AudioChannelSet destinationLayout = juce::AudioChannelSet::stereo();
    double deviceSampleRate = 44100.0;
    static constexpr int bufferFrames = 65536;
    static constexpr int chunkFrames = 1024;
    juce::AbstractFifo audioFifo { bufferFrames + 1 };
    juce::AudioBuffer<float> audioBuffer { 10, bufferFrames + 1 };
    juce::AudioBuffer<float> renderBuffer { 10, chunkFrames };
    juce::AbstractFifo boundaryFifo { bufferFrames + 1 };
    std::vector<Boundary> boundaries;
    std::unique_ptr<Decoder> decoder;
    std::unique_ptr<NativeStream> nativeStream;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    uint64_t generation = 0;
    uint64_t producerSequence = 0;
    juce::int64 producedFrames = 0;
    Boundary activeBoundary;
    std::atomic<juce::int64> consumedFrames { 0 };
    std::atomic<juce::int64> finalFrame { std::numeric_limits<juce::int64>::max() };
    std::atomic_bool loaded { false };
    std::atomic_bool readFailed { false };
    std::atomic<uint64_t> progressVersion { 0 };
    std::atomic<uint64_t> currentGeneration { 0 }, currentSequence { 0 }, currentToken { 0 };
    std::atomic<double> currentPosition { 0.0 }, currentLength { 0.0 };
    std::mutex failureMutex;
    std::vector<AudioPreparationFailure> failures;
};
