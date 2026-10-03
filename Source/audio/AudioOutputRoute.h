#pragma once

#include "OutputLayout.h"

#include <vector>

struct AudioOutputRoute
{
    juce::String deviceIdentifier;
    OutputLayout layout = OutputLayout::stereo;
    std::vector<int> channels;
};
