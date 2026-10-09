#include "library/SmartPlaylistRules.h"

#include <limits>

namespace
{
class SmartPlaylistRulesTests final : public juce::UnitTest
{
public:
    SmartPlaylistRulesTests() : UnitTest ("Smart playlist rules", "library") {}

    void runTest() override
    {
        beginTest ("artist comparisons quantify complete members and negate the entire membership match");
        {
            TrackItem track;
            track.artist = "Shared display";
            track.artists = { "First Artist", "Second Artist" };
            SmartPlaylistRules rules { SmartPlaylistMatch::all,
                { { SmartPlaylistField::artist, SmartPlaylistOperator::equals, "second artist" } } };
            expect (matchesSmartPlaylistRules (rules, track, false, 0));
            rules.rules[0].comparison = SmartPlaylistOperator::notEquals;
            expect (! matchesSmartPlaylistRules (rules, track, false, 0));
            rules.rules[0].textValue = "Absent";
            expect (matchesSmartPlaylistRules (rules, track, false, 0));
            rules.rules[0] = { SmartPlaylistField::artist, SmartPlaylistOperator::contains, "SECOND" };
            expect (matchesSmartPlaylistRules (rules, track, false, 0));
            rules.rules[0].comparison = SmartPlaylistOperator::notContains;
            expect (! matchesSmartPlaylistRules (rules, track, false, 0));
            rules.rules[0].textValue = "Artist Second";
            expect (matchesSmartPlaylistRules (rules, track, false, 0));
            rules.rules[0].textValue = "Shared display";
            expect (matchesSmartPlaylistRules (rules, track, false, 0));
        }

        beginTest ("all and any combine case-insensitive text numeric and live favourite state");
        {
            TrackItem track;
            track.title = "Night Flight";
            track.artist = "ENSEMBLE";
            track.sampleRate = 96000.0;
            SmartPlaylistRules rules { SmartPlaylistMatch::all, {
                { SmartPlaylistField::artist, SmartPlaylistOperator::equals, "ensemble" },
                { SmartPlaylistField::sampleRate, SmartPlaylistOperator::greaterOrEqual, {}, 96000.0 },
                { SmartPlaylistField::favourite, SmartPlaylistOperator::equals, {}, 0.0, true } } };
            expect (validateSmartPlaylistRules (rules).wasOk());
            expect (matchesSmartPlaylistRules (rules, track, true, 1000));
            expect (! matchesSmartPlaylistRules (rules, track, false, 1000));
            rules.match = SmartPlaylistMatch::any;
            expect (matchesSmartPlaylistRules (rules, track, false, 1000));
            track.artist = "Other";
            track.sampleRate = 48000.0;
            expect (! matchesSmartPlaylistRules (rules, track, false, 1000));
            rules.rules = { { SmartPlaylistField::title, SmartPlaylistOperator::contains, "FLIGHT" } };
            expect (matchesSmartPlaylistRules (rules, track, false, 1000));
            rules.rules[0].comparison = SmartPlaylistOperator::notContains;
            expect (! matchesSmartPlaylistRules (rules, track, false, 1000));
            rules.rules[0] = { SmartPlaylistField::available, SmartPlaylistOperator::equals, {}, 0.0, false };
            track.available = false;
            expect (matchesSmartPlaylistRules (rules, track, false, 1000));
        }

        beginTest ("recent additions use an inclusive time window and exclude unknown or future dates");
        {
            constexpr juce::int64 day = 86400000;
            SmartPlaylistRules rules { SmartPlaylistMatch::all,
                { { SmartPlaylistField::addedWithinDays, SmartPlaylistOperator::withinLastDays, {}, 2.0 } } };
            TrackItem track;
            track.addedAtMs = 8 * day;
            expect (validateSmartPlaylistRules (rules).wasOk());
            expect (matchesSmartPlaylistRules (rules, track, false, 10 * day));
            expect (! matchesSmartPlaylistRules (rules, track, false, 10 * day + 1));
            track.addedAtMs = 0;
            expect (! matchesSmartPlaylistRules (rules, track, false, day));
            track.addedAtMs = 11 * day;
            expect (! matchesSmartPlaylistRules (rules, track, false, 10 * day));
        }

        beginTest ("invalid field operator values and rule counts reject the entire expression");
        {
            SmartPlaylistRules rules;
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules.push_back ({ SmartPlaylistField::title, SmartPlaylistOperator::contains, " " });
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules[0] = { SmartPlaylistField::year, SmartPlaylistOperator::contains, "2026" };
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules[0] = { SmartPlaylistField::sampleRate, SmartPlaylistOperator::equals, {},
                               std::numeric_limits<double>::quiet_NaN() };
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules[0] = { SmartPlaylistField::addedWithinDays, SmartPlaylistOperator::withinLastDays, {}, 0.0 };
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules[0].numberValue = 1.5;
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules[0] = { SmartPlaylistField::available, SmartPlaylistOperator::contains, {} };
            expect (validateSmartPlaylistRules (rules).failed());
            rules.rules[0] = { SmartPlaylistField::title, SmartPlaylistOperator::contains, "valid" };
            rules.match = static_cast<SmartPlaylistMatch> (42);
            expect (validateSmartPlaylistRules (rules).failed());
            rules.match = SmartPlaylistMatch::all;
            rules.rules.resize (17, rules.rules[0]);
            expect (validateSmartPlaylistRules (rules).failed());
            TrackItem track;
            track.title = "valid";
            expect (! matchesSmartPlaylistRules (rules, track, true, 1000));
        }
    }
};
SmartPlaylistRulesTests smartPlaylistRulesTests;
}
