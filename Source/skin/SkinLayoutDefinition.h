#pragma once

#include "app/WorkspaceMode.h"
#include <array>

#include <juce_graphics/juce_graphics.h>

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

struct SkinLength
{
    enum class Unit { automatic, pixels, percent, fraction };
    Unit unit = Unit::automatic;
    float value = 0.0f;
};

enum class SkinLayoutNodeType { row, column, grid, stack, scroll, component, visual };
enum class SkinAlign { automatic, start, centre, end, stretch };
enum class SkinJustify { start, centre, end, spaceBetween, spaceAround };

struct SkinInsets
{
    SkinLength top, right, bottom, left;
};

struct SkinAnchors
{
    std::optional<SkinLength> top, right, bottom, left;
    std::optional<float> centreX, centreY;
};

struct SkinLayoutState
{
    bool queueOpen = false;
    bool lyricsOpen = false;
    [[nodiscard]] size_t index() const noexcept
    { return static_cast<size_t> ((queueOpen ? 1 : 0) | (lyricsOpen ? 2 : 0)); }
};

struct SkinLayoutCommon
{
    juce::String id, style;
    SkinLength width, height;
    float minWidth = 0.0f, minHeight = 0.0f;
    float maxWidth = std::numeric_limits<float>::infinity();
    float maxHeight = std::numeric_limits<float>::infinity();
    float grow = 0.0f, shrink = 1.0f;
    SkinInsets margin, padding;
    float aspectRatio = 0.0f, opacity = 1.0f;
    int zIndex = 0, gridColumn = 0, gridRow = 0, columnSpan = 1, rowSpan = 1;
    bool visible = true, clip = false;
    bool blocksPointer = false;
    std::optional<std::array<bool, 4>> visibleWhen;
    SkinAnchors anchors;
    SkinAlign alignSelf = SkinAlign::automatic;
};

struct SkinLayoutNode;

struct SkinFlexLayout
{
    float gap = 0.0f;
    SkinAlign align = SkinAlign::stretch;
    SkinJustify justify = SkinJustify::start;
    std::vector<SkinLayoutNode> children;
};

struct SkinGridLayout
{
    std::vector<SkinLength> columns, rows;
    float columnGap = 0.0f, rowGap = 0.0f;
    std::vector<SkinLayoutNode> children;
};

struct SkinStackLayout { std::vector<SkinLayoutNode> children; };

struct SkinScrollLayout
{
    enum class Direction { vertical, horizontal };
    SkinScrollLayout() = default;
    SkinScrollLayout (const SkinScrollLayout&);
    SkinScrollLayout& operator= (const SkinScrollLayout&);
    SkinScrollLayout (SkinScrollLayout&&) noexcept = default;
    SkinScrollLayout& operator= (SkinScrollLayout&&) noexcept = default;
    ~SkinScrollLayout();

    Direction direction = Direction::vertical;
    std::unique_ptr<SkinLayoutNode> child;
};

struct SkinComponentLayout
{
    juce::String componentId, presentation;
    int focusOrder = 0;
};

struct SkinVisualLayout {};

struct SkinLayoutNode
{
    SkinLayoutNodeType nodeType = SkinLayoutNodeType::visual;
    SkinLayoutCommon common;
    std::variant<SkinFlexLayout, SkinGridLayout, SkinStackLayout,
                 SkinScrollLayout, SkinComponentLayout, SkinVisualLayout> payload;

    [[nodiscard]] SkinLayoutNodeType type() const;
};

struct SkinLayoutDefinition
{
    struct InclusiveRange
    {
        std::optional<float> minimum, maximum;
        [[nodiscard]] bool contains (float value) const
        {
            return (! minimum || value >= *minimum)
                && (! maximum || value <= *maximum);
        }
    };

    struct Breakpoint
    {
        InclusiveRange widths, heights, scaleFactors;
        std::optional<WorkspaceMode> workspace;
        SkinLayoutNode root;
        std::map<juce::String, juce::String> ancillaryStyles;
    };

    std::optional<juce::Point<float>> preferredSize;
    SkinLayoutNode base;
    std::vector<Breakpoint> breakpoints;
    WorkspaceMode defaultWorkspace = WorkspaceMode::listening;
    std::map<juce::String, juce::String> ancillaryStyles;
};
