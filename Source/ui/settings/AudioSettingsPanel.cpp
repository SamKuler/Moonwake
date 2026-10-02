#include "AudioSettingsPanel.h"

#include "SettingsSkinContext.h"

#include "audio/OutputLayout.h"

#include <array>

namespace
{
juce::StringArray channelNames (OutputLayout layout)
{
    juce::StringArray names;
    for (const auto channel : toAudioChannelSet (layout).getChannelTypes())
        names.add (juce::AudioChannelSet::getAbbreviatedChannelTypeName (channel));
    return names;
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
        setInterceptsMouseClicks (true, true);
        getViewport()->getViewedComponent()->setInterceptsMouseClicks (false, false);
        setModel (this);
        setOutlineThickness (0);
        setRowHeight (1);
        setColour (juce::ListBox::backgroundColourId,
                   juce::Colour {});
        setColour (juce::ListBox::outlineColourId,
                   juce::Colour {});
        auto header = std::make_unique<Header> (*this);
        header->setSize (1, 0);
        setHeaderComponent (std::move (header));
    }

    ~ChannelMapView() override { setModel (nullptr); }

    void setState (OutputLayout layoutToUse, const std::vector<AudioDeviceOutputChannel>& activeOutputs)
    {
        names = channelNames (layoutToUse);
        outputs = activeOutputs;
        resized();
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
        if (names.isEmpty())
            if (const auto layout = rowLayout (getWidth()))
                drawTextSlot (graphics, *layout, "empty.message", "No output channels available.", "empty");
    }

    void resized() override
    {
        const auto applyRows = [this] (int width)
        {
            if (const auto layout = rowLayout (width))
            {
                setRowHeight (juce::jmax (1, juce::roundToInt (layout->rowHeight + layout->rowGap)));
                if (auto* header = getHeaderComponent())
                    header->setSize (getWidth(), juce::roundToInt (layout->headerHeight));
            }
        };
        juce::ListBox::resized();
        const auto width = getViewport()->getMaximumVisibleWidth();
        applyRows (width);
        juce::ListBox::resized();
        // Changing row metrics can reveal the scrollbar and cross a width breakpoint.
        if (const auto availableWidth = getViewport()->getMaximumVisibleWidth(); availableWidth != width)
        {
            applyRows (availableWidth);
            juce::ListBox::resized();
        }
    }

private:
    class Header final : public juce::Component
    {
    public:
        explicit Header (ChannelMapView& ownerToUse) : owner (ownerToUse) {}
        void paint (juce::Graphics& graphics) override
        {
            if (const auto layout = owner.rowLayout (owner.getViewport()->getMaximumVisibleWidth()))
                owner.drawTextSlot (graphics, *layout, "header.label", "Output routing", "heading");
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
        const auto connected = static_cast<size_t> (row) < outputs.size();
        return names[row] + " to "
             + (connected ? outputs[static_cast<size_t> (row)].name
                          : juce::String ("Unavailable"))
             + (connected ? ", connected" : ", unavailable");
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& graphics,
                           int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, names.size()))
            return;
        const auto layout = rowLayout (width);
        if (! layout) return;
        SkinRenderContext rowContext;
        rowContext.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
        rowContext.contentBounds = rowContext.bounds;
        const auto connected = static_cast<size_t> (rowNumber) < outputs.size();
        const auto sourceLabel = names[rowNumber];
        const auto deviceLabel = connected
                                   ? outputs[static_cast<size_t> (rowNumber)].name
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
        if (const auto surface = layout->slots.find ("row.surface"); surface != layout->slots.end())
        {
            auto surfaceContext = rowContext;
            surfaceContext.bounds = surface->second;
            surfaceContext.contentBounds = surfaceContext.bounds;
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::channelMap,
                "row.surface", SkinRenderTarget::surface, surfaceContext, rowKey));
        }
        drawTextSlot (graphics, *layout, "row.source", sourceLabel,
                      rowKey, &rowContext);
        drawTextSlot (graphics, *layout, "row.destination", deviceLabel,
                      rowKey, &rowContext);
    }

    [[nodiscard]] std::optional<SkinResolvedRowLayout> rowLayout (int width) const
    {
        if (adapter == nullptr) return std::nullopt;
        SkinRenderContext context;
        context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (getHeight()) };
        context.tableRowCount = names.size();
        context.tableColumnCount = 2;
        return adapter->rowLayoutFor (*this, SkinRenderTarget::channelMap, context);
    }

    void drawTextSlot (juce::Graphics& graphics, const SkinResolvedRowLayout& layout,
                       juce::StringRef slot, const juce::String& text,
                       const juce::String& key,
                       const SkinRenderContext* ownerContext = nullptr)
    {
        const auto frame = layout.slots.find (juce::String (slot));
        if (frame == layout.slots.end()) return;
        SkinRenderContext context;
        context.bounds = frame->second;
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
    std::vector<AudioDeviceOutputChannel> outputs;
};

AudioSettingsPanel::AudioSettingsPanel (PlayerController& controllerToUse,
                                        JuceSkinAdapter* adapter)
    : controller (controllerToUse),
      channelMap (std::make_unique<ChannelMapView> (adapter))
{
    headingLabel.setText ("Audio", juce::dontSendNotification);
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

    for (const auto& layout : outputLayouts())
        channelLayoutBox.addItem (juce::String (layout.name) + " / " + juce::String (layout.channels.size()) + " channels",
                                  static_cast<int> (layout.layout) + 1);

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
        deviceBox.addItem (availableDevices[index], index + 1);
    const auto deviceIndex = availableDevices.indexOf (settings.currentDevice);
    deviceBox.setSelectedItemIndex (deviceIndex, juce::dontSendNotification);

    availableSampleRates = settings.sampleRates;
    sampleRateBox.clear (juce::dontSendNotification);
    for (int index = 0; index < availableSampleRates.size(); ++index)
        sampleRateBox.addItem (sampleRateLabel (availableSampleRates[index]), index + 1);
    sampleRateBox.setSelectedItemIndex (
        availableSampleRates.indexOf (settings.currentSampleRate),
        juce::dontSendNotification);

    availableBufferSizes = settings.bufferSizes;
    bufferSizeBox.clear (juce::dontSendNotification);
    for (int index = 0; index < availableBufferSizes.size(); ++index)
        bufferSizeBox.addItem (juce::String (availableBufferSizes[index]) + " samples",
                               index + 1);
    bufferSizeBox.setSelectedItemIndex (
        availableBufferSizes.indexOf (settings.currentBufferSize),
        juce::dontSendNotification);

    channelLayoutBox.setSelectedId (static_cast<int> (viewState.outputLayout) + 1, juce::dontSendNotification);
    for (const auto& layout : outputLayouts())
        channelLayoutBox.setItemEnabled (static_cast<int> (layout.layout) + 1,
                                         layout.channels.size() <= settings.availableOutputChannels);
    channelLayoutBox.setTooltip ("Speaker layout; this device provides "
        + juce::String (settings.availableOutputChannels) + " output channels.");
    channelMap->setState (viewState.outputLayout, settings.activeOutputChannels);
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
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage(),
                  error.isNotEmpty());
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
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage(),
                  error.isNotEmpty());
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
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage(),
                  error.isNotEmpty());
}

void AudioSettingsPanel::applyChannelLayoutSelection()
{
    if (refreshing)
        return;
    const auto error = controller.setOutputLayout (static_cast<OutputLayout> (channelLayoutBox.getSelectedId() - 1));
    refresh();
    reportStatus (error.isNotEmpty() ? error : controller.getLastStatusMessage(),
                  error.isNotEmpty());
}

void AudioSettingsPanel::reportStatus (const juce::String& message, bool isError)
{
    if (statusCallback)
        statusCallback (message, isError);
}

juce::ComboBox& AudioSettingsPanel::deviceControl() noexcept { return deviceBox; }
juce::ComboBox& AudioSettingsPanel::sampleRateControl() noexcept { return sampleRateBox; }
juce::ComboBox& AudioSettingsPanel::bufferSizeControl() noexcept { return bufferSizeBox; }
juce::ComboBox& AudioSettingsPanel::channelLayoutControl() noexcept { return channelLayoutBox; }
juce::Label& AudioSettingsPanel::headingControl() noexcept { return headingLabel; }
juce::Component& AudioSettingsPanel::channelMapControl() noexcept { return *channelMap; }
int AudioSettingsPanel::displayedChannelCount() const noexcept { return channelMap->displayedCount(); }
