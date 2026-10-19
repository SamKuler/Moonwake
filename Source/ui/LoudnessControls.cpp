#include "ui/LoudnessControls.h"
#include <cmath>

LoudnessControls::LoudnessControls (PlayerController& player) : controller (player)
{
    heading.setText ("Loudness", juce::dontSendNotification);
    modeLabel.setText ("Normalization", juce::dontSendNotification);
    targetLabel.setText ("Target", juce::dontSendNotification);
    mode.setTitle ("Loudness normalization");
    mode.addItemList ({ "Off", "Track", "Album" }, 1);
    mode.setTooltip ("Album mode preserves level differences within an album; track data is used when album data is unavailable.");
    target.setTitle ("Target loudness");
    for (int lufs : { -23, -18, -16, -14 }) target.addItem (juce::String (lufs) + " LUFS", -lufs);
    preventClipping.setTooltip ("Uses source peaks to limit normalization gain with 1 dB of headroom. Unknown peaks are never boosted; routing and effects may add gain.");
    mode.onChange = target.onChange = preventClipping.onClick = [this] { applySettings(); };
    analyse.onClick = [this] { controller.startLoudnessScan(); refresh(); };
    cancel.onClick = [this] { controller.cancelLoudnessScan(); refresh(); };
    for (auto* button : { &original, &processed, &match }) button->setClickingTogglesState (true);
    original.setTitle ("Compare original audio"); processed.setTitle ("Compare effects output");
    original.setTooltip ("Listen to the latency-aligned original while effects continue processing.");
    processed.setTooltip ("Listen through the current effect chain.");
    original.onClick = [this] { controller.setAllEffectsBypassed (true); refresh(); };
    processed.onClick = [this] { controller.setAllEffectsBypassed (false); refresh(); };
    match.onClick = [this] { controller.setLoudnessComparisonEnabled (match.getToggleState()); refresh(); };
    match.setTooltip ("Measures original and effects together during playback, then holds compensation by attenuating the louder path. Plugin edits start a new measurement.");
    remeasure.onClick = [this] { controller.resetLoudnessComparison(); refresh(); };
    analysisStatus.setTitle ("Loudness analysis status"); comparisonStatus.setTitle ("Loudness comparison status");
}

void LoudnessControls::addTo (juce::Component& parent)
{
    for (auto* component : std::initializer_list<juce::Component*> {
        &heading, &modeLabel, &targetLabel, &analysisStatus, &comparisonStatus, &mode, &target,
        &preventClipping, &analyse, &cancel, &original, &processed, &match, &remeasure })
        parent.addAndMakeVisible (*component);
}

void LoudnessControls::bind (SkinComponentBindings& bindings, SkinValidationReport& report)
{
    const std::pair<const char*, juce::Component*> entries[] {
        { "settings.loudness.heading", &heading }, { "settings.loudness.modeLabel", &modeLabel },
        { "settings.loudness.targetLabel", &targetLabel }, { "settings.loudness.mode", &mode },
        { "settings.loudness.target", &target }, { "settings.loudness.preventClipping", &preventClipping },
        { "settings.loudness.analyze", &analyse }, { "settings.loudness.cancel", &cancel },
        { "settings.loudness.status", &analysisStatus }, { "effects.compare.original", &original },
        { "effects.compare.processed", &processed }, { "effects.compare.match", &match },
        { "effects.compare.reset", &remeasure }, { "effects.compare.status", &comparisonStatus }
    };
    for (const auto& [id, component] : entries) bindings.bind (id, *component, report);
}

void LoudnessControls::applySettings()
{
    if (refreshing || mode.getSelectedId() == 0 || target.getSelectedId() == 0) return;
    controller.setLoudnessPreferences ({ static_cast<LoudnessMode> (mode.getSelectedId() - 1),
                                        -static_cast<double> (target.getSelectedId()), preventClipping.getToggleState() });
    refresh();
}

void LoudnessControls::refresh (bool ready)
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto prefs = controller.getLoudnessPreferences();
    mode.setSelectedId (static_cast<int> (prefs.mode) + 1, juce::dontSendNotification);
    const auto targetId = juce::roundToInt (-prefs.targetLufs);
    if (target.indexOfItemId (targetId) < 0) target.addItem (juce::String (-targetId) + " LUFS", targetId);
    target.setSelectedId (targetId, juce::dontSendNotification);
    preventClipping.setToggleState (prefs.preventClipping, juce::dontSendNotification);
    mode.setEnabled (ready);
    target.setEnabled (ready && prefs.mode != LoudnessMode::off);
    preventClipping.setEnabled (ready && prefs.mode != LoudnessMode::off);
    const auto& scan = controller.getLoudnessScanStatus();
    analyse.setEnabled (ready && ! scan.running && ! controller.getLibrary().getTracks().empty());
    cancel.setEnabled (ready && scan.running);
    juce::String text;
    if (scan.running) text = "Analyzing " + juce::String (scan.completed) + " / " + juce::String (scan.total) + juce::String::fromUTF8 (" · ") + scan.currentFile;
    else
    {
        text = juce::String (controller.getMeasuredTrackCount()) + " / "
            + juce::String (controller.getLibrary().getTracks().size()) + " tracks ready";
        if (scan.cancelled) text += juce::String::fromUTF8 (" · Canceled; completed results kept");
        if (scan.failed > 0) text += juce::String::fromUTF8 (" · ") + juce::String (scan.failed) + " could not be measured";
    }
    analysisStatus.setText (text, juce::dontSendNotification);
    analysisStatus.setTooltip (scan.error.isNotEmpty() ? scan.error
        : "Tracks without loudness data keep their original level. Analysis updates Moonwake's cache and leaves audio files unchanged.");
    const auto comparison = controller.getLoudnessComparisonStatus();
    const auto effects = controller.getEffectChain().size() > 0;
    const auto bypassed = controller.areAllEffectsBypassed();
    original.setToggleState (bypassed, juce::dontSendNotification);
    processed.setToggleState (! bypassed, juce::dontSendNotification);
    original.setEnabled (ready && (effects || bypassed)); processed.setEnabled (ready && (effects || bypassed));
    match.setEnabled (ready && (effects || comparison.enabled));
    match.setToggleState (comparison.enabled, juce::dontSendNotification);
    remeasure.setEnabled (ready && comparison.enabled);
    if (! comparison.enabled) text = "";
    else if (comparison.interrupted) text = juce::String::fromUTF8 ("Measurement interrupted · Re-measure");
    else if (comparison.matched) text = juce::String::fromUTF8 ("Matched · ") + juce::String (comparison.dryGainDb < comparison.wetGainDb ? "Original " : "Effects ")
                                        + juce::String (juce::jmin (comparison.dryGainDb, comparison.wetGainDb), 1) + " dB";
    else if (! comparison.capturing) text = juce::String::fromUTF8 ("Finalizing level match…");
    else if (comparison.measuredSeconds >= 3.0) text = "Waiting for measurable audio";
    else text = comparison.measuredSeconds < 0.1 ? "Play audio to measure" : juce::String::fromUTF8 ("Measuring… ") + juce::String (comparison.measuredSeconds, 1) + " s";
    comparisonStatus.setText (text, juce::dontSendNotification);
    comparisonStatus.setTooltip (text);
}
