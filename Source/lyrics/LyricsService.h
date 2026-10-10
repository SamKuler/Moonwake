#pragma once

#include "LyricsDocument.h"
#include "playlist/TrackItem.h"
#include <atomic>
#include <functional>
#include <memory>

struct LyricsSearchResult
{
    juce::int64 id = 0;
    juce::String title, artist, album;
    double durationSeconds = 0.0;
    LyricsDocument document;
    juce::String rawText;
};

struct LyricsLoadResult
{
    enum class State { ready, notFound, networkError, invalid };
    State state = State::notFound;
    LyricsDocument document;
    double offsetSeconds = 0.0;
    juce::String error;
};

struct LyricsSearchResponse
{
    std::vector<LyricsSearchResult> results;
    juce::String error;
};

class LyricsService
{
public:
    struct HttpResponse
    {
        int statusCode = 0;
        juce::String body, error;
    };
    // The injected boundary is for deterministic offline tests. Implementations must
    // respect cancellation and the same response/time bounds as the native client.
    using Cancelled = std::function<bool()>;
    using HttpGet = std::function<HttpResponse (const juce::URL&, const Cancelled&)>;
    using LoadCallback = std::function<void (LyricsLoadResult)>;
    using SearchCallback = std::function<void (LyricsSearchResponse)>;
    using SaveCallback = std::function<void (juce::Result)>;

    explicit LyricsService (juce::File cacheRoot, HttpGet = {});
    ~LyricsService();

    // All callbacks run on the message thread. New reads suppress older load/search
    // completions. Confirmed imports, choices and resets invalidate reads only for
    // their captured track. All confirmed writes finish and report success/failure
    // even after navigation; writes for an older song leave the current read intact.
    void load (TrackItem, LoadCallback, bool refreshOnline = false);
    void importFile (TrackItem, juce::File lyricsFile, LoadCallback);
    void search (TrackItem, juce::String query, SearchCallback);
    void chooseResult (TrackItem, LyricsSearchResult, LoadCallback);
    // Removes the selected/imported override and downloaded cache. Offset remains.
    void reset (TrackItem, LoadCallback);
    void cancel();

    // Offset writes are serialized with source work and do not cancel a load.
    void setTimingOffset (TrackItem, double seconds, SaveCallback = {});
    [[nodiscard]] bool isOnlineLookupEnabled() const noexcept;
    juce::Result setOnlineLookupEnabled (bool);
    [[nodiscard]] static juce::String trackKey (const TrackItem&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LyricsService)
};
