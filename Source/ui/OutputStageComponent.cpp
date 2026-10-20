#include "OutputStageComponent.h"

#include "JuceSkinAdapter.h"
#include <cmath>

void OutputStageComponent::setState (
    OutputLayout layoutToUse,
    WorkspaceMode workspaceModeToUse,
    bool isPlaying,
    juce::String trackTitleToUse,
    juce::String deviceNameToUse,
    int availableChannelsToUse,
    std::array<float, maximumOutputChannels> outputPeakLevelsToUse)
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

void OutputStageComponent::setMeterReadings (OutputMeterReadings next)
{
    const auto changed = readings.samplePeak != next.samplePeak || readings.rms != next.rms
        || readings.truePeak != next.truePeak || readings.maxTruePeak != next.maxTruePeak
        || ! juce::exactlyEqual (readings.momentaryLufs, next.momentaryLufs) || ! juce::exactlyEqual (readings.shortTermLufs, next.shortTermLufs)
        || ! juce::exactlyEqual (readings.integratedLufs, next.integratedLufs) || readings.available != next.available
        || readings.measurementInterrupted != next.measurementInterrupted || readings.channels != next.channels
        || ! juce::exactlyEqual (readings.measuredSeconds, next.measuredSeconds);
    readings = std::move (next);
    if (changed) repaint();
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
    if (readings.available) context.meterPeaks = readings.samplePeak;
    context.meterRms = readings.rms;
    context.meterTruePeaks = readings.truePeak;
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
    context.extraLookup = [this, layoutName = toString (layout)] (
                              std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "output.layout") return SkinExpressionValue (layoutName);
        if (path == "meter.mode") return SkinExpressionValue (juce::String (
            mode == MeterMode::peak ? "peak" : mode == MeterMode::rms ? "rms" : "loudness"));
        if (path == "meter.available") return SkinExpressionValue (readings.available);
        if (path == "meter.interrupted") return SkinExpressionValue (readings.measurementInterrupted);
        if (path == "meter.status") return SkinExpressionValue (juce::String (
            readings.measurementInterrupted ? "Measurement restarted after a processing gap"
                                            : readings.available ? "Post effects / output gain · " + juce::String (readings.measuredSeconds, 1) + " s" : juce::String ("Waiting for audio")));
        const auto number = [] (double value) { return SkinExpressionValue (std::isfinite (value) ? value : -100.0); };
        const auto text = [] (double value) { return SkinExpressionValue (
            std::isfinite (value) ? juce::String (value, 1) : juce::String ("--")); };
        if (path == "meter.momentary") return number (readings.momentaryLufs);
        if (path == "meter.shortTerm") return number (readings.shortTermLufs);
        if (path == "meter.integrated") return number (readings.integratedLufs);
        if (path == "meter.momentaryText") return text (readings.momentaryLufs);
        if (path == "meter.shortTermText") return text (readings.shortTermLufs);
        if (path == "meter.integratedText") return text (readings.integratedLufs);
        if (path == "meter.truePeakText")
        {
            const auto maximum = *std::max_element (readings.maxTruePeak.begin(), readings.maxTruePeak.end());
            return text (maximum > 0.0f ? 20.0 * std::log10 (maximum) : -std::numeric_limits<double>::infinity());
        }
        return std::nullopt;
    };
    static_cast<void> (adapter->drawCustomComponent (
        graphics, *this, SkinRenderTarget::outputStage, std::move (context), "stage"));
}
