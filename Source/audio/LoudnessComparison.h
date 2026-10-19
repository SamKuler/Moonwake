#pragma once

#include "audio/OutputMeter.h"
#include <atomic>
#include <memory>

struct LoudnessComparisonStatus
{
    bool enabled = false, matched = false, interrupted = false, capturing = false;
    double measuredSeconds = 0.0, dryGainDb = 0.0, wetGainDb = 0.0;
};

// Measures the latency-aligned dry and wet signals over the same interval.
// Once matched, compensation is held until an explicit or configuration reset.
class LoudnessComparison final
{
public:
    LoudnessComparison();
    void setChannelWeights (const std::array<float, maximumOutputChannels>&) noexcept;
    struct Block { uint64_t generation = 0, dry = 0, wet = 0; };
    void prepare (double sampleRate, const juce::AudioChannelSet&);
    void release();
    void setEnabled (bool);
    void reset() noexcept;
    [[nodiscard]] Block beginBlock() const noexcept;
    void push (const juce::AudioBuffer<float>& dry, const juce::AudioBuffer<float>& wet,
               int numSamples, Block) noexcept;
    [[nodiscard]] LoudnessComparisonStatus update(); // Control thread only.
    [[nodiscard]] bool isEnabled() const noexcept { return enabled.load (std::memory_order_acquire); }
    [[nodiscard]] float dryGain() const noexcept { return dryTarget.load (std::memory_order_relaxed); }
    [[nodiscard]] float wetGain() const noexcept { return wetTarget.load (std::memory_order_relaxed); }
private:
    struct Measurements { OutputMeter dry, wet; };
    void initialiseMeters();
    std::unique_ptr<Measurements> meters;
    double sampleRate = 0.0;
    juce::AudioChannelSet layout;
    std::atomic_bool enabled { false }, measuring { false }, captureRequested { false };
    std::atomic<uint64_t> generation { 2 };
    std::array<std::atomic<float>, maximumOutputChannels> channelWeights;
    std::atomic<float> dryTarget { 1.0f }, wetTarget { 1.0f };
};
