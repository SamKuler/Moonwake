#pragma once

#include <juce_core/juce_core.h>
#include <vector>

struct LyricLine
{
    double timeSeconds = 0.0;
    juce::String text;
};

struct LyricsDocument
{
    std::vector<LyricLine> lines;
    juce::String plainText;
    juce::String sourceLabel;
    bool instrumental = false;

    [[nodiscard]] bool isSynced() const noexcept { return ! lines.empty(); }
    [[nodiscard]] int activeLineAt (double positionSeconds) const noexcept;
};

juce::Result parseLyrics (const juce::String& text, LyricsDocument& document);
