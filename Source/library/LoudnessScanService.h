#pragma once

#include "playlist/TrackItem.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <mutex>
#include <vector>

[[nodiscard]] juce::String loudnessAlbumKey (const TrackItem&);
[[nodiscard]] juce::String loudnessAlbumSignature (const std::vector<TrackItem>&);
[[nodiscard]] bool sameLoudnessSource (const TrackItem&, const TrackItem&);

struct LoudnessScanUpdate
{
    std::vector<TrackItem> tracks;
    int completed = 0, total = 0, failed = 0;
    juce::String currentFile, error;
    bool running = false, cancelled = false;
};

// Audio decoding and libebur128 run only on this worker. Draining preserves
// completed measurements when the remaining scan is cancelled.
class LoudnessScanService final : private juce::Thread
{
public:
    LoudnessScanService();
    ~LoudnessScanService() override;
    bool start (std::vector<TrackItem>);
    void cancel();
    [[nodiscard]] LoudnessScanUpdate drainUpdates();
private:
    void run() override;
    std::mutex mutex;
    LoudnessScanUpdate pending;
    std::vector<TrackItem> input;
};
