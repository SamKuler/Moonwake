#pragma once

#include "app/PlayerController.h"
#include "skin/SkinComponentBindings.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Components expose behaviour and accessible text. Skin layouts own presentation.
class LoudnessControls final
{
public:
    explicit LoudnessControls (PlayerController&);
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, SkinValidationReport&);
    void refresh (bool ready = true);
private:
    void applySettings();
    PlayerController& controller;
    juce::Label heading, modeLabel, targetLabel, analysisStatus, comparisonStatus;
    juce::ComboBox mode, target;
    juce::ToggleButton preventClipping { "Prevent gain clipping" };
    juce::TextButton analyse { "Analyze library" }, cancel { "Cancel" };
    juce::TextButton original { "Original" }, processed { "Effects" };
    juce::TextButton match { "Match loudness" }, remeasure { "Re-measure" };
    bool refreshing = false;
};
