#include <juce_core/juce_core.h>

#include "library/LibraryFilter.h"

namespace
{
TrackItem track (juce::String fileName, juce::String title,
                 juce::String artist, juce::String album,
                 juce::String format, int channels)
{
    TrackItem item { juce::File ("/Music").getChildFile (fileName),
                     std::move (title), 180.0 };
    item.artist = std::move (artist);
    item.album = std::move (album);
    item.formatName = std::move (format);
    item.channelCount = channels;
    return item;
}

class LibraryFilterTests final : public juce::UnitTest
{
public:
    LibraryFilterTests() : UnitTest ("LibraryFilter", "library") {}

    void runTest() override
    {
        PlaylistModel playlist;
        playlist.addTrack (track ("first.flac", "Halo",
                                  juce::String::fromUTF8 ("Beyoncé"),
                                  "Lemonade", "FLAC", 2));
        playlist.addTrack (track ("forest.wav", "Forest Light", "Sam Kuler", "Natural Works", "WAV", 6));
        playlist.addTrack (track ("night.aiff", "Night", "Moonwake", "Natural Works", "AIFF", 10));

        beginTest ("empty query preserves playback ordering");
        {
            LibraryFilter filter;
            filter.rebuild (playlist);
            expectEquals (filter.visibleCount(), 3);
            expectEquals (filter.playlistIndexForRow (0), 0);
            expectEquals (filter.playlistIndexForRow (2), 2);
        }

        beginTest ("matches folded case and multiple tokens with AND semantics");
        {
            LibraryFilter filter;
            filter.setQuery ("BEYONCE flac");
            filter.rebuild (playlist);
            expectEquals (filter.visibleCount(), 1);
            expectEquals (filter.playlistIndexForRow (0), 0);

            filter.setQuery ("natural 6");
            filter.rebuild (playlist);
            expectEquals (filter.visibleCount(), 1);
            expectEquals (filter.playlistIndexForRow (0), 1);
        }

        beginTest ("preserves non-Latin metadata in search");
        {
            PlaylistModel unicodePlaylist;
            unicodePlaylist.addTrack (track (
                "natural.flac", juce::String::fromUTF8 ("自然醒"),
                juce::String::fromUTF8 ("林间合奏"),
                juce::String::fromUTF8 ("晨光"), "FLAC", 8));
            LibraryFilter filter;
            filter.setQuery (juce::String::fromUTF8 ("自然 林间"));
            filter.rebuild (unicodePlaylist);
            expectEquals (filter.visibleCount(), 1);
            expectEquals (filter.playlistIndexForRow (0), 0);
        }

        beginTest ("matches canonically equivalent composed and decomposed text");
        {
            PlaylistModel unicodePlaylist;
            unicodePlaylist.addTrack (track (
                "cafe.flac", juce::String::fromUTF8 ("Cafe\xcc\x81"),
                "Artist", "Album", "FLAC", 2));
            LibraryFilter filter;
            filter.setQuery (juce::String::fromUTF8 ("Café"));
            filter.rebuild (unicodePlaylist);
            expectEquals (filter.visibleCount(), 1);
        }

        beginTest ("maps rows back to playlist identity after model changes");
        {
            LibraryFilter filter;
            filter.setQuery ("natural");
            filter.rebuild (playlist);
            expectEquals (filter.rowForPlaylistIndex (1), 0);
            expectEquals (filter.rowForPlaylistIndex (2), 1);
            expectEquals (filter.rowForPlaylistIndex (0), -1);

            expect (playlist.moveTrack (2, 0));
            filter.rebuild (playlist);
            expectEquals (filter.playlistIndexForRow (0), 0);
            expectEquals (filter.playlistIndexForRow (1), 2);
        }

        beginTest ("empty and repeated rebuilds reuse normalised track text");
        {
            PlaylistModel largePlaylist;
            constexpr int trackCount = 512;
            for (int index = 0; index < trackCount; ++index)
            {
                auto item = track ("track-" + juce::String (index) + ".flac",
                                   "Needle " + juce::String (index),
                                   "Artist", "Album", "FLAC", 2);
                item.id = "stable-" + juce::String (index);
                largePlaylist.addTrack (std::move (item));
            }

            LibraryFilter filter;
            filter.rebuild (largePlaylist);
            expectEquals (filter.visibleCount(), trackCount);
            expectEquals (filter.searchableTextBuildCountForTesting(), 0);

            filter.setQuery ("needle");
            filter.rebuild (largePlaylist);
            expectEquals (filter.visibleCount(), trackCount);
            expectEquals (filter.searchableTextBuildCountForTesting(), trackCount);

            filter.rebuild (largePlaylist);
            expectEquals (filter.searchableTextBuildCountForTesting(), trackCount);
            expect (largePlaylist.selectTrack (trackCount - 1));
            filter.rebuild (largePlaylist);
            expectEquals (filter.searchableTextBuildCountForTesting(), trackCount);

            filter.setQuery ({});
            filter.rebuild (largePlaylist);
            expectEquals (filter.visibleCount(), trackCount);
            expectEquals (filter.searchableTextBuildCountForTesting(), trackCount);
        }

        beginTest ("cache invalidates only the changed stable track");
        {
            PlaylistModel before;
            auto unchanged = track ("unchanged.flac", "Needle", "Artist",
                                    "Album", "FLAC", 2);
            unchanged.id = "unchanged-id";
            before.addTrack (std::move (unchanged));
            auto changed = track ("changed.flac", "Before", "Artist",
                                  "Album", "FLAC", 2);
            changed.id = "changed-id";
            before.addTrack (std::move (changed));

            LibraryFilter filter;
            filter.setQuery ("before");
            filter.rebuild (before);
            expectEquals (filter.visibleCount(), 1);
            expectEquals (filter.searchableTextBuildCountForTesting(), 2);

            PlaylistModel after;
            auto stillUnchanged = track ("unchanged.flac", "Needle", "Artist",
                                         "Album", "FLAC", 2);
            stillUnchanged.id = "unchanged-id";
            after.addTrack (std::move (stillUnchanged));
            auto nowChanged = track ("changed.flac", "After", "Artist",
                                     "Album", "FLAC", 2);
            nowChanged.id = "changed-id";
            after.addTrack (std::move (nowChanged));

            filter.rebuild (after);
            expectEquals (filter.visibleCount(), 0);
            expectEquals (filter.searchableTextBuildCountForTesting(), 3);
            filter.setQuery ("after");
            filter.rebuild (after);
            expectEquals (filter.visibleCount(), 1);
            expectEquals (filter.searchableTextBuildCountForTesting(), 3);
        }
    }
};

LibraryFilterTests libraryFilterTests;
} // namespace
