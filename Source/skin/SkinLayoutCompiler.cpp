#include "SkinLayoutCompiler.h"
#include "SkinExpression.h"

#include <algorithm>
#include <set>

namespace
{
enum class ParentKind { none, flex, grid, stack, scroll };

struct ParentContext
{
    ParentKind kind = ParentKind::none;
    int gridColumns = 0, gridRows = 0;
};

struct TreeState
{
    std::set<juce::String> components;
    std::set<juce::String> visibleComponents;
    std::set<int> focusOrders;
};

bool hasProperty (const juce::var& object, const char* name)
{
    return object.isObject() && object.getDynamicObject()->hasProperty (name);
}

float number (const juce::var& object, const char* name, float fallback)
{
    if (! hasProperty (object, name)) return fallback;
    return static_cast<float> (static_cast<double> (object.getProperty (name, {})));
}

SkinLength parseLength (const juce::var& value)
{
    if (value.isVoid() || value.toString() == "auto") return {};
    if (value.isInt() || value.isInt64() || value.isDouble())
        return { SkinLength::Unit::pixels, static_cast<float> (static_cast<double> (value)) };
    const auto text = value.toString();
    if (text.endsWithChar ('%'))
        return { SkinLength::Unit::percent, text.dropLastCharacters (1).getFloatValue() };
    if (text.endsWith ("fr"))
        return { SkinLength::Unit::fraction, text.dropLastCharacters (2).getFloatValue() };
    return {};
}

SkinInsets parseInsets (const juce::var& value)
{
    SkinInsets result;
    if (value.isInt() || value.isInt64() || value.isDouble())
    {
        result.top = result.right = result.bottom = result.left = parseLength (value);
        return result;
    }
    if (! value.isObject()) return result;
    result.top = parseLength (value.getProperty ("top", 0));
    result.right = parseLength (value.getProperty ("right", 0));
    result.bottom = parseLength (value.getProperty ("bottom", 0));
    result.left = parseLength (value.getProperty ("left", 0));
    return result;
}

SkinAlign parseAlign (const juce::String& value, SkinAlign fallback)
{
    if (value == "auto") return SkinAlign::automatic;
    if (value == "start") return SkinAlign::start;
    if (value == "center") return SkinAlign::centre;
    if (value == "end") return SkinAlign::end;
    if (value == "stretch") return SkinAlign::stretch;
    return fallback;
}

SkinJustify parseJustify (const juce::String& value)
{
    if (value == "center") return SkinJustify::centre;
    if (value == "end") return SkinJustify::end;
    if (value == "spaceBetween") return SkinJustify::spaceBetween;
    if (value == "spaceAround") return SkinJustify::spaceAround;
    return SkinJustify::start;
}

bool isExplicitZero (const SkinLength& value)
{
    return value.unit != SkinLength::Unit::automatic && value.value == 0.0f;
}

class Compiler
{
public:
    Compiler (SkinLayoutEntry entryIn, SkinValidationReport& reportIn, bool queueOpenIn)
        : entry (entryIn), report (reportIn), queueOpen (queueOpenIn)
    {
    }

    std::optional<SkinLayoutDefinition> run (const juce::var& document)
    {
        SkinLayoutDefinition result;
        result.defaultWorkspace = entry == SkinLayoutEntry::studio
            ? WorkspaceMode::studio : WorkspaceMode::listening;
        if (! parseAncillaryStyles (document, result.ancillaryStyles, {})) return std::nullopt;
        TreeState baseState;
        auto base = parseNode (document.getProperty ("root", {}), 1, {}, baseState,
                               true, 1.0f, false);
        if (! base || ! validateRequired (baseState)) return std::nullopt;
        result.base = std::move (*base);

        if (const auto* breakpoints = document.getProperty ("breakpoints", {}).getArray())
        {
            for (int i = 0; i < breakpoints->size(); ++i)
            {
                const auto& raw = breakpoints->getReference (i);
                SkinLayoutDefinition::Breakpoint breakpoint;
                if (! parseAncillaryStyles (raw, breakpoint.ancillaryStyles,
                                             "/breakpoints/" + juce::String (i))) return std::nullopt;
                const auto when = raw.getProperty ("when", {});
                breakpoint.widths = parseRange (when, "minWidth", "maxWidth");
                breakpoint.heights = parseRange (when, "minHeight", "maxHeight");
                breakpoint.scaleFactors = parseRange (when, "minScaleFactor", "maxScaleFactor");
                if (hasProperty (when, "workspace"))
                    breakpoint.workspace = workspaceModeFromString (
                        when.getProperty ("workspace", {}).toString());
                if (! validRange (breakpoint.widths) || ! validRange (breakpoint.heights)
                    || ! validRange (breakpoint.scaleFactors))
                {
                    fail ("LAYOUT010", "/breakpoints/" + juce::String (i) + "/when",
                          "Breakpoint minimum exceeds its maximum.");
                    return std::nullopt;
                }
                const auto coveredIn = [&] (WorkspaceMode workspace)
                {
                    if (breakpoint.workspace && *breakpoint.workspace != workspace) return true;
                    return std::any_of (result.breakpoints.begin(), result.breakpoints.end(),
                        [&] (const auto& earlier)
                        {
                            return (! earlier.workspace || *earlier.workspace == workspace)
                                && containsRange (earlier.widths, breakpoint.widths)
                                && containsRange (earlier.heights, breakpoint.heights)
                                && containsRange (earlier.scaleFactors, breakpoint.scaleFactors);
                        });
                };
                if (coveredIn (WorkspaceMode::studio) && coveredIn (WorkspaceMode::listening))
                {
                    fail ("LAYOUT010", "/breakpoints/" + juce::String (i) + "/when",
                          "Breakpoint is covered by earlier ranges in every workspace it matches.");
                    return std::nullopt;
                }

                TreeState state;
                auto root = parseNode (raw.getProperty ("root", {}), 1, {}, state,
                                       true, 1.0f, false);
                if (! root || ! validateRequired (state)) return std::nullopt;
                breakpoint.root = std::move (*root);
                result.breakpoints.push_back (std::move (breakpoint));
            }
        }
        return result;
    }

private:
    bool parseAncillaryStyles (const juce::var& document,
                               std::map<juce::String, juce::String>& styles,
                               const juce::String& pointer)
    {
        const auto raw = document.getProperty ("ancillaryStyles", {});
        if (const auto* object = raw.getDynamicObject())
            for (const auto& property : object->getProperties())
            {
                const auto target = property.name.toString();
                const auto style = property.value.toString();
                if (! style.startsWith (target + "."))
                {
                    fail ("LAYOUT013", pointer + "/ancillaryStyles/" + target,
                          "Ancillary style must name a renderer of its declared target.");
                    return false;
                }
                styles.emplace (target, style);
            }
        return true;
    }

    std::optional<SkinLayoutNode> parseNode (
        const juce::var& value, int depth, ParentContext parent, TreeState& state,
        bool ancestorVisible, float ancestorOpacity, bool ancestorZero)
    {
        if (depth > 32)
        {
            fail ("LAYOUT003", {}, "Layout tree exceeds depth 32.");
            return std::nullopt;
        }
        if (++nodeCount > 5000)
        {
            fail ("LAYOUT004", {}, "Layout file declares more than 5000 nodes.");
            return std::nullopt;
        }

        SkinLayoutNode node;
        if (! parseCommon (value, node.common)) return std::nullopt;
        if (! nodeIds.insert (node.common.id).second)
        {
            fail ("LAYOUT002", node.common.id, "Layout node id is duplicated.");
            return std::nullopt;
        }
        if (parent.kind != ParentKind::stack && hasProperty (value, "anchors"))
        {
            fail ("LAYOUT008", node.common.id, "Anchors are valid only on direct stack children.");
            return std::nullopt;
        }
        const auto hasGridPlacement = hasProperty (value, "column") || hasProperty (value, "row")
                                   || hasProperty (value, "columnSpan") || hasProperty (value, "rowSpan");
        if (parent.kind != ParentKind::grid && hasGridPlacement)
        {
            fail ("LAYOUT007", node.common.id,
                  "Grid placement fields are valid only on direct grid children.");
            return std::nullopt;
        }
        if (parent.kind == ParentKind::grid && ! validateGridPlacement (node.common, parent))
            return std::nullopt;
        if (parent.kind == ParentKind::stack && ! validateStackAnchors (node.common))
            return std::nullopt;

        const auto visible = ancestorVisible && node.common.visible
            && (! node.common.visibleWhen || (*node.common.visibleWhen)[queueOpen ? 1 : 0]);
        const auto opacity = ancestorOpacity * node.common.opacity;
        const auto zero = ancestorZero || isExplicitZero (node.common.width)
                       || isExplicitZero (node.common.height)
                       || node.common.maxWidth == 0.0f || node.common.maxHeight == 0.0f;
        const auto type = value.getProperty ("type", {}).toString();
        if (type == "row" || type == "column")
        {
            node.nodeType = type == "row" ? SkinLayoutNodeType::row
                                           : SkinLayoutNodeType::column;
            SkinFlexLayout layout;
            layout.gap = number (value, "gap", 0.0f);
            layout.align = parseAlign (value.getProperty ("align", "stretch").toString(),
                                       SkinAlign::stretch);
            layout.justify = parseJustify (value.getProperty ("justify", "start").toString());
            if (! parseChildren (value.getProperty ("children", {}), depth, { ParentKind::flex },
                                 state, visible, opacity, zero, layout.children))
                return std::nullopt;
            node.payload = std::move (layout);
        }
        else if (type == "grid")
        {
            node.nodeType = SkinLayoutNodeType::grid;
            SkinGridLayout layout;
            for (const auto& track : *value.getProperty ("columns", {}).getArray())
                layout.columns.push_back (parseLength (track));
            for (const auto& track : *value.getProperty ("rows", {}).getArray())
                layout.rows.push_back (parseLength (track));
            layout.columnGap = number (value, "columnGap", 0.0f);
            layout.rowGap = number (value, "rowGap", 0.0f);
            ParentContext childContext { ParentKind::grid,
                                         static_cast<int> (layout.columns.size()),
                                         static_cast<int> (layout.rows.size()) };
            if (! parseChildren (value.getProperty ("children", {}), depth, childContext,
                                 state, visible, opacity, zero, layout.children))
                return std::nullopt;
            node.payload = std::move (layout);
        }
        else if (type == "stack")
        {
            node.nodeType = SkinLayoutNodeType::stack;
            SkinStackLayout layout;
            if (! parseChildren (value.getProperty ("children", {}), depth, { ParentKind::stack },
                                 state, visible, opacity, zero, layout.children))
                return std::nullopt;
            node.payload = std::move (layout);
        }
        else if (type == "scroll")
        {
            node.nodeType = SkinLayoutNodeType::scroll;
            SkinScrollLayout layout;
            layout.direction = value.getProperty ("direction", {}).toString() == "horizontal"
                ? SkinScrollLayout::Direction::horizontal : SkinScrollLayout::Direction::vertical;
            auto child = parseNode (value.getProperty ("child", {}), depth + 1,
                                    { ParentKind::scroll }, state, visible, opacity, zero);
            if (! child) return std::nullopt;
            layout.child = std::make_unique<SkinLayoutNode> (std::move (*child));
            node.payload = std::move (layout);
        }
        else if (type == "component")
        {
            node.nodeType = SkinLayoutNodeType::component;
            SkinComponentLayout layout;
            layout.componentId = value.getProperty ("component", {}).toString();
            layout.presentation = value.getProperty ("presentation", {}).toString();
            layout.focusOrder = static_cast<int> (value.getProperty ("focusOrder", 0));
            if (! validateComponent (node.common, layout, state, visible, opacity, zero))
                return std::nullopt;
            node.payload = std::move (layout);
        }
        else
        {
            node.nodeType = SkinLayoutNodeType::visual;
            if (! node.common.style.startsWith ("surface."))
            {
                fail ("LAYOUT013", node.common.id,
                      "Visual nodes must use a surface renderer target.");
                return std::nullopt;
            }
            node.payload = SkinVisualLayout {};
        }

        if (type != "component" && type != "visual" && node.common.style.isNotEmpty()
            && ! node.common.style.startsWith ("surface."))
        {
            fail ("LAYOUT013", node.common.id,
                  "Container styles must use a surface renderer target.");
            return std::nullopt;
        }
        return node;
    }

    bool parseCommon (const juce::var& value, SkinLayoutCommon& common)
    {
        common.id = value.getProperty ("id", {}).toString();
        common.style = value.getProperty ("style", {}).toString();
        common.width = parseLength (value.getProperty ("width", {}));
        common.height = parseLength (value.getProperty ("height", {}));
        common.minWidth = number (value, "minWidth", 0.0f);
        common.minHeight = number (value, "minHeight", 0.0f);
        common.maxWidth = number (value, "maxWidth", std::numeric_limits<float>::infinity());
        common.maxHeight = number (value, "maxHeight", std::numeric_limits<float>::infinity());
        common.grow = number (value, "grow", 0.0f);
        common.shrink = number (value, "shrink", 1.0f);
        common.margin = parseInsets (value.getProperty ("margin", {}));
        common.padding = parseInsets (value.getProperty ("padding", {}));
        common.alignSelf = parseAlign (value.getProperty ("alignSelf", "auto").toString(),
                                       SkinAlign::automatic);
        common.aspectRatio = number (value, "aspectRatio", 0.0f);
        common.opacity = number (value, "opacity", 1.0f);
        common.clip = static_cast<bool> (value.getProperty ("clip", false));
        common.zIndex = static_cast<int> (value.getProperty ("zIndex", 0));
        common.visible = static_cast<bool> (value.getProperty ("visible", true));
        if (hasProperty (value, "visibleWhen"))
        {
            const auto expression = SkinExpression::compile (value.getProperty ("visibleWhen", {}).toString(),
                { { "view.queueOpen", SkinExpressionType::boolean } }, SkinTokenStore {}, report,
                "layout.json", common.id + "/visibleWhen");
            if (! expression) return false;
            const auto off = expression->evaluateWithBooleanContext ("view.queueOpen", false);
            const auto on = expression->evaluateWithBooleanContext ("view.queueOpen", true);
            if (expression->resultType() != SkinExpressionType::boolean || ! off || ! on)
            {
                fail ("LAYOUT018", common.id, "visibleWhen must evaluate to a boolean in every layout view state.");
                return false;
            }
            common.visibleWhen = std::array<bool, 2> { std::get<bool> (*off), std::get<bool> (*on) };
        }
        common.gridColumn = static_cast<int> (value.getProperty ("column", 0));
        common.gridRow = static_cast<int> (value.getProperty ("row", 0));
        common.columnSpan = static_cast<int> (value.getProperty ("columnSpan", 1));
        common.rowSpan = static_cast<int> (value.getProperty ("rowSpan", 1));
        if (common.minWidth > common.maxWidth || common.minHeight > common.maxHeight)
        {
            fail ("LAYOUT005", common.id, "Layout min constraint exceeds max constraint.");
            return false;
        }
        const auto anchors = value.getProperty ("anchors", {});
        if (anchors.isObject())
        {
            if (hasProperty (anchors, "top")) common.anchors.top = parseLength (anchors["top"]);
            if (hasProperty (anchors, "right")) common.anchors.right = parseLength (anchors["right"]);
            if (hasProperty (anchors, "bottom")) common.anchors.bottom = parseLength (anchors["bottom"]);
            if (hasProperty (anchors, "left")) common.anchors.left = parseLength (anchors["left"]);
            if (hasProperty (anchors, "centerX")) common.anchors.centreX = number (anchors, "centerX", 0);
            if (hasProperty (anchors, "centerY")) common.anchors.centreY = number (anchors, "centerY", 0);
        }
        return true;
    }

    bool parseChildren (const juce::var& raw, int parentDepth, ParentContext parent,
                        TreeState& state, bool visible, float opacity, bool zero,
                        std::vector<SkinLayoutNode>& result)
    {
        const auto* children = raw.getArray();
        jassert (children != nullptr);
        if (children == nullptr) return false;
        for (const auto& child : *children)
        {
            auto parsed = parseNode (child, parentDepth + 1, parent, state,
                                     visible, opacity, zero);
            if (! parsed) return false;
            result.push_back (std::move (*parsed));
        }
        return true;
    }

    bool validateGridPlacement (const SkinLayoutCommon& common, ParentContext parent)
    {
        if ((common.gridColumn == 0 && common.columnSpan != 1)
            || (common.gridRow == 0 && common.rowSpan != 1)
            || (common.gridColumn > 0
                && common.gridColumn + common.columnSpan - 1 > parent.gridColumns)
            || (common.gridRow > 0 && common.gridRow + common.rowSpan - 1 > parent.gridRows))
        {
            fail ("LAYOUT007", common.id, "Grid placement or span exceeds declared tracks.");
            return false;
        }
        return true;
    }

    bool validateStackAnchors (const SkinLayoutCommon& common)
    {
        const auto horizontalEdges = static_cast<int> (common.anchors.left.has_value())
                                   + static_cast<int> (common.anchors.right.has_value());
        const auto verticalEdges = static_cast<int> (common.anchors.top.has_value())
                                 + static_cast<int> (common.anchors.bottom.has_value());
        if ((horizontalEdges == 2 && common.width.unit == SkinLength::Unit::pixels)
            || (verticalEdges == 2 && common.height.unit == SkinLength::Unit::pixels)
            || (common.anchors.centreX && horizontalEdges > 0)
            || (common.anchors.centreY && verticalEdges > 0))
        {
            fail ("LAYOUT008", common.id, "Stack anchors over-constrain an axis.");
            return false;
        }
        return true;
    }

    bool validateComponent (const SkinLayoutCommon& common, const SkinComponentLayout& component,
                            TreeState& state, bool visible, float opacity, bool zero)
    {
        const auto* descriptor = PlayerComponentCatalog::find (component.componentId);
        if (descriptor == nullptr)
        {
            fail ("COMPONENT001", common.id, "Layout references an unknown Component ID.");
            return false;
        }
        if (! state.components.insert (component.componentId).second)
        {
            fail ("COMPONENT003", common.id, "Component ID appears more than once in one root.");
            return false;
        }
        auto expectedTarget = descriptor->defaultRendererTarget;
        if (component.presentation.isNotEmpty())
        {
            const auto target = descriptor->rendererTargetByPresentation.find (component.presentation);
            if (! descriptor->presentations.contains (component.presentation)
                || target == descriptor->rendererTargetByPresentation.end())
            {
                fail ("LAYOUT014", common.id, "Component presentation is not supported.");
                return false;
            }
            expectedTarget = target->second;
        }
        if (common.style.isNotEmpty()
            && common.style.upToFirstOccurrenceOf (".", false, false) != expectedTarget)
        {
            fail ("LAYOUT013", common.id, "Style renderer target does not match the component.");
            return false;
        }
        if (descriptor->interactive)
        {
            if (component.focusOrder <= 0 || ! state.focusOrders.insert (component.focusOrder).second)
            {
                fail ("LAYOUT012", common.id,
                      "Interactive components require a unique positive focusOrder.");
                return false;
            }
        }
        if (visible && opacity > 0.0f && ! zero)
            state.visibleComponents.insert (component.componentId);
        return true;
    }

    bool validateRequired (const TreeState& state)
    {
        if (state.components.contains ("queue.toggle") && ! state.visibleComponents.contains ("queue.toggle"))
        {
            fail ("LAYOUT011", "queue.toggle", "The queue toggle must remain reachable in both view states.");
            return false;
        }
        if (queueOpen && (state.components.contains ("queue.toggle")
                          || state.components.contains ("library.browseQueue"))
            && ! state.visibleComponents.contains ("queue.table"))
        {
            fail ("LAYOUT011", "queue.table", "A queue opener requires a visible queue table when view.queueOpen is true.");
            return false;
        }
        if (! queueOpen && state.components.contains ("library.browseQueue")
            && ! state.visibleComponents.contains ("queue.table")
            && ! state.visibleComponents.contains ("queue.toggle"))
        {
            fail ("LAYOUT011", "queue.toggle", "A collapsible queue opened by library.browseQueue requires a visible queue toggle.");
            return false;
        }
        for (const auto& descriptor : PlayerComponentCatalog::descriptors())
            if (descriptor.requiredIn.contains (entry))
            {
                auto alternatives = descriptor.requiredAlternatives;
                alternatives.add (descriptor.id);
                const auto containsAny = [&alternatives] (const auto& components)
                {
                    return std::any_of (alternatives.begin(), alternatives.end(),
                                        [&components] (const auto& id) { return components.contains (id); });
                };
                if (! containsAny (state.components))
                {
                    fail ("COMPONENT004", descriptor.id,
                          "Final layout root is missing a required component or its supported alternative.");
                    return false;
                }
                if (! containsAny (state.visibleComponents))
                {
                    if (descriptor.id == "queue.table" && ! queueOpen
                        && state.visibleComponents.contains ("queue.toggle")) continue;
                    fail ("LAYOUT011", descriptor.id,
                          "Required component is statically hidden, transparent or zero-sized.");
                    return false;
                }
            }
        return true;
    }

    static SkinLayoutDefinition::InclusiveRange parseRange (
        const juce::var& value, const char* minimum, const char* maximum)
    {
        SkinLayoutDefinition::InclusiveRange result;
        if (hasProperty (value, minimum)) result.minimum = number (value, minimum, 0);
        if (hasProperty (value, maximum)) result.maximum = number (value, maximum, 0);
        return result;
    }

    static bool validRange (const SkinLayoutDefinition::InclusiveRange& range)
    {
        return ! range.minimum || ! range.maximum || *range.minimum <= *range.maximum;
    }

    static bool containsRange (const SkinLayoutDefinition::InclusiveRange& outer,
                               const SkinLayoutDefinition::InclusiveRange& inner)
    {
        const auto minimumContained = ! outer.minimum
            || (inner.minimum && *outer.minimum <= *inner.minimum);
        const auto maximumContained = ! outer.maximum
            || (inner.maximum && *outer.maximum >= *inner.maximum);
        return minimumContained && maximumContained;
    }

    void fail (juce::String code, juce::String pointer, juce::String message)
    {
        report.addError (std::move (code), "layout.json", std::move (pointer),
                         std::move (message));
    }

    SkinLayoutEntry entry;
    SkinValidationReport& report;
    std::set<juce::String> nodeIds;
    int nodeCount = 0;
    bool queueOpen;
};
} // namespace

std::optional<SkinLayoutDefinition> SkinLayoutCompiler::compile (
    const SkinValidatedDocument& document, SkinLayoutEntry entry,
    SkinValidationReport& report)
{
    if (document.kind() != SkinSchemaDocument::layout)
    {
        report.addError ("LAYOUT001", "layout.json", {},
                         "SkinLayoutCompiler requires a validated layout document.");
        return std::nullopt;
    }
    if (! Compiler (entry, report, true).run (document.value())) return std::nullopt;
    return Compiler (entry, report, false).run (document.value());
}
