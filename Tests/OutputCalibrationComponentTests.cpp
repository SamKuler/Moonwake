#include "ui/OutputCalibrationComponent.h"
#include "ui/JuceSkinAdapter.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "fixtures/TestComponentPeer.h"
#include "fixtures/LibraryAudioBackend.h"

namespace
{
template <typename Control>
Control* calibrationControl (juce::Component& component, const juce::String& title)
{
    if (component.getTitle() == title)
        if (auto* found = dynamic_cast<Control*> (&component)) return found;
    for (auto* child : component.getChildren())
        if (auto* found = calibrationControl<Control> (*child, title)) return found;
    return nullptr;
}
class OutputCalibrationComponentTests final : public juce::UnitTest
{
public:
    OutputCalibrationComponentTests() : UnitTest ("OutputCalibrationComponent", "skin") {}
    void runTest() override
    {
        beginTest ("calibration fixtures compile with complete public slot geometry");
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
        auto theme = juce::String ("studio");
        JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
        {
            return JuceSkinAdapter::RuntimeBinding { activeSkin->resolveStyle (target,
                style.isEmpty() ? skinRenderTargetName (target) + "." + theme : style), nullptr, &faults, true };
        }, renderer);
        OutputCalibrationComponent calibration;
        calibration.setLookAndFeel (&adapter);
        calibration.getProperties().set (JuceSkinAdapter::styleProperty, "outputCalibration.studio");
        calibration.setSize (329, 300);
        auto settings = defaultOutputCalibration();
        int changes = 0, tests = 0;
        OutputChannelCalibration received;
        CalibrationTestSignal signal;
        auto changeResult = juce::Result::ok();
        calibration.onChange = [&] (const auto& next) { ++changes; received = next; return changeResult; };
        calibration.onTestChange = [&] (auto next, bool) { ++tests; signal = next; return juce::Result::ok(); };
        calibration.setState (OutputLayout::nineOneSix, settings, {});
        auto* channel = calibrationControl<juce::ComboBox> (calibration, "Calibration speaker");
        auto* trim = calibrationControl<juce::TextEditor> (calibration, "Speaker trim in decibels");
        auto* delay = calibrationControl<juce::TextEditor> (calibration, "Speaker delay in milliseconds");
        auto* tone = calibrationControl<juce::TextEditor> (calibration, "Sine frequency in hertz");
        auto* start = calibrationControl<juce::TextButton> (calibration, "Start or stop calibration test signal");
        auto* polarity = calibrationControl<juce::TextButton> (calibration, "Speaker polarity");
        expect (channel && trim && delay && tone && start && polarity);
        if (! channel || ! trim || ! delay || ! tone || ! start || ! polarity)
        { calibration.setLookAndFeel (nullptr); return; }

        beginTest ("refresh exposes all sixteen speakers without emitting calibration or test actions");
        expectEquals (channel->getNumItems(), 16);
        expectEquals (changes, 0);
        expectEquals (tests, 0);
        for (int index = 0; index < 16; ++index)
        {
            channel->setSelectedItemIndex (index, juce::sendNotificationSync);
            expect (channel->getText().isNotEmpty());
            expectEquals (tests, 0);
        }
        channel->setSelectedItemIndex (0, juce::sendNotificationSync);

        beginTest ("invalid numeric drafts remain visible and never turn into zero");
        trim->setText ("not a number", false);
        trim->onReturnKey();
        expectEquals (changes, 0);
        calibration.setState (OutputLayout::nineOneSix, settings, {});
        expectEquals (trim->getText(), juce::String ("not a number"));
        start->onClick();
        expectEquals (tests, 0);
        trim->setText ("-3.25", false);
        delay->setText ("12.75", false);
        trim->onReturnKey();
        expectEquals (changes, 1);
        expectWithinAbsoluteError (received.trimDb, -3.25f, 0.001f);
        expectWithinAbsoluteError (received.delayMs, 12.75, 0.001);

        beginTest ("failed changes preserve input for correction and retry");
        changeResult = juce::Result::fail ("Audio device unavailable. Retry after reconnecting.");
        trim->setText ("-4.5", false);
        trim->onReturnKey();
        expectEquals (trim->getText(), juce::String ("-4.5"));
        changeResult = juce::Result::ok();
        trim->onReturnKey();
        expectWithinAbsoluteError (received.trimDb, -4.5f, 0.001f);

        beginTest ("LFE identifies a low frequency tone and Start/Stop is explicit");
        const auto layout = toAudioChannelSet (OutputLayout::nineOneSix);
        const auto lfe = layout.getChannelIndexForType (juce::AudioChannelSet::LFE);
        channel->setSelectedItemIndex (lfe, juce::sendNotificationSync);
        expectEquals (tone->getText(), juce::String ("60"));
        start->onClick();
        expectEquals (tests, 1);
        expect (signal.kind == CalibrationTestSignal::Kind::sine);
        expect (signal.channel == juce::AudioChannelSet::LFE);
        expectWithinAbsoluteError (signal.frequencyHz, 60.0, 0.001);
        start->onClick();
        expectEquals (tests, 2);
        expect (signal.kind == CalibrationTestSignal::Kind::off);

        beginTest ("Escape from every numeric editor stops a running test and discards its draft");
        {
            TestUiRoot root;
            OutputCalibrationComponent form;
            form.setLookAndFeel (&adapter);
            form.getProperties().set (JuceSkinAdapter::styleProperty, "outputCalibration.studio");
            root.setBounds (0, 0, 400, 500);
            root.addAndMakeVisible (form);
            form.setBounds (root.getLocalBounds());
            root.addToDesktop (0);
            root.setVisible (true);
            CalibrationTestSignal current;
            form.onTestChange = [&] (auto next, bool) { current = next; return juce::Result::ok(); };
            for (const auto* title : { "Speaker trim in decibels", "Speaker delay in milliseconds",
                                      "Test signal level in dBFS", "Sine frequency in hertz" })
            {
                current = { CalibrationTestSignal::Kind::sine, juce::AudioChannelSet::left, -24.0f, 880.0 };
                form.setState (OutputLayout::stereo, defaultOutputCalibration(), current);
                auto* editor = calibrationControl<juce::TextEditor> (form, title);
                expect (editor != nullptr);
                if (editor == nullptr) continue;
                editor->grabKeyboardFocus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                expect (editor->hasKeyboardFocus (false));
                editor->setText ("invalid", false);
                expect (root.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey)));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                expect (current.kind == CalibrationTestSignal::Kind::off, title);
                expect (editor->getText() != "invalid", title);
            }

            beginTest ("discarding calibration edits clears validation errors even when the model is unchanged");
            auto* draftTrim = calibrationControl<juce::TextEditor> (form, "Speaker trim in decibels");
            auto* draftDelay = calibrationControl<juce::TextEditor> (form, "Speaker delay in milliseconds");
            auto* draftLevel = calibrationControl<juce::TextEditor> (form, "Test signal level in dBFS");
            auto* draftFrequency = calibrationControl<juce::TextEditor> (form, "Sine frequency in hertz");
            auto* startButton = calibrationControl<juce::TextButton> (form, "Start or stop calibration test signal");
            expect (draftTrim && draftDelay && draftLevel && draftFrequency && startButton);
            if (draftTrim && draftDelay && draftLevel && draftFrequency && startButton)
            {
                form.setState (OutputLayout::stereo, defaultOutputCalibration(), {});
                draftTrim->setText ("invalid", false);
                draftTrim->onReturnKey();
                form.setState (OutputLayout::stereo, defaultOutputCalibration(), {});
                form.discardEdits();
                expectEquals (draftTrim->getText(), juce::String ("0.0"));
                expectEquals (draftDelay->getText(), juce::String ("0.00"));
                expect (calibrationControl<juce::Label> (form, "Ready: Left") != nullptr);
                startButton->onClick();
                expect (current.kind == CalibrationTestSignal::Kind::sine);
                startButton->onClick();

                beginTest ("Escape while stopped restores the last accepted test parameters without starting a test");
                draftLevel->setText ("-42", false);
                draftFrequency->setText ("220", false);
                draftLevel->grabKeyboardFocus();
                expect (root.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::returnKey)));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                form.setState (OutputLayout::stereo, defaultOutputCalibration(), {});
                draftLevel->setText ("invalid", false);
                expect (root.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey)));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                expectEquals (draftLevel->getText(), juce::String ("-42.0"));
                expectEquals (draftFrequency->getText(), juce::String ("220"));
                expect (current.kind == CalibrationTestSignal::Kind::off);
            }
            form.setLookAndFeel (nullptr);
        }

        beginTest ("test parameter commits display the configuration accepted by the engine");
        {
            AudioEngine engine (std::make_unique<LibraryAudioBackend>());
            expect (engine.initialise().isEmpty());
            expect (engine.setOutputLayout (OutputLayout::fiveOne).isEmpty());
            TestUiRoot root;
            root.setBounds (0, 0, 400, 500);
            root.addToDesktop (0);
            root.setVisible (true);
            OutputCalibrationComponent form;
            form.setLookAndFeel (&adapter);
            form.getProperties().set (JuceSkinAdapter::styleProperty, "outputCalibration.studio");
            root.addAndMakeVisible (form);
            form.setBounds (root.getLocalBounds());
            int calibrationCommits = 0;
            form.onChange = [&] (const auto& value)
            {
                ++calibrationCommits;
                const auto error = engine.setOutputCalibration (value.channel, value.trimDb, value.delayMs, value.inverted);
                return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
            };
            form.onTestChange = [&] (auto&& next, bool)
            {
                const auto error = engine.setCalibrationTestSignal (next);
                if (error.isNotEmpty()) return juce::Result::fail (error);
                next = engine.getCalibrationTestSignal();
                return juce::Result::ok();
            };
            const auto poll = [&]
            {
                form.setState (OutputLayout::fiveOne, engine.getOutputCalibration(), engine.getCalibrationTestSignal());
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            };
            poll();
            auto* speaker = calibrationControl<juce::ComboBox> (form, "Calibration speaker");
            auto* kind = calibrationControl<juce::ComboBox> (form, "Calibration test signal");
            auto* levelEditor = calibrationControl<juce::TextEditor> (form, "Test signal level in dBFS");
            auto* frequencyEditor = calibrationControl<juce::TextEditor> (form, "Sine frequency in hertz");
            auto* trimEditor = calibrationControl<juce::TextEditor> (form, "Speaker trim in decibels");
            auto* startButton = calibrationControl<juce::TextButton> (form, "Start or stop calibration test signal");
            expect (speaker && kind && levelEditor && frequencyEditor && trimEditor && startButton);
            if (speaker && kind && levelEditor && frequencyEditor && trimEditor && startButton)
            {
                const auto key = [&] (int code)
                {
                    expect (root.getPeer()->handleKeyPress (juce::KeyPress (code)));
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                };
                kind->grabKeyboardFocus();
                key (juce::KeyPress::downKey);
                startButton->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::sine);
                frequencyEditor->grabKeyboardFocus();
                frequencyEditor->setText ("invalid", false);
                kind->grabKeyboardFocus();
                key (juce::KeyPress::upKey);
                poll();
                expectEquals (kind->getSelectedId(), 2, "A rejected mode change must still display the running sine test.");
                expectEquals (frequencyEditor->getText(), juce::String ("invalid"));
                frequencyEditor->grabKeyboardFocus();
                frequencyEditor->setText ("440", false);
                key (juce::KeyPress::returnKey);
                expectWithinAbsoluteError (engine.getCalibrationTestSignal().frequencyHz, 440.0, 0.001);
                startButton->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

                speaker->setSelectedItemIndex (toAudioChannelSet (OutputLayout::fiveOne).getChannelIndexForType (juce::AudioChannelSet::LFE), juce::sendNotificationSync);
                startButton->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                frequencyEditor->grabKeyboardFocus();
                frequencyEditor->setText ("1000", false);
                key (juce::KeyPress::returnKey);
                poll();
                expect (frequencyEditor->hasKeyboardFocus (false));
                expectWithinAbsoluteError (engine.getCalibrationTestSignal().frequencyHz, 60.0, 0.001);
                expectEquals (frequencyEditor->getText(), juce::String ("60"));

                beginTest ("formatted test parameters retain their accepted precision when focus moves");
                levelEditor->grabKeyboardFocus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                levelEditor->setText ("-24.125", false);
                key (juce::KeyPress::returnKey);
                const auto exactLevel = engine.getCalibrationTestSignal().levelDbFS;
                key (juce::KeyPress::tabKey);
                expectEquals (engine.getCalibrationTestSignal().levelDbFS, exactLevel);

                beginTest ("polling updates an unchanged focused value and preserves a later unsubmitted draft");
                auto changedSignal = engine.getCalibrationTestSignal();
                changedSignal.frequencyHz = 80.0;
                expect (engine.setCalibrationTestSignal (changedSignal).isEmpty());
                poll();
                expectEquals (frequencyEditor->getText(), juce::String ("80"));
                frequencyEditor->setText ("35", false);
                changedSignal.frequencyHz = 90.0;
                expect (engine.setCalibrationTestSignal (changedSignal).isEmpty());
                poll();
                expectEquals (frequencyEditor->getText(), juce::String ("35"));
                key (juce::KeyPress::escapeKey);
                expectEquals (frequencyEditor->getText(), juce::String ("90"));

                beginTest ("stopped test parameter recovery clears validation errors");
                levelEditor->grabKeyboardFocus();
                levelEditor->setText ("invalid", false);
                key (juce::KeyPress::returnKey);
                levelEditor->setText ("-42", false);
                key (juce::KeyPress::returnKey);
                expect (calibrationControl<juce::Label> (form, "Ready: LFE") != nullptr);

                beginTest ("temporary focus loss during relayout does not commit calibration drafts");
                trimEditor->grabKeyboardFocus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                trimEditor->setText ("-7.5", false);
                trimEditor->setHighlightedRegion ({ 1, 3 });
                const auto beforeRelayout = calibrationCommits;
                root.removeChildComponent (&form);
                root.addAndMakeVisible (form);
                form.setBounds (0, 0, 350, 450);
                trimEditor->grabKeyboardFocus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                expectEquals (calibrationCommits, beforeRelayout);
                expectEquals (trimEditor->getText(), juce::String ("-7.5"));
                expect (trimEditor->getHighlightedRegion() == juce::Range<int> (1, 3));
                key (juce::KeyPress::tabKey);
                expectEquals (calibrationCommits, beforeRelayout + 1, "A real focus move still commits the valid draft.");

                beginTest ("a stopped host response preserves test parameters until an explicit restart");
                bool explicitStart = false;
                form.onTestChange = [&] (auto& next, bool starting)
                {
                    explicitStart = starting;
                    if (! explicitStart && next.kind != CalibrationTestSignal::Kind::off)
                    {
                        next.kind = CalibrationTestSignal::Kind::off;
                        return juce::Result::ok();
                    }
                    const auto error = engine.setCalibrationTestSignal (next);
                    if (error.isNotEmpty()) return juce::Result::fail (error);
                    next = engine.getCalibrationTestSignal();
                    return juce::Result::ok();
                };
                speaker->setSelectedItemIndex (0, juce::sendNotificationSync);
                for (const auto testKind : { CalibrationTestSignal::Kind::pinkNoise, CalibrationTestSignal::Kind::sine })
                    for (const auto returnKey : { false, true })
                    {
                        expect (engine.setCalibrationTestSignal ({ testKind, juce::AudioChannelSet::left, -30.0f, 1000.0 }).isEmpty());
                        poll();
                        auto* editor = testKind == CalibrationTestSignal::Kind::sine ? frequencyEditor : levelEditor;
                        editor->grabKeyboardFocus();
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                        levelEditor->setText ("-36", false);
                        frequencyEditor->setText ("440", false);
                        engine.stopCalibrationTestSignal();
                        if (returnKey) key (juce::KeyPress::returnKey);
                        else
                        {
                            trimEditor->grabKeyboardFocus();
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                        }
                        expect (! explicitStart);
                        expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
                        expectEquals (startButton->getButtonText(), juce::String ("Start test"));
                        expectEquals (kind->getSelectedId(), testKind == CalibrationTestSignal::Kind::pinkNoise ? 1 : 2);
                        expectEquals (levelEditor->getText(), juce::String ("-36.0"));
                        expectEquals (frequencyEditor->getText(), juce::String ("440"));
                        form.discardEdits();
                        expectEquals (kind->getSelectedId(), testKind == CalibrationTestSignal::Kind::pinkNoise ? 1 : 2);
                        startButton->triggerClick();
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                        expect (explicitStart);
                        expect (engine.getCalibrationTestSignal().kind == testKind);
                        expectWithinAbsoluteError (engine.getCalibrationTestSignal().levelDbFS, -36.0f, 0.001f);
                        expectWithinAbsoluteError (engine.getCalibrationTestSignal().frequencyHz, 440.0, 0.001);
                        startButton->triggerClick();
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                        expect (! explicitStart);
                        expect (engine.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
                    }
            }
            form.setLookAndFeel (nullptr);
        }

        beginTest ("each theme places controls and paints normal, selected and disabled states through Skin");
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile ("build/calibration-ui-tests");
        expect (directory.createDirectory());
        for (const auto& nextTheme : { juce::String ("studio"), juce::String ("listening"), juce::String ("default") })
        {
            theme = nextTheme;
            activeSkin = theme == "default" ? foundation : skin;
            calibration.getProperties().set (JuceSkinAdapter::styleProperty, "outputCalibration." + theme);
            for (const auto width : { 180, 329 })
            {
                calibration.setSize (width, 240);
                calibration.resized();
                expect (channel->getWidth() >= 60 && trim->getWidth() >= 60);
                for (const auto disabled : { false, true })
                {
                    calibration.setEnabled (! disabled);
                    juce::Image image (juce::Image::ARGB, width, 240, true);
                    juce::Graphics graphics (image);
                    calibration.paintEntireComponent (graphics, true);
                    const auto output = directory.getChildFile (theme + "-" + juce::String (width) + (disabled ? "-disabled.png" : "-normal.png")).createOutputStream();
                    if (output) { juce::PNGImageFormat png; expect (png.writeImageToStream (image, *output)); }
                }
            }
        }
        expectEquals (faults.count, 0);
        calibration.setLookAndFeel (nullptr);
    }
};
OutputCalibrationComponentTests outputCalibrationComponentTests;
}
