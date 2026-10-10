#include "lyrics/LyricsService.h"
#include <juce_events/juce_events.h>
#include <optional>

namespace
{
bool pumpUntil (const std::function<bool()>& condition)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    while (! condition() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return condition();
}

juce::String record (const juce::String& title = "Song", const juce::String& artist = "Artist", double duration = 120.0)
{
    auto* value = new juce::DynamicObject();
    value->setProperty ("id", 42);
    value->setProperty ("trackName", title);
    value->setProperty ("artistName", artist);
    value->setProperty ("albumName", "Album");
    value->setProperty ("duration", duration);
    value->setProperty ("instrumental", false);
    value->setProperty ("plainLyrics", "Network lyric");
    value->setProperty ("syncedLyrics", "[00:01]Network lyric");
    return juce::JSON::toString (juce::var (value));
}
}

class LyricsServiceTests final : public juce::UnitTest
{
public:
    LyricsServiceTests() : UnitTest ("LyricsService", "lyrics") {}
    void runTest() override
    {
        beginTest ("Automatic exact match caches lyrics for an offline restart");
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
            .getNonexistentChildFile ("lyrics-services", "");
        expect (directory.createDirectory().wasOk());
        TrackItem track { directory.getChildFile ("song.wav"), "Song", 120.0 };
        track.artist = "Artist";
        track.album = "Album";
        track.libraryId = "stable-song";
        expect (track.file.replaceWithText ("audio placeholder"));
        const auto import = directory.getChildFile ("import.lrc");
        expect (import.replaceWithText ("[00:02]Imported lyric"));
        std::optional<LyricsLoadResult> loaded;
        const auto receive = [&] (LyricsLoadResult result) { loaded = std::move (result); };
        const auto waitLoaded = [&]
        {
            const auto completed = pumpUntil ([&] { return loaded.has_value(); });
            expect (completed, "Lyrics callback did not arrive on the message thread");
            return completed;
        };
        std::atomic<int> requests { 0 };
        auto http = [&] (const juce::URL&, const LyricsService::Cancelled&)
        {
            ++requests;
            return LyricsService::HttpResponse { 200, record(), {} };
        };
        const auto cache = directory.getChildFile ("cache");

        {
            LyricsService service (cache, http);
            service.load (track, receive);
            if (waitLoaded())
            {
                expect (loaded->state == LyricsLoadResult::State::ready);
                expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
            }
            expectEquals (requests.load(), 1);
        }
        beginTest ("Downloaded lyrics are not reused after metadata changes under the same track ID");
        {
            const auto metadataCache = directory.getChildFile ("metadata-cache");
            {
                LyricsService service (metadataCache, [] (const juce::URL&, const LyricsService::Cancelled&)
                    { return LyricsService::HttpResponse { 200, record(), {} }; });
                loaded.reset(); service.load (track, receive);
                if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
            }
            std::atomic<int> offlineRequests { 0 };
            LyricsService service (metadataCache, [&] (const juce::URL&, const LyricsService::Cancelled&)
                { ++offlineRequests; return LyricsService::HttpResponse { 0, {}, "Offline" }; });
            for (int field = 0; field < 5; ++field)
            {
                auto updated = track;
                if (field == 0) updated.title = "Different recording";
                if (field == 1) updated.artist = "Different artist";
                if (field == 2) updated.album = "Different album";
                if (field == 3) updated.durationSeconds = 180.0;
                if (field == 4) updated.fileModifiedAtMs = 123456;
                expectEquals (LyricsService::trackKey (updated), LyricsService::trackKey (track));
                loaded.reset(); service.load (updated, receive);
                if (waitLoaded())
                {
                    expect (loaded->state == LyricsLoadResult::State::networkError);
                    expect (loaded->document.plainText.isEmpty(), "An old recording must not supply lyrics to changed metadata");
                }
            }
            expectEquals (offlineRequests.load(), 5);
            auto updated = track;
            updated.title = "Different recording";
            updated.durationSeconds = 180.0;
            expect (service.setOnlineLookupEnabled (false).wasOk());
            loaded.reset(); service.load (updated, receive);
            if (waitLoaded()) expect (loaded->state == LyricsLoadResult::State::notFound);

            LyricsSearchResult selected; selected.rawText = "[00:02]Explicitly selected lyric";
            loaded.reset(); service.chooseResult (track, selected, receive); waitLoaded();
            loaded.reset(); service.load (updated, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Explicitly selected lyric"));
            loaded.reset(); service.importFile (track, import, receive); waitLoaded();
            loaded.reset(); service.load (updated, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Imported lyric"));
        }

        loaded.reset();
        {
            LyricsService service (cache, [&] (const juce::URL&, const LyricsService::Cancelled&)
                { ++requests; return LyricsService::HttpResponse { 0, {}, "Offline" }; });
            expect (service.setOnlineLookupEnabled (false).wasOk());
            service.load (track, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
            expectEquals (requests.load(), 1);

            beginTest ("Sidecar and confirmed imports override cached content without rewriting audio");
            expect (track.file.withFileExtension ("lrc").replaceWithText ("[00:03]Sidecar lyric"));
            loaded.reset(); service.load (track, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Sidecar lyric"));
            loaded.reset(); service.importFile (track, import, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Imported lyric"));
            expectEquals (track.file.loadFileAsString(), juce::String ("audio placeholder"));

            beginTest ("Failed import preserves the last confirmed lyrics and reports its failure");
            expect (import.replaceWithText ("[00:99]Invalid"));
            loaded.reset(); service.importFile (track, import, receive);
            if (waitLoaded()) expect (loaded->error.isNotEmpty());
            loaded.reset(); service.load (track, receive, true);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Imported lyric"));

            beginTest ("Timing offsets persist independently of queue occurrence and lyric resets");
            std::optional<juce::Result> saved;
            service.setTimingOffset (track, 1.25, [&] (auto result) { saved = result; });
            expect (pumpUntil ([&] { return saved.has_value(); }));
            if (saved) expect (saved->wasOk());
        }
        loaded.reset();
        {
            LyricsService service (cache, http);
            expect (! service.isOnlineLookupEnabled());
            auto occurrence = track; occurrence.id = "another-occurrence";
            expectEquals (LyricsService::trackKey (occurrence), LyricsService::trackKey (track));
            service.load (occurrence, receive);
            if (waitLoaded())
            {
                expectEquals (loaded->document.plainText, juce::String ("Imported lyric"));
                expectWithinAbsoluteError (loaded->offsetSeconds, 1.25, 0.00001);
            }
            loaded.reset(); service.reset (track, receive);
            if (waitLoaded())
            {
                expectEquals (loaded->document.plainText, juce::String ("Sidecar lyric"));
                expectWithinAbsoluteError (loaded->offsetSeconds, 1.25, 0.00001);
            }
        }

        beginTest ("Automatic lookup rejects mismatched title artist album and duration");
        expect (track.file.withFileExtension ("lrc").deleteFile());
        for (int mismatch = 0; mismatch < 4; ++mismatch)
        {
            LyricsService service (directory.getChildFile ("mismatch-" + juce::String (mismatch)),
                [mismatch] (const juce::URL& url, const LyricsService::Cancelled&)
                {
                    auto body = record (mismatch == 0 ? "Other" : "Song", mismatch == 1 ? "Other" : "Artist", mismatch == 2 ? 123.0 : 120.0);
                    if (mismatch == 3) body = body.replace ("Album", "Other album");
                    if (! url.toString (true).contains ("track_name=Song")) return LyricsService::HttpResponse { 400, {}, "Missing title" };
                    return LyricsService::HttpResponse { 200, body, {} };
                });
            loaded.reset(); service.load (track, receive);
            if (waitLoaded()) expect (loaded->state == LyricsLoadResult::State::notFound);
        }

        beginTest ("Manual selection persists a chosen search result as a local override");
        {
            LyricsService service (cache, [] (const juce::URL& url, const LyricsService::Cancelled&)
            {
                return LyricsService::HttpResponse { 200, url.toString (false).contains ("/search") ? "[" + record ("Alternate") + "]" : record(), {} };
            });
            std::optional<LyricsSearchResponse> search;
            service.search (track, "Alternate", [&] (auto result) { search = std::move (result); });
            expect (pumpUntil ([&] { return search.has_value(); }));
            if (search && ! search->results.empty())
            {
                expectEquals (search->results.front().title, juce::String ("Alternate"));
                loaded.reset(); service.chooseResult (track, search->results.front(), receive);
                if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
            }
            else expect (false, "Search returned no selectable lyric");
        }

        beginTest ("Navigation cancels stale network publication and retains confirmed writes");
        {
            std::atomic<bool> entered { false };
            LyricsService service (directory.getChildFile ("cancellation"),
                [&] (const juce::URL&, const LyricsService::Cancelled& cancelled)
                {
                    entered = true;
                    while (! cancelled()) juce::Thread::sleep (2);
                    return LyricsService::HttpResponse { 200, record(), {} };
                });
            bool staleDelivered = false;
            service.load (track, [&] (auto) { staleDelivered = true; });
            expect (pumpUntil ([&] { return entered.load(); }));
            expect (import.replaceWithText ("[00:04]Confirmed import"));
            loaded.reset(); service.importFile (track, import, receive);
            auto other = track; other.libraryId = "other-song";
            other.file = directory.getChildFile ("other.wav");
            expect (other.file.withFileExtension ("lrc").replaceWithText ("Other song lyric"));
            std::optional<LyricsLoadResult> otherLoaded;
            service.load (other, [&] (auto result) { otherLoaded = std::move (result); });
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Confirmed import"));
            expect (pumpUntil ([&] { return otherLoaded.has_value(); }));
            expect (! staleDelivered);
            loaded.reset(); service.load (track, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Confirmed import"));
        }

        beginTest ("Confirmed writes for another track leave the current delayed read deliverable");
        for (int action = 0; action < 3; ++action)
        {
            std::atomic<bool> entered { false }, release { false }, interrupted { false };
            auto current = track;
            current.libraryId = "current-read";
            auto original = track;
            original.libraryId = "confirmed-write";
            original.file = directory.getChildFile ("original-" + juce::String (action) + ".wav");
            expect (original.file.withFileExtension ("lrc").replaceWithText ("Original sidecar"));
            LyricsService service (directory.getChildFile ("cross-track-write-" + juce::String (action)),
                [&] (const juce::URL&, const LyricsService::Cancelled& cancelled)
                {
                    entered = true;
                    while (! release.load() && ! cancelled()) juce::Thread::sleep (1);
                    interrupted = cancelled();
                    return LyricsService::HttpResponse { 200, record(), {} };
                });
            std::optional<LyricsLoadResult> prepared;
            LyricsSearchResult selected;
            selected.rawText = "[00:02]Previously selected lyric";
            service.chooseResult (original, selected, [&] (auto result) { prepared = std::move (result); });
            expect (pumpUntil ([&] { return prepared.has_value(); }));
            std::optional<LyricsLoadResult> currentResult, writeResult;
            service.load (current, [&] (auto result) { currentResult = std::move (result); });
            expect (pumpUntil ([&] { return entered.load(); }));
            const auto receiveWrite = [&] (auto result) { writeResult = std::move (result); };
            const auto imported = directory.getChildFile ("late-import-" + juce::String (action) + ".lrc");
            expect (imported.replaceWithText ("[00:03]Late imported lyric"));
            if (action == 0) service.importFile (original, imported, receiveWrite);
            else if (action == 1)
            {
                selected.rawText = "[00:04]Late selected lyric";
                service.chooseResult (original, selected, receiveWrite);
            }
            else service.reset (original, receiveWrite);
            release = true;
            expect (pumpUntil ([&] { return writeResult.has_value(); }));
            expect (pumpUntil ([&] { return currentResult.has_value(); }), "The current song must leave its loading state");
            expect (! interrupted.load(), "A confirmed action on another song must not cancel this read");
            if (currentResult)
                expectEquals (currentResult->document.plainText, juce::String ("Network lyric"));
            loaded.reset(); service.load (original, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText,
                juce::String (action == 0 ? "Late imported lyric" : action == 1 ? "Late selected lyric" : "Original sidecar"));
        }

        beginTest ("Same-track confirmed writes invalidate a delayed network read");
        for (int action = 0; action < 3; ++action)
        {
            std::atomic<bool> entered { false }, interrupted { false };
            const auto localFile = directory.getChildFile ("same-track-" + juce::String (action) + ".wav");
            auto current = track; current.file = localFile;
            LyricsService service (directory.getChildFile ("same-track-write-" + juce::String (action)),
                [&] (const juce::URL&, const LyricsService::Cancelled& cancelled)
                {
                    entered = true;
                    while (! cancelled()) juce::Thread::sleep (1);
                    interrupted = true;
                    return LyricsService::HttpResponse { 200, record(), {} };
                });
            bool staleDelivered = false;
            service.load (current, [&] (auto) { staleDelivered = true; });
            expect (pumpUntil ([&] { return entered.load(); }));
            const auto imported = directory.getChildFile ("same-import-" + juce::String (action) + ".lrc");
            expect (imported.replaceWithText ("[00:05]New imported lyric"));
            expect (localFile.withFileExtension ("lrc").replaceWithText ("Reset sidecar"));
            loaded.reset();
            if (action == 0) service.importFile (current, imported, receive);
            else if (action == 1)
            {
                LyricsSearchResult selected; selected.rawText = "[00:06]New selected lyric";
                service.chooseResult (current, selected, receive);
            }
            else service.reset (current, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText,
                juce::String (action == 0 ? "New imported lyric" : action == 1 ? "New selected lyric" : "Reset sidecar"));
            expect (interrupted.load());
            expect (! staleDelivered);
        }

        beginTest ("Network errors malformed responses and oversized bodies are visible");
        for (int failure = 0; failure < 3; ++failure)
        {
            LyricsService service (directory.getChildFile ("failure-" + juce::String (failure)),
                [failure] (const juce::URL&, const LyricsService::Cancelled&)
                {
                    if (failure == 0) return LyricsService::HttpResponse { 0, {}, "Offline" };
                    return LyricsService::HttpResponse { 200, failure == 1 ? "{broken" : juce::String::repeatedString ("x", 2 * 1024 * 1024 + 1), {} };
                });
            loaded.reset(); service.load (track, receive);
            if (waitLoaded()) expect (loaded->error.isNotEmpty());
        }
        beginTest ("Embedded text takes precedence over a downloaded cache");
        {
            juce::MemoryOutputStream comments;
            comments.writeInt (0); comments.writeInt (1);
            const juce::String value ("LYRICS=[00:05]Embedded lyric");
            comments.writeInt (static_cast<int> (value.getNumBytesAsUTF8()));
            comments.write (value.toRawUTF8(), value.getNumBytesAsUTF8());
            juce::MemoryOutputStream data;
            data.write ("fLaC", 4); data.writeByte (static_cast<char> (0x84));
            for (int shift = 2; shift >= 0; --shift)
                data.writeByte (static_cast<char> ((comments.getDataSize() >> (shift * 8)) & 0xff));
            data.write (comments.getData(), comments.getDataSize());
            auto embeddedTrack = track;
            embeddedTrack.libraryId = "embedded";
            const auto embeddedCache = directory.getChildFile ("embedded-cache");
            LyricsService service (embeddedCache, http);
            loaded.reset(); service.load (embeddedTrack, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
            expect (embeddedTrack.file.replaceWithData (data.getData(), data.getDataSize()));
            loaded.reset(); service.load (embeddedTrack, receive);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Embedded lyric"));
            expect (embeddedTrack.file.replaceWithText ("audio placeholder"));
        }

        beginTest ("An offline refresh keeps the previously downloaded document");
        {
            auto refreshTrack = track; refreshTrack.libraryId = "refresh";
            const auto refreshCache = directory.getChildFile ("refresh-cache");
            {
                LyricsService service (refreshCache, http);
                loaded.reset(); service.load (refreshTrack, receive);
                waitLoaded();
            }
            LyricsService service (refreshCache, [] (const juce::URL&, const LyricsService::Cancelled&)
                { return LyricsService::HttpResponse { 0, {}, "Offline" }; });
            loaded.reset(); service.load (refreshTrack, receive, true);
            if (waitLoaded())
            {
                expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
                expect (loaded->error.isNotEmpty());
            }
        }

        beginTest ("Instrumental and unsynchronized provider records remain distinct");
        for (const auto instrumental : { false, true })
        {
            LyricsService service (directory.getChildFile (instrumental ? "instrumental" : "plain"),
                [instrumental] (const juce::URL&, const LyricsService::Cancelled&)
                {
                    auto json = juce::JSON::parse (record());
                    auto* object = json.getDynamicObject();
                    object->setProperty ("instrumental", instrumental);
                    object->setProperty ("syncedLyrics", juce::var());
                    if (instrumental) object->setProperty ("plainLyrics", juce::var());
                    return LyricsService::HttpResponse { 200, juce::JSON::toString (json), {} };
                });
            loaded.reset(); service.load (track, receive);
            if (waitLoaded())
            {
                expect (loaded->state == LyricsLoadResult::State::ready);
                expect (loaded->document.instrumental == instrumental);
                expect (! loaded->document.isSynced());
                if (! instrumental) expectEquals (loaded->document.plainText, juce::String ("Network lyric"));
            }
        }

        beginTest ("Invalid identity metadata does not trigger an unconstrained lookup");
        {
            int invalidRequests = 0;
            LyricsService service (directory.getChildFile ("invalid-metadata"),
                [&] (const juce::URL&, const LyricsService::Cancelled&)
                { ++invalidRequests; return LyricsService::HttpResponse { 200, record(), {} }; });
            auto invalid = track; invalid.durationSeconds = 0.0;
            loaded.reset(); service.load (invalid, receive); waitLoaded();
            invalid.durationSeconds = 3601.0;
            loaded.reset(); service.load (invalid, receive); waitLoaded();
            invalid.durationSeconds = 120.0; invalid.artist = {};
            loaded.reset(); service.load (invalid, receive); waitLoaded();
            expectEquals (invalidRequests, 0);
        }

        beginTest ("Invalid saved offsets report an error without hiding valid lyrics");
        {
            const auto brokenCache = directory.getChildFile ("broken-offset");
            LyricsService service (brokenCache, http);
            expect (brokenCache.createDirectory().wasOk());
            expect (brokenCache.getChildFile (LyricsService::trackKey (track) + ".offset.json")
                .replaceWithText ("{\"seconds\":\"invalid\"}"));
            loaded.reset(); service.load (track, receive);
            if (waitLoaded())
            {
                expect (loaded->state == LyricsLoadResult::State::ready);
                expect (loaded->error.isNotEmpty());
                expectWithinAbsoluteError (loaded->offsetSeconds, 0.0, 0.00001);
            }
        }

        beginTest ("Confirmed selection survives navigation and reloads after restart");
        {
            const auto chosenCache = directory.getChildFile ("chosen-cache");
            {
                LyricsService service (chosenCache, http);
                LyricsSearchResult selected;
                selected.rawText = "[00:07]Chosen lyric";
                loaded.reset(); service.chooseResult (track, selected, receive);
                service.cancel();
                if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Chosen lyric"));
            }
            LyricsService service (chosenCache, http);
            loaded.reset(); service.load (track, receive, true);
            if (waitLoaded()) expectEquals (loaded->document.plainText, juce::String ("Chosen lyric"));
        }

        beginTest ("Shutdown drains confirmed imports and stable file identity ignores queue IDs");
        {
            auto savedTrack = track; savedTrack.libraryId = {}; savedTrack.fileIdentity = "posix:disk:file";
            auto moved = savedTrack; moved.file = directory.getChildFile ("moved.wav"); moved.id = "new-id";
            expectEquals (LyricsService::trackKey (savedTrack), LyricsService::trackKey (moved));
            expect (import.replaceWithText ("[00:06]Saved before shutdown"));
            const auto shutdownCache = directory.getChildFile ("shutdown-cache");
            {
                LyricsService service (shutdownCache, http);
                service.importFile (savedTrack, import, {});
                service.setTimingOffset (savedTrack, -0.75);
            }
            LyricsService service (shutdownCache, http);
            loaded.reset(); service.load (moved, receive);
            if (waitLoaded())
            {
                expectEquals (loaded->document.plainText, juce::String ("Saved before shutdown"));
                expectWithinAbsoluteError (loaded->offsetSeconds, -0.75, 0.00001);
            }
        }
        expect (directory.deleteRecursively());
    }
};
static LyricsServiceTests lyricsServiceTests;
