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
    juce::String selectedTrackArtist;
    juce::String selectedTrackAlbum;
    juce::String selectedTrackAlbumArtist;
    juce::String selectedTrackGenre;
    juce::String selectedTrackFormat;
    juce::String selectedTrackArtworkPath;
    juce::String selectedTrackPath;
    int selectedTrackYear = 0;
    int selectedTrackNumber = 0;
    int selectedTrackDiscNumber = 0;
    int selectedTrackBitDepth = 0;
    int selectedTrackChannelCount = 0;
    double selectedTrackSampleRate = 0.0;
    double selectedTrackDurationSeconds = 0.0;
    bool selectedTrackAvailable = false;

    TransportIntent transportIntent = TransportIntent::stopped;
    bool isPlaying = false;
    bool playbackFinished = false;
    bool canPlay = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
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
    std::array<float, maximumOutputChannels> outputPeakLevels {};
    WorkspaceMode workspaceMode = WorkspaceMode::listening;

    juce::String statusMessage;
};
