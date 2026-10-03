#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "ui/JuceSkinAdapter.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
juce::Label* comboLabel (juce::ComboBox& combo)
{
    for (auto* child : combo.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child)) return label;
    return nullptr;
}

bool samePixels (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
    return true;
}

class ComboFaults final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override { ++count; }
    int count = 0;
};

class SkinEditableComboTests final : public juce::UnitTest
{
public:
    SkinEditableComboTests() : UnitTest ("SkinEditableCombo", "skin") {}

    void runTest() override
    {
        beginTest ("editable combo fixtures compile and have a native display");
        SkinValidationReport report;
        const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
        const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
        const auto defaultPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
        const auto skin = defaultPackage && foundation ? SkinCompiler::compile (*defaultPackage, foundation, report) : nullptr;
        expect (foundation != nullptr && skin != nullptr, report.toText());
        expect (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr);
        if (! foundation || ! skin) return;
        const auto screenshots = juce::File::getCurrentWorkingDirectory().getChildFile ("build/skin-editable-combo-tests");
        expect (screenshots.createDirectory());

        for (const auto& variant : { juce::String ("default"), juce::String ("studio"), juce::String ("listening") })
        {
            beginTest ("real editor visibility, painting and native lifecycle / " + variant);
            const auto compiled = variant == "default" ? foundation : skin;
            auto theme = variant;
            ComboFaults faults;
            SkinRenderer renderer;
            JuceSkinAdapter adapter ([&] (const juce::Component&, SkinRenderTarget target, const juce::String&)
            {
                return JuceSkinAdapter::RuntimeBinding {
                    compiled->resolveStyle (target, skinRenderTargetName (target) + "." + theme), nullptr, &faults, true
                };
            }, renderer);
            juce::Component host;
            host.setBounds (100, 100, 420, 180);
            host.setLookAndFeel (&adapter);
            juce::ComboBox combo;
            combo.setBounds (20, 20, 340, 42);
            combo.getProperties().set (JuceSkinAdapter::scaleFactorProperty, 1.0);
            combo.addItem ("Original playlist caption", 1);
            combo.setSelectedId (1, juce::dontSendNotification);
            combo.setEditableText (true);
            host.addAndMakeVisible (combo);
            host.addToDesktop (0);
            host.setVisible (true);
            host.toFront (true);
            auto* label = comboLabel (combo);
            expect (label != nullptr);
            if (label == nullptr) { host.setLookAndFeel (nullptr); host.removeFromDesktop(); continue; }
            int shown = 0, hidden = 0, changes = 0;
            bool hiddenBeforeNotification = false;
            label->onEditorShow = [&] { ++shown; };
            label->onEditorHide = [&] { ++hidden; hiddenBeforeNotification = label->getAlpha() == 0.0f; };
            combo.onChange = [&] { ++changes; };
            const auto imageOf = [&]
            {
                juce::Image image (juce::Image::ARGB, combo.getWidth(), combo.getHeight(), true);
                juce::Graphics graphics (image);
                combo.paintEntireComponent (graphics, true);
                return image;
            };
            const auto expectedBackground = [&] (const juce::String& text)
            {
                juce::Image image (juce::Image::ARGB, combo.getWidth(), combo.getHeight(), true);
                juce::Graphics graphics (image);
                SkinRenderContext context;
                context.bounds = context.contentBounds = image.getBounds().toFloat();
                context.state.enabled = combo.isEnabled();
                context.state.hover = combo.isMouseOver (true);
                context.state.focused = combo.hasKeyboardFocus (true);
                context.primaryText = text;
                context.extraLookup = [&] (std::string_view path) -> std::optional<SkinExpressionValue>
                {
                    if (path == "combo.popupOpen") return SkinExpressionValue (false);
                    if (path == "combo.hasSelection") return SkinExpressionValue (combo.getSelectedId() > 0);
                    return std::nullopt;
                };
                const auto* entry = compiled->resolveStyle (SkinRenderTarget::comboBox, "comboBox." + theme);
                renderer.setAssetCache (entry->assets);
                renderer.draw (graphics, entry->renderer, context, true, faults);
                return image;
            };
            const auto save = [&] (const juce::Image& image, const juce::String& state)
            {
                const auto file = screenshots.getChildFile (variant + "-" + state + ".png");
                const auto output = file.createOutputStream();
                expect (output != nullptr);
                if (output != nullptr)
                {
                    output->setPosition (0);
                    output->truncate();
                    juce::PNGImageFormat png;
                    expect (png.writeImageToStream (image, *output));
                }
            };
            expectWithinAbsoluteError (label->getAlpha(), 0.0f, 0.001f);
            expect (samePixels (imageOf(), expectedBackground (combo.getText())), "Closed combo paints the declarative renderer exactly once.");
            save (imageOf(), "normal");
            combo.showEditor();
            auto* editor = label->getCurrentTextEditor();
            expect (editor != nullptr);
            if (editor != nullptr)
            {
                expectEquals (shown, 1);
                expect (editor->isVisible() && editor->isShowing());
                expect (editor->hasKeyboardFocus (false));
                expectWithinAbsoluteError (label->getAlpha(), 1.0f, 0.001f);
                editor->setCaretVisible (false);
                editor->setText ({}, false);
                editor->setHighlightedRegion ({ 0, 0 });
                expect (samePixels (imageOf(), expectedBackground ({})),
                        "Editing paints only the combo background and arrow, without the old caption or a second focus border.");
                editor->insertTextAtCaret ("Renamed playlist");
                expectEquals (combo.getText(), juce::String ("Original playlist caption"));
                expect (! samePixels (imageOf(), expectedBackground ({})), "Typed native text must contribute visible pixels.");
                const auto colourKey = theme == "default" ? "colors.text" : "colors." + theme + ".text";
                const auto* expectedColour = compiled->tokens.find<juce::Colour> (colourKey);
                expect (expectedColour != nullptr);
                if (expectedColour != nullptr) expect (editor->findColour (juce::TextEditor::textColourId) == *expectedColour);
                save (imageOf(), "editing");
                combo.setSize (380, 46);
                expectWithinAbsoluteError (label->getAlpha(), 1.0f, 0.001f);
                static_cast<void> (imageOf());
                expectEquals (editor->getWidth(), combo.getWidth());
                expect (editor->getBorder().getRight() > 0, "The editor reserves the skin caption's arrow margin.");
                if (theme != "default")
                {
                    theme = theme == "studio" ? "listening" : "studio";
                    static_cast<void> (imageOf());
                    const auto* themedColour = compiled->tokens.find<juce::Colour> ("colors." + theme + ".text");
                    expect (themedColour != nullptr);
                    if (themedColour != nullptr) expect (editor->findColour (juce::TextEditor::textColourId) == *themedColour);
                    const auto controls = adapter.controlStyleFor (*editor, SkinRenderTarget::textEditor);
                    expect (controls.has_value());
                    if (controls && controls->caretColour)
                        expect (editor->findColour (juce::CaretComponent::caretColourId) == *controls->caretColour);
                    editor->setText ({}, false);
                    expect (samePixels (imageOf(), expectedBackground ({})), "The current theme still owns one focus treatment.");
                    editor->setText ("Renamed playlist", false);
                }
                label->hideEditor (false);
                const auto notificationDeadline = juce::Time::getMillisecondCounterHiRes() + 250.0;
                while (changes == 0 && juce::Time::getMillisecondCounterHiRes() < notificationDeadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                expectEquals (hidden, 1);
                expect (hiddenBeforeNotification);
                expectEquals (changes, 1);
                expectEquals (combo.getText(), juce::String ("Renamed playlist"));
                expectWithinAbsoluteError (label->getAlpha(), 0.0f, 0.001f);
                expect (samePixels (imageOf(), expectedBackground (combo.getText())));
                combo.showEditor();
                if (auto* cancelled = label->getCurrentTextEditor()) cancelled->setText ("Discarded rename", false);
                label->hideEditor (true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                expectEquals (shown, 2);
                expectEquals (hidden, 2);
                expectEquals (changes, 1);
                expectEquals (combo.getText(), juce::String ("Renamed playlist"));
                expectWithinAbsoluteError (label->getAlpha(), 0.0f, 0.001f);
            }
            combo.setEnabled (false);
            expect (samePixels (imageOf(), expectedBackground (combo.getText())));
            expectEquals (faults.count, 0);
            label->onEditorShow = {};
            label->onEditorHide = {};
            combo.onChange = {};
            host.removeFromDesktop();
            host.setLookAndFeel (nullptr);
        }
    }
};

SkinEditableComboTests skinEditableComboTests;
}
