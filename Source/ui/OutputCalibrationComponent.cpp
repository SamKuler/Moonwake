#include "OutputCalibrationComponent.h"
#include "JuceSkinAdapter.h"

#include <cmath>
#include <cstdlib>

namespace
{
std::optional<double> numberInRange (const juce::String& text, double minimum, double maximum)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty()) return std::nullopt;
    const auto utf8 = trimmed.toRawUTF8();
    char* end = nullptr;
    const auto value = std::strtod (utf8, &end);
    if (end == utf8 || *end != '\0' || ! std::isfinite (value) || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}
}

OutputCalibrationComponent::OutputCalibrationComponent()
{
    setTitle ("Output calibration");
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    const std::array<std::pair<juce::Label*, const char*>, 7> labels {{
        { &channelLabel, "Speaker" }, { &trimLabel, "Trim (dB)" }, { &delayLabel, "Delay (ms)" },
        { &signalLabel, "Test signal" }, { &levelLabel, "Level (dBFS)" },
        { &frequencyLabel, "Tone (Hz)" }, { &status, "Select a speaker to calibrate." }
    }};
    for (const auto& [label, caption] : labels)
    {
        label->setText (caption, juce::dontSendNotification);
        label->setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (label);
    }
    int focus = 1;
    for (auto* control : std::array<juce::Component*, 8> { &channel, &trim, &delay, &polarity, &signalKind, &level, &frequency, &startStop })
    {
        control->setExplicitFocusOrder (focus++);
        content.addAndMakeVisible (control);
    }
    channel.setTitle ("Calibration speaker");
    channel.setTextWhenNoChoicesAvailable ("No output speakers");
    trim.setTitle ("Speaker trim in decibels");
    trim.setTooltip ("-24 to +12 dB");
    delay.setTitle ("Speaker delay in milliseconds");
    delay.setTooltip ("0 to 200 ms");
    polarity.setTitle ("Speaker polarity");
    polarity.setClickingTogglesState (true);
    polarity.setTooltip ("Invert the selected speaker's polarity");
    signalKind.setTitle ("Calibration test signal");
    signalKind.addItem ("Pink noise", 1);
    signalKind.addItem ("Sine tone", 2);
    signalKind.setSelectedId (1, juce::dontSendNotification);
    level.setTitle ("Test signal level in dBFS");
    level.setTooltip ("-60 to -12 dBFS");
    level.setText ("-30", false);
    frequency.setTitle ("Sine frequency in hertz");
    frequency.setTooltip ("20 to 20000 Hz; LFE starts at 60 Hz");
    frequency.setText ("1000", false);
    startStop.setTitle ("Start or stop calibration test signal");
    channel.onChange = [this] { if (! refreshing) selectChannel(); };
    for (auto* editor : { &trim, &delay })
    {
        editor->onReturnKey = [this] { commitCalibration(); };
        editor->onFocusLost = [this, editor]
        {
            if (! refreshing && ! editor->hasKeyboardFocus (true)) commitCalibration();
        };
        editor->onEscapeKey = [this] { escapeEditing(); };
    }
    for (auto* editor : { &level, &frequency })
    {
        editor->onReturnKey = [this] { commitTestParameters(); };
        editor->onFocusLost = [this, editor]
        {
            if (! refreshing && ! editor->hasKeyboardFocus (true)) commitTestParameters();
        };
        editor->onEscapeKey = [this] { escapeEditing(); };
    }
    polarity.onClick = [this] { commitCalibration(); };
    signalKind.onChange = [this] { if (! refreshing) commitTestParameters(); };
    startStop.onClick = [this] { toggleTest(); };
    refreshTest();
}

OutputCalibrationComponent::~OutputCalibrationComponent()
{
    refreshing = true;
    for (auto* editor : { &trim, &delay, &level, &frequency })
    {
        editor->onReturnKey = {}; editor->onFocusLost = {}; editor->onEscapeKey = {};
    }
    viewport.setViewedComponent (nullptr, false);
}

void OutputCalibrationComponent::Editor::focusGained (FocusChangeType cause)
{
    juce::TextEditor::focusGained (cause);
    owner.reveal (*this);
}
void OutputCalibrationComponent::Selector::focusGained (FocusChangeType cause)
{
    juce::ComboBox::focusGained (cause);
    owner.reveal (*this);
}
void OutputCalibrationComponent::Button::focusGained (FocusChangeType cause)
{
    juce::TextButton::focusGained (cause);
    owner.reveal (*this);
}
void OutputCalibrationComponent::reveal (juce::Component& control)
{
    const auto view = viewport.getViewArea();
    if (control.getY() < view.getY()) viewport.setViewPosition (0, control.getY());
    else if (control.getBottom() > view.getBottom()) viewport.setViewPosition (0, control.getBottom() - view.getHeight());
}

juce::AudioChannelSet::ChannelType OutputCalibrationComponent::selectedChannel() const
{
    const auto index = channel.getSelectedItemIndex();
    return index >= 0 && index < channels.size() ? channels.getTypeOfChannel (index) : juce::AudioChannelSet::unknown;
}

void OutputCalibrationComponent::setState (OutputLayout layout, const std::vector<OutputChannelCalibration>& next, CalibrationTestSignal signal)
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto nextChannels = toAudioChannelSet (layout);
    const auto previousChannel = selectedChannel();
    const auto changed = nextChannels != channels;
    const auto calibrationChanged = settings != next;
    settings = next;
    if (changed)
    {
        channels = nextChannels;
        channel.clear (juce::dontSendNotification);
        int selection = 1;
        for (int index = 0; index < channels.size(); ++index)
        {
            const auto speaker = channels.getTypeOfChannel (index);
            channel.addItem (juce::AudioChannelSet::getChannelTypeName (speaker), index + 1);
            if (speaker == previousChannel) selection = index + 1;
        }
        channel.setSelectedId (channels.isDisabled() ? 0 : selection, juce::dontSendNotification);
    }
    const auto testChanged = signal.kind != testSignal.kind || signal.channel != testSignal.channel
        || ! juce::exactlyEqual (signal.levelDbFS, testSignal.levelDbFS)
        || ! juce::exactlyEqual (signal.frequencyHz, testSignal.frequencyHz);
    testSignal = signal;
    if (changed || calibrationChanged) refreshChannel();
    if (signal.kind != CalibrationTestSignal::Kind::off && testChanged)
    {
        const auto levelUnchanged = level.getText() == juce::String (acceptedLevelDbFS, 1)
            || numberInRange (level.getText(), acceptedLevelDbFS, acceptedLevelDbFS).has_value();
        const auto frequencyUnchanged = frequency.getText() == juce::String (acceptedFrequencyHz, 0)
            || numberInRange (frequency.getText(), acceptedFrequencyHz, acceptedFrequencyHz).has_value();
        acceptedSignalKind = signal.kind;
        acceptedLevelDbFS = signal.levelDbFS;
        acceptedFrequencyHz = signal.frequencyHz;
        signalKind.setSelectedId (signal.kind == CalibrationTestSignal::Kind::pinkNoise ? 1 : 2, juce::dontSendNotification);
        if (levelUnchanged) level.setText (juce::String (signal.levelDbFS, 1), false);
        if (frequencyUnchanged) frequency.setText (juce::String (signal.frequencyHz, 0), false);
    }
    if (testChanged || changed) refreshTest();
}

void OutputCalibrationComponent::selectChannel()
{
    if (testSignal.kind != CalibrationTestSignal::Kind::off)
    {
        auto stopped = testSignal;
        stopped.kind = CalibrationTestSignal::Kind::off;
        if (! sendTest (stopped)) return;
    }
    const auto lfe = selectedChannel() == juce::AudioChannelSet::LFE || selectedChannel() == juce::AudioChannelSet::LFE2;
    acceptedSignalKind = lfe ? CalibrationTestSignal::Kind::sine : CalibrationTestSignal::Kind::pinkNoise;
    signalKind.setSelectedId (lfe ? 2 : 1, juce::dontSendNotification);
    acceptedFrequencyHz = lfe ? 60.0 : 1000.0;
    frequency.setText (juce::String (acceptedFrequencyHz, 0), false);
    refreshChannel();
    refreshTest();
}

void OutputCalibrationComponent::discardEdits()
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    refreshChannel();
    signalKind.setSelectedId (acceptedSignalKind == CalibrationTestSignal::Kind::pinkNoise ? 1 : 2, juce::dontSendNotification);
    level.setText (juce::String (acceptedLevelDbFS, 1), false);
    frequency.setText (juce::String (acceptedFrequencyHz, 0), false);
    refreshTest();
}

void OutputCalibrationComponent::escapeEditing()
{
    if (testSignal.kind != CalibrationTestSignal::Kind::off)
    {
        auto stopped = testSignal;
        stopped.kind = CalibrationTestSignal::Kind::off;
        if (! sendTest (stopped)) return;
    }
    discardEdits();
}

void OutputCalibrationComponent::refreshChannel()
{
    const auto speaker = selectedChannel();
    auto value = OutputChannelCalibration { speaker };
    for (const auto& setting : settings) if (setting.channel == speaker) value = setting;
    trim.setText (juce::String (value.trimDb, 1), false);
    delay.setText (juce::String (value.delayMs, 2), false);
    polarity.setToggleState (value.inverted, juce::dontSendNotification);
    polarity.setButtonText (value.inverted ? "Polarity: inverted" : "Polarity: normal");
    for (auto* control : std::array<juce::Component*, 7> { &trim, &delay, &polarity, &signalKind, &level, &frequency, &startStop })
        control->setEnabled (speaker != juce::AudioChannelSet::unknown);
}

void OutputCalibrationComponent::showError (juce::String message)
{
    status.setText (std::move (message), juce::dontSendNotification);
    status.setTooltip (status.getText());
    status.setTitle ("Calibration error: " + status.getText());
    reveal (status);
}

bool OutputCalibrationComponent::commitCalibration()
{
    if (refreshing || selectedChannel() == juce::AudioChannelSet::unknown) return false;
    const auto trimValue = numberInRange (trim.getText(), minimumCalibrationTrimDb, maximumCalibrationTrimDb);
    const auto delayValue = numberInRange (delay.getText(), 0.0, maximumCalibrationDelayMs);
    if (! trimValue) { showError ("Trim: enter -24 to +12 dB."); return false; }
    if (! delayValue) { showError ("Delay: enter 0 to 200 ms."); return false; }
    auto previous = OutputChannelCalibration { selectedChannel() };
    for (const auto& setting : settings) if (setting.channel == selectedChannel()) previous = setting;
    const auto exactTrim = trim.getText() == juce::String (previous.trimDb, 1) ? previous.trimDb : static_cast<float> (*trimValue);
    const auto exactDelay = delay.getText() == juce::String (previous.delayMs, 2) ? previous.delayMs : *delayValue;
    const OutputChannelCalibration value { selectedChannel(), exactTrim, exactDelay, polarity.getToggleState() };
    auto found = std::find_if (settings.begin(), settings.end(), [&] (const auto& entry) { return entry.channel == value.channel; });
    const auto changed = found == settings.end() || *found != value;
    if (changed && onChange)
    {
        const auto result = onChange (value);
        if (result.failed()) { showError (result.getErrorMessage()); return false; }
    }
    found = std::find_if (settings.begin(), settings.end(), [&] (const auto& entry) { return entry.channel == value.channel; });
    if (found != settings.end()) *found = value; else settings.push_back (value);
    polarity.setButtonText (value.inverted ? "Polarity: inverted" : "Polarity: normal");
    refreshTest();
    return true;
}

bool OutputCalibrationComponent::commitTestParameters (bool starting)
{
    const auto levelValue = numberInRange (level.getText(), -60.0, -12.0);
    const auto frequencyValue = numberInRange (frequency.getText(), 20.0, 20000.0);
    const auto requestedKind = signalKind.getSelectedId() == 1 ? CalibrationTestSignal::Kind::pinkNoise : CalibrationTestSignal::Kind::sine;
    const auto active = starting || testSignal.kind != CalibrationTestSignal::Kind::off;
    auto proposed = testSignal;
    auto result = juce::Result::ok();
    if (! levelValue) result = juce::Result::fail ("Level: enter -60 to -12 dBFS.");
    else if (! frequencyValue) result = juce::Result::fail ("Frequency: enter 20 to 20000 Hz.");
    else
    {
        proposed.levelDbFS = level.getText() == juce::String (acceptedLevelDbFS, 1)
            ? acceptedLevelDbFS : static_cast<float> (*levelValue);
        proposed.frequencyHz = frequency.getText() == juce::String (acceptedFrequencyHz, 0)
            ? acceptedFrequencyHz : *frequencyValue;
        if (active)
        {
            proposed.kind = requestedKind;
            if (starting) proposed.channel = selectedChannel();
            result = onTestChange ? onTestChange (proposed, starting) : juce::Result::fail ("Test signal is unavailable.");
        }
    }
    if (result.failed())
    {
        signalKind.setSelectedId (acceptedSignalKind == CalibrationTestSignal::Kind::pinkNoise ? 1 : 2, juce::dontSendNotification);
        refreshTest();
        showError (result.getErrorMessage());
        return false;
    }
    if (active) testSignal = proposed;
    acceptedSignalKind = active && proposed.kind != CalibrationTestSignal::Kind::off ? proposed.kind : requestedKind;
    acceptedLevelDbFS = proposed.levelDbFS;
    acceptedFrequencyHz = proposed.frequencyHz;
    signalKind.setSelectedId (acceptedSignalKind == CalibrationTestSignal::Kind::pinkNoise ? 1 : 2, juce::dontSendNotification);
    level.setText (juce::String (acceptedLevelDbFS, 1), false);
    frequency.setText (juce::String (acceptedFrequencyHz, 0), false);
    refreshTest();
    return true;
}

bool OutputCalibrationComponent::sendTest (CalibrationTestSignal proposed)
{
    const auto result = onTestChange ? onTestChange (proposed, false) : juce::Result::fail ("Test signal is unavailable.");
    if (result.failed()) { showError (result.getErrorMessage()); return false; }
    testSignal = proposed;
    refreshTest();
    return true;
}

void OutputCalibrationComponent::toggleTest()
{
    if (testSignal.kind != CalibrationTestSignal::Kind::off)
    {
        auto proposed = testSignal;
        proposed.kind = CalibrationTestSignal::Kind::off;
        sendTest (proposed);
    }
    else if (commitCalibration()) commitTestParameters (true);
}

void OutputCalibrationComponent::refreshTest()
{
    const auto active = testSignal.kind != CalibrationTestSignal::Kind::off;
    startStop.setButtonText (active ? "Stop test" : "Start test");
    frequency.setEnabled (selectedChannel() != juce::AudioChannelSet::unknown && signalKind.getSelectedId() == 2);
    const auto speaker = active ? testSignal.channel : selectedChannel();
    status.setText (speaker == juce::AudioChannelSet::unknown ? "No output speaker available."
        : (active ? "Testing: " : "Ready: ") + juce::AudioChannelSet::getChannelTypeName (speaker), juce::dontSendNotification);
    status.setTitle (status.getText());
    status.setTooltip (status.getText());
}

void OutputCalibrationComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    if (getLocalBounds().isEmpty()) return;
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (adapter == nullptr) return;
    SkinRenderContext context;
    context.bounds = getLocalBounds().toFloat();
    auto layout = adapter->rowLayoutFor (*this, SkinRenderTarget::outputCalibration, context);
    if (! layout) return;
    const auto height = juce::roundToInt (layout->rowHeight);
    const auto width = juce::jmax (0, getWidth() - (height > getHeight() ? viewport.getScrollBarThickness() : 0));
    context.bounds.setWidth (static_cast<float> (width));
    layout = adapter->rowLayoutFor (*this, SkinRenderTarget::outputCalibration, context);
    if (! layout) return;
    content.setSize (width, juce::roundToInt (layout->rowHeight));
    for (const auto& [control, slot] : std::array<std::pair<juce::Component*, const char*>, 15> {{
        { &channelLabel, "calibration.channelLabel" }, { &channel, "calibration.channel" },
        { &trimLabel, "calibration.trimLabel" }, { &trim, "calibration.trim" },
        { &delayLabel, "calibration.delayLabel" }, { &delay, "calibration.delay" },
        { &polarity, "calibration.polarity" }, { &signalLabel, "calibration.signalLabel" },
        { &signalKind, "calibration.signal" }, { &levelLabel, "calibration.levelLabel" },
        { &level, "calibration.level" }, { &frequencyLabel, "calibration.frequencyLabel" },
        { &frequency, "calibration.frequency" }, { &startStop, "calibration.startStop" }, { &status, "calibration.status" }
    }})
    {
        adapter->applySlotStyle (*control, *this, SkinRenderTarget::outputCalibration, slot);
        const auto found = layout->slots.find (slot);
        control->setBounds (found == layout->slots.end() ? juce::Rectangle<int>{} : found->second.getSmallestIntegerContainer());
    }
    for (auto* editor : { &trim, &delay, &level, &frequency }) adapter->configureTextEditor (*editor);
}

void OutputCalibrationComponent::paint (juce::Graphics& graphics)
{
    if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel()))
        static_cast<void> (adapter->drawCustomComponent (graphics, *this, SkinRenderTarget::outputCalibration, {}));
}
