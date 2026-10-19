#include "library/LoudnessScanService.h"
#include "library/LibraryProjection.h"
#include "audio/ChannelMappedAudioSource.h"
#include "audio/Ebur128Support.h"
#include <algorithm>
#include <cmath>
#include <map>

juce::String loudnessAlbumKey (const TrackItem& track)
{
    if (track.album.trim().isEmpty()) return "track:" + track.file.getFullPathName();
    return libraryAlbumId (track);
}

juce::String loudnessAlbumSignature (const std::vector<TrackItem>& tracks)
{
    juce::StringArray members;
    for (const auto& track : tracks)
        members.add ((track.fileIdentity.isNotEmpty() ? track.fileIdentity : track.file.getFullPathName()) + "\t"
                     + juce::String (track.fileSizeBytes) + "\t" + juce::String (track.fileModifiedAtMs));
    members.sort (false);
    // The signature is a cache key, independent of library/queue ordering.
    return juce::String::toHexString (members.joinIntoString ("\n").hashCode64());
}

bool sameLoudnessSource (const TrackItem& a, const TrackItem& b)
{
    return a.file == b.file && a.fileIdentity == b.fileIdentity
        && a.fileSizeBytes == b.fileSizeBytes && a.fileModifiedAtMs == b.fileModifiedAtMs;
}

namespace
{
struct EbuState
{
    ebur128_state* value = nullptr;
    ~EbuState()
    {
        const std::lock_guard lock (MoonwakeEbu::ebur128WorkerMutex());
        if (value != nullptr) ebur128_destroy (&value);
    }
};
}

LoudnessScanService::LoudnessScanService() : juce::Thread ("Library loudness analysis") {}
LoudnessScanService::~LoudnessScanService() { stopThread (-1); }

bool LoudnessScanService::start (std::vector<TrackItem> tracks)
{
    if (isThreadRunning() || tracks.empty()) return false;
    stopThread (-1);
    input = std::move (tracks);
    {
        const std::lock_guard lock (mutex);
        pending = {};
        pending.running = true;
        pending.total = static_cast<int> (input.size());
    }
    startThread (juce::Thread::Priority::low);
    return true;
}

void LoudnessScanService::cancel() { signalThreadShouldExit(); }
LoudnessScanUpdate LoudnessScanService::drainUpdates()
{
    const std::lock_guard lock (mutex);
    auto tracks = std::move (pending.tracks);
    auto result = pending;
    result.tracks = std::move (tracks);
    pending.tracks.clear();
    return result;
}

void LoudnessScanService::run()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::map<juce::String, std::vector<TrackItem>> albums;
    for (const auto& track : input) albums[loudnessAlbumKey (track)].push_back (track);
    constexpr int blockSize = 4096;
    for (auto& [key, members] : albums)
    {
        juce::ignoreUnused (key);
        std::vector<std::unique_ptr<EbuState>> states;
        std::vector<TrackItem> measured;
        double albumPeak = 0.0;
        for (auto track : members)
        {
            if (threadShouldExit()) break;
            {
                const std::lock_guard lock (mutex);
                pending.currentFile = track.file.getFileName();
            }
            juce::String error;
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (track.file));
            auto state = std::make_unique<EbuState>();
            const auto rate = reader != nullptr ? reader->sampleRate : 0.0;
            const auto frames = reader != nullptr ? reader->lengthInSamples : 0;
            if (reader == nullptr || ! std::isfinite (rate) || rate < 16 || rate > 2822400
                || frames <= 0 || reader->numChannels > maximumOutputChannels)
                error = "Unsupported or unreadable audio stream.";
            if (error.isEmpty())
            {
                ChannelMappedAudioSource source (std::move (reader), juce::AudioChannelSet::stereo());
                const auto layout = source.getSourceLayout();
                source.setDestinationLayout (layout);
                source.prepareToPlay (blockSize, rate);
                {
                    const std::lock_guard lock (MoonwakeEbu::ebur128WorkerMutex());
                    state->value = ebur128_init (static_cast<unsigned> (layout.size()),
                        static_cast<unsigned long> (std::llround (rate)),
                        EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM);
                    if (state->value == nullptr) error = "Could not initialise loudness measurement.";
                    for (int ch = 0; ch < layout.size() && error.isEmpty(); ++ch)
                    {
                        const auto position = MoonwakeEbu::ebuChannel (layout.getTypeOfChannel (ch));
                        if (position < 0 || ebur128_set_channel (state->value, static_cast<unsigned> (ch), position) != EBUR128_SUCCESS)
                            error = "This speaker layout has no EBU loudness mapping.";
                    }
                }
                juce::AudioBuffer<float> block (layout.size(), blockSize);
                std::vector<float> interleaved (static_cast<size_t> (layout.size() * blockSize));
                for (juce::int64 offset = 0; offset < frames && error.isEmpty() && ! threadShouldExit();)
                {
                    const auto count = static_cast<int> (juce::jmin (frames - offset, juce::int64 { blockSize }));
                    source.getNextAudioBlock ({ &block, 0, count });
                    if (source.hasReadError()) { error = "Audio decoding failed."; break; }
                    for (int frame = 0; frame < count; ++frame)
                        for (int ch = 0; ch < layout.size(); ++ch)
                            interleaved[static_cast<size_t> (frame * layout.size() + ch)] = block.getSample (ch, frame);
                    {
                        const std::lock_guard lock (MoonwakeEbu::ebur128WorkerMutex());
                        if (ebur128_add_frames_float (state->value, interleaved.data(), static_cast<size_t> (count)) != EBUR128_SUCCESS)
                            error = "Loudness measurement failed.";
                    }
                    offset += count;
                }
                if (threadShouldExit()) break;
                if (error.isEmpty() && (track.file.getSize() != track.fileSizeBytes
                    || track.file.getLastModificationTime().toMilliseconds() != track.fileModifiedAtMs))
                    error = "Audio file changed during analysis.";
                if (error.isEmpty())
                {
                    const std::lock_guard lock (MoonwakeEbu::ebur128WorkerMutex());
                    double lufs = 0.0, peak = 0.0;
                    if (ebur128_loudness_global (state->value, &lufs) != EBUR128_SUCCESS || ! std::isfinite (lufs))
                        error = "No measurable programme loudness (silent or very short audio).";
                    for (int ch = 0; ch < layout.size() && error.isEmpty(); ++ch)
                    {
                        double channelPeak = 0.0;
                        if (ebur128_true_peak (state->value, static_cast<unsigned> (ch), &channelPeak) != EBUR128_SUCCESS)
                            error = "True-peak measurement failed.";
                        peak = juce::jmax (peak, channelPeak);
                    }
                    if (error.isEmpty())
                    {
                        const auto previous = track.loudness;
                        const auto signature = loudnessAlbumSignature (members);
                        track.loudness = {};
                        if (previous.albumGainDb && (! previous.measured || previous.albumSignature == signature))
                        {
                            track.loudness.albumGainDb = previous.albumGainDb;
                            track.loudness.albumPeak = previous.albumPeak;
                            track.loudness.albumSignature = signature;
                        }
                        track.loudness.measured = true;
                        track.loudness.trackGainDb = -18.0 - lufs;
                        if (peak > 0.0 && std::isfinite (peak)) track.loudness.trackPeak = peak;
                        albumPeak = juce::jmax (albumPeak, peak);
                        measured.push_back (track);
                    }
                }
            }
            {
                const std::lock_guard lock (mutex);
                ++pending.completed;
                if (error.isNotEmpty()) { ++pending.failed; pending.error = track.file.getFileName() + ": " + error; }
                else pending.tracks.push_back (track);
            }
            if (error.isEmpty()) states.push_back (std::move (state));
        }
        if (! threadShouldExit() && measured.size() == members.size() && ! states.empty())
        {
            std::vector<ebur128_state*> pointers;
            for (auto& state : states) pointers.push_back (state->value);
            double lufs = 0.0;
            int code = 0;
            {
                const std::lock_guard lock (MoonwakeEbu::ebur128WorkerMutex());
                code = ebur128_loudness_global_multiple (pointers.data(), pointers.size(), &lufs);
            }
            if (code == EBUR128_SUCCESS && std::isfinite (lufs))
            {
                const auto signature = loudnessAlbumSignature (members);
                const std::lock_guard lock (mutex);
                for (auto& track : measured)
                {
                    track.loudness.albumGainDb = -18.0 - lufs;
                    if (albumPeak > 0.0) track.loudness.albumPeak = albumPeak;
                    track.loudness.albumSignature = signature;
                    pending.tracks.push_back (std::move (track));
                }
            }
        }
        if (threadShouldExit()) break;
    }
    const std::lock_guard lock (mutex);
    pending.cancelled = threadShouldExit();
    pending.running = false;
}
