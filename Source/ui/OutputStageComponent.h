#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "app/WorkspaceMode.h"
#include "audio/OutputLayout.h"

#include <array>

class OutputStageComponent final : public juce::Component
{
public:
    void setState (OutputLayout layoutToUse,
                   WorkspaceMode workspaceModeToUse,
                   bool isPlaying,
                   juce::String trackTitleToUse,
                   juce::String deviceNameToUse,
                   int availableChannelsToUse,
                   std::array<float, 10> outputPeakLevelsToUse);

    void paint (juce::Graphics&) override;

private:
    OutputLayout layout = OutputLayout::stereo;
    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    bool playing = false;
    juce::String trackTitle;
    juce::String deviceName;
    int availableChannels = 0;
    std::array<float, 10> outputPeakLevels {};
};
