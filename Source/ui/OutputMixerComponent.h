#pragma once

#include "audio/OutputChannelMix.h"
#include "audio/OutputLayout.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

class OutputMixerComponent final : public juce::Component
{
public:
    OutputMixerComponent();
    ~OutputMixerComponent() override;
    void setState (OutputLayout, const std::vector<OutputChannelMix>&,
                   const std::array<float, maximumOutputChannels>& peaks);
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void (juce::AudioChannelSet::ChannelType, float, bool, bool)> onChange;

private:
    class Row;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    juce::AudioChannelSet channels;
};
