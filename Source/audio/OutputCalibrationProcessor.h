#pragma once

#include "OutputCalibration.h"

#include <array>
#include <atomic>

// Control-thread targets are published atomically; history belongs to the audio callback.
class OutputCalibrationProcessor
{
public:
    OutputCalibrationProcessor();
    void setCalibration (const juce::AudioChannelSet&, const std::vector<OutputChannelCalibration>&);
    void prepare (double sampleRate);
    void setTestSignal (CalibrationTestSignal) noexcept;
    void stopTestSignal() noexcept;
    [[nodiscard]] bool isTestSignalActive() const noexcept;
    bool renderTestSignal (juce::AudioBuffer<float>&) noexcept;
    void process (juce::AudioBuffer<float>&) noexcept;

private:
    std::array<std::atomic<float>, maximumOutputChannels> gainTargets;
    std::array<std::atomic<double>, maximumOutputChannels> delayTargets;
    std::array<juce::SmoothedValue<float>, maximumOutputChannels> gains;
    std::array<juce::SmoothedValue<double>, maximumOutputChannels> delays;
    juce::AudioChannelSet channelLayout = juce::AudioChannelSet::stereo();
    juce::AudioBuffer<float> history;
    int writePosition = 0;
    double sampleRate = 48000.0;
    std::atomic<int> signalChannel { -1 };
    std::atomic<int> signalKind { 0 };
    std::atomic<float> signalGain { 0.0f };
    std::atomic<double> signalFrequency { 1000.0 };
    std::atomic<bool> signalLowFrequency { false };
    std::atomic<unsigned> signalRevision { 0 };
    unsigned observedSignalRevision = 0;
    bool wasTestSignalActive = false;
    CalibrationTestSignal::Kind activeSignalKind = CalibrationTestSignal::Kind::off;
    int activeSignalChannel = -1;
    float activeSignalGain = 0.0f;
    double activeSignalFrequency = 1000.0;
    bool activeSignalLowFrequency = false;
    double phase = 0.0;
    juce::SmoothedValue<float> signalEnvelope;
    std::array<float, maximumOutputChannels> lastOutput {};
    std::array<float, maximumOutputChannels> stopOutput {};
    int stopFadeRemaining = 0;
    int stopFadeLength = 240;
    std::array<float, 16> noiseRows {};
    uint32_t noiseCounter = 0, noiseSeed = 0x51f15e5dU;
    float lowPass = 0.0f;
    float nextWhiteNoise() noexcept;
};
