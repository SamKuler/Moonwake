#include "skin/SkinLayoutCompiler.h"

#include "skin/PlayerComponentCatalog.h"
#include "skin/SkinSchemaValidator.h"

#include <juce_core/juce_core.h>

namespace
{
juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

juce::var array (std::vector<juce::var> values)
{
    juce::Array<juce::var> result;
    for (auto& value : values) result.add (std::move (value));
    return juce::var (std::move (result));
}

juce::var componentNode (const juce::String& nodeId, const juce::String& componentId,
                         int focusOrder = 0)
{
    auto node = object();
    node.getDynamicObject()->setProperty ("id", nodeId);
    node.getDynamicObject()->setProperty ("type", "component");
    node.getDynamicObject()->setProperty ("component", componentId);
    if (const auto* descriptor = PlayerComponentCatalog::find (componentId))
    {
        node.getDynamicObject()->setProperty ("style",
                                              descriptor->defaultRendererTarget + ".default");
        if (descriptor->presentations.contains ("linearHorizontal"))
            node.getDynamicObject()->setProperty ("presentation", "linearHorizontal");
        if (descriptor->interactive && focusOrder > 0)
            node.getDynamicObject()->setProperty ("focusOrder", focusOrder);
    }
    return node;
}

juce::var workspaceRoot (const juce::String& prefix)
{
    auto root = object();
    root.getDynamicObject()->setProperty ("id", prefix + "root");
    root.getDynamicObject()->setProperty ("type", "column");
    std::vector<juce::var> children;
    int focus = 10;
    for (const auto& id : { "library.add", "queue.table", "transport.previous",
                            "transport.playPause", "transport.next", "transport.seek",
                            "transport.time", "transport.volume", "settings.open",
                            "workspace.studio", "workspace.listening", "app.status" })
    {
        const auto* descriptor = PlayerComponentCatalog::find (id);
        children.push_back (componentNode (prefix + juce::String (id).replace (".", "-"),
                                           id, descriptor != nullptr && descriptor->interactive
                                                   ? focus : 0));
        if (descriptor != nullptr && descriptor->interactive) focus += 10;
    }
    root.getDynamicObject()->setProperty ("children", array (std::move (children)));
    return root;
}

juce::var layoutDocument (juce::var root)
{
    auto document = object();
    document.getDynamicObject()->setProperty ("apiVersion", 1);
    document.getDynamicObject()->setProperty ("root", std::move (root));
    return document;
}

juce::var visualNode (const juce::String& id)
{
    auto node = object();
    node.getDynamicObject()->setProperty ("id", id);
    node.getDynamicObject()->setProperty ("type", "visual");
    node.getDynamicObject()->setProperty ("style", "surface.card");
    node.getDynamicObject()->setProperty ("pointerEvents", "none");
    return node;
}

juce::var* directComponent (juce::var& root, const juce::String& id)
{
    auto* children = root.getProperty ("children", {}).getArray();
    if (children == nullptr) return nullptr;
    for (auto& child : *children)
        if (child.getProperty ("component", {}).toString() == id)
            return &child;
    return nullptr;
}

std::optional<SkinLayoutDefinition> compileLayout (const juce::var& raw,
                                                   SkinLayoutEntry entry,
                                                   SkinValidationReport& report)
{
    const auto validated = SkinSchemaValidator::validate (SkinSchemaDocument::layout,
                                                           raw, "layout.json", report);
    if (! validated) return std::nullopt;
    return SkinLayoutCompiler::compile (*validated, entry, report);
}

class SkinLayoutCompilerTests final : public juce::UnitTest
{
public:
    SkinLayoutCompilerTests() : UnitTest ("SkinLayoutCompiler") {}

    void runTest() override
    {
        beginTest ("parses all seven node types and typed percent/fr lengths");
        {
            auto root = workspaceRoot ("base-");
            auto* children = root.getProperty ("children", {}).getArray();
            auto row = object();
            row.getDynamicObject()->setProperty ("id", "row");
            row.getDynamicObject()->setProperty ("type", "row");
            auto rowVisual = visualNode ("row-visual");
            rowVisual.getDynamicObject()->setProperty ("width", "36%");
            row.getDynamicObject()->setProperty ("children", array ({ rowVisual }));
            children->add (row);

            auto grid = object();
            grid.getDynamicObject()->setProperty ("id", "grid");
            grid.getDynamicObject()->setProperty ("type", "grid");
            grid.getDynamicObject()->setProperty ("columns", array ({ "1fr", "2fr" }));
            grid.getDynamicObject()->setProperty ("rows", array ({ "auto" }));
            auto gridVisual = visualNode ("grid-visual");
            gridVisual.getDynamicObject()->setProperty ("column", 2);
            gridVisual.getDynamicObject()->setProperty ("row", 1);
            grid.getDynamicObject()->setProperty ("children", array ({ gridVisual }));
            children->add (grid);

            auto stack = object();
            stack.getDynamicObject()->setProperty ("id", "stack");
            stack.getDynamicObject()->setProperty ("type", "stack");
            auto anchored = visualNode ("anchored");
            auto anchors = object();
            anchors.getDynamicObject()->setProperty ("left", 4);
            anchors.getDynamicObject()->setProperty ("top", "10%");
            anchored.getDynamicObject()->setProperty ("anchors", anchors);
            stack.getDynamicObject()->setProperty ("children", array ({ anchored }));
            children->add (stack);

            auto scroll = object();
            scroll.getDynamicObject()->setProperty ("id", "scroll");
            scroll.getDynamicObject()->setProperty ("type", "scroll");
            scroll.getDynamicObject()->setProperty ("direction", "vertical");
            scroll.getDynamicObject()->setProperty ("child", visualNode ("scroll-child"));
            children->add (scroll);

            SkinValidationReport report;
            const auto compiled = compileLayout (layoutDocument (root), SkinLayoutEntry::studio,
                                                 report);
            expect (compiled.has_value(), report.toText());
            if (compiled)
            {
                expect (compiled->base.type() == SkinLayoutNodeType::column);
                const auto& base = std::get<SkinFlexLayout> (compiled->base.payload);
                expect (base.children.size() == 16);
                const auto& parsedRow = std::get<SkinFlexLayout> (base.children[12].payload);
                expect (parsedRow.children[0].common.width.unit == SkinLength::Unit::percent
                        && parsedRow.children[0].common.width.value == 36.0f);
                const auto& parsedGrid = std::get<SkinGridLayout> (base.children[13].payload);
                expect (parsedGrid.columns[0].unit == SkinLength::Unit::fraction
                        && parsedGrid.columns[1].value == 2.0f);
                expect (base.children[14].type() == SkinLayoutNodeType::stack);
                expect (base.children[15].type() == SkinLayoutNodeType::scroll);
            }
        }

        beginTest ("rejects duplicate IDs, invalid anchors, grid overflow and bad min/max");
        {
            auto root = workspaceRoot ("duplicate-");
            root.getProperty ("children", {}).getArray()->add (visualNode ("duplicate-root"));
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT002"));
        }
        {
            auto root = workspaceRoot ("anchor-");
            auto* node = directComponent (root, "library.add");
            auto anchors = object();
            anchors.getDynamicObject()->setProperty ("left", 0);
            if (node) node->getDynamicObject()->setProperty ("anchors", anchors);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT008"));
        }
        {
            auto root = workspaceRoot ("grid-overflow-");
            auto grid = object();
            grid.getDynamicObject()->setProperty ("id", "overflow-grid");
            grid.getDynamicObject()->setProperty ("type", "grid");
            grid.getDynamicObject()->setProperty ("columns", array ({ "1fr" }));
            grid.getDynamicObject()->setProperty ("rows", array ({ "1fr" }));
            auto child = visualNode ("overflow-child");
            child.getDynamicObject()->setProperty ("column", 1);
            child.getDynamicObject()->setProperty ("columnSpan", 2);
            child.getDynamicObject()->setProperty ("row", 1);
            grid.getDynamicObject()->setProperty ("children", array ({ child }));
            root.getProperty ("children", {}).getArray()->add (grid);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT007"));
        }
        {
            auto root = workspaceRoot ("overconstrained-");
            auto stack = object();
            stack.getDynamicObject()->setProperty ("id", "over-stack");
            stack.getDynamicObject()->setProperty ("type", "stack");
            auto child = visualNode ("over-child");
            child.getDynamicObject()->setProperty ("width", 100);
            auto anchors = object();
            anchors.getDynamicObject()->setProperty ("left", 0);
            anchors.getDynamicObject()->setProperty ("right", 0);
            child.getDynamicObject()->setProperty ("anchors", anchors);
            stack.getDynamicObject()->setProperty ("children", array ({ child }));
            root.getProperty ("children", {}).getArray()->add (stack);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT008"));
        }
        {
            auto root = workspaceRoot ("minmax-");
            auto* node = directComponent (root, "library.add");
            if (node)
            {
                node->getDynamicObject()->setProperty ("minWidth", 100);
                node->getDynamicObject()->setProperty ("maxWidth", 20);
            }
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT005"));
        }

        beginTest ("enforces depth 32 and the 5000 declared-node budget");
        {
            auto nested = workspaceRoot ("deep-leaf-");
            for (int i = 0; i < 32; ++i)
            {
                auto parent = object();
                parent.getDynamicObject()->setProperty ("id", "deep-" + juce::String (i));
                parent.getDynamicObject()->setProperty ("type", "column");
                parent.getDynamicObject()->setProperty ("children", array ({ nested }));
                nested = parent;
            }
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (nested), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT003"));
        }
        {
            auto root = workspaceRoot ("budget-");
            for (int group = 0; group < 5; ++group)
            {
                auto container = object();
                container.getDynamicObject()->setProperty ("id", "budget-group-" + juce::String (group));
                container.getDynamicObject()->setProperty ("type", "row");
                std::vector<juce::var> decorations;
                for (int i = 0; i < 1000; ++i)
                    decorations.push_back (visualNode ("budget-" + juce::String (group)
                                                       + "-" + juce::String (i)));
                container.getDynamicObject()->setProperty ("children", array (std::move (decorations)));
                root.getProperty ("children", {}).getArray()->add (container);
            }
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT004"));
        }

        beginTest ("breakpoint ranges are inclusive at every exact boundary and first-match ordered");
        {
            auto document = layoutDocument (workspaceRoot ("base-"));
            auto breakpoint = object();
            auto when = object();
            when.getDynamicObject()->setProperty ("minWidth", 1000);
            when.getDynamicObject()->setProperty ("maxWidth", 1200);
            when.getDynamicObject()->setProperty ("minHeight", 700);
            when.getDynamicObject()->setProperty ("maxHeight", 900);
            when.getDynamicObject()->setProperty ("minScaleFactor", 1.0);
            when.getDynamicObject()->setProperty ("maxScaleFactor", 2.0);
            breakpoint.getDynamicObject()->setProperty ("when", when);
            breakpoint.getDynamicObject()->setProperty ("root", workspaceRoot ("bp-"));
            document.getDynamicObject()->setProperty ("breakpoints", array ({ breakpoint }));
            SkinValidationReport report;
            const auto compiled = compileLayout (document, SkinLayoutEntry::listening, report);
            expect (compiled.has_value(), report.toText());
            if (compiled)
            {
                const auto& condition = compiled->breakpoints[0];
                expect (condition.widths.contains (1000.0f) && condition.widths.contains (1200.0f));
                expect (! condition.widths.contains (999.999f) && ! condition.widths.contains (1200.001f));
                expect (condition.heights.contains (700.0f) && condition.heights.contains (900.0f));
                expect (condition.scaleFactors.contains (1.0f)
                        && condition.scaleFactors.contains (2.0f));
            }
        }
        {
            auto document = layoutDocument (workspaceRoot ("order-base-"));
            auto broad = object();
            auto broadWhen = object();
            broadWhen.getDynamicObject()->setProperty ("maxWidth", 1200);
            broad.getDynamicObject()->setProperty ("when", broadWhen);
            broad.getDynamicObject()->setProperty ("root", workspaceRoot ("order-broad-"));
            auto narrow = object();
            auto narrowWhen = object();
            narrowWhen.getDynamicObject()->setProperty ("maxWidth", 1100);
            narrow.getDynamicObject()->setProperty ("when", narrowWhen);
            narrow.getDynamicObject()->setProperty ("root", workspaceRoot ("order-narrow-"));
            document.getDynamicObject()->setProperty ("breakpoints", array ({ broad, narrow }));
            SkinValidationReport report;
            expect (! compileLayout (document, SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT010"));
        }
        {
            auto document = layoutDocument (workspaceRoot ("complete-base-"));
            auto breakpoint = object();
            auto when = object();
            when.getDynamicObject()->setProperty ("maxWidth", 1000);
            auto incomplete = workspaceRoot ("incomplete-bp-");
            incomplete.getProperty ("children", {}).getArray()->remove (0);
            breakpoint.getDynamicObject()->setProperty ("when", when);
            breakpoint.getDynamicObject()->setProperty ("root", incomplete);
            document.getDynamicObject()->setProperty ("breakpoints", array ({ breakpoint }));
            SkinValidationReport report;
            expect (! compileLayout (document, SkinLayoutEntry::studio, report));
            expect (report.hasCode ("COMPONENT004"));
        }

        beginTest ("enforces Component singletons, required visibility, targets and focus order");
        {
            auto root = workspaceRoot ("unknown-");
            if (auto* node = directComponent (root, "library.add"))
                node->getDynamicObject()->setProperty ("component", "unknown.component");
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("SCHEMA003") || report.hasCode ("SCHEMA005")
                    || report.hasCode ("COMPONENT001"));
        }
        {
            auto root = workspaceRoot ("missing-");
            auto* children = root.getProperty ("children", {}).getArray();
            children->remove (0);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("COMPONENT004"));
        }
        {
            auto root = workspaceRoot ("duplicate-component-");
            root.getProperty ("children", {}).getArray()->add (
                componentNode ("second-add", "library.add", 999));
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("COMPONENT003"));
        }
        {
            auto root = workspaceRoot ("hidden-");
            if (auto* node = directComponent (root, "library.add"))
                node->getDynamicObject()->setProperty ("visible", false);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT011"));
        }
        {
            auto root = workspaceRoot ("zero-");
            if (auto* node = directComponent (root, "library.add"))
                node->getDynamicObject()->setProperty ("width", 0);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT011"));
        }
        {
            auto root = workspaceRoot ("style-");
            if (auto* node = directComponent (root, "library.add"))
                node->getDynamicObject()->setProperty ("style", "label.default");
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT013"));
        }
        {
            auto root = workspaceRoot ("presentation-");
            if (auto* node = directComponent (root, "transport.seek"))
                node->getDynamicObject()->setProperty ("presentation", "rotary");
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT014"));
        }
        {
            auto root = workspaceRoot ("missing-focus-");
            if (auto* node = directComponent (root, "library.add"))
                node->getDynamicObject()->removeProperty ("focusOrder");
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT012"));
        }
        {
            auto root = workspaceRoot ("duplicate-focus-");
            auto* first = directComponent (root, "library.add");
            auto* second = directComponent (root, "queue.table");
            if (first && second)
                second->getDynamicObject()->setProperty (
                    "focusOrder", first->getProperty ("focusOrder", {}));
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("LAYOUT012"));
        }

        beginTest ("schema rejects negative lengths and scroll objects with extra children");
        {
            auto root = workspaceRoot ("negative-");
            if (auto* node = directComponent (root, "library.add"))
                node->getDynamicObject()->setProperty ("width", -1);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("SCHEMA004") || report.hasCode ("SCHEMA005"));
        }
        {
            auto root = workspaceRoot ("scroll-shape-");
            auto scroll = object();
            scroll.getDynamicObject()->setProperty ("id", "bad-scroll");
            scroll.getDynamicObject()->setProperty ("type", "scroll");
            scroll.getDynamicObject()->setProperty ("direction", "vertical");
            scroll.getDynamicObject()->setProperty ("child", visualNode ("one-child"));
            scroll.getDynamicObject()->setProperty ("children", array ({ visualNode ("extra-child") }));
            root.getProperty ("children", {}).getArray()->add (scroll);
            SkinValidationReport report;
            expect (! compileLayout (layoutDocument (root), SkinLayoutEntry::studio, report));
            expect (report.hasCode ("SCHEMA001") || report.hasCode ("SCHEMA005"));
        }
    }
};

SkinLayoutCompilerTests skinLayoutCompilerTests;
} // namespace
