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
        OutputMixerComponent mixer;
        mixer.setBounds (0, 0, 329, 368);
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

        beginTest ("actual mixer rows render through declared skin slots at compact and default sizes");
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
        JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
        {
            return JuceSkinAdapter::RuntimeBinding { skin->resolveStyle (target,
                style.isEmpty() ? "channelMap.studioMixer" : style), nullptr, &faults, true };
        }, renderer);
        mixer.getProperties().set (JuceSkinAdapter::styleProperty, "channelMap.studioMixer");
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
        mixer.getProperties().set (JuceSkinAdapter::styleProperty, "channelMap.default");
        expect (mixer.createComponentSnapshot (mixer.getLocalBounds()).isValid());
        expectEquals (faults.count, 0, "Foundation supplies every mixer control slot");
        mixer.setLookAndFeel (nullptr);
    }
} outputMixerTests;
}
