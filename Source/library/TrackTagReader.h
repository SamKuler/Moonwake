#pragma once

#include <juce_core/juce_core.h>

struct TrackTagData
{
    juce::StringPairArray metadata;
    juce::StringArray artists;
};

// Reads metadata and format-defined artist values in one bounded container pass.
[[nodiscard]] TrackTagData readTrackTags (const juce::File&);

// Supplement flattened decoder metadata with format-defined artist values.
// ID3v2.3/2.4 TPE1, FLAC comments, and the first Ogg Vorbis logical stream.
// Unsupported, oversized, or malformed tags return no values for decoder fallback.
[[nodiscard]] juce::StringArray readTrackArtistTags (const juce::File&);

// ID3v2.3/2.4 USLT and FLAC/Ogg Vorbis LYRICS or UNSYNCEDLYRICS.
// Uses the same bounded container traversal as artist extraction; no file writes.
[[nodiscard]] juce::String readTrackLyricsTag (const juce::File&);

// Bounded title, album, album artist, genre, date, track and disc tags for decoders
// that expose only audio stream information. Native metadata keeps precedence.
[[nodiscard]] juce::StringPairArray readTrackMetadataTags (const juce::File&);
