#include "skin/SkinFontResolver.h"

class SkinFontResolverTests final : public juce::UnitTest
{
public:
    SkinFontResolverTests() : UnitTest ("SkinFontResolver", "skin") {}

    void runTest() override
    {
        beginTest ("generic CSS families map to JUCE platform families");
        expectEquals (SkinFontResolver::resolveGenericFamily ("sans-serif"),
                      juce::Font::getDefaultSansSerifFontName());
        expectEquals (SkinFontResolver::resolveGenericFamily ("serif"),
                      juce::Font::getDefaultSerifFontName());
        expectEquals (SkinFontResolver::resolveGenericFamily ("monospace"),
                      juce::Font::getDefaultMonospacedFontName());
        expectEquals (SkinFontResolver::resolveGenericFamily ("Avenir Next"),
                      juce::String ("Avenir Next"));

        beginTest ("numeric weights select the closest real style");
        const juce::StringArray styles {
            "Regular", "Italic", "Medium", "Medium Italic",
            "Demi Bold", "Demi Bold Italic", "Bold", "Heavy"
        };
        expectEquals (SkinFontResolver::chooseClosestStyle (styles, 400, false),
                      juce::String ("Regular"));
        expectEquals (SkinFontResolver::chooseClosestStyle (styles, 500, false),
                      juce::String ("Medium"));
        expectEquals (SkinFontResolver::chooseClosestStyle (styles, 600, false),
                      juce::String ("Demi Bold"));
        expectEquals (SkinFontResolver::chooseClosestStyle (styles, 700, false),
                      juce::String ("Bold"));
        expectEquals (SkinFontResolver::chooseClosestStyle (styles, 500, true),
                      juce::String ("Medium Italic"));
    }
};

static SkinFontResolverTests skinFontResolverTests;
