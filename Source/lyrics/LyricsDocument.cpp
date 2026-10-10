#include "LyricsDocument.h"

#include <algorithm>
#include <cmath>

namespace
{
bool decimal (const juce::String& text)
{
    return text.isNotEmpty() && text.containsOnly ("0123456789");
}

bool timestamp (const juce::String& text, double& seconds)
{
    const auto colon = text.indexOfChar (':');
    if (colon < 1 || colon > 4) return false;
    const auto minutes = text.substring (0, colon);
    const auto remainder = text.substring (colon + 1);
    const auto dot = remainder.indexOfChar ('.');
    const auto whole = dot < 0 ? remainder : remainder.substring (0, dot);
    const auto fraction = dot < 0 ? juce::String() : remainder.substring (dot + 1);
    if (! decimal (minutes) || whole.length() != 2 || ! decimal (whole)
        || whole.getIntValue() >= 60
        || (dot >= 0 && (! decimal (fraction) || fraction.length() > 3))) return false;
    auto milliseconds = fraction.isEmpty() ? 0 : fraction.getIntValue();
    for (int places = fraction.length(); places < 3; ++places) milliseconds *= 10;
    seconds = static_cast<double> (minutes.getIntValue() * 60000 + whole.getIntValue() * 1000 + milliseconds) / 1000.0;
    return true;
}
}

int LyricsDocument::activeLineAt (double positionSeconds) const noexcept
{
    if (! std::isfinite (positionSeconds)) return -1;
    const auto next = std::upper_bound (lines.begin(), lines.end(), positionSeconds,
        [] (double time, const LyricLine& line) { return time < line.timeSeconds; });
    return next == lines.begin() ? -1 : static_cast<int> (next - lines.begin() - 1);
}

juce::Result parseLyrics (const juce::String& input, LyricsDocument& document)
{
    document = {};
    if (input.getNumBytesAsUTF8() > 1024 * 1024)
        return juce::Result::fail ("Lyrics exceed the 1 MiB limit.");
    auto text = input.replace ("\r\n", "\n").replaceCharacter ('\r', '\n');
    if (text.startsWithChar (0xfeff)) text = text.substring (1);
    const auto rows = juce::StringArray::fromLines (text);
    if (rows.size() > 20000) return juce::Result::fail ("Lyrics contain too many lines.");
    LyricsDocument parsed;
    juce::StringArray plain;
    double offsetSeconds = 0.0;
    bool malformedTiming = false;
    for (auto row : rows)
    {
        if (row.length() > 16384) return juce::Result::fail ("A lyric line is too long.");
        row = row.trim();
        std::vector<double> times;
        bool metadata = false, badTiming = false;
        while (row.startsWithChar ('['))
        {
            const auto close = row.indexOfChar (']');
            if (close < 0)
            {
                if (row.length() > 1 && juce::CharacterFunctions::isDigit (row[1]))
                    malformedTiming = badTiming = true;
                break;
            }
            const auto tag = row.substring (1, close);
            double time = 0.0;
            if (timestamp (tag, time)) times.push_back (time);
            else if (tag.startsWithIgnoreCase ("offset:"))
            {
                const auto value = tag.substring (7).trim();
                const auto digits = value.startsWithChar ('-') || value.startsWithChar ('+') ? value.substring (1) : value;
                if (! decimal (digits) || digits.length() > 8)
                    return juce::Result::fail ("Invalid lyric offset.");
                offsetSeconds = static_cast<double> (value.getIntValue()) / 1000.0;
                metadata = true;
            }
            else if (tag.isNotEmpty() && juce::CharacterFunctions::isDigit (tag[0]))
            {
                malformedTiming = badTiming = true;
            }
            else if (tag.containsChar (':')) metadata = true;
            else break;
            row = row.substring (close + 1).trimStart();
        }
        if (badTiming && times.empty()) continue;
        if (metadata && times.empty() && row.isEmpty()) continue;
        plain.add (row);
        for (const auto time : times)
        {
            if (parsed.lines.size() >= 20000) return juce::Result::fail ("Lyrics contain too many timestamps.");
            parsed.lines.push_back ({ time, row });
        }
    }
    parsed.plainText = plain.joinIntoString ("\n").trim();
    if (parsed.plainText.isEmpty() || (malformedTiming && parsed.lines.empty()))
        return juce::Result::fail ("No readable lyrics were found.");
    std::stable_sort (parsed.lines.begin(), parsed.lines.end(),
        [] (const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    std::vector<LyricLine> grouped;
    for (auto& line : parsed.lines)
    {
        line.timeSeconds -= offsetSeconds;
        if (! grouped.empty() && std::abs (grouped.back().timeSeconds - line.timeSeconds) < 0.0005)
        {
            if (line.text.isNotEmpty())
                grouped.back().text += (grouped.back().text.isEmpty() ? "" : "\n") + line.text;
        }
        else grouped.push_back (std::move (line));
    }
    parsed.lines = std::move (grouped);
    document = std::move (parsed);
    return juce::Result::ok();
}
