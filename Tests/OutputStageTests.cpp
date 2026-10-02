#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinRenderCompiler.h"
#include "skin/SkinSchemaValidator.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/OutputStageComponent.h"

#include <cmath>

namespace
{
struct OutputFaultSink final : SkinRuntimeFaultSink
{
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override { failed = true; }
    bool failed = false;
};

juce::Image drawOutput (const SkinRenderStyleEntry& style, OutputLayout layout,
                        std::array<float, 10> peaks, int width, int height,
                        OutputFaultSink& sink, juce::Colour background = juce::Colours::transparentBlack)
{
    SkinRenderer renderer;
    JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget, const juce::String&)
        { return JuceSkinAdapter::RuntimeBinding { &style, nullptr, &sink, true }; }, renderer);
    OutputStageComponent output;
    output.setBounds (0, 0, width, height);
    output.setLookAndFeel (&adapter);
    output.setState (layout, WorkspaceMode::studio, true, "Track", "Built-in Output",
                     getChannelCount (layout), peaks);
    juce::Image image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics graphics (image);
        graphics.fillAll (background);
        output.paint (graphics);
    }
    output.setLookAndFeel (nullptr);
    return image;
}

class OutputStageTests final : public juce::UnitTest
{
public:
    OutputStageTests() : UnitTest ("OutputStage", "skin") {}

    void runTest() override
    {
        beginTest ("actual output component exposes additive dBFS context without changing linear peaks");
        {
            const auto document = juce::JSON::parse (R"json({
                "apiVersion":1,"target":"outputStage","layers":[
                    {"id":"channels","type":"repeat","source":"meter.channels","limit":10,
                     "template":{"id":"peak","type":"rect","frame":{
                       "x":{"$expr":"item.index * 10"},"y":0,"width":5,
                       "height":{"$expr":"item.peakDb + 61"}},"fill":"#FFFFFFFF"}},
                    {"id":"linear","type":"rect","frame":{"x":0,"y":80,"width":10,"height":10},
                     "visible":{"$expr":"meter.channels[4].peak == 0.5 and meter.channels[6].peak == 2 and meter.channels[6].peakDb > 6 and meter.channels[6].peakDb < 6.03 and meter.channels[0].peakDb == -60"},
                     "fill":"#00FF00FF"}
                ]})json");
            SkinValidationReport report;
            const auto validated = SkinSchemaValidator::validate (
                SkinSchemaDocument::render, document, "output-peak.json", report);
            SkinTokenStore tokens;
            const auto program = validated
                ? SkinRenderCompiler::compileProgram (*validated, tokens, report) : std::nullopt;
            expect (program.has_value(), report.toText());
            if (program)
            {
                SkinRenderStyleEntry style;
                style.renderer = *program;
                OutputFaultSink sink;
                const auto image = drawOutput (style, OutputLayout::sevenOneTwo,
                    { 0.0f, 0.0001f, 0.001f, 0.1f, 0.5f, 1.0f, 2.0f, 0.01f, 0.00099f, 0.25f }, 100, 100, sink);
                const std::array<int, 10> lastSolidPixel { 0, 0, 0, 40, 53, 60, 66, 20, 0, 47 };
                const std::array<int, 10> firstEmptyPixel { 1, 1, 2, 42, 55, 61, 68, 22, 1, 49 };
                for (size_t channel = 0; channel < lastSolidPixel.size(); ++channel)
                {
                    const auto x = static_cast<int> (channel) * 10 + 2;
                    expect (image.getPixelAt (x, lastSolidPixel[channel]).getAlpha() > 250,
                            "channel " + juce::String (channel));
                    expectEquals (static_cast<int> (image.getPixelAt (x, firstEmptyPixel[channel]).getAlpha()), 0);
                }
                expect (image.getPixelAt (5, 85) == juce::Colours::lime,
                        "indexed dB values must retain above-full-scale peaks and the existing linear values");
                expect (! sink.failed);
            }
        }

        beginTest ("Default compiles and renders compact dB meters for every actual output layout");
        {
            SkinValidationReport report;
            const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
            const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
            const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
            const auto skin = package && foundation ? SkinCompiler::compile (*package, foundation, report) : nullptr;
            expect (skin != nullptr, report.toText());
            if (! skin) return;
            const auto* style = skin->resolveStyle (SkinRenderTarget::outputStage, "outputStage.studio");
            expect (style != nullptr);
            if (! style) return;
            const auto background = *skin->tokens.find<juce::Colour> ("colors.studio.surface");
            const auto track = *skin->tokens.find<juce::Colour> ("colors.studio.background");
            const auto fill = *skin->tokens.find<juce::Colour> ("colors.studio.active");
            const auto active = track.overlaidWith (fill.withAlpha (0.8f));
            const auto isActive = [&] (juce::Colour colour)
            {
                return std::abs (colour.getRed() - active.getRed()) <= 2
                    && std::abs (colour.getGreen() - active.getGreen()) <= 2
                    && std::abs (colour.getBlue() - active.getBlue()) <= 2;
            };
            for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                       OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
                for (const auto size : { juce::Point<int> (329, 412), juce::Point<int> (405, 512) })
                {
                    OutputFaultSink sink;
                    std::array<float, 10> peaks;
                    peaks.fill (0.1f); // -20 dBFS, two thirds of the -60..0 range.
                    const auto image = drawOutput (*style, layout, peaks, size.x, size.y, sink, background);
                    std::vector<juce::Range<int>> bars;
                    int first = -1;
                    for (int x = 0; x <= size.x; ++x)
                    {
                        const auto activePixel = x < size.x && isActive (image.getPixelAt (x, size.y - 100));
                        if (activePixel && first < 0) first = x;
                        if (! activePixel && first >= 0)
                        {
                            bars.emplace_back (first, x);
                            first = -1;
                        }
                    }
                    expectEquals (static_cast<int> (bars.size()), getChannelCount (layout), toString (layout));
                    for (size_t index = 0; index < bars.size(); ++index)
                    {
                        expect (bars[index].getLength() >= 14 && bars[index].getLength() <= 16);
                        if (index > 0)
                            expect (bars[index].getStart() - bars[index - 1].getStart() <= 33,
                                    "channel bars must remain a compact group");
                        const auto x = bars[index].getStart() + 7;
                        const auto levelY = 84 + (size.y - 172) / 3;
                        expect (! isActive (image.getPixelAt (x, levelY - 4)), "-20 dBFS top boundary");
                        expect (isActive (image.getPixelAt (x, levelY + 4)), "-20 dBFS occupies two thirds of the range");
                    }
                    expect (image.getPixelAt (0, size.y / 2) == background, "the containing layout owns the panel surface");
                    peaks.fill (0.0f);
                    const auto silent = drawOutput (*style, layout, peaks, size.x, size.y, sink, background);
                    peaks.fill (2.0f);
                    const auto overFullScale = drawOutput (*style, layout, peaks, size.x, size.y, sink, background);
                    for (const auto bar : bars)
                    {
                        const auto x = bar.getStart() + 7;
                        expect (silent.getPixelAt (x, size.y - 100) == track, "silence must leave an empty meter");
                        expect (isActive (overFullScale.getPixelAt (x, 88)), "above-full-scale samples must fill the visible range");
                    }
                    expect (! sink.failed, toString (layout));
                }
        }
    }
};
OutputStageTests outputStageTests;
}
