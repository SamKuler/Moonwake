#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class AlbumArtworkComponent final : public juce::Component
{
public:
    void setTrack (juce::String stableIdToUse, juce::String titleToUse);
    void paint (juce::Graphics&) override;

private:
    juce::String stableId;
    juce::String title;
};
