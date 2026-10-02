#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "app/WorkspaceMode.h"
#include "audio/OutputLayout.h"
#include "audio/OutputMeter.h"

#include <array>

class OutputStageComponent final : public juce::Component
{
public:
    enum class MeterMode { peak, rms, loudness };
    void setMeterMode (MeterMode modeToUse) { mode = modeToUse; repaint(); }
    void setMeterReadings (OutputMeterReadings readingsToUse) { readings = std::move (readingsToUse); repaint(); }
    void setState (OutputLayout layoutToUse,
                   WorkspaceMode workspaceModeToUse,
                   bool isPlaying,
                   juce::String trackTitleToUse,
                   juce::String deviceNameToUse,
                   int availableChannelsToUse,
                   std::array<float, maximumOutputChannels> outputPeakLevelsToUse);

    void paint (juce::Graphics&) override;

private:
    OutputLayout layout = OutputLayout::stereo;
    WorkspaceMode workspaceMode = WorkspaceMode::listening;
    bool playing = false;
    juce::String trackTitle;
    juce::String deviceName;
    int availableChannels = 0;
    std::array<float, maximumOutputChannels> outputPeakLevels {};
    MeterMode mode = MeterMode::rms;
    OutputMeterReadings readings;
};
