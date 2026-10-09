#include "SmartPlaylistRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool allows (SmartPlaylistField field, SmartPlaylistOperator comparison)
{
    using Field = SmartPlaylistField;
    using Op = SmartPlaylistOperator;
    switch (field)
    {
        case Field::title: case Field::artist: case Field::album:
        case Field::albumArtist: case Field::genre: case Field::format:
            return comparison == Op::contains || comparison == Op::notContains
                || comparison == Op::equals || comparison == Op::notEquals;
        case Field::year: case Field::channelCount: case Field::sampleRate:
            return comparison == Op::equals || comparison == Op::notEquals
                || comparison == Op::lessThan || comparison == Op::lessOrEqual
                || comparison == Op::greaterThan || comparison == Op::greaterOrEqual;
        case Field::addedWithinDays:
            return comparison == Op::withinLastDays;
        case Field::favourite: case Field::available:
            return comparison == Op::equals || comparison == Op::notEquals;
    }
    return false;
}

bool matchesRule (const SmartPlaylistRule& rule, const TrackItem& track,
                  bool favourite, juce::int64 nowMs)
{
    using Field = SmartPlaylistField;
    using Op = SmartPlaylistOperator;
    if (rule.field == Field::addedWithinDays)
        return track.addedAtMs > 0 && track.addedAtMs <= nowMs
            && nowMs - track.addedAtMs <= static_cast<juce::int64> (rule.numberValue) * 86400000;
    if (smartPlaylistValueKind (rule.field) == SmartPlaylistValueKind::boolean)
    {
        const auto value = rule.field == Field::favourite ? favourite : track.available;
        return (value == rule.boolValue) == (rule.comparison == Op::equals);
    }
    if (smartPlaylistValueKind (rule.field) == SmartPlaylistValueKind::number)
    {
        const auto value = rule.field == Field::year ? static_cast<double> (track.year)
                         : rule.field == Field::channelCount ? static_cast<double> (track.channelCount)
                         : track.sampleRate;
        switch (rule.comparison)
        {
            case Op::equals: return juce::exactlyEqual (value, rule.numberValue);
            case Op::notEquals: return ! juce::exactlyEqual (value, rule.numberValue);
            case Op::lessThan: return value < rule.numberValue;
            case Op::lessOrEqual: return value <= rule.numberValue;
            case Op::greaterThan: return value > rule.numberValue;
            case Op::greaterOrEqual: return value >= rule.numberValue;
            case Op::contains: case Op::notContains: case Op::withinLastDays: return false;
        }
        return false;
    }
    if (rule.field == Field::artist)
    {
        const auto names = track.artistNames();
        const auto text = rule.textValue.trim();
        const auto contains = rule.comparison == Op::contains || rule.comparison == Op::notContains;
        const auto anyMatch = std::any_of (names.begin(), names.end(), [&] (const auto& name)
        {
            return contains ? name.containsIgnoreCase (text) : name.equalsIgnoreCase (text);
        });
        return rule.comparison == Op::notContains || rule.comparison == Op::notEquals
            ? ! anyMatch : anyMatch;
    }
    const auto* value = &track.title;
    switch (rule.field)
    {
        case Field::title: break;
        case Field::artist: value = &track.artist; break;
        case Field::album: value = &track.album; break;
        case Field::albumArtist: value = &track.albumArtist; break;
        case Field::genre: value = &track.genre; break;
        case Field::format: value = &track.formatName; break;
        case Field::year: case Field::channelCount: case Field::sampleRate:
        case Field::addedWithinDays: case Field::favourite: case Field::available: return false;
    }
    const auto text = rule.textValue.trim();
    switch (rule.comparison)
    {
        case Op::contains: return value->containsIgnoreCase (text);
        case Op::notContains: return ! value->containsIgnoreCase (text);
        case Op::equals: return value->equalsIgnoreCase (text);
        case Op::notEquals: return ! value->equalsIgnoreCase (text);
        case Op::lessThan: case Op::lessOrEqual: case Op::greaterThan:
        case Op::greaterOrEqual: case Op::withinLastDays: return false;
    }
    return false;
}
}

SmartPlaylistValueKind smartPlaylistValueKind (SmartPlaylistField field)
{
    switch (field)
    {
        case SmartPlaylistField::year: case SmartPlaylistField::channelCount:
        case SmartPlaylistField::sampleRate: case SmartPlaylistField::addedWithinDays:
            return SmartPlaylistValueKind::number;
        case SmartPlaylistField::favourite: case SmartPlaylistField::available:
            return SmartPlaylistValueKind::boolean;
        case SmartPlaylistField::title: case SmartPlaylistField::artist:
        case SmartPlaylistField::album: case SmartPlaylistField::albumArtist:
        case SmartPlaylistField::genre: case SmartPlaylistField::format:
            return SmartPlaylistValueKind::text;
    }
    return SmartPlaylistValueKind::text;
}

std::vector<SmartPlaylistOperator> smartPlaylistOperatorsForField (SmartPlaylistField field)
{
    using Op = SmartPlaylistOperator;
    std::vector<Op> result;
    for (const auto comparison : { Op::contains, Op::notContains, Op::equals, Op::notEquals,
                                   Op::lessThan, Op::lessOrEqual, Op::greaterThan,
                                   Op::greaterOrEqual, Op::withinLastDays })
        if (allows (field, comparison)) result.push_back (comparison);
    return result;
}

juce::Result validateSmartPlaylistRules (const SmartPlaylistRules& rules)
{
    if (rules.match != SmartPlaylistMatch::all && rules.match != SmartPlaylistMatch::any)
        return juce::Result::fail ("Choose whether to match all or any rules.");
    if (rules.rules.empty() || rules.rules.size() > SmartPlaylistRules::maximumRules)
        return juce::Result::fail ("Use between 1 and 16 rules.");
    for (const auto& rule : rules.rules)
    {
        if (! allows (rule.field, rule.comparison))
            return juce::Result::fail ("The comparison is not supported for this field.");
        const auto kind = smartPlaylistValueKind (rule.field);
        if (kind == SmartPlaylistValueKind::text && rule.textValue.trim().isEmpty())
            return juce::Result::fail ("Enter text for each text rule.");
        if (kind == SmartPlaylistValueKind::number)
        {
            if (! std::isfinite (rule.numberValue) || rule.numberValue < 0.0)
                return juce::Result::fail ("Enter a non-negative number.");
            if (rule.field == SmartPlaylistField::addedWithinDays)
            {
                if (rule.numberValue < 1.0 || std::floor (rule.numberValue) != rule.numberValue
                    || rule.numberValue > static_cast<double> (std::numeric_limits<juce::int64>::max() / 86400000))
                    return juce::Result::fail ("Enter a positive whole number of days.");
            }
            else if (rule.field != SmartPlaylistField::sampleRate
                     && (std::floor (rule.numberValue) != rule.numberValue
                         || rule.numberValue > std::numeric_limits<int>::max()))
                return juce::Result::fail ("Enter a non-negative whole number.");
        }
    }
    return juce::Result::ok();
}

bool matchesSmartPlaylistRules (const SmartPlaylistRules& rules, const TrackItem& track,
                                bool favourite, juce::int64 nowMs)
{
    if (validateSmartPlaylistRules (rules).failed()) return false;
    const auto match = [&] (const auto& rule) { return matchesRule (rule, track, favourite, nowMs); };
    return rules.match == SmartPlaylistMatch::all
        ? std::all_of (rules.rules.begin(), rules.rules.end(), match)
        : std::any_of (rules.rules.begin(), rules.rules.end(), match);
}
