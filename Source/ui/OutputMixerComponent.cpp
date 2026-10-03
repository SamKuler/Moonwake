#include "OutputMixerComponent.h"
#include "JuceSkinAdapter.h"

class OutputMixerComponent::Row final : public juce::Component
{
    struct Fader final : juce::Slider
    {
        explicit Fader (Row& ownerToUse) : owner (ownerToUse) {}
        void paint (juce::Graphics& g) override
        {
            SkinRenderContext context;
            context.normalisedValue = valueToProportionOfLength (getValue());
            context.minimum = getMinimum(); context.maximum = getMaximum();
            context.displayValue = getTextFromValue (getValue());
            owner.draw (g, *this, "row.gain", SkinRenderTarget::linearSlider, std::move (context));
        }
        void focusGained (FocusChangeType cause) override
        {
            juce::Slider::focusGained (cause);
            owner.reveal();
        }
        Row& owner;
    };
    struct Switch final : juce::ToggleButton
    {
        Switch (Row& ownerToUse, juce::String slotToUse, juce::String label)
            : juce::ToggleButton (std::move (label)), owner (ownerToUse), slot (std::move (slotToUse)) {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            SkinRenderContext context;
            context.primaryText = getButtonText(); context.state.selected = getToggleState();
            context.state.hover = over; context.state.pressed = down;
            owner.draw (g, *this, slot, SkinRenderTarget::toggleButton, std::move (context));
        }
        void focusGained (FocusChangeType cause) override
        {
            juce::ToggleButton::focusGained (cause);
            owner.reveal();
        }
        Row& owner;
        juce::String slot;
    };
public:
    Row (OutputMixerComponent& ownerToUse, juce::AudioChannelSet::ChannelType speakerToUse, int index)
        : speaker (speakerToUse), owner (ownerToUse), gain (*this), mute (*this, "row.mute", "M"), solo (*this, "row.solo", "S")
    {
        name = juce::AudioChannelSet::getAbbreviatedChannelTypeName (speaker);
        setTitle (name + " output channel");
        gain.setRange (-60.0, 6.0, 0.1);
        gain.setSliderStyle (juce::Slider::LinearHorizontal);
        gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.setScrollWheelEnabled (false);
        gain.setWantsKeyboardFocus (true);
        gain.setTitle (name + " gain"); gain.setTooltip ("Channel gain; double-click to reset to 0 dB");
        mute.setTitle ("Mute " + name); mute.setTooltip ("Mute " + name);
        solo.setTitle ("Solo " + name); solo.setTooltip ("Solo " + name + "; multiple channels may be soloed");
        gain.setExplicitFocusOrder (index * 3 + 1);
        mute.setExplicitFocusOrder (index * 3 + 2);
        solo.setExplicitFocusOrder (index * 3 + 3);
        gain.onValueChange = [this]
        {
            current.gain = gain.getValue() <= -60.0 ? 0.0f
                : juce::Decibels::decibelsToGain (static_cast<float> (gain.getValue()));
            notifyChange();
        };
        mute.onClick = solo.onClick = [this] { notifyChange(); };
        for (auto* control : std::array<juce::Component*, 3> { &gain, &mute, &solo }) addAndMakeVisible (control);
    }

    void update (const OutputChannelMix& state, float peakToUse)
    {
        if (current == state && juce::exactlyEqual (peak, peakToUse)) return;
        current = state;
        peak = peakToUse;
        gain.setValue (juce::Decibels::gainToDecibels (state.gain, -60.0f), juce::dontSendNotification);
        mute.setToggleState (state.muted, juce::dontSendNotification);
        solo.setToggleState (state.solo, juce::dontSendNotification);
        repaint();
    }

    void setPeak (float next)
    {
        if (! juce::exactlyEqual (peak, next)) { peak = next; repaint(); }
    }

    void reveal()
    {
        const auto view = owner.viewport.getViewArea();
        if (getY() < view.getY()) owner.viewport.setViewPosition (0, getY());
        else if (getBottom() > view.getBottom())
            owner.viewport.setViewPosition (0, getBottom() - view.getHeight());
    }

    void resized() override
    {
        if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&owner.getLookAndFeel()))
            adapter->applySlotStyle (gain, owner, SkinRenderTarget::channelMixer, "row.gain");
        gain.setBounds (slotBounds ("row.gain").getSmallestIntegerContainer());
        gain.resized();
        mute.setBounds (slotBounds ("row.mute").getSmallestIntegerContainer());
        solo.setBounds (slotBounds ("row.solo").getSmallestIntegerContainer());
    }

    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext surface;
        surface.bounds = surface.contentBounds = slotBounds ("row.surface");
        if (! surface.bounds.isEmpty()) draw (graphics, *this, "row.surface", SkinRenderTarget::surface, surface);
        const auto label = [&] (juce::StringRef slot, const juce::String& value, juce::Rectangle<float> bounds)
        {
            if (bounds.isEmpty()) return;
            SkinRenderContext context;
            context.bounds = bounds; context.contentBounds = bounds; context.primaryText = value;
            draw (graphics, *this, slot, SkinRenderTarget::label, std::move (context));
        };
        label ("row.source", name, slotBounds ("row.source"));
        label ("row.peak", peak > 0.0f ? juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS" : "-- dBFS",
               slotBounds ("row.peak"));
        label ("row.destination", gain.getValue() <= -60.0 ? "-inf dB" : juce::String (gain.getValue(), 1) + " dB",
               slotBounds ("row.destination"));
    }

    void draw (juce::Graphics& graphics, const juce::Component& control, juce::StringRef slot,
               SkinRenderTarget target, SkinRenderContext context)
    {
        auto* adapter = dynamic_cast<JuceSkinAdapter*> (&owner.getLookAndFeel());
        if (! adapter) return;
        if (context.bounds.isEmpty()) context.bounds = control.getLocalBounds().toFloat();
        if (context.contentBounds.isEmpty()) context.contentBounds = context.bounds;
        context.state.enabled = control.isEnabled();
        context.state.focused = JuceSkinAdapter::hasVisibleKeyboardFocus (control);
        context.state.hover = context.state.hover || control.isMouseOver();
        context.state.pressed = context.state.pressed || control.isMouseButtonDown();
        static_cast<void> (adapter->drawCustomSlot (graphics, owner, SkinRenderTarget::channelMixer, slot, target,
            std::move (context), "mixer:" + name + ":" + juce::String (slot)));
    }

    juce::AudioChannelSet::ChannelType speaker;
private:
    juce::Rectangle<float> slotBounds (const juce::String& slot) const
    {
        const auto found = owner.rowLayout.slots.find (slot);
        return found != owner.rowLayout.slots.end() ? found->second : juce::Rectangle<float>{};
    }

    void notifyChange()
    {
        if (owner.onChange)
            owner.onChange (speaker, current.gain, mute.getToggleState(), solo.getToggleState());
        repaint();
    }

    OutputMixerComponent& owner;
    OutputChannelMix current;
    juce::String name;
    float peak = 0.0f;
    Fader gain;
    Switch mute, solo;
};

OutputMixerComponent::OutputMixerComponent()
{
    setTitle ("Output channel mixer");
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}
OutputMixerComponent::~OutputMixerComponent() { viewport.setViewedComponent (nullptr, false); }
void OutputMixerComponent::setState (OutputLayout layout, const std::vector<OutputChannelMix>& settings,
                                    const std::array<float, maximumOutputChannels>& peaks)
{
    const auto next = toAudioChannelSet (layout);
    if (channels != next)
    {
        rows.clear(); channels = next;
        for (const auto channel : channels.getChannelTypes())
        {
            auto row = std::make_unique<Row> (*this, channel, static_cast<int> (rows.size()));
            content.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        resized();
    }
    for (size_t index = 0; index < rows.size(); ++index)
    {
        auto found = std::find_if (settings.begin(), settings.end(), [&] (const auto& setting) { return setting.channel == rows[index]->speaker; });
        if (found != settings.end()) rows[index]->update (*found, peaks[index]);
    }
}
void OutputMixerComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    if (getLocalBounds().isEmpty()) return;
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (! adapter) return;
    SkinRenderContext context;
    context.bounds = getLocalBounds().toFloat();
    context.tableRowCount = static_cast<int> (rows.size());
    const auto layout = adapter->rowLayoutFor (*this, SkinRenderTarget::channelMixer, context);
    if (! layout) return;
    const auto rowHeight = juce::jmax (1, juce::roundToInt (layout->rowHeight));
    const auto rowGap = juce::jmax (0, juce::roundToInt (layout->rowGap));
    const auto height = static_cast<int> (rows.size()) * (rowHeight + rowGap) - (rows.empty() ? 0 : rowGap);
    const auto width = juce::jmax (0, getWidth() - (height > getHeight() ? viewport.getScrollBarThickness() : 0));
    context.bounds.setWidth (static_cast<float> (width));
    rowLayout = adapter->rowLayoutFor (*this, SkinRenderTarget::channelMixer, context).value_or (*layout);
    const auto finalHeight = juce::jmax (1, juce::roundToInt (rowLayout.rowHeight));
    const auto finalGap = juce::jmax (0, juce::roundToInt (rowLayout.rowGap));
    content.setSize (width, static_cast<int> (rows.size()) * (finalHeight + finalGap) - (rows.empty() ? 0 : finalGap));
    for (size_t index = 0; index < rows.size(); ++index)
    {
        rows[index]->setBounds (0, static_cast<int> (index) * (finalHeight + finalGap), width, finalHeight);
        rows[index]->resized();
    }
}

void OutputMixerComponent::paint (juce::Graphics& graphics)
{
    if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel()))
        static_cast<void> (adapter->drawCustomComponent (
            graphics, *this, SkinRenderTarget::channelMixer, {}, "mixer"));
}

void OutputMixerComponent::setPeaks (const std::array<float, maximumOutputChannels>& peaks)
{
    for (size_t index = 0; index < rows.size(); ++index) rows[index]->setPeak (peaks[index]);
}
