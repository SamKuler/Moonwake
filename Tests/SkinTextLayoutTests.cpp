#include "skin/SkinRenderer.h"
#include "skin/SkinAssetCache.h"
#include "skin/SkinBuiltInPackages.h"

#include <juce_graphics/juce_graphics.h>

#include <cmath>

namespace
{
class TextFaultSink final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override { failed = true; }
    bool failed = false;
};

SkinRenderProgram textProgram (const juce::String& text, const juce::String& overflow,
                               float fontSize, int maxLines, juce::Rectangle<float> frame)
{
    SkinRenderLayer layer;
    layer.id = "label";
    layer.primitive = SkinPrimitive::text;
    const auto set = [&] (const juce::String& path, SkinCompiledRenderValue value)
    {
        const auto* descriptor = skinRenderPropertyDescriptor (SkinPrimitive::text, path);
        layer.properties[path] = { std::move (value), descriptor->fallback, descriptor, false, {}, {}, 0, {} };
    };
    set ("visible", true);
    set ("opacity", 1.0);
    set ("clip", false);
    for (const auto* name : { "transform.translateX", "transform.translateY", "transform.rotation",
                              "shadow.radius", "shadow.offsetX", "shadow.offsetY" })
        set (name, 0.0);
    set ("transform.scaleX", 1.0);
    set ("transform.scaleY", 1.0);
    set ("shadow.color", juce::Colours::transparentBlack);
    set ("frame.x", static_cast<double> (frame.getX()));
    set ("frame.y", static_cast<double> (frame.getY()));
    set ("frame.width", static_cast<double> (frame.getWidth()));
    set ("frame.height", static_cast<double> (frame.getHeight()));
    set ("text", text);
    set ("font", SkinFontValue {});
    set ("fontSize", static_cast<double> (fontSize));
    set ("color", juce::Colours::white);
    set ("align", juce::String ("topLeft"));
    set ("maxLines", static_cast<double> (maxLines));
    set ("overflow", overflow);
    SkinRenderProgram program;
    program.target = SkinRenderTarget::label;
    program.baseLayers.push_back (std::move (layer));
    const auto& names = skinVariantNamesInMergeOrder();
    for (size_t index = 0; index < names.size(); ++index) program.variants[index].name = names[index];
    program.worstCaseOperations = 1;
    return program;
}

juce::Image blankImage (juce::Rectangle<float> frame)
{
    return { juce::Image::ARGB, static_cast<int> (std::ceil (frame.getRight())) + 8,
             static_cast<int> (std::ceil (frame.getBottom())) + 8, true };
}

bool samePixels (const juce::Image& left, const juce::Image& right)
{
    if (left.getBounds() != right.getBounds()) return false;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            if (left.getPixelAt (x, y) != right.getPixelAt (x, y)) return false;
    return true;
}

juce::Image referenceLines (const juce::String& text, float fontSize, juce::Rectangle<float> frame)
{
    auto image = blankImage (frame);
    juce::Graphics graphics (image);
    graphics.reduceClipRegion (frame.getSmallestIntegerContainer());
    juce::AttributedString string;
    string.setJustification (juce::Justification::topLeft);
    string.append (text, juce::Font (juce::FontOptions (fontSize)), juce::Colours::white);
    juce::TextLayout layout;
    layout.createLayout (string, frame.getWidth());
    layout.draw (graphics, frame);
    return image;
}

juce::Image referenceGlyphParagraphs (const juce::String& text, const juce::Font& font, juce::Rectangle<float> frame)
{
    auto image = blankImage (frame);
    juce::Graphics graphics (image);
    graphics.reduceClipRegion (frame.getSmallestIntegerContainer());
    graphics.setColour (juce::Colours::white);
    juce::GlyphArrangement glyphs;
    glyphs.addJustifiedText (font, text, 0.0f, 0.0f, frame.getWidth(), juce::Justification::left);
    glyphs.justifyGlyphs (0, glyphs.getNumGlyphs(), frame.getX(), frame.getY(), frame.getWidth(), frame.getHeight(), juce::Justification::topLeft);
    glyphs.draw (graphics);
    return image;
}

class SkinTextLayoutTests final : public juce::UnitTest
{
public:
    SkinTextLayoutTests() : UnitTest ("SkinTextLayout", "skin") {}

    void runTest() override
    {
        beginTest ("ligatures are drawn once in skin text, including centred buttons and ellipsis");
        {
            SkinValidationReport report;
            const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
            expect (package.has_value(), report.toText());
            if (package)
            {
                SkinFontValue requested;
                requested.families = { "Avenir Next", "DejaVu Sans", "serif" };
                requested.weight = 500;
                const auto assets = SkinAssetCache::create (*package, "ligature-test", "1.0.0", {}, { requested }, report);
                expect (assets.has_value(), report.toText());
                if (assets)
                {
                    const auto font = (*assets)->makeFont (requested, 36.0f);
                    for (const auto& text : { juce::String ("Configure device..."), juce::String ("fi fl ff ffi ffl office affinity") })
                        for (const auto width : { 520.0f, 150.0f })
                        {
                            const juce::Rectangle<float> frame (8, 6, width, 60);
                            auto program = textProgram (text, "ellipsis", 36, 1, frame);
                            program.baseLayers[0].properties["font"].value = requested;
                            program.baseLayers[0].properties["align"].value = juce::String ("center");
                            auto actual = blankImage (frame);
                            SkinRenderContext context;
                            context.bounds = actual.getBounds().toFloat();
                            context.contentBounds = context.bounds;
                            TextFaultSink sink;
                            SkinRenderer renderer (*assets);
                            {
                                juce::Graphics graphics (actual);
                                expect (renderer.draw (graphics, program, context, true, sink) == SkinRenderStatus::ok);
                            }
                            auto expected = blankImage (frame);
                            {
                                juce::Graphics reference (expected);
                                reference.reduceClipRegion (frame.getSmallestIntegerContainer());
                                reference.setFont (font);
                                reference.setColour (juce::Colours::white);
                                reference.drawText (text, frame, juce::Justification::centred, true);
                            }
                            expect (samePixels (actual, expected), text + " / " + juce::String (width));
                        }
                }
            }
        }

        beginTest ("clip and ellipsis retain the requested font in a short multi-line frame");
        {
            const juce::Rectangle<float> frame (8, 6, 170, 32);
            const juce::String text ("MMMM WWWW MMMM WWWW MMMM WWWW");
            for (const auto* overflow : { "clip", "ellipsis" })
            {
                auto reference = blankImage (frame);
                {
                    juce::Graphics graphics (reference);
                    graphics.reduceClipRegion (frame.getSmallestIntegerContainer());
                    graphics.setFont (juce::Font (juce::FontOptions (28.0f)));
                    graphics.setColour (juce::Colours::white);
                    graphics.drawText (text, frame, juce::Justification::topLeft,
                                       juce::String (overflow) == "ellipsis");
                }
                expect (samePixels (render (text, overflow, 28, 2, frame), reference), overflow);
            }
        }

        beginTest ("soft wrapping respects maxLines without resizing the first line");
        {
            const juce::Font font (juce::FontOptions (20.0f));
            const auto width = std::ceil (juce::GlyphArrangement::getStringWidth (font, "WIDE")) + 2.0f;
            const juce::Rectangle<float> frame (8, 6, width, 80);
            const auto clipped = render ("WIDE WIDE WIDE", "clip", 20, 2, frame);
            const auto ellipsis = render ("WIDE WIDE WIDE", "ellipsis", 20, 2, frame);
            expect (samePixels (clipped, referenceLines ("WIDE\nWIDE", 20, frame)));
            expect (! samePixels (clipped, ellipsis));
            expect (samePixels (clipped.getClippedImage ({ 0, 0, clipped.getWidth(), 26 }),
                                ellipsis.getClippedImage ({ 0, 0, ellipsis.getWidth(), 26 })));
        }

        beginTest ("explicit newlines stop at maxLines and ellipsis marks omitted paragraphs");
        {
            const juce::Rectangle<float> frame (8, 6, 260, 100);
            expect (samePixels (render ("First\nSecond\nThird", "clip", 20, 2, frame),
                                referenceLines ("First\nSecond", 20, frame)));
            expect (samePixels (render ("First\nSecond\nThird", "ellipsis", 20, 2, frame),
                                referenceLines (juce::String::fromUTF8 ("First\nSecond…"), 20, frame)));
            expect (samePixels (render ("First\nSecond", "ellipsis", 20, 1, frame),
                                referenceLines (juce::String::fromUTF8 ("First…"), 20, frame)));
        }

        beginTest ("Unicode shaping preserves CJK combining marks emoji and RTL text when lines are limited");
        {
            const juce::Rectangle<float> frame (8, 6, 360, 120);
            for (const auto& visible : { juce::String::fromUTF8 ("音乐与声音\nCafé é 🐈"),
                                         juce::String::fromUTF8 ("שלום עולם\nمرحبا بالعالم") })
            {
                const auto clipped = render (visible + "\nHidden", "clip", 20, 2, frame);
                expect (samePixels (clipped, referenceGlyphParagraphs (visible + "\n", juce::Font (juce::FontOptions (20.0f)), frame)),
                        "Clipped Unicode paragraphs must match the same complete paragraphs without a line limit.");
                const auto expected = visible + juce::String::charToString (0x2026);
                const auto ellipsis = render (visible + "\nHidden", "ellipsis", 20, 2, frame);
                expect (samePixels (ellipsis, referenceGlyphParagraphs (expected, juce::Font (juce::FontOptions (20.0f)), frame)),
                        "Unicode ellipsis must preserve the unscaled shaping of the expected visible paragraphs.");
            }
        }

        beginTest ("shrink retains fitted-text sizing while respecting explicit newline limits");
        {
            const juce::Rectangle<float> frame (8, 6, 100, 30);
            const juce::String text ("MMMM WWWW MMMM WWWW MMMM WWWW");
            auto reference = blankImage (frame);
            {
                juce::Graphics graphics (reference);
                graphics.reduceClipRegion (frame.getSmallestIntegerContainer());
                graphics.setFont (juce::Font (juce::FontOptions (28.0f)));
                graphics.setColour (juce::Colours::white);
                graphics.drawFittedText (text, frame.toNearestInt(), juce::Justification::topLeft, 2, 0.2f);
            }
            const auto shrunk = render (text, "shrink", 28, 2, frame);
            expect (samePixels (shrunk, reference));
            expect (! samePixels (shrunk, render (text, "clip", 28, 2, frame)));
            const juce::Rectangle<float> multiline (8, 6, 260, 100);
            expect (samePixels (render ("First\nSecond\nThird", "shrink", 20, 2, multiline),
                                render ("First\nSecond", "shrink", 20, 2, multiline)));
        }

        beginTest ("shrink preserves mixed-font Unicode lines before truncating later paragraphs");
        {
            const juce::Rectangle<float> frame (8, 6, 360, 120);
            for (const auto& visible : { juce::String::fromUTF8 ("音乐 Café é 🐈\nשלום world مرحبا"),
                                         juce::String::fromUTF8 ("مرحبا 🐈 中\nÁ שלום 音乐") })
            {
                auto reference = blankImage (frame);
                {
                    juce::Graphics graphics (reference);
                    graphics.reduceClipRegion (frame.getSmallestIntegerContainer());
                    graphics.setFont (juce::Font (juce::FontOptions (20.0f)));
                    graphics.setColour (juce::Colours::white);
                    graphics.drawFittedText (visible, frame.toNearestInt(),
                                             juce::Justification::topLeft, 2, 0.2f);
                }
                expect (samePixels (render (visible, "shrink", 20, 2, frame), reference),
                        "Mixed-font glyphs must not truncate an otherwise complete two-line layout.");
                expect (samePixels (render (visible + "\nHidden 第三行 🐈", "shrink", 20, 2, frame), reference),
                        "Truncating the third paragraph must preserve both preceding mixed-font lines.");
            }
        }
    }

private:
    juce::Image render (const juce::String& text, const juce::String& overflow,
                        float fontSize, int maxLines, juce::Rectangle<float> frame)
    {
        auto image = blankImage (frame);
        juce::Graphics graphics (image);
        SkinRenderContext context;
        context.bounds = image.getBounds().toFloat();
        context.contentBounds = context.bounds;
        TextFaultSink sink;
        SkinRenderer renderer;
        expect (renderer.draw (graphics, textProgram (text, overflow, fontSize, maxLines, frame),
                               context, true, sink) == SkinRenderStatus::ok);
        expect (! sink.failed);
        return image;
    }
};

SkinTextLayoutTests skinTextLayoutTests;
} // namespace
