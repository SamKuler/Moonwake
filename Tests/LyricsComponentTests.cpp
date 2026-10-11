#include "ui/LyricsComponent.h"
#include "ui/AlbumArtworkComponent.h"
#include "ui/JuceSkinAdapter.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinLayoutEngine.h"
#include "fixtures/TestComponentPeer.h"

class LyricsComponentTests final : public juce::UnitTest
{
public:
    LyricsComponentTests() : UnitTest ("LyricsComponent", "lyrics") {}

    void runTest() override
    {
        SkinValidationReport report;
        const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
        const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
        const auto defaultPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
        const auto skin = defaultPackage && foundation ? SkinCompiler::compile (*defaultPackage, foundation, report) : nullptr;
        beginTest ("built-in presentations compile with their skin-owned minimum and preferred geometry");
        expect (skin != nullptr, report.toText());
        if (! skin) return;
        for (const auto entry : { SkinLayoutEntry::listening, SkinLayoutEntry::immersive, SkinLayoutEntry::mini })
            for (const auto small : { false, true })
            {
                const auto mini = entry == SkinLayoutEntry::mini;
                const juce::Rectangle<float> bounds (0, 0, mini ? (small ? 380.0f : 480.0f) : (small ? 1040.0f : 1240.0f),
                                                           mini ? (small ? 170.0f : 200.0f) : (small ? 700.0f : 800.0f));
                SkinValidationReport geometry;
                const auto layout = SkinLayoutEngine::resolve (skin->layouts.at (entry), bounds, 1, geometry,
                    WorkspaceMode::listening, skin.get(), { false, true });
                expect (layout.has_value(), geometry.toText());
                if (! layout) continue;
                for (const auto& id : { "lyrics.view", "transport.playPause", "app.status" })
                {
                    const auto* node = layout->component (id);
                    expect (node != nullptr && node->visible && ! node->bounds.isEmpty(), id);
                    if (node) expect (bounds.contains (node->bounds), id);
                }
                if (mini)
                {
                    expect (skin->layouts.at (entry).preferredSize == juce::Point<float> (480, 200));
                    const auto* notice = layout->component ("app.status");
                    expect (notice != nullptr && notice->bounds.getHeight() < bounds.getHeight() / 2,
                            "Mini notifications must remain a compact overlay");
                    for (const auto& id : { "player.restore", "transport.playPause" })
                        if (const auto* control = layout->component (id); notice && control)
                            expect (! notice->bounds.intersects (control->bounds),
                                    "Mini notification must preserve " + juce::String (id));
                }
            }

        beginTest ("all player presentations preserve real artwork pixels above their frame slots");
        std::vector<std::shared_ptr<const CompiledSkin>> artworkSkins { foundation, skin };
        {
            const auto source = juce::File (MOONWAKE_SOURCE_DIR).getChildFile ("docs/skin-api/examples/CompleteThirdParty");
            std::vector<SkinPackage::EmbeddedFile> files;
            for (const auto& file : source.findChildFiles (juce::File::findFiles, true))
            {
                juce::MemoryBlock data;
                expect (file.loadFileAsData (data));
                files.push_back ({ file.getRelativePathFrom (source).replaceCharacter ('\\', '/'), std::move (data) });
            }
            SkinValidationReport exampleReport;
            const auto package = SkinPackage::fromEmbedded ("artwork-example", std::move (files), exampleReport);
            const auto example = package ? SkinCompiler::compile (*package, foundation, exampleReport) : nullptr;
            expect (example != nullptr, exampleReport.toText());
            if (example) artworkSkins.push_back (example);
        }
        const juce::Colour imageColour (0xfff1279d);
        juce::Image sourceImage (juce::Image::ARGB, 64, 64, true);
        sourceImage.clear (sourceImage.getBounds(), imageColour);
        for (const auto& artworkSkin : artworkSkins)
            for (const auto entry : { SkinLayoutEntry::mini, SkinLayoutEntry::immersive })
            {
                SkinValidationReport layoutReport;
                const auto bounds = entry == SkinLayoutEntry::mini ? juce::Rectangle<float> (0, 0, 380, 170)
                                                                  : juce::Rectangle<float> (0, 0, 1040, 700);
                const auto layout = SkinLayoutEngine::resolve (artworkSkin->layouts.at (entry), bounds, 1, layoutReport,
                    WorkspaceMode::listening, artworkSkin.get());
                expect (layout.has_value(), layoutReport.toText());
                if (! layout) continue;
                const auto* node = layout->component ("artwork.cover");
                expect (node != nullptr);
                if (! node) continue;
                SkinRenderer artworkRenderer;
                JuceSkinAdapter artworkAdapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& requested)
                {
                    const auto name = requested.containsChar ('.') ? requested : skinRenderTargetName (target) + "." + requested;
                    return JuceSkinAdapter::RuntimeBinding { artworkSkin->resolveStyle (target, name), nullptr, nullptr, false };
                }, artworkRenderer);
                AlbumArtworkComponent artwork;
                artwork.getProperties().set (JuceSkinAdapter::styleProperty, node->style);
                artwork.getProperties().set (JuceSkinAdapter::presentationProperty, node->presentation);
                artwork.setLookAndFeel (&artworkAdapter);
                artwork.setSize (juce::roundToInt (node->bounds.getWidth()), juce::roundToInt (node->bounds.getHeight()));
                artwork.setImage ("pixel-probe", "Pixel probe", sourceImage);
                const auto imageStyle = artworkAdapter.artworkImageStyleFor (artwork, {});
                expect (imageStyle.has_value());
                if (imageStyle)
                {
                    const auto centre = imageStyle->bounds.getCentre().roundToInt();
                    for (const auto enabled : { true, false })
                    {
                        artwork.setEnabled (enabled);
                        const auto rendered = artwork.createComponentSnapshot (artwork.getLocalBounds());
                        expect (rendered.getPixelAt (centre.x, centre.y) == imageColour,
                                artworkSkin->manifest.id + " / " + skinLayoutEntryName (entry)
                                    + ": frame overlays must preserve actual image pixels");
                    }
                    artwork.setImage ("empty-probe", "No cover", {});
                    const auto empty = artwork.createComponentSnapshot (artwork.getLocalBounds());
                    expect (empty.getPixelAt (centre.x, centre.y) != imageColour,
                            "An absent cover must not retain the previous image");
                }
                artwork.setLookAndFeel (nullptr);
            }

        beginTest ("Listening player actions keep their anchors when lyrics open and close");
        for (const auto& playerSkin : artworkSkins)
            for (const auto bounds : { juce::Rectangle<float> (0, 0, 1040, 700),
                                       juce::Rectangle<float> (0, 0, 1240, 800) })
            {
                SkinValidationReport layoutReport;
                const auto& listening = playerSkin->layouts.at (SkinLayoutEntry::listening);
                const auto closed = SkinLayoutEngine::resolve (listening, bounds, 1, layoutReport,
                    WorkspaceMode::listening, playerSkin.get(), { false, false });
                const auto open = SkinLayoutEngine::resolve (listening, bounds, 1, layoutReport,
                    WorkspaceMode::listening, playerSkin.get(), { false, true });
                expect (closed.has_value() && open.has_value(), layoutReport.toText());
                if (! closed || ! open) continue;
                for (const auto* id : { "lyrics.toggle", "player.fullscreen", "player.mini" })
                {
                    const auto* before = closed->component (id);
                    const auto* after = open->component (id);
                    expect (before && after, id);
                    if (before && after)
                        expect (before->bounds == after->bounds, playerSkin->manifest.id + ": " + id);
                }
            }

        SkinRenderer renderer;
        SkinAnimationEngine animation;
        animation.setReduceMotionEnabled (true);
        JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& requested)
        {
            const auto name = requested.containsChar ('.') ? requested : skinRenderTargetName (target) + "." + requested;
            return JuceSkinAdapter::RuntimeBinding { skin->resolveStyle (target, name), &animation, nullptr, false };
        }, renderer);
        LyricsComponent component;
        component.getProperties().set (JuceSkinAdapter::styleProperty, "lyrics.document");
        component.setLookAndFeel (&adapter);
        component.setSize (320, 400);
        auto document = std::make_shared<LyricsDocument>();
        document->lines = { { 1, "First line" }, { 4, juce::String::fromUTF8 ("风经过漫长的街道，我们仍然听见远方的声音。风经过漫长的街道，我们仍然听见远方的声音。") },
                            { 9, "A line after the long Unicode paragraph" }, { 14, "Last line" } };
        component.setDocument (document);
        component.setPlaybackPosition (4);

        beginTest ("Unicode wrapping retains font size and stable row geometry when the active line changes");
        expectEquals (static_cast<int> (component.rows.size()), 4);
        expect (component.rows[1].height > component.rows[0].height * 2);
        const auto lineHeight = component.rows[1].height;
        component.setPlaybackPosition (9);
        expectWithinAbsoluteError (component.rows[1].height, lineHeight, 0.01f);
        expect (! component.isTimerRunning(), "Reduced motion must snap without starting follow animation");
        const auto current = component.rows[2];
        expectEquals (component.rowAt ({ 20, current.y - component.scrollPosition + current.height / 2 }), 2);
        expectEquals (component.rowAt ({ 20, current.y - component.scrollPosition + current.height + component.lineGap / 2 }), -1);
        const auto image = component.createComponentSnapshot (component.getLocalBounds());
        expect (image.isValid());
        component.setPlaybackPosition (0);
        expectEquals (component.activeLine, -1);
        expectWithinAbsoluteError (component.scrollPosition, 0.0f, 0.01f);
        component.setPlaybackPosition (9);

        beginTest ("keyboard focus belongs to one lyric or the empty title without changing text geometry");
        {
            TestUiRoot root;
            root.setBounds (0, 0, 320, 400);
            root.addAndMakeVisible (component);
            root.setVisible (true);
            root.addToDesktop (0);
            root.toFront (true);
            component.getProperties().set (JuceSkinAdapter::focusVisibleProperty, true);
            component.grabKeyboardFocus();
            expect (component.hasKeyboardFocus (false));
            const auto bounds = component.getLocalBounds().toFloat();
            auto focused = component.contextFor (2, bounds);
            expect (focused.state.focused, "Tab focus starts on the current lyric");
            expect (! component.contextFor (0, bounds).state.focused);
            auto unfocused = focused;
            unfocused.state.focused = false;
            const auto focusedStyle = adapter.slotTextStyleFor (component, SkinRenderTarget::lyrics, "line.text", focused);
            const auto unfocusedStyle = adapter.slotTextStyleFor (component, SkinRenderTarget::lyrics, "line.text", unfocused);
            expect (focusedStyle && unfocusedStyle);
            if (focusedStyle && unfocusedStyle)
            {
                expect (focusedStyle->font == unfocusedStyle->font, "Focus must preserve wrapping metrics");
                expect (focusedStyle->colour != unfocusedStyle->colour, "Focus must visibly emphasize the lyric text");
            }
            component.setPlaybackPosition (0);
            expect (component.contextFor (0, bounds).state.focused, "Before playback, focus starts on the first lyric");
            expect (component.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
            expect (component.contextFor (1, bounds).state.focused);
            expect (! component.contextFor (0, bounds).state.focused);
            component.setDocument ({});
            expect (component.contextFor (-1, bounds).state.focused, "An empty document focuses the status title");
            root.removeChildComponent (&component);
            root.removeFromDesktop();
            component.setDocument (document);
            component.setPlaybackPosition (9);
        }

        beginTest ("keyboard browsing pauses following, Enter seeks the original timestamp, and Home resumes");
        int followingChanges = 0;
        double seek = -1;
        component.onFollowingChanged = [&] (bool) { ++followingChanges; };
        component.onSeek = [&] (double time) { seek = time; };
        expect (component.keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
        expect (! component.isFollowing());
        expectEquals (component.keyboardLine, 1);
        const auto browsedPosition = component.scrollPosition;
        component.setPlaybackPosition (14);
        expectWithinAbsoluteError (component.scrollPosition, browsedPosition, 0.01f);
        expect (component.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        expectWithinAbsoluteError (seek, 4.0, 0.001);
        expect (component.isFollowing());
        expect (followingChanges >= 2);
        component.setFollowing (false);
        expect (component.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
        expect (component.isFollowing());

        beginTest ("wheel browsing and pointer selection use the same wrapped rows");
        const auto now = juce::Time::getCurrentTime();
        const auto pointer = [&] (juce::Point<float> position, juce::ModifierKeys mods)
        {
            return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                0, 0, 0, 0, 0, &component, &component, now, position, now, 1, false };
        };
        juce::MouseWheelDetails wheel;
        wheel.deltaY = 0.2f;
        component.mouseWheelMove (pointer ({ 20, 20 }, {}), wheel);
        expect (! component.isFollowing());
        component.scrollPosition = component.rows[1].y;
        seek = -1;
        component.mouseDown (pointer ({ 20, component.rows[1].height / 2 }, juce::ModifierKeys::leftButtonModifier));
        expectWithinAbsoluteError (seek, 4.0, 0.001);
        expect (component.isFollowing());

        beginTest ("plain lyrics remain readable and never expose a seek action");
        auto plain = std::make_shared<LyricsDocument>();
        plain->plainText = "A plain first paragraph\n\nAn unsynchronized second paragraph";
        component.setDocument (plain);
        seek = -1;
        component.mouseDown (pointer ({ 20, 20 }, juce::ModifierKeys::leftButtonModifier));
        expect (! component.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        expectWithinAbsoluteError (seek, -1.0, 0.001);
        expectEquals (static_cast<int> (component.rows.size()), 3);

        beginTest ("long paragraphs remain visible beyond the renderer's per-draw line limit");
        auto longText = std::make_shared<LyricsDocument>();
        for (int i = 0; i < 100; ++i) longText->plainText += juce::String::fromUTF8 ("风穿过城市和远方的山川，");
        component.setDocument (longText);
        expect (component.rows.front().height > 1500.0f);
        component.scrollTo (1200);
        const auto lowerParagraph = component.createComponentSnapshot (component.getLocalBounds());
        int visibleTextPixels = 0;
        for (int y = 10; y < lowerParagraph.getHeight() - 10; ++y)
            for (int x = 10; x < lowerParagraph.getWidth() - 10; ++x)
                if (lowerParagraph.getPixelAt (x, y).getAlpha() > 0) ++visibleTextPixels;
        expect (visibleTextPixels > 100, "Wrapped content after line 32 must paint visible glyphs");

        beginTest ("mini presents the current line without document scrolling");
        component.getProperties().set (JuceSkinAdapter::styleProperty, "lyrics.line");
        component.getProperties().set (JuceSkinAdapter::presentationProperty, "line");
        component.setSize (350, 26);
        component.setDocument (document);
        component.setPlaybackPosition (9);
        expect (component.compact());
        expectEquals (component.rowAt ({ 10, 10 }), 2);
        component.mouseWheelMove (pointer ({ 10, 10 }, {}), wheel);
        expect (component.isFollowing());
        expect (! component.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
        seek = -1;
        expect (component.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        expectWithinAbsoluteError (seek, document->lines[2].timeSeconds, 0.001);
        expect (component.createComponentSnapshot (component.getLocalBounds()).isValid());
        component.setLookAndFeel (nullptr);
    }
};

static LyricsComponentTests lyricsComponentTests;
