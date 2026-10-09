#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/ChannelMappedAudioSource.h"
#include "audio/OutputLayout.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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

    // Load, seek, clear and layout edits require the transport callback to be suspended.
    // Upcoming edits prepare and publish while the current audio continues.
    bool loadPlan (AudioPlaybackPlan, double startSeconds = 0.0);
    bool setUpcoming (std::vector<AudioPlaybackItem>, int repeatFrom,
                      std::optional<AudioPlaybackProgress> expected = {});
    bool seekToSeconds (double);
    void clear();
    void setDestinationLayout (juce::AudioChannelSet);
    // Changes output format once, restoring the previous renderable source on failure.
    // The optional output preparation runs before publishing the replacement PCM.
    juce::Result reconfigureOutput (juce::AudioChannelSet, double sampleRate,
                                   const std::function<juce::Result()>& prepareOutput = {});
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
    struct PlaybackBuffer;
    struct RenderState;
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
        long double renderedEnd = 0.0L;
    };

    int useTimeSlice() override;
    void prepareFutureRenderer (long double firstFrame);
    [[nodiscard]] PlaybackBuffer* playbackBuffer() const noexcept;
    bool prepareDecoder (uint64_t sequence, double startSeconds);
    bool produceChunk();
    bool resetAt (uint64_t sequence, double startSeconds, PlaybackBuffer* target = nullptr);
    void stopPreparing();
    void recordFailure (uint64_t sequence, const juce::String&, bool hadBufferedAudio = false);
    [[nodiscard]] int indexForSequence (uint64_t sequence) const noexcept;
    [[nodiscard]] bool repeatCycleUnavailable() const;

    juce::AudioFormatManager& formats;
    juce::TimeSliceThread& readAheadThread;
    std::unique_ptr<RenderState> rendering;
    juce::AudioChannelSet destinationLayout = juce::AudioChannelSet::stereo();
    double deviceSampleRate = 44100.0;
    static constexpr int bufferFrames = 65536;
    static constexpr int chunkFrames = 1024;
    std::array<std::unique_ptr<PlaybackBuffer>, 2> buffers;
    // Bit 0: callback active, bit 1: buffer index, upper bits: publication/callback serial.
    std::atomic<uint64_t> bufferState { 0 };
    juce::AudioBuffer<float> renderBuffer { maximumOutputChannels, chunkFrames };

    std::atomic<juce::int64> consumedFrames { 0 };
    std::atomic_bool loaded { false };
    std::mutex failureMutex;
    std::vector<AudioPreparationFailure> failures;
};
