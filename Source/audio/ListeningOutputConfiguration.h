#pragma once

#include "OutputCalibration.h"
#include "OutputChannelMix.h"

struct ListeningOutputConfiguration
{
    juce::String deviceIdentifier;
    juce::String deviceName;
    OutputLayout layout = OutputLayout::stereo;
    double sampleRate = 0.0;
    int bufferSize = 0;
    std::vector<int> routing;
    std::vector<OutputChannelMix> channelMix;
    std::vector<OutputChannelCalibration> calibration;
};
