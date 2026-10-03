#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "audio/OutputLayout.h"
#include "audio/OutputChannelMix.h"
#include "audio/AudioOutputRoute.h"
#include "app/WorkspaceMode.h"
#include "playlist/PlaylistModel.h"
#include "library/MusicLibrary.h"
#include "plugins/EffectChainState.h"
#include "plugins/PluginFormatPreferences.h"

struct AudioDevicePreferences
{
    juce::String outputDeviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    std::vector<AudioOutputRoute> outputRoutes {};
};

struct PlayerAppState
{
    PlaylistModel playlist;
    MusicLibrary library;
    juce::String libraryCollectionId = "all";
    juce::String librarySearchQuery;
    EffectChainState effectChain;
    PluginFormatPreferences pluginFormats;
    juce::StringArray customVst3Directories;
    double volume = 0.8;
    double playbackPositionSeconds = 0.0;
    OutputLayout outputLayout = OutputLayout::stereo;
    std::vector<OutputChannelMix> outputChannelMix = defaultOutputChannelMix();
    AudioDevicePreferences audioDevice;
    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    juce::String activeSkinId = "builtin.default";
    juce::String activeSkinVersion;
    juce::String developerSkinDirectory;
};

class AppStateStore
{
public:
    static juce::ValueTree toValueTree (const PlayerAppState& state);
    static PlayerAppState fromValueTree (const juce::ValueTree& tree);
    static bool writeAtomically (const PlayerAppState& state,
                                 const juce::File& targetFile,
                                 juce::String& errorMessage);
    static bool readFromFile (const juce::File& sourceFile,
                              PlayerAppState& state,
                              juce::String& errorMessage);
};
