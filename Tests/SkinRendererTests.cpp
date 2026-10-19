#include "skin/SkinAnimationEngine.h"
#include "skin/SkinAssetCache.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinComponentBindings.h"
#include "skin/SkinRenderer.h"
#include "ui/AlbumArtworkComponent.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/OutputStageComponent.h"
#include "ui/SkinEffectChainViewport.h"
#include "ui/SkinTableListBox.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_cryptography/juce_cryptography.h>

#include <cmath>
#include <set>
#include <tuple>

namespace
{
SkinRenderPropertyBinding literalBinding (SkinPrimitive primitive,
                                          const juce::String& path,
                                          SkinCompiledRenderValue value)
{
    const auto* descriptor = skinRenderPropertyDescriptor (primitive, path);
    jassert (descriptor != nullptr);
    return { std::move (value), descriptor->fallback, descriptor, false, {}, {}, 0, {} };
}

void setCommon (SkinRenderLayer& layer)
{
    const auto set = [&] (const juce::String& path, SkinCompiledRenderValue value)
    {
        layer.properties[path] = literalBinding (layer.primitive, path, std::move (value));
    };
    set ("visible", true);
    set ("opacity", 1.0);
    set ("transform.translateX", 0.0);
    set ("transform.translateY", 0.0);
    set ("transform.scaleX", 1.0);
    set ("transform.scaleY", 1.0);
    set ("transform.rotation", 0.0);
    set ("transform.pivotX", 100.0);
    set ("transform.pivotY", 60.0);
    set ("clip", false);
    set ("shadow.color", juce::Colours::transparentBlack);
    set ("shadow.radius", 0.0);
    set ("shadow.offsetX", 0.0);
    set ("shadow.offsetY", 0.0);
}

SkinRenderLayer rectangleLayer (juce::String id, juce::Rectangle<float> frame,
                                juce::Colour fill, double radius = 0.0)
{
    SkinRenderLayer layer;
    layer.id = std::move (id);
    layer.primitive = radius > 0.0 ? SkinPrimitive::roundedRectangle
                                  : SkinPrimitive::rectangle;
    setCommon (layer);
    const auto set = [&] (const juce::String& path, SkinCompiledRenderValue value)
    {
        layer.properties[path] = literalBinding (layer.primitive, path, std::move (value));
    };
    set ("frame.x", static_cast<double> (frame.getX()));
    set ("frame.y", static_cast<double> (frame.getY()));
    set ("frame.width", static_cast<double> (frame.getWidth()));
    set ("frame.height", static_cast<double> (frame.getHeight()));
    set ("fill", fill);
    if (radius > 0.0) set ("radius", radius);
    set ("stroke.color", juce::Colours::transparentBlack);
    set ("stroke.width", 0.0);
    set ("stroke.placement", juce::String ("center"));
    set ("stroke.cap", juce::String ("butt"));
    return layer;
}

void setExplicitFrame (SkinRenderLayer& layer, juce::Rectangle<float> frame)
{
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>> {
             { "frame.x", frame.getX() }, { "frame.y", frame.getY() },
             { "frame.width", frame.getWidth() }, { "frame.height", frame.getHeight() } })
        layer.properties[name] = literalBinding (layer.primitive, name, value);
}

SkinRenderProgram simpleProgram (SkinRenderTarget target, SkinRenderLayer layer)
{
    SkinRenderProgram program;
    program.target = target;
    program.baseLayers.push_back (std::move (layer));
    const auto& names = skinVariantNamesInMergeOrder();
    for (size_t i = 0; i < names.size(); ++i) program.variants[i].name = names[i];
    program.worstCaseOperations = 1;
    return program;
}

void setControlStyle (SkinRenderProgram& program,
                      std::initializer_list<std::pair<const char*, SkinCompiledRenderValue>> values)
{
    program.controlStyle.emplace();
    for (const auto& [name, value] : values)
    {
        const auto& descriptors = skinControlStylePropertyDescriptors (program.target);
        const auto found = std::find_if (descriptors.begin(), descriptors.end(), [&] (const auto& descriptor)
        { return descriptor.path == name; });
        jassert (found != descriptors.end());
        program.controlStyle->properties[name] = { value, found->fallback, &*found, false, {}, {}, 0, {} };
    }
}

juce::Image render (const SkinRenderProgram& program, SkinRenderContext context,
                    SkinRenderer& renderer, SkinRuntimeFaultSink& sink)
{
    juce::Image image (juce::Image::ARGB, 200, 120, true);
    juce::Graphics graphics (image);
    context.bounds = { 0.0f, 0.0f, 200.0f, 120.0f };
    context.contentBounds = context.bounds;
    renderer.draw (graphics, program, context, true, sink);
    return image;
}

juce::String pixelHash (const juce::Image& image)
{
    juce::MemoryBlock pixels;
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < data.height; ++y)
        // Native backends may pad each row; padding is not part of the image.
        pixels.append (data.getLinePointer (y), static_cast<size_t> (data.width * data.pixelStride));
    return juce::SHA256 (pixels).toHexString();
}

class FaultRecorder final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic& diagnostic,
                            SkinRenderStatus newStatus) override
    {
        diagnostics.push_back (diagnostic);
        status = newStatus;
    }

    std::vector<SkinDiagnostic> diagnostics;
    SkinRenderStatus status = SkinRenderStatus::ok;
};

std::vector<SkinPackage::EmbeddedFile> minimalPngPackage()
{
    juce::Image image (juce::Image::ARGB, 2, 2, true);
    image.setPixelAt (0, 0, juce::Colours::red);
    image.setPixelAt (1, 0, juce::Colours::green);
    image.setPixelAt (0, 1, juce::Colours::blue);
    image.setPixelAt (1, 1, juce::Colours::white);
    juce::MemoryOutputStream stream;
    juce::PNGImageFormat().writeImageToStream (image, stream);
    return { { "assets/pixels.png", stream.getMemoryBlock() } };
}

std::vector<SkinPackage::EmbeddedFile> solidPngPackage (juce::Colour colour)
{
    juce::Image image (juce::Image::ARGB, 2, 2, true);
    image.clear (image.getBounds(), colour);
    juce::MemoryOutputStream stream;
    juce::PNGImageFormat().writeImageToStream (image, stream);
    return { { "assets/pixels.png", stream.getMemoryBlock() } };
}

std::optional<SkinPackage> packageFrom (
    juce::String id, std::vector<SkinPackage::EmbeddedFile> files,
    SkinValidationReport& report)
{
    return SkinPackage::fromEmbedded (std::move (id), std::move (files), report);
}

class SkinRendererTests final : public juce::UnitTest
{
public:
    SkinRendererTests() : UnitTest ("SkinRenderer", "skin") {}

    void runTest() override
    {
        beginTest ("renders deterministic primitive fixtures with clipping, opacity and stroke placement");
        {
            FaultRecorder sink;
            SkinRenderer renderer;
            SkinRenderContext context;
            auto rect = rectangleLayer ("body", { 10.0f, 10.0f, 80.0f, 50.0f },
                                        juce::Colour (0xff336699), 9.0);
            rect.properties["opacity"] = literalBinding (
                rect.primitive, "opacity", 0.75);
            rect.properties["stroke.color"] = literalBinding (
                rect.primitive, "stroke.color", juce::Colour (0xffffaa00));
            rect.properties["stroke.width"] = literalBinding (
                rect.primitive, "stroke.width", 4.0);
            rect.properties["stroke.placement"] = literalBinding (
                rect.primitive, "stroke.placement", juce::String ("inside"));
            const auto image = render (simpleProgram (SkinRenderTarget::surface, rect),
                                       context, renderer, sink);
            // Native rasterization and default fonts have platform-specific pixels.
           #if JUCE_WINDOWS
            expectEquals (pixelHash (image),
                          juce::String ("92bc7e37363e52e9400630fe2be1e93cf4b76b814c44d633234bc780bb79d0af"));
           #else
            expectEquals (pixelHash (image),
                          juce::String ("1d08f82368912257dfe27d4fe2844d2b559f4497478da98513d42e0f28778c7e"));
           #endif
            expect (sink.status == SkinRenderStatus::ok);
        }

        beginTest ("static renderers skip hidden geometry and repeat children with an animation engine attached");
        {
            SkinTokenStore tokens;
            SkinValidationReport report;
            const auto geometry = SkinExpression::compile ("meter.integrated",
                skinRenderContextForTarget (SkinRenderTarget::outputStage), tokens, report, "static", "/width");
            expect (geometry.has_value(), report.toText());
            auto hidden = rectangleLayer ("hidden", { 0, 0, 20, 20 }, juce::Colours::red);
            hidden.properties["visible"] = literalBinding (hidden.primitive, "visible", false);
            if (geometry) hidden.properties["frame.width"].value = *geometry;
            auto program = simpleProgram (SkinRenderTarget::outputStage, hidden);
            SkinRenderLayer repeated;
            repeated.id = "channels";
            repeated.primitive = SkinPrimitive::repeat;
            repeated.repeatSource = "meter.channels";
            repeated.repeatLimit = maximumOutputChannels;
            setCommon (repeated);
            hidden.id = "hidden-channel";
            repeated.children.push_back (hidden);
            program.baseLayers.push_back (repeated);
            program.baseLayers.push_back (rectangleLayer ("visible", { 30, 10, 20, 20 }, juce::Colours::green));
            SkinRenderContext context;
            context.bounds = { 0, 0, 80, 40 };
            context.contentBounds = context.bounds;
            context.meterLabels = { "L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf", "Ltr", "Rtr" };
            int hiddenGeometryReads = 0;
            context.extraLookup = [&] (std::string_view) -> std::optional<SkinExpressionValue>
            {
                ++hiddenGeometryReads;
                return 20.0;
            };
            SkinAnimationEngine animations;
            FaultRecorder sink;
            SkinRenderer renderer;
            juce::Image image (juce::Image::ARGB, 80, 40, true);

            {
                juce::Graphics graphics (image);
                expect (renderer.draw (graphics, program, context, true, sink, &animations, "static") == SkinRenderStatus::ok);
            }
            expectEquals (hiddenGeometryReads, 0);
            expect (image.getPixelAt (5, 5).isTransparent());
            expect (image.getPixelAt (35, 15) == juce::Colours::green);
            expect (sink.diagnostics.empty());
            expect (! animations.isTimerNeeded());
        }

        beginTest ("merges selected hover pressed focused and disabled variants in frozen order");
        {
            auto program = simpleProgram (
                SkinRenderTarget::textButton,
                rectangleLayer ("body", { 0.0f, 0.0f, 200.0f, 120.0f },
                                juce::Colours::black));
            const std::array colours {
                juce::Colours::red, juce::Colours::green, juce::Colours::blue,
                juce::Colours::yellow, juce::Colours::magenta, juce::Colours::grey
            };
            for (size_t i = 0; i < colours.size(); ++i)
                program.variants[i].properties["body.fill"] = literalBinding (
                    SkinPrimitive::rectangle, "fill", colours[i]);

            SkinRenderContext context;
            context.state = { false, true, true, true, true, true };
            FaultRecorder sink;
            SkinRenderer renderer;
            const auto image = render (program, context, renderer, sink);
            expect (image.getPixelAt (100, 60) == juce::Colours::grey);
        }

        beginTest ("renders ellipse line arc path text group and repeat primitives deterministically");
        {
            SkinRenderProgram program;
            program.target = SkinRenderTarget::outputStage;

            auto ellipse = rectangleLayer (
                "ellipse", { 8.0f, 8.0f, 36.0f, 24.0f }, juce::Colour (0xff4c78a8));
            ellipse.primitive = SkinPrimitive::ellipse;
            ellipse.properties.clear();
            setCommon (ellipse);
            setExplicitFrame (ellipse, { 8.0f, 8.0f, 36.0f, 24.0f });
            ellipse.properties["fill"] = literalBinding (
                ellipse.primitive, "fill", juce::Colour (0xff4c78a8));
            ellipse.properties["stroke.color"] = literalBinding (
                ellipse.primitive, "stroke.color", juce::Colours::white);
            ellipse.properties["stroke.width"] = literalBinding (
                ellipse.primitive, "stroke.width", 2.0);
            ellipse.properties["stroke.placement"] = literalBinding (
                ellipse.primitive, "stroke.placement", juce::String ("outside"));
            ellipse.properties["stroke.cap"] = literalBinding (
                ellipse.primitive, "stroke.cap", juce::String ("round"));
            program.baseLayers.push_back (std::move (ellipse));

            SkinRenderLayer line;
            line.id = "line";
            line.primitive = SkinPrimitive::line;
            setCommon (line);
            for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>> {
                     { "start.x", 54.0 }, { "start.y", 12.0 },
                     { "end.x", 94.0 }, { "end.y", 30.0 }, { "width", 5.0 } })
                line.properties[name] = literalBinding (line.primitive, name, value);
            line.properties["color"] = literalBinding (
                line.primitive, "color", juce::Colour (0xfff58518));
            line.properties["cap"] = literalBinding (
                line.primitive, "cap", juce::String ("round"));
            program.baseLayers.push_back (std::move (line));

            SkinRenderLayer arc;
            arc.id = "arc";
            arc.primitive = SkinPrimitive::arc;
            setCommon (arc);
            for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>> {
                     { "center.x", 125.0 }, { "center.y", 22.0 },
                     { "radiusX", 22.0 }, { "radiusY", 14.0 },
                     { "startAngle", 15.0 }, { "endAngle", 300.0 },
                     { "stroke.width", 3.0 } })
                arc.properties[name] = literalBinding (arc.primitive, name, value);
            arc.properties["stroke.color"] = literalBinding (
                arc.primitive, "stroke.color", juce::Colour (0xff54a24b));
            arc.properties["stroke.placement"] = literalBinding (
                arc.primitive, "stroke.placement", juce::String ("center"));
            arc.properties["stroke.cap"] = literalBinding (
                arc.primitive, "stroke.cap", juce::String ("round"));
            program.baseLayers.push_back (std::move (arc));

            SkinRenderLayer path;
            path.id = "path";
            path.primitive = SkinPrimitive::path;
            setCommon (path);
            auto compiledPath = std::make_shared<juce::Path>();
            compiledPath->startNewSubPath (155.0f, 8.0f);
            compiledPath->lineTo (190.0f, 8.0f);
            compiledPath->lineTo (174.0f, 34.0f);
            compiledPath->closeSubPath();
            auto dataBinding = literalBinding (
                path.primitive, "data", juce::String ("M155 8L190 8L174 34Z"));
            dataBinding.compiledPath = compiledPath;
            path.properties["data"] = dataBinding;
            path.compiledPath = compiledPath;
            path.properties["fill"] = literalBinding (
                path.primitive, "fill", juce::Colour (0xffe45756));
            path.properties["stroke.color"] = literalBinding (
                path.primitive, "stroke.color", juce::Colours::transparentBlack);
            path.properties["stroke.width"] = literalBinding (
                path.primitive, "stroke.width", 0.0);
            path.properties["stroke.placement"] = literalBinding (
                path.primitive, "stroke.placement", juce::String ("center"));
            path.properties["stroke.cap"] = literalBinding (
                path.primitive, "stroke.cap", juce::String ("butt"));
            program.baseLayers.push_back (std::move (path));

            SkinRenderLayer text;
            text.id = "text";
            text.primitive = SkinPrimitive::text;
            setCommon (text);
            setExplicitFrame (text, { 8.0f, 42.0f, 90.0f, 24.0f });
            SkinFontValue font;
            font.families.add (juce::Font::getDefaultSansSerifFontName());
            font.weight = 600;
            text.properties["text"] = literalBinding (
                text.primitive, "text", juce::String ("SKIN API 1"));
            text.properties["font"] = literalBinding (text.primitive, "font", font);
            text.properties["fontSize"] = literalBinding (text.primitive, "fontSize", 13.0);
            text.properties["color"] = literalBinding (
                text.primitive, "color", juce::Colour (0xffeeeeee));
            text.properties["align"] = literalBinding (
                text.primitive, "align", juce::String ("center"));
            text.properties["maxLines"] = literalBinding (text.primitive, "maxLines", 1.0);
            text.properties["overflow"] = literalBinding (
                text.primitive, "overflow", juce::String ("clip"));
            program.baseLayers.push_back (std::move (text));

            SkinRenderLayer group;
            group.id = "group";
            group.primitive = SkinPrimitive::group;
            setCommon (group);
            group.properties["opacity"] = literalBinding (group.primitive, "opacity", 0.65);
            group.properties["clip"] = literalBinding (group.primitive, "clip", true);
            group.properties["transform.translateX"] = literalBinding (
                group.primitive, "transform.translateX", 4.0);
            group.children.push_back (rectangleLayer (
                "group-child", { 180.0f, 44.0f, 40.0f, 22.0f },
                juce::Colour (0xffb279a2), 6.0));
            program.baseLayers.push_back (std::move (group));

            SkinRenderLayer repeat;
            repeat.id = "repeat";
            repeat.primitive = SkinPrimitive::repeat;
            repeat.repeatSource = "meter.channels";
            repeat.repeatLimit = 3;
            setCommon (repeat);
            auto repeated = rectangleLayer (
                "bar", { 0.0f, 0.0f, 12.0f, 10.0f }, juce::Colour (0xff72b7b2), 2.0);
            SkinValidationReport expressionReport;
            SkinTokenStore emptyTokens;
            const auto itemOffset = SkinExpression::compile (
                "item.index * 20 + 18",
                skinRenderContextForTarget (SkinRenderTarget::outputStage, "meter.channels"),
                emptyTokens, expressionReport, "fixture", "/repeat/transform/translateX");
            expect (itemOffset.has_value());
            if (itemOffset)
            {
                auto offsetBinding = literalBinding (
                    repeated.primitive, "transform.translateX", 0.0);
                offsetBinding.value = *itemOffset;
                repeated.properties["transform.translateX"] = std::move (offsetBinding);
            }
            repeated.properties["transform.translateY"] = literalBinding (
                repeated.primitive, "transform.translateY", 78.0);
            repeat.children.push_back (std::move (repeated));
            program.baseLayers.push_back (std::move (repeat));

            const auto& names = skinVariantNamesInMergeOrder();
            for (size_t i = 0; i < names.size(); ++i) program.variants[i].name = names[i];
            program.worstCaseOperations = 10;

            SkinRenderContext context;
            context.meterLabels.addArray ({ "L", "R", "C" });
            context.meterPeaks = { 0.2f, 0.5f, 0.8f };
            FaultRecorder sink;
            SkinRenderer renderer;
            const auto image = render (program, context, renderer, sink);
           #if JUCE_WINDOWS
            // DirectWrite's default font may vary with Windows/font versions.
            // Keep an exact geometry baseline, and check native text independently.
            const juce::Rectangle<int> textFrame (8, 42, 90, 24);
            int textPixels = 0;
            for (int y = textFrame.getY(); y < textFrame.getBottom(); ++y)
                for (int x = textFrame.getX(); x < textFrame.getRight(); ++x)
                    if (! image.getPixelAt (x, y).isTransparent()) ++textPixels;
            expect (textPixels > 0 && textPixels < textFrame.getWidth() * textFrame.getHeight(),
                    "Native text must draw glyphs inside its frame");
            expectEquals (pixelHash (image), pixelHash (render (program, context, renderer, sink)),
                          "Rendering the same native font twice must be deterministic");
            auto geometry = image.createCopy();
            geometry.clear (textFrame);
            expectEquals (pixelHash (geometry),
                          juce::String ("5fb37950eca6d4d10ce9945fde1807a6fb29df677147e1f0031921958f4d7e64"));
           #else
            expectEquals (pixelHash (image),
                          juce::String ("62a36eff76f990565ede4b3f3d8ebad3464d33f7571388f96e07fb9f1387254a"));
           #endif
        }

        beginTest ("native table repeat bindings include the fourth column and skip hidden columns");
        {
            SkinRenderLayer repeat;
            repeat.id = "columns";
            repeat.primitive = SkinPrimitive::repeat;
            repeat.repeatSource = "table.visibleColumns";
            repeat.repeatLimit = 4;
            setCommon (repeat);
            auto column = rectangleLayer ("album", { 0, 0, 10, 20 }, juce::Colours::green);
            SkinValidationReport report;
            SkinTokenStore tokens;
            const auto contexts = skinRenderContextForTarget (SkinRenderTarget::table, "table.visibleColumns");
            const auto x = SkinExpression::compile ("item.index * 20", contexts, tokens, report, "fixture", "/x");
            const auto visible = SkinExpression::compile ("item.label == 'Album'", contexts, tokens, report, "fixture", "/visible");
            expect (x.has_value() && visible.has_value(), report.toText());
            if (x && visible)
            {
                column.properties["frame.x"].value = *x;
                column.properties["visible"].value = *visible;
                repeat.children.push_back (std::move (column));
                SkinRenderStyleEntry style;
                style.renderer = simpleProgram (SkinRenderTarget::table, std::move (repeat));
                SkinRenderer renderer;
                JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget, const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding { &style };
                }, renderer);
                juce::TableListBox table;
                table.setBounds (0, 0, 200, 40);
                int id = 0;
                for (const auto* label : { "Track", "Artist", "Time", "Album" })
                    table.getHeader().addColumn (label, ++id, 50);
                juce::Image image (juce::Image::ARGB, 200, 40, true);

                {
                    juce::Graphics graphics (image);
                    adapter.drawListBoxBackground (graphics, table, 0, 4);
                }
                expect (image.getPixelAt (65, 10) == juce::Colours::green);
                table.getHeader().setColumnVisible (4, false);
                image.clear (image.getBounds(), juce::Colours::transparentBlack);
                {
                    juce::Graphics graphics (image);
                    adapter.drawListBoxBackground (graphics, table, 0, 3);
                }
                expect (image.getPixelAt (65, 10).isTransparent());
            }
        }

        beginTest ("renders cached bitmap and SVG primitives with fit and tint deterministically");
        {
            auto files = minimalPngPackage();
            const juce::String svg =
                "<svg width=\"2\" height=\"2\" viewBox=\"0 0 2 2\">"
                "<rect x=\"0\" y=\"0\" width=\"2\" height=\"2\" fill=\"#ffffff\"/>"
                "</svg>";
            files.push_back ({ "assets/mark.svg",
                               juce::MemoryBlock (svg.toRawUTF8(),
                                                  static_cast<size_t> (svg.getNumBytesAsUTF8())) });
            SkinValidationReport packageReport;
            auto package = packageFrom ("render-assets", std::move (files), packageReport);
            SkinValidationReport cacheReport;
            auto cache = SkinAssetCache::create (
                *package, "skin.assets", "1.0.0",
                { "assets/pixels.png", "assets/mark.svg" }, {}, cacheReport);
            expect (cache.has_value());
            if (! cache) return;

            SkinRenderProgram program;
            program.target = SkinRenderTarget::surface;
            const auto addAsset = [&] (SkinPrimitive primitive, juce::String id,
                                       juce::String source, juce::Rectangle<float> frame,
                                       juce::String fit, juce::Colour tint)
            {
                SkinRenderLayer layer;
                layer.id = std::move (id);
                layer.primitive = primitive;
                setCommon (layer);
                setExplicitFrame (layer, frame);
                layer.properties["source"] = literalBinding (
                    primitive, "source", std::move (source));
                layer.properties["fit"] = literalBinding (
                    primitive, "fit", std::move (fit));
                layer.properties["tint"] = literalBinding (primitive, "tint", tint);
                program.baseLayers.push_back (std::move (layer));
            };
            addAsset (SkinPrimitive::image, "bitmap", "assets/pixels.png",
                      { 10.0f, 10.0f, 80.0f, 100.0f }, "cover",
                      juce::Colour (0xbfffffff));
            addAsset (SkinPrimitive::svg, "svg", "assets/mark.svg",
                      { 110.0f, 20.0f, 70.0f, 70.0f }, "contain",
                      juce::Colour (0xffeeca52));
            const auto& names = skinVariantNamesInMergeOrder();
            for (size_t i = 0; i < names.size(); ++i) program.variants[i].name = names[i];
            program.worstCaseOperations = 2;

            FaultRecorder sink;
            SkinRenderer renderer (*cache);
            const auto image = render (program, {}, renderer, sink);
            // Native rasterization and default fonts have platform-specific pixels.
           #if JUCE_WINDOWS
            expectEquals (pixelHash (image),
                          juce::String ("29b647ee6284774c6c7f4a63de3ce544f4391cf00c3de5314555719953771c9e"));
           #else
            expectEquals (pixelHash (image),
                          juce::String ("7d80ee9fd35b5af9d87375f331432c268ff6d4d904feb916b5ebf9bbb303603b"));
           #endif
            expect (sink.status == SkinRenderStatus::ok);
        }

        beginTest ("focus layer remains visible when its visible expression is enabled");
        {
            auto program = simpleProgram (
                SkinRenderTarget::textButton,
                rectangleLayer ("body", { 0.0f, 0.0f, 200.0f, 120.0f },
                                juce::Colours::darkgrey));
            auto focus = rectangleLayer ("focus", { 8.0f, 8.0f, 184.0f, 104.0f },
                                         juce::Colours::transparentBlack, 10.0);
            focus.properties["stroke.color"] = literalBinding (
                focus.primitive, "stroke.color", juce::Colours::yellow);
            focus.properties["stroke.width"] = literalBinding (
                focus.primitive, "stroke.width", 3.0);
            program.baseLayers.push_back (std::move (focus));
            program.variants[4].properties["focus.fill"] = literalBinding (
                SkinPrimitive::roundedRectangle, "fill", juce::Colour (0x30ffff00));
            SkinRenderContext context;
            context.state.focused = true;
            FaultRecorder sink;
            SkinRenderer renderer;
            const auto image = render (program, context, renderer, sink);
            expect (image.getPixelAt (9, 60).getAlpha() > 0);
        }

        beginTest ("asset cache validates bitmaps and isolates cache namespaces");
        {
            SkinValidationReport packageReport;
            auto package = packageFrom ("fixture", minimalPngPackage(), packageReport);
            expect (package.has_value());
            SkinValidationReport firstReport;
            SkinValidationReport secondReport;
            const auto first = SkinAssetCache::create (
                *package, "skin.alpha", "1.0.0", { "assets/pixels.png" }, {}, firstReport);
            const auto second = SkinAssetCache::create (
                *package, "skin.alpha", "2.0.0", { "assets/pixels.png" }, {}, secondReport);
            expect (first.has_value() && second.has_value());
            if (first && second)
            {
                expect ((*first)->findImage ("assets/pixels.png") != nullptr);
                expect ((*first)->namespaceKey() != (*second)->namespaceKey());
                expectEquals ((*first)->statistics().decodedPixels, int64_t (4));
            }
        }

        beginTest ("asset cache rejects missing resources, DTD, unsafe SVG and external references");
        {
            SkinValidationReport missingPackageReport;
            auto missingPackage = packageFrom (
                "missing-fixture", minimalPngPackage(), missingPackageReport);
            SkinValidationReport missingReport;
            expect (! SkinAssetCache::create (
                *missingPackage, "skin.missing", "1.0.0", { "assets/absent.png" }, {},
                missingReport).has_value());
            expect (missingReport.hasCode ("ASSET001"));

            SkinValidationReport packageReport;
            constexpr char unsafeSvg[] =
                "<!DOCTYPE svg [<!ENTITY x 'boom'>]>"
                "<svg><script>&x;</script></svg>";
            auto package = packageFrom (
                "svg-fixture",
                { { "assets/bad.svg",
                    juce::MemoryBlock (unsafeSvg, sizeof (unsafeSvg) - 1) } },
                packageReport);
            expect (package.has_value());
            SkinValidationReport report;
            const auto cache = SkinAssetCache::create (
                *package, "skin.bad", "1.0.0", { "assets/bad.svg" }, {}, report);
            expect (! cache.has_value());
            expect (report.hasCode ("ASSET006"));

            SkinValidationReport externalPackageReport;
            const juce::String externalSvg =
                "<svg><path id=\"p\" d=\"M0 0L1 1\" fill=\"url(https://bad/x)\"/></svg>";
            auto externalPackage = packageFrom (
                "external-fixture",
                { { "assets/external.svg",
                    juce::MemoryBlock (externalSvg.toRawUTF8(),
                                       static_cast<size_t> (externalSvg.getNumBytesAsUTF8())) } },
                externalPackageReport);
            SkinValidationReport externalReport;
            const auto external = SkinAssetCache::create (
                *externalPackage, "skin.bad", "1.0.1", { "assets/external.svg" }, {},
                externalReport);
            expect (! external.has_value());
            expect (externalReport.hasCode ("ASSET009"));
        }

        beginTest ("asset cache enforces SVG element and attribute allowlists");
        {
            const auto cacheForSvg = [&] (const juce::String& id, const juce::String& svg,
                                          SkinValidationReport& report)
            {
                SkinValidationReport packageReport;
                auto package = packageFrom (
                    id, { { "assets/test.svg",
                            juce::MemoryBlock (svg.toRawUTF8(), static_cast<size_t> (
                                svg.getNumBytesAsUTF8())) } }, packageReport);
                return SkinAssetCache::create (
                    *package, id, "1.0.0", { "assets/test.svg" }, {}, report);
            };

            SkinValidationReport elementReport;
            expect (! cacheForSvg ("bad-element",
                "<svg><foreignObject/></svg>", elementReport).has_value());
            expect (elementReport.hasCode ("ASSET007"));
            for (const auto* forbidden : { "script", "style", "image" })
            {
                SkinValidationReport forbiddenReport;
                expect (! cacheForSvg (
                    "bad-" + juce::String (forbidden),
                    "<svg><" + juce::String (forbidden) + "/></svg>",
                    forbiddenReport).has_value());
                expect (forbiddenReport.hasCode ("ASSET007"));
            }

            SkinValidationReport attributeReport;
            expect (! cacheForSvg ("bad-attribute",
                "<svg><rect x=\"0\" y=\"0\" width=\"1\" height=\"1\" onclick=\"x\"/></svg>",
                attributeReport).has_value());
            expect (attributeReport.hasCode ("ASSET008"));

            SkinValidationReport geometryReport;
            expect (! cacheForSvg ("bad-geometry",
                "<svg><rect d=\"M0 0\"/></svg>", geometryReport).has_value());
            expect (geometryReport.hasCode ("ASSET008"));

            SkinValidationReport pathGrammarReport;
            expect (! cacheForSvg ("bad-path-grammar",
                "<svg><path d=\"M0,\"/></svg>", pathGrammarReport).has_value());
            expect (pathGrammarReport.hasCode ("ASSET006"));

            beginTest ("asset cache enforces local SVG fragment resolution");
            SkinValidationReport fragmentReport;
            expect (! cacheForSvg ("bad-fragment",
                "<svg><rect x=\"0\" y=\"0\" width=\"1\" height=\"1\" clip-path=\"url(#missing)\"/></svg>",
                fragmentReport).has_value());
            expect (fragmentReport.hasCode ("ASSET009"));

            beginTest ("asset cache rejects more than 512 asset files before decoding");
            std::vector<SkinPackage::EmbeddedFile> tooMany;
            std::set<juce::String> tooManyReferences;
            for (int i = 0; i < 513; ++i)
            {
                const auto path = "resources/" + juce::String (i) + ".png";
                tooMany.push_back ({ path, juce::MemoryBlock ("x", 1) });
                tooManyReferences.insert (path);
            }
            SkinValidationReport manyPackageReport;
            auto manyPackage = packageFrom (
                "too-many-assets", std::move (tooMany), manyPackageReport);
            expect (manyPackage.has_value());
            SkinValidationReport manyReport;
            const auto manyCache = manyPackage
                ? SkinAssetCache::create (*manyPackage, "too-many-assets", "1.0.0",
                                          tooManyReferences, {}, manyReport)
                : std::nullopt;
            expect (! manyCache.has_value());
            expect (manyReport.hasCode ("ASSET004"));

            beginTest ("asset cache rejects oversized bitmap headers before decoding");
            std::array<uint8_t, 24> oversizedPng {};
            const std::array<uint8_t, 8> signature { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
            std::copy (signature.begin(), signature.end(), oversizedPng.begin());
            oversizedPng[16] = 0x00;
            oversizedPng[17] = 0x00;
            oversizedPng[18] = 0x10;
            oversizedPng[19] = 0x01;
            oversizedPng[23] = 0x01;
            SkinValidationReport imagePackageReport;
            auto imagePackage = packageFrom (
                "oversized-image",
                { { "assets/huge.png",
                    juce::MemoryBlock (oversizedPng.data(), oversizedPng.size()) } },
                imagePackageReport);
            SkinValidationReport imageReport;
            expect (! SkinAssetCache::create (
                *imagePackage, "oversized-image", "1.0.0", { "assets/huge.png" }, {},
                imageReport).has_value());
            expect (imageReport.hasCode ("ASSET003"));

            beginTest ("asset cache enforces 32-million-pixel and 128-MiB decoded budgets");
            juce::Image largeImage (juce::Image::ARGB, 2048, 2048, true);
            largeImage.clear (largeImage.getBounds(), juce::Colour (0xff17324d));
            juce::MemoryOutputStream largePngStream;
            expect (juce::PNGImageFormat().writeImageToStream (largeImage, largePngStream));
            std::vector<SkinPackage::EmbeddedFile> largeFiles;
            for (int i = 0; i < 8; ++i)
                largeFiles.push_back ({ "assets/large-" + juce::String (i) + ".png",
                                        largePngStream.getMemoryBlock() });
            SkinValidationReport largePackageReport;
            auto largePackage = packageFrom (
                "decoded-budget", std::move (largeFiles), largePackageReport);
            SkinValidationReport largeReport;
            const auto largeCache = largePackage
                ? SkinAssetCache::create (*largePackage, "decoded-budget", "1.0.0",
                                          {}, {}, largeReport)
                : std::nullopt;
            expect (! largeCache.has_value());
            expect (largeReport.hasCode ("ASSET004"));

            beginTest ("asset cache enforces the 100000-element package SVG DOM budget");
            juce::String manyGroups;
            manyGroups.preallocateBytes (40000);
            for (int i = 0; i < 9090; ++i) manyGroups += "<g/>";
            const auto denseSvg = "<svg>" + manyGroups + "</svg>";
            std::vector<SkinPackage::EmbeddedFile> denseSvgFiles;
            std::set<juce::String> denseSvgReferences;
            for (int i = 0; i < 11; ++i)
            {
                const auto path = "assets/dense-" + juce::String (i) + ".svg";
                denseSvgFiles.push_back ({ path,
                    juce::MemoryBlock (denseSvg.toRawUTF8(), static_cast<size_t> (
                        denseSvg.getNumBytesAsUTF8())) });
                denseSvgReferences.insert (path);
            }
            SkinValidationReport densePackageReport;
            auto densePackage = packageFrom (
                "svg-dom-budget", std::move (denseSvgFiles), densePackageReport);
            SkinValidationReport denseReport;
            const auto denseCache = densePackage
                ? SkinAssetCache::create (*densePackage, "svg-dom-budget", "1.0.0",
                                          denseSvgReferences, {}, denseReport)
                : std::nullopt;
            expect (! denseCache.has_value());
            expect (denseReport.hasCode ("ASSET010"));

            beginTest ("SVG element bombs are rejected by streaming preflight");
            juce::String elementBomb;
            elementBomb.preallocateBytes (50000);
            for (int i = 0; i < 10000; ++i) elementBomb += "<g/>";
            SkinValidationReport elementBombReport;
            expect (! cacheForSvg (
                "element-bomb", "<svg>" + elementBomb + "</svg>",
                elementBombReport).has_value());
            expect (elementBombReport.hasCode ("ASSET010"));

            beginTest ("asset cache rejects SVG package path-command budget overflow");
            juce::String pathData;
            pathData.preallocateBytes (1300000);
            for (int i = 0; i < 250001; ++i) pathData += "M0 0";
            SkinValidationReport pathReport;
            expect (! cacheForSvg (
                "path-budget", "<svg><path d=\"" + pathData + "\"/></svg>",
                pathReport).has_value());
            expect (pathReport.hasCode ("ASSET010"));

            beginTest ("polyline points consume the shared SVG geometry budget");
            juce::String pointData;
            pointData.preallocateBytes (1100000);
            for (int i = 0; i < 250001; ++i) pointData += "0,0 ";
            SkinValidationReport pointReport;
            expect (! cacheForSvg (
                "points-budget", "<svg><polyline points=\"" + pointData
                    + "\"/></svg>", pointReport).has_value());
            expect (pointReport.hasCode ("ASSET010"));

            SkinValidationReport validPointsReport;
            const auto validPoints = cacheForSvg (
                "points-valid",
                "<svg width=\"3\" height=\"3\"><polyline points=\"0,0 1,1 2,2\"/></svg>",
                validPointsReport);
            expect (validPoints.has_value(), validPointsReport.toText());
            if (validPoints)
                expectEquals ((*validPoints)->statistics().svgPathCommands, 3);

            beginTest ("polygon closure consumes one SVG geometry command");
            juce::String polygonBoundary;
            polygonBoundary.preallocateBytes (1000000);
            for (int i = 0; i < 250000; ++i) polygonBoundary += "0,0 ";
            SkinValidationReport polygonBoundaryReport;
            expect (! cacheForSvg (
                "polygon-boundary", "<svg><polygon points=\"" + polygonBoundary
                    + "\"/></svg>", polygonBoundaryReport).has_value());
            expect (polygonBoundaryReport.hasCode ("ASSET010"));

            SkinValidationReport validPolygonReport;
            const auto validPolygon = cacheForSvg (
                "polygon-valid",
                "<svg width=\"3\" height=\"3\"><polygon points=\"0,0 1,1 2,2\"/></svg>",
                validPolygonReport);
            expect (validPolygon.has_value(), validPolygonReport.toText());
            if (validPolygon)
                expectEquals ((*validPolygon)->statistics().svgPathCommands, 4);

            beginTest ("oversized SVG transform attributes are rejected before parsing");
            const auto oversizedTransform = juce::String::repeatedString (
                "translate(0 0) ", 300);
            SkinValidationReport transformReport;
            expect (! cacheForSvg (
                "transform-budget", "<svg><g transform=\"" + oversizedTransform
                    + "\"/></svg>", transformReport).has_value());
            expect (transformReport.hasCode ("ASSET010"));

            SkinValidationReport gradientTransformReport;
            expect (! cacheForSvg (
                "gradient-transform-budget",
                "<svg><defs><linearGradient id=\"g\" gradientTransform=\""
                    + oversizedTransform + "\"/></defs></svg>",
                gradientTransformReport).has_value());
            expect (gradientTransformReport.hasCode ("ASSET010"));

            beginTest ("wrong-element geometry names cannot bypass SVG attribute limits");
            const auto oversizedGeometryName = juce::String::repeatedString ("0", 4097);
            SkinValidationReport wrongPointsReport;
            expect (! cacheForSvg (
                "wrong-points-budget", "<svg><rect points=\""
                    + oversizedGeometryName + "\"/></svg>",
                wrongPointsReport).has_value());
            expect (wrongPointsReport.hasCode ("ASSET010"));

            SkinValidationReport wrongPathReport;
            expect (! cacheForSvg (
                "wrong-path-budget", "<svg><g d=\"" + oversizedGeometryName
                    + "\"/></svg>", wrongPathReport).has_value());
            expect (wrongPathReport.hasCode ("ASSET010"));

            SkinSvgPathCompileError budgetError = SkinSvgPathCompileError::none;
            expect (! skinCompileSvgPathData (
                "M0 0L1 1L2 2", 2, &budgetError).has_value());
            expect (budgetError == SkinSvgPathCompileError::commandBudgetExceeded);
            SkinSvgPathCompileError lexemeError = SkinSvgPathCompileError::none;
            expect (! skinCompileSvgPathData (
                "M" + juce::String::repeatedString ("1", 65) + " 0",
                10, &lexemeError).has_value());
            expect (lexemeError == SkinSvgPathCompileError::invalidGrammar);
        }

        beginTest ("invalid package font uses declared system-family fallback without cache leakage");
        {
            SkinValidationReport packageReport;
            auto package = packageFrom (
                "font-fallback",
                { { "assets/broken.ttf", juce::MemoryBlock ("not-a-font", 10) } },
                packageReport);
            SkinFontValue font;
            font.source = "assets/broken.ttf";
            font.families.add (juce::Font::getDefaultSansSerifFontName());
            SkinValidationReport report;
            const auto cache = SkinAssetCache::create (
                *package, "font-fallback", "1.0.0", {}, { font }, report);
            expect (cache.has_value());
            if (cache)
                expect (((*cache)->makeFont (font, 14.0f).getHeight()) > 0.0f);

           #if JUCE_MAC
            beginTest ("package font families remain isolated across live and retained caches");
            juce::MemoryBlock monacoBytes;
            const juce::File monacoFile ("/System/Library/Fonts/Monaco.ttf");
            expect (monacoFile.existsAsFile());
            expect (monacoFile.loadFileAsData (monacoBytes));
            SkinValidationReport firstPackageReport;
            auto firstPackage = packageFrom (
                "font-owner",
                { { "assets/owner.ttf", monacoBytes } }, firstPackageReport);
            SkinFontValue ownedFont;
            ownedFont.source = "assets/owner.ttf";
            ownedFont.families.add ("Monaco");
            SkinValidationReport firstCacheReport;
            auto firstCache = SkinAssetCache::create (
                *firstPackage, "font-owner", "1.0.0", {}, { ownedFont },
                firstCacheReport);
            expect (firstCache.has_value());
            if (firstCache)
            {
                auto retainedFont = (*firstCache)->makeFont (ownedFont, 14.0f);
                const auto ownedFamily = retainedFont.getTypefaceName();
                SkinValidationReport secondPackageReport;
                auto secondPackage = packageFrom (
                    "font-neighbour",
                    { { "assets/broken.ttf", juce::MemoryBlock ("not-a-font", 10) } },
                    secondPackageReport);
                SkinFontValue neighbourFont;
                neighbourFont.source = "assets/broken.ttf";
                neighbourFont.families.add (ownedFamily);
                SkinValidationReport secondCacheReport;
                auto secondCache = SkinAssetCache::create (
                    *secondPackage, "font-neighbour", "1.0.0", {}, { neighbourFont },
                    secondCacheReport);
                expect (secondCache.has_value());
                if (secondCache)
                    expect ((*secondCache)->makeFont (
                                neighbourFont, 14.0f).getTypefaceName() != ownedFamily);

                firstCache.reset();
                expect (retainedFont.getTypefaceName() == ownedFamily);
                SkinValidationReport retainedCacheReport;
                auto retainedNeighbourCache = SkinAssetCache::create (
                    *secondPackage, "font-neighbour-retained", "1.0.0", {},
                    { neighbourFont }, retainedCacheReport);
                expect (retainedNeighbourCache.has_value());
                if (retainedNeighbourCache)
                    expect ((*retainedNeighbourCache)->makeFont (
                                neighbourFont, 14.0f).getTypefaceName() != ownedFamily);
            }
           #endif
        }

        beginTest ("animation easing, retargeting, reduce motion and bounded activity are deterministic");
        {
            double now = 0.0;
            SkinAnimationEngine engine ([&] { return now; });
            expectWithinAbsoluteError (SkinAnimationEngine::applyEasing (
                                           SkinAnimationEasing::linear, 0.25),
                                       0.25, 1.0e-9);
            expectWithinAbsoluteError (SkinAnimationEngine::applyEasing (
                                           SkinAnimationEasing::easeInOut, 0.0),
                                       0.0, 1.0e-9);
            expectWithinAbsoluteError (SkinAnimationEngine::applyEasing (
                                           SkinAnimationEasing::easeInOut, 1.0),
                                       1.0, 1.0e-9);

            SkinRenderProgram program;
            program.transitions[SkinAnimationTrigger::value] = {
                100, 0, SkinAnimationEasing::linear, { "meter.opacity" }, {}, true, {}
            };
            SkinAnimationInput input;
            input.normalisedValue = 0.0;
            SkinAnimationPropertyMap values { { "meter.opacity", 0.0 } };
            auto displayed = engine.resolve ("meter", program, input, values);
            expectEquals (std::get<double> (displayed.at ("meter.opacity")), 0.0);

            input.normalisedValue = 1.0;
            values["meter.opacity"] = 1.0;
            displayed = engine.resolve ("meter", program, input, values);
            now = 50.0;
            displayed = engine.resolve ("meter", program, input, values);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.5, 1.0e-6);

            values["meter.opacity"] = 0.0;
            input.normalisedValue = 0.2;
            displayed = engine.resolve ("meter", program, input, values);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.5, 1.0e-6);
            now = 100.0;
            displayed = engine.resolve ("meter", program, input, values);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.25, 1.0e-6);

            now = 150.0;
            displayed = engine.resolve ("meter", program, input, values);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.0, 1.0e-6);
            expect (! engine.isTimerNeeded());

            engine.setReduceMotionEnabled (true);
            input.normalisedValue = 0.8;
            values["meter.opacity"] = 0.8;
            displayed = engine.resolve ("meter", program, input, values);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.8, 1.0e-6);
            expect (! engine.isTimerNeeded());
            engine.cancelAll();
            expectEquals (static_cast<int> (engine.activeTransitionCount()), 0);

            double cancellationNow = 0.0;
            SkinAnimationEngine cancellationEngine ([&] { return cancellationNow; });
            SkinAnimationInput cancellationInput;
            SkinAnimationPropertyMap cancellationValues { { "meter.opacity", 0.0 } };
            displayed = cancellationEngine.resolve (
                "cancel-stale", program, cancellationInput, cancellationValues);
            cancellationInput.normalisedValue = 1.0;
            cancellationValues["meter.opacity"] = 1.0;
            displayed = cancellationEngine.resolve (
                "cancel-stale", program, cancellationInput, cancellationValues);
            cancellationNow = 50.0;
            cancellationValues["meter.opacity"] = 0.25;
            displayed = cancellationEngine.resolve (
                "cancel-stale", program, cancellationInput, cancellationValues);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.25, 1.0e-6);
            expect (! cancellationEngine.isTimerNeeded());

            double optOutNow = 0.0;
            SkinAnimationEngine optOutEngine ([&] { return optOutNow; });
            auto optOutProgram = program;
            optOutProgram.transitions[SkinAnimationTrigger::value].respectReduceMotion = false;
            SkinAnimationInput optOutInput;
            SkinAnimationPropertyMap optOutValues { { "meter.opacity", 0.0 } };
            displayed = optOutEngine.resolve (
                "reduce-motion-opt-out", optOutProgram, optOutInput, optOutValues);
            optOutInput.normalisedValue = 1.0;
            optOutValues["meter.opacity"] = 1.0;
            displayed = optOutEngine.resolve (
                "reduce-motion-opt-out", optOutProgram, optOutInput, optOutValues);
            optOutEngine.setReduceMotionEnabled (true);
            expect (optOutEngine.isTimerNeeded());
            optOutNow = 50.0;
            displayed = optOutEngine.resolve (
                "reduce-motion-opt-out", optOutProgram, optOutInput, optOutValues);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("meter.opacity")),
                                       0.5, 1.0e-6);
        }

        beginTest ("entry animations start from declared values and active channels stop at 128");
        {
            double now = 0.0;
            SkinAnimationEngine engine ([&] { return now; });
            SkinRenderProgram entryProgram;
            entryProgram.transitions[SkinAnimationTrigger::workspaceEnter] = {
                200, 0, SkinAnimationEasing::linear, { "body.opacity" }, {}, true, {}
            };
            SkinAnimationInput entryInput;
            entryInput.entryTrigger = SkinAnimationTrigger::workspaceEnter;
            entryInput.entryFromValues["body.opacity"] = 0.0;
            SkinAnimationPropertyMap entryTarget { { "body.opacity", 1.0 } };
            auto displayed = engine.resolve (
                "entry", entryProgram, entryInput, entryTarget);
            expectEquals (std::get<double> (displayed.at ("body.opacity")), 0.0);
            now = 100.0;
            entryInput.entryTrigger.reset();
            displayed = engine.resolve ("entry", entryProgram, entryInput, entryTarget);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("body.opacity")),
                                       0.5, 1.0e-6);
            now = 250.0;
            displayed = engine.resolve ("entry", entryProgram, entryInput, entryTarget);
            expectEquals (std::get<double> (displayed.at ("body.opacity")), 1.0);
            entryInput.entryTrigger = SkinAnimationTrigger::workspaceEnter;
            entryInput.entrySequence = 1;
            displayed = engine.resolve ("entry", entryProgram, entryInput, entryTarget);
            expectEquals (std::get<double> (displayed.at ("body.opacity")), 0.0);
            now = 350.0;
            displayed = engine.resolve ("entry", entryProgram, entryInput, entryTarget);
            expectWithinAbsoluteError (std::get<double> (displayed.at ("body.opacity")),
                                       0.5, 1.0e-6);

            SkinRenderProgram boundedProgram;
            SkinTransition valueTransition;
            valueTransition.durationMs = 1000;
            valueTransition.easing = SkinAnimationEasing::linear;
            SkinAnimationPropertyMap initial;
            SkinAnimationPropertyMap targets;
            for (int i = 0; i < 129; ++i)
            {
                const auto property = "layer.value" + juce::String (i);
                valueTransition.properties.push_back (property);
                initial[property] = 0.0;
                targets[property] = 1.0;
            }
            boundedProgram.transitions[SkinAnimationTrigger::value] = valueTransition;
            SkinAnimationInput boundedInput;
            displayed = engine.resolve ("bounded", boundedProgram, boundedInput, initial);
            boundedInput.normalisedValue = 1.0;
            displayed = engine.resolve ("bounded", boundedProgram, boundedInput, targets);
            expectEquals (static_cast<int> (engine.activeTransitionCount()), 128);
            // One entry transition is already active; the bounded renderer may occupy only 127.
            expectEquals (std::get<double> (displayed.at ("layer.value128")), 1.0);
            engine.cancelAll();
            expectEquals (static_cast<int> (engine.activeTransitionCount()), 0);
        }

        beginTest ("discrete resource transitions retain the displayed package cache");
        {
            SkinValidationReport cacheReport;
            const auto firstPackage = packageFrom (
                "transition-cache-first", solidPngPackage (juce::Colours::red),
                cacheReport);
            const auto secondPackage = packageFrom (
                "transition-cache-second", solidPngPackage (juce::Colours::blue),
                cacheReport);
            const auto thirdPackage = packageFrom (
                "transition-cache-third", solidPngPackage (juce::Colours::green),
                cacheReport);
            expect (firstPackage.has_value() && secondPackage.has_value()
                        && thirdPackage.has_value(),
                    cacheReport.toText());
            if (! firstPackage || ! secondPackage || ! thirdPackage) return;

            const auto firstCache = SkinAssetCache::create (
                *firstPackage, "first", "1.0.0", { "assets/pixels.png" }, {},
                cacheReport);
            const auto secondCache = SkinAssetCache::create (
                *secondPackage, "second", "1.0.0", { "assets/pixels.png" }, {},
                cacheReport);
            const auto thirdCache = SkinAssetCache::create (
                *thirdPackage, "third", "1.0.0", { "assets/pixels.png" }, {},
                cacheReport);
            expect (firstCache.has_value() && secondCache.has_value()
                        && thirdCache.has_value(),
                    cacheReport.toText());
            if (! firstCache || ! secondCache || ! thirdCache) return;

            double now = 0.0;
            SkinAnimationEngine engine ([&] { return now; });
            SkinRenderProgram program;
            program.transitions[SkinAnimationTrigger::hover] = {
                100, 0, SkinAnimationEasing::linear, { "art.source" }, {}, true, {}
            };
            program.propertyDescriptors["art.source"] =
                skinRenderPropertyDescriptor (SkinPrimitive::image, "source");

            SkinAnimationInput input;
            SkinAnimationPropertyMap values {
                { "art.source", juce::String ("assets/pixels.png") }
            };
            SkinAnimationResourceMap resources { { "art.source", *firstCache } };
            SkinAnimationResourceMap displayedResources;
            auto displayed = engine.resolve (
                "resource", program, input, values, resources, displayedResources);
            expectEquals (std::get<juce::String> (displayed.at ("art.source")),
                          juce::String ("assets/pixels.png"));
            expect (displayedResources.at ("art.source") == *firstCache);

            input.state.hover = true;
            resources["art.source"] = *secondCache;
            displayed = engine.resolve (
                "resource", program, input, values, resources, displayedResources);
            expect (displayedResources.at ("art.source") == *firstCache);
            now = 99.0;
            displayed = engine.resolve (
                "resource", program, input, values, resources, displayedResources);
            expect (displayedResources.at ("art.source") == *firstCache);
            now = 100.0;
            displayed = engine.resolve (
                "resource", program, input, values, resources, displayedResources);
            expect (displayedResources.at ("art.source") == *secondCache);

            double retargetNow = 0.0;
            SkinAnimationEngine retargetEngine ([&] { return retargetNow; });
            SkinAnimationInput retargetInput;
            resources["art.source"] = *firstCache;
            auto retargetDisplayed = retargetEngine.resolve (
                "resource-retarget", program, retargetInput, values, resources,
                displayedResources);
            retargetInput.state.hover = true;
            resources["art.source"] = *secondCache;
            retargetDisplayed = retargetEngine.resolve (
                "resource-retarget", program, retargetInput, values, resources,
                displayedResources);
            retargetNow = 50.0;
            retargetInput.state.selected = true;
            resources["art.source"] = *thirdCache;
            retargetDisplayed = retargetEngine.resolve (
                "resource-retarget", program, retargetInput, values, resources,
                displayedResources);
            expectEquals (std::get<juce::String> (
                              retargetDisplayed.at ("art.source")),
                          juce::String ("assets/pixels.png"));
            expect (displayedResources.at ("art.source") == *thirdCache);

            auto assetLayer = SkinRenderLayer();
            assetLayer.id = "art";
            assetLayer.primitive = SkinPrimitive::image;
            setCommon (assetLayer);
            setExplicitFrame (assetLayer, { 0.0f, 0.0f, 16.0f, 16.0f });
            assetLayer.properties["source"] = literalBinding (
                SkinPrimitive::image, "source", juce::String ("assets/pixels.png"));
            assetLayer.properties["source"].assets = *firstCache;
            assetLayer.properties["fit"] = literalBinding (
                SkinPrimitive::image, "fit", juce::String ("fill"));
            assetLayer.properties["tint"] = literalBinding (
                SkinPrimitive::image, "tint", juce::Colours::white);

            auto renderProgram = simpleProgram (
                SkinRenderTarget::artwork, std::move (assetLayer));
            auto hoverBinding = literalBinding (
                SkinPrimitive::image, "source", juce::String ("assets/pixels.png"));
            hoverBinding.assets = *secondCache;
            renderProgram.variants[1].properties["art.source"] =
                std::move (hoverBinding);
            renderProgram.transitions = program.transitions;
            renderProgram.propertyDescriptors = program.propertyDescriptors;

            double renderNow = 0.0;
            SkinAnimationEngine renderEngine ([&] { return renderNow; });
            SkinRenderer renderer (*firstCache);
            FaultRecorder sink;
            SkinRenderContext context;
            context.bounds = { 0.0f, 0.0f, 16.0f, 16.0f };
            context.contentBounds = context.bounds;
            const auto draw = [&]
            {
                juce::Image image (juce::Image::ARGB, 16, 16, true);

                {
                    juce::Graphics graphics (image);
                    expect (renderer.draw (graphics, renderProgram, context, true, sink,
                                           &renderEngine, "resource-render")
                            == SkinRenderStatus::ok);
                }
                return image.getPixelAt (8, 8);
            };
            expect (draw() == juce::Colours::red);
            context.state.hover = true;
            expect (draw() == juce::Colours::red);
            renderNow = 100.0;
            expect (draw() == juce::Colours::blue);

            SkinRenderLayer repeat;
            repeat.id = "repeat";
            repeat.primitive = SkinPrimitive::repeat;
            repeat.repeatSource = "meter.channels";
            repeat.repeatLimit = 1;
            setCommon (repeat);
            repeat.children.push_back (renderProgram.baseLayers.front());
            auto repeatProgram = simpleProgram (
                SkinRenderTarget::outputStage, std::move (repeat));
            repeatProgram.variants[1].properties["art.source"] =
                renderProgram.variants[1].properties.at ("art.source");
            repeatProgram.transitions = renderProgram.transitions;
            repeatProgram.propertyDescriptors = renderProgram.propertyDescriptors;

            double repeatNow = 0.0;
            SkinAnimationEngine repeatEngine ([&] { return repeatNow; });
            SkinRenderer repeatRenderer (*firstCache);
            SkinRenderContext repeatContext;
            repeatContext.bounds = { 0.0f, 0.0f, 16.0f, 16.0f };
            repeatContext.contentBounds = repeatContext.bounds;
            repeatContext.meterLabels.add ("L");
            const auto drawRepeat = [&]
            {
                juce::Image image (juce::Image::ARGB, 16, 16, true);

                {
                    juce::Graphics graphics (image);
                    expect (repeatRenderer.draw (
                                graphics, repeatProgram, repeatContext, true, sink,
                                &repeatEngine, "resource-repeat")
                            == SkinRenderStatus::ok);
                }
                return image.getPixelAt (8, 8);
            };
            expect (drawRepeat() == juce::Colours::red);
            repeatContext.state.hover = true;
            expect (drawRepeat() == juce::Colours::red);
            repeatNow = 100.0;
            expect (drawRepeat() == juce::Colours::blue);
        }

        beginTest ("colour animation uses premultiplied linear sRGB and strings switch atomically");
        {
            const auto midpoint = SkinAnimationEngine::interpolateValue (
                juce::Colour (0x00ff0000), juce::Colour (0xff0000ff), 0.5);
            expect (std::holds_alternative<juce::Colour> (midpoint));
            const auto colour = std::get<juce::Colour> (midpoint);
            expectWithinAbsoluteError (static_cast<double> (colour.getFloatAlpha()), 0.5, 0.01);
            expect (colour.getBlue() > colour.getRed());
            expectEquals (std::get<juce::String> (SkinAnimationEngine::interpolateValue (
                              juce::String ("old"), juce::String ("new"), 0.99)),
                          juce::String ("old"));
            expectEquals (std::get<juce::String> (SkinAnimationEngine::interpolateValue (
                              juce::String ("old"), juce::String ("new"), 1.0)),
                          juce::String ("new"));
        }

        beginTest ("renderer reports recoverable and fatal faults without throwing from paint");
        {
            auto imageLayer = rectangleLayer (
                "missing", { 0.0f, 0.0f, 20.0f, 20.0f }, juce::Colours::white);
            imageLayer.primitive = SkinPrimitive::image;
            imageLayer.properties.clear();
            setCommon (imageLayer);
            for (const auto& [name, value] : std::initializer_list<std::pair<const char*, SkinCompiledRenderValue>> {
                     { "frame.x", 0.0 }, { "frame.y", 0.0 }, { "frame.width", 20.0 },
                     { "frame.height", 20.0 }, { "source", juce::String ("assets/missing.png") },
                     { "fit", juce::String ("contain") }, { "tint", juce::Colours::white } })
                imageLayer.properties[name] = literalBinding (imageLayer.primitive, name, value);
            imageLayer.properties["opacity"] = literalBinding (
                imageLayer.primitive, "opacity", 0.5);
            const auto program = simpleProgram (SkinRenderTarget::surface, imageLayer);
            juce::Image image (juce::Image::ARGB, 20, 20, true);

            SkinRenderContext context;
            context.bounds = image.getBounds().toFloat();
            context.contentBounds = context.bounds;
            FaultRecorder optionalSink;
            FaultRecorder requiredSink;
            SkinRenderer renderer;
            {
                juce::Graphics graphics (image);
                expect (renderer.draw (graphics, program, context, false, optionalSink)
                        == SkinRenderStatus::recoverableFault);
            }
            expect (optionalSink.diagnostics.front().code == "RUNTIME001");
            {
                juce::Graphics graphics (image);
                expect (renderer.draw (graphics, program, context, true, requiredSink)
                        == SkinRenderStatus::fatalFault);
            }
            expect (requiredSink.diagnostics.front().code == "RUNTIME002");

            auto validProgram = simpleProgram (
                SkinRenderTarget::surface,
                rectangleLayer ("valid", { 0.0f, 0.0f, 20.0f, 20.0f },
                                juce::Colours::red));
            FaultRecorder validSink;
            {
                juce::Graphics graphics (image);
                expect (renderer.draw (graphics, validProgram, context, true, validSink)
                        == SkinRenderStatus::ok);
            }
            expect (image.getPixelAt (10, 10) == juce::Colours::red);

            auto extreme = rectangleLayer (
                "extreme", { 0.0f, 0.0f, 10.0f, 10.0f }, juce::Colours::white);
            extreme.properties["frame.x"] = literalBinding (
                extreme.primitive, "frame.x", 1.0e308);
            auto extremeProgram = simpleProgram (SkinRenderTarget::surface, extreme);
            FaultRecorder extremeSink;
            {
                juce::Graphics graphics (image);
                expect (renderer.draw (graphics, extremeProgram, context, true, extremeSink)
                        == SkinRenderStatus::fatalFault);
            }
            expect (extremeSink.diagnostics.front().message.containsIgnoreCase ("range"));
        }

        beginTest ("renderer contains shadow layer and offscreen pixel budget violations");
        {
            SkinRenderProgram tooManyShadows;
            tooManyShadows.target = SkinRenderTarget::surface;
            for (int i = 0; i < 9; ++i)
            {
                auto layer = rectangleLayer (
                    "shadow-" + juce::String (i),
                    { static_cast<float> (i * 3), static_cast<float> (i * 2), 20.0f, 20.0f },
                    juce::Colours::white);
                layer.properties["shadow.color"] = literalBinding (
                    layer.primitive, "shadow.color", juce::Colour (0x80000000));
                layer.properties["shadow.radius"] = literalBinding (
                    layer.primitive, "shadow.radius", 2.0);
                tooManyShadows.baseLayers.push_back (std::move (layer));
            }
            const auto& names = skinVariantNamesInMergeOrder();
            for (size_t i = 0; i < names.size(); ++i)
                tooManyShadows.variants[i].name = names[i];
            juce::Image image (juce::Image::ARGB, 200, 120, true);
            juce::Graphics graphics (image);
            SkinRenderContext context;
            context.bounds = image.getBounds().toFloat();
            context.contentBounds = context.bounds;
            FaultRecorder sink;
            SkinRenderer renderer;
            expect (renderer.draw (graphics, tooManyShadows, context, true, sink)
                    == SkinRenderStatus::fatalFault);
            expect (sink.diagnostics.back().pointer.contains ("shadow"));

            auto huge = rectangleLayer (
                "huge", { 0.0f, 0.0f, 5000.0f, 5000.0f }, juce::Colours::white);
            huge.properties["shadow.color"] = literalBinding (
                huge.primitive, "shadow.color", juce::Colour (0x80000000));
            huge.properties["shadow.radius"] = literalBinding (
                huge.primitive, "shadow.radius", 20.0);
            auto hugeProgram = simpleProgram (SkinRenderTarget::surface, huge);
            FaultRecorder hugeSink;
            expect (renderer.draw (graphics, hugeProgram, context, true, hugeSink)
                    == SkinRenderStatus::fatalFault);
            expect (hugeSink.diagnostics.back().message.containsIgnoreCase ("pixel"));
        }

        beginTest ("table cells follow header reordering visibility and resizing");
        {
            std::map<juce::String, SkinRenderStyleEntry> entries;
            const auto addStyle = [&] (const juce::String& name,
                                       SkinRenderTarget target, juce::Colour colour)
            {
                auto layer = rectangleLayer ("body", {}, colour);
                for (const auto* property : {
                         "frame.x", "frame.y", "frame.width", "frame.height" })
                    layer.properties.erase (property);
                layer.properties["frame.anchor"] = literalBinding (
                    SkinPrimitive::rectangle, "frame.anchor", juce::String ("bounds"));
                entries[name].renderer = simpleProgram (target, std::move (layer));
            };
            addStyle ("table.default", SkinRenderTarget::table,
                      juce::Colours::transparentBlack);
            addStyle ("surface.default", SkinRenderTarget::surface,
                      juce::Colours::transparentBlack);
            addStyle ("label.title", SkinRenderTarget::label, juce::Colours::red);
            addStyle ("label.location", SkinRenderTarget::label, juce::Colours::green);
            addStyle ("label.duration", SkinRenderTarget::label, juce::Colours::blue);
            entries["table.default"].slotStyles = {
                { "cell.title", "label.title" },
                { "cell.location", "label.location" },
                { "cell.duration", "label.duration" }
            };
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String& style)
                {
                    const auto name = style == "default"
                        ? skinRenderTargetName (target) + ".default" : style;
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (name), nullptr, &sink, true
                    };
                }, renderer);
            SkinTableListBox table;
            table.setSize (180, 60);
            auto& header = table.getHeader();
            header.setStretchToFitActive (false);
            header.addColumn ("Title", 1, 60);
            header.addColumn ("Location", 2, 80);
            header.addColumn ("Duration", 3, 40);
            juce::Image image (juce::Image::ARGB, 180, 20, true);
            const auto drawRow = [&]
            {
                image.clear (image.getBounds(), juce::Colours::transparentBlack);
                juce::Graphics graphics (image);
                adapter.drawListBoxRow (
                    graphics, table, 0, 180, 20, false, "Track", "Folder", "3:12",
                    table.rowGeometry (20));
            };
            drawRow();
            expect (image.getPixelAt (10, 10) == juce::Colours::red);
            expect (image.getPixelAt (80, 10) == juce::Colours::green);
            expect (image.getPixelAt (160, 10) == juce::Colours::blue);

            header.moveColumn (2, 0);
            drawRow();
            expect (image.getPixelAt (10, 10) == juce::Colours::green);
            expect (image.getPixelAt (90, 10) == juce::Colours::red);
            expect (image.getPixelAt (160, 10) == juce::Colours::blue);

            header.setColumnVisible (2, false);
            drawRow();
            expect (image.getPixelAt (10, 10) == juce::Colours::red);
            expect (image.getPixelAt (80, 10) == juce::Colours::blue);
            expect (image.getPixelAt (150, 10).isTransparent());

            header.setColumnWidth (1, 90);
            drawRow();
            expect (image.getPixelAt (80, 10) == juce::Colours::red);
            expect (image.getPixelAt (110, 10) == juce::Colours::blue);
            expect (image.getPixelAt (150, 10).isTransparent());
            expect (sink.diagnostics.empty());
        }

        beginTest ("JUCE adapter propagates semantic label and table alignment");
        {
            auto alignmentLayer = rectangleLayer (
                "alignment-probe", { 0.0f, 0.0f, 200.0f, 120.0f },
                juce::Colours::white);
            for (const auto* property : {
                     "frame.x", "frame.y", "frame.width", "frame.height" })
                alignmentLayer.properties.erase (property);
            alignmentLayer.properties["frame.anchor"] = literalBinding (
                SkinPrimitive::rectangle, "frame.anchor", juce::String ("bounds"));

            SkinValidationReport expressionReport;
            SkinTokenStore noTokens;
            const auto rightAligned = SkinExpression::compile (
                "text.align == 'right'",
                skinRenderContextForTarget (SkinRenderTarget::label),
                noTokens, expressionReport, "adapter-alignment", "/visible");
            expect (rightAligned.has_value(), expressionReport.toText());
            if (rightAligned)
            {
                auto visible = literalBinding (
                    SkinPrimitive::rectangle, "visible", false);
                visible.value = *rightAligned;
                alignmentLayer.properties["visible"] = std::move (visible);
            }

            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            entries[SkinRenderTarget::label].renderer = simpleProgram (
                SkinRenderTarget::label, std::move (alignmentLayer));
            entries[SkinRenderTarget::surface].renderer = simpleProgram (
                SkinRenderTarget::surface,
                rectangleLayer ("transparent", { 0.0f, 0.0f, 200.0f, 120.0f },
                                juce::Colours::transparentBlack));
            entries[SkinRenderTarget::table].renderer = simpleProgram (
                SkinRenderTarget::table,
                rectangleLayer ("table", { 0.0f, 0.0f, 200.0f, 120.0f },
                                juce::Colours::transparentBlack));
            entries[SkinRenderTarget::table].slotStyles = {
                { "header.text", "label.table" },
                { "cell.title", "label.table" },
                { "cell.location", "label.table" },
                { "cell.duration", "label.table" }
            };

            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (target), nullptr, &sink, true
                    };
                }, renderer);
            juce::Image image (juce::Image::ARGB, 200, 120, true);


            SkinComponentBindings bindings;
            SkinValidationReport bindingReport;
            juce::Label queueCount;
            queueCount.setBounds (0, 0, 100, 20);
            queueCount.setText ("8 tracks", juce::dontSendNotification);
            queueCount.setJustificationType (juce::Justification::centredLeft);
            expect (bindings.bind ("queue.count", queueCount, bindingReport),
                    bindingReport.toText());
            {
                juce::Graphics graphics (image);
                adapter.drawLabel (graphics, queueCount);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0,
                    "bound queue.count must use catalog right alignment");

            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            juce::Label unbound;
            unbound.setBounds (0, 0, 100, 20);
            unbound.setText ("Fallback", juce::dontSendNotification);
            unbound.setJustificationType (juce::Justification::centredRight);
            {
                juce::Graphics graphics (image);
                adapter.drawLabel (graphics, unbound);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0,
                    "unbound labels must preserve JUCE justification");

            juce::ListBox list;
            const JuceSkinAdapter::ListBoxRowGeometry rowGeometry {
                { 0.0f, 0.0f, 20.0f, 20.0f },
                { 30.0f, 0.0f, 20.0f, 20.0f },
                { 60.0f, 0.0f, 10.0f, 20.0f }
            };
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                adapter.drawListBoxRow (
                    graphics, list, 0, 100, 20, false, "Track", "Folder", "3:12",
                    rowGeometry);
            }
            expect (image.getPixelAt (10, 10).isTransparent());
            expect (image.getPixelAt (40, 10).isTransparent());
            expect (image.getPixelAt (65, 10).getAlpha() > 0,
                    "duration cells must be right aligned");

            SkinTableListBox table;
            table.setBounds (0, 0, 200, 120);
            table.getHeader().addColumn ("Title", 1, 80);
            table.getHeader().addColumn ("Location", 2, 70);
            table.getHeader().addColumn ("Duration", 3, 50);
            auto& header = table.getHeader();
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                adapter.drawTableHeaderColumn (
                    graphics, header, "Title", 1, 80, 20, false, false, 0);
            }
            expect (image.getPixelAt (10, 10).isTransparent());
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                adapter.drawTableHeaderColumn (
                    graphics, header, "Duration", 3, 50, 20, false, false, 0);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0,
                    "duration header must be right aligned");
        }

        beginTest ("JUCE adapter routes native and ancillary controls to frozen targets");
        {
            std::vector<SkinRenderTarget> requested;
            std::vector<std::pair<SkinRenderTarget, juce::String>> requestedStyles;
            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            for (const auto target : { SkinRenderTarget::textButton,
                                       SkinRenderTarget::toggleButton,
                                       SkinRenderTarget::comboBox,
                                       SkinRenderTarget::textEditor,
                                       SkinRenderTarget::linearSlider,
                                       SkinRenderTarget::rotarySlider,
                                       SkinRenderTarget::progressBar,
                                       SkinRenderTarget::scrollBar,
                                       SkinRenderTarget::popupMenu,
                                       SkinRenderTarget::tooltip,
                                       SkinRenderTarget::table,
                                       SkinRenderTarget::surface,
                                       SkinRenderTarget::label,
                                       SkinRenderTarget::artwork,
                                       SkinRenderTarget::effectChain,
                                       SkinRenderTarget::outputStage })
                entries[target].renderer = simpleProgram (
                    target, rectangleLayer ("body", { 0.0f, 0.0f, 200.0f, 120.0f },
                                            juce::Colours::white));
            SkinValidationReport contextExpressionReport;
            SkinTokenStore noTokens;
            const auto setVisibilityExpression = [&] (
                SkinRenderTarget target, const juce::String& expression)
            {
                const auto compiled = SkinExpression::compile (
                    expression, skinRenderContextForTarget (target), noTokens,
                    contextExpressionReport, "adapter", "/visible");
                expect (compiled.has_value(), contextExpressionReport.toText());
                if (! compiled) return;
                auto visible = literalBinding (
                    SkinPrimitive::rectangle, "visible", false);
                visible.value = *compiled;
                entries[target].renderer.baseLayers.front().properties["visible"] =
                    std::move (visible);
            };
            setVisibilityExpression (
                SkinRenderTarget::scrollBar,
                "scroll.part == 'bar' and scroll.thumbStart == 20 and scroll.thumbSize == 30");
            setVisibilityExpression (
                SkinRenderTarget::popupMenu,
                "menu.part == 'item' and menu.itemIndex == 0 and state.hover and not state.selected");
            setVisibilityExpression (SkinRenderTarget::label, "state.hover");
            auto& labelLayer = entries[SkinRenderTarget::label].renderer.baseLayers.front();
            for (const auto* property : { "frame.x", "frame.y", "frame.width", "frame.height" })
                labelLayer.properties.erase (property);
            labelLayer.properties["frame.anchor"] = literalBinding (
                SkinPrimitive::rectangle, "frame.anchor", juce::String ("bounds"));
            entries[SkinRenderTarget::surface].renderer.baseLayers.front()
                .properties["fill"] = literalBinding (
                    SkinPrimitive::rectangle, "fill", juce::Colours::transparentBlack);
            entries[SkinRenderTarget::table].slotStyles = {
                { "header.surface", "surface.header" },
                { "header.text", "label.header" },
                { "row.surface", "surface.row" },
                { "cell.title", "label.title" },
                { "cell.location", "label.location" },
                { "cell.duration", "label.duration" }
            };
            entries[SkinRenderTarget::effectChain].slotStyles = {
                { "slot.surface", "surface.slot" },
                { "slot.name", "label.slot" },
                { "slot.bypass", "textButton.bypass" },
                { "slot.editor", "textButton.editor" },
                { "slot.remove", "textButton.remove" },
                { "slot.dragIndicator", "surface.drag" }
            };
            setVisibilityExpression (
                SkinRenderTarget::effectChain, "chain.slotCount == 3");
            auto& buttonProgram = entries[SkinRenderTarget::textButton].renderer;
            SkinValidationReport scaleExpressionReport;
            const auto retinaVisible = SkinExpression::compile (
                "scaleFactor == 2 and icon.name == 'play'",
                skinRenderContextForTarget (SkinRenderTarget::textButton),
                noTokens, scaleExpressionReport, "adapter", "/scaleFactor");
            expect (retinaVisible.has_value());
            if (retinaVisible)
            {
                auto visible = literalBinding (
                    SkinPrimitive::rectangle, "visible", false);
                visible.value = *retinaVisible;
                buttonProgram.baseLayers.front().properties["visible"] = std::move (visible);
            }
            buttonProgram.variants[1].properties["body.opacity"] = literalBinding (
                SkinPrimitive::rectangle, "opacity", 0.5);
            buttonProgram.transitions[SkinAnimationTrigger::hover] = {
                100, 0, SkinAnimationEasing::linear, { "body.opacity" }, {}, true, {}
            };
            FaultRecorder sink;
            SkinRenderer renderer;
            SkinAnimationEngine animations ([] { return 0.0; });
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
                {
                    requested.push_back (target);
                    requestedStyles.emplace_back (target, style);
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (target), &animations, &sink, true
                    };
                }, renderer);

            juce::Image image (juce::Image::ARGB, 200, 120, true);

            class RetinaTextButton final : public juce::TextButton
            {
            public:
                using juce::TextButton::TextButton;
                float getDesktopScaleFactor() const override { return 2.0f; }
            };
            RetinaTextButton textButton ("Play");
            textButton.getProperties().set (JuceSkinAdapter::scaleFactorProperty, 2.0);
            textButton.getProperties().set (SkinComponentBindings::iconNameProperty, "play");
            {
                juce::Graphics graphics (image);
                adapter.drawButtonBackground (graphics, textButton, {}, false, false);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            {
                juce::Graphics graphics (image);
                adapter.drawButtonBackground (graphics, textButton, {}, true, false);
            }
            juce::ToggleButton toggle ("Repeat");
            {
                juce::Graphics graphics (image);
                adapter.drawToggleButton (graphics, toggle, false, false);
            }
            juce::ComboBox combo;
            {
                juce::Graphics graphics (image);
                adapter.drawComboBox (graphics, 100, 30, false, 70, 0, 30, 30, combo);
            }
            juce::TextEditor editor;
            {
                juce::Graphics graphics (image);
                adapter.fillTextEditorBackground (graphics, 100, 30, editor);
            }
            juce::Slider slider;
            {
                juce::Graphics graphics (image);
                adapter.drawLinearSlider (graphics, 0, 0, 100, 20, 50, 0, 100,
                                          juce::Slider::LinearHorizontal, slider);
            }
            {
                juce::Graphics graphics (image);
                adapter.drawRotarySlider (graphics, 0, 0, 40, 40, 0.5f, 0.0f, 6.0f, slider);
            }
            double progressValue = 0.5;
            juce::ProgressBar progress (progressValue);
            {
                juce::Graphics graphics (image);
                adapter.drawProgressBar (graphics, progress, 100, 20, 0.5, "50%");
            }
            juce::ScrollBar scroll (true);
            scroll.getProperties().set (JuceSkinAdapter::styleProperty, "scrollBar.loud");
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                adapter.drawScrollbar (graphics, scroll, 0, 0, 12, 100, true,
                                       20, 30, false, false);
            }
            expect (image.getPixelAt (6, 50).getAlpha() > 0);
            {
                juce::Graphics graphics (image);
                adapter.drawScrollbarButton (graphics, scroll, 12, 12, 0, true, false, false);
            }
            {
                juce::Graphics graphics (image);
                adapter.drawPopupMenuBackground (graphics, 100, 80);
            }
            juce::PopupMenu::Item popupItem;
            popupItem.itemID = 73;
            popupItem.text = "Item";
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                adapter.drawPopupMenuItemWithOptions (
                    graphics, { 0, 0, 100, 20 }, true, popupItem, {});
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            {
                juce::Graphics graphics (image);
                adapter.drawPopupMenuItem (graphics, { 0, 20, 100, 4 }, true, true, false,
                                           false, false, {}, {}, nullptr, nullptr);
            }
            {
                juce::Graphics graphics (image);
                adapter.drawPopupMenuSectionHeader (graphics, { 0, 24, 100, 20 }, "Section");
            }
            {
                juce::Graphics graphics (image);
                adapter.drawPopupMenuUpDownArrow (graphics, 100, 12, true);
            }
            {
                juce::Graphics graphics (image);
                adapter.drawPopupMenuColumnSeparatorWithOptions (
                    graphics, { 50, 0, 1, 80 }, {});
            }
            {
                juce::Graphics graphics (image);
                adapter.drawTooltip (graphics, "Tip", 100, 20);
            }
            SkinTableListBox table;
            table.setBounds (0, 0, 200, 120);
            table.setLookAndFeel (&adapter);
            table.getProperties().set (JuceSkinAdapter::styleProperty,
                                       "table.custom");
            table.getHeader().addColumn ("Title", 1, 100);
            table.getHeader().addColumn ("Location", 2, 100);
            auto& header = table.getHeader();
            expect (header.isStretchToFitActive());
            table.setSize (201, 120);
            expect (header.getTotalWidth() <= table.getWidth(),
                    "skinnable table columns must fit without a horizontal scrollbar");
            {
                juce::Graphics graphics (image);
                adapter.drawTableHeaderBackground (graphics, header);
            }
            {
                juce::Graphics graphics (image);
                adapter.drawTableHeaderColumn (
                    graphics, header, "Title", 1, 100, 20, false, false, 0);
            }
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                table.paint (graphics);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0,
                    "empty table body must use its bound table renderer");
            expect (std::find (requestedStyles.begin(), requestedStyles.end(),
                               std::pair { SkinRenderTarget::table,
                                           juce::String ("table.custom") })
                    != requestedStyles.end(),
                    "table header and body must inherit the owner style");
            juce::ListBox list;
            adapter.configureListBox (list);
            {
                juce::Graphics graphics (image);
                adapter.drawListBoxBackground (graphics, list, 12, 3);
            }
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            const JuceSkinAdapter::ListBoxRowGeometry rowGeometry {
                { 0.0f, 0.0f, 20.0f, 20.0f },
                { 30.0f, 0.0f, 20.0f, 20.0f },
                { 60.0f, 0.0f, 10.0f, 20.0f }
            };
            {
                juce::Graphics graphics (image);
                adapter.drawListBoxRow (
                    graphics, list, 2, 100, 20, true, "Track", "Folder", "3:12",
                    rowGeometry, true, false);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            expect (image.getPixelAt (40, 10).getAlpha() > 0);
            expect (image.getPixelAt (65, 10).getAlpha() > 0);
            expect (image.getPixelAt (25, 10).isTransparent());
            expect (image.getPixelAt (55, 10).isTransparent());

            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            auto hiddenLocation = rowGeometry;
            hiddenLocation.location = {};
            {
                juce::Graphics graphics (image);
                adapter.drawListBoxRow (
                    graphics, list, 2, 100, 20, true, "Track", "Folder", "3:12",
                    hiddenLocation, true, false);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            expect (image.getPixelAt (65, 10).getAlpha() > 0);
            expect (image.getPixelAt (55, 10).isTransparent(),
                    "an explicitly empty cell must remain hidden without fallback geometry");
            expect (list.findColour (juce::ListBox::backgroundColourId).isTransparent());
            expect (adapter.isAnimationDriveActive());

            juce::Component customComponent;
            customComponent.setBounds (0, 0, 200, 120);
            SkinRenderContext customContext;
            customContext.primaryText = "Track";
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                expect (adapter.drawCustomComponent (
                    graphics, customComponent, SkinRenderTarget::artwork,
                    customContext, "artwork"));
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            customContext.meterPeaks[0] = 0.75f;
            {
                juce::Graphics graphics (image);
                expect (adapter.drawCustomComponent (
                    graphics, customComponent, SkinRenderTarget::outputStage,
                    customContext, "output"));
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);

            SkinEffectChainViewport effectChain;
            effectChain.setBounds (0, 0, 200, 120);
            effectChain.setSlotCount (3);
            effectChain.setLookAndFeel (&adapter);
            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            {
                juce::Graphics graphics (image);
                effectChain.paint (graphics);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            SkinRenderContext slotContext;
            slotContext.bounds = { 0.0f, 0.0f, 40.0f, 20.0f };
            slotContext.contentBounds = slotContext.bounds;
            const auto expectSlot = [&] (juce::StringRef slot,
                                         SkinRenderTarget target,
                                         const juce::String& style)
            {
                {
                    juce::Graphics graphics (image);
                    expect (adapter.drawCustomSlot (
                        graphics, effectChain, SkinRenderTarget::effectChain,
                        slot, target, slotContext,
                        "slot:0:" + juce::String (slot)));
                }
                expect (std::find (requestedStyles.begin(), requestedStyles.end(),
                                   std::pair { target, style })
                        != requestedStyles.end(), juce::String (slot));
            };
            expectSlot ("slot.surface", SkinRenderTarget::surface,
                        "surface.slot");
            expectSlot ("slot.name", SkinRenderTarget::label, "label.slot");
            expectSlot ("slot.bypass", SkinRenderTarget::textButton,
                        "textButton.bypass");
            expectSlot ("slot.editor", SkinRenderTarget::textButton,
                        "textButton.editor");
            expectSlot ("slot.remove", SkinRenderTarget::textButton,
                        "textButton.remove");
            expectSlot ("slot.dragIndicator", SkinRenderTarget::surface,
                        "surface.drag");

            for (const auto target : { SkinRenderTarget::textButton,
                                       SkinRenderTarget::toggleButton,
                                       SkinRenderTarget::comboBox,
                                       SkinRenderTarget::textEditor,
                                       SkinRenderTarget::linearSlider,
                                       SkinRenderTarget::rotarySlider,
                                       SkinRenderTarget::progressBar,
                                       SkinRenderTarget::scrollBar,
                                       SkinRenderTarget::popupMenu,
                                       SkinRenderTarget::tooltip,
                                       SkinRenderTarget::table,
                                       SkinRenderTarget::artwork,
                                       SkinRenderTarget::effectChain,
                                       SkinRenderTarget::outputStage })
                expect (std::find (requested.begin(), requested.end(), target)
                        != requested.end());
            for (const auto target : { SkinRenderTarget::scrollBar,
                                       SkinRenderTarget::popupMenu,
                                       SkinRenderTarget::tooltip })
                for (const auto& [requestedTarget, style] : requestedStyles)
                    if (requestedTarget == target) expectEquals (style, juce::String ("default"));
            for (const auto& [target, style] : requestedStyles)
            {
                if (target == SkinRenderTarget::surface)
                    expect (style == "surface.header" || style == "surface.row"
                            || style == "surface.slot" || style == "surface.drag");
                if (target == SkinRenderTarget::label)
                    expect (style == "label.header" || style == "label.title"
                            || style == "label.location" || style == "label.duration"
                            || style == "label.slot");
            }
            for (const auto& expected : {
                     std::pair { SkinRenderTarget::surface, juce::String ("surface.header") },
                     std::pair { SkinRenderTarget::surface, juce::String ("surface.row") },
                     std::pair { SkinRenderTarget::label, juce::String ("label.header") },
                     std::pair { SkinRenderTarget::label, juce::String ("label.title") },
                     std::pair { SkinRenderTarget::label, juce::String ("label.location") },
                     std::pair { SkinRenderTarget::label, juce::String ("label.duration") } })
                expect (std::find (requestedStyles.begin(), requestedStyles.end(), expected)
                        != requestedStyles.end());

            table.setLookAndFeel (nullptr);
            effectChain.setLookAndFeel (nullptr);
        }

        beginTest ("native single-value Slider paint supplies its actual normalized value including skew");
        {
            auto fill = rectangleLayer ("fill", { 0.0f, 0.0f, 100.0f, 12.0f }, juce::Colours::red);
            SkinTokenStore tokens;
            SkinValidationReport report;
            const auto width = SkinExpression::compile ("value.normalized * 100",
                skinRenderContextForTarget (SkinRenderTarget::linearSlider), tokens, report, "slider", "/frame/width");
            expect (width.has_value(), report.toText());
            if (width) fill.properties["frame.width"].value = *width;
            SkinRenderStyleEntry entry;
            entry.renderer = simpleProgram (SkinRenderTarget::linearSlider, std::move (fill));
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    target == SkinRenderTarget::linearSlider ? &entry : nullptr, nullptr, &sink, true
                };
            }, renderer);
            juce::Slider slider;
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setLookAndFeel (&adapter);
            slider.setBounds (0, 0, 120, 24);
            const auto expectWidth = [&] (double value, int expectedPixels)
            {
                slider.setValue (value, juce::dontSendNotification);
                juce::Image image (juce::Image::ARGB, 120, 24, true);

                {
                    juce::Graphics graphics (image);
                    slider.paintEntireComponent (graphics, true);
                }
                int pixels = 0;
                for (int x = 0; x < image.getWidth(); ++x)
                    if (image.getPixelAt (x, 6).getAlpha() > 127) ++pixels;
                expectEquals (pixels, expectedPixels, "slider value " + juce::String (value));
            };
            slider.setRange (0.0, 1.0);
            for (const auto& [value, pixels] : { std::pair { 0.0, 0 }, { 0.25, 25 }, { 0.8, 80 }, { 1.0, 100 } })
                expectWidth (value, pixels);
            slider.setRange (20.0, 220.0);
            slider.setSkewFactor (2.0);
            expectWidth (20.0, 0);
            expectWidth (120.0, 25);
            expectWidth (220.0, 100);
            expect (sink.diagnostics.empty());
            slider.setLookAndFeel (nullptr);
        }

        beginTest ("native slider hit geometry and scrollbar metrics follow their resolved control profiles");
        {
            SkinRenderStyleEntry sliderEntry, ownerEntry, scrollEntry;
            sliderEntry.renderer = simpleProgram (SkinRenderTarget::linearSlider,
                rectangleLayer ("track", { 7, 12, 186, 4 }, juce::Colours::red));
            setControlStyle (sliderEntry.renderer, {
                { "trackBounds.x", 7.0 }, { "trackBounds.y", 12.0 },
                { "trackBounds.width", 186.0 }, { "trackBounds.height", 4.0 }, { "thumbRadius", 3.0 }
            });
            ownerEntry.renderer.target = SkinRenderTarget::channelMixer;
            ownerEntry.slotStyles["row.gain"] = "linearSlider.mixer";
            scrollEntry.renderer.target = SkinRenderTarget::scrollBar;
            setControlStyle (scrollEntry.renderer, { { "thickness", 11.0 }, { "minThumbLength", 29.0 } });
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
            {
                const auto* entry = target == SkinRenderTarget::channelMixer ? &ownerEntry
                    : target == SkinRenderTarget::scrollBar ? &scrollEntry
                    : style == "linearSlider.mixer" ? &sliderEntry : nullptr;
                return JuceSkinAdapter::RuntimeBinding { entry, nullptr, &sink, true };
            }, renderer);
            juce::Component owner;
            juce::Slider slider;
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setBounds (0, 0, 200, 28);
            slider.setRange (0, 1);
            slider.setLookAndFeel (&adapter);
            adapter.applySlotStyle (slider, owner, SkinRenderTarget::channelMixer, "row.gain");
            expectEquals (slider.getProperties()[JuceSkinAdapter::styleProperty].toString(), juce::String ("linearSlider.mixer"));
            expectWithinAbsoluteError (slider.getPositionOfValue (0.0), 7.0f, 0.001f);
            expectWithinAbsoluteError (slider.getPositionOfValue (1.0), 193.0f, 0.001f);
            expectEquals (adapter.getSliderThumbRadius (slider), 3);
            juce::Image painted (juce::Image::ARGB, 200, 28, true);
            { juce::Graphics graphics (painted); slider.paintEntireComponent (graphics, true); }
            expect (painted.getPixelAt (7, 12) == juce::Colours::red);
            expect (painted.getPixelAt (6, 12).isTransparent());
            expectEquals (adapter.getDefaultScrollbarWidth(), 11);
            juce::ScrollBar scrollBar (true);
            expectEquals (adapter.getMinimumScrollbarThumbSize (scrollBar), 29);
            expect (sink.diagnostics.empty());
            slider.setLookAndFeel (nullptr);
        }

        beginTest ("button icon bounds come from the owner control profile");
        {
            SkinRenderStyleEntry buttonEntry, iconEntry;
            buttonEntry.renderer = simpleProgram (SkinRenderTarget::textButton,
                rectangleLayer ("body", { 0, 0, 80, 40 }, juce::Colours::transparentBlack));
            buttonEntry.slotStyles["icon.play"] = "surface.icon";
            setControlStyle (buttonEntry.renderer, {
                { "iconBounds.x", 4.0 }, { "iconBounds.y", 6.0 },
                { "iconBounds.width", 12.0 }, { "iconBounds.height", 14.0 }
            });
            auto icon = rectangleLayer ("icon", { 0, 0, 1, 1 }, juce::Colours::red);
            for (const auto* field : { "x", "y", "width", "height" }) icon.properties.erase ("frame." + juce::String (field));
            icon.properties["frame.anchor"] = literalBinding (icon.primitive, "frame.anchor", juce::String ("bounds"));
            iconEntry.renderer = simpleProgram (SkinRenderTarget::surface, icon);
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding { target == SkinRenderTarget::textButton ? &buttonEntry : &iconEntry,
                                                        nullptr, nullptr, false };
            }, renderer);
            juce::TextButton button;
            button.setSize (80, 40);
            button.getProperties().set (SkinComponentBindings::iconNameProperty, "play");
            juce::Image painted (juce::Image::ARGB, 80, 40, true);
            { juce::Graphics graphics (painted); adapter.drawButtonBackground (graphics, button, {}, false, false); }
            expect (painted.getPixelAt (4, 6) == juce::Colours::red);
            expect (painted.getPixelAt (15, 19) == juce::Colours::red);
            expect (painted.getPixelAt (16, 19).isTransparent());
            expect (painted.getPixelAt (4, 20).isTransparent());
        }

        beginTest ("adapter row geometry inherits the default profile and reports invalid active metrics");
        {
            SkinTokenStore tokens;
            SkinValidationReport report;
            const auto rowHeight = SkinExpression::compile ("bounds.height / table.rowCount",
                skinRowLayoutContextTypes (SkinRenderTarget::table, false), tokens, report, "row", "/height");
            const auto slotWidth = SkinExpression::compile ("bounds.width - 16",
                skinRowLayoutContextTypes (SkinRenderTarget::table, true), tokens, report, "row", "/width");
            expect (rowHeight && slotWidth, report.toText());
            SkinRenderStyleEntry named, fallback;
            named.renderer.target = fallback.renderer.target = SkinRenderTarget::table;
            fallback.renderer.rowLayout.emplace();
            const auto& metric = skinRowLayoutMetricDescriptor ("rowHeight");
            SkinRenderPropertyBinding height { 50.0, metric.fallback, &metric, false, {}, {}, 0, {} };
            if (rowHeight) height.value = *rowHeight;
            fallback.renderer.rowLayout->base.rowHeight = height;
            auto width = literalBinding (SkinPrimitive::rectangle, "frame.width", 100.0);
            if (slotWidth) width.value = *slotWidth;
            fallback.renderer.rowLayout->base.slots["cell.title"] = {
                literalBinding (SkinPrimitive::rectangle, "frame.x", 8.0),
                literalBinding (SkinPrimitive::rectangle, "frame.y", 0.0), width,
                literalBinding (SkinPrimitive::rectangle, "frame.height", 40.0)
            };
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget, const juce::String& style)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    style == "default" ? &fallback : &named, nullptr, &sink, true
                };
            }, renderer);
            juce::Component owner;
            owner.setSize (200, 100);
            owner.getProperties().set (JuceSkinAdapter::styleProperty, "table.custom");
            SkinRenderContext context;
            context.tableRowCount = 2;
            const auto inherited = adapter.rowLayoutFor (owner, SkinRenderTarget::table, context);
            expect (inherited.has_value());
            if (inherited)
            {
                expectWithinAbsoluteError (inherited->rowHeight, 50.0f, 0.001f);
                expectWithinAbsoluteError (inherited->slots.at ("cell.title").getWidth(), 184.0f, 0.001f);
            }
            named.renderer.rowLayout = fallback.renderer.rowLayout;
            named.renderer.rowLayout->base.rowHeight->value = SkinCompiledRenderValue (40.0);
            const auto explicitLayout = adapter.rowLayoutFor (owner, SkinRenderTarget::table, context);
            expect (explicitLayout.has_value());
            if (explicitLayout) expectWithinAbsoluteError (explicitLayout->rowHeight, 40.0f, 0.001f);
            named.renderer.rowLayout->base.rowHeight->value = SkinCompiledRenderValue (-1.0);
            expect (! adapter.rowLayoutFor (owner, SkinRenderTarget::table, context));
            expect (sink.status == SkinRenderStatus::fatalFault);
            expect (std::any_of (sink.diagnostics.begin(), sink.diagnostics.end(), [] (const auto& diagnostic)
            { return diagnostic.code == "RENDER004"; }));
        }

        beginTest ("native ComboBox paint routes placeholders and disabled selection through one skin renderer");
        {
            SkinTokenStore tokens;
            SkinValidationReport report;
            SkinRenderStyleEntry entry;
            entry.renderer = simpleProgram (SkinRenderTarget::comboBox,
                rectangleLayer ("placeholder", { 18.0f, 8.0f, 140.0f, 20.0f }, juce::Colours::green));
            entry.renderer.baseLayers.push_back (
                rectangleLayer ("selected", { 18.0f, 8.0f, 140.0f, 20.0f }, juce::Colours::blue));
            for (size_t index = 0; index < entry.renderer.baseLayers.size(); ++index)
            {
                auto& layer = entry.renderer.baseLayers[index];
                const auto visible = SkinExpression::compile (
                    index == 0 ? "text.primary == 'Choose output'" : "text.primary == 'Stereo'",
                    skinRenderContextForTarget (SkinRenderTarget::comboBox), tokens, report, "combo", "/visible");
                expect (visible.has_value(), report.toText());
                if (visible) layer.properties["visible"].value = *visible;
                entry.renderer.variants[5].properties[layer.id + ".opacity"] =
                    literalBinding (layer.primitive, "opacity", 0.4);
            }
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    target == SkinRenderTarget::comboBox ? &entry : nullptr, nullptr, &sink, true
                };
            }, renderer);
            juce::ComboBox combo;
            combo.setLookAndFeel (&adapter);
            combo.setBounds (0, 0, 180, 36);
            combo.addItem ("Stereo", 1);
            combo.setTextWhenNothingSelected ("Choose output");
            for (const auto enabled : { true, false })
                for (const auto selected : { false, true })
                {
                    combo.setSelectedId (selected ? 1 : 0, juce::dontSendNotification);
                    combo.setEnabled (enabled);
                    juce::Image actual (juce::Image::ARGB, 180, 36, true);
                    {
                        juce::Graphics graphics (actual);
                        combo.paintEntireComponent (graphics, true);
                    }
                    juce::Image expected (juce::Image::ARGB, 180, 36, true);
                    {
                        juce::Graphics graphics (expected);
                        SkinRenderContext context;
                        context.bounds = expected.getBounds().toFloat();
                        context.contentBounds = context.bounds;
                        context.primaryText = selected ? "Stereo" : "Choose output";
                        context.state.enabled = enabled;
                        expect (renderer.draw (graphics, entry.renderer, context, true, sink) == SkinRenderStatus::ok);
                    }
                    expectEquals (pixelHash (actual), pixelHash (expected),
                        "native ComboBox must not add its own placeholder, label, colour or font");
                }
            expect (sink.diagnostics.empty());
            combo.setLookAndFeel (nullptr);
        }

        beginTest ("ComboBox transfers its focus feedback to the popup and restores it on dismissal");
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        {
            expect (false, "native ComboBox regression requires access to a display");
        }
        else
        {
            SkinRenderStyleEntry comboEntry;
            comboEntry.renderer = simpleProgram (SkinRenderTarget::comboBox,
                rectangleLayer ("body", { 0, 0, 180, 36 }, juce::Colours::blue));
            comboEntry.renderer.variants[4].properties["body.fill"] = literalBinding (
                SkinPrimitive::rectangle, "fill", juce::Colours::green);
            SkinRenderStyleEntry menuEntry;
            menuEntry.renderer = simpleProgram (SkinRenderTarget::popupMenu,
                rectangleLayer ("body", { 0, 0, 180, 108 }, juce::Colours::grey, 8));
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    target == SkinRenderTarget::comboBox ? &comboEntry : &menuEntry, nullptr, &sink, true
                };
            }, renderer);
            juce::Component host;
            host.setBounds (100, 100, 300, 220);
            host.setLookAndFeel (&adapter);
            juce::ComboBox combo;
            combo.setBounds (20, 20, 180, 36);
            combo.addItemList ({ "Repeat off", "Repeat all", "Repeat one" }, 1);
            combo.setSelectedId (1, juce::dontSendNotification);
            host.addAndMakeVisible (combo);
            host.addToDesktop (0);
            host.setVisible (true);
            host.toFront (true);
            combo.grabKeyboardFocus();
            const auto paintedColour = [&]
            {
                juce::Image image (juce::Image::ARGB, 180, 36, true);

                {
                    juce::Graphics graphics (image);
                    combo.paintEntireComponent (graphics, true);
                }
                return image.getPixelAt (5, 5);
            };
            expect (combo.hasKeyboardFocus (true));
            expect (paintedColour() == juce::Colours::green);
            combo.showPopup();
            expect (combo.isPopupActive());
            expect (paintedColour() == juce::Colours::blue,
                          "the open menu must own focus feedback at its rounded transparent corners");
            combo.hidePopup();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            combo.grabKeyboardFocus();
            expect (! combo.isPopupActive());
            expect (paintedColour() == juce::Colours::green);
            host.removeFromDesktop();
            host.setLookAndFeel (nullptr);
            expect (sink.diagnostics.empty());
        }

        beginTest ("built-in ComboBox triggers remain visible while menus are open");
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        {
            expect (false, "native built-in ComboBox regression requires access to a display");
        }
        else
        {
            SkinValidationReport report;
            const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
            const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
            const auto defaultPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
            const auto defaultSkin = defaultPackage && foundation
                ? SkinCompiler::compile (*defaultPackage, foundation, report) : nullptr;
            expect (foundation != nullptr && defaultSkin != nullptr, report.toText());
            for (const auto& [skin, suffix] : {
                     std::pair { foundation, juce::String ("default") },
                     std::pair { defaultSkin, juce::String ("studio") },
                     std::pair { defaultSkin, juce::String ("listening") } })
            {
                if (skin == nullptr) continue;
                FaultRecorder sink;
                SkinRenderer renderer;
                JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        skin->resolveStyle (target, skinRenderTargetName (target) + "." + suffix), nullptr, &sink, true
                    };
                }, renderer);
                juce::Component host;
                host.setBounds (100, 100, 320, 260);
                host.setLookAndFeel (&adapter);
                juce::ComboBox combo;
                combo.setBounds (20, 20, 220, 36);
                combo.addItemList ({ "Repeat off", "Repeat all", "Repeat one" }, 1);
                combo.setTextWhenNothingSelected ("No matching effects");
                host.addAndMakeVisible (combo);
                host.addToDesktop (0);
                host.setVisible (true);
                host.toFront (true);
                const auto opaquePixels = [&]
                {
                    const auto image = combo.createComponentSnapshot (combo.getLocalBounds());
                    int count = 0;
                    for (int y = 0; y < image.getHeight(); ++y)
                        for (int x = 0; x < image.getWidth(); ++x)
                            if (! image.getPixelAt (x, y).isTransparent()) ++count;
                    return count;
                };
                for (const auto selected : { 1, 2, 3, 0, -1 })
                {
                    if (selected < 0) combo.clear (juce::dontSendNotification);
                    combo.setSelectedId (juce::jmax (0, selected), juce::dontSendNotification);
                    combo.grabKeyboardFocus();
                    const auto focused = combo.createComponentSnapshot (combo.getLocalBounds());
                    expect (opaquePixels() > 0, suffix + " closed trigger remains visible");
                    combo.showPopup();
                    juce::Component* menu = nullptr;
                    for (int index = 0; index < juce::Desktop::getInstance().getNumComponents(); ++index)
                        if (auto* candidate = juce::Desktop::getInstance().getComponent (index); candidate->getName() == "menu")
                            menu = candidate;
                    expect (menu != nullptr, suffix + " native menu must be created");
                    if (menu != nullptr)
                    {
                        expect (opaquePixels() > 0,
                                suffix + " opening a menu must preserve the trigger even when it is covered");
                        const auto popup = menu->createComponentSnapshot (menu->getLocalBounds());
                        expect (popup.getPixelAt (0, 0).isTransparent(), suffix + " menu corner stays transparent");
                        expect (! popup.getPixelAt (popup.getWidth() / 2, popup.getHeight() / 2).isTransparent());
                    }
                    combo.hidePopup();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                    combo.grabKeyboardFocus();
                    expectEquals (pixelHash (combo.createComponentSnapshot (combo.getLocalBounds())), pixelHash (focused),
                                  suffix + " closing restores the complete focused trigger");
                }
                host.removeFromDesktop();
                host.setLookAndFeel (nullptr);
                expect (sink.diagnostics.empty());
            }
        }

        beginTest ("real popup windows preserve skin transparency and a single themed highlight");
        {
            SkinTokenStore tokens;
            SkinValidationReport report;
            const auto backgroundVisible = SkinExpression::compile ("menu.part == 'background'",
                skinRenderContextForTarget (SkinRenderTarget::popupMenu), tokens, report, "popup", "/visible");
            const auto highlightVisible = SkinExpression::compile ("menu.part == 'item' and state.hover",
                skinRenderContextForTarget (SkinRenderTarget::popupMenu), tokens, report, "popup", "/visible");
            expect (backgroundVisible && highlightVisible, report.toText());
            for (const auto colour : { juce::Colour (0x80273640), juce::Colour (0x80dce9ee) })
            {
                if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
                {
                    expect (false, "native PopupMenu regression requires access to a display");
                    break;
                }
                auto background = rectangleLayer ("body", { 0, 0, 160, 40 }, colour, 8.0);
                for (const auto* property : { "frame.x", "frame.y", "frame.width", "frame.height" })
                    background.properties.erase (property);
                background.properties["frame.anchor"] = literalBinding (
                    background.primitive, "frame.anchor", juce::String ("bounds"));
                for (const auto* side : { "top", "right", "bottom", "left" })
                    background.properties["frame.inset." + juce::String (side)] = literalBinding (
                        background.primitive, "frame.inset." + juce::String (side), 0.5);
                SkinRenderStyleEntry tooltipEntry;
                tooltipEntry.renderer = simpleProgram (SkinRenderTarget::tooltip, background);
                if (backgroundVisible) background.properties["visible"].value = *backgroundVisible;
                auto highlight = background;
                highlight.id = "highlight";
                highlight.properties["fill"] = literalBinding (highlight.primitive, "fill", juce::Colours::transparentBlack);
                highlight.properties["stroke.color"] = literalBinding (highlight.primitive, "stroke.color", juce::Colours::cyan);
                highlight.properties["stroke.width"] = literalBinding (highlight.primitive, "stroke.width", 1.0);
                for (const auto* side : { "top", "right", "bottom", "left" })
                    highlight.properties["frame.inset." + juce::String (side)] = literalBinding (
                        highlight.primitive, "frame.inset." + juce::String (side), 2.0);
                if (highlightVisible) highlight.properties["visible"].value = *highlightVisible;
                SkinRenderStyleEntry entry;
                entry.renderer = simpleProgram (SkinRenderTarget::popupMenu, background);
                entry.renderer.baseLayers.push_back (highlight);
                FaultRecorder sink;
                SkinRenderer renderer;
                JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        target == SkinRenderTarget::popupMenu ? &entry : &tooltipEntry, nullptr, &sink, true
                    };
                }, renderer);
                juce::Component host;
                host.setBounds (0, 0, 400, 260);
                host.setVisible (true);
                host.setLookAndFeel (&adapter);
                juce::PopupMenu menu;
                menu.setLookAndFeel (&adapter);
                menu.addItem (1, "Track");
                menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (&host)
                    .withTargetScreenArea ({ 20, 20, 1, 1 }).withMinimumWidth (160)
                    .withStandardItemHeight (36).withInitiallySelectedItem (1));
                juce::Component* window = nullptr;
                for (auto* child : host.getChildren())
                    if (child->getName() == "menu") window = child;
                expect (window != nullptr, "the native PopupMenu component must be created");
                if (window != nullptr)
                {
                    expect (! window->isOpaque());
                    juce::Image actual (juce::Image::ARGB, window->getWidth(), window->getHeight(), true);
                    {
                        juce::Graphics graphics (actual);
                        window->paintEntireComponent (graphics, true);
                    }
                    expect (actual.getPixelAt (0, 0).isTransparent());
                    expectEquals (static_cast<int> (actual.getPixelAt (actual.getWidth() / 2,
                                                                    actual.getHeight() / 2).getAlpha()), 128,
                                  "the translucent background must be composited only once");
                    juce::Image expected (juce::Image::ARGB, actual.getWidth(), actual.getHeight(), true);
                    {
                        juce::Graphics graphics (expected);
                        SkinRenderContext context;
                        context.bounds = expected.getBounds().toFloat();
                        context.contentBounds = context.bounds;
                        context.menuPart = "background";
                        renderer.draw (graphics, entry.renderer, context, true, sink);
                        expectEquals (window->getNumChildComponents(), 1);
                        if (auto* item = window->getChildComponent (0))
                        {
                            graphics.setOrigin (item->getPosition());
                            context.bounds = item->getLocalBounds().toFloat();
                            context.contentBounds = context.bounds;
                            context.menuPart = "item";
                            context.state.hover = true;
                            renderer.draw (graphics, entry.renderer, context, true, sink);
                        }
                    }
                    expectEquals (pixelHash (actual), pixelHash (expected),
                                  "a real menu must contain only the skin background and its single highlight");
                }
                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                {
                    juce::TooltipWindow tooltip (&host, 500);
                    tooltip.setOpaque (false);
                    tooltip.setLookAndFeel (&adapter);
                    tooltip.displayTip (host.localPointToGlobal (juce::Point<int> (20, 20)), "Track");
                    juce::Image image (juce::Image::ARGB, tooltip.getWidth(), tooltip.getHeight(), true);
                    {
                        juce::Graphics graphics (image);
                        tooltip.paintEntireComponent (graphics, true);
                    }
                    expect (image.getPixelAt (0, 0).isTransparent(), "TooltipWindow must not add the popup's white pre-fill");
                    expectEquals (static_cast<int> (image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2).getAlpha()), 128);
                    tooltip.hideTip();
                    tooltip.setLookAndFeel (nullptr);
                }
                expect (sink.diagnostics.empty());
                host.setLookAndFeel (nullptr);
            }
        }

        beginTest ("native editable text inherits colour and font from its skin renderer");
        {
            SkinRenderStyleEntry entry;
            entry.renderer.target = SkinRenderTarget::textEditor;
            SkinRenderLayer label;
            label.id = "label";
            label.primitive = SkinPrimitive::text;
            SkinFontValue fontValue;
            fontValue.families.add ("sans-serif");
            fontValue.weight = 500;
            const auto expectedColour = juce::Colour (0xff28323c);
            label.properties["color"] = literalBinding (
                SkinPrimitive::text, "color", expectedColour);
            label.properties["font"] = literalBinding (
                SkinPrimitive::text, "font", fontValue);
            label.properties["fontSize"] = literalBinding (
                SkinPrimitive::text, "fontSize", 14.0);
            entry.renderer.baseLayers.push_back (std::move (label));

            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        target == SkinRenderTarget::textEditor ? &entry : nullptr,
                        nullptr, nullptr, false
                    };
                }, renderer);

            juce::TextEditor editor;
            editor.setTextToShowWhenEmpty ("Search", juce::Colours::white);
            adapter.configureTextEditor (editor);
            expect (editor.findColour (juce::TextEditor::textColourId)
                    == expectedColour);
            expectWithinAbsoluteError (
                static_cast<double> (editor.getFont().getHeight()), 14.0, 0.01);

            juce::ComboBox combo;
            juce::Label comboLabel;
            adapter.positionComboBoxText (combo, comboLabel);
            expect (combo.findColour (juce::ComboBox::textColourId).isTransparent());
            expect (comboLabel.findColour (
                        juce::Label::textColourId).isTransparent());
            expectWithinAbsoluteError (
                static_cast<double> (comboLabel.getAlpha()), 0.0, 0.001);
        }

        beginTest ("native editor text, prompt, caret and selection follow the same skin label geometry");
        {
            SkinRenderLayer label;
            label.id = "label";
            label.primitive = SkinPrimitive::text;
            setCommon (label);
            const auto set = [&] (const juce::String& key, SkinCompiledRenderValue value)
            { label.properties[key] = literalBinding (label.primitive, key, std::move (value)); };
            SkinFontValue font;
            font.families = { "sans-serif" };
            set ("font", font);
            set ("fontSize", 18.0);
            set ("color", juce::Colour (0xff468ace));
            set ("frame.anchor", juce::String ("bounds"));
            set ("frame.inset.left", 11.0);
            set ("frame.inset.right", 13.0);
            set ("frame.inset.top", 5.0);
            set ("frame.inset.bottom", 7.0);
            set ("align", juce::String ("left"));
            set ("maxLines", 1.0);
            set ("overflow", juce::String ("clip"));
            SkinValidationReport report;
            SkinTokenStore tokens;
            const auto text = SkinExpression::compile ("text.primary",
                skinRenderContextForTarget (SkinRenderTarget::textEditor), tokens, report, "editor", "/text");
            expect (text.has_value(), report.toText());
            set ("text", juce::String());
            if (text) label.properties["text"].value = *text;
            SkinRenderStyleEntry entry;
            entry.renderer = simpleProgram (SkinRenderTarget::textEditor, label);
            setControlStyle (entry.renderer, {
                { "selectionColour", juce::Colour (0x40468ace) },
                { "selectedTextColour", juce::Colour (0xff468ace) },
                { "caretColour", juce::Colour (0xff468ace) }
            });
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding { target == SkinRenderTarget::textEditor ? &entry : nullptr,
                                                        nullptr, &sink, true };
            }, renderer);
            juce::TextEditor editor;
            editor.setLookAndFeel (&adapter);
            editor.setTextToShowWhenEmpty ("Agjp", juce::Colours::white);
            const auto paintEditor = [&]
            {
                juce::Image image (juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
                juce::Graphics graphics (image);
                editor.paintEntireComponent (graphics, true);
                return image;
            };
            const auto inkBounds = [] (const juce::Image& image)
            {
                juce::Rectangle<int> bounds;
                for (int y = 0; y < image.getHeight(); ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (image.getPixelAt (x, y).getAlpha() > 32)
                            bounds = bounds.getUnion ({ x, y, 1, 1 });
                return bounds;
            };
            for (const auto size : { juce::Point<int> (240, 48), juce::Point<int> (310, 64) })
            {
                editor.setSize (size.x, size.y);
                editor.setText ({}, false);
                const auto prompt = paintEditor();
                juce::Image expected (juce::Image::ARGB, size.x, size.y, true);
                {
                    juce::Graphics graphics (expected);
                    SkinRenderContext context;
                    context.bounds = expected.getBounds().toFloat();
                    context.contentBounds = context.bounds;
                    context.primaryText = "Agjp";
                    renderer.draw (graphics, entry.renderer, context, true, sink);
                }
                expectEquals (pixelHash (prompt), pixelHash (expected), "placeholder must be painted exactly once by the label renderer");
                editor.setText ("Agjp", false);
                const auto typed = paintEditor();
                const auto promptBounds = inkBounds (prompt);
                const auto typedBounds = inkBounds (typed);
                expect (! typedBounds.isEmpty());
                expectWithinAbsoluteError (typedBounds.getX(), promptBounds.getX(), 1);
                expectWithinAbsoluteError (typedBounds.getY(), promptBounds.getY(), 1);
                expectWithinAbsoluteError (typedBounds.getWidth(), promptBounds.getWidth(), 1);
                expectWithinAbsoluteError (typedBounds.getHeight(), promptBounds.getHeight(), 1);
                expect (editor.getBorder() == juce::BorderSize<int> (5, 11, 7, 13));
                expect (editor.getJustificationType() == juce::Justification::centredLeft);
                const auto caret = editor.getCaretRectangleForCharIndex (0);
                const auto selection = editor.getTextBounds ({ 0, 4 }).getBounds();
                expectEquals (caret.getX(), 11);
                expect (selection.contains (typedBounds));
                expectWithinAbsoluteError (caret.getCentreY(), (size.y + 5 - 7) / 2, 2);
                expect (editor.findColour (juce::CaretComponent::caretColourId) == juce::Colour (0xff468ace));
            }
            auto& changed = entry.renderer.baseLayers.front();
            changed.properties["fontSize"] = literalBinding (label.primitive, "fontSize", 22.0);
            changed.properties["color"] = literalBinding (label.primitive, "color", juce::Colours::green);
            changed.properties["align"] = literalBinding (label.primitive, "align", juce::String ("right"));
            changed.properties["frame.inset.right"] = literalBinding (label.primitive, "frame.inset.right", 21.0);
            entry.renderer.controlStyle->properties.at ("selectedTextColour").value =
                SkinCompiledRenderValue (juce::Colours::red);
            static_cast<void> (paintEditor());
            expectWithinAbsoluteError (editor.getFont().getHeight(), 22.0f, 0.001f);
            expect (editor.getJustificationType() == juce::Justification::centredRight);
            expectEquals (editor.getBorder().getRight(), 21);
            expect (editor.findColour (juce::TextEditor::textColourId) == juce::Colours::green);
            expect (editor.findColour (juce::TextEditor::highlightedTextColourId) == juce::Colours::red);
            expect (sink.diagnostics.empty());
            editor.setLookAndFeel (nullptr);
        }

        beginTest ("popup and tooltip measurements reserve the resolved label font and insets");
        {
            const auto makeEntry = [] (SkinRenderTarget target, double size)
            {
                SkinRenderLayer label;
                label.id = "label";
                label.primitive = SkinPrimitive::text;
                setCommon (label);
                SkinFontValue font;
                font.families = { "sans-serif" };
                label.properties["font"] = literalBinding (label.primitive, "font", font);
                label.properties["fontSize"] = literalBinding (label.primitive, "fontSize", size);
                label.properties["color"] = literalBinding (label.primitive, "color", juce::Colours::white);
                label.properties["frame.anchor"] = literalBinding (label.primitive, "frame.anchor", juce::String ("bounds"));
                for (const auto* side : { "left", "right", "top", "bottom" })
                    label.properties["frame.inset." + juce::String (side)] = literalBinding (
                        label.primitive, "frame.inset." + juce::String (side), 7.0);
                label.properties["maxLines"] = literalBinding (label.primitive, "maxLines", 3.0);
                SkinRenderStyleEntry entry;
                entry.renderer = simpleProgram (target, std::move (label));
                return entry;
            };
            auto popup = makeEntry (SkinRenderTarget::popupMenu, 28.0);
            auto tooltip = makeEntry (SkinRenderTarget::tooltip, 15.0);
            popup.renderer.rowLayout.emplace();
            const auto& rowHeightDescriptor = skinRowLayoutMetricDescriptor ("rowHeight");
            popup.renderer.rowLayout->base.rowHeight = SkinRenderPropertyBinding {
                66.0, rowHeightDescriptor.fallback, &rowHeightDescriptor, false, {}, {}, 0, {} };
            setControlStyle (tooltip.renderer, {
                { "maxWidth", 400.0 }, { "offsetX", 24.0 }, { "offsetY", 6.0 }
            });
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    target == SkinRenderTarget::popupMenu ? &popup : &tooltip, nullptr, nullptr, true
                };
            }, renderer);
            const juce::String text ("Choose output device");
            int width = 0, height = 0;
            adapter.getIdealPopupMenuItemSize (text, false, 18, width, height);
            const auto font = adapter.getPopupMenuFont();
            expectWithinAbsoluteError (font.getHeight(), 28.0f, 0.001f);
            expect (width >= juce::GlyphArrangement::getStringWidthInt (font, text) + 14);
            expect (height >= 66);
            adapter.getIdealPopupMenuItemSize (text, false, 80, width, height);
            expect (height >= 80);
            const juce::Rectangle<int> parent (0, 0, 800, 600);
            const auto tip = adapter.getTooltipBounds (text, { 40, 40 }, parent);
            expect (parent.contains (tip));
            expectEquals (tip.getX(), 64);
            expectEquals (tip.getY(), 46);
            expect (tip.getHeight() >= 29, "tooltip must reserve both seven-pixel vertical insets");
            const auto wrapped = adapter.getTooltipBounds ("A longer tooltip with several words that wraps over multiple lines",
                                                          { 40, 40 }, { 0, 0, 180, 600 });
            expect (wrapped.getHeight() > tip.getHeight());
            expect (wrapped.getHeight() <= 59);
        }

        beginTest ("composite slot renderers consume owner-specific contexts");
        {
            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            entries[SkinRenderTarget::pluginList].renderer = simpleProgram (
                SkinRenderTarget::pluginList,
                rectangleLayer ("owner", { 0.0f, 0.0f, 80.0f, 24.0f },
                                juce::Colours::transparentBlack));
            entries[SkinRenderTarget::pluginList].slotStyles = {
                { "row.surface", "surface.pluginRow" }
            };
            entries[SkinRenderTarget::surface].renderer = simpleProgram (
                SkinRenderTarget::surface,
                rectangleLayer ("row", { 0.0f, 0.0f, 80.0f, 24.0f },
                                juce::Colours::white));

            SkinValidationReport report;
            SkinTokenStore tokens;
            const auto visible = SkinExpression::compile (
                "plugin.name == 'Compressor'",
                skinRenderContextForCompositeSlot (
                    SkinRenderTarget::pluginList, "row.surface",
                    SkinRenderTarget::surface),
                tokens, report, "slot-context", "/visible");
            expect (visible.has_value(), report.toText());
            if (visible)
            {
                auto binding = literalBinding (
                    SkinPrimitive::rectangle, "visible", false);
                binding.value = *visible;
                entries[SkinRenderTarget::surface].renderer.baseLayers.front()
                    .properties["visible"] = std::move (binding);
            }

            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (target), nullptr, &sink, true
                    };
                }, renderer);
            juce::Component owner;
            owner.setBounds (0, 0, 80, 24);
            juce::Image image (juce::Image::ARGB, 80, 24, true);

            const auto drawNamedPlugin = [&] (const juce::String& name)
            {
                image.clear (image.getBounds(), juce::Colours::transparentBlack);
                SkinRenderContext context;
                context.bounds = image.getBounds().toFloat();
                context.contentBounds = context.bounds;
                context.extraLookup = [name] (std::string_view path)
                    -> std::optional<SkinExpressionValue>
                {
                    if (path == "plugin.name") return SkinExpressionValue (name);
                    return std::nullopt;
                };
                {
                    juce::Graphics graphics (image);
                    expect (adapter.drawCustomSlot (
                        graphics, owner, SkinRenderTarget::pluginList,
                        "row.surface", SkinRenderTarget::surface,
                        std::move (context), "plugin:0"));
                }
            };
            drawNamedPlugin ("Limiter");
            expect (image.getPixelAt (10, 10).isTransparent());
            drawNamedPlugin ("Compressor");
            expect (image.getPixelAt (10, 10).getAlpha() > 0);
            expect (! report.hasErrors(), report.toText());
        }

        beginTest ("table row slots preserve the frozen library metadata contract");
        {
            const auto context = skinRenderContextForCompositeSlot (
                SkinRenderTarget::table, "row.surface",
                SkinRenderTarget::surface);
            expect (context.contains ("row.location"));
            expect (context.contains ("row.artist"));
            expect (context.contains ("row.album"));
            for (const auto* slot : { "row.surface", "cell.title", "cell.location", "cell.duration" })
            {
                const auto fields = skinRenderContextForCompositeSlot (
                    SkinRenderTarget::table, slot,
                    juce::String (slot) == "row.surface" ? SkinRenderTarget::surface : SkinRenderTarget::label);
                expect (fields.contains ("row.playing") && fields.at ("row.playing") == SkinExpressionType::boolean);
                expect (fields.contains ("row.animating") && fields.at ("row.animating") == SkinExpressionType::boolean);
                expect (fields.contains ("row.playbackSeconds") && fields.at ("row.playbackSeconds") == SkinExpressionType::number);
                expect (fields.contains ("row.favourite") && fields.at ("row.favourite") == SkinExpressionType::boolean);
                expect (fields.contains ("row.dropEdge") && fields.at ("row.dropEdge") == SkinExpressionType::number);
            }

            SkinValidationReport report;
            SkinTokenStore tokens;
            auto locationLayer = rectangleLayer (
                "location-contract", { 0.0f, 0.0f, 80.0f, 24.0f },
                juce::Colours::white);
            for (const auto* property : {
                     "frame.x", "frame.y", "frame.width", "frame.height" })
                locationLayer.properties.erase (property);
            locationLayer.properties["frame.anchor"] = literalBinding (
                SkinPrimitive::rectangle, "frame.anchor", juce::String ("bounds"));
            const auto visible = SkinExpression::compile (
                "column.id == 'location' and "
                "text.primary == '/Music/Album/Track.flac' and "
                "row.location == text.primary and "
                "row.artist == 'Artist' and row.album == 'Album' and "
                "not row.selected and row.playing and row.favourite and row.dropEdge == -1",
                skinRenderContextForCompositeSlot (
                    SkinRenderTarget::table, "cell.location",
                    SkinRenderTarget::label),
                tokens, report, "table-location-contract", "/visible");
            expect (visible.has_value(), report.toText());
            if (visible)
            {
                auto binding = literalBinding (
                    SkinPrimitive::rectangle, "visible", false);
                binding.value = *visible;
                locationLayer.properties["visible"] = std::move (binding);
            }

            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            entries[SkinRenderTarget::label].renderer = simpleProgram (
                SkinRenderTarget::label, std::move (locationLayer));
            entries[SkinRenderTarget::surface].renderer = simpleProgram (
                SkinRenderTarget::surface,
                rectangleLayer ("transparent", { 0.0f, 0.0f, 80.0f, 24.0f },
                                juce::Colours::transparentBlack));
            entries[SkinRenderTarget::table].renderer = simpleProgram (
                SkinRenderTarget::table,
                rectangleLayer ("table", { 0.0f, 0.0f, 80.0f, 24.0f },
                                juce::Colours::transparentBlack));
            entries[SkinRenderTarget::table].slotStyles = {
                { "row.surface", "surface.default" },
                { "cell.title", "label.default" },
                { "cell.location", "label.default" },
                { "cell.duration", "label.default" }
            };

            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (target), nullptr, &sink, true
                    };
                }, renderer);
            juce::ListBox list;
            juce::Image image (juce::Image::ARGB, 80, 24, true);

            const JuceSkinAdapter::ListBoxRowGeometry geometry {
                { 0.0f, 0.0f, 24.0f, 24.0f },
                { 24.0f, 0.0f, 40.0f, 24.0f },
                { 64.0f, 0.0f, 16.0f, 24.0f }
            };
            {
                juce::Graphics graphics (image);
                adapter.drawListBoxRow (
                    graphics, list, 0, 80, 24, false, "Track",
                    "/Music/Album/Track.flac", "3:12", geometry,
                    false, false, true, "Artist", "Album", true, true, -1);
            }
            expect (image.getPixelAt (40, 12).getAlpha() > 0,
                    "cell.location must receive the absolute path in text.primary "
                    "while row.artist and row.album remain additive context");
            for (const auto& state : std::vector<std::tuple<bool, bool, int>> {
                     { false, true, -1 }, { true, false, -1 }, { true, true, 0 }, { true, true, 1 } })
            {
                image.clear (image.getBounds(), juce::Colours::transparentBlack);
                {
                    juce::Graphics graphics (image);
                    adapter.drawListBoxRow (
                        graphics, list, 0, 80, 24, false, "Track",
                        "/Music/Album/Track.flac", "3:12", geometry,
                        false, false, true, "Artist", "Album",
                        std::get<0> (state), std::get<1> (state), std::get<2> (state));
                }
                expect (image.getPixelAt (40, 12).isTransparent(),
                        "playing, favourite and insertion edge must reach row slots independently");
            }
            expect (! report.hasErrors(), report.toText());
        }


        beginTest ("playback row animation follows transport state and Reduce Motion");
        {
            SkinValidationReport report;
            SkinTokenStore tokens;
            auto indicator = rectangleLayer ("indicator", { 0.0f, 0.0f, 40.0f, 24.0f }, juce::Colours::white);
            const auto visible = SkinExpression::compile (
                "row.animating and row.playbackSeconds == 1.25",
                skinRenderContextForCompositeSlot (SkinRenderTarget::table, "row.surface", SkinRenderTarget::surface),
                tokens, report, "playback-indicator", "/visible");
            expect (visible.has_value(), report.toText());
            if (visible)
            {
                auto binding = literalBinding (SkinPrimitive::rectangle, "visible", false);
                binding.value = *visible;
                indicator.properties["visible"] = std::move (binding);
            }
            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            entries[SkinRenderTarget::surface].renderer = simpleProgram (SkinRenderTarget::surface, std::move (indicator));
            entries[SkinRenderTarget::label].renderer = simpleProgram (SkinRenderTarget::label,
                rectangleLayer ("empty", { 0.0f, 0.0f, 40.0f, 24.0f }, juce::Colours::transparentBlack));
            entries[SkinRenderTarget::table].slotStyles = {
                { "row.surface", "surface.default" }, { "cell.title", "label.default" },
                { "cell.location", "label.default" }, { "cell.duration", "label.default" }
            };
            SkinAnimationEngine animations;
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            { return JuceSkinAdapter::RuntimeBinding { &entries.at (target), &animations, &sink, true }; }, renderer);
            juce::ListBox list;
            for (const auto& [current, active, reduced] : std::vector<std::tuple<bool, bool, bool>> {
                    { true, true, false }, { true, false, false }, { true, true, true }, { false, true, false } })
            {
                animations.setReduceMotionEnabled (reduced);
                juce::Image image (juce::Image::ARGB, 40, 24, true);
                {
                    juce::Graphics graphics (image);
                    adapter.drawListBoxRow (graphics, list, 0, 40, 24, false, "Track", {}, {}, std::nullopt,
                        false, false, true, {}, {}, current, false, 0, active, 1.25);
                }
                expect (image.getPixelAt (2, 12).isOpaque() == (current && active && ! reduced));
            }
            expect (! report.hasErrors(), report.toText());
            expect (sink.diagnostics.empty());
        }

        beginTest ("playback and favourite indicators follow a reordered title column");
        {
            SkinValidationReport report;
            const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
            const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
            const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
            const auto skin = package && foundation ? SkinCompiler::compile (*package, foundation, report) : nullptr;
            expect (skin != nullptr, report.toText());
            if (skin)
                for (const auto& theme : { juce::String ("studio"), juce::String ("listening") })
                {
                    SkinAnimationEngine animations;
                    FaultRecorder sink;
                    SkinRenderer renderer;
                    JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
                    { return JuceSkinAdapter::RuntimeBinding { skin->resolveStyle (target, style), &animations, &sink, true }; }, renderer);
                    SkinTableListBox table;
                    table.setBounds (0, 0, 800, 120);
                    table.getHeader().addColumn ("Track", 1, 220);
                    table.getHeader().addColumn ("Artist", 2, 220);
                    table.getHeader().addColumn ("Time", 3, 80);
                    table.getHeader().addColumn ("Album", 4, 220);
                    table.getProperties().set (JuceSkinAdapter::styleProperty, "table." + theme + "Collection");
                    table.setLookAndFeel (&adapter);
                    adapter.configureListBox (table);
                    table.setPresentation ("table");
                    table.getHeader().moveColumn (2, 0);
                    const auto geometry = table.rowGeometry (40);
                    expect (geometry.title.getX() > 0.0f);
                    juce::Image image (juce::Image::ARGB, 800, 40, true);
                    {
                        juce::Graphics graphics (image);
                        adapter.drawListBoxRow (graphics, table, 0, 800, 40, false, "Title", {}, "3:10", geometry,
                            false, false, true, "Artist", "Album", true, true, 0, true, 0.125);
                    }
                    const auto* accent = skin->tokens.find<juce::Colour> ("colors." + theme + ".active");
                    expect (accent != nullptr);
                    if (accent)
                    {
                        const auto pixels = [&] (int start, int width)
                        {
                            int count = 0;
                            for (int y = 0; y < image.getHeight(); ++y)
                                for (int x = start; x < start + width; ++x)
                                    if (image.getPixelAt (x, y) == *accent) ++count;
                            return count;
                        };
                        expectEquals (pixels (12, 10), 0, "Artist column keeps its text space.");
                        expect (pixels (juce::roundToInt (geometry.title.getX()) + 12, 10) > 0,
                                "Playback marker stays with the title.");
                        expect (pixels (juce::roundToInt (geometry.title.getRight()) - 16, 8) > 0,
                                "Favourite marker stays with the title.");
                    }
                    expect (sink.diagnostics.empty());
                    table.setLookAndFeel (nullptr);
                }
        }

        beginTest ("custom artwork and output components populate their frozen context");
        {
            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            SkinValidationReport report;
            SkinTokenStore tokens;
            const auto makeConditionalProgram = [&] (
                SkinRenderTarget target, const juce::String& expression)
            {
                auto program = simpleProgram (
                    target, rectangleLayer ("body", { 0.0f, 0.0f, 200.0f, 120.0f },
                                            juce::Colours::white));
                auto& layer = program.baseLayers.front();
                for (const auto* property : {
                         "frame.x", "frame.y", "frame.width", "frame.height" })
                    layer.properties.erase (property);
                layer.properties["frame.anchor"] = literalBinding (
                    SkinPrimitive::rectangle, "frame.anchor",
                    juce::String ("bounds"));
                const auto compiled = SkinExpression::compile (
                    expression, skinRenderContextForTarget (target), tokens,
                    report, "component-context", "/visible");
                expect (compiled.has_value(), report.toText());
                if (compiled)
                {
                    auto visible = literalBinding (
                        SkinPrimitive::rectangle, "visible", false);
                    visible.value = *compiled;
                    layer.properties["visible"] = std::move (visible);
                }
                return program;
            };

            entries[SkinRenderTarget::artwork].renderer = makeConditionalProgram (
                SkinRenderTarget::artwork,
                "artwork.hasTrack and artwork.hasImage and artwork.aspectRatio == 1");
            const auto channels = toAudioChannelSet (OutputLayout::fiveOneTwo)
                                      .getChannelTypes();
            const auto lfeIndex = channels.indexOf (juce::AudioChannelSet::LFE);
            entries[SkinRenderTarget::outputStage].renderer =
                makeConditionalProgram (
                    SkinRenderTarget::outputStage,
                    "output.layout == '5.1.2' and meter.channelCount == 8 and "
                    "meter.channels[" + juce::String (lfeIndex) + "].isLfe");

            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (target), nullptr, &sink, true
                    };
                }, renderer);

            juce::Image image (juce::Image::ARGB, 200, 120, true);
            const auto artworkFile = juce::File::getSpecialLocation (
                juce::File::currentExecutableFile).getParentDirectory()
                    .getNonexistentChildFile (
                    "moonwake-artwork-context", ".png", true);
            juce::Image squareArtwork (juce::Image::ARGB, 32, 32, true);
            juce::MemoryOutputStream artworkBytes;
            juce::PNGImageFormat png;
            expect (png.writeImageToStream (squareArtwork, artworkBytes));
            expect (artworkFile.replaceWithData (
                artworkBytes.getData(), artworkBytes.getDataSize()));
            AlbumArtworkComponent artwork;
            artwork.setBounds (0, 0, 200, 120);
            artwork.setLookAndFeel (&adapter);
            artwork.setTrack ("track-id", "Track", artworkFile);
            {
                juce::Graphics graphics (image);
                artwork.paint (graphics);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0,
                    "artwork.hasTrack, hasImage and aspectRatio were not populated");

            image.clear (image.getBounds(), juce::Colours::transparentBlack);
            OutputStageComponent output;
            output.setBounds (0, 0, 200, 120);
            output.setLookAndFeel (&adapter);
            std::array<float, maximumOutputChannels> peaks {};
            output.setState (OutputLayout::fiveOneTwo, WorkspaceMode::studio,
                             true, "Track", "Device", 8, peaks);
            {
                juce::Graphics graphics (image);
                output.paint (graphics);
            }
            expect (image.getPixelAt (10, 10).getAlpha() > 0,
                    "output layout, channel count or LFE flags were not populated");
            expect (! report.hasErrors(), report.toText());
            artwork.setLookAndFeel (nullptr);
            output.setLookAndFeel (nullptr);
            expect (artworkFile.deleteFile());
        }

        beginTest ("artwork image geometry and ambience use active renderer variants");
        {
            auto clip = rectangleLayer ("image-clip", { 20.0f, 20.0f, 60.0f, 60.0f },
                                        juce::Colours::transparentBlack, 8.0);
            clip.properties["clip"] = literalBinding (clip.primitive, "clip", true);
            auto ambience = rectangleLayer ("image-ambience", { 0.0f, 0.0f, 100.0f, 100.0f },
                                            juce::Colours::transparentBlack, 20.0);
            SkinRenderStyleEntry entry;
            entry.renderer = simpleProgram (SkinRenderTarget::artwork, clip);
            entry.renderer.baseLayers.push_back (ambience);
            auto& selected = entry.renderer.variants[0].properties;
            selected["image-ambience.opacity"] = literalBinding (ambience.primitive, "opacity", 0.6);
            selected["image-clip.radius"] = literalBinding (clip.primitive, "radius", 15.0);
            auto& disabled = entry.renderer.variants[5].properties;
            disabled["image-ambience.opacity"] = literalBinding (ambience.primitive, "opacity", 0.15);
            disabled["image-ambience.visible"] = literalBinding (ambience.primitive, "visible", false);
            disabled["image-clip.frame.width"] = literalBinding (clip.primitive, "frame.width", 40.0);
            disabled["image-clip.radius"] = literalBinding (clip.primitive, "radius", 6.0);
            disabled["image-clip.shadow.radius"] = literalBinding (clip.primitive, "shadow.radius", 12.0);
            SkinTokenStore tokens;
            SkinValidationReport report;
            const auto width = SkinExpression::compile ("bounds.width - 12",
                skinRenderContextForTarget (SkinRenderTarget::artwork), tokens, report, "ambience", "/frame/width");
            expect (width.has_value(), report.toText());
            if (width) entry.renderer.baseLayers[1].properties["frame.width"].value = *width;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget,
                                          const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding { &entry, nullptr, nullptr, true };
            }, renderer);
            juce::Component component;
            component.setSize (100, 100);
            SkinRenderContext context;
            context.state.selected = true;
            const auto selectedStyle = adapter.artworkAmbienceStyleFor (component, context);
            expect (selectedStyle.has_value());
            if (selectedStyle)
            {
                expectWithinAbsoluteError (selectedStyle->bounds.getWidth(), 88.0f, 0.001f);
                expectWithinAbsoluteError (selectedStyle->opacity, 0.6f, 0.001f);
                expect (selectedStyle->visible);
            }
            component.setEnabled (false);
            const auto disabledStyle = adapter.artworkAmbienceStyleFor (component, context);
            const auto disabledClip = adapter.artworkImageStyleFor (component, context);
            expect (disabledStyle.has_value() && disabledClip.has_value());
            if (disabledStyle && disabledClip)
            {
                expectWithinAbsoluteError (disabledStyle->opacity, 0.15f, 0.001f);
                expect (! disabledStyle->visible);
                expectWithinAbsoluteError (disabledClip->bounds.getWidth(), 40.0f, 0.001f);
                expectWithinAbsoluteError (disabledClip->cornerRadius, 6.0f, 0.001f);
                expect (disabledClip->clip);
            }
            disabled["image-ambience.opacity"] = literalBinding (ambience.primitive, "opacity", 2.0);
            expectWithinAbsoluteError (adapter.artworkAmbienceStyleFor (component, context)->opacity,
                                       1.0f, 0.001f);
            disabled["image-ambience.opacity"] = literalBinding (ambience.primitive, "opacity", -0.5);
            expectWithinAbsoluteError (adapter.artworkAmbienceStyleFor (component, context)->opacity,
                                       0.0f, 0.001f);
            entry.renderer.baseLayers.pop_back();
            expect (! adapter.artworkAmbienceStyleFor (component, context));
        }

        beginTest ("artwork ambience is blurred, clipped, cached and optional");
        {
            auto ambience = rectangleLayer ("image-ambience", { 10.0f, 5.0f, 100.0f, 70.0f },
                                            juce::Colours::transparentBlack, 20.0);
            ambience.properties["opacity"] = literalBinding (ambience.primitive, "opacity", 0.5);
            auto clip = rectangleLayer ("image-clip", { 40.0f, 20.0f, 40.0f, 40.0f },
                                        juce::Colours::transparentBlack, 6.0);
            clip.properties["clip"] = literalBinding (clip.primitive, "clip", true);
            auto fallback = rectangleLayer ("empty", { 0.0f, 0.0f, 120.0f, 80.0f },
                                            juce::Colours::darkgrey);
            SkinTokenStore tokens;
            SkinValidationReport report;
            const auto visible = SkinExpression::compile ("not artwork.hasImage",
                skinRenderContextForTarget (SkinRenderTarget::artwork), tokens, report, "empty", "/visible");
            expect (visible.has_value(), report.toText());
            if (visible) fallback.properties["visible"].value = *visible;
            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            auto& entry = entries[SkinRenderTarget::artwork];
            entry.renderer = simpleProgram (SkinRenderTarget::artwork, std::move (fallback));
            entry.renderer.baseLayers.push_back (ambience);
            auto shadow = clip;
            shadow.id = "cover-shadow";
            shadow.properties["clip"] = literalBinding (shadow.primitive, "clip", false);
            entry.renderer.baseLayers.push_back (shadow);
            entry.renderer.baseLayers.push_back (clip);
            entry.renderer.variants[5].properties["image-ambience.opacity"] =
                literalBinding (ambience.primitive, "opacity", 0.125);
            entry.renderer.variants[5].properties["cover-shadow.shadow.color"] =
                literalBinding (clip.primitive, "shadow.color", juce::Colours::black);
            entry.renderer.variants[5].properties["cover-shadow.shadow.radius"] =
                literalBinding (clip.primitive, "shadow.radius", 4.0);
            entry.renderer.variants[5].properties["cover-shadow.shadow.offsetY"] =
                literalBinding (clip.primitive, "shadow.offsetY", 12.0);
            entries[SkinRenderTarget::surface].renderer = simpleProgram (SkinRenderTarget::surface,
                rectangleLayer ("transparent", { 0.0f, 0.0f, 120.0f, 80.0f }, juce::Colours::transparentBlack));
            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target,
                                          const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding { &entries.at (target), nullptr, &sink, true };
            }, renderer);
            const auto artworkFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory().getNonexistentChildFile ("moonwake-artwork-ambience", ".png", true);
            juce::Image cover (juce::Image::ARGB, 64, 64, true);
            cover.clear ({ 0, 0, 32, 64 }, juce::Colours::red);
            cover.clear ({ 32, 0, 32, 64 }, juce::Colours::blue);
            const auto saveCover = [&]
            {
                juce::MemoryOutputStream bytes;
                expect (juce::PNGImageFormat().writeImageToStream (cover, bytes));
                expect (artworkFile.replaceWithData (bytes.getData(), bytes.getDataSize()));
            };
            saveCover();
            AlbumArtworkComponent artwork;
            artwork.setBounds (0, 0, 120, 80);
            artwork.setLookAndFeel (&adapter);
            artwork.setTrack ("first", "First", artworkFile);
            const auto paintArtwork = [&]
            {
                juce::Image image (juce::Image::ARGB, 120, 80, true);
                juce::Graphics graphics (image);
                artwork.paint (graphics);
                return image;
            };
            const auto normal = paintArtwork();
            expect (normal.getPixelAt (0, 40).isTransparent());
            expect (normal.getPixelAt (10, 5).isTransparent(), "ambience corners must be clipped");
            const auto blurred = normal.getPixelAt (60, 12);
            expect (blurred.getRed() > 40 && blurred.getBlue() > 40,
                    "the ambience must blend the two cover colours outside the actual image");
            expect (blurred.getAlpha() > 60 && blurred.getAlpha() <= 128);
            expect (normal.getPixelAt (50, 40) == juce::Colours::red,
                    "blurring must not modify the original cover");
            artwork.setEnabled (false);
            const auto disabledImage = paintArtwork();
            expectWithinAbsoluteError (static_cast<float> (disabledImage.getPixelAt (25, 40).getAlpha()),
                static_cast<float> (normal.getPixelAt (25, 40).getAlpha()) * 0.25f, 2.0f);
            expect (disabledImage.getPixelAt (60, 67).getAlpha() > 128,
                    "the variant shadow must remain above the ambience and below the cover");
            artwork.setEnabled (true);
            expect (artworkFile.deleteFile());
            artwork.setTrack ("first", "Renamed", artworkFile);
            expectEquals (pixelHash (paintArtwork()), pixelHash (normal),
                          "title updates and paints must retain the cached cover and ambience");
            cover.clear (cover.getBounds(), juce::Colours::green);
            saveCover();
            artwork.setTrack ("second", "Second", artworkFile);
            const auto changed = paintArtwork();
            expect (changed.getPixelAt (50, 40) == juce::Colours::green);
            expect (changed.getPixelAt (60, 12).getGreen() > 80);
            entry.renderer.baseLayers.erase (entry.renderer.baseLayers.begin() + 1);
            expect (paintArtwork().getPixelAt (25, 40).isTransparent(),
                    "styles without image-ambience must retain their original rendering");
            entry.renderer.baseLayers.insert (entry.renderer.baseLayers.begin() + 1, ambience);
            artwork.setTrack ({}, {});
            expect (paintArtwork().getPixelAt (60, 12) == juce::Colours::darkgrey,
                    "missing artwork must use the skin renderer fallback without stale ambience");
            expect (sink.diagnostics.empty());
            artwork.setLookAndFeel (nullptr);
            expect (artworkFile.deleteFile());
        }

        beginTest ("artwork image clip rounds the actual image pixels");
        {
            auto imageClip = rectangleLayer (
                "image-clip", { 0.0f, 0.0f, 40.0f, 40.0f },
                juce::Colours::transparentBlack, 10.0);
            imageClip.properties["clip"] = literalBinding (
                SkinPrimitive::roundedRectangle, "clip", true);

            std::map<SkinRenderTarget, SkinRenderStyleEntry> entries;
            entries[SkinRenderTarget::artwork].renderer = simpleProgram (
                SkinRenderTarget::artwork, std::move (imageClip));
            entries[SkinRenderTarget::surface].renderer = simpleProgram (
                SkinRenderTarget::surface,
                rectangleLayer ("transparent", { 0.0f, 0.0f, 40.0f, 40.0f },
                                juce::Colours::transparentBlack));
            entries[SkinRenderTarget::artwork].slotStyles = {
                { "frame.background", "surface.default" },
                { "frame.overlay", "surface.default" }
            };

            FaultRecorder sink;
            SkinRenderer renderer;
            JuceSkinAdapter adapter (
                [&] (const juce::Component&, SkinRenderTarget target,
                     const juce::String&)
                {
                    return JuceSkinAdapter::RuntimeBinding {
                        &entries.at (target), nullptr, &sink, true
                    };
                }, renderer);

            const auto artworkFile = juce::File::getSpecialLocation (
                juce::File::currentExecutableFile).getParentDirectory()
                    .getNonexistentChildFile (
                    "moonwake-artwork-pixel-clip", ".png", true);
            juce::Image opaqueArtwork (juce::Image::ARGB, 32, 32, true);
            opaqueArtwork.clear (opaqueArtwork.getBounds(), juce::Colours::red);
            juce::MemoryOutputStream artworkBytes;
            juce::PNGImageFormat png;
            expect (png.writeImageToStream (opaqueArtwork, artworkBytes));
            expect (artworkFile.replaceWithData (
                artworkBytes.getData(), artworkBytes.getDataSize()));

            AlbumArtworkComponent artwork;
            artwork.setBounds (0, 0, 40, 40);
            artwork.setLookAndFeel (&adapter);
            artwork.setTrack ("track-id", "Track", artworkFile);
            juce::Image image (juce::Image::ARGB, 40, 40, true);
            {
                juce::Graphics graphics (image);
                artwork.paint (graphics);
            }
            expect (image.getPixelAt (20, 20).getAlpha() > 0,
                    "the opaque artwork centre must be painted");
            expect (image.getPixelAt (20, 1).getAlpha() > 0,
                    "the rounded clip must retain pixels away from the corners");
            expect (image.getPixelAt (0, 0).isTransparent(),
                    "the actual artwork corner must be clipped to transparency");
            artwork.setLookAndFeel (nullptr);
            expect (artworkFile.deleteFile());
        }
    }
};

SkinRendererTests skinRendererTests;
} // namespace
