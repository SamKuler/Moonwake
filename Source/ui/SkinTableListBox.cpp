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
