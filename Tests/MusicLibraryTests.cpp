#include "library/MusicLibrary.h"
#include "fixtures/FileAlias.h"

namespace
{
class MusicLibraryTests final : public juce::UnitTest
{
public:
    MusicLibraryTests() : UnitTest ("MusicLibrary", "library") {}

    void runTest() override
    {
        beginTest ("smart playlist lifecycle preserves rules and rejects manual entry edits");
        {
            MusicLibrary library;
            TrackItem first { juce::File ("/music/smart-a.wav"), "A", 10.0 };
            first.genre = "Ambient";
            const auto a = library.upsertTrack (first);
            const auto b = library.upsertTrack ({ juce::File ("/music/smart-b.wav"), "B", 20.0 });
            SmartPlaylistRules rules { SmartPlaylistMatch::all,
                { { SmartPlaylistField::genre, SmartPlaylistOperator::equals, "ambient" } } };
            expect (library.createSmartPlaylist ("Invalid", {}).isEmpty());
            const auto id = library.createSmartPlaylist ("Quiet", rules);
            expect (id.isNotEmpty());
            if (id.isNotEmpty())
            {
                expect (library.getPlaylist (id)->smartRules == rules);
                expect (library.getPlaylist (id)->entries.empty());
                expect (library.addToPlaylist (id, { a }).empty());
                expect (! library.removeEntries (id, { "entry" }));
                expect (! library.moveEntries (id, { "entry" }, 0));
                expect (! library.replacePlaylistEntries (id, { { "entry", a } }));
                expect (library.renamePlaylist (id, "Still quiet"));
                expect (library.setPlaylistArtwork (id, "custom-cover"));
                const auto copy = library.duplicatePlaylist (id, "Quiet copy");
                expect (copy.isNotEmpty());
                if (copy.isNotEmpty()) expect (library.getPlaylist (copy)->smartRules == rules);
                const auto regular = library.materializeSmartPlaylist (id, "Saved songs", 1000);
                expect (regular.isNotEmpty());
                if (regular.isNotEmpty())
                {
                    const auto* saved = library.getPlaylist (regular);
                    expect (! saved->smartRules.has_value());
                    expectEquals (saved->artworkCacheKey, juce::String ("custom-cover"));
                    expectEquals (static_cast<int> (saved->entries.size()), 1);
                    if (! saved->entries.empty()) expectEquals (saved->entries.front().trackId, a);
                    expect (library.addToPlaylist (regular, { b }).size() == 1);
                    expect (! library.setSmartPlaylistRules (regular, rules));
                }
                rules.rules[0].textValue = "Rock";
                expect (library.setSmartPlaylistRules (id, rules));
                expect (! library.setSmartPlaylistRules (id, {}));
                expect (library.removePlaylist (id));
            }
        }

        beginTest ("import time survives metadata replacements and restored unknown timestamps stay unknown");
        {
            MusicLibrary library;
            const auto before = juce::Time::currentTimeMillis();
            const auto id = library.upsertTrack ({ juce::File ("/music/added-time.wav"), "Old", 1.0 });
            const auto added = library.getTrack (id)->addedAtMs;
            expect (added >= before && added <= juce::Time::currentTimeMillis());
            TrackItem updated { juce::File ("/music/added-time.wav"), "New", 2.0 };
            updated.addedAtMs = 123;
            expectEquals (library.upsertTrack (updated), id);
            expectEquals (library.getTrack (id)->addedAtMs, added);
            expect (library.replaceTrack (id, updated));
            expectEquals (library.getTrack (id)->addedAtMs, added);
            auto saved = library.getTracks();
            saved[0].addedAtMs = 0;
            expect (library.restore (saved, {}, {}));
            expectEquals (library.upsertTrack (updated), id);
            expectEquals (library.getTrack (id)->addedAtMs, juce::int64 { 0 });
        }

        beginTest ("restored smart playlists reject invalid rules and stored manual entries atomically");
        {
            MusicLibrary library;
            const auto existing = library.createPlaylist ("Existing");
            TrackItem track { juce::File ("/music/restore-smart.wav"), "Track", 10.0 };
            SavedPlaylist smart;
            smart.name = "Invalid";
            smart.smartRules = SmartPlaylistRules{};
            expect (! library.restore ({ track }, { smart }, {}));
            smart.smartRules->rules = { { SmartPlaylistField::title, SmartPlaylistOperator::contains, "Track" } };
            smart.entries = { { "manual", track.id } };
            expect (! library.restore ({ track }, { smart }, {}));
            expect (library.getPlaylist (existing) != nullptr);
        }

        beginTest ("file aliases merge while playlist occurrences remain independent");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                .getNonexistentChildFile ("music-library-tests", {});
            expect (directory.createDirectory());
            const auto file = directory.getChildFile ("original.wav");
            const auto alias = directory.getChildFile ("alias.wav");
            expect (file.replaceWithText ("audio fixture"));
            expect (createTestFileAlias (file, alias));
            MusicLibrary library;
            const auto id = library.upsertTrack ({ file, "Original", 1.0 });
            const auto aliasId = library.upsertTrack ({ alias, "Updated", 2.0 });
            expectEquals (aliasId, id);
            expectEquals (static_cast<int> (library.getTracks().size()), 1);
            expectEquals (library.getTrack (id)->title, juce::String ("Updated"));
            const auto a = library.createPlaylist ("Same name");
            const auto b = library.createPlaylist ("Same name");
            expect (a != b);
            const auto added = library.addToPlaylist (a, { id, id });
            expectEquals (static_cast<int> (added.size()), 2);
            expect (added[0].id != added[1].id);
            expect (library.addToPlaylist (b, { id }).size() == 1);
            expect (library.removeEntries (a, { added[0].id }));
            expect (library.getPlaylist (a)->entries == std::vector<PlaylistEntry> { added[1] });
            expect (library.getPlaylist (b)->entries.size() == 1);
            expect (file.existsAsFile());
            expect (directory.deleteRecursively());
        }

        beginTest ("relocation and metadata replacement preserve favourites and every reference");
        {
            MusicLibrary library;
            TrackItem missing { juce::File ("/missing/library-track.wav"), "Saved", 30.0 };
            missing.available = false;
            const auto id = library.upsertTrack (missing);
            const auto playlist = library.createPlaylist (juce::String::fromUTF8 ("收藏"));
            const auto entries = library.addToPlaylist (playlist, { id, id });
            expect (library.setFavourite (id, true));
            expect (! library.setFavourite (id, true));
            TrackItem relocated { juce::File (juce::String::fromUTF8 ("/music/重新定位.wav")), "New metadata", 60.0 };
            expect (library.replaceTrack (id, relocated));
            expectEquals (library.getTrack (id)->id, id);
            expect (library.getTrack (id)->file == relocated.file);
            expect (library.getPlaylist (playlist)->entries == entries);
            expect (library.isFavourite (id));
            expect (library.findTrackByFile (missing.file) == nullptr);
            expect (library.findTrackByFile (relocated.file) == library.getTrack (id));
            const auto otherId = library.upsertTrack ({ juce::File ("/music/another.wav"), "Other", 1.0 });
            expect (! library.replaceTrack (id, *library.getTrack (otherId)));
            expect (library.getTrack (id)->file == relocated.file);
            expect (library.findTrackByFile (relocated.file) == library.getTrack (id));
            expect (library.setFavourite (id, false));
            expect (! library.isFavourite (id));
            expect (! library.setFavourite ("unknown", true));
        }

        beginTest ("batch operations preserve list order and support exact entry restoration");
        {
            MusicLibrary library;
            std::vector<juce::String> ids;
            for (int i = 0; i < 5; ++i)
                ids.push_back (library.upsertTrack ({ juce::File ("/music/" + juce::String (i) + ".wav"),
                                                     juce::String (i), 1.0 }));
            const auto list = library.createPlaylist ("Ordered");
            const auto original = library.addToPlaylist (list, ids);
            expect (library.moveEntries (list, { original[3].id, original[1].id }, 5));
            expect (library.getPlaylist (list)->entries == std::vector<PlaylistEntry> {
                original[0], original[2], original[4], original[1], original[3] });
            expect (library.replacePlaylistEntries (list, original));
            expect (library.moveEntries (list, { original[3].id, original[1].id }, 0));
            expect (library.getPlaylist (list)->entries == std::vector<PlaylistEntry> {
                original[1], original[3], original[0], original[2], original[4] });
            expect (library.replacePlaylistEntries (list, original));
            expect (! library.moveEntries (list, { original[1].id, original[2].id }, 2));
            expect (library.removeEntries (list, { original[3].id, original[1].id }));
            expect (library.getPlaylist (list)->entries == std::vector<PlaylistEntry> {
                original[0], original[2], original[4] });
            expect (library.addToPlaylist (list, { ids[1], ids[1] }, 1).size() == 2);
            expect (library.getPlaylist (list)->entries[3] == original[2]);
            const auto unchanged = library.getPlaylist (list)->entries;
            expect (library.addToPlaylist (list, { ids[0], "unknown" }).empty());
            expect (library.getPlaylist (list)->entries == unchanged);
            expect (! library.replacePlaylistEntries (list, { original[0], original[0] }));
            expect (library.getPlaylist (list)->entries == unchanged);
        }

        beginTest ("playlist CRUD retains identities and duplicate lists receive independent entry IDs");
        {
            MusicLibrary library;
            const auto id = library.upsertTrack ({ juce::File ("/music/one.wav"), "One", 1.0 });
            const auto a = library.createPlaylist ("Original");
            const auto entries = library.addToPlaylist (a, { id, id });
            const auto b = library.duplicatePlaylist (a, "Copy");
            expect (b.isNotEmpty() && b != a);
            expect (library.getPlaylist (b)->entries[0].id != entries[0].id);
            expectEquals (library.getPlaylist (b)->entries[0].trackId, id);
            expect (library.renamePlaylist (a, "  Renamed  "));
            expectEquals (library.getPlaylist (a)->name, juce::String ("Renamed"));
            expect (library.getPlaylist (a)->entries == entries);
            expect (library.movePlaylist (b, 0));
            expectEquals (library.getPlaylists()[0].id, b);
            expect (library.removePlaylist (a));
            expect (library.getTrack (id) != nullptr);
            expect (library.getPlaylist (b) != nullptr);
            expect (library.createPlaylist ("   ").isEmpty());
        }

        beginTest ("playlist creation and copying record their own creation time");
        {
            MusicLibrary library;
            const auto before = juce::Time::currentTimeMillis();
            const auto created = library.createPlaylist ("New playlist");
            const auto after = juce::Time::currentTimeMillis();
            expect (library.getPlaylist (created)->createdAtMs >= before
                    && library.getPlaylist (created)->createdAtMs <= after);
            expectEquals (library.getPlaylist (created)->updatedAtMs,
                          library.getPlaylist (created)->createdAtMs);
            auto saved = library.getPlaylists();
            saved.front().createdAtMs = 100;
            saved.front().updatedAtMs = 200;
            expect (library.restore ({}, saved, {}));
            const auto copyBefore = juce::Time::currentTimeMillis();
            const auto copied = library.duplicatePlaylist (created, "Copy");
            const auto copyAfter = juce::Time::currentTimeMillis();
            expect (library.getPlaylist (copied)->createdAtMs >= copyBefore
                    && library.getPlaylist (copied)->createdAtMs <= copyAfter);
            expectEquals (library.getPlaylist (copied)->updatedAtMs,
                          library.getPlaylist (copied)->createdAtMs);
            expectEquals (library.getPlaylist (created)->createdAtMs, juce::int64 (100));
            expectEquals (library.getPlaylist (created)->updatedAtMs, juce::int64 (200));
        }

        beginTest ("successful playlist edits update modification time and preserve creation time");
        {
            MusicLibrary library;
            TrackItem track { juce::File ("/music/timestamp.wav"), "Track", 1.0 };
            track.id = "timestamp-track";
            SavedPlaylist saved, other;
            saved.id = "saved";
            saved.name = "Saved";
            saved.entries = { { "first", track.id }, { "second", track.id }, { "third", track.id } };
            saved.createdAtMs = 100;
            saved.updatedAtMs = 200;
            other.id = "other";
            other.name = "Other";
            other.createdAtMs = 300;
            other.updatedAtMs = 400;
            const auto expectModification = [&] (auto operation)
            {
                expect (library.restore ({ track }, { saved, other }, {}));
                const auto before = juce::Time::currentTimeMillis();
                expect (operation());
                const auto after = juce::Time::currentTimeMillis();
                expectEquals (library.getPlaylist (saved.id)->createdAtMs, saved.createdAtMs);
                expect (library.getPlaylist (saved.id)->updatedAtMs >= before
                        && library.getPlaylist (saved.id)->updatedAtMs <= after);
                expectEquals (library.getPlaylist (other.id)->updatedAtMs, other.updatedAtMs);
            };
            expectModification ([&] { return library.renamePlaylist (saved.id, "Renamed"); });
            expectModification ([&] { return ! library.addToPlaylist (saved.id, { track.id }).empty(); });
            expectModification ([&] { return library.removeEntries (saved.id, { "second" }); });
            expectModification ([&] { return library.moveEntries (saved.id, { "first" }, 3); });
            expectModification ([&] { return library.replacePlaylistEntries (saved.id, { saved.entries.back() }); });
            expectModification ([&] { return library.movePlaylist (saved.id, 1); });

            expect (library.restore ({ track }, { saved, other }, {}));
            expect (! library.renamePlaylist (saved.id, "Saved"));
            expect (! library.renamePlaylist (saved.id, "   "));
            expect (library.addToPlaylist (saved.id, {}).empty());
            expect (library.addToPlaylist (saved.id, { track.id, "unknown" }).empty());
            expect (! library.removeEntries (saved.id, {}));
            expect (! library.removeEntries (saved.id, { "first", "unknown" }));
            expect (! library.moveEntries (saved.id, { "first", "unknown" }, 3));
            expect (! library.moveEntries (saved.id, { "first" }, 1));
            expect (! library.moveEntries (saved.id, { "first" }, 4));
            expect (! library.movePlaylist (saved.id, 0));
            expect (! library.replacePlaylistEntries (saved.id, saved.entries));
            expect (library.getPlaylist (saved.id)->entries == saved.entries);
            expectEquals (library.getPlaylist (saved.id)->createdAtMs, saved.createdAtMs);
            expectEquals (library.getPlaylist (saved.id)->updatedAtMs, saved.updatedAtMs);
        }

        beginTest ("restored unknown playlist timestamps stay unknown until a playlist edit");
        {
            MusicLibrary library;
            TrackItem track { juce::File ("/music/legacy-timestamp.wav"), "Track", 1.0 };
            track.id = "legacy-track";
            SavedPlaylist saved;
            saved.name = "Legacy";
            saved.entries = { { "legacy-entry", track.id } };
            expect (library.restore ({ track }, { saved }, {}));
            expectEquals (library.getPlaylist (saved.id)->createdAtMs, juce::int64 (0));
            expectEquals (library.getPlaylist (saved.id)->updatedAtMs, juce::int64 (0));
            track.title = "Refreshed track";
            expect (library.replaceTrack (track.id, track));
            expectEquals (library.getPlaylist (saved.id)->updatedAtMs, juce::int64 (0));
            expect (library.renamePlaylist (saved.id, "Named"));
            expectEquals (library.getPlaylist (saved.id)->createdAtMs, juce::int64 (0));
            expect (library.getPlaylist (saved.id)->updatedAtMs > 0);
        }

        beginTest ("large batch entry edits retain source order and reject unknown IDs atomically");
        {
            MusicLibrary library;
            const auto track = library.upsertTrack ({ juce::File ("/music/repeated.wav"), "Repeated", 1.0 });
            const auto playlist = library.createPlaylist ("Large playlist");
            const auto original = library.addToPlaylist (playlist, std::vector<juce::String> (2048, track));
            std::vector<juce::String> selected;
            for (int i = 2046; i >= 0; i -= 2) selected.push_back (original[static_cast<size_t> (i)].id);
            selected.push_back (selected.front());
            auto invalid = selected;
            invalid.push_back ("unknown");
            const auto unchangedTime = library.getPlaylist (playlist)->updatedAtMs;
            expect (! library.removeEntries (playlist, invalid));
            expect (! library.moveEntries (playlist, invalid, 2048));
            expect (library.getPlaylist (playlist)->entries == original);
            expectEquals (library.getPlaylist (playlist)->updatedAtMs, unchangedTime);
            expect (library.moveEntries (playlist, selected, 2048));
            for (size_t i = 0; i < 1024; ++i)
            {
                expect (library.getPlaylist (playlist)->entries[i] == original[i * 2 + 1]);
                expect (library.getPlaylist (playlist)->entries[i + 1024] == original[i * 2]);
            }
            expect (library.removeEntries (playlist, selected));
            expectEquals (static_cast<int> (library.getPlaylist (playlist)->entries.size()), 1024);
            for (size_t i = 0; i < 1024; ++i)
                expect (library.getPlaylist (playlist)->entries[i] == original[i * 2 + 1]);
        }

        beginTest ("favourite membership follows restore, removal and independent copies without changing order");
        {
            MusicLibrary library;
            std::vector<juce::String> ids;
            for (int i = 0; i < 512; ++i)
                ids.push_back (library.upsertTrack ({ juce::File ("/music/favourite-" + juce::String (i) + ".wav"), "Track", 1.0 }));
            for (const auto& id : ids) expect (library.setFavourite (id, true));
            expect (library.getFavouriteTrackIds() == ids);
            for (const auto& id : ids) expect (library.isFavourite (id));
            auto copy = library;
            expect (copy.setFavourite (ids[0], false));
            expect (! copy.isFavourite (ids[0]));
            expect (library.isFavourite (ids[0]));
            expect (! copy.setFavourite (ids[0], false));
            expect (copy.getFavouriteTrackIds().front() == ids[1]);
            expect (! copy.restore (copy.getTracks(), {}, { ids[0], ids[0] }));
            expect (! copy.isFavourite (ids[0]) && copy.isFavourite (ids[1]));
            expect (! copy.restore (copy.getTracks(), {}, { "unknown" }));
            expect (copy.isFavourite (ids[1]));
            expect (copy.restore (copy.getTracks(), {}, { ids[4], ids[2] }));
            expect (copy.getFavouriteTrackIds() == std::vector<juce::String> { ids[4], ids[2] });
            expect (! copy.isFavourite (ids[1]) && copy.isFavourite (ids[2]));
            MusicLibrary moved = std::move (copy);
            expect (moved.isFavourite (ids[4]) && moved.isFavourite (ids[2]));
            expect (moved.restore (moved.getTracks(), {}, {}));
            expect (! moved.isFavourite (ids[4]) && moved.getFavouriteTrackIds().empty());
            expect (library.isFavourite (ids[4]));
        }

        beginTest ("invalid graph restoration cannot partially replace the current library");
        {
            MusicLibrary library;
            const auto id = library.upsertTrack ({ juce::File ("/music/one.wav"), "One", 1.0 });
            const auto list = library.createPlaylist ("Existing");
            library.addToPlaylist (list, { id });
            auto invalidPlaylists = library.getPlaylists();
            invalidPlaylists[0].entries[0].trackId = "missing-id";
            expect (! library.restore (library.getTracks(), invalidPlaylists, {}));
            expectEquals (library.getPlaylist (list)->entries[0].trackId, id);
            expect (! library.restore ({ *library.getTrack (id), *library.getTrack (id) }, {}, {}));
            expectEquals (static_cast<int> (library.getTracks().size()), 1);
            expect (! library.restore (library.getTracks(), library.getPlaylists(), { "unknown" }));
            expect (library.findTrackByFile (juce::File ("/music/one.wav")) == library.getTrack (id));
        }

        beginTest ("same-path file replacement refreshes alias identity without changing the canonical track");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                .getNonexistentChildFile ("music-library-replaced-file", {});
            expect (directory.createDirectory());
            const auto file = directory.getChildFile ("track.wav");
            const auto oldFile = directory.getChildFile ("old-track.wav");
            const auto alias = directory.getChildFile ("new-alias.wav");
            expect (file.replaceWithText ("original audio"));
            MusicLibrary library;
            const auto id = library.upsertTrack ({ file, "Original", 1.0 });
            expect (file.moveFileTo (oldFile));
            expect (file.replaceWithText ("replacement audio"));
            expect (createTestFileAlias (file, alias));
            expectEquals (library.upsertTrack ({ file, "Replacement", 2.0 }), id);
            expect (library.findTrackByFile (oldFile) == nullptr);
            expect (library.findTrackByFile (alias) == library.getTrack (id));
            expect (library.findTrackByFile (file) == library.getTrack (id));
            expectEquals (library.getTrack (id)->title, juce::String ("Replacement"));
            expectEquals (static_cast<int> (library.getTracks().size()), 1);
            expect (directory.deleteRecursively());
        }

        beginTest ("path lookup retains JUCE filename case semantics for missing files");
        {
            MusicLibrary library;
            const auto file = juce::File ("/missing/Moonwake-Indexed-Track.wav");
            const auto variant = juce::File ("/missing/moonwake-indexed-track.wav");
            const auto id = library.upsertTrack ({ file, "Original", 1.0 });
            expect ((library.findTrackByFile (variant) != nullptr) == (file == variant));
            const auto variantId = library.upsertTrack ({ variant, "Variant", 2.0 });
            expect ((variantId == id) == (file == variant));
        }

        beginTest ("restore retains shared physical identities and exact paths take precedence over aliases");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                .getNonexistentChildFile ("music-library-shared-identity", {});
            expect (directory.createDirectory());
            const auto first = directory.getChildFile ("first.wav");
            const auto second = directory.getChildFile ("second.wav");
            const auto alias = directory.getChildFile ("alias.wav");
            const auto relocated = directory.getChildFile ("relocated.wav");
            expect (first.replaceWithText ("shared audio"));
            expect (createTestFileAlias (first, second));
            expect (createTestFileAlias (first, alias));
            expect (relocated.replaceWithText ("other audio"));
            TrackItem a { first, "First", 1.0 }, b { second, "Second", 1.0 };
            a.id = "a";
            b.id = "b";
            MusicLibrary library;
            expect (library.restore ({ a, b }, {}, {}));
            expect (library.findTrackByFile (first) == library.getTrack ("a"));
            expect (library.findTrackByFile (second) == library.getTrack ("b"));
            expect (library.findTrackByFile (alias) == library.getTrack ("a"));
            expectEquals (library.upsertTrack ({ second, "Updated second", 2.0 }), juce::String ("b"));
            expectEquals (library.getTrack ("a")->title, juce::String ("First"));
            expectEquals (library.getTrack ("b")->title, juce::String ("Updated second"));
            expect (library.replaceTrack ("a", { relocated, "Relocated", 3.0 }));
            expect (library.findTrackByFile (alias) == library.getTrack ("b"));
            expect (library.findTrackByFile (relocated) == library.getTrack ("a"));
            expect (! library.replaceTrack ("a", { second, "Collision", 4.0 }));
            expect (! library.replaceTrack ("a", { alias, "Alias collision", 4.0 }));
            expect (library.findTrackByFile (relocated) == library.getTrack ("a"));
            expectEquals (library.getTrack ("a")->title, juce::String ("Relocated"));
            expect (! library.restore ({ a, a }, {}, {}));
            expect (library.findTrackByFile (alias) == library.getTrack ("b"));
            expect (library.findTrackByFile (relocated) == library.getTrack ("a"));
            expect (library.restore ({ b }, {}, {}));
            expect (library.getTrack ("a") == nullptr);
            expect (library.findTrackByFile (relocated) == nullptr);
            expect (library.findTrackByFile (alias) == library.getTrack ("b"));
            expect (directory.deleteRecursively());
        }

        beginTest ("ID and file lookup stay independent after growth, copy and move");
        {
            MusicLibrary original;
            for (int index = 0; index < 512; ++index)
            {
                TrackItem track { juce::File ("/music/index-" + juce::String (index) + ".wav"),
                                  juce::String (index), 1.0 };
                track.id = "id-" + juce::String (index);
                expectEquals (original.upsertTrack (track), track.id);
            }
            auto copied = original;
            MusicLibrary assigned;
            assigned = copied;
            MusicLibrary moved = std::move (copied);
            const auto relocated = juce::File ("/music/relocated-copy.wav");
            expect (moved.replaceTrack ("id-0", { relocated, "Moved copy", 2.0 }));
            expect (moved.findTrackByFile (original.getTrack ("id-0")->file) == nullptr);
            expect (original.findTrackByFile (relocated) == nullptr);
            expect (assigned.findTrackByFile (relocated) == nullptr);
            for (int index = 0; index < 512; ++index)
            {
                const auto id = "id-" + juce::String (index);
                expectEquals (original.getTracks()[static_cast<size_t> (index)].id, id);
                expectEquals (original.getTrack (id)->title, juce::String (index));
                expect (assigned.findTrackByFile (original.getTrack (id)->file) == assigned.getTrack (id));
                if (index != 0) expect (moved.findTrackByFile (original.getTrack (id)->file) == moved.getTrack (id));
            }
            TrackItem collision { juce::File ("/music/new-copy.wav"), "New", 1.0 };
            collision.id = "id-0";
            const auto inserted = moved.upsertTrack (collision);
            expect (inserted != collision.id);
            expect (moved.getTrack (inserted) == moved.findTrackByFile (collision.file));
            expect (original.getTrack (inserted) == nullptr);
            expectEquals (moved.getTrack ("id-0")->title, juce::String ("Moved copy"));
        }
    }
};

MusicLibraryTests musicLibraryTests;
} // namespace
