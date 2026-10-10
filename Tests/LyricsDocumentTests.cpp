#include "lyrics/LyricsDocument.h"
#include <cmath>

class LyricsDocumentTests final : public juce::UnitTest
{
public:
    LyricsDocumentTests() : UnitTest ("LyricsDocument", "lyrics") {}
    void runTest() override
    {
        beginTest ("Multiple timestamps and equal-time translations share a seek target");
        LyricsDocument document;
        expect (parseLyrics (juce::String::fromUTF8 ("[ar:Artist]\n[00:01.2][00:03.250]Hello\n[00:01.200]你好\n[00:05]End"), document).wasOk());
        expectEquals (static_cast<int> (document.lines.size()), 3);
        if (document.lines.size() == 3)
        {
            expectEquals (document.lines[0].text, juce::String::fromUTF8 ("Hello\n你好"));
            expectWithinAbsoluteError (document.lines[0].timeSeconds, 1.2, 0.00001);
            expectWithinAbsoluteError (document.lines[1].timeSeconds, 3.25, 0.00001);
            expectEquals (document.activeLineAt (1.199), -1);
            expectEquals (document.activeLineAt (1.2), 0);
            expectEquals (document.activeLineAt (4.0), 1);
            expectEquals (document.activeLineAt (100.0), 2);
            expectEquals (document.activeLineAt (NAN), -1);
        }

        beginTest ("File offsets apply globally and BOM and CRLF preserve Unicode");
        expect (parseLyrics (juce::String::charToString (0xfeff)
            + juce::String::fromUTF8 ("[00:02.00]世界\r\n[offset:500]\r\n[00:01.50]First"), document).wasOk());
        if (document.lines.size() == 2)
        {
            expectWithinAbsoluteError (document.lines[0].timeSeconds, 1.0, 0.00001);
            expectWithinAbsoluteError (document.lines[1].timeSeconds, 1.5, 0.00001);
            expectEquals (document.lines[1].text, juce::String::fromUTF8 ("世界"));
        }
        else expect (false, "Offset fixture must contain two timestamped lines");

        beginTest ("Plain lyrics have no synchronized hit target and keep blank verses");
        expect (parseLyrics ("First verse\r\n\r\nSecond verse", document).wasOk());
        expect (! document.isSynced());
        expectEquals (document.plainText, juce::String ("First verse\n\nSecond verse"));
        expectEquals (document.activeLineAt (50), -1);

        beginTest ("Malformed timing does not silently become seekable lyrics");
        expect (parseLyrics ("[00:99.2]Broken\n[12:xx]Wrong", document).failed());
        expect (! document.isSynced());
        expect (parseLyrics ("[00:03Missing closing bracket", document).failed());
        expect (parseLyrics ("[Verse 1]\nA readable verse", document).wasOk());
        expect (parseLyrics ("[00:02]Good\n[00:99]Broken\nA plain bridge", document).wasOk());
        expectEquals (static_cast<int> (document.lines.size()), 1);
        expect (document.plainText.contains ("A plain bridge"));
        expect (parseLyrics ("[ar:Artist]\n[ti:Title]", document).failed());
        expect (parseLyrics (" \n\t", document).failed());
        expect (parseLyrics (juce::String::repeatedString ("a", 1024 * 1024 + 1), document).failed());
        expect (! document.isSynced());
    }
};
static LyricsDocumentTests lyricsDocumentTests;
