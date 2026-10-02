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
    if (presentation == "table")
        tableColumns = header.toString();

    presentation = nextPresentation;
    header.setStretchToFitActive (false);
    setRowHeight (presentation == "list" ? 56 : 40);
    setHeaderHeight (presentation == "list" ? 0 : 28);
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
        if (presentation == "table")
        {
            const auto width = getVisibleContentWidth();
            const auto titleWidth = juce::roundToInt (static_cast<double> (width) * 0.4);
            header.setColumnWidth (1, titleWidth);
            header.setColumnWidth (2, juce::jmax (0, width - titleWidth - 64));
            header.setColumnWidth (3, 64);
        }
    }
    resized();
}

void SkinTableListBox::resetPresentation()
{
    if (! originalState) return;
    presentation.clear();
    auto& header = getHeader();
    header.setStretchToFitActive (false);
    setRowHeight (originalState->rowHeight);
    setHeaderHeight (originalState->headerHeight);
    header.restoreFromString (originalState->columns);
    header.setStretchToFitActive (originalState->stretchToFit);
    setMinimumContentWidth (header.getTotalWidth());
    tableColumns.clear();
    originalState.reset();
}

void SkinTableListBox::resized()
{
    juce::TableListBox::resized();
    layoutPresentationColumns();
}

void SkinTableListBox::layoutPresentationColumns()
{
    if (presentation.isEmpty()) return;
    auto& header = getHeader();
    const auto width = getVisibleContentWidth();
    if (presentation == "list")
    {
        header.setColumnWidth (3, 56);
        header.setColumnWidth (1, juce::jmax (0, width - header.getColumnWidth (3)));
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
    setMinimumContentWidth (header.getTotalWidth());
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
