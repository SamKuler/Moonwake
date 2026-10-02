#include "GaplessAudioSource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <optional>

namespace
{
constexpr auto unknownEnd = std::numeric_limits<juce::int64>::max() / 4;
}

struct GaplessAudioSource::CachedTrack
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 0.0;
};

struct GaplessAudioSource::Decoder
{
    std::unique_ptr<ChannelMappedAudioSource> source;
    const CachedTrack* cached = nullptr;
    Boundary boundary;
    double sampleRate = 0.0;
    juce::int64 lengthFrames = 0;
    juce::int64 emittedFrames = 0;
    juce::int64 firstFrame = 0;
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
    : formats (formatsToUse), readAheadThread (threadToUse),
      boundaries (bufferFrames + 1)
{
    finalFrame.store (unknownEnd);
}

GaplessAudioSource::~GaplessAudioSource()
{
    stopPreparing();
}

void GaplessAudioSource::stopPreparing()
{
    readAheadThread.removeTimeSliceClient (this);
    resampler.reset();
    nativeStream.reset();
    decoder.reset();
}

int GaplessAudioSource::indexForSequence (uint64_t sequence) const noexcept
{
    if (sequence < plan.items.size())
        return static_cast<int> (sequence);
    if (plan.repeatFrom < 0)
        return -1;
    const auto cycleSize = plan.items.size() - static_cast<size_t> (plan.repeatFrom);
    return plan.repeatFrom + static_cast<int> ((sequence - plan.items.size()) % cycleSize);
}

bool GaplessAudioSource::repeatCycleUnavailable() const
{
    return plan.repeatFrom >= 0
        && std::all_of (unavailable.begin() + plan.repeatFrom, unavailable.end(),
                        [] (bool failed) { return failed; });
}

void GaplessAudioSource::recordFailure (uint64_t sequence, const juce::String& error,
                                        bool hadBufferedAudio)
{
    const auto index = indexForSequence (sequence);
    if (index < 0) return;
    unavailable[static_cast<size_t> (index)] = true;
    const std::lock_guard lock (failureMutex);
    failures.push_back ({ generation, sequence,
                          plan.items[static_cast<size_t> (index)].token, error,
                          hadBufferedAudio });
}

bool GaplessAudioSource::prepareDecoder (uint64_t sequence, double startSeconds)
{
    const auto index = indexForSequence (sequence);
    if (index < 0) return false;
    const auto& item = plan.items[static_cast<size_t> (index)];
    auto next = std::make_unique<Decoder>();
    if (const auto* cached = cachedTracks[static_cast<size_t> (index)].get())
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
    const auto start = nativeStream->decodedOutputFrames;
    const auto offset = static_cast<juce::int64> (std::llround (start));
    next->boundary = { offset,
        startSeconds * deviceSampleRate + static_cast<double> (static_cast<long double> (offset) - start),
        sequence, item.token, lengthSeconds };
    nativeStream->spans.push_back ({ next->boundary,
        start, start + static_cast<long double> (next->lengthFrames - next->firstFrame)
                         * deviceSampleRate / next->sampleRate,
        next->sampleRate });
    decoder = std::move (next);
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
        if (! owner.decoder)
        {
            auto index = owner.indexForSequence (owner.producerSequence);
            while (index >= 0 && owner.unavailable[static_cast<size_t> (index)])
            {
                if (index >= owner.plan.repeatFrom && owner.repeatCycleUnavailable())
                {
                    index = -1;
                    break;
                }
                index = owner.indexForSequence (++owner.producerSequence);
            }
            if (index < 0)
            {
                ended = true;
                break;
            }
            if (! owner.prepareDecoder (owner.producerSequence, 0.0))
            {
                ++owner.producerSequence;
                continue;
            }
        }
        auto& track = *owner.decoder;
        const auto count = static_cast<int> (juce::jmin (
            track.lengthFrames - track.emittedFrames, static_cast<juce::int64> (chunkFrames)));
        if (count == 0)
        {
            owner.decoder.reset();
            ++owner.producerSequence;
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
                const auto isCurrent = owner.producerSequence == owner.currentSequence.load();
                owner.recordFailure (owner.producerSequence, "Audio read failed while preparing playback.",
                    isCurrent ? owner.producedFrames > track.boundary.streamOffset
                              : track.emittedFrames > track.firstFrame);
                spans.back().outputEnd = decodedOutputFrames;
                if (track.emittedFrames == track.firstFrame)
                    spans.pop_back();
                if (isCurrent)
                {
                    owner.readFailed.store (true);
                    ended = true;
                    return false;
                }
                owner.decoder.reset();
                ++owner.producerSequence;
                continue;
            }
            if (track.emittedFrames == 0 && count == track.lengthFrames
                && owner.cachedFrames + count <= bufferFrames)
            {
                auto cached = std::make_unique<CachedTrack>();
                cached->samples.setSize (maximumOutputChannels, count);
                for (int channel = 0; channel < maximumOutputChannels; ++channel)
                    cached->samples.copyFrom (channel, 0, samples, channel, available, count);
                cached->sampleRate = track.sampleRate;
                owner.cachedTracks[static_cast<size_t> (owner.indexForSequence (owner.producerSequence))]
                    = std::move (cached);
                owner.cachedFrames += count;
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

bool GaplessAudioSource::resetAt (uint64_t sequence, double startSeconds)
{
    stopPreparing();
    audioFifo.reset();
    boundaryFifo.reset();
    producedFrames = 0;
    producerSequence = sequence;
    finalFrame.store (unknownEnd);
    readFailed.store (false);
    const auto index = indexForSequence (sequence);
    activeBoundary = { 0, 0, sequence,
        index >= 0 ? plan.items[static_cast<size_t> (index)].token : 0, 0.0 };
    publish (activeBoundary, 0);
    nativeStream = std::make_unique<NativeStream> (*this);
    if (! prepareDecoder (sequence, startSeconds))
    {
        readFailed.store (true);
        loaded.store (false);
        finalFrame.store (0);
        return false;
    }
    resampler = std::make_unique<juce::ResamplingAudioSource> (nativeStream.get(), false, maximumOutputChannels);
    resampler->setResamplingRatio (decoder->sampleRate / deviceSampleRate);
    resampler->prepareToPlay (chunkFrames, deviceSampleRate);
    activeBoundary = decoder->boundary;
    publish (activeBoundary, 0);
    loaded.store (true);
    readAheadThread.addTimeSliceClient (this);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    while (audioFifo.getNumReady() < chunkFrames * 2
           && finalFrame.load() == unknownEnd && ! readFailed.load()
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (1);
    return ! readFailed.load();
}

bool GaplessAudioSource::loadPlan (AudioPlaybackPlan nextPlan, double startSeconds)
{
    if (nextPlan.items.empty() || nextPlan.repeatFrom < -1
        || nextPlan.repeatFrom >= static_cast<int> (nextPlan.items.size()))
        return false;
    stopPreparing();
    plan = std::move (nextPlan);
    unavailable.assign (plan.items.size(), false);
    cachedTracks.clear();
    cachedTracks.resize (plan.items.size());
    cachedFrames = 0;
    ++generation;
    return resetAt (0, startSeconds);
}

bool GaplessAudioSource::setUpcoming (std::vector<AudioPlaybackItem> upcoming,
                                      int repeatFrom)
{
    const auto current = progress();
    const auto index = indexForSequence (current.sequence);
    if (! isLoaded() || index < 0) return false;
    upcoming.insert (upcoming.begin(), plan.items[static_cast<size_t> (index)]);
    return loadPlan ({ std::move (upcoming), repeatFrom }, current.positionSeconds);
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
    loaded.store (false);
    readFailed.store (false);
    plan = {};
    unavailable.clear();
    cachedTracks.clear();
    cachedFrames = 0;
    audioFifo.reset();
    boundaryFifo.reset();
    producedFrames = 0;
    finalFrame.store (0);
    activeBoundary = {};
    publish (activeBoundary, 0);
}

void GaplessAudioSource::setDestinationLayout (juce::AudioChannelSet layout)
{
    if (destinationLayout == layout) return;
    const auto current = progress();
    stopPreparing();
    destinationLayout = std::move (layout);
    for (auto& cached : cachedTracks) cached.reset();
    cachedFrames = 0;
    if (isLoaded())
        static_cast<void> (resetAt (current.sequence, current.positionSeconds));
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
        const auto current = progress();
        stopPreparing();
        deviceSampleRate = sampleRate;
        for (auto& cached : cachedTracks) cached.reset();
        cachedFrames = 0;
        if (isLoaded())
            static_cast<void> (resetAt (current.sequence, current.positionSeconds));
    }
    else if (isLoaded())
    {
        readAheadThread.removeTimeSliceClient (this);
        if (finalFrame.load() == unknownEnd)
            readAheadThread.addTimeSliceClient (this);
    }
}

void GaplessAudioSource::releaseResources()
{
    readAheadThread.removeTimeSliceClient (this);
}

bool GaplessAudioSource::produceChunk()
{
    auto& stream = *nativeStream;
    const auto maximum = juce::jmin (chunkFrames, audioFifo.getFreeSpace());
    if (maximum == 0 || boundaryFifo.getFreeSpace() == 0) return false;
    if (! stream.fill (0, static_cast<long double> (producedFrames) + maximum + 1))
    {
        finalFrame.store (producedFrames);
        return false;
    }
    const auto endFrame = static_cast<juce::int64> (std::llround (stream.decodedOutputFrames));
    if (stream.ended && producedFrames >= endFrame)
    {
        finalFrame.store (producedFrames);
        return false;
    }
    while (stream.spans.size() > 1 && stream.spans[1].outputStart <= static_cast<long double> (producedFrames))
        stream.spans.pop_front();
    if (stream.spans.empty())
    {
        finalFrame.store (producedFrames);
        return false;
    }
    const auto& span = stream.spans.front();
    auto count = static_cast<int> (juce::jmin (static_cast<juce::int64> (maximum),
        static_cast<juce::int64> (std::floor (span.outputEnd)) - producedFrames));
    auto ratio = span.sampleRate / deviceSampleRate;
    if (count <= 0)
    {
        // A rate boundary may lie inside one device-frame interval. Advance by
        // its actual native distance so the next track inherits the same phase.
        count = 1;
        ratio = stream.ratioAcrossBoundary (static_cast<long double> (producedFrames));
    }
    if (stream.ended)
        count = static_cast<int> (juce::jmin (static_cast<juce::int64> (count), endFrame - producedFrames));
    // JUCE requests round(output * ratio) + 3 native samples, including lookahead.
    if (! stream.fill (juce::roundToInt (count * ratio) + 3, 0.0L))
    {
        finalFrame.store (producedFrames);
        return false;
    }
    auto boundary = span.boundary;
    for (const auto& candidate : stream.spans)
    {
        if (candidate.boundary.streamOffset > producedFrames) break;
        boundary = candidate.boundary;
    }
    resampler->setResamplingRatio (ratio);
    resampler->getNextAudioBlock ({ &renderBuffer, 0, count });
    if (stream.writtenSequence != boundary.sequence)
    {
        const auto write = boundaryFifo.write (1);
        boundaries[static_cast<size_t> (write.startIndex1)] = boundary;
        stream.writtenSequence = boundary.sequence;
    }
    const auto write = audioFifo.write (count);
    for (int channel = 0; channel < audioBuffer.getNumChannels(); ++channel)
    {
        audioBuffer.copyFrom (channel, write.startIndex1,
                              renderBuffer.getReadPointer (channel), write.blockSize1);
        audioBuffer.copyFrom (channel, write.startIndex2,
                              renderBuffer.getReadPointer (channel) + write.blockSize1, write.blockSize2);
    }
    producedFrames += count;
    return true;
}

int GaplessAudioSource::useTimeSlice()
{
    for (int operation = 0; operation < 1024; ++operation)
    {
        const auto advanced = produceChunk();
        if (readFailed.load() || finalFrame.load() != unknownEnd) return -1;
        if (! advanced) return 2;
    }
    return 0;
}

void GaplessAudioSource::publish (const Boundary& boundary,
                                  juce::int64 streamPosition) noexcept
{
    progressVersion.fetch_add (1, std::memory_order_acq_rel);
    consumedFrames.store (streamPosition, std::memory_order_release);
    currentGeneration.store (generation, std::memory_order_relaxed);
    currentSequence.store (boundary.sequence, std::memory_order_relaxed);
    currentToken.store (boundary.token, std::memory_order_relaxed);
    currentLength.store (boundary.lengthSeconds, std::memory_order_relaxed);
    currentPosition.store (juce::jlimit (0.0, boundary.lengthSeconds,
        (boundary.startFrame + static_cast<double> (streamPosition - boundary.streamOffset))
            / deviceSampleRate), std::memory_order_relaxed);
    progressVersion.fetch_add (1, std::memory_order_release);
}

AudioPlaybackProgress GaplessAudioSource::progress() const noexcept
{
    AudioPlaybackProgress result;
    for (;;)
    {
        const auto before = progressVersion.load (std::memory_order_acquire);
        if ((before & 1u) != 0) continue;
        result.generation = currentGeneration.load (std::memory_order_relaxed);
        result.sequence = currentSequence.load (std::memory_order_relaxed);
        result.token = currentToken.load (std::memory_order_relaxed);
        result.positionSeconds = currentPosition.load (std::memory_order_relaxed);
        result.lengthSeconds = currentLength.load (std::memory_order_relaxed);
        result.finished = isLoaded() && consumedFrames.load (std::memory_order_relaxed)
                                        >= finalFrame.load (std::memory_order_acquire);
        result.readError = readFailed.load (std::memory_order_acquire);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (progressVersion.load (std::memory_order_acquire) == before) break;
    }
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
    const auto read = audioFifo.read (info.numSamples);
    const auto count = read.blockSize1 + read.blockSize2;
    for (int channel = 0; channel < juce::jmin (maximumOutputChannels, info.buffer->getNumChannels()); ++channel)
    {
        info.buffer->copyFrom (channel, info.startSample,
            audioBuffer.getReadPointer (channel, read.startIndex1), read.blockSize1);
        info.buffer->copyFrom (channel, info.startSample + read.blockSize1,
            audioBuffer.getReadPointer (channel, read.startIndex2), read.blockSize2);
    }
    const auto end = consumedFrames.load (std::memory_order_relaxed) + count;
    for (;;)
    {
        int start1, size1, start2, size2;
        boundaryFifo.prepareToRead (1, start1, size1, start2, size2);
        if (size1 == 0) break;
        const auto& next = boundaries[static_cast<size_t> (start1)];
        if (next.streamOffset >= end) break;
        activeBoundary = next;
        boundaryFifo.finishedRead (1);
    }
    publish (activeBoundary, end);
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
    return isLoaded() ? finalFrame.load (std::memory_order_acquire) : 0;
}
