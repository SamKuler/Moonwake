#include "ui/PlaylistGridComponent.h"
#include "ui/JuceSkinAdapter.h"
#include "skin/SkinRenderCompiler.h"
#include "skin/SkinSchemaValidator.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
bool dispatchGridUntil (const std::function<bool()>& ready)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (! ready() && juce::Time::getMillisecondCounterHiRes() < deadline)
        if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10)) break;
    return ready();
}

std::optional<SkinRenderProgram> gridTestProgram (SkinValidationReport& report)
{
    const auto document = SkinSchemaValidator::validate (SkinSchemaDocument::render,
        juce::JSON::parse (R"json({
          "apiVersion": 1, "target": "playlistGrid",
          "layers": [{ "id": "body", "type": "rect", "frame": { "anchor": "bounds" }, "fill": "#00000000" }],
          "variants": { "focused": { "body.fill": "#FFFFFF20" } },
          "controlStyle": { "minItemWidth": 160, "itemGap": 16 },
          "rowLayout": {
            "rowHeight": { "$expr": "bounds.width + 72" },
            "slots": {
              "card.surface": { "x": 0, "y": 0, "width": { "$context": "bounds.width" }, "height": { "$context": "layout.rowHeight" } },
              "card.cover": { "x": 0, "y": 0, "width": { "$context": "bounds.width" }, "height": { "$context": "bounds.width" } },
              "card.title": { "x": 0, "y": { "$context": "bounds.width" }, "width": { "$context": "bounds.width" }, "height": 24 },
              "card.summary": { "x": 0, "y": { "$expr": "bounds.width + 24" }, "width": { "$context": "bounds.width" }, "height": 24 },
              "card.date": { "x": 0, "y": { "$expr": "bounds.width + 48" }, "width": { "$context": "bounds.width" }, "height": 24 }
            }
          }
        })json"), "grid-test.json", report);
    if (! document) return std::nullopt;
    return SkinRenderCompiler::compileProgram (*document, SkinTokenStore {}, report);
}

class PlaylistGridTests final : public juce::UnitTest
{
public:
    PlaylistGridTests() : UnitTest ("PlaylistGrid", "library") {}

    void runTest() override
    {
        beginTest ("collection covers use one image or four filled quadrants with bounded dimensions");
        const auto directory = juce::File (MOONWAKE_SOURCE_DIR).getChildFile ("build")
            .getNonexistentChildFile ("playlist-grid-tests", {});
        expect (directory.createDirectory());
        std::vector<juce::File> files;
        const std::vector<juce::Colour> colours {
            juce::Colours::red, juce::Colours::green, juce::Colours::blue, juce::Colours::yellow
        };
        for (const auto colour : colours)
        {
            const auto file = directory.getChildFile (juce::String (static_cast<int> (files.size())) + ".png");
            juce::Image image (juce::Image::ARGB, 128, 64, true);
            juce::Graphics (image).fillAll (colour);
            juce::FileOutputStream output (file);
            expect (output.openedOk());
            expect (juce::PNGImageFormat().writeImageToStream (image, output));
            output.flush();
            files.push_back (file);
        }
        expect (PlaylistGridComponent::buildCollectionCover ({}).isNull());
        expect (PlaylistGridComponent::buildCollectionCover ({ directory.getChildFile ("missing.png") }).isNull());
        const auto single = PlaylistGridComponent::buildCollectionCover ({ files[0], files[0] }, 64);
        expect (single.isValid());
        expectEquals (single.getWidth(), 64);
        expectEquals (single.getHeight(), 32);
        expect (single.getPixelAt (32, 16) == colours[0]);
        for (size_t count = 2; count <= 4; ++count)
        {
            const auto image = PlaylistGridComponent::buildCollectionCover (
                std::vector<juce::File> (files.begin(), files.begin() + static_cast<ptrdiff_t> (count)), 64);
            expectEquals (image.getWidth(), 64);
            expectEquals (image.getHeight(), 64);
            for (int index = 0; index < 4; ++index)
                expect (image.getPixelAt ((index % 2) * 32 + 16, (index / 2) * 32 + 16)
                        == colours[static_cast<size_t> (index) % count]);
        }

        beginTest ("grid uses skin card geometry and preserves scroll and card identity for unchanged collections");
        SkinValidationReport report;
        const auto program = gridTestProgram (report);
        expect (program.has_value(), report.toText());
        if (program)
        {
            SkinRenderStyleEntry entry;
            entry.renderer = *program;
            entry.slotStyles["card.cover"] = "artwork.card";
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding { target == SkinRenderTarget::playlistGrid ? &entry : nullptr };
            }, renderer);
            PlaylistGridComponent grid;
            grid.setLookAndFeel (&adapter);
            grid.setVisible (true);
            grid.setBounds (0, 0, 640, 240);
            std::vector<LibraryCollectionSummary> collections;
            for (int i = 0; i < 8; ++i)
            {
                LibraryCollectionSummary collection;
                collection.id = "playlist:" + juce::String (i);
                collection.name = i == 0 ? "Evening Mix" : "Collection " + juce::String (i);
                collection.subtitle = i == 1 ? "Evening sessions" : "Archive";
                collections.push_back (collection);
            }
            grid.setCollections (collections, {});
            expectEquals (grid.visibleCollectionCount(), 8);
            auto* viewport = dynamic_cast<juce::Viewport*> (grid.getChildComponent (0));
            expect (viewport != nullptr);
            if (viewport != nullptr)
            {
                auto* content = viewport->getViewedComponent();
                auto* first = content->getChildComponent (0);
                auto* second = content->getChildComponent (1);
                expectEquals (first->getHeight(), first->getWidth() + 72);
                expectEquals (second->getX() - first->getRight(), 16);
                expect (first->getChildComponent (0)->getProperties()[JuceSkinAdapter::styleProperty]
                            .toString() == "artwork.card");
                viewport->setViewPosition (0, 300);
                const auto viewPosition = viewport->getViewPosition();
                grid.setCollections (collections, {});
                expect (content->getChildComponent (0) == first);
                expect (viewport->getViewPosition() == viewPosition);
                grid.setSize (360, 240);
                expect (content->getChildComponent (2)->getY() > content->getChildComponent (0)->getY());
            }

            beginTest ("query filters names and subtitles and a card dispatches its stable collection ID");
            grid.setQuery (" EVENING ");
            expectEquals (grid.visibleCollectionCount(), 2);
            if (viewport != nullptr)
            {
                auto* button = dynamic_cast<juce::Button*> (viewport->getViewedComponent()->getChildComponent (1));
                expect (button != nullptr);
                juce::String opened;
                grid.onOpenCollection = [&] (juce::String id) { opened = std::move (id); };
                if (button != nullptr)
                {
                    expect (static_cast<juce::Component*> (button)->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
                    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000;
                    while (opened.isEmpty() && juce::Time::getMillisecondCounterHiRes() < deadline)
                        if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10)) break;
                    expectEquals (opened, juce::String ("playlist:1"));
                }
            }
            grid.setQuery ("absent collection");
            expectEquals (grid.visibleCollectionCount(), 0);
            grid.setQuery ({});
            expectEquals (grid.visibleCollectionCount(), 8);
            grid.setLookAndFeel (nullptr);

            beginTest ("scroll cancellation skips queued covers and stale results cannot reset a replacement request");
            {
                juce::WaitableEvent releaseFirst, releaseReplacement;
                std::atomic<int> firstLoads { 0 }, otherLoads { 0 };
                std::atomic<bool> firstStarted { false }, replacementStarted { false };
                PlaylistGridComponent loadingGrid ([&] (const std::vector<juce::File>& requested)
                {
                    const auto firstFile = requested.front().getFileNameWithoutExtension() == "first";
                    const auto occurrence = firstFile ? ++firstLoads : ++otherLoads;
                    if (firstFile && occurrence == 1)
                    {
                        firstStarted.store (true);
                        releaseFirst.wait (2000);
                    }
                    else if (firstFile && occurrence == 2)
                    {
                        replacementStarted.store (true);
                        releaseReplacement.wait (2000);
                    }
                    juce::Image result (juce::Image::ARGB, 16, 16, true);
                    juce::Graphics (result).fillAll (firstFile
                        ? (occurrence == 1 ? juce::Colours::red : juce::Colours::blue)
                        : juce::Colours::green);
                    return result;
                });
                loadingGrid.setLookAndFeel (&adapter);
                loadingGrid.setVisible (true);
                loadingGrid.setBounds (0, 0, 200, 180);
                LibraryCollectionSummary firstCollection, otherCollection;
                firstCollection.id = "first";
                firstCollection.name = "First";
                firstCollection.artworkKeys.add ("first");
                otherCollection.id = "other";
                otherCollection.name = "Other";
                otherCollection.artworkKeys.add ("other");
                int firstResolutions = 0, otherResolutions = 0;
                loadingGrid.setCollections ({ firstCollection, otherCollection }, [&] (const juce::String& key)
                {
                    if (key == "first") ++firstResolutions;
                    else ++otherResolutions;
                    return directory.getChildFile (key + ".png");
                });
                expect (dispatchGridUntil ([&] { return firstStarted.load(); }));
                auto* loadingViewport = dynamic_cast<juce::Viewport*> (loadingGrid.getChildComponent (0));
                expect (loadingViewport != nullptr);
                if (loadingViewport != nullptr)
                {
                    auto* firstCard = loadingViewport->getViewedComponent()->getChildComponent (0);
                    auto* otherCard = loadingViewport->getViewedComponent()->getChildComponent (1);
                    loadingViewport->setViewPosition (0, otherCard->getY());
                    expect (dispatchGridUntil ([&] { return otherResolutions == 1; }));
                    loadingViewport->setViewPosition (0, 0);
                    expect (dispatchGridUntil ([&] { return firstResolutions == 2; }));
                    releaseFirst.signal();
                    expect (dispatchGridUntil ([&] { return replacementStarted.load(); }));
                    expectEquals (otherLoads.load(), 0, "The offscreen queued cover must not call the loader.");

                    loadingGrid.resized();
                    bool drained = false;
                    juce::MessageManager::callAsync ([&] { drained = true; });
                    expect (dispatchGridUntil ([&] { return drained; }));
                    expectEquals (firstResolutions, 2, "An old result must not clear the replacement request's pending state.");
                    releaseReplacement.signal();
                    const auto coverColour = [] (juce::Component& card)
                    {
                        auto* cover = card.getChildComponent (0);
                        const auto image = cover->createComponentSnapshot (cover->getLocalBounds());
                        return image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2);
                    };
                    expect (dispatchGridUntil ([&] { return coverColour (*firstCard) == juce::Colours::blue; }),
                            "The returning card must display the replacement request, not the old red image.");
                    loadingViewport->setViewPosition (0, otherCard->getY());
                    expect (dispatchGridUntil ([&] { return coverColour (*otherCard) == juce::Colours::green; }),
                            "A previously cancelled queued card must load when it becomes visible again.");
                    expectEquals (otherLoads.load(), 1);
                }
                releaseFirst.signal();
                releaseReplacement.signal();
                loadingGrid.setLookAndFeel (nullptr);
            }
        }
        expect (directory.deleteRecursively());
    }
};

PlaylistGridTests playlistGridTests;
}
