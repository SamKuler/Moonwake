#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceSkinAdapter.h"

#include <optional>

class SkinTableListBox final : public juce::TableListBox
{
public:
    SkinTableListBox();
    void setPresentation (const juce::String&);
    void resetPresentation();
    void resized() override;
    void paint (juce::Graphics&) override;
    [[nodiscard]] JuceSkinAdapter::ListBoxRowGeometry rowGeometry (int rowHeight) const;

private:
    struct OriginalState
    {
        juce::String columns;
        int rowHeight = 0;
        int headerHeight = 0;
        bool stretchToFit = false;
    };

    std::optional<OriginalState> originalState;
    juce::String presentation;
    juce::String tableColumns;

    void layoutPresentationColumns();
};
