#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class SkinTableListBox final : public juce::TableListBox
{
public:
    SkinTableListBox();
    void paint (juce::Graphics&) override;
};
