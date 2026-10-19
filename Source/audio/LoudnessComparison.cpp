#include "audio/LoudnessComparison.h"
#include <cmath>

LoudnessComparison::LoudnessComparison()
{
    for (auto& weight : channelWeights) weight.store (1.0f, std::memory_order_relaxed);
}

void LoudnessComparison::setChannelWeights (const std::array<float, maximumOutputChannels>& weights) noexcept
{
    bool changed = false;
    for (size_t ch = 0; ch < weights.size(); ++ch)
    {
        changed = changed || std::abs (channelWeights[ch].load (std::memory_order_relaxed) - weights[ch]) > 0.000001f;
        channelWeights[ch].store (weights[ch], std::memory_order_relaxed);
    }
    if (changed) reset();
}

void LoudnessComparison::initialiseMeters()
{
    if (meters != nullptr || sampleRate <= 0.0) return;
    meters = std::make_unique<Measurements>();
    meters->dry.prepare (sampleRate, layout);
    meters->wet.prepare (sampleRate, layout);
}

void LoudnessComparison::prepare (double rate, const juce::AudioChannelSet& channels)
{
    // The audio producer is stopped during device preparation.
    meters.reset();
    sampleRate = rate;
    layout = channels;
    if (isEnabled()) initialiseMeters();
    reset();
}

void LoudnessComparison::release()
{
    meters.reset();
    sampleRate = 0.0;
    reset();
}

void LoudnessComparison::setEnabled (bool value)
{
    if (value == isEnabled()) return;
    if (value) initialiseMeters();
    reset();
    enabled.store (value, std::memory_order_release);
    measuring.store (value, std::memory_order_release);
    captureRequested.store (value, std::memory_order_release);
}

void LoudnessComparison::reset() noexcept
{
    // Odd generations close the producer gate until both meter windows are reset.
    generation.fetch_add (1, std::memory_order_acq_rel);
    if (meters != nullptr) { meters->dry.reset(); meters->wet.reset(); }
    dryTarget.store (1.0f, std::memory_order_relaxed);
    wetTarget.store (1.0f, std::memory_order_relaxed);
    measuring.store (isEnabled(), std::memory_order_release);
    captureRequested.store (isEnabled(), std::memory_order_release);
    generation.fetch_add (1, std::memory_order_release);
}

LoudnessComparison::Block LoudnessComparison::beginBlock() const noexcept
{
    if (! isEnabled() || ! captureRequested.load (std::memory_order_acquire) || meters == nullptr) return {};
    const auto current = generation.load (std::memory_order_acquire);
    if ((current & 1u) != 0) return {};
    const Block result { current, meters->dry.beginBlock(), meters->wet.beginBlock() };
    return generation.load (std::memory_order_acquire) == current ? result : Block {};
}

void LoudnessComparison::push (const juce::AudioBuffer<float>& dry, const juce::AudioBuffer<float>& wet,
                               int samples, Block block) noexcept
{
    if (! captureRequested.load (std::memory_order_acquire) || block.generation == 0 || block.generation != generation.load (std::memory_order_acquire)) return;
    std::array<float, maximumOutputChannels> weights;
    for (size_t ch = 0; ch < weights.size(); ++ch) weights[ch] = channelWeights[ch].load (std::memory_order_relaxed);
    meters->dry.push (dry, 0, samples, block.dry, &weights);
    meters->wet.push (wet, 0, samples, block.wet, &weights);
}

LoudnessComparisonStatus LoudnessComparison::update()
{
    LoudnessComparisonStatus status;
    status.enabled = isEnabled();
    if (! status.enabled || meters == nullptr) return status;
    const auto dry = meters->dry.getReadings(), wet = meters->wet.getReadings();
    status.measuredSeconds = juce::jmin (dry.measuredSeconds, wet.measuredSeconds);
    status.interrupted = dry.measurementInterrupted || wet.measurementInterrupted;
    const auto programmeMeasured = status.measuredSeconds >= 3.0
        && std::isfinite (dry.integratedLufs) && std::isfinite (wet.integratedLufs);
    if (status.interrupted || programmeMeasured) captureRequested.store (false, std::memory_order_release);
    status.capturing = captureRequested.load (std::memory_order_acquire);
    // Stop adding audio once both workers have enough programme material.
    // Draining the same captured interval avoids requiring two workers to publish simultaneously.
    if (measuring.load (std::memory_order_acquire) && ! status.interrupted && programmeMeasured
        && std::abs (dry.measuredSeconds - wet.measuredSeconds) < 1.5 / sampleRate)
    {
        const auto difference = dry.integratedLufs - wet.integratedLufs;
        // Attenuate the louder path; matching never introduces extra gain.
        dryTarget.store (static_cast<float> (std::pow (10.0, juce::jlimit (-60.0, 0.0, -difference) / 20.0)));
        wetTarget.store (static_cast<float> (std::pow (10.0, juce::jlimit (-60.0, 0.0, difference) / 20.0)));
        measuring.store (false, std::memory_order_release);
    }
    status.matched = ! measuring.load (std::memory_order_acquire);
    status.dryGainDb = juce::Decibels::gainToDecibels (dryGain());
    status.wetGainDb = juce::Decibels::gainToDecibels (wetGain());
    return status;
}
