#pragma once

#include "skin/SkinExpression.h"

#include <juce_gui_extra/juce_gui_extra.h>

class AlbumArtworkComponent final : public juce::Component
{
public:
    void setTrack (juce::String stableIdToUse, juce::String titleToUse,
                   const juce::File& artworkFile = {});
    void setImage (juce::String stableIdToUse, juce::String titleToUse, juce::Image image);
    void paint (juce::Graphics&) override;
    std::function<std::optional<SkinExpressionValue> (std::string_view)> extraContext;

private:
    juce::String stableId;
    juce::String title;
    juce::String artworkPath;
    juce::Image artwork;
    juce::Image ambience;
};
