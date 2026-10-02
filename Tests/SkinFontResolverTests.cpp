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

        beginTest ("generic-only font stacks retain their requested platform family category");
        for (const auto* generic : { "sans-serif", "serif", "monospace" })
        {
            SkinFontValue value;
            value.families = { generic };
            const auto face = SkinFontResolver::resolve (value, {}, value.families);
            expectEquals (face.family, SkinFontResolver::resolveGenericFamily (generic));
            const auto font = SkinFontResolver::makeFont (face, 17.0f);
            expect (font.getTypefacePtr() != nullptr);
            if (juce::String (generic) == "monospace")
                expectWithinAbsoluteError (juce::GlyphArrangement::getStringWidth (font, "iiii"),
                                           juce::GlyphArrangement::getStringWidth (font, "WWWW"), 0.01f);
        }

        beginTest ("missing first family falls through to an available monospaced family");
        {
            const auto mono = juce::Font::findAllTypefaceStyles ("Menlo").isEmpty()
                ? juce::Font::getDefaultMonospacedFontName() : juce::String ("Menlo");
            SkinFontValue value;
            value.families = { "Moonwake nonexistent font regression", mono, "monospace" };
            value.weight = 500;
            const auto resolved = SkinFontResolver::resolve (value, {}, value.families);
            expectEquals (resolved.family, mono);
            for (const auto& font : { SkinFontResolver::makeFont (resolved, 17.0f),
                                     SkinFontResolver::makeFallbackFont (value, 17.0f) })
            {
                expectWithinAbsoluteError (juce::GlyphArrangement::getStringWidth (font, "iiii"),
                                           juce::GlyphArrangement::getStringWidth (font, "WWWW"), 0.01f);
                expectWithinAbsoluteError (font.getHeight(), 17.0f, 0.001f);
            }
        }

        beginTest ("real regular medium and demi-bold styles retain requested numeric weights");
        {
            const auto styles = juce::Font::findAllTypefaceStyles ("Avenir Next");
            if (! styles.isEmpty())
                for (const auto& [weight, style] : { std::pair { 400, "Regular" }, { 500, "Medium" }, { 600, "Demi Bold" } })
                {
                    SkinFontValue value;
                    value.families = { "Avenir Next", "sans-serif" };
                    value.weight = weight;
                    const auto resolved = SkinFontResolver::resolve (value, {}, value.families);
                    expectEquals (resolved.style, juce::String (style));
                    expectEquals (SkinFontResolver::makeFont (resolved, 17.0f).getTypefacePtr()->getStyle(), juce::String (style));
                }
        }

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
