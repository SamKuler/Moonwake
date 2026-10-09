#include "LibraryProjection.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace
{
juce::String albumArtist (const TrackItem& track)
{
    return track.albumArtist.isNotEmpty() ? track.albumArtist : track.artist;
}

bool albumTrackLess (const TrackItem& a, const TrackItem& b)
{
    return std::make_tuple (a.discNumber, a.trackNumber, a.title)
         < std::make_tuple (b.discNumber, b.trackNumber, b.title);
}

TrackItem libraryRow (const TrackItem& track, const juce::String& occurrenceId)
{
    auto row = track;
    row.libraryId = track.id;
    row.id = occurrenceId;
    return row;
}
}

juce::String libraryAlbumId (const TrackItem& track)
{
    const auto artist = albumArtist (track);
    return "album:" + juce::String (artist.length()) + ":" + artist + track.album;
}

std::vector<LibraryCollection> libraryCollections (const MusicLibrary& library)
{
    std::vector<LibraryCollection> result {
        { "all", "All songs", {} }, { "favourites", "Favourites", "Playlists" }
    };
    for (const auto& playlist : library.getPlaylists())
        result.push_back ({ "playlist:" + playlist.id, playlist.name, "Playlists" });

    std::map<juce::String, juce::String> albums, artists;
    for (const auto& track : library.getTracks())
    {
        if (track.album.isNotEmpty())
        {
            const auto artist = albumArtist (track);
            albums[libraryAlbumId (track)] = track.album
                + (artist.isEmpty() ? juce::String{} : juce::String::fromUTF8 (" — ") + artist);
        }
        for (const auto& artist : track.artistNames())
            artists["artist:" + artist] = artist;
    }
    const auto appendSorted = [&result] (const auto& values, const juce::String& section)
    {
        std::vector<LibraryCollection> sorted;
        for (const auto& [id, name] : values) sorted.push_back ({ id, name, section });
        std::stable_sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b)
        {
            return a.name.compareNatural (b.name) < 0;
        });
        result.insert (result.end(), sorted.begin(), sorted.end());
    };
    appendSorted (albums, "Albums");
    appendSorted (artists, "Artists");
    return result;
}

std::vector<TrackItem> libraryTracks (const MusicLibrary& library,
                                     const PlaylistModel& queue,
                                     const juce::String& collectionId, juce::int64 nowMs)
{
    if (collectionId == "queue") return queue.getTracks();

    std::vector<TrackItem> result;
    if (collectionId.startsWith ("playlist:"))
    {
        if (const auto* playlist = library.getPlaylist (collectionId.substring (9)))
        {
            if (playlist->smartRules.has_value())
            {
                for (const auto& track : library.getTracks())
                    if (matchesSmartPlaylistRules (*playlist->smartRules, track, library.isFavourite (track.id), nowMs))
                        result.push_back (libraryRow (track, track.id));
            }
            else
                for (const auto& entry : playlist->entries)
                    if (const auto* track = library.getTrack (entry.trackId))
                        result.push_back (libraryRow (*track, entry.id));
        }
        return result;
    }
    const auto isAlbum = collectionId.startsWith ("album:");
    const auto isArtist = collectionId.startsWith ("artist:");
    for (const auto& track : library.getTracks())
    {
        if (collectionId == "favourites" && ! library.isFavourite (track.id)) continue;
        if (isAlbum && libraryAlbumId (track) != collectionId) continue;
        if (isArtist && ! track.artistNames().contains (collectionId.substring (7))) continue;
        result.push_back (libraryRow (track, track.id));
    }
    if (isAlbum)
        std::stable_sort (result.begin(), result.end(), albumTrackLess);
    return result;
}

namespace
{
void appendSummaryTrack (LibraryCollectionSummary& summary, const TrackItem& track)
{
    ++summary.trackCount;
    summary.durationSeconds += juce::jmax (0.0, track.durationSeconds);
    if (summary.artworkKeys.size() < 4 && track.artworkCacheKey.isNotEmpty())
        summary.artworkKeys.addIfNotAlreadyThere (track.artworkCacheKey);
}
}

std::vector<LibraryCollectionSummary> libraryCollectionSummaries (
    const MusicLibrary& library, const juce::String& category, juce::int64 nowMs)
{
    std::vector<LibraryCollectionSummary> result;
    if (category == "playlists")
    {
        LibraryCollectionSummary favourites;
        favourites.id = "favourites";
        favourites.name = "Favourites";
        favourites.subtitle = "Favourite songs";
        for (const auto& track : library.getTracks())
            if (library.isFavourite (track.id)) appendSummaryTrack (favourites, track);
        result.push_back (std::move (favourites));
        for (const auto& playlist : library.getPlaylists())
        {
            LibraryCollectionSummary summary;
            summary.id = "playlist:" + playlist.id;
            summary.name = playlist.name;
            summary.createdAtMs = playlist.createdAtMs;
            if (playlist.smartRules.has_value())
            {
                summary.subtitle = "Smart playlist";
                for (const auto& track : library.getTracks())
                    if (matchesSmartPlaylistRules (*playlist.smartRules, track, library.isFavourite (track.id), nowMs))
                        appendSummaryTrack (summary, track);
            }
            else
                for (const auto& entry : playlist.entries)
                    if (const auto* track = library.getTrack (entry.trackId)) appendSummaryTrack (summary, *track);
            if (playlist.artworkCacheKey.isNotEmpty()) summary.artworkKeys = { playlist.artworkCacheKey };
            result.push_back (std::move (summary));
        }
        return result;
    }
    const auto album = category == "albums";
    std::map<juce::String, std::vector<const TrackItem*>> groups;
    for (const auto& track : library.getTracks())
    {
        if (album)
        {
            if (track.album.isNotEmpty()) groups[libraryAlbumId (track)].push_back (&track);
        }
        else
            for (const auto& artist : track.artistNames())
                groups["artist:" + artist].push_back (&track);
    }
    for (auto& [id, tracks] : groups)
    {
        if (album)
            std::stable_sort (tracks.begin(), tracks.end(), [] (const auto* a, const auto* b)
                { return albumTrackLess (*a, *b); });
        LibraryCollectionSummary summary;
        summary.id = id;
        summary.name = album ? tracks.front()->album : id.substring (7);
        summary.subtitle = album ? albumArtist (*tracks.front()) : juce::String {};
        for (const auto* track : tracks) appendSummaryTrack (summary, *track);
        result.push_back (std::move (summary));
    }
    std::stable_sort (result.begin(), result.end(), [] (const auto& a, const auto& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });
    return result;
}

LibraryCollectionSummary libraryCollectionSummary (
    const MusicLibrary& library, const PlaylistModel& queue, const juce::String& collectionId, juce::int64 nowMs)
{
    LibraryCollectionSummary result;
    result.id = collectionId;
    if (collectionId == "all") result.name = "All songs";
    else if (collectionId == "favourites") result.name = "Favourites";
    else if (collectionId == "queue") result.name = "Play queue";
    else if (collectionId.startsWith ("playlist:"))
        if (const auto* playlist = library.getPlaylist (collectionId.substring (9)))
        {
            result.name = playlist->name;
            result.createdAtMs = playlist->createdAtMs;
            if (playlist->smartRules.has_value()) result.subtitle = "Smart playlist";
        }
    const auto tracks = libraryTracks (library, queue, collectionId, nowMs);
    if (! tracks.empty())
    {
        if (collectionId.startsWith ("album:"))
        {
            result.name = tracks.front().album;
            result.subtitle = albumArtist (tracks.front());
        }
        else if (collectionId.startsWith ("artist:")) result.name = collectionId.substring (7);
    }
    for (const auto& track : tracks) appendSummaryTrack (result, track);
    if (collectionId.startsWith ("playlist:"))
        if (const auto* playlist = library.getPlaylist (collectionId.substring (9));
            playlist != nullptr && playlist->artworkCacheKey.isNotEmpty())
            result.artworkKeys = { playlist->artworkCacheKey };
    return result;
}
