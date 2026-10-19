#include "audio/LoudnessNormalization.h"
#include <cmath>
#include <cstdlib>

namespace
{
std::optional<double> number (juce::String text, bool gain)
{
    text = text.trim();
    if (gain && text.endsWithIgnoreCase ("dB")) text = text.dropLastCharacters (2).trim();
    const auto* start = text.toRawUTF8();
    char* end = nullptr;
    const auto value = std::strtod (start, &end);
    if (end == start || *end != '\0' || ! std::isfinite (value)) return {};
    if (gain ? (value < -100.0 || value > 100.0) : (value <= 0.0 || value > 64.0)) return {};
    return value;
}
}

TrackLoudness readReplayGainTags (const juce::StringPairArray& metadata)
{
    TrackLoudness result;
    const auto keys = metadata.getAllKeys(), values = metadata.getAllValues();
    for (int i = 0; i < keys.size(); ++i)
    {
        const auto key = keys[i].toLowerCase().removeCharacters (" _-");
        if (key == "replaygaintrackgain") result.trackGainDb = number (values[i], true);
        else if (key == "replaygaintrackpeak") result.trackPeak = number (values[i], false);
        else if (key == "replaygainalbumgain") result.albumGainDb = number (values[i], true);
        else if (key == "replaygainalbumpeak") result.albumPeak = number (values[i], false);
    }
    return result;
}

float normalizationGain (const TrackLoudness& data, const LoudnessPreferences& prefs, bool albumValid)
{
    if (prefs.mode == LoudnessMode::off) return 1.0f;
    const auto album = prefs.mode == LoudnessMode::album && albumValid && data.albumGainDb.has_value();
    const auto db = album ? data.albumGainDb : data.trackGainDb;
    if (! db || ! std::isfinite (*db)) return 1.0f;
    const auto target = std::isfinite (prefs.targetLufs) ? juce::jlimit (-30.0, -9.0, prefs.targetLufs) : -18.0;
    auto gain = std::pow (10.0, juce::jlimit (-60.0, 24.0, *db + target + 18.0) / 20.0);
    const auto peak = album ? data.albumPeak : data.trackPeak;
    if (prefs.preventClipping)
        gain = juce::jmin (gain, peak && std::isfinite (*peak) && *peak > 0.0
                                    ? std::pow (10.0, -1.0 / 20.0) / *peak : 1.0);
    return static_cast<float> (gain);
}

juce::ValueTree loudnessToTree (const TrackLoudness& data)
{
    juce::ValueTree tree ("Loudness");
    tree.setProperty ("version", 1, nullptr);
    tree.setProperty ("measured", data.measured, nullptr);
    tree.setProperty ("albumSignature", data.albumSignature, nullptr);
    const auto set = [&] (const char* key, std::optional<double> value)
    { if (value && std::isfinite (*value)) tree.setProperty (key, *value, nullptr); };
    set ("trackGainDb", data.trackGainDb); set ("trackPeak", data.trackPeak);
    set ("albumGainDb", data.albumGainDb); set ("albumPeak", data.albumPeak);
    return tree;
}

TrackLoudness loudnessFromTree (const juce::ValueTree& tree)
{
    TrackLoudness result;
    if (! tree.hasType ("Loudness") || static_cast<int> (tree.getProperty ("version")) != 1) return result;
    const auto get = [&] (const char* key, bool gain) -> std::optional<double>
    { return tree.hasProperty (key) ? number (tree.getProperty (key).toString(), gain) : std::nullopt; };
    result.trackGainDb = get ("trackGainDb", true); result.trackPeak = get ("trackPeak", false);
    result.albumGainDb = get ("albumGainDb", true); result.albumPeak = get ("albumPeak", false);
    result.measured = static_cast<bool> (tree.getProperty ("measured", false));
    result.albumSignature = tree.getProperty ("albumSignature").toString();
    return result;
}
