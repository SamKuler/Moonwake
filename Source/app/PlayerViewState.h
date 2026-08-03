#pragma once

#include <juce_core/juce_core.h>

#include "app/PlaybackCoordinator.h"
#include "app/WorkspaceMode.h"
#include "audio/OutputLayout.h"
#include "playlist/PlaylistModel.h"

#include <array>

struct PlayerViewState
{
    int selectedIndex = -1;
    int playlistSize = 0;
    juce::String selectedTrackId;
    juce::String selectedTrackTitle;
    juce::String selectedTrackPath;
    bool selectedTrackAvailable = false;

    TransportIntent transportIntent = TransportIntent::stopped;
    bool isPlaying = false;
    double positionSeconds = 0.0;
    double lengthSeconds = 0.0;
    double seekRatio = 0.0;
    double volume = 0.8;

    bool shuffleEnabled = false;
    RepeatMode repeatMode = RepeatMode::off;
    OutputLayout outputLayout = OutputLayout::stereo;
    int availableOutputChannels = 0;
    juce::String audioDeviceName;
    double audioSampleRate = 0.0;
    int audioBufferSize = 0;
    std::array<float, 10> outputPeakLevels {};
    WorkspaceMode workspaceMode = WorkspaceMode::listening;

    juce::String statusMessage;
};
