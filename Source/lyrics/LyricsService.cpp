#include "LyricsService.h"
#include "library/TrackTagReader.h"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
constexpr int maximumLyricsBytes = 1024 * 1024;
constexpr int maximumResponseBytes = 2 * maximumLyricsBytes;
constexpr int networkTimeoutMs = 8000;
using State = LyricsLoadResult::State;

juce::Result readText (const juce::File& file, juce::String& text, int maximumBytes)
{
    juce::FileInputStream input (file);
    if (! input.openedOk()) return juce::Result::fail ("Could not read " + file.getFileName() + ".");
    if (input.getTotalLength() > maximumBytes) return juce::Result::fail ("Lyrics file exceeds the size limit.");
    juce::MemoryBlock bytes;
    input.readIntoMemoryBlock (bytes, maximumBytes + 1);
    if (input.getStatus().failed() || bytes.getSize() != static_cast<size_t> (input.getTotalLength()))
        return juce::Result::fail ("The lyrics file could not be read completely.");
    if (bytes.getSize() > static_cast<size_t> (maximumBytes))
        return juce::Result::fail ("Lyrics file exceeds the size limit.");
    const auto* data = static_cast<const char*> (bytes.getData());
    const auto count = static_cast<int> (bytes.getSize());
    // UTF-16 with a BOM is accepted by JUCE's text decoder; other input must be UTF-8.
    const auto* unsignedData = static_cast<const unsigned char*> (bytes.getData());
    const bool utf16 = count >= 2 && ((unsignedData[0] == 0xff && unsignedData[1] == 0xfe)
                                    || (unsignedData[0] == 0xfe && unsignedData[1] == 0xff));
    if ((utf16 && count % 2 != 0) || (! utf16 && ! juce::CharPointer_UTF8::isValidString (data, count)))
        return juce::Result::fail ("Lyrics must use UTF-8 or UTF-16 text encoding.");
    text = juce::String::createStringFromData (data, count);
    return juce::Result::ok();
}

juce::Result writeJson (const juce::File& file, const juce::var& value)
{
    const auto created = file.getParentDirectory().createDirectory();
    if (created.failed()) return created;
    juce::TemporaryFile temporary (file);
    {
        juce::FileOutputStream output (temporary.getFile());
        const auto text = juce::JSON::toString (value);
        if (! output.openedOk() || ! output.write (text.toRawUTF8(), text.getNumBytesAsUTF8()))
            return juce::Result::fail ("Could not save lyrics: " + output.getStatus().getErrorMessage());
        output.flush();
        if (output.getStatus().failed()) return output.getStatus();
    } // Close the file before replacing it; Windows does not allow renaming this open handle.
    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace the saved lyrics file.");
    return juce::Result::ok();
}

juce::String normalised (const juce::String& text)
{
    auto words = juce::StringArray::fromTokens (text.trim().toLowerCase(), " \t\r\n", "");
    words.removeEmptyStrings();
    return words.joinIntoString (" ");
}

juce::Result parseRecord (const juce::var& json, LyricsSearchResult& result)
{
    if (! json.isObject() || ! json["trackName"].isString() || ! json["artistName"].isString()
        || ! (json["duration"].isDouble() || json["duration"].isInt() || json["duration"].isInt64()))
        return juce::Result::fail ("The lyrics provider returned an invalid record.");
    result.id = static_cast<juce::int64> (json["id"]);
    result.title = json["trackName"].toString();
    result.artist = json["artistName"].toString();
    result.album = json["albumName"].toString();
    result.durationSeconds = static_cast<double> (json["duration"]);
    if (! std::isfinite (result.durationSeconds) || result.durationSeconds < 0.0)
        return juce::Result::fail ("The lyrics provider returned an invalid duration.");
    result.document.instrumental = static_cast<bool> (json["instrumental"]);
    result.document.sourceLabel = "LRCLIB";
    if (result.document.instrumental) return juce::Result::ok();
    result.rawText = json["syncedLyrics"].toString();
    auto parsed = parseLyrics (result.rawText, result.document);
    if (parsed.failed())
    {
        result.rawText = json["plainLyrics"].toString();
        parsed = parseLyrics (result.rawText, result.document);
    }
    result.document.sourceLabel = "LRCLIB";
    return parsed;
}

juce::var storedDocument (const juce::String& text, const LyricsDocument& document)
{
    auto* value = new juce::DynamicObject();
    value->setProperty ("version", 1);
    value->setProperty ("text", text);
    value->setProperty ("instrumental", document.instrumental);
    value->setProperty ("source", document.sourceLabel);
    return juce::var (value);
}

juce::String downloadIdentity (const TrackItem& track)
{
    const juce::Array<juce::var> metadata {
        normalised (track.title), normalised (track.artist), normalised (track.album),
        juce::String (track.durationSeconds, 3), juce::var (track.fileModifiedAtMs)
    };
    const auto text = juce::JSON::toString (juce::var (metadata), true);
    return juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString();
}

juce::Result readDocument (const juce::File& file, LyricsDocument& document,
                           const juce::String& expectedIdentity = {})
{
    juce::String text;
    auto result = readText (file, text, maximumResponseBytes);
    if (result.failed()) return result;
    juce::var json;
    result = juce::JSON::parse (text, json);
    if (result.failed() || ! json.isObject() || static_cast<int> (json["version"]) != 1)
        return juce::Result::fail ("The saved lyrics file is invalid.");
    if (expectedIdentity.isNotEmpty() && json["lookupIdentity"].toString() != expectedIdentity)
        return juce::Result::fail ("The downloaded lyrics do not match the current track metadata.");
    if (static_cast<bool> (json["instrumental"])) document.instrumental = true;
    else if ((result = parseLyrics (json["text"].toString(), document)).failed()) return result;
    document.sourceLabel = json["source"].toString();
    return juce::Result::ok();
}
}

struct LyricsService::Impl final : private juce::Timer
{
    struct Lifetime
    {
        std::atomic<bool> alive { true };
        std::atomic<uint64_t> generation { 0 };
    };
    struct Work
    {
        bool persistent = false;
        std::function<void()> run;
    };

    Impl (juce::File directory, HttpGet boundary) : root (std::move (directory)), http (std::move (boundary))
    {
        const auto settings = root.getChildFile ("settings.json");
        if (settings.existsAsFile())
        {
            juce::String text;
            const auto read = readText (settings, text, 4096);
            const auto json = juce::JSON::parse (text);
            online.store (read.wasOk() && json["online"].isBool() && static_cast<bool> (json["online"]));
        }
        startTimer (50);
        worker = std::thread ([this] { run(); });
    }

    ~Impl() override
    {
        stopTimer();
        lifetime->alive = false;
        ++lifetime->generation;
        cancelNetwork();
        {
            std::lock_guard lock (mutex);
            stopping = true;
            // Confirmed writes remain queued and are drained before shutdown.
            std::erase_if (queue, [] (const Work& work) { return ! work.persistent; });
        }
        condition.notify_one();
        if (worker.joinable()) worker.join();
    }

    uint64_t nextRequest (const juce::String& readTrackKey = {})
    {
        uint64_t value;
        {
            std::lock_guard lock (mutex);
            currentReadTrackKey = readTrackKey;
            value = ++lifetime->generation;
            std::erase_if (queue, [] (const Work& work) { return ! work.persistent; });
        }
        cancelNetwork();
        return value;
    }

    uint64_t invalidateTrack (const TrackItem& track)
    {
        uint64_t value;
        {
            std::lock_guard lock (mutex);
            if (currentReadTrackKey != LyricsService::trackKey (track))
                return lifetime->generation.load();
            value = ++lifetime->generation;
            std::erase_if (queue, [] (const Work& work) { return ! work.persistent; });
        }
        cancelNetwork();
        return value;
    }

    Cancelled cancelled (uint64_t generation) const
    {
        return [life = lifetime, generation]
        {
            return ! life->alive.load() || life->generation.load() != generation;
        };
    }

    template <typename Callback, typename Result>
    void publish (uint64_t generation, bool persistent, Callback callbackToUse, Result resultToUse)
    {
        if (! callbackToUse) return;
        juce::MessageManager::callAsync ([life = lifetime, generation, persistent,
            callback = std::move (callbackToUse), result = std::move (resultToUse)] () mutable
        {
            if (life->alive.load() && (persistent || life->generation.load() == generation))
                callback (std::move (result));
        });
    }

    void enqueue (bool persistent, std::function<void()> work)
    {
        {
            std::lock_guard lock (mutex);
            queue.push_back ({ persistent, std::move (work) });
        }
        condition.notify_one();
    }

    void run()
    {
        for (;;)
        {
            Work work;
            {
                std::unique_lock lock (mutex);
                condition.wait (lock, [this] { return stopping || ! queue.empty(); });
                if (queue.empty()) return;
                work = std::move (queue.front());
                queue.pop_front();
            }
            work.run();
        }
    }

    juce::File file (const TrackItem& track, const juce::String& suffix) const
    {
        return root.getChildFile (LyricsService::trackKey (track) + suffix);
    }

    double offset (const TrackItem& track, juce::String* error = nullptr) const
    {
        juce::String text;
        const auto target = file (track, ".offset.json");
        if (! target.existsAsFile()) return 0.0;
        const auto read = readText (target, text, 4096);
        const auto saved = juce::JSON::parse (text)["seconds"];
        const auto value = static_cast<double> (saved);
        if (read.failed() || (! saved.isDouble() && ! saved.isInt() && ! saved.isInt64())
            || ! std::isfinite (value) || value < -60.0 || value > 60.0)
        {
            if (error != nullptr) *error = "The saved lyric timing offset is invalid.";
            return 0.0;
        }
        return value;
    }

    void timerCallback() override
    {
        std::shared_ptr<juce::WebInputStream> stream;
        {
            std::lock_guard lock (networkMutex);
            if (network && juce::Time::getMillisecondCounterHiRes() >= deadline) stream = network;
        }
        if (stream) stream->cancel();
    }

    void cancelNetwork()
    {
        std::shared_ptr<juce::WebInputStream> stream;
        {
            std::lock_guard lock (networkMutex);
            stream = network;
        }
        if (stream) stream->cancel();
    }

    HttpResponse request (const juce::URL& url, const Cancelled& isCancelled)
    {
        if (isCancelled()) return {};
        if (http)
        {
            auto response = http (url, isCancelled);
            if (response.body.getNumBytesAsUTF8() > maximumResponseBytes)
                return { 0, {}, "The lyrics response exceeds the size limit." };
            return response;
        }
        auto stream = std::make_shared<juce::WebInputStream> (url, false);
        stream->withConnectionTimeout (networkTimeoutMs).withNumRedirectsToFollow (0)
            .withExtraHeaders ("Accept: application/json\r\nUser-Agent: Moonwake/0.10 (desktop music player)\r\n");
        const auto expires = juce::Time::getMillisecondCounterHiRes() + networkTimeoutMs;
        {
            std::lock_guard lock (networkMutex);
            network = stream;
            deadline = expires;
        }
        struct Listener final : juce::WebInputStream::Listener
        {
            Listener (const Cancelled& check, double time) : cancel (check), expires (time) {}
            bool postDataSendProgress (juce::WebInputStream&, int, int) override
            {
                return ! cancel() && juce::Time::getMillisecondCounterHiRes() < expires;
            }
            Cancelled cancel;
            double expires;
        } listener (isCancelled, expires);
        HttpResponse response;
        if (isCancelled() || ! stream->connect (&listener)) response.error = "Could not connect to the lyrics provider.";
        else
        {
            response.statusCode = stream->getStatusCode();
            if (stream->getTotalLength() > maximumResponseBytes)
                response.error = "The lyrics response exceeds the size limit.";
            else
            {
                juce::MemoryOutputStream body;
                std::array<char, 4096> bytes {};
                while (! stream->isExhausted() && ! stream->isError() && ! isCancelled()
                       && juce::Time::getMillisecondCounterHiRes() < expires)
                {
                    const auto count = stream->read (bytes.data(), static_cast<int> (bytes.size()));
                    if (count <= 0) break;
                    if (body.getDataSize() + static_cast<size_t> (count) > maximumResponseBytes)
                    {
                        response.error = "The lyrics response exceeds the size limit.";
                        break;
                    }
                    body.write (bytes.data(), static_cast<size_t> (count));
                }
                if (stream->isError() || juce::Time::getMillisecondCounterHiRes() >= expires)
                    response.error = "The lyrics request timed out or the connection was interrupted.";
                if (response.error.isEmpty() && ! isCancelled())
                {
                    const auto* data = static_cast<const char*> (body.getData());
                    const auto size = static_cast<int> (body.getDataSize());
                    if (! juce::CharPointer_UTF8::isValidString (data, size)) response.error = "The lyrics response is not valid UTF-8.";
                    else response.body = juce::String::fromUTF8 (data, size);
                }
            }
        }
        stream->cancel();
        {
            std::lock_guard lock (networkMutex);
            if (network == stream) network.reset();
        }
        return response;
    }

    LyricsLoadResult resolve (const TrackItem& track, bool refresh, const Cancelled& isCancelled, bool allowNetwork = true)
    {
        LyricsLoadResult result;
        result.offsetSeconds = offset (track, &result.error);
        const auto override = file (track, ".override.json");
        if (override.existsAsFile())
        {
            const auto read = readDocument (override, result.document);
            result.state = read.wasOk() ? State::ready : State::invalid;
            if (read.failed()) result.error = read.getErrorMessage();
            return result;
        }
        const auto sidecar = track.file.withFileExtension ("lrc");
        if (sidecar.existsAsFile())
        {
            juce::String text;
            auto read = readText (sidecar, text, maximumLyricsBytes);
            if (read.wasOk()) read = parseLyrics (text, result.document);
            result.document.sourceLabel = "Local LRC";
            result.state = read.wasOk() ? State::ready : State::invalid;
            if (read.failed()) result.error = read.getErrorMessage();
            return result;
        }
        if (isCancelled()) return result;
        if (const auto text = readTrackLyricsTag (track.file); text.isNotEmpty())
        {
            const auto read = parseLyrics (text, result.document);
            result.document.sourceLabel = "Embedded lyrics";
            result.state = read.wasOk() ? State::ready : State::invalid;
            if (read.failed()) result.error = read.getErrorMessage();
            return result;
        }
        const auto cache = file (track, ".cache.json");
        LyricsDocument cached;
        const auto cacheRead = cache.existsAsFile() ? readDocument (cache, cached, downloadIdentity (track)) : juce::Result::fail ("");
        const bool hasCache = cacheRead.wasOk();
        if (hasCache && (! refresh || ! online.load() || ! allowNetwork))
        {
            result.document = std::move (cached);
            result.state = State::ready;
            return result;
        }
        if (! online.load() || ! allowNetwork || isCancelled()) return result;
        if (track.title.trim().isEmpty() || track.artist.trim().isEmpty()
            || ! std::isfinite (track.durationSeconds) || track.durationSeconds < 1.0 || track.durationSeconds > 3600.0)
            return result;
        auto url = juce::URL ("https://lrclib.net/api/get")
            .withParameter ("track_name", track.title).withParameter ("artist_name", track.artist)
            .withParameter ("duration", juce::String (track.durationSeconds, 3));
        if (track.album.isNotEmpty()) url = url.withParameter ("album_name", track.album);
        const auto response = request (url, isCancelled);
        if (isCancelled()) return result;
        if (response.error.isNotEmpty() || (response.statusCode != 200 && response.statusCode != 404))
        {
            result.state = State::networkError;
            result.error = response.error.isNotEmpty() ? response.error
                : "The lyrics provider returned HTTP " + juce::String (response.statusCode) + ".";
        }
        else if (response.statusCode == 200)
        {
            LyricsSearchResult candidate;
            const auto parsed = parseRecord (juce::JSON::parse (response.body), candidate);
            if (parsed.failed())
            {
                result.state = State::invalid;
                result.error = parsed.getErrorMessage();
            }
            else if (normalised (candidate.title) == normalised (track.title)
                     && normalised (candidate.artist) == normalised (track.artist)
                     && (track.album.trim().isEmpty() || normalised (candidate.album) == normalised (track.album))
                     && std::abs (candidate.durationSeconds - track.durationSeconds) <= 2.0)
            {
                result.document = std::move (candidate.document);
                result.state = State::ready;
                auto cachedValue = storedDocument (candidate.rawText, result.document);
                cachedValue.getDynamicObject()->setProperty ("lookupIdentity", downloadIdentity (track));
                const auto saved = writeJson (cache, cachedValue);
                if (saved.failed()) result.error = saved.getErrorMessage();
                return result;
            }
        }
        if (hasCache)
        {
            result.document = std::move (cached);
            result.state = State::ready;
        }
        return result;
    }

    LyricsLoadResult saveOverride (const TrackItem& track, const juce::String& text, const LyricsDocument& document)
    {
        const auto saved = writeJson (file (track, ".override.json"), storedDocument (text, document));
        if (saved.failed()) return failedImport (track, saved.getErrorMessage());
        LyricsLoadResult result { State::ready, document, 0.0, {} };
        result.offsetSeconds = offset (track, &result.error);
        return result;
    }

    LyricsLoadResult failedImport (const TrackItem& track, const juce::String& error)
    {
        auto result = resolve (track, false, [] { return false; }, false);
        if (result.state != State::ready) result.state = State::invalid;
        result.error = error;
        return result;
    }

    juce::File root;
    HttpGet http;
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    std::atomic<bool> online { true };
    std::mutex mutex, networkMutex;
    std::condition_variable condition;
    std::deque<Work> queue;
    juce::String currentReadTrackKey;
    bool stopping = false;
    std::thread worker;
    std::shared_ptr<juce::WebInputStream> network;
    double deadline = 0.0;
};

LyricsService::LyricsService (juce::File cacheRoot, HttpGet http)
    : impl (std::make_unique<Impl> (std::move (cacheRoot), std::move (http))) {}
LyricsService::~LyricsService() = default;

void LyricsService::load (TrackItem trackToUse, LoadCallback callbackToUse, bool refresh)
{
    const auto generation = impl->nextRequest (trackKey (trackToUse));
    impl->enqueue (false, [state = impl.get(), generation, track = std::move (trackToUse), callback = std::move (callbackToUse), refresh] () mutable
    {
        const auto cancelled = state->cancelled (generation);
        if (! cancelled()) state->publish (generation, false, std::move (callback), state->resolve (track, refresh, cancelled));
    });
}

void LyricsService::importFile (TrackItem trackToUse, juce::File fileToUse, LoadCallback callbackToUse)
{
    const auto generation = impl->invalidateTrack (trackToUse);
    impl->enqueue (true, [state = impl.get(), generation, track = std::move (trackToUse), lyricsFile = std::move (fileToUse), callback = std::move (callbackToUse)] () mutable
    {
        juce::String text;
        LyricsDocument document;
        auto parsed = readText (lyricsFile, text, maximumLyricsBytes);
        if (parsed.wasOk()) parsed = parseLyrics (text, document);
        document.sourceLabel = "Imported lyrics";
        auto result = parsed.wasOk() ? state->saveOverride (track, text, document) : state->failedImport (track, parsed.getErrorMessage());
        state->publish (generation, true, std::move (callback), std::move (result));
    });
}

void LyricsService::search (TrackItem trackToUse, juce::String queryToUse, SearchCallback callbackToUse)
{
    const auto generation = impl->nextRequest (trackKey (trackToUse));
    impl->enqueue (false, [state = impl.get(), generation, track = std::move (trackToUse), query = std::move (queryToUse), callback = std::move (callbackToUse)] () mutable
    {
        const auto cancelled = state->cancelled (generation);
        if (cancelled()) return;
        if (query.trim().isEmpty()) query = track.title + " " + track.artist;
        const auto response = state->request (juce::URL ("https://lrclib.net/api/search").withParameter ("q", query.substring (0, 512)), cancelled);
        LyricsSearchResponse result;
        if (response.error.isNotEmpty()) result.error = response.error;
        else if (response.statusCode != 200) result.error = "Lyrics search failed (HTTP " + juce::String (response.statusCode) + ").";
        else
        {
            const auto json = juce::JSON::parse (response.body);
            if (const auto* records = json.getArray())
            {
                for (int index = 0; index < juce::jmin (20, records->size()); ++index)
                {
                    LyricsSearchResult candidate;
                    if (parseRecord ((*records)[index], candidate).wasOk()) result.results.push_back (std::move (candidate));
                }
            }
            else result.error = "The lyrics provider returned an invalid search response.";
        }
        state->publish (generation, false, std::move (callback), std::move (result));
    });
}

void LyricsService::chooseResult (TrackItem trackToUse, LyricsSearchResult selectedToUse, LoadCallback callbackToUse)
{
    const auto generation = impl->invalidateTrack (trackToUse);
    impl->enqueue (true, [state = impl.get(), generation, track = std::move (trackToUse), selected = std::move (selectedToUse), callback = std::move (callbackToUse)] () mutable
    {
        LyricsDocument document;
        auto parsed = juce::Result::ok();
        if (selected.document.instrumental) document.instrumental = true;
        else parsed = parseLyrics (selected.rawText, document);
        document.sourceLabel = "LRCLIB (selected)";
        auto result = parsed.wasOk() ? state->saveOverride (track, selected.rawText, document)
                                    : state->failedImport (track, parsed.getErrorMessage());
        state->publish (generation, true, std::move (callback), std::move (result));
    });
}

void LyricsService::reset (TrackItem trackToUse, LoadCallback callbackToUse)
{
    const auto generation = impl->invalidateTrack (trackToUse);
    impl->enqueue (true, [state = impl.get(), generation, track = std::move (trackToUse), callback = std::move (callbackToUse)] () mutable
    {
        juce::String error;
        for (const auto suffix : { ".override.json", ".cache.json" })
        {
            const auto file = state->file (track, suffix);
            if (file.exists() && ! file.deleteFile()) error = "Could not remove the saved lyrics file.";
        }
        auto result = state->resolve (track, false, state->cancelled (generation));
        if (error.isNotEmpty()) result.error = error;
        state->publish (generation, true, std::move (callback), std::move (result));
    });
}

void LyricsService::cancel() { impl->nextRequest(); }

void LyricsService::setTimingOffset (TrackItem trackToUse, double seconds, SaveCallback callbackToUse)
{
    impl->enqueue (true, [state = impl.get(), track = std::move (trackToUse), seconds, callback = std::move (callbackToUse)] () mutable
    {
        auto result = juce::Result::fail ("Timing offset must be between -60 and 60 seconds.");
        if (std::isfinite (seconds) && seconds >= -60.0 && seconds <= 60.0)
        {
            auto* value = new juce::DynamicObject();
            value->setProperty ("seconds", seconds);
            result = writeJson (state->file (track, ".offset.json"), juce::var (value));
        }
        state->publish (0, true, std::move (callback), std::move (result));
    });
}

bool LyricsService::isOnlineLookupEnabled() const noexcept { return impl->online.load(); }

juce::Result LyricsService::setOnlineLookupEnabled (bool enabled)
{
    auto* value = new juce::DynamicObject();
    value->setProperty ("online", enabled);
    const auto saved = writeJson (impl->root.getChildFile ("settings.json"), juce::var (value));
    if (saved.wasOk())
    {
        impl->online = enabled;
        if (! enabled) impl->nextRequest();
    }
    return saved;
}

juce::String LyricsService::trackKey (const TrackItem& track)
{
    const auto identity = track.libraryId.isNotEmpty() ? "library:" + track.libraryId
        : track.fileIdentity.isNotEmpty() ? "file:" + track.fileIdentity : "path:" + track.file.getFullPathName();
    return juce::SHA256 (identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString();
}
