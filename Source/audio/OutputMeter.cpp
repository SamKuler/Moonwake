#include "audio/OutputMeter.h"

#include <ebur128.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace
{
// Upstream initialization rewrites process-global energy tables. Serialize all
// library access across meter instances; this mutex is worker-only.
std::mutex ebur128Mutex;

int ebuChannel (juce::AudioChannelSet::ChannelType type)
{
    using C = juce::AudioChannelSet;
    static constexpr std::pair<C::ChannelType, int> positions[] {
        { C::left, EBUR128_Mp030 }, { C::right, EBUR128_Mm030 },
        { C::centre, EBUR128_Mp000 },
        { C::LFE, EBUR128_UNUSED }, { C::LFE2, EBUR128_UNUSED },
        { C::leftSurround, EBUR128_Mp110 }, { C::rightSurround, EBUR128_Mm110 },
        { C::leftSurroundSide, EBUR128_Mp090 }, { C::rightSurroundSide, EBUR128_Mm090 },
        { C::leftSurroundRear, EBUR128_Mp135 }, { C::rightSurroundRear, EBUR128_Mm135 },
        { C::centreSurround, EBUR128_Mp180 },
        { C::leftCentre, EBUR128_MpSC }, { C::rightCentre, EBUR128_MmSC },
        { C::wideLeft, EBUR128_Mp060 }, { C::wideRight, EBUR128_Mm060 },
        { C::topMiddle, EBUR128_Tp000 },
        { C::topFrontLeft, EBUR128_Up030 }, { C::topFrontRight, EBUR128_Um030 },
        { C::topFrontCentre, EBUR128_Up000 },
        { C::topSideLeft, EBUR128_Up090 }, { C::topSideRight, EBUR128_Um090 },
        { C::topRearLeft, EBUR128_Up135 }, { C::topRearRight, EBUR128_Um135 },
        { C::topRearCentre, EBUR128_Up180 }
    };
    for (const auto& [speaker, position] : positions)
        if (speaker == type) return position;
    return -1;
}
} // namespace

struct OutputMeter::Impl final : juce::Thread
{
    static_assert (std::atomic<size_t>::is_always_lock_free);
    static_assert (std::atomic<uint64_t>::is_always_lock_free);
    static_assert (std::atomic_bool::is_always_lock_free);
    static constexpr size_t packetFrames = 512;
    static constexpr size_t queueCapacity = 256;
    struct Packet
    {
        std::array<float, packetFrames * maximumOutputChannels> samples {};
        size_t frames = 0;
        uint64_t epoch = 0;
    };

    Impl() : juce::Thread ("Output loudness measurement"), packets (queueCapacity) {}
    ~Impl() override { stopThread (-1); }

    void publish (const OutputMeterReadings& value, uint64_t publishedEpoch)
    {
        const std::lock_guard lock (readingsMutex);
        readings = value;
        readingsEpoch = publishedEpoch;
    }

    void run() override
    {
        ebur128_state* state = nullptr;
        uint64_t activeEpoch = 0;
        OutputMeterReadings current;
        std::vector<double> rmsHistory;
        std::array<double, maximumOutputChannels> rmsSums {};
        size_t rmsIndex = 0;
        const auto rmsFrames = static_cast<size_t> (std::max (1.0, std::round (sampleRate * 0.3)));
        uint64_t measuredFrames = 0, queryFrames = 0;
        const auto queryInterval = static_cast<uint64_t> (std::max (1.0, sampleRate * 0.05));

        while (! threadShouldExit())
        {
            const auto requestedEpoch = epoch.load (std::memory_order_acquire);
            if (activeEpoch != requestedEpoch)
            {
                // Reclaim stale queued audio before the comparatively expensive
                // filter initialization, leaving room for the new measurement.
                auto stale = readIndex.load (std::memory_order_relaxed);
                while (stale != writeIndex.load (std::memory_order_acquire)
                       && packets[stale % queueCapacity].epoch < requestedEpoch)
                    readIndex.store (++stale, std::memory_order_release);
                const std::lock_guard libraryLock (ebur128Mutex);
                if (state != nullptr) ebur128_destroy (&state);
                state = ebur128_init (static_cast<unsigned> (channels),
                                     static_cast<unsigned long> (std::llround (sampleRate)),
                                     EBUR128_MODE_S | EBUR128_MODE_I
                                         | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM);
                if (state != nullptr)
                    for (int channel = 0; channel < channels; ++channel)
                        ebur128_set_channel (state, static_cast<unsigned> (channel),
                                             channelMap[static_cast<size_t> (channel)]);
                activeEpoch = requestedEpoch;
                current = {};
                current.channels = channels;
                current.available = state != nullptr;
                measuredFrames = queryFrames = 0;
                rmsIndex = 0;
                rmsSums.fill (0.0);
                rmsHistory.assign (rmsFrames * static_cast<size_t> (channels), 0.0);
                publish (current, activeEpoch);
            }

            const auto read = readIndex.load (std::memory_order_relaxed);
            if (read == writeIndex.load (std::memory_order_acquire))
            {
                wait (2);
                continue;
            }
            const auto& packet = packets[read % queueCapacity];
            // A reset may arrive between reading the epoch and the queue head.
            // Leave newer audio in place until its state has been initialized.
            if (packet.epoch > activeEpoch) continue;
            if (packet.epoch == activeEpoch && state != nullptr)
            {
                const std::lock_guard libraryLock (ebur128Mutex);
                const auto result = ebur128_add_frames_float (state, packet.samples.data(), packet.frames);
                current.available = result == EBUR128_SUCCESS;
                if (current.available)
                {
                    for (size_t frame = 0; frame < packet.frames; ++frame)
                    {
                        for (int channel = 0; channel < channels; ++channel)
                        {
                            const auto c = static_cast<size_t> (channel);
                            const auto sample = static_cast<double> (packet.samples[frame * static_cast<size_t> (channels) + c]);
                            auto& old = rmsHistory[rmsIndex * static_cast<size_t> (channels) + c];
                            rmsSums[c] += sample * sample - old;
                            old = sample * sample;
                        }
                        rmsIndex = (rmsIndex + 1) % rmsFrames;
                    }
                    measuredFrames += packet.frames;
                    queryFrames += packet.frames;
                    current.measuredSeconds = static_cast<double> (measuredFrames) / sampleRate;
                    const auto release = static_cast<float> (std::pow (10.0, -static_cast<double> (packet.frames) / sampleRate));
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        const auto c = static_cast<size_t> (channel);
                        double samplePeak = 0.0, truePeak = 0.0, maxTruePeak = 0.0;
                        ebur128_prev_sample_peak (state, static_cast<unsigned> (channel), &samplePeak);
                        ebur128_prev_true_peak (state, static_cast<unsigned> (channel), &truePeak);
                        ebur128_true_peak (state, static_cast<unsigned> (channel), &maxTruePeak);
                        current.samplePeak[c] = std::max (current.samplePeak[c] * release, static_cast<float> (samplePeak));
                        current.truePeak[c] = std::max (current.truePeak[c] * release, static_cast<float> (truePeak));
                        current.maxTruePeak[c] = static_cast<float> (maxTruePeak);
                        current.rms[c] = static_cast<float> (std::sqrt (std::max (0.0, rmsSums[c]) / static_cast<double> (rmsFrames)));
                    }
                    if (queryFrames >= queryInterval)
                    {
                        queryFrames = 0;
                        // Do not display zero-padded partial windows as complete measurements.
                        if (current.measuredSeconds >= 0.4)
                        {
                            ebur128_loudness_momentary (state, &current.momentaryLufs);
                            ebur128_loudness_global (state, &current.integratedLufs);
                        }
                        if (current.measuredSeconds >= 3.0)
                            ebur128_loudness_shortterm (state, &current.shortTermLufs);
                    }
                    publish (current, activeEpoch);
                }
                else
                {
                    current = {};
                    current.channels = channels;
                    publish (current, activeEpoch);
                }
            }
            readIndex.store (read + 1, std::memory_order_release);
        }
        const std::lock_guard libraryLock (ebur128Mutex);
        if (state != nullptr) ebur128_destroy (&state);
    }

    std::vector<Packet> packets;
    std::atomic<size_t> readIndex { 0 }, writeIndex { 0 };
    std::atomic<uint64_t> epoch { 1 }, droppedFrames { 0 };
    std::atomic_bool interrupted { false };
    int channels = 0;
    double sampleRate = 0.0;
    std::array<int, maximumOutputChannels> channelMap {};
    mutable std::mutex readingsMutex;
    OutputMeterReadings readings;
    uint64_t readingsEpoch = 0;
};

OutputMeter::OutputMeter() : impl (std::make_unique<Impl>()) {}
OutputMeter::~OutputMeter() = default;

void OutputMeter::prepare (double sampleRate, const juce::AudioChannelSet& layout)
{
    release();
    if (! std::isfinite (sampleRate) || sampleRate < 16.0
        || sampleRate > 2822400.0 || layout.isDisabled()
        || layout.size() > maximumOutputChannels)
        return;
    for (int channel = 0; channel < layout.size(); ++channel)
    {
        const auto mapped = ebuChannel (layout.getTypeOfChannel (channel));
        if (mapped < 0) return;
        impl->channelMap[static_cast<size_t> (channel)] = mapped;
    }
    impl->sampleRate = sampleRate;
    impl->channels = layout.size();
    impl->readIndex.store (0);
    impl->writeIndex.store (0);
    impl->startThread();
}

void OutputMeter::release()
{
    impl->stopThread (-1);
    impl->channels = 0;
    reset();
}

void OutputMeter::reset() noexcept
{
    impl->droppedFrames.store (0, std::memory_order_relaxed);
    impl->interrupted.store (false, std::memory_order_relaxed);
    impl->epoch.fetch_add (1, std::memory_order_release);
}

uint64_t OutputMeter::beginBlock() const noexcept
{
    return impl->epoch.load (std::memory_order_acquire);
}

void OutputMeter::push (const juce::AudioBuffer<float>& source, int startSample, int numSamples,
                        uint64_t blockEpoch) noexcept
{
    if (impl->channels <= 0 || numSamples <= 0) return;
    if (blockEpoch != impl->epoch.load (std::memory_order_acquire)) return;
    for (int offset = 0; offset < numSamples;)
    {
        const auto write = impl->writeIndex.load (std::memory_order_relaxed);
        if (write - impl->readIndex.load (std::memory_order_acquire) >= Impl::queueCapacity)
        {
            impl->droppedFrames.fetch_add (static_cast<uint64_t> (numSamples - offset), std::memory_order_relaxed);
            impl->interrupted.store (true, std::memory_order_relaxed);
            impl->epoch.fetch_add (1, std::memory_order_release);
            return;
        }
        auto& packet = impl->packets[write % Impl::queueCapacity];
        packet.frames = std::min (Impl::packetFrames, static_cast<size_t> (numSamples - offset));
        packet.epoch = blockEpoch;
        for (int channel = 0; channel < impl->channels; ++channel)
        {
            const auto* input = channel < source.getNumChannels()
                                    ? source.getReadPointer (channel, startSample + offset) : nullptr;
            for (size_t frame = 0; frame < packet.frames; ++frame)
                packet.samples[frame * static_cast<size_t> (impl->channels) + static_cast<size_t> (channel)]
                    = input != nullptr ? input[frame] : 0.0f;
        }
        impl->writeIndex.store (write + 1, std::memory_order_release);
        offset += static_cast<int> (packet.frames);
    }
}

OutputMeterReadings OutputMeter::getReadings() const
{
    const std::lock_guard lock (impl->readingsMutex);
    auto result = impl->readingsEpoch == impl->epoch.load (std::memory_order_acquire)
                      ? impl->readings : OutputMeterReadings {};
    result.droppedFrames = impl->droppedFrames.load (std::memory_order_relaxed);
    result.measurementInterrupted = impl->interrupted.load (std::memory_order_relaxed);
    return result;
}
