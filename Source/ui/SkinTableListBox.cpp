#include "SkinTableListBox.h"

#include "JuceSkinAdapter.h"

SkinTableListBox::SkinTableListBox()
{
    getHeader().setStretchToFitActive (true);
}

void SkinTableListBox::setPresentation (const juce::String& nextPresentation)
{
    if (nextPresentation != "table" && nextPresentation != "list")
    {
        resetPresentation();
        return;
    }
    if (presentation == nextPresentation)
    {
        resized();
        return;
    }

    auto& header = getHeader();
    if (! originalState)
        originalState = OriginalState { header.toString(), getRowHeight(),
                                        getHeaderHeight(), header.isStretchToFitActive() };
    rememberTableColumns();

    presentation = nextPresentation;
    header.setStretchToFitActive (false);
    if (presentation == "table" && tableColumns.isNotEmpty())
    {
        header.restoreFromString (tableColumns);
    }
    else
    {
        for (int index = 0; index < header.getNumColumns (false); ++index)
        {
            const auto id = header.getColumnIdOfIndex (index, false);
            header.setColumnVisible (id, id == 1 || id == 3
                || (presentation == "table" && id == 2));
        }
        header.moveColumn (1, 0);
        if (presentation == "table") header.moveColumn (2, 1);
        header.moveColumn (3, presentation == "list" ? 1 : 2);
    }
    if (presentation == "table") lastAppliedTableColumns = header.toString();
    resized();
}

void SkinTableListBox::resetPresentation()
{
    if (! originalState) return;
    rememberTableColumns();
    presentation.clear();
    auto& header = getHeader();
    header.setStretchToFitActive (false);
    setRowHeight (originalState->rowHeight);
    setHeaderHeight (originalState->headerHeight);
    header.restoreFromString (originalState->columns);
    header.setStretchToFitActive (originalState->stretchToFit);
    setMinimumContentWidth (header.getTotalWidth());
    originalState.reset();
}

void SkinTableListBox::resized()
{
    juce::TableListBox::resized();
    if (presentation.isEmpty()) return;
    if (const auto layout = resolvedRowLayout())
    {
        const auto rowHeight = juce::roundToInt (layout->rowHeight + layout->rowGap);
        const auto headerHeight = juce::roundToInt (layout->headerHeight);
        if (getRowHeight() != rowHeight) setRowHeight (rowHeight);
        if (getHeaderHeight() != headerHeight) setHeaderHeight (headerHeight);
    }
    layoutPresentationColumns();
}

std::optional<SkinResolvedRowLayout> SkinTableListBox::resolvedRowLayout() const
{
    if (getLocalBounds().isEmpty() || getVisibleContentWidth() <= 0) return std::nullopt;
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (adapter == nullptr) return std::nullopt;
    SkinRenderContext context;
    context.bounds = { 0, 0, static_cast<float> (getVisibleContentWidth()), static_cast<float> (getHeight()) };
    context.contentBounds = context.bounds;
    if (auto* model = getTableListBoxModel()) context.tableRowCount = model->getNumRows();
    context.tableColumnCount = getHeader().getNumColumns (true);
    context.extraLookup = [mode = presentation.isEmpty() ? juce::String ("table") : presentation]
        (std::string_view path) -> std::optional<SkinExpressionValue>
    {
        if (path == "table.presentation") return SkinExpressionValue (mode);
        return std::nullopt;
    };
    return adapter->rowLayoutFor (*this, SkinRenderTarget::table, std::move (context));
}

void SkinTableListBox::layoutPresentationColumns()
{
    if (presentation.isEmpty() || getLocalBounds().isEmpty()) return;
    auto& header = getHeader();
    const auto width = getVisibleContentWidth();
    if (width <= 0) return;
    if (presentation == "list")
    {
        if (const auto layout = resolvedRowLayout())
            for (const auto& [id, slot] : { std::pair { 1, "cell.title" },
                                           std::pair { 3, "cell.duration" } })
                if (const auto found = layout->slots.find (slot); found != layout->slots.end())
                    header.setColumnWidth (id, juce::roundToInt (found->second.getWidth()));
    }
    else
    {
        rememberTableColumns();
        const auto layout = customTableColumns ? std::nullopt : resolvedRowLayout();
        if (layout)
        {
            for (const auto& [id, slot] : { std::pair { 1, "cell.title" },
                                           std::pair { 2, "cell.location" },
                                           std::pair { 3, "cell.duration" } })
                if (const auto found = layout->slots.find (slot); found != layout->slots.end())
                    header.setColumnWidth (id, juce::roundToInt (found->second.getWidth()));
        }
        else
        {
            const auto flexibleWidth = header.getColumnWidth (1) + header.getColumnWidth (2);
            const auto remaining = juce::jmax (0, width - header.getColumnWidth (3));
            if (flexibleWidth > 0)
            {
                const auto titleWidth = juce::roundToInt (
                    static_cast<double> (remaining) * header.getColumnWidth (1) / flexibleWidth);
                header.setColumnWidth (1, titleWidth);
                header.setColumnWidth (2, remaining - titleWidth);
            }
        }
        tableColumns = lastAppliedTableColumns = header.toString();
    }
    setMinimumContentWidth (header.getTotalWidth());
}

void SkinTableListBox::rememberTableColumns()
{
    if (presentation != "table" || getLocalBounds().isEmpty() || getVisibleContentWidth() <= 0) return;
    const auto current = getHeader().toString();
    if (lastAppliedTableColumns.isNotEmpty() && current != lastAppliedTableColumns)
        customTableColumns = true;
    tableColumns = current;
}

void SkinTableListBox::paint (juce::Graphics& graphics)
{
    juce::TableListBox::paint (graphics);
    if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel()))
        adapter->drawListBoxBackground (
            graphics, *this, getNumRows(), getHeader().getNumColumns (true));
}

JuceSkinAdapter::ListBoxRowGeometry SkinTableListBox::rowGeometry (int rowHeight) const
{
    if (presentation == "list")
        if (const auto layout = resolvedRowLayout())
        {
            const auto slot = [&] (const char* name)
            {
                const auto found = layout->slots.find (name);
                return found != layout->slots.end() ? found->second : juce::Rectangle<float>{};
            };
            return { slot ("cell.title"), slot ("cell.location"), slot ("cell.duration") };
        }
    const auto cell = [this, rowHeight] (int columnId)
    {
        const auto& header = getHeader();
        const auto index = header.getIndexOfColumnId (columnId, true);
        if (index < 0)
            return juce::Rectangle<float> {};
        return header.getColumnPosition (index)
            .withY (0).withHeight (rowHeight).toFloat();
    };
    return { cell (1), cell (2), cell (3) };
}
