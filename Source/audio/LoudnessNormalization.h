#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <optional>

// Gains use the ReplayGain reference of -18 LUFS. Peaks are linear amplitudes.
struct TrackLoudness
{
    std::optional<double> trackGainDb, trackPeak, albumGainDb, albumPeak;
    bool measured = false;
    juce::String albumSignature;
};

enum class LoudnessMode { off, track, album };
struct LoudnessPreferences
{
    LoudnessMode mode = LoudnessMode::off;
    double targetLufs = -18.0;
    bool preventClipping = true;
};

[[nodiscard]] TrackLoudness readReplayGainTags (const juce::StringPairArray&);
[[nodiscard]] float normalizationGain (const TrackLoudness&, const LoudnessPreferences&,
                                      bool albumValid = true);
[[nodiscard]] juce::ValueTree loudnessToTree (const TrackLoudness&);
[[nodiscard]] TrackLoudness loudnessFromTree (const juce::ValueTree&);
