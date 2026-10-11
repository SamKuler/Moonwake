#include "skin/SkinLayoutEngine.h"

#include <juce_core/juce_core.h>

namespace
{
SkinLength pixels (float value) { return { SkinLength::Unit::pixels, value }; }
SkinLength percent (float value) { return { SkinLength::Unit::percent, value }; }
SkinLength fraction (float value) { return { SkinLength::Unit::fraction, value }; }

SkinLayoutNode visual (juce::String id, SkinLength width = {}, SkinLength height = {})
{
    SkinLayoutNode node;
    node.nodeType = SkinLayoutNodeType::visual;
    node.common.id = std::move (id);
    node.common.style = "surface.default";
    node.common.width = width;
    node.common.height = height;
    node.payload = SkinVisualLayout {};
    return node;
}

SkinLayoutNode component (juce::String id, juce::String componentId,
                          SkinLength width = {}, SkinLength height = {})
{
    SkinLayoutNode node;
    node.nodeType = SkinLayoutNodeType::component;
    node.common.id = std::move (id);
    node.common.width = width;
    node.common.height = height;
    SkinComponentLayout layout;
    layout.componentId = std::move (componentId);
    layout.focusOrder = 1;
    node.payload = std::move (layout);
    return node;
}

SkinLayoutDefinition definitionWith (SkinLayoutNode root)
{
    SkinLayoutDefinition result;
    result.base = std::move (root);
    return result;
}

SkinLayoutNode flex (SkinLayoutNodeType type, juce::String id,
                     std::vector<SkinLayoutNode> children)
{
    SkinLayoutNode node;
    node.nodeType = type;
    node.common.id = std::move (id);
    SkinFlexLayout layout;
    layout.children = std::move (children);
    node.payload = std::move (layout);
    return node;
}

const SkinResolvedNode* child (const SkinResolvedNode& root, juce::StringRef id)
{
    for (const auto& candidate : root.children)
        if (candidate.nodeId == id) return &candidate;
    return nullptr;
}

class SkinLayoutEngineTests final : public juce::UnitTest
{
public:
    SkinLayoutEngineTests() : UnitTest ("SkinLayoutEngine") {}

    void runTest() override
    {
        beginTest ("conditional subtrees reclaim width and gap without modifying the definition");
        {
            auto content = visual ("content");
            content.common.grow = 1;
            auto queue = component ("queue", "queue.table", pixels (300));
            queue.common.visibleWhen = std::array<bool, 4> { false, true, false, true };
            auto root = flex (SkinLayoutNodeType::row, "root", { content, queue });
            std::get<SkinFlexLayout> (root.payload).gap = 16;
            const auto layout = definitionWith (root);
            SkinValidationReport report;
            const auto open = SkinLayoutEngine::resolve (layout, { 0, 0, 1000, 400 }, 1, report, {}, nullptr, { true });
            const auto closed = SkinLayoutEngine::resolve (layout, { 0, 0, 1000, 400 }, 1, report, {}, nullptr, { false });
            expect (open && closed, report.toText());
            if (open && closed)
            {
                expectWithinAbsoluteError (open->root.children.front().bounds.getWidth(), 684.0f, 0.01f);
                expectWithinAbsoluteError (closed->root.children.front().bounds.getWidth(), 1000.0f, 0.01f);
                expect (closed->component ("queue.table") == nullptr);
                expectEquals (static_cast<int> (std::get<SkinFlexLayout> (layout.base.payload).children.size()), 2);
            }
        }

        beginTest ("row applies padding, margin, gap, percent, grow and stretch exactly");
        {
            auto fixed = visual ("fixed", pixels (50));
            fixed.common.margin.left = pixels (5);
            fixed.common.margin.right = pixels (5);
            auto proportional = visual ("percent", percent (25));
            auto growing = visual ("grow");
            growing.common.grow = 1;
            auto root = flex (SkinLayoutNodeType::row, "root",
                              { std::move (fixed), std::move (proportional), std::move (growing) });
            root.common.padding.top = root.common.padding.right
                = root.common.padding.bottom = root.common.padding.left = pixels (10);
            std::get<SkinFlexLayout> (root.payload).gap = 10;
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 240, 100 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                const auto* a = child (result->root, "fixed");
                const auto* b = child (result->root, "percent");
                const auto* c = child (result->root, "grow");
                expect (a && a->bounds == juce::Rectangle<float> (15, 10, 50, 80));
                expect (b && b->bounds == juce::Rectangle<float> (80, 10, 55, 80));
                expect (c && c->bounds == juce::Rectangle<float> (145, 10, 85, 80));
            }
        }

        beginTest ("flex shrink honours min sizes and cumulative rounding closes the edge");
        {
            auto a = visual ("a", {}, pixels (80));
            auto b = visual ("b", {}, pixels (80));
            a.common.minHeight = b.common.minHeight = 30;
            auto root = flex (SkinLayoutNodeType::column, "root",
                              { std::move (a), std::move (b) });
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 100, 100 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expect (result->root.children[0].bounds == juce::Rectangle<float> (0, 0, 100, 50));
                expect (result->root.children[1].bounds == juce::Rectangle<float> (0, 50, 100, 50));
            }
        }
        {
            auto capped = visual ("capped", pixels (50));
            capped.common.grow = 1;
            capped.common.maxWidth = 60;
            auto remainder = visual ("remainder", pixels (50));
            remainder.common.grow = 1;
            auto root = flex (SkinLayoutNodeType::row, "root",
                              { std::move (capped), std::move (remainder) });
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 200, 40 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expectWithinAbsoluteError (result->root.children[0].bounds.getWidth(), 60.0f, 0.001f);
                expectWithinAbsoluteError (result->root.children[1].bounds.getWidth(), 140.0f, 0.001f);
            }
        }
        {
            auto a = visual ("a"); a.common.grow = 1;
            auto b = visual ("b"); b.common.grow = 1;
            auto c = visual ("c"); c.common.grow = 1;
            auto root = flex (SkinLayoutNodeType::row, "root",
                              { std::move (a), std::move (b), std::move (c) });
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 100, 40 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expectWithinAbsoluteError (result->root.children[0].bounds.getWidth(), 33.0f, 0.001f);
                expectWithinAbsoluteError (result->root.children[1].bounds.getWidth(), 34.0f, 0.001f);
                expectWithinAbsoluteError (result->root.children[2].bounds.getRight(), 100.0f, 0.001f);
            }
        }

        beginTest ("justify spaceBetween places remaining space between siblings");
        {
            auto root = flex (SkinLayoutNodeType::row, "root",
                              { visual ("a", pixels (50)), visual ("b", pixels (50)) });
            std::get<SkinFlexLayout> (root.payload).justify = SkinJustify::spaceBetween;
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 200, 40 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expectWithinAbsoluteError (result->root.children[0].bounds.getX(), 0.0f, 0.001f);
                expectWithinAbsoluteError (result->root.children[1].bounds.getX(), 150.0f, 0.001f);
            }
        }

        beginTest ("grid resolves fixed and fr tracks, gaps and explicit spans");
        {
            SkinLayoutNode root;
            root.nodeType = SkinLayoutNodeType::grid;
            root.common.id = "grid";
            SkinGridLayout grid;
            grid.columns = { pixels (100), fraction (1), fraction (2) };
            grid.rows = { fraction (1), fraction (1) };
            grid.columnGap = 10;
            grid.rowGap = 10;
            auto first = visual ("first");
            first.common.gridColumn = 2;
            first.common.gridRow = 1;
            auto span = visual ("span");
            span.common.gridColumn = 1;
            span.common.gridRow = 2;
            span.common.columnSpan = 3;
            grid.children.push_back (std::move (first));
            grid.children.push_back (std::move (span));
            root.payload = std::move (grid);
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 300, 110 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                const auto* firstResult = child (result->root, "first");
                const auto* spanResult = child (result->root, "span");
                expect (firstResult && firstResult->bounds == juce::Rectangle<float> (110, 0, 60, 50));
                expect (spanResult && spanResult->bounds == juce::Rectangle<float> (0, 60, 300, 50));
            }
        }

        beginTest ("automatic grid placement contributes to auto track sizes");
        {
            SkinLayoutNode root;
            root.nodeType = SkinLayoutNodeType::grid;
            root.common.id = "grid";
            SkinGridLayout grid;
            grid.columns = { SkinLength {} };
            grid.rows = { SkinLength {} };
            grid.children.push_back (component ("play", "transport.playPause"));
            root.payload = std::move (grid);
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (
                definitionWith (std::move (root)), { 0, 0, 200, 100 }, 1.0f,
                report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                const auto* play = child (result->root, "play");
                expect (play != nullptr);
                if (play != nullptr)
                {
                    expect (play->bounds.getPosition() == juce::Point<float> (0, 0));
                    expect (play->bounds.getWidth() >= 28.0f);
                    expect (play->bounds.getHeight() >= 28.0f);
                }
            }
        }

        beginTest ("stack anchors, z-order and clipping are deterministic");
        {
            SkinLayoutNode root;
            root.nodeType = SkinLayoutNodeType::stack;
            root.common.id = "stack";
            root.common.padding.top = root.common.padding.right
                = root.common.padding.bottom = root.common.padding.left = pixels (10);
            root.common.clip = true;
            SkinStackLayout stack;
            auto fill = visual ("fill");
            fill.common.anchors.left = pixels (5);
            fill.common.anchors.right = pixels (15);
            fill.common.anchors.top = pixels (10);
            fill.common.anchors.bottom = pixels (20);
            fill.common.zIndex = 2;
            auto badge = visual ("badge", pixels (30), pixels (20));
            badge.common.anchors.right = pixels (0);
            badge.common.anchors.bottom = pixels (0);
            badge.common.zIndex = 1;
            stack.children.push_back (std::move (fill));
            stack.children.push_back (std::move (badge));
            root.payload = std::move (stack);
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 200, 120 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expectEquals (result->root.children[0].nodeId, juce::String ("badge"));
                expectEquals (result->root.children[1].nodeId, juce::String ("fill"));
                const auto* fillResult = child (result->root, "fill");
                expect (fillResult && fillResult->bounds == juce::Rectangle<float> (15, 20, 160, 70));
                expect (fillResult && fillResult->clipBounds == juce::Rectangle<float> (0, 0, 200, 120));
            }
        }

        beginTest ("vertical scroll preserves content height and clips to its viewport");
        {
            SkinLayoutNode root;
            root.nodeType = SkinLayoutNodeType::scroll;
            root.common.id = "scroll";
            root.common.padding.top = root.common.padding.right
                = root.common.padding.bottom = root.common.padding.left = pixels (6);
            SkinScrollLayout scroll;
            scroll.direction = SkinScrollLayout::Direction::vertical;
            scroll.child = std::make_unique<SkinLayoutNode> (visual ("content", {}, pixels (240)));
            root.payload = std::move (scroll);
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 180, 100 }, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expect (result->root.scrollViewportBounds
                        == juce::Rectangle<float> (6, 6, 168, 88));
                expect (result->root.scrollContentBounds
                        == juce::Rectangle<float> (6, 6, 160, 240));
                expect (result->root.children[0].bounds
                        == juce::Rectangle<float> (6, 6, 160, 240));
                expect (result->root.children[0].clipBounds
                        == juce::Rectangle<float> (6, 6, 168, 88));
            }
        }

        beginTest ("scrolling content does not impose its minimum along the scrolling axis");
        for (const auto vertical : { true, false })
        {
            auto contents = visual ("contents", pixels (400), pixels (400));
            if (vertical) contents.common.minHeight = 400;
            else contents.common.minWidth = 400;
            SkinLayoutNode viewport;
            viewport.nodeType = SkinLayoutNodeType::scroll;
            viewport.common.id = "viewport";
            viewport.common.grow = 1;
            SkinScrollLayout scroll;
            scroll.direction = vertical ? SkinScrollLayout::Direction::vertical
                                        : SkinScrollLayout::Direction::horizontal;
            scroll.child = std::make_unique<SkinLayoutNode> (std::move (contents));
            viewport.payload = std::move (scroll);
            auto footer = visual ("fixed", pixels (40), pixels (40));
            footer.common.shrink = 0;
            auto root = flex (vertical ? SkinLayoutNodeType::column : SkinLayoutNodeType::row,
                              "root", { std::move (viewport), std::move (footer) });
            SkinValidationReport report;
            const auto resolved = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                { 0, 0, 180, 180 }, 1.0f, report);
            expect (resolved.has_value(), report.toText());
            if (resolved)
            {
                const auto* view = child (resolved->root, "viewport");
                expect (view != nullptr);
                if (view)
                {
                    expectWithinAbsoluteError (vertical ? view->bounds.getHeight() : view->bounds.getWidth(), 140.0f, 0.01f);
                    expectWithinAbsoluteError (vertical ? view->scrollContentBounds.getHeight() : view->scrollContentBounds.getWidth(), 400.0f, 0.01f);
                }
            }
        }

        beginTest ("horizontal scroll reserves its scrollbar without clipping content height");
        {
            SkinLayoutNode root;
            root.nodeType = SkinLayoutNodeType::scroll;
            root.common.id = "horizontal-scroll";
            root.common.padding.top = root.common.padding.right
                = root.common.padding.bottom = root.common.padding.left = pixels (6);
            SkinScrollLayout scroll;
            scroll.direction = SkinScrollLayout::Direction::horizontal;
            scroll.child = std::make_unique<SkinLayoutNode> (
                visual ("content", pixels (240), {}));
            root.payload = std::move (scroll);
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (
                definitionWith (std::move (root)), { 0, 0, 180, 100 }, 1.0f,
                report);
            expect (result.has_value(), report.toText());
            if (result)
            {
                expect (result->root.scrollViewportBounds
                        == juce::Rectangle<float> (6, 6, 168, 88));
                expect (result->root.scrollContentBounds
                        == juce::Rectangle<float> (6, 6, 240, 80));
                expect (result->root.children[0].bounds
                        == juce::Rectangle<float> (6, 6, 240, 80));
            }
        }

        beginTest ("subpixel overflow follows the same rounded geometry as JUCE");
        {
            SkinLayoutNode root;
            root.nodeType = SkinLayoutNodeType::scroll;
            root.common.id = "subpixel-scroll";
            SkinScrollLayout scroll;
            scroll.direction = SkinScrollLayout::Direction::vertical;
            scroll.child = std::make_unique<SkinLayoutNode> (
                visual ("content", {}, pixels (100.4f)));
            root.payload = std::move (scroll);
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (
                definitionWith (std::move (root)), { 0, 0, 180, 100 }, 1.0f,
                report);
            expect (result.has_value(), report.toText());
            if (result)
                expectEquals (juce::roundToInt (
                                  result->root.scrollContentBounds.getWidth()),
                              180,
                              "a hidden scrollbar must not leave a cross-axis gutter");
        }

        beginTest ("first inclusive breakpoint root wins at exact width, height and scale bounds");
        {
            auto definition = definitionWith (visual ("base"));
            SkinLayoutDefinition::Breakpoint first;
            first.widths.minimum = 1000.0f; first.widths.maximum = 1200.0f;
            first.heights.minimum = 700.0f; first.heights.maximum = 900.0f;
            first.scaleFactors.minimum = 1.0f; first.scaleFactors.maximum = 2.0f;
            first.root = visual ("first");
            SkinLayoutDefinition::Breakpoint second;
            second.widths.minimum = 1000.0f; second.widths.maximum = 1200.0f;
            second.root = visual ("second");
            definition.breakpoints.push_back (std::move (first));
            definition.breakpoints.push_back (std::move (second));
            for (const auto bounds : { juce::Rectangle<float> (0, 0, 1000, 700),
                                       juce::Rectangle<float> (0, 0, 1200, 900) })
            {
                SkinValidationReport report;
                const auto result = SkinLayoutEngine::resolve (definition, bounds,
                                                               bounds.getWidth() < 1100.0f ? 1.0f : 2.0f,
                                                               report);
                expect (result.has_value(), report.toText());
                if (result) expectEquals (result->root.nodeId, juce::String ("first"));
            }
        }

        beginTest ("workspace predicates combine with dimensions and preserve legacy defaults");
        {
            auto definition = definitionWith (visual ("base"));
            definition.ancillaryStyles = { { "popupMenu", "popupMenu.default" },
                                            { "tooltip", "tooltip.default" } };
            for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
            {
                SkinLayoutDefinition::Breakpoint breakpoint;
                breakpoint.workspace = workspace;
                breakpoint.widths.maximum = 1240.0f;
                breakpoint.heights.minimum = 700.0f;
                breakpoint.scaleFactors.maximum = 2.0f;
                breakpoint.root = visual (toString (workspace));
                if (workspace == WorkspaceMode::studio)
                    breakpoint.ancillaryStyles = { { "popupMenu", "popupMenu.studio" },
                                                    { "scrollBar", "scrollBar.studio" } };
                definition.breakpoints.push_back (std::move (breakpoint));
            }
            for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
            {
                SkinValidationReport report;
                const auto result = SkinLayoutEngine::resolve (definition, { 0, 0, 1240, 700 },
                                                               2.0f, report, workspace);
                expect (result.has_value(), report.toText());
                if (result)
                {
                    expectEquals (result->root.nodeId, toString (workspace));
                    expectEquals (result->ancillaryStyles.at ("tooltip"), juce::String ("tooltip.default"));
                    expectEquals (result->ancillaryStyles.at ("popupMenu"), juce::String (
                        workspace == WorkspaceMode::studio ? "popupMenu.studio" : "popupMenu.default"));
                    expect (result->ancillaryStyles.contains ("scrollBar") == (workspace == WorkspaceMode::studio));
                }
                const auto outside = SkinLayoutEngine::resolve (definition, { 0, 0, 1241, 700 },
                                                                2.0f, report, workspace);
                expect (outside.has_value(), report.toText());
                if (outside)
                {
                    expectEquals (outside->root.nodeId, juce::String ("base"));
                    expect (outside->ancillaryStyles == definition.ancillaryStyles);
                }
                definition.defaultWorkspace = workspace;
                const auto inferred = SkinLayoutEngine::resolve (definition, { 0, 0, 1240, 700 },
                                                                 2.0f, report);
                expect (inferred.has_value(), report.toText());
                if (inferred) expectEquals (inferred->root.nodeId, toString (workspace));
            }
        }

        beginTest ("rejects non-finite bounds, impossible minimums and sub-28 interactive controls");
        {
            SkinValidationReport report;
            expect (! SkinLayoutEngine::resolve (definitionWith (visual ("root")),
                                                 { 0, 0, std::numeric_limits<float>::infinity(), 100 },
                                                 1.0f, report));
            expect (report.hasCode ("LAYOUT017"));
        }
        {
            auto root = visual ("root");
            root.common.minWidth = 200;
            SkinValidationReport report;
            expect (! SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                 { 0, 0, 100, 100 }, 1.0f, report));
            expect (report.hasCode ("LAYOUT016"));
        }
        {
            auto root = flex (SkinLayoutNodeType::row, "root",
                              { component ("button", "library.add", pixels (20), pixels (20)) });
            SkinValidationReport report;
            expect (! SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                 { 0, 0, 100, 100 }, 1.0f, report));
            expect (report.hasCode ("LAYOUT015"));
        }
        {
            auto root = flex (SkinLayoutNodeType::row, "root",
                              { component ("label", "app.status", pixels (10), pixels (10)) });
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (std::move (root)),
                                                           { 0, 0, 100, 20 }, 1.0f, report);
            expect (result.has_value(), report.toText());
        }

        beginTest ("standard product size matrix always resolves to the exact supplied root bounds");
        for (const auto bounds : { juce::Rectangle<float> (0, 0, 1040, 700),
                                   juce::Rectangle<float> (0, 0, 1240, 800),
                                   juce::Rectangle<float> (0, 0, 1440, 900),
                                   juce::Rectangle<float> (0, 0, 1728, 1117) })
        {
            SkinValidationReport report;
            const auto result = SkinLayoutEngine::resolve (definitionWith (visual ("root")),
                                                           bounds, 1.0f, report);
            expect (result.has_value(), report.toText());
            if (result) expect (result->root.bounds == bounds);
        }
    }
};

SkinLayoutEngineTests skinLayoutEngineTests;
} // namespace
