#pragma once

#include <juce_core/juce_core.h>

enum class PlayerPresentation { normal, immersive, mini };

struct PlayerWindowSizing
{
    int width = 1240, height = 800;
    int minimumWidth = 1040, minimumHeight = 700;
    int maximumWidth = 1800, maximumHeight = 1200;
};
