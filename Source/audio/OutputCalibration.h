#pragma once

#include "OutputLayout.h"

#include <vector>

struct OutputChannelCalibration
{
    juce::AudioChannelSet::ChannelType channel = juce::AudioChannelSet::unknown;
    float trimDb = 0.0f;
    double delayMs = 0.0;
    bool inverted = false;

    bool operator== (const OutputChannelCalibration& other) const
    {
        return channel == other.channel && juce::exactlyEqual (trimDb, other.trimDb)
            && juce::exactlyEqual (delayMs, other.delayMs) && inverted == other.inverted;
    }
};

inline constexpr float minimumCalibrationTrimDb = -24.0f;
inline constexpr float maximumCalibrationTrimDb = 12.0f;
inline constexpr double maximumCalibrationDelayMs = 200.0;

[[nodiscard]] bool isValidOutputCalibration (const OutputChannelCalibration&);
[[nodiscard]] std::vector<OutputChannelCalibration> defaultOutputCalibration();

struct CalibrationTestSignal
{
    enum class Kind { off, pinkNoise, sine };
    Kind kind = Kind::off;
    juce::AudioChannelSet::ChannelType channel = juce::AudioChannelSet::unknown;
    float levelDbFS = -30.0f;
    double frequencyHz = 1000.0;
};
