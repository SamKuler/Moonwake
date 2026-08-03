#include "OutputStageComponent.h"

#include "JuceSkinAdapter.h"

void OutputStageComponent::setState (
    OutputLayout layoutToUse,
    WorkspaceMode workspaceModeToUse,
    bool isPlaying,
    juce::String trackTitleToUse,
    juce::String deviceNameToUse,
    int availableChannelsToUse,
    std::array<float, 10> outputPeakLevelsToUse)
{
    const auto changed = layout != layoutToUse || workspaceMode != workspaceModeToUse
        || playing != isPlaying || trackTitle != trackTitleToUse
        || deviceName != deviceNameToUse
        || availableChannels != availableChannelsToUse
        || outputPeakLevels != outputPeakLevelsToUse;

    layout = layoutToUse;
    workspaceMode = workspaceModeToUse;
    playing = isPlaying;
    trackTitle = std::move (trackTitleToUse);
    deviceName = std::move (deviceNameToUse);
    availableChannels = availableChannelsToUse;
    outputPeakLevels = std::move (outputPeakLevelsToUse);

    if (changed)
        repaint();
}

void OutputStageComponent::paint (juce::Graphics& graphics)
{
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (adapter == nullptr)
        return;

    SkinRenderContext context;
    context.primaryText = trackTitle;
    context.secondaryText = deviceName;
    context.displayValue = toString (layout);
    context.iconName = workspaceMode == WorkspaceMode::studio
                           ? "studio" : "listening";
    context.normalisedValue = playing ? 1.0 : 0.0;
    context.minimum = 0.0;
    context.maximum = static_cast<double> (availableChannels);
    context.meterPeaks = outputPeakLevels;
    int channelIndex = 0;
    for (const auto channel : toAudioChannelSet (layout).getChannelTypes())
    {
        context.meterLabels.add (
            juce::AudioChannelSet::getAbbreviatedChannelTypeName (channel));
        if (channelIndex < static_cast<int> (context.meterIsLfe.size()))
            context.meterIsLfe[static_cast<size_t> (channelIndex)] =
                channel == juce::AudioChannelSet::LFE;
        ++channelIndex;
    }
    context.extraLookup = [layoutName = toString (layout)] (
                              std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "output.layout") return SkinExpressionValue (layoutName);
        return std::nullopt;
    };
    static_cast<void> (adapter->drawCustomComponent (
        graphics, *this, SkinRenderTarget::outputStage, std::move (context), "stage"));
}
