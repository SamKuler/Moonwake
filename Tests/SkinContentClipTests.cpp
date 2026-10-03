#include "skin/SkinRenderCompiler.h"
#include "skin/SkinSchemaValidator.h"
#include "ui/JuceSkinAdapter.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class ClipFaults final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic& diagnostic, SkinRenderStatus) override
    {
        diagnostics.push_back (diagnostic);
    }
    std::vector<SkinDiagnostic> diagnostics;
};

juce::var clipRendererDocument()
{
    return juce::JSON::parse (R"json({
      "apiVersion": 1, "target": "skinList", "contentClip": "body",
      "layers": [{ "id": "body", "type": "roundedRect",
        "frame": { "anchor": "bounds" }, "radius": 14,
        "fill": "#183040FF", "stroke": { "color": "#00FF00FF", "width": 2, "placement": "inside" } }],
      "variants": { "focused": { "body.stroke.color": "#FFFF00FF", "body.stroke.width": 4 },
                    "disabled": { "body.stroke.color": "#888888FF" } }
    })json");
}

std::optional<SkinRenderProgram> compileClip (juce::var value, SkinValidationReport& report)
{
    const auto validated = SkinSchemaValidator::validate (SkinSchemaDocument::render, value, "clip.json", report);
    return validated ? SkinRenderCompiler::compileProgram (*validated, {}, report) : std::nullopt;
}

class ClipList final : public juce::ListBox, private juce::ListBoxModel
{
public:
    explicit ClipList (JuceSkinAdapter& adapterToUse) : adapter (adapterToUse)
    {
        setModel (this);
        setRowHeight (20);
        setOutlineThickness (0);
        setColour (backgroundColourId, juce::Colours::transparentBlack);
        setColour (outlineColourId, juce::Colours::transparentBlack);
        setLookAndFeel (&adapter);
        setSize (100, 100);
        updateContent();
        adapter.configureListContentClip (*this, SkinRenderTarget::skinList);
    }
    ~ClipList() override { setModel (nullptr); setLookAndFeel (nullptr); }
    void paint (juce::Graphics& graphics) override
    {
        adapter.drawCustomComponent (graphics, *this, SkinRenderTarget::skinList, {});
        juce::ListBox::paint (graphics);
    }
    int getNumRows() override { return rows; }
    void paintListBoxItem (int, juce::Graphics& graphics, int, int, bool selected) override
    {
        graphics.fillAll (selected ? juce::Colours::red : juce::Colours::blue);
    }
    JuceSkinAdapter& adapter;
    int rows = 12;
};

bool borderPixelsPreserved (ClipList& list, const juce::Image& composite, juce::Colour border)
{
    juce::Image background (juce::Image::ARGB, list.getWidth(), list.getHeight(), true);
    { juce::Graphics graphics (background); list.paint (graphics); }
    int checked = 0;
    for (int y = 0; y < background.getHeight(); ++y)
        for (int x = 0; x < background.getWidth(); ++x)
            if (background.getPixelAt (x, y) == border)
            {
                ++checked;
                if (composite.getPixelAt (x, y) != border) return false;
            }
    return checked > 0;
}

struct ClipScene
{
    explicit ClipScene (const SkinRenderProgram& program)
        : entry { program, {}, {} },
          adapter ([this] (const juce::Component&, SkinRenderTarget target, const juce::String&)
          {
              return JuceSkinAdapter::RuntimeBinding {
                  target == SkinRenderTarget::skinList ? &entry : nullptr, nullptr, &faults, true
              };
          }, renderer), list (adapter) {}
    SkinRenderStyleEntry entry;
    ClipFaults faults;
    SkinRenderer renderer;
    JuceSkinAdapter adapter;
    ClipList list;
};

class SkinContentClipTests final : public juce::UnitTest
{
public:
    SkinContentClipTests() : UnitTest ("SkinContentClip", "skin") {}
    void runTest() override
    {
        beginTest ("list children remain inside the skin shape for first, last and scrolled selected rows");
        {
            SkinValidationReport report;
            const auto program = compileClip (clipRendererDocument(), report);
            expect (program.has_value(), report.toText());
            if (! program) return;
            SkinRenderStyleEntry entry;
            entry.renderer = *program;
            SkinRenderStyleEntry scrollbar;
            const auto scrollProgram = compileClip (juce::JSON::parse (R"json({
                "apiVersion":1, "target":"scrollBar", "layers":[
                  {"id":"bar", "type":"rect", "frame":{"anchor":"bounds"}, "fill":"#FFFF00FF"}]
            })json"), report);
            expect (scrollProgram.has_value(), report.toText());
            if (scrollProgram) scrollbar.renderer = *scrollProgram;
            ClipFaults faults;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    target == SkinRenderTarget::skinList ? &entry : target == SkinRenderTarget::scrollBar ? &scrollbar : nullptr,
                    nullptr, &faults, true
                };
            }, renderer);
            ClipList list (adapter);
            for (const auto row : { 0, 11, 6 })
            {
                list.selectRow (row);
                list.scrollToEnsureRowIsOnscreen (row);
                const auto image = list.createComponentSnapshot (list.getLocalBounds());
                expect (image.getPixelAt (0, 0).isTransparent(), "top-left row pixels must be clipped");
                expect (image.getPixelAt (0, 99).isTransparent(), "bottom-left row pixels must be clipped");
                expect (image.getPixelAt (50, 0) == juce::Colours::lime, "the owner's border stays above selected content");
                expect (borderPixelsPreserved (list, image, juce::Colours::lime), "selected rows preserve the complete solid border");
                const auto rowBounds = list.getRowPosition (row, true);
                expect (image.getPixelAt (50, juce::jlimit (4, 95, rowBounds.getCentreY())) == juce::Colours::red);
                expect (list.getVerticalScrollBar().isVisible());
                expect (image.getPixelAt (96, 50) == juce::Colours::yellow, "native scrollbars remain visible inside the boundary");
            }
            auto* cache = list.getViewport()->getCachedComponentImage();
            adapter.configureListContentClip (list, SkinRenderTarget::skinList);
            expect (cache != nullptr && list.getViewport()->getCachedComponentImage() == cache);
            const auto scaled = list.createComponentSnapshot (list.getLocalBounds(), true, 2.0f);
            expect (scaled.getPixelAt (0, 0).isTransparent());
            expect (scaled.getPixelAt (100, 0) == juce::Colours::lime);
            expect (faults.diagnostics.empty());
        }

        beginTest ("headers, disabled rows and empty lists use the same content boundary without changing old skins");
        {
            SkinValidationReport report;
            const auto program = compileClip (clipRendererDocument(), report);
            expect (program.has_value(), report.toText());
            if (! program) return;
            ClipScene scene (*program);
            struct Header final : juce::Component
            {
                void paint (juce::Graphics& graphics) override { graphics.fillAll (juce::Colours::red); }
            };
            auto header = std::make_unique<Header>();
            header->setSize (100, 20);
            scene.list.setHeaderComponent (std::move (header));
            scene.adapter.configureListContentClip (scene.list, SkinRenderTarget::skinList);
            auto image = scene.list.createComponentSnapshot (scene.list.getLocalBounds());
            expect (image.getPixelAt (0, 0).isTransparent(), "a separate native header must share the list clip");
            expect (image.getPixelAt (50, 6) == juce::Colours::red);
            expect (image.getPixelAt (50, 0) == juce::Colours::lime);
            scene.list.setEnabled (false);
            image = scene.list.createComponentSnapshot (scene.list.getLocalBounds());
            expect (image.getPixelAt (0, 0).isTransparent());
            expect (image.getPixelAt (50, 0) == juce::Colour (0xff888888));
            scene.list.setHeaderComponent (nullptr);
            scene.list.rows = 0;
            scene.list.updateContent();
            image = scene.list.createComponentSnapshot (scene.list.getLocalBounds());
            expect (image.getPixelAt (50, 50) == juce::Colour (0xff183040));
            expect (image.getPixelAt (0, 0).isTransparent());
            scene.entry.renderer.contentClip.clear();
            scene.list.setEnabled (true);
            scene.list.rows = 12;
            scene.list.updateContent();
            scene.list.selectRow (0);
            image = scene.list.createComponentSnapshot (scene.list.getLocalBounds());
            expect (image.getPixelAt (0, 0) == juce::Colours::red,
                    "omitting contentClip retains the legacy rectangular child paint boundary");
            expect (scene.faults.diagnostics.empty());
        }

        beginTest ("focused list borders stay visible outside the clipped native rows");
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        {
            expect (false, "native list focus regression requires display access");
        }
        else
        {
            SkinValidationReport report;
            const auto program = compileClip (clipRendererDocument(), report);
            expect (program.has_value(), report.toText());
            if (! program) return;
            ClipScene scene (*program);
            juce::Component host;
            host.setBounds (100, 100, 100, 100);
            host.addAndMakeVisible (scene.list);
            host.addToDesktop (0);
            host.setVisible (true);
            host.toFront (true);
            scene.list.selectRow (0);
            scene.list.grabKeyboardFocus();
            expect (scene.list.hasKeyboardFocus (false));
            const auto image = scene.list.createComponentSnapshot (scene.list.getLocalBounds());
            expect (image.getPixelAt (50, 2) == juce::Colours::yellow);
            expect (borderPixelsPreserved (scene.list, image, juce::Colours::yellow));
            expect (image.getPixelAt (50, 6) == juce::Colours::red);
            expect (image.getPixelAt (0, 0).isTransparent());
            host.removeFromDesktop();
            expect (scene.faults.diagnostics.empty());
        }

        beginTest ("installing list clipping preserves an existing component cache");
        {
            SkinValidationReport report;
            const auto program = compileClip (clipRendererDocument(), report);
            expect (program.has_value(), report.toText());
            if (! program) return;
            ClipScene scene (*program);
            struct ExistingCache final : juce::CachedComponentImage
            {
                void paint (juce::Graphics&) override {}
                bool invalidateAll() override { return true; }
                bool invalidate (const juce::Rectangle<int>&) override { return true; }
                void releaseResources() override {}
            };
            auto* cache = new ExistingCache;
            scene.list.getViewport()->setCachedComponentImage (cache);
            scene.adapter.configureListContentClip (scene.list, SkinRenderTarget::skinList);
            expect (scene.list.getViewport()->getCachedComponentImage() == cache);
        }

        beginTest ("content clips validate root shape references while old definitions remain valid");
        {
            for (const auto& primitive : { "rect", "roundedRect", "ellipse" })
            {
                auto document = clipRendererDocument();
                auto& layer = document.getDynamicObject()->getProperty ("layers").getArray()->getReference (0);
                layer.getDynamicObject()->setProperty ("type", primitive);
                if (juce::String (primitive) != "roundedRect") layer.getDynamicObject()->removeProperty ("radius");
                SkinValidationReport report;
                const auto program = compileClip (document, report);
                expect (program.has_value(), report.toText());
                if (program)
                {
                    ClipScene scene (*program);
                    scene.list.selectRow (0);
                    const auto image = scene.list.createComponentSnapshot (scene.list.getLocalBounds());
                    expect (image.getPixelAt (50, 10) == juce::Colours::red);
                    expect (borderPixelsPreserved (scene.list, image, juce::Colours::lime));
                    expect (image.getPixelAt (0, 0).isTransparent() == (juce::String (primitive) != "rect"));
                }
            }
            for (const auto& reference : { "missing", "" })
            {
                auto document = clipRendererDocument();
                document.getDynamicObject()->setProperty ("contentClip", reference);
                SkinValidationReport report;
                expect (! compileClip (document, report));
            }
            auto document = clipRendererDocument();
            document.getDynamicObject()->setProperty ("target", "textButton");
            SkinValidationReport wrongTarget;
            expect (! compileClip (document, wrongTarget));
            expect (wrongTarget.hasCode ("RENDER004"));
            document.getDynamicObject()->removeProperty ("contentClip");
            SkinValidationReport legacy;
            expect (compileClip (document, legacy).has_value(), legacy.toText());
            auto nested = clipRendererDocument();
            auto group = juce::JSON::parse (R"json({"id":"container","type":"group","children":[]})json");
            group.getDynamicObject()->setProperty ("children", nested.getProperty ("layers", {}));
            juce::Array<juce::var> roots;
            roots.add (group);
            nested.getDynamicObject()->setProperty ("layers", juce::var (roots));
            SkinValidationReport nestedReference;
            expect (! compileClip (nested, nestedReference));
            expect (nestedReference.hasCode ("RENDER004"));
        }
    }
};

SkinContentClipTests skinContentClipTests;
} // namespace
