#include "OutputCalibrationProcessor.h"

#include <algorithm>
#include <bit>
#include <cmath>

bool isValidOutputCalibration (const OutputChannelCalibration& setting)
{
    if (! std::isfinite (setting.trimDb) || setting.trimDb < minimumCalibrationTrimDb
        || setting.trimDb > maximumCalibrationTrimDb || ! std::isfinite (setting.delayMs)
        || setting.delayMs < 0.0 || setting.delayMs > maximumCalibrationDelayMs)
        return false;
    for (const auto& layout : outputLayouts())
        if (layout.channels.getChannelIndexForType (setting.channel) >= 0) return true;
    return false;
}

std::vector<OutputChannelCalibration> defaultOutputCalibration()
{
    std::vector<OutputChannelCalibration> result;
    for (const auto& layout : outputLayouts())
        for (const auto channel : layout.channels.getChannelTypes())
            if (std::none_of (result.begin(), result.end(), [channel] (const auto& setting)
                { return setting.channel == channel; }))
                result.push_back ({ channel });
    return result;
}

OutputCalibrationProcessor::OutputCalibrationProcessor()
{
    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<double>::is_always_lock_free);
    for (auto& gain : gainTargets) gain.store (1.0f);
    for (auto& delay : delayTargets) delay.store (0.0);
}

void OutputCalibrationProcessor::setCalibration (
    const juce::AudioChannelSet& layout, const std::vector<OutputChannelCalibration>& settings)
{
    channelLayout = layout;
    for (int channel = 0; channel < maximumOutputChannels; ++channel)
    {
        const auto speaker = channel < layout.size() ? layout.getTypeOfChannel (channel)
                                                     : juce::AudioChannelSet::unknown;
        const auto found = std::find_if (settings.begin(), settings.end(), [speaker] (const auto& setting)
            { return setting.channel == speaker; });
        const auto gain = found != settings.end() ? juce::Decibels::decibelsToGain (found->trimDb)
            * (found->inverted ? -1.0f : 1.0f) : 1.0f;
        gainTargets[static_cast<size_t> (channel)].store (gain, std::memory_order_relaxed);
        delayTargets[static_cast<size_t> (channel)].store (
            found != settings.end() ? found->delayMs : 0.0, std::memory_order_relaxed);
    }
}

void OutputCalibrationProcessor::prepare (double rate)
{
    sampleRate = rate;
    history.setSize (maximumOutputChannels,
                     static_cast<int> (std::ceil (rate * maximumCalibrationDelayMs / 1000.0)) + 2);
    history.clear();
    writePosition = 0;
    lastOutput.fill (0.0f);
    stopFadeRemaining = 0;
    stopFadeLength = juce::jmax (1, static_cast<int> (rate * 0.005));
    for (size_t channel = 0; channel < gains.size(); ++channel)
    {
        gains[channel].reset (rate, 0.02);
        gains[channel].setCurrentAndTargetValue (gainTargets[channel].load (std::memory_order_relaxed));
        delays[channel].reset (rate, 0.02);
        delays[channel].setCurrentAndTargetValue (delayTargets[channel].load (std::memory_order_relaxed) * rate / 1000.0);
    }
    signalEnvelope.reset (rate, 0.02);
    signalEnvelope.setCurrentAndTargetValue (0.0f);
}

void OutputCalibrationProcessor::setTestSignal (CalibrationTestSignal signal) noexcept
{
    signalRevision.fetch_add (1, std::memory_order_acq_rel);
    signalGain.store (juce::Decibels::decibelsToGain (signal.levelDbFS), std::memory_order_relaxed);
    signalFrequency.store (signal.frequencyHz, std::memory_order_relaxed);
    signalLowFrequency.store (signal.channel == juce::AudioChannelSet::LFE
        || signal.channel == juce::AudioChannelSet::LFE2, std::memory_order_relaxed);
    signalChannel.store (channelLayout.getChannelIndexForType (signal.channel), std::memory_order_relaxed);
    signalKind.store (static_cast<int> (signal.kind), std::memory_order_relaxed);
    signalRevision.fetch_add (1, std::memory_order_release);
}

void OutputCalibrationProcessor::stopTestSignal() noexcept
{
    if (signalKind.load (std::memory_order_relaxed) == 0) return;
    signalRevision.fetch_add (1, std::memory_order_acq_rel);
    signalKind.store (0, std::memory_order_relaxed);
    signalRevision.fetch_add (1, std::memory_order_release);
}

bool OutputCalibrationProcessor::isTestSignalActive() const noexcept
{
    return signalKind.load (std::memory_order_relaxed) != 0;
}

float OutputCalibrationProcessor::nextWhiteNoise() noexcept
{
    noiseSeed ^= noiseSeed << 13;
    noiseSeed ^= noiseSeed >> 17;
    noiseSeed ^= noiseSeed << 5;
    return static_cast<float> (static_cast<double> (noiseSeed) / 2147483648.0 - 1.0);
}

bool OutputCalibrationProcessor::renderTestSignal (juce::AudioBuffer<float>& buffer) noexcept
{
    auto revision = signalRevision.load (std::memory_order_acquire);
    if ((revision & 1U) == 0 && observedSignalRevision != revision)
    {
        const auto kind = static_cast<CalibrationTestSignal::Kind> (signalKind.load (std::memory_order_relaxed));
        const auto channel = signalChannel.load (std::memory_order_relaxed);
        const auto gain = signalGain.load (std::memory_order_relaxed);
        const auto frequency = signalFrequency.load (std::memory_order_relaxed);
        const auto lowFrequency = signalLowFrequency.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (signalRevision.load (std::memory_order_relaxed) == revision)
        {
            activeSignalKind = kind;
            activeSignalChannel = channel;
            activeSignalGain = gain;
            activeSignalFrequency = frequency;
            activeSignalLowFrequency = lowFrequency;
        }
        else
            revision = observedSignalRevision;
    }
    else
        revision = observedSignalRevision;
    const auto active = activeSignalKind != CalibrationTestSignal::Kind::off;
    if (observedSignalRevision != revision)
    {
        // Test history must never enter music or a different speaker after a test change.
        history.clear();
        writePosition = 0;
        stopOutput = lastOutput;
        stopFadeRemaining = wasTestSignalActive ? stopFadeLength : 0;
        observedSignalRevision = revision;
        phase = 0.0;
        noiseCounter = 0;
        noiseRows.fill (0.0f);
        lowPass = 0.0f;
        signalEnvelope.setCurrentAndTargetValue (0.0f);
        signalEnvelope.setTargetValue (active ? 1.0f : 0.0f);
        wasTestSignalActive = active;
    }
    if (! active) return false;
    buffer.clear();
    const auto channel = activeSignalChannel;
    if (channel < 0 || channel >= buffer.getNumChannels()) return true;
    const auto gain = activeSignalGain;
    const auto phaseStep = juce::MathConstants<double>::twoPi * activeSignalFrequency / sampleRate;
    const auto lowFrequency = activeSignalLowFrequency;
    const auto lowPassCoefficient = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 120.0 / sampleRate));
    auto* samples = buffer.getWritePointer (channel);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float value;
        if (activeSignalKind == CalibrationTestSignal::Kind::sine)
        {
            value = static_cast<float> (std::sin (phase));
            phase += phaseStep;
            if (phase >= juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
        }
        else
        {
            // Octave-spaced sample-and-hold rows produce a bounded pink spectrum.
            const auto row = static_cast<size_t> (std::countr_zero (++noiseCounter));
            if (row < noiseRows.size()) noiseRows[row] = nextWhiteNoise();
            value = nextWhiteNoise();
            for (const auto held : noiseRows) value += held;
            value /= 17.0f;
            if (lowFrequency)
            {
                lowPass += lowPassCoefficient * (value - lowPass);
                value = lowPass;
            }
        }
        samples[sample] = value * gain * signalEnvelope.getNextValue();
    }
    return true;
}

void OutputCalibrationProcessor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (history.getNumSamples() == 0) return;
    const auto channels = juce::jmin (maximumOutputChannels, buffer.getNumChannels());
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto index = static_cast<size_t> (channel);
        gains[index].setTargetValue (gainTargets[index].load (std::memory_order_relaxed));
        delays[index].setTargetValue (delayTargets[index].load (std::memory_order_relaxed) * sampleRate / 1000.0);
    }
    const auto size = history.getNumSamples();
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<size_t> (channel);
            auto* delayLine = history.getWritePointer (channel);
            delayLine[writePosition] = buffer.getSample (channel, sample);
            const auto delay = delays[index].getNextValue();
            const auto whole = static_cast<int> (delay);
            const auto fraction = static_cast<float> (delay - whole);
            auto recent = writePosition - whole;
            if (recent < 0) recent += size;
            const auto older = recent == 0 ? size - 1 : recent - 1;
            auto output = (delayLine[recent] + fraction * (delayLine[older] - delayLine[recent]))
                * gains[index].getNextValue();
            if (stopFadeRemaining > 0)
                output += stopOutput[index] * static_cast<float> (stopFadeRemaining) / static_cast<float> (stopFadeLength);
            buffer.setSample (channel, sample, output);
            lastOutput[index] = output;
        }
        if (stopFadeRemaining > 0) --stopFadeRemaining;
        if (++writePosition == size) writePosition = 0;
    }
}
