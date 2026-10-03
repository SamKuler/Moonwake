#include "library/LibraryProjection.h"
#include "library/LibraryFilter.h"

#include <algorithm>

namespace
{
class LibraryProjectionTests final : public juce::UnitTest
{
public:
    LibraryProjectionTests() : UnitTest ("Library projection", "library") {}

    void runTest() override
    {
        beginTest ("playlist and queue projections retain independent occurrence identities");
        {
            MusicLibrary library;
            PlaylistModel queue;
            const auto a = library.upsertTrack ({ juce::File ("/music/A.wav"), "A", 30.0 });
            const auto b = library.upsertTrack ({ juce::File ("/music/B.wav"), "B", 60.0 });
            const auto playlist = library.createPlaylist ("Repeated");
            const auto entries = library.addToPlaylist (playlist, { a, b, a });
            auto rows = libraryTracks (library, queue, "playlist:" + playlist);
            expectEquals (static_cast<int> (rows.size()), 3);
            if (rows.size() == 3)
            {
                expectEquals (rows[0].libraryId, a);
                expectEquals (rows[2].libraryId, a);
                expect (rows[0].id != rows[2].id);
                for (size_t index = 0; index < rows.size(); ++index)
                    expectEquals (rows[index].id, entries[index].id);
                for (auto& row : rows) row.id = juce::Uuid().toString();
                expect (queue.replaceTracks (rows));
                const auto queued = libraryTracks (library, queue, "queue");
                expectEquals (queued[0].id, rows[0].id);
                expectEquals (queued[2].id, rows[2].id);
                expect (queued[0].id != entries[0].id);
                expectEquals (queued[2].libraryId, a);
            }
            const auto all = libraryTracks (library, queue, "all");
            expectEquals (static_cast<int> (all.size()), 2);
            for (const auto& row : all) expectEquals (row.id, row.libraryId);
            expect (libraryTracks (library, queue, "playlist:deleted").empty());
        }

        beginTest ("album groups use album artist and sort discs before track numbers");
        {
            MusicLibrary library;
            PlaylistModel queue;
            const auto add = [&] (const juce::String& title, const juce::String& artist,
                                 const juce::String& albumArtist, int disc, int number)
            {
                TrackItem track { juce::File ("/music/" + title + ".wav"), title, 10.0 };
                track.artist = artist;
                track.albumArtist = albumArtist;
                track.album = "Shared album";
                track.discNumber = disc;
                track.trackNumber = number;
                return library.upsertTrack (track);
            };
            const auto last = add ("Disc two", "Guest", "Ensemble", 2, 1);
            const auto second = add ("Second", "Soloist", "Ensemble", 1, 2);
            const auto first = add ("First", "Soloist", "Ensemble", 1, 1);
            const auto other = add ("Other recording", "Other artist", {}, 1, 1);
            const auto album = libraryAlbumId (*library.getTrack (first));
            const auto rows = libraryTracks (library, queue, album);
            expectEquals (static_cast<int> (rows.size()), 3);
            if (rows.size() == 3)
            {
                expectEquals (rows[0].libraryId, first);
                expectEquals (rows[1].libraryId, second);
                expectEquals (rows[2].libraryId, last);
            }
            expect (album != libraryAlbumId (*library.getTrack (other)));
            const auto soloist = libraryTracks (library, queue, "artist:Soloist");
            expectEquals (static_cast<int> (soloist.size()), 2);
            const auto collections = libraryCollections (library);
            expectEquals (static_cast<int> (std::count_if (collections.begin(), collections.end(),
                [] (const auto& item) { return item.section == "Albums"; })), 2);
            expectEquals (static_cast<int> (std::count_if (collections.begin(), collections.end(),
                [] (const auto& item) { return item.section == "Artists"; })), 3);
        }

        beginTest ("favourites and projected search preserve missing repeated entries");
        {
            MusicLibrary library;
            PlaylistModel queue;
            TrackItem track { juce::File ("/missing/recording.wav"), juce::String::fromUTF8 ("Café"), 10.0 };
            track.artist = "Artist";
            track.available = false;
            const auto id = library.upsertTrack (track);
            const auto playlist = library.createPlaylist ("Duplicates");
            const auto entries = library.addToPlaylist (playlist, { id, id });
            expect (library.setFavourite (id, true));
            const auto favourites = libraryTracks (library, queue, "favourites");
            expectEquals (static_cast<int> (favourites.size()), 1);
            if (! favourites.empty()) expect (! favourites.front().available);
            const auto rows = libraryTracks (library, queue, "playlist:" + playlist);
            LibraryFilter filter;
            filter.setQuery ("cafe artist");
            filter.rebuild (rows);
            expectEquals (filter.visibleCount(), 2);
            if (filter.visibleCount() == 2)
            {
                expectEquals (rows[static_cast<size_t> (filter.playlistIndexForRow (0))].id, entries[0].id);
                expectEquals (rows[static_cast<size_t> (filter.playlistIndexForRow (1))].id, entries[1].id);
            }
            expect (library.setFavourite (id, false));
            expect (libraryTracks (library, queue, "favourites").empty());
            expectEquals (static_cast<int> (libraryTracks (library, queue, "playlist:" + playlist).size()), 2);
        }

        beginTest ("playlist gallery summaries count occurrences and keep the first four distinct artwork references");
        {
            MusicLibrary library;
            PlaylistModel queue;
            const std::vector<double> durations { 12.5, 0.0, -8.0, 31.25, 2.0, 7.0, 9.0 };
            const juce::StringArray artwork { "cover-a", "cover-b", "cover-a", "", "cover-c", "cover-d", "cover-e" };
            std::vector<juce::String> ids;
            for (size_t index = 0; index < durations.size(); ++index)
            {
                TrackItem track { juce::File ("/music/gallery-" + juce::String (static_cast<int> (index)) + ".wav"),
                                  "Track " + juce::String (static_cast<int> (index)), durations[index] };
                track.artworkCacheKey = artwork[static_cast<int> (index)];
                track.available = index != 3;
                ids.push_back (library.upsertTrack (track));
            }
            const auto firstId = library.createPlaylist ("Shared name");
            const auto secondId = library.createPlaylist ("Shared name");
            library.addToPlaylist (firstId, { ids[0], ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6] });
            auto saved = library.getPlaylists();
            saved[0].createdAtMs = 1704164645123LL;
            saved[0].updatedAtMs = 1750000000789LL;
            saved[1].createdAtMs = saved[1].updatedAtMs = 0;
            expect (library.restore (library.getTracks(), saved, { ids[0], ids[1] }));
            const auto summaries = libraryCollectionSummaries (library, "playlists");
            expectEquals (static_cast<int> (summaries.size()), 2);
            if (summaries.size() == 2)
            {
                expectEquals (summaries[0].id, "playlist:" + firstId);
                expectEquals (summaries[1].id, "playlist:" + secondId);
                expectEquals (summaries[0].name, juce::String ("Shared name"));
                expectEquals (summaries[1].name, summaries[0].name);
                expectEquals (summaries[0].trackCount, 8);
                expectWithinAbsoluteError (summaries[0].durationSeconds, 74.25, 0.000001);
                expectEquals (summaries[0].createdAtMs, saved[0].createdAtMs);
                expect (summaries[0].artworkKeys == juce::StringArray { "cover-a", "cover-b", "cover-c", "cover-d" });
                expectEquals (summaries[1].trackCount, 0);
                expectWithinAbsoluteError (summaries[1].durationSeconds, 0.0, 0.000001);
                expectEquals (summaries[1].createdAtMs, juce::int64 { 0 });
                expect (summaries[1].artworkKeys.isEmpty());
                expect (libraryCollectionSummary (library, queue, "playlist:" + firstId) == summaries[0]);
            }
            const auto all = libraryCollectionSummary (library, queue, "all");
            expectEquals (all.name, juce::String ("All songs"));
            expectEquals (all.trackCount, 7);
            expectWithinAbsoluteError (all.durationSeconds, 61.75, 0.000001);
            expectEquals (all.createdAtMs, juce::int64 { 0 });
            const auto favourites = libraryCollectionSummary (library, queue, "favourites");
            expectEquals (favourites.trackCount, 2);
            expectWithinAbsoluteError (favourites.durationSeconds, 12.5, 0.000001);
            expect (favourites.artworkKeys == juce::StringArray { "cover-a", "cover-b" });
            for (const auto& id : { ids[0], ids[0], ids[6] })
            {
                auto occurrence = *library.getTrack (id);
                occurrence.libraryId = id;
                occurrence.id = juce::Uuid().toString();
                queue.addTrack (std::move (occurrence));
            }
            const auto queued = libraryCollectionSummary (library, queue, "queue");
            expectEquals (queued.name, juce::String ("Play queue"));
            expectEquals (queued.trackCount, 3);
            expectWithinAbsoluteError (queued.durationSeconds, 34.0, 0.000001);
            expect (queued.artworkKeys == juce::StringArray { "cover-a", "cover-e" });
            expectEquals (queued.createdAtMs, juce::int64 { 0 });
            const auto missing = libraryCollectionSummary (library, queue, "playlist:deleted");
            expectEquals (missing.trackCount, 0);
            expect (missing.name.isEmpty() && missing.artworkKeys.isEmpty());
            expectEquals (missing.createdAtMs, juce::int64 { 0 });
        }

        beginTest ("album and artist gallery summaries use their own metadata groups and natural album order");
        {
            MusicLibrary library;
            PlaylistModel queue;
            const auto add = [&] (const juce::String& title, const juce::String& artist,
                                 const juce::String& albumArtist, const juce::String& album,
                                 int disc, double duration, const juce::String& artwork)
            {
                TrackItem track { juce::File ("/music/" + title + ".wav"), title, duration };
                track.artist = artist;
                track.albumArtist = albumArtist;
                track.album = album;
                track.discNumber = disc;
                track.trackNumber = 1;
                track.artworkCacheKey = artwork;
                return library.upsertTrack (track);
            };
            const auto discTwo = add ("Disc two", "Solo Artist", "Ensemble", "Album 10", 2, 30.0, "disc-two");
            const auto discOne = add ("Disc one", "Guest Artist", "Ensemble", "Album 10", 1, 10.0, "disc-one");
            const auto fallback = add ("Earlier album", "Solo Artist", {}, "Album 2", 1, 20.0, "earlier");
            const auto other = add ("Other album", "Other Artist", "Other Ensemble", "Album 10", 1, 40.0, "other");
            add ("Untagged", {}, "Ensemble", {}, 0, 50.0, {});
            const auto albums = libraryCollectionSummaries (library, "albums");
            expectEquals (static_cast<int> (albums.size()), 3);
            if (! albums.empty()) expectEquals (albums.front().name, juce::String ("Album 2"));
            const auto ensembleId = libraryAlbumId (*library.getTrack (discOne));
            expectEquals (ensembleId, libraryAlbumId (*library.getTrack (discTwo)));
            expect (ensembleId != libraryAlbumId (*library.getTrack (other)));
            for (const auto& summary : albums)
            {
                expectEquals (summary.createdAtMs, juce::int64 { 0 });
                if (summary.id == ensembleId)
                {
                    expectEquals (summary.name, juce::String ("Album 10"));
                    expectEquals (summary.subtitle, juce::String ("Ensemble"));
                    expectEquals (summary.trackCount, 2);
                    expectWithinAbsoluteError (summary.durationSeconds, 40.0, 0.000001);
                    expect (summary.artworkKeys == juce::StringArray { "disc-one", "disc-two" });
                    const auto detail = libraryCollectionSummary (library, queue, summary.id);
                    expect (detail == summary, "Album cards and details share metadata and the projected track order.");
                }
                else if (summary.id == libraryAlbumId (*library.getTrack (fallback)))
                {
                    expectEquals (summary.subtitle, juce::String ("Solo Artist"));
                    expectEquals (summary.trackCount, 1);
                    expectWithinAbsoluteError (summary.durationSeconds, 20.0, 0.000001);
                }
                else
                {
                    expectEquals (summary.id, libraryAlbumId (*library.getTrack (other)));
                    expectEquals (summary.subtitle, juce::String ("Other Ensemble"));
                    expectEquals (summary.trackCount, 1);
                    expectWithinAbsoluteError (summary.durationSeconds, 40.0, 0.000001);
                }
            }
            const auto artists = libraryCollectionSummaries (library, "artists");
            expectEquals (static_cast<int> (artists.size()), 3);
            if (artists.size() == 3)
            {
                expectEquals (artists[0].name, juce::String ("Guest Artist"));
                expectEquals (artists[1].name, juce::String ("Other Artist"));
                expectEquals (artists[2].name, juce::String ("Solo Artist"));
                expectEquals (artists[2].id, juce::String ("artist:Solo Artist"));
                expectEquals (artists[2].trackCount, 2);
                expectWithinAbsoluteError (artists[2].durationSeconds, 50.0, 0.000001);
                expect (artists[2].artworkKeys == juce::StringArray { "disc-two", "earlier" });
                for (const auto& summary : artists) expectEquals (summary.createdAtMs, juce::int64 { 0 });
            }
        }

        beginTest ("album card covers follow track order before choosing the first four unique images");
        {
            MusicLibrary library;
            PlaylistModel queue;
            juce::String albumId;
            for (int number = 6; number >= 1; --number)
            {
                TrackItem track { juce::File ("/music/reversed-album-" + juce::String (number) + ".wav"),
                                  "Song " + juce::String (number), 10.0 };
                track.artist = "Performer";
                track.albumArtist = "Album Artist";
                track.album = "Out of order imports";
                track.discNumber = 1;
                track.trackNumber = number;
                track.artworkCacheKey = "cover-" + juce::String (number);
                albumId = libraryAlbumId (track);
                library.upsertTrack (track);
            }
            const auto cards = libraryCollectionSummaries (library, "albums");
            expectEquals (static_cast<int> (cards.size()), 1);
            if (! cards.empty())
            {
                expectEquals (cards[0].trackCount, 6);
                expectWithinAbsoluteError (cards[0].durationSeconds, 60.0, 0.000001);
                expect (cards[0].artworkKeys == juce::StringArray { "cover-1", "cover-2", "cover-3", "cover-4" });
                expect (libraryCollectionSummary (library, queue, albumId) == cards[0]);
            }
        }

        beginTest ("album keys distinguish concatenation collisions");
        {
            TrackItem first, second;
            first.artist = "ab";
            first.album = "c";
            second.artist = "a";
            second.album = "bc";
            expect (libraryAlbumId (first) != libraryAlbumId (second));
        }
    }
};

LibraryProjectionTests libraryProjectionTests;
}
