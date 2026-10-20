#include "GaplessAudioSource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>

namespace
{
static_assert (std::atomic<uint64_t>::is_always_lock_free);
static_assert (std::atomic<double>::is_always_lock_free);
static_assert (std::numeric_limits<long double>::digits <= 2 * std::numeric_limits<double>::digits);
constexpr auto unknownEnd = std::numeric_limits<juce::int64>::max() / 4;
}

struct GaplessAudioSource::PlaybackBuffer
{
    struct Snapshot
    {
        AudioPlaybackProgress progress;
        juce::int64 frame = 0;
        Boundary boundary;
    };

    juce::AbstractFifo audioFifo { bufferFrames + 1 }, boundaryFifo { bufferFrames + 1 };
    juce::AudioBuffer<float> audio { maximumOutputChannels, bufferFrames + 1 };
    std::vector<Boundary> boundaries = std::vector<Boundary> (bufferFrames + 1);
    juce::int64 baseFrame = 0, nextReadFrame = 0;
    Boundary activeBoundary;
    uint64_t generation = 0;
    double sampleRate = 44100.0;
    std::atomic<juce::int64> finalFrame { unknownEnd };
    std::atomic_bool readFailed { false };
    std::atomic<uint64_t> progressVersion { 0 }, sequence { 0 }, token { 0 };
    std::atomic<juce::int64> reportedFrame { 0 }, boundaryFrame { 0 };
    std::atomic<double> position { 0.0 }, length { 0.0 }, startFrame { 0.0 };
    // Two double atomics preserve the render clock without a locking atomic<long double>.
    std::atomic<double> renderedEndHigh { 0.0 }, renderedEndLow { 0.0 };

    void reset (uint64_t nextGeneration, double rate, juce::int64 firstFrame, Boundary first)
    {
        audioFifo.reset();
        boundaryFifo.reset();
        generation = nextGeneration;
        sampleRate = rate;
        baseFrame = nextReadFrame = firstFrame;
        activeBoundary = first;
        finalFrame.store (unknownEnd);
        readFailed.store (false);
        publish (first, firstFrame);
    }

    void publish (const Boundary& boundary, juce::int64 frame) noexcept
    {
        progressVersion.fetch_add (1, std::memory_order_acq_rel);
        reportedFrame.store (frame, std::memory_order_relaxed);
        sequence.store (boundary.sequence, std::memory_order_relaxed);
        token.store (boundary.token, std::memory_order_relaxed);
        length.store (boundary.lengthSeconds, std::memory_order_relaxed);
        boundaryFrame.store (boundary.streamOffset, std::memory_order_relaxed);
        startFrame.store (boundary.startFrame, std::memory_order_relaxed);
        const auto high = static_cast<double> (boundary.renderedEnd);
        renderedEndHigh.store (high, std::memory_order_relaxed);
        renderedEndLow.store (static_cast<double> (boundary.renderedEnd - static_cast<long double> (high)),
                              std::memory_order_relaxed);
        position.store (juce::jlimit (0.0, boundary.lengthSeconds,
            (boundary.startFrame + static_cast<double> (frame - boundary.streamOffset)) / sampleRate),
            std::memory_order_relaxed);
        progressVersion.fetch_add (1, std::memory_order_release);
    }

    Snapshot snapshot() const noexcept
    {
        Snapshot result;
        for (;;)
        {
            const auto before = progressVersion.load (std::memory_order_acquire);
            if ((before & 1u) != 0) continue;
            result.progress.generation = generation;
            result.progress.sequence = sequence.load (std::memory_order_relaxed);
            result.progress.token = token.load (std::memory_order_relaxed);
            result.progress.positionSeconds = position.load (std::memory_order_relaxed);
            result.progress.lengthSeconds = length.load (std::memory_order_relaxed);
            result.frame = reportedFrame.load (std::memory_order_relaxed);
            result.boundary = { boundaryFrame.load (std::memory_order_relaxed),
                startFrame.load (std::memory_order_relaxed), result.progress.sequence,
                result.progress.token, result.progress.lengthSeconds,
                static_cast<long double> (renderedEndHigh.load (std::memory_order_relaxed))
                    + static_cast<long double> (renderedEndLow.load (std::memory_order_relaxed)) };
            result.progress.finished = result.frame >= finalFrame.load (std::memory_order_acquire);
            result.progress.readError = readFailed.load (std::memory_order_acquire);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (progressVersion.load (std::memory_order_acquire) == before) return result;
        }
    }
};

struct GaplessAudioSource::CachedTrack
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 0.0;
};

struct GaplessAudioSource::Decoder
{
    std::unique_ptr<ChannelMappedAudioSource> source;
    std::shared_ptr<const CachedTrack> cached;
    Boundary boundary;
    double sampleRate = 0.0;
    juce::int64 lengthFrames = 0;
    juce::int64 emittedFrames = 0;
    juce::int64 firstFrame = 0;
};

struct GaplessAudioSource::RenderState
{
    AudioPlaybackPlan plan;
    std::vector<bool> unavailable;
    std::vector<std::shared_ptr<CachedTrack>> cachedTracks;
    int cachedFrames = 0;
    std::unique_ptr<Decoder> decoder;
    std::unique_ptr<NativeStream> nativeStream;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    uint64_t generation = 0, producerSequence = 0;
    juce::int64 producedFrames = 0;
    PlaybackBuffer* queue = nullptr;
    std::optional<long double> futureCutoverFrame;
};

// The worker joins native PCM before a single resampler. Track boundaries describe
// the same continuous clock as the PCM, including fractional device frames.
struct GaplessAudioSource::NativeStream final : juce::AudioSource
{
    struct Span
    {
        Boundary boundary;
        long double outputStart = 0.0L, outputEnd = 0.0L;
        double sampleRate = 0.0;
    };

    explicit NativeStream (GaplessAudioSource& source) : owner (source) {}
    void prepareToPlay (int, double) override {}
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();
        const auto count = juce::jmin (info.numSamples, available);
        for (int channel = 0; channel < juce::jmin (maximumOutputChannels, info.buffer->getNumChannels()); ++channel)
            info.buffer->copyFrom (channel, info.startSample, samples, channel, offset, count);
        offset += count;
        available -= count;
    }

    bool fill (int minimumFrames, long double throughOutputFrame);
    double ratioAcrossBoundary (long double outputFrame) const
    {
        const auto end = outputFrame + 1.0L;
        long double distance = 0.0L;
        for (const auto& span : spans)
        {
            const auto through = juce::jmin (end, span.outputEnd);
            if (through <= outputFrame) continue;
            distance += (through - outputFrame) * span.sampleRate / owner.deviceSampleRate;
            outputFrame = through;
            if (outputFrame >= end) break;
        }
        if (outputFrame < end)
            distance += (end - outputFrame) * spans.back().sampleRate / owner.deviceSampleRate;
        return static_cast<double> (distance);
    }

    GaplessAudioSource& owner;
    std::deque<Span> spans;
    juce::AudioBuffer<float> samples { maximumOutputChannels, chunkFrames * 2 };
    int offset = 0, available = 0;
    long double decodedOutputFrames = 0.0L;
    bool ended = false;
    std::optional<uint64_t> writtenSequence;
};

GaplessAudioSource::GaplessAudioSource (juce::AudioFormatManager& formatsToUse,
                                        juce::TimeSliceThread& threadToUse)
    : formats (formatsToUse), readAheadThread (threadToUse)
{
    for (auto& buffer : buffers) buffer = std::make_unique<PlaybackBuffer>();
    rendering = std::make_unique<RenderState>();
    rendering->queue = buffers.front().get();
}

GaplessAudioSource::~GaplessAudioSource()
{
    stopPreparing();
}

void GaplessAudioSource::stopPreparing()
{
    readAheadThread.removeTimeSliceClient (this);
    rendering->resampler.reset();
    rendering->nativeStream.reset();
    rendering->futureCutoverFrame.reset();
}

int GaplessAudioSource::indexForSequence (uint64_t sequence) const noexcept
{
    if (sequence < rendering->plan.items.size())
        return static_cast<int> (sequence);
    if (rendering->plan.repeatFrom < 0)
        return -1;
    const auto cycleSize = rendering->plan.items.size() - static_cast<size_t> (rendering->plan.repeatFrom);
    return rendering->plan.repeatFrom + static_cast<int> ((sequence - rendering->plan.items.size()) % cycleSize);
}

bool GaplessAudioSource::repeatCycleUnavailable() const
{
    return rendering->plan.repeatFrom >= 0
        && std::all_of (rendering->unavailable.begin() + rendering->plan.repeatFrom, rendering->unavailable.end(),
                        [] (bool failed) { return failed; });
}

void GaplessAudioSource::recordFailure (uint64_t sequence, const juce::String& error,
                                        bool hadBufferedAudio)
{
    const auto index = indexForSequence (sequence);
    if (index < 0) return;
    rendering->unavailable[static_cast<size_t> (index)] = true;
    const std::lock_guard lock (failureMutex);
    failures.push_back ({ rendering->generation, sequence,
                          rendering->plan.items[static_cast<size_t> (index)].token, error,
                          hadBufferedAudio });
}

bool GaplessAudioSource::prepareDecoder (uint64_t sequence, double startSeconds)
{
    const auto index = indexForSequence (sequence);
    if (index < 0) return false;
    const auto& item = rendering->plan.items[static_cast<size_t> (index)];
    auto next = std::make_unique<Decoder>();
    if (rendering->decoder && rendering->decoder->boundary.sequence == sequence
        && rendering->decoder->source)
    {
        next->sampleRate = rendering->decoder->sampleRate;
        next->lengthFrames = rendering->decoder->lengthFrames;
        next->source = std::move (rendering->decoder->source);
        next->source->setDestinationLayout (destinationLayout);
    }
    else if (const auto cached = rendering->cachedTracks[static_cast<size_t> (index)])
    {
        next->cached = cached;
        next->sampleRate = cached->sampleRate;
        next->lengthFrames = cached->samples.getNumSamples();
    }
    else
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (item.file));
        if (! reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            recordFailure (sequence, "Could not prepare audio file: " + item.file.getFullPathName());
            return false;
        }
        next->sampleRate = reader->sampleRate;
        next->lengthFrames = reader->lengthInSamples;
        next->source = std::make_unique<ChannelMappedAudioSource> (
            std::move (reader), destinationLayout);
        next->source->prepareToPlay (chunkFrames, next->sampleRate);
    }
    const auto lengthSeconds = static_cast<double> (next->lengthFrames) / next->sampleRate;
    startSeconds = juce::jlimit (0.0, lengthSeconds, startSeconds);
    next->firstFrame = next->emittedFrames = juce::jlimit (juce::int64{}, next->lengthFrames,
        static_cast<juce::int64> (std::llround (startSeconds * next->sampleRate)));
    if (next->source)
        next->source->setNextReadPosition (next->firstFrame);
    const auto start = rendering->nativeStream->decodedOutputFrames;
    const auto offset = static_cast<juce::int64> (std::llround (start));
    const auto end = start + static_cast<long double> (next->lengthFrames - next->firstFrame)
                               * deviceSampleRate / next->sampleRate;
    next->boundary = { offset,
        startSeconds * deviceSampleRate + static_cast<double> (static_cast<long double> (offset) - start),
        sequence, item.token, lengthSeconds, end, item.normalizationGain.get() };
    rendering->nativeStream->spans.push_back ({ next->boundary, start, end, next->sampleRate });
    rendering->decoder = std::move (next);
    return true;
}

bool GaplessAudioSource::NativeStream::fill (int minimumFrames, long double throughOutputFrame)
{
    if (offset > 0)
    {
        for (int channel = 0; channel < maximumOutputChannels; ++channel)
            std::memmove (samples.getWritePointer (channel), samples.getReadPointer (channel, offset),
                          static_cast<size_t> (available) * sizeof (float));
        offset = 0;
    }
    while (! ended && (available < minimumFrames || decodedOutputFrames < throughOutputFrame))
    {
        if (! owner.rendering->decoder)
        {
            auto index = owner.indexForSequence (owner.rendering->producerSequence);
            while (index >= 0 && owner.rendering->unavailable[static_cast<size_t> (index)])
            {
                if (index >= owner.rendering->plan.repeatFrom && owner.repeatCycleUnavailable())
                {
                    index = -1;
                    break;
                }
                index = owner.indexForSequence (++owner.rendering->producerSequence);
            }
            if (index < 0)
            {
                ended = true;
                break;
            }
            if (! owner.prepareDecoder (owner.rendering->producerSequence, 0.0))
            {
                ++owner.rendering->producerSequence;
                continue;
            }
        }
        auto& track = *owner.rendering->decoder;
        const auto count = static_cast<int> (juce::jmin (
            track.lengthFrames - track.emittedFrames, static_cast<juce::int64> (chunkFrames)));
        if (count == 0)
        {
            owner.rendering->decoder.reset();
            ++owner.rendering->producerSequence;
            continue;
        }
        if (available + count > samples.getNumSamples())
            samples.setSize (maximumOutputChannels, juce::jmax (available + count, samples.getNumSamples() * 2), true);
        if (track.cached)
        {
            for (int channel = 0; channel < maximumOutputChannels; ++channel)
                samples.copyFrom (channel, available, track.cached->samples, channel,
                                  static_cast<int> (track.emittedFrames), count);
        }
        else
        {
            track.source->getNextAudioBlock ({ &samples, available, count });
            if (track.source->hasReadError())
            {
                const auto isCurrent = owner.rendering->producerSequence == owner.rendering->queue->snapshot().progress.sequence;
                owner.recordFailure (owner.rendering->producerSequence, "Audio read failed while preparing playback.",
                    isCurrent ? owner.rendering->producedFrames > track.boundary.streamOffset
                              : track.emittedFrames > track.firstFrame);
                spans.back().outputEnd = decodedOutputFrames;
                if (track.emittedFrames == track.firstFrame)
                    spans.pop_back();
                if (isCurrent)
                {
                    owner.rendering->queue->readFailed.store (true);
                    ended = true;
                    return false;
                }
                owner.rendering->decoder.reset();
                ++owner.rendering->producerSequence;
                continue;
            }
            if (! owner.rendering->futureCutoverFrame && track.emittedFrames == 0 && count == track.lengthFrames
                && owner.rendering->cachedFrames + count <= bufferFrames)
            {
                auto cached = std::make_shared<CachedTrack>();
                cached->samples.setSize (maximumOutputChannels, count);
                for (int channel = 0; channel < maximumOutputChannels; ++channel)
                    cached->samples.copyFrom (channel, 0, samples, channel, available, count);
                cached->sampleRate = track.sampleRate;
                owner.rendering->cachedTracks[static_cast<size_t> (owner.indexForSequence (owner.rendering->producerSequence))]
                    = std::move (cached);
                owner.rendering->cachedFrames += count;
            }
        }
        track.emittedFrames += count;
        available += count;
        decodedOutputFrames = spans.back().outputStart
            + static_cast<long double> (track.emittedFrames - track.firstFrame)
                * owner.deviceSampleRate / track.sampleRate;
    }
    return true;
}

bool GaplessAudioSource::resetAt (uint64_t sequence, double startSeconds, PlaybackBuffer* target)
{
    const auto previous = progress();
    stopPreparing();
    rendering->queue = target != nullptr ? target : playbackBuffer();
    rendering->producedFrames = 0;
    consumedFrames.store (0);
    rendering->producerSequence = sequence;
    const auto index = indexForSequence (sequence);
    rendering->queue->reset (rendering->generation, deviceSampleRate, 0,
        { 0, 0, sequence, index >= 0 ? rendering->plan.items[static_cast<size_t> (index)].token : 0, 0.0 });
    rendering->nativeStream = std::make_unique<NativeStream> (*this);
    if (! prepareDecoder (sequence, startSeconds))
    {
        rendering->queue->readFailed.store (true);
        loaded.store (false);
        rendering->queue->finalFrame.store (0);
        if (previous.generation == rendering->generation && previous.sequence == sequence)
        {
            const auto position = juce::jlimit (0.0, previous.lengthSeconds, startSeconds);
            rendering->queue->publish ({ 0, position * deviceSampleRate, sequence,
                previous.token, previous.lengthSeconds, 0.0L }, 0);
        }
        return false;
    }
    rendering->resampler = std::make_unique<juce::ResamplingAudioSource> (rendering->nativeStream.get(), false, maximumOutputChannels);
    rendering->resampler->setResamplingRatio (rendering->decoder->sampleRate / deviceSampleRate);
    rendering->resampler->prepareToPlay (chunkFrames, deviceSampleRate);
    rendering->queue->activeBoundary = rendering->decoder->boundary;
    rendering->queue->publish (rendering->decoder->boundary, 0);
    loaded.store (true);
    readAheadThread.addTimeSliceClient (this);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    while (rendering->queue->audioFifo.getNumReady() < chunkFrames * 2
           && rendering->queue->finalFrame.load() == unknownEnd && ! rendering->queue->readFailed.load()
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (1);
    return ! rendering->queue->readFailed.load();
}

bool GaplessAudioSource::loadPlan (AudioPlaybackPlan nextPlan, double startSeconds)
{
    if (nextPlan.items.empty() || nextPlan.repeatFrom < -1
        || nextPlan.repeatFrom >= static_cast<int> (nextPlan.items.size()))
        return false;
    stopPreparing();
    rendering->decoder.reset();
    rendering->plan = std::move (nextPlan);
    rendering->unavailable.assign (rendering->plan.items.size(), false);
    rendering->cachedTracks.clear();
    rendering->cachedTracks.resize (rendering->plan.items.size());
    rendering->cachedFrames = 0;
    ++rendering->generation;
    return resetAt (0, startSeconds);
}

GaplessAudioSource::PlaybackBuffer* GaplessAudioSource::playbackBuffer() const noexcept
{
    return buffers[static_cast<size_t> ((bufferState.load (std::memory_order_acquire) >> 1) & 1u)].get();
}

void GaplessAudioSource::prepareFutureRenderer (long double firstFrame)
{
    rendering->resampler.reset();
    rendering->nativeStream.reset();
    rendering->decoder.reset();
    rendering->futureCutoverFrame.reset();
    rendering->producedFrames = static_cast<juce::int64> (std::llround (firstFrame));
    rendering->producerSequence = 1;
    rendering->nativeStream = std::make_unique<NativeStream> (*this);
    rendering->nativeStream->decodedOutputFrames = firstFrame;
    rendering->nativeStream->writtenSequence = 0;
    rendering->resampler = std::make_unique<juce::ResamplingAudioSource> (
        rendering->nativeStream.get(), false, maximumOutputChannels);
    rendering->resampler->prepareToPlay (chunkFrames, deviceSampleRate);
}

bool GaplessAudioSource::setUpcoming (std::vector<AudioPlaybackItem> upcoming,
                                      int repeatFrom, std::optional<AudioPlaybackProgress> expected)
{
    if (! isLoaded() || repeatFrom < -1 || repeatFrom > static_cast<int> (upcoming.size())) return false;
    readAheadThread.removeTimeSliceClient (this);
    auto& previous = *playbackBuffer();
    const auto current = previous.snapshot();
    const auto index = indexForSequence (current.progress.sequence);
    const auto matches = [] (const AudioPlaybackProgress& a, const AudioPlaybackProgress& b)
    { return a.generation == b.generation && a.sequence == b.sequence && a.token == b.token; };
    if (index < 0 || (expected && ! matches (*expected, current.progress)))
    {
        if (previous.finalFrame.load() == unknownEnd) readAheadThread.addTimeSliceClient (this);
        return false;
    }
    auto& next = *buffers[&previous == buffers[0].get() ? 1 : 0];
    const auto exactEnd = current.boundary.renderedEnd;
    const auto currentEnd = static_cast<juce::int64> (std::llround (exactEnd));
    const auto completedCurrent = rendering->producedFrames >= currentEnd;
    const auto reachedFuture = rendering->producerSequence > current.progress.sequence
        || (! rendering->nativeStream->spans.empty()
            && rendering->nativeStream->spans.back().boundary.sequence > current.progress.sequence);
    const auto preservedEnd = juce::jmin (rendering->producedFrames, currentEnd);
    auto boundary = current.boundary;
    boundary.normalizationGain = rendering->plan.items[static_cast<size_t> (index)].normalizationGain.get();
    boundary.sequence = 0;
    next.reset (rendering->generation + 1, deviceSampleRate, current.frame, boundary);
    const auto preserved = static_cast<int> (juce::jmax (juce::int64{}, preservedEnd - current.frame));
    const auto sourceOffset = static_cast<int> ((current.frame - previous.baseFrame) % (bufferFrames + 1));
    const auto first = juce::jmin (preserved, bufferFrames + 1 - sourceOffset);
    for (int channel = 0; channel < maximumOutputChannels; ++channel)
    {
        next.audio.copyFrom (channel, 0, previous.audio, channel, sourceOffset, first);
        next.audio.copyFrom (channel, first, previous.audio, channel, 0, preserved - first);
    }
    next.audioFifo.finishedWrite (preserved);

    auto prior = std::move (rendering);
    rendering = std::make_unique<RenderState>();
    rendering->queue = &next;
    rendering->generation = next.generation;
    upcoming.insert (upcoming.begin(), prior->plan.items[static_cast<size_t> (index)]);
    rendering->plan = { std::move (upcoming), repeatFrom };
    rendering->unavailable.assign (rendering->plan.items.size(), false);
    rendering->cachedTracks.resize (rendering->plan.items.size());
    rendering->cachedTracks[0] = prior->cachedTracks[static_cast<size_t> (index)];
    rendering->cachedFrames = rendering->cachedTracks[0] ? rendering->cachedTracks[0]->samples.getNumSamples() : 0;
    if (completedCurrent)
        prepareFutureRenderer (exactEnd);
    else
    {
        // No decoding occurs before commit on this path, so a rejected publication
        // can return the exact current reader/resampler and its clock to the old queue.
        rendering->decoder = std::move (prior->decoder);
        rendering->nativeStream = std::move (prior->nativeStream);
        rendering->resampler = std::move (prior->resampler);
        rendering->producedFrames = prior->producedFrames;
        rendering->producerSequence = prior->producerSequence - current.progress.sequence;
        if (rendering->decoder) rendering->decoder->boundary.sequence -= current.progress.sequence;
        for (auto& span : rendering->nativeStream->spans) span.boundary.sequence -= current.progress.sequence;
        rendering->nativeStream->writtenSequence = 0;
        if (reachedFuture) rendering->futureCutoverFrame = exactEnd;
    }
    const auto reject = [&]
    {
        if (! completedCurrent)
        {
            if (rendering->decoder) rendering->decoder->boundary.sequence += current.progress.sequence;
            for (auto& span : rendering->nativeStream->spans) span.boundary.sequence += current.progress.sequence;
            rendering->nativeStream->writtenSequence = current.progress.sequence;
            prior->decoder = std::move (rendering->decoder);
            prior->nativeStream = std::move (rendering->nativeStream);
            prior->resampler = std::move (rendering->resampler);
        }
        {
            const std::lock_guard lock (failureMutex);
            std::erase_if (failures, [&] (const auto& failure) { return failure.generation == next.generation; });
        }
        rendering = std::move (prior);
        if (previous.finalFrame.load() == unknownEnd && ! previous.readFailed.load())
            readAheadThread.addTimeSliceClient (this);
        return false;
    };
    for (;;)
    {
        auto state = bufferState.load (std::memory_order_acquire);
        if ((state & 1u) != 0) { juce::Thread::sleep (1); continue; }
        const auto latest = previous.snapshot();
        if (! matches (latest.progress, current.progress)) return reject();
        // Cover the current read clock as well as the normal prefill window.
        if (completedCurrent && next.finalFrame.load() == unknownEnd && ! next.readFailed.load()
            && rendering->producedFrames < latest.frame + chunkFrames * 2
            && next.audioFifo.getFreeSpace() > 0)
        {
            if (produceChunk()) continue;
        }
        next.publish (boundary, latest.frame);
        // The serial changes at every callback entry/exit. A callback that started
        // after the snapshot forces a recheck, without ever waiting on the audio thread.
        if (bufferState.compare_exchange_weak (state, (state + 4u) ^ 2u,
                                              std::memory_order_acq_rel))
            break;
    }
    if (next.finalFrame.load() == unknownEnd && ! next.readFailed.load())
        readAheadThread.addTimeSliceClient (this);
    return true;
}

bool GaplessAudioSource::seekToSeconds (double seconds)
{
    if (! isLoaded() || ! std::isfinite (seconds)) return false;
    const auto current = progress();
    return resetAt (current.sequence, seconds);
}

void GaplessAudioSource::clear()
{
    stopPreparing();
    rendering->decoder.reset();
    loaded.store (false);
    rendering->plan = {};
    rendering->unavailable.clear();
    rendering->cachedTracks.clear();
    rendering->cachedFrames = 0;
    rendering->producedFrames = 0;
    consumedFrames.store (0);
    rendering->queue = playbackBuffer();
    rendering->queue->reset (rendering->generation, deviceSampleRate, 0, {});
    rendering->queue->finalFrame.store (0);
}

void GaplessAudioSource::setDestinationLayout (juce::AudioChannelSet layout)
{
    if (destinationLayout == layout) return;
    const auto current = progress();
    stopPreparing();
    destinationLayout = std::move (layout);
    for (auto& cached : rendering->cachedTracks) cached.reset();
    rendering->cachedFrames = 0;
    if (isLoaded())
        static_cast<void> (resetAt (current.sequence, current.positionSeconds));
}

juce::Result GaplessAudioSource::reconfigureOutput (juce::AudioChannelSet layout, double sampleRate,
                                                   const std::function<juce::Result()>& prepareOutput)
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0)
        return juce::Result::fail ("The output sample rate is invalid.");
    if (destinationLayout == layout && juce::approximatelyEqual (deviceSampleRate, sampleRate))
        return prepareOutput ? prepareOutput() : juce::Result::ok();

    if (! isLoaded())
    {
        if (prepareOutput)
            if (const auto result = prepareOutput(); result.failed()) return result;
        setDestinationLayout (std::move (layout));
        deviceSampleRate = sampleRate;
        return juce::Result::ok();
    }

    readAheadThread.removeTimeSliceClient (this);
    const auto previousProgress = progress();
    const auto previousLayout = destinationLayout;
    const auto previousRate = deviceSampleRate;
    const auto previousConsumed = consumedFrames.load();
    std::vector<AudioPreparationFailure> previousFailures;
    {
        const std::lock_guard lock (failureMutex);
        previousFailures = failures;
    }
    auto previous = std::move (rendering);
    bool committed = false;
    const juce::ScopeGuard restorePrevious { [&]
    {
        if (committed) return;
        readAheadThread.removeTimeSliceClient (this);
        rendering = std::move (previous);
        destinationLayout = previousLayout;
        deviceSampleRate = previousRate;
        consumedFrames.store (previousConsumed);
        loaded.store (true);
        {
            const std::lock_guard lock (failureMutex);
            failures = std::move (previousFailures);
        }
        if (rendering->queue->finalFrame.load() == unknownEnd && ! rendering->queue->readFailed.load())
            readAheadThread.addTimeSliceClient (this);
    } };

    rendering = std::make_unique<RenderState>();
    rendering->plan = previous->plan;
    rendering->generation = previous->generation;
    rendering->unavailable = previous->unavailable;
    rendering->cachedTracks.resize (previous->cachedTracks.size());
    if (layout == previousLayout)
    {
        rendering->cachedTracks = previous->cachedTracks;
        rendering->cachedFrames = previous->cachedFrames;
    }
    if (previous->decoder && previous->decoder->source
        && previous->decoder->boundary.sequence == previousProgress.sequence)
    {
        auto& decoder = rendering->decoder = std::make_unique<Decoder>();
        decoder->source = previous->decoder->source->forkForLayout (layout);
        decoder->sampleRate = previous->decoder->sampleRate;
        decoder->lengthFrames = previous->decoder->lengthFrames;
        decoder->boundary = previous->decoder->boundary;
    }
    destinationLayout = std::move (layout);
    deviceSampleRate = sampleRate;
    const auto active = bufferState.load (std::memory_order_acquire);
    jassert ((active & 1u) == 0);
    auto* stagingBuffer = buffers[static_cast<size_t> (((active >> 1) & 1u) ^ 1u)].get();
    if (! resetAt (previousProgress.sequence, previousProgress.positionSeconds, stagingBuffer))
        return juce::Result::fail ("The playback source could not prepare the requested output format. Previous audio was restored.");
    if (prepareOutput)
        if (const auto result = prepareOutput(); result.failed()) return result;

    // The callback is suspended. Publish only after preparation succeeds, keeping
    // the old PCM, resampler and decoder available throughout the transaction.
    bufferState.store ((active + 4u) ^ 2u, std::memory_order_release);
    committed = true;
    return juce::Result::ok();
}

bool GaplessAudioSource::isLoaded() const noexcept
{
    return loaded.load (std::memory_order_acquire);
}

void GaplessAudioSource::prepareToPlay (int, double sampleRate)
{
    if (sampleRate <= 0.0) return;
    if (! juce::approximatelyEqual (deviceSampleRate, sampleRate))
    {
        const auto result = reconfigureOutput (destinationLayout, sampleRate);
        if (result.failed()) throw std::runtime_error (result.getErrorMessage().toStdString());
    }
    else if (isLoaded())
    {
        readAheadThread.removeTimeSliceClient (this);
        if (rendering->queue->finalFrame.load() == unknownEnd)
            readAheadThread.addTimeSliceClient (this);
    }
}

void GaplessAudioSource::releaseResources()
{
    readAheadThread.removeTimeSliceClient (this);
}

bool GaplessAudioSource::produceChunk()
{
    auto throughFrame = std::numeric_limits<juce::int64>::max();
    if (rendering->futureCutoverFrame)
    {
        const auto cut = static_cast<juce::int64> (std::llround (*rendering->futureCutoverFrame));
        if (rendering->producedFrames >= cut)
            prepareFutureRenderer (*rendering->futureCutoverFrame);
        else
            throughFrame = juce::jmin (throughFrame, cut);
    }
    auto& stream = *rendering->nativeStream;
    auto& queue = *rendering->queue;
    const auto available = juce::jmin (chunkFrames, queue.audioFifo.getFreeSpace());
    const auto maximum = static_cast<int> (juce::jmin (static_cast<juce::int64> (available),
                                                     throughFrame - rendering->producedFrames));
    if (maximum == 0 || queue.boundaryFifo.getFreeSpace() == 0) return false;
    if (! stream.fill (0, static_cast<long double> (rendering->producedFrames) + maximum + 1))
    {
        queue.finalFrame.store (rendering->producedFrames);
        return false;
    }
    const auto endFrame = static_cast<juce::int64> (std::llround (stream.decodedOutputFrames));
    if (stream.ended && rendering->producedFrames >= endFrame)
    {
        queue.finalFrame.store (rendering->producedFrames);
        return false;
    }
    while (stream.spans.size() > 1 && stream.spans[1].outputStart <= static_cast<long double> (rendering->producedFrames))
        stream.spans.pop_front();
    if (stream.spans.empty())
    {
        queue.finalFrame.store (rendering->producedFrames);
        return false;
    }
    const auto& span = stream.spans.front();
    auto count = static_cast<int> (juce::jmin (static_cast<juce::int64> (maximum),
        static_cast<juce::int64> (std::floor (span.outputEnd)) - rendering->producedFrames));
    auto ratio = span.sampleRate / deviceSampleRate;
    if (count <= 0)
    {
        // A rate boundary may lie inside one device-frame interval. Advance by
        // its actual native distance so the next track inherits the same phase.
        count = 1;
        ratio = stream.ratioAcrossBoundary (static_cast<long double> (rendering->producedFrames));
    }
    if (stream.ended)
        count = static_cast<int> (juce::jmin (static_cast<juce::int64> (count), endFrame - rendering->producedFrames));
    // JUCE requests round(output * ratio) + 3 native samples, including lookahead.
    if (! stream.fill (juce::roundToInt (count * ratio) + 3, 0.0L))
    {
        queue.finalFrame.store (rendering->producedFrames);
        return false;
    }
    auto boundary = span.boundary;
    for (const auto& candidate : stream.spans)
    {
        if (candidate.boundary.streamOffset > rendering->producedFrames) break;
        boundary = candidate.boundary;
    }
    rendering->resampler->setResamplingRatio (ratio);
    rendering->resampler->getNextAudioBlock ({ &renderBuffer, 0, count });
    if (stream.writtenSequence != boundary.sequence)
    {
        const auto write = queue.boundaryFifo.write (1);
        queue.boundaries[static_cast<size_t> (write.startIndex1)] = boundary;
        stream.writtenSequence = boundary.sequence;
    }
    const auto write = queue.audioFifo.write (count);
    for (int channel = 0; channel < queue.audio.getNumChannels(); ++channel)
    {
        queue.audio.copyFrom (channel, write.startIndex1,
                              renderBuffer.getReadPointer (channel), write.blockSize1);
        queue.audio.copyFrom (channel, write.startIndex2,
                              renderBuffer.getReadPointer (channel) + write.blockSize1, write.blockSize2);
    }
    rendering->producedFrames += count;
    return true;
}

int GaplessAudioSource::useTimeSlice()
{
    for (int operation = 0; operation < 1024; ++operation)
    {
        const auto advanced = produceChunk();
        if (rendering->queue->readFailed.load() || rendering->queue->finalFrame.load() != unknownEnd) return -1;
        if (! advanced) return 2;
    }
    return 0;
}

AudioPlaybackProgress GaplessAudioSource::progress() const noexcept
{
    auto result = playbackBuffer()->snapshot().progress;
    result.finished = isLoaded() && result.finished;
    return result;
}

std::vector<AudioPreparationFailure> GaplessAudioSource::drainPreparationFailures()
{
    const std::lock_guard lock (failureMutex);
    std::vector<AudioPreparationFailure> result;
    result.swap (failures);
    return result;
}

void GaplessAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    auto state = bufferState.load (std::memory_order_acquire);
    while (! bufferState.compare_exchange_weak (state, state | 1u, std::memory_order_acq_rel)) {}
    auto& queue = *buffers[static_cast<size_t> ((state >> 1) & 1u)];
    const auto position = consumedFrames.load (std::memory_order_relaxed);
    const auto skip = static_cast<int> (juce::jmin (
        juce::jmax (juce::int64{}, position - queue.nextReadFrame),
        static_cast<juce::int64> (queue.audioFifo.getNumReady())));
    const auto count = juce::jmin (info.numSamples, queue.audioFifo.getNumReady() - skip);
    const auto offset = static_cast<int> ((queue.nextReadFrame + skip - queue.baseFrame) % (bufferFrames + 1));
    const auto first = juce::jmin (count, bufferFrames + 1 - offset);
    for (int channel = 0; channel < juce::jmin (maximumOutputChannels, info.buffer->getNumChannels()); ++channel)
    {
        info.buffer->copyFrom (channel, info.startSample, queue.audio, channel, offset, first);
        info.buffer->copyFrom (channel, info.startSample + first, queue.audio, channel, 0, count - first);
    }
    const auto end = position + count;
    auto firstBoundary = queue.activeBoundary;
    queue.nextReadFrame += skip + count;
    auto gainPosition = position;
    const auto applyNormalization = [&] (juce::int64 until)
    {
        const auto samples = static_cast<int> (until - gainPosition);
        if (samples <= 0) return;
        const auto& boundary = queue.activeBoundary;
        const auto target = boundary.normalizationGain != nullptr
            ? boundary.normalizationGain->load (std::memory_order_relaxed) : 1.0f;
        if (normalizationGeneration != queue.generation || normalizationToken != boundary.token)
        {
            normalizationGeneration = queue.generation;
            normalizationToken = boundary.token;
            normalizationRamp.reset (deviceSampleRate, 0.02);
            normalizationRamp.setCurrentAndTargetValue (target);
        }
        normalizationRamp.setTargetValue (target);
        const auto firstGain = normalizationRamp.getCurrentValue();
        const auto lastGain = normalizationRamp.skip (samples);
        info.buffer->applyGainRamp (info.startSample + static_cast<int> (gainPosition - position),
                                   samples, firstGain, lastGain);
        gainPosition = until;
    };
    for (;;)
    {
        int start1, size1, start2, size2;
        queue.boundaryFifo.prepareToRead (1, start1, size1, start2, size2);
        if (size1 == 0) break;
        const auto& next = queue.boundaries[static_cast<size_t> (start1)];
        if (next.streamOffset >= end) break;
        applyNormalization (juce::jmax (gainPosition, next.streamOffset));
        if (next.streamOffset <= position) firstBoundary = next;
        queue.activeBoundary = next;
        queue.boundaryFifo.finishedRead (1);
    }
    applyNormalization (end);
    lastBlockStartPositionSeconds = juce::jlimit (0.0, firstBoundary.lengthSeconds,
        (firstBoundary.startFrame + static_cast<double> (position - firstBoundary.streamOffset)) / queue.sampleRate);
    consumedFrames.store (end, std::memory_order_release);
    queue.publish (queue.activeBoundary, end);
    // Publish the matching read clock before the producer may overwrite consumed PCM.
    queue.audioFifo.finishedRead (skip + count);
    bufferState.store (state + 4u, std::memory_order_release);
}

void GaplessAudioSource::setNextReadPosition (juce::int64 newPosition)
{
    if (isLoaded())
        static_cast<void> (seekToSeconds (static_cast<double> (newPosition) / deviceSampleRate));
}

juce::int64 GaplessAudioSource::getNextReadPosition() const
{
    return consumedFrames.load (std::memory_order_acquire);
}

juce::int64 GaplessAudioSource::getTotalLength() const
{
    return isLoaded() ? playbackBuffer()->finalFrame.load (std::memory_order_acquire) : 0;
}
