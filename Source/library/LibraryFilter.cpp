#include "LibraryFilter.h"

#include <cstring>
#include <utility>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#else
 #include <array>
#endif

void LibraryFilter::setQuery (juce::String queryToUse)
{
    const auto trimmedQuery = queryToUse.trim();
    if (trimmedQuery == currentQuery)
        return;

    currentQuery = trimmedQuery;
    queryTokens.clear();
    queryTokens.addTokens (normalise (currentQuery), " \t\r\n", "");
    queryTokens.removeEmptyStrings (true);
}

void LibraryFilter::rebuild (const PlaylistModel& playlist)
{
    rebuild (playlist.getTracks());
}

void LibraryFilter::rebuild (const std::vector<TrackItem>& tracks)
{
    visiblePlaylistIndices.clear();
    visiblePlaylistIndices.reserve (tracks.size());

    if (queryTokens.isEmpty())
    {
        for (int index = 0; index < static_cast<int> (tracks.size()); ++index)
            visiblePlaylistIndices.push_back (index);
        return;
    }

    const auto generation = ++cacheGeneration;
    for (int index = 0; index < static_cast<int> (tracks.size()); ++index)
    {
        const auto* track = &tracks[static_cast<size_t> (index)];
        const auto& haystack = cachedSearchableText (*track, generation).text;
        bool matches = true;
        for (const auto& token : queryTokens)
            if (! haystack.contains (token))
            {
                matches = false;
                break;
            }
        if (matches)
            visiblePlaylistIndices.push_back (index);
    }
    removeUnusedCacheEntries (generation);
}

const juce::String& LibraryFilter::query() const noexcept
{
    return currentQuery;
}

int LibraryFilter::visibleCount() const noexcept
{
    return static_cast<int> (visiblePlaylistIndices.size());
}

int LibraryFilter::playlistIndexForRow (int visibleRow) const noexcept
{
    if (! juce::isPositiveAndBelow (
            visibleRow, static_cast<int> (visiblePlaylistIndices.size())))
        return -1;
    return visiblePlaylistIndices[static_cast<size_t> (visibleRow)];
}

int LibraryFilter::rowForPlaylistIndex (int playlistIndex) const noexcept
{
    for (size_t row = 0; row < visiblePlaylistIndices.size(); ++row)
        if (visiblePlaylistIndices[row] == playlistIndex)
            return static_cast<int> (row);
    return -1;
}

int LibraryFilter::searchableTextBuildCountForTesting() const noexcept
{
    return searchableTextBuildCount;
}

bool LibraryFilter::CachedSearchableText::matches (
    const TrackItem& track) const noexcept
{
    return valid
        && file == track.file
        && title == track.title
        && artist == track.artist
        && artists == track.artists
        && album == track.album
        && albumArtist == track.albumArtist
        && genre == track.genre
        && formatName == track.formatName
        && year == track.year
        && trackNumber == track.trackNumber
        && channelCount == track.channelCount
        && std::memcmp (&sampleRate, &track.sampleRate,
                        sizeof (sampleRate)) == 0;
}

void LibraryFilter::CachedSearchableText::updateFrom (const TrackItem& track)
{
    file = track.file;
    title = track.title;
    artist = track.artist;
    artists = track.artists;
    album = track.album;
    albumArtist = track.albumArtist;
    genre = track.genre;
    formatName = track.formatName;
    year = track.year;
    trackNumber = track.trackNumber;
    channelCount = track.channelCount;
    sampleRate = track.sampleRate;
    valid = true;
}

LibraryFilter::CachedSearchableText& LibraryFilter::cachedSearchableText (
    const TrackItem& track, size_t generation)
{
    auto& cached = searchCache[track.id];
    if (! cached.matches (track))
    {
        CachedSearchableText rebuilt;
        rebuilt.updateFrom (track);
        rebuilt.text = searchableText (track);
        rebuilt.lastSeenGeneration = generation;
        cached = std::move (rebuilt);
        ++searchableTextBuildCount;
    }
    cached.lastSeenGeneration = generation;
    return cached;
}

void LibraryFilter::removeUnusedCacheEntries (size_t generation)
{
    for (auto position = searchCache.begin(); position != searchCache.end();)
    {
        if (position->second.lastSeenGeneration != generation)
            position = searchCache.erase (position);
        else
            ++position;
    }
}

juce::String LibraryFilter::normalise (juce::String value)
{
    value = value.toLowerCase();
   #if JUCE_MAC
    const auto original = value.toCFString();
    const auto folded = CFStringCreateMutableCopy (kCFAllocatorDefault, 0, original);
    CFRelease (original);
    if (folded != nullptr)
    {
        CFStringNormalize (folded, kCFStringNormalizationFormC);
        CFStringFold (folded, kCFCompareCaseInsensitive | kCFCompareDiacriticInsensitive,
                      nullptr);
        static_cast<void> (CFStringTransform (folded, nullptr, CFSTR ("Latin-ASCII"), false));
        value = juce::String::fromCFString (folded);
        CFRelease (folded);
    }
   #else
    const std::array<std::pair<const char*, const char*>, 47> folds {{
        { "à", "a" }, { "á", "a" }, { "â", "a" }, { "ã", "a" },
        { "ä", "a" }, { "å", "a" }, { "ā", "a" }, { "ă", "a" },
        { "ą", "a" }, { "æ", "ae" }, { "ç", "c" }, { "ć", "c" },
        { "č", "c" }, { "ď", "d" }, { "è", "e" }, { "é", "e" },
        { "ê", "e" }, { "ë", "e" }, { "ē", "e" }, { "ė", "e" },
        { "ę", "e" }, { "ì", "i" }, { "í", "i" }, { "î", "i" },
        { "ï", "i" }, { "ī", "i" }, { "ł", "l" }, { "ñ", "n" },
        { "ń", "n" }, { "ò", "o" }, { "ó", "o" }, { "ô", "o" },
        { "õ", "o" }, { "ö", "o" }, { "ø", "o" }, { "ō", "o" },
        { "œ", "oe" }, { "ř", "r" }, { "š", "s" }, { "ß", "ss" },
        { "ť", "t" }, { "ù", "u" }, { "ú", "u" }, { "û", "u" },
        { "ü", "u" }, { "ý", "y" }, { "ž", "z" }
    }};
    for (const auto& [source, replacement] : folds)
        value = value.replace (juce::String::fromUTF8 (source),
                               juce::String::fromUTF8 (replacement));
   #endif
    juce::String result;
    bool previousWasSeparator = true;
    const auto isCombiningMark = [] (juce::juce_wchar character)
    {
        return (character >= 0x0300 && character <= 0x036f)
            || (character >= 0x1ab0 && character <= 0x1aff)
            || (character >= 0x1dc0 && character <= 0x1dff)
            || (character >= 0x20d0 && character <= 0x20ff)
            || (character >= 0xfe20 && character <= 0xfe2f);
    };
    for (int index = 0; index < value.length(); ++index)
    {
        const auto character = value[index];
        if (isCombiningMark (character))
            continue;
        if (juce::CharacterFunctions::isLetterOrDigit (character))
        {
            result += character;
            previousWasSeparator = false;
        }
        else if (! previousWasSeparator)
        {
            result += ' ';
            previousWasSeparator = true;
        }
    }
    return result.trim();
}

juce::String LibraryFilter::searchableText (const TrackItem& track)
{
    juce::StringArray fields {
        track.title, track.artist, track.album, track.albumArtist, track.genre,
        track.file.getFileName(), track.file.getFileExtension(), track.formatName,
        juce::String (track.year), juce::String (track.trackNumber),
        juce::String (track.channelCount), juce::String (track.sampleRate, 0)
    };
    fields.addArray (track.artists);
    return normalise (fields.joinIntoString (" "));
}
