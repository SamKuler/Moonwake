#include "ui/OutputMixerComponent.h"
#include "ui/JuceSkinAdapter.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"

namespace
{
template <typename Control>
Control* findControl (juce::Component& component, const juce::String& title)
{
    if (component.getTitle() == title)
        if (auto* found = dynamic_cast<Control*> (&component)) return found;
    for (auto* child : component.getChildren())
        if (auto* found = findControl<Control> (*child, title)) return found;
    return nullptr;
}

class OutputMixerTests final : public juce::UnitTest
{
public:
    OutputMixerTests() : UnitTest ("OutputMixer", "skin") {}
    void runTest() override
    {
        beginTest ("skin fixtures compile with mixer control geometry");
        SkinValidationReport report;
        const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
        const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
        const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
        const auto skin = package && foundation ? SkinCompiler::compile (*package, foundation, report) : nullptr;
        expect (skin != nullptr, report.toText());
        if (! skin) return;
        struct Faults final : SkinRuntimeFaultSink
        {
            void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override { ++count; }
            int count = 0;
        } faults;
        SkinRenderer renderer;
        auto activeSkin = skin;
        JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
        {
            return JuceSkinAdapter::RuntimeBinding { activeSkin->resolveStyle (target,
                style.isEmpty() ? "channelMixer.studio" : style), nullptr, &faults, true };
        }, renderer);
        OutputMixerComponent mixer;
        mixer.setBounds (0, 0, 329, 368);
        mixer.getProperties().set (JuceSkinAdapter::styleProperty, "channelMixer.studio");
        mixer.setLookAndFeel (&adapter);
        auto settings = defaultOutputChannelMix();
        auto& left = *std::find_if (settings.begin(), settings.end(), [] (const auto& setting)
            { return setting.channel == juce::AudioChannelSet::left; });
        left.gain = 0.123456f;
        int notifications = 0;
        OutputChannelMix received;
        mixer.onChange = [&] (auto channel, float gain, bool muted, bool solo)
        {
            ++notifications;
            received = { channel, gain, muted, solo };
        };

        beginTest ("refresh preserves exact gain and speaker identity without sending changes");
        mixer.setState (OutputLayout::nineOneSix, settings, {});
        expectEquals (notifications, 0);
        auto* mute = findControl<juce::ToggleButton> (mixer, "Mute L");
        auto* solo = findControl<juce::ToggleButton> (mixer, "Solo L");
        auto* gain = findControl<juce::Slider> (mixer, "L gain");
        expect (mute != nullptr && solo != nullptr && gain != nullptr);
        if (mute && solo && gain)
        {
            mute->setToggleState (true, juce::sendNotificationSync);
            expectEquals (received.channel, juce::AudioChannelSet::left);
            expectEquals (received.gain, left.gain);
            expect (received.muted && ! received.solo);
            solo->setToggleState (true, juce::sendNotificationSync);
            expect (received.muted && received.solo);
            expectEquals (received.gain, left.gain);
            gain->setValue (-60.0, juce::sendNotificationSync);
            expectEquals (received.gain, 0.0f);
            gain->setValue (6.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (received.gain, juce::Decibels::decibelsToGain (6.0f), 0.00001f);

            beginTest ("channel faders support keyboard adjustment and report the correct speaker");
            expect (gain->getWantsKeyboardFocus());
            const auto beforeKey = notifications;
            expect (gain->keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
            expectEquals (notifications, beforeKey + 1);
            expectEquals (received.channel, juce::AudioChannelSet::left);
            expectWithinAbsoluteError (received.gain, juce::Decibels::decibelsToGain (5.9f), 0.00001f);
            expect (gain->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
            expectWithinAbsoluteError (received.gain, juce::Decibels::decibelsToGain (6.0f), 0.00001f);
        }

        beginTest ("all 16 speakers have independently named controls and layout changes restore roles");
        const auto channels = toAudioChannelSet (OutputLayout::nineOneSix);
        for (const auto channel : channels.getChannelTypes())
        {
            const auto name = juce::AudioChannelSet::getAbbreviatedChannelTypeName (channel);
            expect (findControl<juce::Slider> (mixer, name + " gain") != nullptr);
            expect (findControl<juce::ToggleButton> (mixer, "Mute " + name) != nullptr);
            expect (findControl<juce::ToggleButton> (mixer, "Solo " + name) != nullptr);
        }
        beginTest ("keyboard focus callbacks reveal every channel control in both traversal directions");
        auto* scroll = dynamic_cast<juce::Viewport*> (mixer.getChildComponent (0));
        expect (scroll != nullptr);
        if (scroll)
            for (const auto backwards : { false, true })
                for (int index = 0; index < channels.size(); ++index)
                {
                    const auto channel = channels.getTypeOfChannel (backwards ? channels.size() - index - 1 : index);
                    const auto name = juce::AudioChannelSet::getAbbreviatedChannelTypeName (channel);
                    for (const auto& title : { name + " gain", "Mute " + name, "Solo " + name })
                        if (auto* control = findControl<juce::Component> (mixer, title))
                        {
                            control->focusGained (juce::Component::focusChangedByTabKey);
                            const auto bounds = scroll->getViewedComponent()->getLocalArea (control, control->getLocalBounds());
                            expect (scroll->getViewArea().contains (bounds), title + " must remain visible while editing");
                        }
                }

        const auto beforeRefresh = notifications;
        mixer.setState (OutputLayout::mono, settings, {});
        expect (findControl<juce::ToggleButton> (mixer, "Mute L") == nullptr);
        expect (findControl<juce::ToggleButton> (mixer, "Mute C") != nullptr);
        mixer.setState (OutputLayout::stereo, settings, {});
        expectEquals (notifications, beforeRefresh);
        gain = findControl<juce::Slider> (mixer, "L gain");
        expect (gain != nullptr);
        if (gain) expectWithinAbsoluteError (gain->getValue(), static_cast<double> (juce::Decibels::gainToDecibels (left.gain)), 0.051);

        beginTest ("skin-owned mixer columns preserve speaker order and native control geometry");
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne, OutputLayout::fiveOneTwo,
                                   OutputLayout::sevenOneTwo, OutputLayout::nineOneSix })
        {
            mixer.setState (layout, settings, {});
            mixer.setSize (420, 520);
            const auto speakers = toAudioChannelSet (layout);
            auto* viewport = dynamic_cast<juce::Viewport*> (mixer.getChildComponent (0));
            expect (viewport != nullptr);
            if (! viewport) continue;
            auto* content = viewport->getViewedComponent();
            expect (content != nullptr);
            if (! content) continue;
            expect (content->getHeight() <= viewport->getHeight(), "16 channels fit the default mixer height");
            for (int index = 0; index < speakers.size(); ++index)
            {
                const auto name = juce::AudioChannelSet::getAbbreviatedChannelTypeName (speakers.getTypeOfChannel (index));
                auto* fader = findControl<juce::Slider> (mixer, name + " gain");
                expect (fader != nullptr);
                if (! fader) continue;
                auto* row = fader->getParentComponent();
                expect ((row->getX() == 0) == (index % 2 == 0));
                if (index % 2 != 0)
                    expectEquals (row->getY(), content->getChildComponent (index - 1)->getY());
                if (index >= 2)
                    expect (row->getY() >= content->getChildComponent (index - 2)->getBottom());
                for (auto* control : row->getChildren())
                    expect (row->getLocalBounds().contains (control->getBounds()), control->getTitle());
            }
        }
        mixer.setState (OutputLayout::stereo, settings, {});
        gain = findControl<juce::Slider> (mixer, "L gain");

        beginTest ("same-size skin changes update the native fader range with an unchanged style name");
        if (gain)
        {
            const auto originalBounds = gain->getBounds();
            const auto originalStart = gain->getPositionOfValue (gain->getMinimum());
            auto changedSkin = std::make_shared<CompiledSkin> (*skin);
            const auto gainStyle = skin->renderers.at ("channelMixer.studio").slotStyles.at ("row.gain");
            auto& controls = changedSkin->renderers.at (gainStyle).renderer.controlStyle;
            expect (controls.has_value());
            if (controls)
            {
                controls->properties.at ("trackBounds.x").value = SkinCompiledRenderValue (17.0);
                activeSkin = changedSkin;
                mixer.resized();
                expect (gain->getBounds() == originalBounds);
                expectWithinAbsoluteError (gain->getPositionOfValue (gain->getMinimum()), 17.0f, 0.01f);
                activeSkin = skin;
                mixer.resized();
                expectWithinAbsoluteError (gain->getPositionOfValue (gain->getMinimum()), originalStart, 0.01f);
            }
        }

        beginTest ("actual mixer rows render through declared skin slots at compact and default sizes");
        mixer.getProperties().set (JuceSkinAdapter::styleProperty, "channelMixer.studio");
        mixer.setLookAndFeel (&adapter);
        mixer.setState (OutputLayout::nineOneSix, settings, {});
        for (const auto size : { juce::Point<int> (329, 368), juce::Point<int> (405, 468) })
        {
            mixer.setSize (size.x, size.y);
            const auto snapshot = mixer.createComponentSnapshot (mixer.getLocalBounds());
            expect (snapshot.isValid());
            expectEquals (faults.count, 0);
            auto* viewport = dynamic_cast<juce::Viewport*> (mixer.getChildComponent (0));
            expect (viewport != nullptr && viewport->getViewedComponent() != nullptr);
            if (viewport && viewport->getViewedComponent())
            {
                viewport->setViewPosition (0, 100000);
                expect (viewport->getViewArea().getBottom() >= viewport->getViewedComponent()->getHeight());
            }
        }
        mixer.getProperties().set (JuceSkinAdapter::styleProperty, "channelMixer.default");
        mixer.resized();
        beginTest ("existing mixer renderers retain their single-column presentation");
        if (auto* viewport = dynamic_cast<juce::Viewport*> (mixer.getChildComponent (0)))
            if (auto* content = viewport->getViewedComponent())
                for (auto* row : content->getChildren()) expectEquals (row->getX(), 0);
        expect (mixer.createComponentSnapshot (mixer.getLocalBounds()).isValid());
        expectEquals (faults.count, 0, "Foundation supplies every mixer control slot");
        mixer.setLookAndFeel (nullptr);
    }
} outputMixerTests;
}
