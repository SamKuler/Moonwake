#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceSkinAdapter.h"

class SkinTableListBox final : public juce::TableListBox
{
public:
    SkinTableListBox();
    void paint (juce::Graphics&) override;
    [[nodiscard]] JuceSkinAdapter::ListBoxRowGeometry rowGeometry (int rowHeight) const;
};
