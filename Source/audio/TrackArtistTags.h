#pragma once

#include <juce_core/juce_core.h>

// Supplement flattened decoder metadata with format-defined artist values.
// ID3v2.3/2.4 TPE1, FLAC comments, and the first Ogg Vorbis logical stream.
// Unsupported, oversized, or malformed tags return no values for decoder fallback.
[[nodiscard]] juce::StringArray readTrackArtistTags (const juce::File&);
