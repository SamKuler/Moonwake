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
                        std::array<float, maximumOutputChannels> peaks, int width, int height,
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
            const auto fill = *skin->tokens.find<juce::Colour> ("colors.studio.active");
            const auto activePixels = [fill] (const juce::Image& image)
            {
                int count = 0;
                for (int y = 0; y < image.getHeight(); ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (image.getPixelAt (x, y) == fill) ++count;
                return count;
            };
            for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                       OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo,
                                       OutputLayout::sevenOneFour, OutputLayout::sevenOneSix,
                                       OutputLayout::nineOneSix })
                for (const auto size : { juce::Point<int> (329, 368), juce::Point<int> (405, 468) })
                {
                    OutputFaultSink sink;
                    std::array<float, maximumOutputChannels> peaks {};
                    const auto silent = drawOutput (*style, layout, peaks, size.x, size.y, sink, background);
                    expectEquals (activePixels (silent), 0, "silence must leave all meters empty");
                    peaks.fill (0.1f);
                    const auto loud = drawOutput (*style, layout, peaks, size.x, size.y, sink, background);
                    peaks.fill (0.01f);
                    const auto quiet = drawOutput (*style, layout, peaks, size.x, size.y, sink, background);
                    const auto loudArea = activePixels (loud), quietArea = activePixels (quiet);
                    expect (quietArea > 0, toString (layout));
                    expectWithinAbsoluteError (static_cast<double> (loudArea) / juce::jmax (1, quietArea),
                                               2.0, 0.12, "-20 vs -40 dBFS occupy 2:1 areas on a -60 dB scale");
                    int representedChannels = 0;
                    for (int channel = 0; channel < getChannelCount (layout); ++channel)
                    {
                        peaks.fill (0.0f);
                        peaks[static_cast<size_t> (channel)] = 0.1f;
                        if (activePixels (drawOutput (*style, layout, peaks, size.x, size.y, sink, background)) > 0)
                            ++representedChannels;
                    }
                    expectEquals (representedChannels, getChannelCount (layout), "every channel including heights must render");
                    expect (! sink.failed, toString (layout));
                }
        }
    }
};
OutputStageTests outputStageTests;
}
