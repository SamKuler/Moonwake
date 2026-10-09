#pragma once

#include "playlist/TrackItem.h"

#include <vector>

enum class SmartPlaylistMatch { all, any };
enum class SmartPlaylistField
{
    title, artist, album, albumArtist, genre, format,
    year, channelCount, sampleRate, addedWithinDays, favourite, available
};
enum class SmartPlaylistOperator
{
    contains, notContains, equals, notEquals,
    lessThan, lessOrEqual, greaterThan, greaterOrEqual, withinLastDays
};
enum class SmartPlaylistValueKind { text, number, boolean };

[[nodiscard]] SmartPlaylistValueKind smartPlaylistValueKind (SmartPlaylistField);
[[nodiscard]] std::vector<SmartPlaylistOperator> smartPlaylistOperatorsForField (SmartPlaylistField);

struct SmartPlaylistRule
{
    SmartPlaylistField field = SmartPlaylistField::title;
    SmartPlaylistOperator comparison = SmartPlaylistOperator::contains;
    juce::String textValue;
    double numberValue = 0.0;
    bool boolValue = true;

    bool operator== (const SmartPlaylistRule& other) const
    {
        return field == other.field && comparison == other.comparison
            && textValue == other.textValue && juce::exactlyEqual (numberValue, other.numberValue)
            && boolValue == other.boolValue;
    }
};

struct SmartPlaylistRules
{
    SmartPlaylistMatch match = SmartPlaylistMatch::all;
    std::vector<SmartPlaylistRule> rules;
    static constexpr size_t maximumRules = 16;

    bool operator== (const SmartPlaylistRules&) const = default;
};

[[nodiscard]] juce::Result validateSmartPlaylistRules (const SmartPlaylistRules&);
// Time is injected so previews, projections and saved copies can share one instant.
[[nodiscard]] bool matchesSmartPlaylistRules (const SmartPlaylistRules&, const TrackItem&,
                                              bool favourite, juce::int64 nowMs);
