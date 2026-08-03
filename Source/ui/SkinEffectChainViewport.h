#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class SkinEffectChainViewport final : public juce::Viewport
{
public:
    void setSlotCount (int);
    [[nodiscard]] int getSlotCount() const noexcept { return slotCount; }
    void paint (juce::Graphics&) override;

private:
    int slotCount = 0;
};
