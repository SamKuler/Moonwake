#include "app/PlayerController.h"
#include <algorithm>
#include <cmath>
#include <set>

void PlayerController::setLoudnessPreferences (LoudnessPreferences value)
{
    value.targetLufs = std::isfinite (value.targetLufs) ? juce::jlimit (-30.0, -9.0, value.targetLufs) : -18.0;
    if (value.mode == loudnessPreferences.mode && std::abs (value.targetLufs - loudnessPreferences.targetLufs) < 0.0001
        && value.preventClipping == loudnessPreferences.preventClipping) return;
    loudnessPreferences = value;
    refreshNormalizationGains();
    ++stateRevision;
}

namespace
{
juce::String normalizationSourceKey (const TrackItem& track)
{
    const auto recording = track.libraryId.isNotEmpty() ? track.libraryId : track.id;
    const auto identity = track.fileIdentity.isNotEmpty() ? track.fileIdentity : track.file.getFullPathName();
    return recording + "\n" + identity + "\n" + juce::String (track.fileSizeBytes)
        + "\n" + juce::String (track.fileModifiedAtMs);
}
}

void PlayerController::refreshNormalizationGains()
{
    std::map<juce::String, std::vector<TrackItem>> albums;
    for (const auto& track : library.getTracks()) albums[loudnessAlbumKey (track)].push_back (track);
    albumSignatures.clear();
    for (const auto& [key, tracks] : albums) albumSignatures[key] = loudnessAlbumSignature (tracks);
    std::set<juce::String> currentSources;
    for (const auto& track : library.getTracks())
    {
        const auto key = normalizationSourceKey (track);
        currentSources.insert (key);
        static_cast<void> (normalizationGainFor (track));
        normalizationGains[key].source = track;
    }
    bool changed = false;
    for (auto it = normalizationGains.begin(); it != normalizationGains.end();)
    {
        auto& entry = it->second;
        if (! currentSources.contains (it->first) && entry.gain.use_count() == 1)
        {
            it = normalizationGains.erase (it);
            continue;
        }
        const auto& track = entry.source;
        const auto album = albumSignatures.find (loudnessAlbumKey (track));
        const auto valid = ! track.loudness.measured || (album != albumSignatures.end()
            && track.loudness.albumSignature.isNotEmpty() && track.loudness.albumSignature == album->second);
        const auto target = normalizationGain (track.loudness, loudnessPreferences, valid);
        changed = changed || std::abs (entry.gain->load (std::memory_order_relaxed) - target) > 0.000001f;
        entry.gain->store (target, std::memory_order_relaxed);
        ++it;
    }
    if (changed) audioEngine.resetLoudnessComparison();
}

std::shared_ptr<std::atomic<float>> PlayerController::normalizationGainFor (const TrackItem& track)
{
    auto& entry = normalizationGains[normalizationSourceKey (track)];
    if (entry.gain == nullptr)
    {
        const auto album = albumSignatures.find (loudnessAlbumKey (track));
        const auto valid = ! track.loudness.measured || (album != albumSignatures.end()
            && track.loudness.albumSignature.isNotEmpty() && album->second == track.loudness.albumSignature);
        entry.source = track;
        entry.gain = std::make_shared<std::atomic<float>> (normalizationGain (track.loudness, loudnessPreferences, valid));
    }
    return entry.gain;
}

bool PlayerController::startLoudnessScan()
{
    pollLoudnessScan();
    if (! loudnessScan.start (library.getTracks())) return false;
    loudnessScanStatus = loudnessScan.drainUpdates();
    return true;
}

void PlayerController::cancelLoudnessScan() { loudnessScan.cancel(); }
int PlayerController::getMeasuredTrackCount() const
{
    return static_cast<int> (std::count_if (library.getTracks().begin(), library.getTracks().end(),
        [] (const auto& track) { return track.loudness.trackGainDb.has_value(); }));
}

void PlayerController::pollLoudnessScan()
{
    auto update = loudnessScan.drainUpdates();
    const auto changed = ! update.tracks.empty();
    for (const auto& result : update.tracks)
        if (const auto* current = library.getTrack (result.id); current != nullptr && sameLoudnessSource (*current, result))
        {
            auto track = *current;
            track.loudness = result.loudness;
            library.replaceTrack (result.id, std::move (track));
        }
    update.tracks.clear();
    loudnessScanStatus = std::move (update);
    if (changed)
    {
        refreshQueueMetadata();
        ++stateRevision;
    }
}

void PlayerController::setLoudnessComparisonEnabled (bool value)
{
    audioEngine.setLoudnessComparisonEnabled (value);
    comparisonEffectRevision = effectChainRevision;
    comparisonParameterRevision = pluginStateRevision.load (std::memory_order_acquire);
}

LoudnessComparisonStatus PlayerController::getLoudnessComparisonStatus()
{
    const auto parameters = pluginStateRevision.load (std::memory_order_acquire);
    if (comparisonEffectRevision != effectChainRevision || comparisonParameterRevision != parameters)
    {
        audioEngine.resetLoudnessComparison();
        comparisonEffectRevision = effectChainRevision;
        comparisonParameterRevision = parameters;
    }
    return audioEngine.getLoudnessComparisonStatus();
}
