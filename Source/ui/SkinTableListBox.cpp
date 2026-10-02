#include "SkinTableListBox.h"

#include "JuceSkinAdapter.h"

SkinTableListBox::SkinTableListBox()
{
    getHeader().setStretchToFitActive (true);
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
