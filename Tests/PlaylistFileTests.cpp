#include "playlist/PlaylistFile.h"

#include <limits>

namespace
{
class PlaylistFileTests final : public juce::UnitTest
{
public:
    PlaylistFileTests() : UnitTest ("PlaylistFile", "playlist") {}

    void runTest() override
    {
        beginTest ("relative UTF-8 round trip preserves titles, order, duplicates and missing entries");
        const auto root = juce::File (MOONWAKE_SOURCE_DIR).getChildFile ("build-playlist-file-tests")
            .getChildFile (juce::Uuid().toString());
        expect (root.createDirectory().wasOk());
        const auto existing = root.getChildFile (juce::String::fromUTF8 ("自然 #1 + mix.flac"));
        expect (existing.replaceWithText ("audio placeholder"));
        const auto missing = root.getChildFile (juce::String::fromUTF8 ("缺失 song.wav"));
        const auto playlist = root.getChildFile ("exchange.m3u8");

        const auto writeText = [this, &playlist] (const juce::String& text)
        {
            expect (playlist.replaceWithText (text, false, false, nullptr));
        };

        {
            const std::vector<TrackItem> tracks {
                { existing, juce::String::fromUTF8 ("晨光, 第一首"), 12.75 },
                { missing, "Missing", -1.0 },
                { existing, "Second occurrence", 0.0 }
            };
            juce::String error = "old error";
            expect (PlaylistFile::writeM3u8 (playlist, tracks, true, error), error);
            expect (error.isEmpty(), error);
            const auto text = playlist.loadFileAsString();
            expect (text.startsWith ("#EXTM3U\n"));
            expect (text.contains ("./" + existing.getFileName() + "\n"));
            expect (! text.contains (root.getFullPathName()));
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            expectEquals (static_cast<int> (result.entries.size()), 3);
            expectEquals (result.warnings.size(), 1);
            if (result.entries.size() == 3)
            {
                expect (result.entries[0].file == existing);
                expectEquals (result.entries[0].title, tracks[0].title);
                expectWithinAbsoluteError (result.entries[0].durationSeconds, 12.75, 0.000001);
                expect (result.entries[1].file == missing);
                expectWithinAbsoluteError (result.entries[1].durationSeconds, -1.0, 0.000001);
                expect (result.entries[2].file == existing);
                expectEquals (result.entries[2].title, juce::String ("Second occurrence"));
                expectWithinAbsoluteError (result.entries[2].durationSeconds, 0.0, 0.000001);
            }
        }

       #if JUCE_WINDOWS
        beginTest ("relative export keeps cross-drive references absolute and round-trips missing files");
        {
            const juce::String otherDrive = root.getFullPathName().startsWithIgnoreCase ("D:") ? "C:\\" : "D:\\";
            const juce::File external (otherDrive + "moonwake-missing-" + juce::Uuid().toString() + "\\recording.flac");
            expect (! external.existsAsFile());
            const auto fallbackPath = external.getRelativePathFrom (playlist.getParentDirectory());
            expect (juce::File::isAbsolutePath (fallbackPath));
            expectEquals (fallbackPath, external.getFullPathName());
            const std::vector<TrackItem> tracks {
                { existing, "Same drive", 5.0 }, { external, "Cross-drive missing", 18.75 }
            };
            juce::String error;
            expect (PlaylistFile::writeM3u8 (playlist, tracks, true, error), error);
            const auto text = playlist.loadFileAsString();
            expect (text.contains ("\n./" + existing.getFileName() + "\n"));
            expect (text.contains ("\n" + external.getFullPathName() + "\n"));
            expect (! text.contains ("\n./" + external.getFullPathName() + "\n"));
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            expectEquals (result.warnings.size(), 1);
            expectEquals (static_cast<int> (result.entries.size()), 2);
            if (result.entries.size() == 2)
            {
                expect (result.entries[0].file == existing);
                expect (result.entries[1].file == external);
                expectEquals (result.entries[1].title, tracks[1].title);
                expectWithinAbsoluteError (result.entries[1].durationSeconds, 18.75, 0.000001);
            }
        }
       #endif

        beginTest ("BOM, CRLF, comments and parent-relative paths preserve optional metadata");
        {
            const auto nested = root.getChildFile ("lists");
            expect (nested.createDirectory().wasOk());
            const auto input = nested.getChildFile ("bom.m3u");
            const auto text = juce::String::fromUTF8 ("\xef\xbb\xbf")
                + "#EXTM3U\r\n \t\r\n# comment\r\n#EXTINF:12.5,Title, with comma\r\n"
                  "# between metadata and path\r\n../" + existing.getFileName()
                + "\r\n" + existing.getFullPathName() + "\r\n";
            expect (input.replaceWithData (text.toRawUTF8(), text.getNumBytesAsUTF8()));
            const auto result = PlaylistFile::read (input);
            expect (result.ok(), result.error);
            expectEquals (result.warnings.size(), 0);
            expectEquals (static_cast<int> (result.entries.size()), 2);
            if (result.entries.size() == 2)
            {
                expect (result.entries[0].file == existing);
                expectEquals (result.entries[0].title, juce::String ("Title, with comma"));
                expectWithinAbsoluteError (result.entries[0].durationSeconds, 12.5, 0.000001);
                expect (result.entries[1].file == existing);
                expect (result.entries[1].title.isEmpty());
                expectWithinAbsoluteError (result.entries[1].durationSeconds, -1.0, 0.000001);
            }
        }

        beginTest ("relative and absolute export represent directive-like names and literal spaces");
        {
            juce::StringArray names { "#directive.wav", "~literal.wav", "inner spaces.wav" };
           #if ! JUCE_WINDOWS
            names.addArray ({ "#EXTINF:4,filename.wav", "https:literal.wav", " leading and trailing .wav " });
           #endif
            std::vector<TrackItem> tracks;
            for (const auto& name : names)
            {
                const auto file = root.getChildFile ("./" + name);
                expect (file.replaceWithText ("audio placeholder"));
                tracks.emplace_back (file, "Title\r\n#EXTINF:4,Injected\nhttp://example.invalid", 3.5);
            }
            for (const auto relative : { true, false })
            {
                juce::String error;
                expect (PlaylistFile::writeM3u8 (playlist, tracks, relative, error), error);
                const auto result = PlaylistFile::read (playlist);
                expect (result.ok(), result.error);
                expectEquals (result.warnings.size(), 0);
                expectEquals (static_cast<int> (result.entries.size()), names.size());
                if (result.entries.size() == tracks.size())
                    for (size_t i = 0; i < tracks.size(); ++i)
                    {
                        expect (result.entries[i].file == tracks[i].file);
                        expect (! result.entries[i].title.containsAnyOf ("\r\n"));
                        expectEquals (result.entries[i].title,
                                      juce::String ("Title  #EXTINF:4,Injected http://example.invalid"));
                    }
                expect (playlist.loadFileAsString().contains (root.getFullPathName()) == ! relative);
            }
        }

        beginTest ("local file URLs decode Unicode, spaces, plus and escaped hash for local authorities");
        {
            const auto localUrl = juce::URL (existing).toString (true);
            const auto localhostUrl = "file://localhost" + localUrl.substring (7);
            writeText ("#EXTM3U\n#EXTINF:5,URL title\n" + localUrl + "\n" + localhostUrl + "\n");
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            expectEquals (result.warnings.size(), 0);
            expectEquals (static_cast<int> (result.entries.size()), 2);
            if (result.entries.size() == 2)
            {
                expect (result.entries[0].file == existing);
                expect (result.entries[1].file == existing);
                expectEquals (result.entries[0].title, juce::String ("URL title"));
            }
        }

        beginTest ("unsupported network references are skipped without leaking metadata to local entries");
        {
            writeText ("#EXTM3U\n#EXTINF:99,Remote metadata\nhttps://example.invalid/stream\n"
                       "rtsp://example.invalid/stream\n//example.invalid/share/song.flac\n"
                       "\\\\example.invalid\\share\\song.flac\nfile://example.invalid/song.flac\n"
                       "file:////example.invalid/song.flac\n" + existing.getFileName() + "\n");
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            expectEquals (result.warnings.size(), 6);
            expectEquals (static_cast<int> (result.entries.size()), 1);
            if (result.entries.size() == 1)
            {
                expect (result.entries[0].file == existing);
                expect (result.entries[0].title.isEmpty());
                expectWithinAbsoluteError (result.entries[0].durationSeconds, -1.0, 0.000001);
            }
        }

        beginTest ("malformed local URLs are diagnosed before decoding");
        {
            writeText ("file:///tmp/%ZZ.flac\nfile:///tmp/%00.flac\nfile:///tmp/%E2%28%A1.flac\n"
                       "file:///tmp/raw#hash.flac\nfile://localhost\n");
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            expect (result.entries.empty());
            expectEquals (result.warnings.size(), 5);
        }

        beginTest ("malformed duration is diagnosed while local file and title remain usable");
        {
            writeText ("#EXTINF:12oops,Title\n" + existing.getFileName()
                       + "\n#EXTINF:nan,Not finite\n" + existing.getFileName()
                       + "\n#EXTINF:4\n" + existing.getFileName() + "\n#EXTINF:10,Dangling\n");
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            expectEquals (static_cast<int> (result.entries.size()), 3);
            expectEquals (result.warnings.size(), 4);
            for (const auto& entry : result.entries)
                expectWithinAbsoluteError (entry.durationSeconds, -1.0, 0.000001);
            if (! result.entries.empty())
                expectEquals (result.entries[0].title, juce::String ("Title"));
        }

        beginTest ("invalid encoding, binary text and HLS fail without partially parsed entries");
        {
            const unsigned char invalid[][4] {
                { 0xe2, 0x28, 0xa1, 0x0a }, { 0xc0, 0xaf, 0x0a, 0x0a },
                { 0xed, 0xa0, 0x80, 0x0a }, { 0xf4, 0x90, 0x80, 0x80 },
                { 0xff, 0xfe, 0x41, 0x00 }, { 'a', 0x00, 'b', 0x0a }
            };
            for (const auto& bytes : invalid)
            {
                expect (playlist.replaceWithData (bytes, sizeof (bytes)));
                const auto result = PlaylistFile::read (playlist);
                expect (! result.ok());
                expect (result.error.contains ("UTF-8"));
                expect (result.entries.empty());
            }
            writeText (existing.getFileName() + "\n#EXT-X-TARGETDURATION:10\nsegment.ts\n");
            const auto hls = PlaylistFile::read (playlist);
            expect (! hls.ok());
            expect (hls.error.contains ("streaming"));
            expect (hls.entries.empty());
            expect (! PlaylistFile::read (root.getChildFile ("absent.m3u")).ok());
            expect (! PlaylistFile::read (root).ok());
        }

        beginTest ("empty playlists round trip and unknown durations remain unknown");
        {
            writeText ({});
            expect (PlaylistFile::read (playlist).ok());
            juce::String error;
            expect (PlaylistFile::writeM3u8 (playlist, {}, true, error), error);
            expect (PlaylistFile::read (playlist).entries.empty());
            expectEquals (playlist.loadFileAsString(), juce::String ("#EXTM3U\n"));
            const std::vector<TrackItem> tracks { { existing, "Unknown", std::numeric_limits<double>::infinity() } };
            expect (PlaylistFile::writeM3u8 (playlist, tracks, false, error), error);
            const auto result = PlaylistFile::read (playlist);
            expect (result.ok(), result.error);
            if (result.entries.size() == 1)
                expectWithinAbsoluteError (result.entries[0].durationSeconds, -1.0, 0.000001);
            else
                expect (false, "Expected the exported entry");
        }

        beginTest ("failed exports preserve existing target and clean temporary files");
        {
            writeText ("original playlist\n");
            const auto original = playlist.loadFileAsString();
            std::vector<TrackItem> tracks { { existing, "Valid", 1.0 }, { {}, "No path", 1.0 } };
            juce::String error;
            expect (! PlaylistFile::writeM3u8 (playlist, tracks, true, error));
            expect (error.isNotEmpty());
            expectEquals (playlist.loadFileAsString(), original);
            tracks[1].file = root.getChildFile ("line\nbreak.wav");
            expect (! PlaylistFile::writeM3u8 (playlist, tracks, true, error));
            expectEquals (playlist.loadFileAsString(), original);
            expect (! PlaylistFile::writeM3u8 (root, {}, true, error));
            expect (! PlaylistFile::writeM3u8 ({}, {}, true, error));
            expect (! PlaylistFile::writeM3u8 (root.getChildFile ("absent/export.m3u8"), {}, true, error));
            expect (error.isNotEmpty());
            expectEquals (root.findChildFiles (juce::File::findFiles, true, "*_temp*").size(), 0);
        }

        beginTest ("file and entry limits reject unbounded input and export without partial results");
        {
            const auto large = root.getChildFile ("large.m3u8");
            {
                juce::FileOutputStream output (large);
                expect (output.openedOk());
                expect (output.setPosition (static_cast<juce::int64> (PlaylistFile::maximumFileBytes)));
                expect (output.writeByte ('\n'));
            }
            const auto tooLarge = PlaylistFile::read (large);
            expect (! tooLarge.ok());
            expect (tooLarge.error.contains ("16 MiB"));
            expect (tooLarge.entries.empty());

            const auto line = juce::String ("https://example.invalid/stream\n");
            writeText (juce::String::repeatedString (line, static_cast<int> (PlaylistFile::maximumEntries + 1)));
            const auto tooMany = PlaylistFile::read (playlist);
            expect (! tooMany.ok());
            expect (tooMany.error.contains ("100,000"));
            expect (tooMany.entries.empty());
            expect (tooMany.warnings.size() <= 101);

            writeText ("original playlist\n");
            const std::vector<TrackItem> hugeTitle {
                { existing, juce::String::repeatedString ("x", static_cast<int> (PlaylistFile::maximumFileBytes)), 1.0 }
            };
            juce::String error;
            expect (! PlaylistFile::writeM3u8 (playlist, hugeTitle, true, error));
            expect (error.contains ("16 MiB"));
            expectEquals (playlist.loadFileAsString(), juce::String ("original playlist\n"));
        }

        expect (root.deleteRecursively());
    }
};

PlaylistFileTests playlistFileTests;
} // namespace
