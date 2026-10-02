#include "SkinLayoutEngine.h"
#include "CompiledSkin.h"
#include "SkinControlStyle.h"

#include "PlayerComponentCatalog.h"

#include <algorithm>
#include <cmath>

namespace
{
struct Insets
{
    float top = 0, right = 0, bottom = 0, left = 0;
    [[nodiscard]] float horizontal() const { return left + right; }
    [[nodiscard]] float vertical() const { return top + bottom; }
};

struct Measured
{
    float minimumWidth = 0, minimumHeight = 0;
    float preferredWidth = 0, preferredHeight = 0;
};

float pixelValue (const SkinLength& value)
{
    return value.unit == SkinLength::Unit::pixels ? value.value : 0.0f;
}

Insets resolveInsets (const SkinInsets& value)
{
    return { pixelValue (value.top), pixelValue (value.right),
             pixelValue (value.bottom), pixelValue (value.left) };
}

float resolveLength (const SkinLength& value, float parentExtent, float automatic)
{
    switch (value.unit)
    {
        case SkinLength::Unit::automatic: return automatic;
        case SkinLength::Unit::pixels: return value.value;
        case SkinLength::Unit::percent: return parentExtent * value.value * 0.01f;
        case SkinLength::Unit::fraction: return automatic;
    }
    return automatic;
}

float resolveAnchor (const SkinLength& value, float parentExtent)
{
    return value.unit == SkinLength::Unit::percent
        ? parentExtent * value.value * 0.01f : pixelValue (value);
}

bool finiteRectangle (juce::Rectangle<float> rectangle)
{
    return std::isfinite (rectangle.getX()) && std::isfinite (rectangle.getY())
        && std::isfinite (rectangle.getWidth()) && std::isfinite (rectangle.getHeight())
        && rectangle.getWidth() >= 0 && rectangle.getHeight() >= 0;
}

juce::Rectangle<float> contentRectangle (juce::Rectangle<float> bounds, Insets padding)
{
    return { bounds.getX() + padding.left, bounds.getY() + padding.top,
             std::max (0.0f, bounds.getWidth() - padding.horizontal()),
             std::max (0.0f, bounds.getHeight() - padding.vertical()) };
}

juce::String componentTarget (const SkinLayoutNode& node)
{
    if (node.type() != SkinLayoutNodeType::component) return {};
    const auto& component = std::get<SkinComponentLayout> (node.payload);
    const auto* descriptor = PlayerComponentCatalog::find (component.componentId);
    if (descriptor == nullptr) return {};
    if (component.presentation.isNotEmpty())
    {
        const auto found = descriptor->rendererTargetByPresentation.find (component.presentation);
        if (found != descriptor->rendererTargetByPresentation.end()) return found->second;
    }
    return descriptor->defaultRendererTarget;
}

class Resolver
{
public:
    Resolver (float scaleFactorIn, SkinValidationReport& reportIn, const CompiledSkin* skinIn)
        : scaleFactor (scaleFactorIn), report (reportIn), skin (skinIn)
    {
    }

    std::optional<SkinResolvedLayout> run (const SkinLayoutDefinition& definition,
                                           juce::Rectangle<float> bounds,
                                           WorkspaceMode workspace)
    {
        if (! finiteRectangle (bounds) || ! std::isfinite (scaleFactor) || scaleFactor <= 0)
        {
            fail ("LAYOUT017", {}, "Root bounds and scale factor must be finite and non-negative.");
            return std::nullopt;
        }

        const SkinLayoutNode* selected = &definition.base;
        auto ancillaryStyles = definition.ancillaryStyles;
        for (const auto& breakpoint : definition.breakpoints)
            if ((! breakpoint.workspace || *breakpoint.workspace == workspace)
                && breakpoint.widths.contains (bounds.getWidth())
                && breakpoint.heights.contains (bounds.getHeight())
                && breakpoint.scaleFactors.contains (scaleFactor))
            {
                selected = &breakpoint.root;
                for (const auto& [target, style] : breakpoint.ancillaryStyles)
                    ancillaryStyles.insert_or_assign (target, style);
                break;
            }

        if (skin != nullptr)
        {
            const auto chosen = ancillaryStyles.find ("scrollBar");
            auto* style = skin->resolveStyle (SkinRenderTarget::scrollBar,
                chosen == ancillaryStyles.end() ? "default" : chosen->second);
            if (style != nullptr && ! style->renderer.controlStyle)
                style = skin->resolveStyle (SkinRenderTarget::scrollBar, "default");
            if (style != nullptr)
            {
                SkinRenderContext context;
                context.bounds = context.contentBounds = bounds.withPosition (0, 0);
                context.scaleFactor = scaleFactor;
                if (const auto control = resolveSkinControlStyle (style->renderer, context, report))
                    scrollBarWidth = control->thickness.value_or (scrollBarWidth);
            }
            if (report.hasErrors()) return std::nullopt;
        }

        if (selected->common.minWidth > bounds.getWidth()
            || selected->common.minHeight > bounds.getHeight())
        {
            fail ("LAYOUT016", selected->common.id,
                  "Root minimum constraints do not fit the supplied window.");
            return std::nullopt;
        }

        auto root = resolveNode (*selected, bounds, bounds);
        if (! root || failed) return std::nullopt;
        return SkinResolvedLayout { std::move (*root), std::move (ancillaryStyles), scrollBarWidth };
    }

private:
    Measured measure (const SkinLayoutNode& node)
    {
        Measured result;
        if (node.type() == SkinLayoutNodeType::component)
        {
            const auto dimensions = PlayerComponentCatalog::dimensionsForTarget (
                componentTarget (node));
            result = { dimensions.minimumWidth, dimensions.minimumHeight,
                       dimensions.preferredWidth, dimensions.preferredHeight };
        }
        else if (node.type() == SkinLayoutNodeType::row
                 || node.type() == SkinLayoutNodeType::column)
        {
            const auto& flex = std::get<SkinFlexLayout> (node.payload);
            const auto horizontal = node.type() == SkinLayoutNodeType::row;
            for (const auto& child : flex.children)
            {
                const auto childSize = measure (child);
                const auto margin = resolveInsets (child.common.margin);
                if (horizontal)
                {
                    result.minimumWidth += childSize.minimumWidth + margin.horizontal();
                    result.preferredWidth += childSize.preferredWidth + margin.horizontal();
                    result.minimumHeight = std::max (result.minimumHeight,
                                                     childSize.minimumHeight + margin.vertical());
                    result.preferredHeight = std::max (result.preferredHeight,
                                                       childSize.preferredHeight + margin.vertical());
                }
                else
                {
                    result.minimumHeight += childSize.minimumHeight + margin.vertical();
                    result.preferredHeight += childSize.preferredHeight + margin.vertical();
                    result.minimumWidth = std::max (result.minimumWidth,
                                                    childSize.minimumWidth + margin.horizontal());
                    result.preferredWidth = std::max (result.preferredWidth,
                                                      childSize.preferredWidth + margin.horizontal());
                }
            }
            const auto gaps = flex.children.size() > 1
                ? flex.gap * static_cast<float> (flex.children.size() - 1) : 0.0f;
            if (horizontal) result.minimumWidth += gaps, result.preferredWidth += gaps;
            else result.minimumHeight += gaps, result.preferredHeight += gaps;
        }
        else if (node.type() == SkinLayoutNodeType::grid)
        {
            const auto& grid = std::get<SkinGridLayout> (node.payload);
            for (const auto& track : grid.columns)
                if (track.unit == SkinLength::Unit::pixels)
                    result.minimumWidth += track.value, result.preferredWidth += track.value;
            for (const auto& track : grid.rows)
                if (track.unit == SkinLength::Unit::pixels)
                    result.minimumHeight += track.value, result.preferredHeight += track.value;
            if (grid.columns.size() > 1)
                result.minimumWidth += grid.columnGap * static_cast<float> (grid.columns.size() - 1),
                result.preferredWidth += grid.columnGap * static_cast<float> (grid.columns.size() - 1);
            if (grid.rows.size() > 1)
                result.minimumHeight += grid.rowGap * static_cast<float> (grid.rows.size() - 1),
                result.preferredHeight += grid.rowGap * static_cast<float> (grid.rows.size() - 1);
        }
        else if (node.type() == SkinLayoutNodeType::stack)
        {
            for (const auto& child : std::get<SkinStackLayout> (node.payload).children)
            {
                const auto childSize = measure (child);
                const auto margin = resolveInsets (child.common.margin);
                result.minimumWidth = std::max (result.minimumWidth,
                                                childSize.minimumWidth + margin.horizontal());
                result.minimumHeight = std::max (result.minimumHeight,
                                                 childSize.minimumHeight + margin.vertical());
                result.preferredWidth = std::max (result.preferredWidth,
                                                  childSize.preferredWidth + margin.horizontal());
                result.preferredHeight = std::max (result.preferredHeight,
                                                   childSize.preferredHeight + margin.vertical());
            }
        }
        else if (node.type() == SkinLayoutNodeType::scroll)
        {
            const auto& scroll = std::get<SkinScrollLayout> (node.payload);
            if (scroll.child != nullptr) result = measure (*scroll.child);
        }

        const auto padding = resolveInsets (node.common.padding);
        result.minimumWidth += padding.horizontal();
        result.preferredWidth += padding.horizontal();
        result.minimumHeight += padding.vertical();
        result.preferredHeight += padding.vertical();

        if (node.common.width.unit == SkinLength::Unit::pixels)
            result.preferredWidth = node.common.width.value;
        if (node.common.height.unit == SkinLength::Unit::pixels)
            result.preferredHeight = node.common.height.value;
        result.minimumWidth = std::max (result.minimumWidth, node.common.minWidth);
        result.minimumHeight = std::max (result.minimumHeight, node.common.minHeight);
        result.preferredWidth = std::clamp (result.preferredWidth,
                                            node.common.minWidth, node.common.maxWidth);
        result.preferredHeight = std::clamp (result.preferredHeight,
                                             node.common.minHeight, node.common.maxHeight);
        return result;
    }

    std::pair<float, float> desiredSize (const SkinLayoutNode& node,
                                         float parentWidth, float parentHeight)
    {
        const auto measured = measure (node);
        auto width = resolveLength (node.common.width, parentWidth, measured.preferredWidth);
        auto height = resolveLength (node.common.height, parentHeight, measured.preferredHeight);
        if (node.common.aspectRatio > 0)
        {
            if (node.common.width.unit != SkinLength::Unit::automatic
                && node.common.height.unit == SkinLength::Unit::automatic)
                height = width / node.common.aspectRatio;
            else if (node.common.height.unit != SkinLength::Unit::automatic
                     && node.common.width.unit == SkinLength::Unit::automatic)
                width = height * node.common.aspectRatio;
        }
        width = std::clamp (width, node.common.minWidth, node.common.maxWidth);
        height = std::clamp (height, node.common.minHeight, node.common.maxHeight);
        return { width, height };
    }

    std::optional<SkinResolvedNode> resolveNode (const SkinLayoutNode& node,
                                                 juce::Rectangle<float> bounds,
                                                 juce::Rectangle<float> inheritedClip)
    {
        if (! finiteRectangle (bounds))
        {
            fail ("LAYOUT017", node.common.id, "Resolved node bounds are non-finite or negative.");
            return std::nullopt;
        }

        SkinResolvedNode result;
        result.type = node.type();
        result.nodeId = node.common.id;
        result.style = node.common.style;
        result.bounds = bounds;
        result.clipBounds = node.common.clip ? inheritedClip.getIntersection (bounds)
                                             : inheritedClip;
        result.opacity = node.common.opacity;
        result.zIndex = node.common.zIndex;
        result.visible = node.common.visible;
        if (node.type() == SkinLayoutNodeType::component)
        {
            const auto& component = std::get<SkinComponentLayout> (node.payload);
            result.componentId = component.componentId;
            result.presentation = component.presentation;
            result.focusOrder = component.focusOrder;
            if (const auto* descriptor = PlayerComponentCatalog::find (component.componentId);
                descriptor != nullptr && descriptor->interactive
                && (bounds.getWidth() < 28.0f || bounds.getHeight() < 28.0f))
            {
                fail ("LAYOUT015", node.common.id,
                      "Interactive component resolves below the 28 x 28 hit target.");
                return std::nullopt;
            }
            return result;
        }

        const auto content = contentRectangle (bounds, resolveInsets (node.common.padding));
        if (node.type() == SkinLayoutNodeType::row || node.type() == SkinLayoutNodeType::column)
        {
            if (! resolveFlex (node, content, result.clipBounds, result.children))
                return std::nullopt;
        }
        else if (node.type() == SkinLayoutNodeType::grid)
        {
            if (! resolveGrid (node, content, result.clipBounds, result.children))
                return std::nullopt;
        }
        else if (node.type() == SkinLayoutNodeType::stack)
        {
            if (! resolveStack (node, content, result.clipBounds, result.children))
                return std::nullopt;
        }
        else if (node.type() == SkinLayoutNodeType::scroll)
        {
            result.scrollVertical = std::get<SkinScrollLayout> (node.payload).direction
                                 == SkinScrollLayout::Direction::vertical;
            if (! resolveScroll (node, content, result)) return std::nullopt;
        }
        return result;
    }

    bool resolveFlex (const SkinLayoutNode& node, juce::Rectangle<float> content,
                      juce::Rectangle<float> clip, std::vector<SkinResolvedNode>& output)
    {
        const auto& layout = std::get<SkinFlexLayout> (node.payload);
        const auto horizontal = node.type() == SkinLayoutNodeType::row;
        const auto mainExtent = horizontal ? content.getWidth() : content.getHeight();
        const auto crossExtent = horizontal ? content.getHeight() : content.getWidth();
        struct Item
        {
            const SkinLayoutNode* node = nullptr;
            Insets margin;
            float main = 0, cross = 0, minimum = 0, maximum = 0;
        };
        std::vector<Item> items;
        float used = layout.children.size() > 1
            ? layout.gap * static_cast<float> (layout.children.size() - 1) : 0.0f;
        for (const auto& child : layout.children)
        {
            const auto desired = desiredSize (child, content.getWidth(), content.getHeight());
            const auto measured = measure (child);
            Item item;
            item.node = &child;
            item.margin = resolveInsets (child.common.margin);
            item.main = horizontal ? desired.first : desired.second;
            item.cross = horizontal ? desired.second : desired.first;
            const auto declared = horizontal ? child.common.width : child.common.height;
            const auto commonMinimum = horizontal ? child.common.minWidth : child.common.minHeight;
            const auto intrinsicMinimum = horizontal ? measured.minimumWidth : measured.minimumHeight;
            item.minimum = declared.unit == SkinLength::Unit::automatic
                ? std::max (commonMinimum, intrinsicMinimum) : commonMinimum;
            item.maximum = horizontal ? child.common.maxWidth : child.common.maxHeight;
            item.main = std::clamp (item.main, item.minimum, item.maximum);
            used += item.main + (horizontal ? item.margin.horizontal() : item.margin.vertical());
            items.push_back (item);
        }

        auto remaining = mainExtent - used;
        if (remaining > 0.0001f)
            distributeGrow (items, remaining);
        else if (remaining < -0.0001f)
            distributeShrink (items, -remaining);

        used = layout.children.size() > 1
            ? layout.gap * static_cast<float> (layout.children.size() - 1) : 0.0f;
        for (const auto& item : items)
            used += item.main + (horizontal ? item.margin.horizontal() : item.margin.vertical());
        if (used > mainExtent + 0.01f)
        {
            fail ("LAYOUT016", node.common.id,
                  "Flex minimum contributions do not fit their container.");
            return false;
        }

        const auto spare = std::max (0.0f, mainExtent - used);
        auto between = layout.gap;
        auto leading = 0.0f;
        if (layout.justify == SkinJustify::centre) leading = spare * 0.5f;
        else if (layout.justify == SkinJustify::end) leading = spare;
        else if (layout.justify == SkinJustify::spaceBetween && items.size() > 1)
            between += spare / static_cast<float> (items.size() - 1);
        else if (layout.justify == SkinJustify::spaceAround && ! items.empty())
        {
            between += spare / static_cast<float> (items.size());
            leading = (between - layout.gap) * 0.5f;
        }

        auto cursor = (horizontal ? content.getX() : content.getY()) + leading;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            const auto before = horizontal ? item.margin.left : item.margin.top;
            const auto after = horizontal ? item.margin.right : item.margin.bottom;
            const auto rawStart = cursor + before;
            const auto rawEnd = rawStart + item.main;
            const auto mainStart = std::round (rawStart);
            const auto mainEnd = std::round (rawEnd);

            const auto crossBefore = horizontal ? item.margin.top : item.margin.left;
            const auto crossAfter = horizontal ? item.margin.bottom : item.margin.right;
            const auto alignment = item.node->common.alignSelf == SkinAlign::automatic
                ? layout.align : item.node->common.alignSelf;
            auto crossSize = item.cross;
            const auto crossLength = horizontal ? item.node->common.height : item.node->common.width;
            if (alignment == SkinAlign::stretch
                && crossLength.unit == SkinLength::Unit::automatic)
                crossSize = std::max (0.0f, crossExtent - crossBefore - crossAfter);
            const auto crossMinimum = horizontal ? item.node->common.minHeight
                                                 : item.node->common.minWidth;
            const auto crossMaximum = horizontal ? item.node->common.maxHeight
                                                 : item.node->common.maxWidth;
            crossSize = std::clamp (crossSize, crossMinimum, crossMaximum);
            auto crossStart = (horizontal ? content.getY() : content.getX()) + crossBefore;
            const auto crossSpare = crossExtent - crossSize - crossBefore - crossAfter;
            if (alignment == SkinAlign::centre) crossStart += crossSpare * 0.5f;
            else if (alignment == SkinAlign::end) crossStart += crossSpare;
            crossStart = std::round (crossStart);
            crossSize = std::round (crossStart + crossSize) - crossStart;

            const auto childBounds = horizontal
                ? juce::Rectangle<float> (mainStart, crossStart, mainEnd - mainStart, crossSize)
                : juce::Rectangle<float> (crossStart, mainStart, crossSize, mainEnd - mainStart);
            auto resolved = resolveNode (*item.node, childBounds, clip);
            if (! resolved) return false;
            output.push_back (std::move (*resolved));
            cursor = rawEnd + after + (i + 1 < items.size() ? between : 0.0f);
        }
        return true;
    }

    template <typename Items>
    static void distributeGrow (Items& items, float amount)
    {
        for (int pass = 0; pass < 64 && amount > 0.0001f; ++pass)
        {
            float weights = 0;
            for (const auto& item : items)
                if (item.node->common.grow > 0 && item.main < item.maximum - 0.0001f)
                    weights += item.node->common.grow;
            if (weights <= 0) break;
            const auto available = amount;
            float applied = 0;
            for (auto& item : items)
                if (item.node->common.grow > 0 && item.main < item.maximum - 0.0001f)
                {
                    const auto share = available * item.node->common.grow / weights;
                    const auto delta = std::min (share, item.maximum - item.main);
                    item.main += delta;
                    applied += delta;
                }
            amount -= applied;
            if (applied <= 0.0001f) break;
        }
    }

    template <typename Items>
    static void distributeShrink (Items& items, float deficit)
    {
        for (int pass = 0; pass < 64 && deficit > 0.0001f; ++pass)
        {
            float weights = 0;
            for (const auto& item : items)
                if (item.node->common.shrink > 0 && item.main > item.minimum + 0.0001f)
                    weights += item.node->common.shrink * std::max (item.main, 0.0001f);
            if (weights <= 0) break;
            const auto available = deficit;
            float applied = 0;
            for (auto& item : items)
                if (item.node->common.shrink > 0 && item.main > item.minimum + 0.0001f)
                {
                    const auto weight = item.node->common.shrink * std::max (item.main, 0.0001f);
                    const auto share = available * weight / weights;
                    const auto delta = std::min (share, item.main - item.minimum);
                    item.main -= delta;
                    applied += delta;
                }
            deficit -= applied;
            if (applied <= 0.0001f) break;
        }
    }

    struct GridPlacement
    {
        const SkinLayoutNode* node = nullptr;
        int column = 0, row = 0;
    };

    std::optional<std::vector<float>> resolveTracks (
        const std::vector<SkinLength>& tracks, float extent, float gap,
        const std::vector<GridPlacement>& placements, bool horizontal)
    {
        std::vector<float> sizes (tracks.size(), 0.0f);
        const auto totalGaps = tracks.size() > 1
            ? gap * static_cast<float> (tracks.size() - 1) : 0.0f;
        float used = totalGaps;
        float fractionWeight = 0;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            if (tracks[i].unit == SkinLength::Unit::pixels) sizes[i] = tracks[i].value;
            else if (tracks[i].unit == SkinLength::Unit::percent)
                sizes[i] = extent * tracks[i].value * 0.01f;
            else if (tracks[i].unit == SkinLength::Unit::fraction)
                fractionWeight += tracks[i].value;
            used += sizes[i];
        }

        for (const auto& placement : placements)
        {
            const auto& child = *placement.node;
            const auto start = horizontal ? placement.column : placement.row;
            const auto span = horizontal ? child.common.columnSpan : child.common.rowSpan;
            if (span != 1) continue;
            const auto index = static_cast<size_t> (start - 1);
            if (index < tracks.size() && tracks[index].unit == SkinLength::Unit::automatic)
            {
                const auto childMeasure = measure (child);
                const auto contribution = horizontal ? childMeasure.minimumWidth
                                                     : childMeasure.minimumHeight;
                const auto delta = std::max (0.0f, contribution - sizes[index]);
                sizes[index] += delta;
                used += delta;
            }
        }

        for (const auto& placement : placements)
        {
            const auto& child = *placement.node;
            const auto start = horizontal ? placement.column : placement.row;
            const auto span = horizontal ? child.common.columnSpan : child.common.rowSpan;
            if (span <= 1) continue;
            const auto begin = static_cast<size_t> (start - 1);
            const auto end = std::min (tracks.size(), begin + static_cast<size_t> (span));
            float current = gap * static_cast<float> (span - 1);
            std::vector<size_t> automaticTracks, fractionTracks;
            for (size_t i = begin; i < end; ++i)
            {
                current += sizes[i];
                if (tracks[i].unit == SkinLength::Unit::automatic) automaticTracks.push_back (i);
                if (tracks[i].unit == SkinLength::Unit::fraction) fractionTracks.push_back (i);
            }
            const auto childMeasure = measure (child);
            const auto contribution = horizontal ? childMeasure.minimumWidth
                                                 : childMeasure.minimumHeight;
            auto deficit = std::max (0.0f, contribution - current);
            auto& recipients = automaticTracks.empty() ? fractionTracks : automaticTracks;
            if (! recipients.empty() && deficit > 0)
            {
                const auto share = deficit / static_cast<float> (recipients.size());
                for (const auto index : recipients) sizes[index] += share;
                used += deficit;
            }
        }

        const auto remaining = extent - used;
        if (fractionWeight > 0 && remaining > 0)
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].unit == SkinLength::Unit::fraction)
                    sizes[i] += remaining * tracks[i].value / fractionWeight;
        if (used > extent + 0.01f)
            return std::nullopt;
        return sizes;
    }

    bool resolveGrid (const SkinLayoutNode& node, juce::Rectangle<float> content,
                      juce::Rectangle<float> clip, std::vector<SkinResolvedNode>& output)
    {
        const auto& layout = std::get<SkinGridLayout> (node.payload);
        std::vector<GridPlacement> placements;
        placements.reserve (layout.children.size());
        int automaticIndex = 0;
        for (const auto& child : layout.children)
        {
            auto column = child.common.gridColumn;
            auto row = child.common.gridRow;
            if (column <= 0 || row <= 0)
            {
                column = automaticIndex % static_cast<int> (layout.columns.size()) + 1;
                row = automaticIndex / static_cast<int> (layout.columns.size()) + 1;
                ++automaticIndex;
            }
            row = std::min (row, static_cast<int> (layout.rows.size()));
            placements.push_back ({ &child, column, row });
        }
        const auto columns = resolveTracks (layout.columns, content.getWidth(), layout.columnGap,
                                            placements, true);
        const auto rows = resolveTracks (layout.rows, content.getHeight(), layout.rowGap,
                                         placements, false);
        if (! columns || ! rows)
        {
            fail ("LAYOUT016", node.common.id, "Grid tracks do not fit their container.");
            return false;
        }

        std::vector<float> columnStarts, rowStarts;
        auto position = content.getX();
        for (const auto size : *columns)
        {
            columnStarts.push_back (position);
            position += size + layout.columnGap;
        }
        position = content.getY();
        for (const auto size : *rows)
        {
            rowStarts.push_back (position);
            position += size + layout.rowGap;
        }

        for (const auto& placement : placements)
        {
            const auto& child = *placement.node;
            const auto column = placement.column;
            const auto row = placement.row;
            const auto columnEnd = std::min (static_cast<int> (columns->size()),
                                             column + child.common.columnSpan - 1);
            const auto rowEnd = std::min (static_cast<int> (rows->size()),
                                          row + child.common.rowSpan - 1);
            const auto x = columnStarts[static_cast<size_t> (column - 1)];
            const auto y = rowStarts[static_cast<size_t> (row - 1)];
            const auto right = columnStarts[static_cast<size_t> (columnEnd - 1)]
                             + (*columns)[static_cast<size_t> (columnEnd - 1)];
            const auto bottom = rowStarts[static_cast<size_t> (rowEnd - 1)]
                              + (*rows)[static_cast<size_t> (rowEnd - 1)];
            const auto margin = resolveInsets (child.common.margin);
            auto cell = juce::Rectangle<float> (std::round (x + margin.left),
                                                std::round (y + margin.top),
                                                std::round (right - margin.right)
                                                    - std::round (x + margin.left),
                                                std::round (bottom - margin.bottom)
                                                    - std::round (y + margin.top));
            const auto desired = desiredSize (child, cell.getWidth(), cell.getHeight());
            if (child.common.width.unit != SkinLength::Unit::automatic)
                cell.setWidth (std::round (desired.first));
            if (child.common.height.unit != SkinLength::Unit::automatic)
                cell.setHeight (std::round (desired.second));
            auto resolved = resolveNode (child, cell, clip);
            if (! resolved) return false;
            output.push_back (std::move (*resolved));
        }
        return true;
    }

    bool resolveStack (const SkinLayoutNode& node, juce::Rectangle<float> content,
                       juce::Rectangle<float> clip, std::vector<SkinResolvedNode>& output)
    {
        const auto& layout = std::get<SkinStackLayout> (node.payload);
        for (const auto& child : layout.children)
        {
            const auto desired = desiredSize (child, content.getWidth(), content.getHeight());
            const auto margin = resolveInsets (child.common.margin);
            auto width = desired.first;
            auto height = desired.second;
            auto x = content.getX() + margin.left;
            auto y = content.getY() + margin.top;
            if (child.common.anchors.left && child.common.anchors.right)
            {
                const auto left = resolveAnchor (*child.common.anchors.left, content.getWidth());
                const auto right = resolveAnchor (*child.common.anchors.right, content.getWidth());
                x = content.getX() + left + margin.left;
                width = content.getWidth() - left - right - margin.horizontal();
            }
            else if (child.common.anchors.left)
                x = content.getX() + resolveAnchor (*child.common.anchors.left,
                                                    content.getWidth()) + margin.left;
            else if (child.common.anchors.right)
                x = content.getRight() - resolveAnchor (*child.common.anchors.right,
                                                        content.getWidth()) - width - margin.right;
            else if (child.common.anchors.centreX)
                x = content.getCentreX() - width * 0.5f + *child.common.anchors.centreX;

            if (child.common.anchors.top && child.common.anchors.bottom)
            {
                const auto top = resolveAnchor (*child.common.anchors.top, content.getHeight());
                const auto bottom = resolveAnchor (*child.common.anchors.bottom, content.getHeight());
                y = content.getY() + top + margin.top;
                height = content.getHeight() - top - bottom - margin.vertical();
            }
            else if (child.common.anchors.top)
                y = content.getY() + resolveAnchor (*child.common.anchors.top,
                                                    content.getHeight()) + margin.top;
            else if (child.common.anchors.bottom)
                y = content.getBottom() - resolveAnchor (*child.common.anchors.bottom,
                                                         content.getHeight()) - height - margin.bottom;
            else if (child.common.anchors.centreY)
                y = content.getCentreY() - height * 0.5f + *child.common.anchors.centreY;

            auto bounds = juce::Rectangle<float> (std::round (x), std::round (y),
                                                   std::round (x + width) - std::round (x),
                                                   std::round (y + height) - std::round (y));
            auto resolved = resolveNode (child, bounds, clip);
            if (! resolved) return false;
            output.push_back (std::move (*resolved));
        }
        std::stable_sort (output.begin(), output.end(), [] (const auto& left, const auto& right)
        {
            return left.zIndex < right.zIndex;
        });
        return true;
    }

    bool resolveScroll (const SkinLayoutNode& node, juce::Rectangle<float> viewport,
                        SkinResolvedNode& output)
    {
        const auto& layout = std::get<SkinScrollLayout> (node.payload);
        output.scrollViewportBounds = viewport;
        if (layout.child == nullptr) return true;
        auto childViewport = viewport;
        auto desired = desiredSize (*layout.child, viewport.getWidth(),
                                    viewport.getHeight());
        const auto needsScrollBar = layout.direction
                                          == SkinScrollLayout::Direction::vertical
                                      ? juce::roundToInt (desired.second)
                                            > juce::roundToInt (viewport.getHeight())
                                      : juce::roundToInt (desired.first)
                                            > juce::roundToInt (viewport.getWidth());
        if (needsScrollBar)
        {
            if (layout.direction == SkinScrollLayout::Direction::vertical)
                childViewport.setWidth (std::max (
                    0.0f, childViewport.getWidth()
                              - scrollBarWidth));
            else
                childViewport.setHeight (std::max (
                    0.0f, childViewport.getHeight()
                               - scrollBarWidth));
            desired = desiredSize (*layout.child, childViewport.getWidth(),
                                   childViewport.getHeight());
        }

        auto width = desired.first;
        auto height = desired.second;
        if (layout.direction == SkinScrollLayout::Direction::vertical)
            width = childViewport.getWidth();
        else
            height = childViewport.getHeight();
        auto contentBounds = juce::Rectangle<float> (viewport.getX(), viewport.getY(),
                                                      std::round (width), std::round (height));
        output.scrollContentBounds = contentBounds;
        auto resolved = resolveNode (*layout.child, contentBounds,
                                     output.clipBounds.getIntersection (viewport));
        if (! resolved) return false;
        output.children.push_back (std::move (*resolved));
        return true;
    }

    void fail (juce::String code, juce::String pointer, juce::String message)
    {
        if (failed) return;
        failed = true;
        report.addError (std::move (code), "layout", std::move (pointer), std::move (message));
    }

    float scaleFactor;
    SkinValidationReport& report;
    const CompiledSkin* skin;
    float scrollBarWidth = static_cast<float> (SkinLayoutEngine::scrollBarThickness);
    bool failed = false;
};

const SkinResolvedNode* findComponent (const SkinResolvedNode& node, const juce::String& id)
{
    if (node.componentId == id) return &node;
    for (const auto& child : node.children)
        if (const auto* found = findComponent (child, id)) return found;
    return nullptr;
}
} // namespace

const SkinResolvedNode* SkinResolvedLayout::component (const juce::String& id) const
{
    return findComponent (root, id);
}

std::optional<SkinResolvedLayout> SkinLayoutEngine::resolve (
    const SkinLayoutDefinition& definition, juce::Rectangle<float> bounds,
    float scaleFactor, SkinValidationReport& report,
    std::optional<WorkspaceMode> workspace, const CompiledSkin* skin)
{
    return Resolver (scaleFactor, report, skin).run (
        definition, bounds, workspace.value_or (definition.defaultWorkspace));
}
