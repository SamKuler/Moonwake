#include "AudioSettingsPanel.h"

#include "SettingsSkinContext.h"

#include "audio/OutputLayout.h"

#include <array>

namespace
{
juce::StringArray channelNames (OutputLayout layout)
{
    switch (layout)
    {
        case OutputLayout::fiveOne:
            return { "L", "R", "C", "LFE", "Ls", "Rs" };
        case OutputLayout::fiveOneTwo:
            return { "L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf" };
        case OutputLayout::sevenOneTwo:
            return { "L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs", "Ltf", "Rtf" };
        case OutputLayout::stereo:
            return { "L", "R" };
    }
    return { "L", "R" };
}

juce::String sampleRateLabel (double rate)
{
    return rate >= 1000.0 ? juce::String (rate / 1000.0, rate == 44100.0 ? 1 : 0) + " kHz"
                          : juce::String (rate, 0) + " Hz";
}
} // namespace

class AudioSettingsPanel::ChannelMapView final : public juce::ListBox,
                                                 private juce::ListBoxModel
{
public:
    explicit ChannelMapView (JuceSkinAdapter* adapterToUse)
        : juce::ListBox ("Output channel map", nullptr),
          adapter (adapterToUse)
    {
        setName ("Read-only output channel map");
        setTitle ("Output channel map");
        setDescription ("Shows how the selected multichannel layout maps to CoreAudio outputs.");
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setInterceptsMouseClicks (false, false);
        setModel (this);
        setRowHeight (rowHeight);
        setColour (juce::ListBox::backgroundColourId,
                   juce::Colour {});
        setColour (juce::ListBox::outlineColourId,
                   juce::Colour {});
        auto header = std::make_unique<Header> (*this);
        header->setSize (1, headerHeight);
        setHeaderComponent (std::move (header));
    }

    ~ChannelMapView() override { setModel (nullptr); }

    void setState (OutputLayout layoutToUse, int availableOutputs)
    {
        names = channelNames (layoutToUse);
        availableChannelCount = juce::jmax (0, availableOutputs);
        updateRowHeight();
        updateContent();
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (
                juce::AccessibilityEvent::structureChanged);
    }

    [[nodiscard]] int displayedCount() const noexcept
    {
        return names.size();
    }

    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext background;
        background.bounds = getLocalBounds().toFloat();
        background.contentBounds = background.bounds;
        background.state.enabled = isEnabled();
        background.state.focused = hasKeyboardFocus (true);
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomComponent (
                graphics, *this, SkinRenderTarget::channelMap,
                background, "settings.audio.channelMap"));

        juce::ListBox::paint (graphics);
    }

    void resized() override
    {
        updateRowHeight();
        juce::ListBox::resized();
    }

private:
    class Header final : public juce::Component
    {
    public:
        explicit Header (ChannelMapView& ownerToUse) : owner (ownerToUse) {}
        void paint (juce::Graphics& graphics) override
        {
            owner.drawTextSlot (graphics, getLocalBounds(), "row.source",
                                "OUTPUT MAP  /  READ ONLY", "heading");
        }

        std::unique_ptr<juce::AccessibilityHandler>
            createAccessibilityHandler() override
        {
            return juce::Component::createIgnoredAccessibilityHandler (*this);
        }

    private:
        ChannelMapView& owner;
    };

    int getNumRows() override { return names.size(); }

    juce::String getNameForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, names.size()))
            return {};
        const auto connected = row < availableChannelCount;
        return names[row] + " to "
             + (connected ? "CoreAudio Out " + juce::String (row + 1)
                          : juce::String ("Unavailable"))
             + (connected ? ", connected" : ", unavailable");
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& graphics,
                           int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, names.size()))
            return;
        auto row = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);
        SkinRenderContext rowContext;
        rowContext.bounds = row.toFloat();
        rowContext.contentBounds = rowContext.bounds;
        const auto connected = rowNumber < availableChannelCount;
        const auto sourceLabel = names[rowNumber];
        const auto deviceLabel = connected
                                   ? "CoreAudio Out " + juce::String (rowNumber + 1)
                                   : juce::String ("Unavailable");
        rowContext.state.enabled = connected;
        rowContext.extraLookup = [rowNumber, sourceLabel,
                                  deviceLabel, connected] (std::string_view path)
            -> std::optional<SkinExpressionValue>
        {
            return SettingsSkinContext::channelMapValue (
                path, rowNumber, sourceLabel, deviceLabel, connected);
        };
        const auto rowKey = "channel:" + juce::String (rowNumber);
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::channelMap,
                "row.surface", SkinRenderTarget::surface, rowContext, rowKey));

        auto sourceBounds = row.removeFromLeft (
            juce::jmax (52, row.getWidth() / 3));
        auto destinationBounds = row.removeFromRight (
            juce::jmax (72, row.getWidth() / 2));
        drawTextSlot (graphics, sourceBounds, "row.source", sourceLabel,
                      rowKey, &rowContext);
        drawTextSlot (graphics, destinationBounds, "row.destination", deviceLabel,
                      rowKey, &rowContext);
    }

    void updateRowHeight()
    {
        const auto contentHeight = juce::jmax (0, getHeight() - headerHeight - 16);
        setRowHeight (names.isEmpty()
                          ? rowHeight
                          : juce::jlimit (30, 44, contentHeight / names.size()));
    }

    void drawTextSlot (juce::Graphics& graphics, juce::Rectangle<int> bounds,
                       juce::StringRef slot, const juce::String& text,
                       const juce::String& key,
                       const SkinRenderContext* ownerContext = nullptr)
    {
        SkinRenderContext context;
        context.bounds = bounds.toFloat();
        context.contentBounds = context.bounds;
        context.primaryText = text;
        context.state.enabled = isEnabled();
        if (ownerContext != nullptr)
            context.extraLookup = ownerContext->extraLookup;
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::channelMap,
                slot, SkinRenderTarget::label, context,
                key + ":" + juce::String (slot)));
    }

    JuceSkinAdapter* adapter = nullptr;
    juce::StringArray names;
    int availableChannelCount = 0;
    static constexpr int headerHeight = 28;
    static constexpr int rowHeight = 36;
};

AudioSettingsPanel::AudioSettingsPanel (PlayerController& controllerToUse,
                                        JuceSkinAdapter* adapter)
    : controller (controllerToUse),
      channelMap (std::make_unique<ChannelMapView> (adapter))
{
    headingLabel.setText ("AUDIO  /  CORE AUDIO", juce::dontSendNotification);
    headingLabel.setName ("Audio settings");
    headingLabel.setTitle ("Audio and CoreAudio settings");
    headingLabel.setJustificationType (juce::Justification::centredLeft);

    deviceBox.setTextWhenNothingSelected ("Output device");
    sampleRateBox.setTextWhenNothingSelected ("Sample rate");
    bufferSizeBox.setTextWhenNothingSelected ("Buffer size");
    channelLayoutBox.setTextWhenNothingSelected ("Channel layout");

    deviceBox.setTitle ("CoreAudio output device");
    sampleRateBox.setTitle ("Audio sample rate");
    bufferSizeBox.setTitle ("Audio buffer size");
    channelLayoutBox.setTitle ("Multichannel output layout");

    channelLayoutBox.addItem ("CHANNEL LAYOUT  /  Stereo / 2 channels", 1);
    channelLayoutBox.addItem ("CHANNEL LAYOUT  /  5.1 / 6 channels", 2);
    channelLayoutBox.addItem ("CHANNEL LAYOUT  /  5.1.2 / 8 channels", 3);
    channelLayoutBox.addItem ("CHANNEL LAYOUT  /  7.1.2 / 10 channels", 4);

    deviceBox.onChange = [this] { applyDeviceSelection(); };
    sampleRateBox.onChange = [this] { applySampleRateSelection(); };
    bufferSizeBox.onChange = [this] { applyBufferSizeSelection(); };
    channelLayoutBox.onChange = [this] { applyChannelLayoutSelection(); };
    refresh();
}

AudioSettingsPanel::~AudioSettingsPanel() = default;

void AudioSettingsPanel::setStatusCallback (StatusCallback callback)
{
    statusCallback = std::move (callback);
}

void AudioSettingsPanel::addTo (juce::Component& parent)
{
    for (auto* component : std::array<juce::Component*, 6> {
             &headingLabel, &deviceBox, &sampleRateBox, &bufferSizeBox, &channelLayoutBox,
             channelMap.get() })
        parent.addAndMakeVisible (component);
}

void AudioSettingsPanel::bind (SkinComponentBindings& bindings,
                               SkinValidationReport& report)
{
    static_cast<void> (bindings.bind ("settings.audio.heading", headingLabel, report));
    static_cast<void> (bindings.bind ("settings.audio.device", deviceBox, report));
    static_cast<void> (bindings.bind ("settings.audio.sampleRate", sampleRateBox, report));
    static_cast<void> (bindings.bind ("settings.audio.bufferSize", bufferSizeBox, report));
    static_cast<void> (bindings.bind ("settings.audio.channelLayout", channelLayoutBox, report));
    static_cast<void> (bindings.bind ("settings.audio.channelMap", *channelMap, report));
}

void AudioSettingsPanel::setVisible (bool shouldBeVisible)
{
    headingLabel.setVisible (shouldBeVisible);
    deviceBox.setVisible (shouldBeVisible);
    sampleRateBox.setVisible (shouldBeVisible);
    bufferSizeBox.setVisible (shouldBeVisible);
    channelLayoutBox.setVisible (shouldBeVisible);
    channelMap->setVisible (shouldBeVisible);
}

void AudioSettingsPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto settings = controller.getAudioDeviceSettings();
    const auto viewState = controller.getViewState();

    availableDevices = settings.devices;
    deviceBox.clear (juce::dontSendNotification);
    for (int index = 0; index < availableDevices.size(); ++index)
        deviceBox.addItem ("OUTPUT DEVICE  /  " + availableDevices[index], index + 1);
    const auto deviceIndex = availableDevices.indexOf (settings.currentDevice);
    deviceBox.setSelectedItemIndex (deviceIndex, juce::dontSendNotification);

    availableSampleRates = settings.sampleRates;
    sampleRateBox.clear (juce::dontSendNotification);
    for (int index = 0; index < availableSampleRates.size(); ++index)
        sampleRateBox.addItem ("SAMPLE RATE  /  " + sampleRateLabel (availableSampleRates[index]),
                               index + 1);
    sampleRateBox.setSelectedItemIndex (
        availableSampleRates.indexOf (settings.currentSampleRate),
        juce::dontSendNotification);

    availableBufferSizes = settings.bufferSizes;
    bufferSizeBox.clear (juce::dontSendNotification);
    for (int index = 0; index < availableBufferSizes.size(); ++index)
        bufferSizeBox.addItem ("BUFFER SIZE  /  " + juce::String (availableBufferSizes[index])
                                   + " samples",
                               index + 1);
    bufferSizeBox.setSelectedItemIndex (
        availableBufferSizes.indexOf (settings.currentBufferSize),
        juce::dontSendNotification);

    auto layoutId = 1;
    switch (viewState.outputLayout)
    {
        case OutputLayout::fiveOne: layoutId = 2; break;
        case OutputLayout::fiveOneTwo: layoutId = 3; break;
        case OutputLayout::sevenOneTwo: layoutId = 4; break;
        case OutputLayout::stereo: layoutId = 1; break;
    }
    channelLayoutBox.setSelectedId (layoutId, juce::dontSendNotification);
    channelMap->setState (viewState.outputLayout, settings.availableOutputChannels);
}

void AudioSettingsPanel::applyDeviceSelection()
{
    if (refreshing || deviceBox.getSelectedId() <= 0)
        return;
    const auto index = deviceBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, availableDevices.size()))
        return;
    const auto error = controller.selectOutputDevice (availableDevices[index]);
    refresh();
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage());
}

void AudioSettingsPanel::applySampleRateSelection()
{
    if (refreshing)
        return;
    const auto index = sampleRateBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, availableSampleRates.size()))
        return;
    const auto error = controller.setAudioSampleRate (availableSampleRates[index]);
    refresh();
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage());
}

void AudioSettingsPanel::applyBufferSizeSelection()
{
    if (refreshing)
        return;
    const auto index = bufferSizeBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (index, availableBufferSizes.size()))
        return;
    const auto error = controller.setAudioBufferSize (availableBufferSizes[index]);
    refresh();
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage());
}

void AudioSettingsPanel::applyChannelLayoutSelection()
{
    if (refreshing)
        return;
    juce::String error;
    switch (channelLayoutBox.getSelectedId())
    {
        case 2: error = controller.setOutputLayout (OutputLayout::fiveOne); break;
        case 3: error = controller.setOutputLayout (OutputLayout::fiveOneTwo); break;
        case 4: error = controller.setOutputLayout (OutputLayout::sevenOneTwo); break;
        default: error = controller.setOutputLayout (OutputLayout::stereo); break;
    }
    refresh();
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage());
}

void AudioSettingsPanel::reportStatus (const juce::String& message)
{
    if (statusCallback)
        statusCallback (message);
}

juce::ComboBox& AudioSettingsPanel::deviceControl() noexcept { return deviceBox; }
juce::ComboBox& AudioSettingsPanel::sampleRateControl() noexcept { return sampleRateBox; }
juce::ComboBox& AudioSettingsPanel::bufferSizeControl() noexcept { return bufferSizeBox; }
juce::ComboBox& AudioSettingsPanel::channelLayoutControl() noexcept { return channelLayoutBox; }
juce::Label& AudioSettingsPanel::headingControl() noexcept { return headingLabel; }
juce::Component& AudioSettingsPanel::channelMapControl() noexcept { return *channelMap; }
int AudioSettingsPanel::displayedChannelCount() const noexcept { return channelMap->displayedCount(); }
