#include "JuceSkinAdapter.h"
#include "SkinTableListBox.h"

#include "skin/SkinAssetCache.h"
#include "skin/SkinFontResolver.h"
#include "skin/SkinComponentBindings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

const juce::Identifier JuceSkinAdapter::styleProperty { "skinStyle" };
const juce::Identifier JuceSkinAdapter::scaleFactorProperty { "skinScaleFactor" };
const juce::Identifier JuceSkinAdapter::presentationProperty { "skinPresentation" };

class SkinListContentPainter final : public juce::CachedComponentImage
{
public:
    SkinListContentPainter (juce::Component& childToUse, juce::ListBox& ownerToUse,
                            SkinRenderTarget targetToUse)
        : child (childToUse), owner (&ownerToUse), target (targetToUse) {}

    void paint (juce::Graphics& graphics) override
    {
        juce::Graphics::ScopedSaveState state (graphics);
        if (owner != nullptr)
            if (const auto* adapter = dynamic_cast<const JuceSkinAdapter*> (&owner->getLookAndFeel()))
                adapter->clipListContent (graphics, *owner, child, target);
        if (! graphics.isClipEmpty()) child.paintEntireComponent (graphics, false);
    }
    bool invalidateAll() override { return true; }
    bool invalidate (const juce::Rectangle<int>&) override { return true; }
    void releaseResources() override {}

private:
    juce::Component& child;
    juce::Component::SafePointer<juce::ListBox> owner;
    SkinRenderTarget target;
};

namespace
{
class NullFaultSink final : public SkinRuntimeFaultSink
{
public:
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override {}
};

NullFaultSink& nullFaultSink()
{
    static NullFaultSink sink;
    return sink;
}

float renderingScaleFactorFor (const juce::Component& component)
{
    const auto overrideValue = static_cast<double> (
        component.getProperties().getWithDefault (
            JuceSkinAdapter::scaleFactorProperty, 0.0));
    if (std::isfinite (overrideValue) && overrideValue > 0.0)
        return static_cast<float> (overrideValue);

    const auto componentScale =
        juce::Component::getApproximateScaleFactorForComponent (&component);
    auto platformScale = component.getPeer() != nullptr
        ? component.getPeer()->getPlatformScaleFactor() : 1.0;
    if (component.getPeer() == nullptr)
    {
        if (const auto* display = juce::Desktop::getInstance().getDisplays()
                                      .getDisplayForPoint (juce::Desktop::getMousePosition()))
            platformScale = display->scale;
    }
    const auto result = static_cast<double> (componentScale) * platformScale;
    return static_cast<float> (
        std::isfinite (result) && result > 0.0 ? result : 1.0);
}

juce::Component& tableStyleOwner (juce::TableHeaderComponent& header)
{
    for (auto* component = header.getParentComponent(); component != nullptr;
         component = component->getParentComponent())
        if (dynamic_cast<juce::TableListBox*> (component) != nullptr)
            return *component;
    return header;
}

juce::String alignmentNameFor (juce::Justification justification)
{
    switch (justification.getFlags())
    {
        case juce::Justification::topLeft: return "topLeft";
        case juce::Justification::centredTop: return "top";
        case juce::Justification::topRight: return "topRight";
        case juce::Justification::centredLeft: return "left";
        case juce::Justification::centred: return "center";
        case juce::Justification::centredRight: return "right";
        case juce::Justification::bottomLeft: return "bottomLeft";
        case juce::Justification::centredBottom: return "bottom";
        case juce::Justification::bottomRight: return "bottomRight";
        default: return "left";
    }
}

juce::Justification justificationForAlignment (const juce::String& name)
{
    for (const auto flags : { juce::Justification::topLeft, juce::Justification::centredTop,
                             juce::Justification::topRight, juce::Justification::centredLeft,
                             juce::Justification::centred, juce::Justification::centredRight,
                             juce::Justification::bottomLeft, juce::Justification::centredBottom,
                             juce::Justification::bottomRight })
        if (alignmentNameFor (flags) == name) return flags;
    return juce::Justification::centredLeft;
}

SkinRenderLayer layerWithVariants (const SkinRenderProgram& program,
                                   const SkinRenderLayer& source, const SkinControlState& state)
{
    auto layer = source;
    const std::array activeVariants {
        state.selected, state.hover, state.pressed,
        state.dragging, state.focused, ! state.enabled
    };
    const auto prefix = layer.id + ".";
    for (size_t index = 0; index < activeVariants.size(); ++index)
        if (activeVariants[index])
            for (const auto& [path, property] : program.variants[index].properties)
                if (path.startsWith (prefix))
                    layer.properties.insert_or_assign (path.substring (prefix.length()), property);
    return layer;
}

const SkinRenderLayer* findLayer (
    const std::vector<SkinRenderLayer>& layers, juce::StringRef id)
{
    for (const auto& layer : layers)
    {
        if (layer.id == id)
            return &layer;
        if (const auto* nested = findLayer (layer.children, id))
            return nested;
    }
    return nullptr;
}

template <typename Value>
std::optional<Value> literalProperty (
    const SkinRenderLayer& layer, juce::StringRef propertyName)
{
    const auto found = layer.properties.find (juce::String (propertyName));
    if (found == layer.properties.end())
        return std::nullopt;
    const auto* compiled = std::get_if<SkinCompiledRenderValue> (
        &found->second.value);
    if (compiled == nullptr)
        return std::nullopt;
    if (const auto* value = std::get_if<Value> (compiled))
        return *value;
    return std::nullopt;
}

SkinExpressionContext expressionContextFor (const SkinRenderContext& context)
{
    return { [&context] (std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        const auto number = [] (double value) -> std::optional<SkinExpressionValue>
        { return SkinExpressionValue (value); };
        const auto boolean = [] (bool value) -> std::optional<SkinExpressionValue>
        { return SkinExpressionValue (value); };
        const auto string = [] (juce::String value) -> std::optional<SkinExpressionValue>
        { return SkinExpressionValue (std::move (value)); };

        if (path == "bounds.x") return number (context.bounds.getX());
        if (path == "bounds.y") return number (context.bounds.getY());
        if (path == "bounds.width") return number (context.bounds.getWidth());
        if (path == "bounds.height") return number (context.bounds.getHeight());
        if (path == "contentBounds.x") return number (context.contentBounds.getX());
        if (path == "contentBounds.y") return number (context.contentBounds.getY());
        if (path == "contentBounds.width") return number (context.contentBounds.getWidth());
        if (path == "contentBounds.height") return number (context.contentBounds.getHeight());
        if (path == "scaleFactor") return number (context.scaleFactor);
        if (path == "state.enabled") return boolean (context.state.enabled);
        if (path == "state.hover") return boolean (context.state.hover);
        if (path == "state.pressed") return boolean (context.state.pressed);
        if (path == "state.focused") return boolean (context.state.focused);
        if (path == "state.selected") return boolean (context.state.selected);
        if (path == "state.dragging") return boolean (context.state.dragging);
        if (path == "value.normalized") return number (context.normalisedValue);
        if (path == "value.minimum") return number (context.minimum);
        if (path == "value.maximum") return number (context.maximum);
        if (path == "value.display") return string (context.displayValue);
        if (path == "text.primary") return string (context.primaryText);
        if (path == "text.secondary") return string (context.secondaryText);
        if (path == "text.align") return string (context.textAlignment);
        if (path == "icon.name") return string (context.iconName);
        if (path == "icon.path") return string (context.iconPathData);
        if (path == "table.rowCount") return number (context.tableRowCount);
        if (path == "table.columnCount") return number (context.tableColumnCount);
        return context.extraLookup ? context.extraLookup (path) : std::nullopt;
    } };
}

template <typename Value>
std::optional<Value> resolvedProperty (
    const SkinRenderLayer& layer, juce::StringRef propertyName,
    const SkinExpressionContext& expressionContext,
    juce::Rectangle<float> frame)
{
    const auto found = layer.properties.find (juce::String (propertyName));
    if (found == layer.properties.end())
        return std::nullopt;
    const auto value = found->second.resolve (expressionContext, frame);
    if (const auto* typed = std::get_if<Value> (&value))
        return *typed;
    return std::nullopt;
}

juce::Rectangle<float> resolvedFrameFor (
    const SkinRenderLayer& layer, const SkinRenderContext& context,
    const SkinExpressionContext& expressionContext)
{
    const auto number = [&] (juce::StringRef property, double fallback)
    {
        return static_cast<float> (resolvedProperty<double> (
            layer, property, expressionContext, context.bounds).value_or (fallback));
    };

    if (layer.properties.contains ("frame.anchor"))
    {
        const auto anchor = resolvedProperty<juce::String> (
            layer, "frame.anchor", expressionContext, context.bounds)
                                .value_or ("bounds");
        auto frame = anchor == "contentBounds" ? context.contentBounds : context.bounds;
        const auto top = number ("frame.inset.top", 0.0);
        const auto right = number ("frame.inset.right", 0.0);
        const auto bottom = number ("frame.inset.bottom", 0.0);
        const auto left = number ("frame.inset.left", 0.0);
        frame.setBounds (frame.getX() + left, frame.getY() + top,
                         juce::jmax (0.0f, frame.getWidth() - left - right),
                         juce::jmax (0.0f, frame.getHeight() - top - bottom));
        return frame;
    }

    if (layer.properties.contains ("frame.x"))
        return { number ("frame.x", 0.0), number ("frame.y", 0.0),
                 juce::jmax (0.0f, number ("frame.width", 0.0)),
                 juce::jmax (0.0f, number ("frame.height", 0.0)) };
    return context.bounds;
}
} // namespace

JuceSkinAdapter::JuceSkinAdapter (StyleProvider provider, SkinRenderer& rendererToUse)
    : styleProvider (std::move (provider)), renderer (rendererToUse)
{
    ancillaryProxy.setName ("Skin ancillary renderer proxy");
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
}

JuceSkinAdapter::RuntimeBinding JuceSkinAdapter::bindingFor (
    const juce::Component& component, SkinRenderTarget target,
    const juce::String& forcedStyle) const
{
    if (! styleProvider) return {};
    auto style = forcedStyle;
    if (style.isEmpty())
        style = component.getProperties().getWithDefault (styleProperty, "default").toString();
    if (style.isEmpty()) style = "default";
    return styleProvider (component, target, style);
}

bool JuceSkinAdapter::drawBinding (
    juce::Graphics& graphics, const juce::Component& component,
    SkinRenderTarget target, SkinRenderContext context,
    const juce::String& forcedStyle, const juce::String& instanceKey)
{
    RuntimeBinding binding;
    try
    {
        binding = bindingFor (component, target, forcedStyle);
    }
    catch (...)
    {
        return false;
    }
    if (binding.styleEntry == nullptr) return false;
    if (context.bounds.isEmpty()) context.bounds = component.getLocalBounds().toFloat();
    if (context.contentBounds.isEmpty()) context.contentBounds = context.bounds;
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (component));
    auto& sink = binding.faultSink != nullptr ? *binding.faultSink : nullFaultSink();
    const auto style = forcedStyle.isNotEmpty()
                     ? forcedStyle
                     : component.getProperties().getWithDefault (
                           styleProperty, "default").toString();
    const auto animationKey = animationKeyFor (
        component, target, style, instanceKey);
    renderer.setAssetCache (binding.styleEntry->assets);
    renderer.draw (graphics, binding.styleEntry->renderer, context,
                   binding.requiredComponent, sink, binding.animations, animationKey);
    if (binding.animations != nullptr
        && binding.animations->isTimerNeededFor (animationKey))
        watchAnimations (component, target, style, instanceKey);
    return true;
}

bool JuceSkinAdapter::drawSlotBinding (
    juce::Graphics& graphics, const juce::Component& component,
    SkinRenderTarget ownerTarget, juce::StringRef slotNameRef,
    SkinRenderTarget slotTarget, SkinRenderContext context,
    const juce::String& instanceKey)
{
    RuntimeBinding owner;
    try
    {
        owner = bindingFor (component, ownerTarget);
    }
    catch (...)
    {
        return false;
    }
    if (owner.styleEntry == nullptr) return false;
    const juce::String slotName (slotNameRef);
    const auto mapped = owner.styleEntry->slotStyles.find (slotName);
    const auto requestedStyle = mapped != owner.styleEntry->slotStyles.end()
                              ? mapped->second : juce::String ("default");
    return drawBinding (graphics, component, slotTarget, std::move (context),
                        requestedStyle, instanceKey);
}

SkinControlState JuceSkinAdapter::stateFor (
    const juce::Component& component, bool over, bool down, bool selected)
{
    SkinControlState state;
    state.enabled = component.isEnabled();
    state.hover = over;
    state.pressed = down;
    state.focused = component.hasKeyboardFocus (true);
    state.selected = selected;
    state.dragging = down && component.isMouseButtonDown();
    return state;
}

juce::String JuceSkinAdapter::animationKeyFor (
    const juce::Component& component, SkinRenderTarget target,
    const juce::String& style, const juce::String& instanceKey)
{
    auto result = juce::String::toHexString (static_cast<juce::int64> (
               reinterpret_cast<std::uintptr_t> (&component)))
         + ":" + skinRenderTargetName (target) + ":" + style;
    if (instanceKey.isNotEmpty()) result += ":" + instanceKey;
    return result;
}

void JuceSkinAdapter::watchAnimations (
    const juce::Component& component, SkinRenderTarget target,
    const juce::String& style, const juce::String& instanceKey)
{
    const auto alreadyWatching = std::any_of (
        animationWatches.begin(), animationWatches.end(), [&] (const auto& watch)
        {
            return watch.component.getComponent() == &component
                && watch.target == target && watch.style == style
                && watch.instanceKey == instanceKey;
        });
    if (! alreadyWatching)
        animationWatches.push_back ({
            const_cast<juce::Component*> (&component), target, style, instanceKey
        });
    if (! isTimerRunning()) startTimerHz (60);
}

void JuceSkinAdapter::timerCallback()
{
    bool active = false;
    bool repaintDesktop = false;
    for (auto it = animationWatches.begin(); it != animationWatches.end();)
    {
        auto* component = it->component.getComponent();
        if (component == nullptr)
        {
            it = animationWatches.erase (it);
            continue;
        }

        RuntimeBinding binding;
        try
        {
            binding = bindingFor (*component, it->target, it->style);
        }
        catch (...)
        {
            it = animationWatches.erase (it);
            continue;
        }
        const auto animationKey = animationKeyFor (
            *component, it->target, it->style, it->instanceKey);
        if (binding.animations == nullptr
            || ! binding.animations->isTimerNeededFor (animationKey))
        {
            it = animationWatches.erase (it);
            continue;
        }

        active = true;
        if (component == &ancillaryProxy)
            repaintDesktop = true;
        else
            component->repaint();
        ++it;
    }

    if (repaintDesktop)
        for (int index = 0; index < juce::Desktop::getInstance().getNumComponents(); ++index)
            if (auto* component = juce::Desktop::getInstance().getComponent (index))
                component->repaint();
    if (! active) stopTimer();
}

void JuceSkinAdapter::drawButtonBackground (
    juce::Graphics& graphics, juce::Button& button, const juce::Colour&,
    bool highlighted, bool down)
{
    SkinRenderContext context;
    context.state = stateFor (button, highlighted, down, button.getToggleState());
    context.primaryText = button.getButtonText();
    context.iconName = button.getProperties()
                           .getWithDefault (SkinComponentBindings::iconNameProperty, {})
                           .toString();
    context.iconPathData = PlayerComponentCatalog::fallbackIconPathData (
        context.iconName);
    context.iconPath = PlayerComponentCatalog::fallbackIconPath (context.iconName);
    const auto state = context.state;
    const auto iconName = context.iconName;
    const auto iconPath = context.iconPathData;
    if (! drawBinding (graphics, button, SkinRenderTarget::textButton,
                       std::move (context))
        || iconName.isEmpty() || iconPath.isEmpty())
        return;

    RuntimeBinding owner;
    try
    {
        owner = bindingFor (button, SkinRenderTarget::textButton);
    }
    catch (...)
    {
        return;
    }
    const auto slot = "icon." + iconName;
    if (owner.styleEntry == nullptr
        || ! owner.styleEntry->slotStyles.contains (slot))
        return;

    SkinRenderContext iconContext;
    iconContext.state = state;
    iconContext.iconName = iconName;
    iconContext.iconPathData = iconPath;
    iconContext.iconPath = PlayerComponentCatalog::fallbackIconPath (iconName);
    const auto controlStyle = controlStyleFor (button, SkinRenderTarget::textButton, iconContext);
    if (! controlStyle || ! controlStyle->iconBounds) return;
    iconContext.bounds = *controlStyle->iconBounds;
    iconContext.contentBounds = iconContext.bounds;
    static_cast<void> (drawSlotBinding (
        graphics, button, SkinRenderTarget::textButton, slot,
        SkinRenderTarget::surface, std::move (iconContext), slot));
}

void JuceSkinAdapter::drawButtonText (
    juce::Graphics&, juce::TextButton&, bool, bool)
{
    // Text is part of the declarative textButton renderer and must not be painted twice.
}

void JuceSkinAdapter::drawToggleButton (
    juce::Graphics& graphics, juce::ToggleButton& button,
    bool highlighted, bool down)
{
    SkinRenderContext context;
    context.state = stateFor (button, highlighted, down, button.getToggleState());
    context.primaryText = button.getButtonText();
    drawBinding (graphics, button, SkinRenderTarget::toggleButton, std::move (context));
}

void JuceSkinAdapter::drawComboBox (
    juce::Graphics& graphics, int width, int height, bool down,
    int, int, int, int, juce::ComboBox& box)
{
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (box, box.isMouseOver (true), down);
    // The popup owns focus feedback while its rounded corners expose the trigger.
    context.state.focused = context.state.focused && ! box.isPopupActive();
    context.primaryText = box.getText().isNotEmpty() ? box.getText() : box.getTextWhenNothingSelected();
    context.extraLookup = [open = box.isPopupActive(), selected = box.getSelectedId() > 0] (std::string_view path) -> std::optional<SkinExpressionValue>
    {
        if (path == "combo.popupOpen") return SkinExpressionValue (open);
        if (path == "combo.hasSelection") return SkinExpressionValue (selected);
        return std::nullopt;
    };
    drawBinding (graphics, box, SkinRenderTarget::comboBox, std::move (context));
}

void JuceSkinAdapter::drawComboBoxTextWhenNothingSelected (
    juce::Graphics&, juce::ComboBox&, juce::Label&)
{
    // The comboBox renderer owns both selected text and the empty-selection prompt.
}

void JuceSkinAdapter::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (box.getLocalBounds());
    box.setColour (juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    label.setColour (juce::Label::textColourId, juce::Colours::transparentBlack);
    label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label.setAlpha (0.0f);
}

void JuceSkinAdapter::fillTextEditorBackground (
    juce::Graphics& graphics, int width, int height, juce::TextEditor& editor)
{
    configureTextEditor (editor);
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (editor, editor.isMouseOver (true), false);
    // The renderer draws the empty prompt; editable text, selection and caret
    // share its label geometry through the native editor viewport.
    if (editor.isEmpty() && ! editor.hasKeyboardFocus (false))
        context.primaryText = editor.getTextToShowWhenEmpty();
    drawBinding (graphics, editor, SkinRenderTarget::textEditor, std::move (context));
}

std::optional<JuceSkinAdapter::TextStyle> JuceSkinAdapter::textStyleFor (
    const juce::Component& component, SkinRenderTarget target, SkinRenderContext context) const
{
    RuntimeBinding binding;
    try { binding = bindingFor (component, target); }
    catch (...) { return std::nullopt; }
    if (binding.styleEntry == nullptr) return std::nullopt;
    const auto* found = findLayer (binding.styleEntry->renderer.baseLayers, "label");
    if (found == nullptr) return std::nullopt;
    if (context.bounds.isEmpty()) context.bounds = component.getLocalBounds().toFloat();
    if (context.contentBounds.isEmpty()) context.contentBounds = context.bounds;
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (component));
    const auto layer = layerWithVariants (binding.styleEntry->renderer, *found, context.state);
    const auto expressionContext = expressionContextFor (context);
    const auto bounds = resolvedFrameFor (layer, context, expressionContext);
    const auto fontValue = resolvedProperty<SkinFontValue> (layer, "font", expressionContext, bounds);
    const auto fontSize = resolvedProperty<double> (layer, "fontSize", expressionContext, bounds);
    const auto colour = resolvedProperty<juce::Colour> (layer, "color", expressionContext, bounds);
    if (! fontValue || ! fontSize || *fontSize <= 0.0 || ! colour) return std::nullopt;
    const auto fontBinding = layer.properties.find ("font");
    const auto assets = fontBinding != layer.properties.end() && fontBinding->second.assets != nullptr
        ? fontBinding->second.assets : binding.styleEntry->assets;
    const auto font = assets != nullptr ? assets->makeFont (*fontValue, static_cast<float> (*fontSize))
                                       : SkinFontResolver::makeFallbackFont (*fontValue, static_cast<float> (*fontSize));
    const auto opacity = resolvedProperty<bool> (layer, "visible", expressionContext, bounds).value_or (true)
        ? juce::jlimit (0.0, 1.0, resolvedProperty<double> (layer, "opacity", expressionContext, bounds).value_or (1.0))
        : 0.0;
    return TextStyle { font, colour->withMultipliedAlpha (static_cast<float> (opacity)), bounds,
        justificationForAlignment (resolvedProperty<juce::String> (layer, "align", expressionContext, bounds).value_or ("left")),
        juce::jlimit (1, 32, juce::roundToInt (resolvedProperty<double> (layer, "maxLines", expressionContext, bounds).value_or (1.0))) };
}

void JuceSkinAdapter::configureTextEditor (juce::TextEditor& editor) const
{
    SkinRenderContext context;
    context.state = stateFor (editor, editor.isMouseOver (true), false);
    context.primaryText = editor.getText();
    const auto style = textStyleFor (editor, SkinRenderTarget::textEditor, context);
    if (! style) return;
    if (editor.findColour (juce::TextEditor::textColourId) != style->colour)
        editor.applyColourToAllText (style->colour, true);
    if (const auto controls = controlStyleFor (editor, SkinRenderTarget::textEditor, context))
        for (const auto& [id, colour] : {
                 std::pair<int, std::optional<juce::Colour>> { juce::TextEditor::highlightColourId, controls->selectionColour },
                 { juce::TextEditor::highlightedTextColourId, controls->selectedTextColour },
                 { juce::CaretComponent::caretColourId, controls->caretColour } })
            if (colour && editor.findColour (id) != *colour) editor.setColour (id, *colour);
    editor.setTextToShowWhenEmpty (editor.getTextToShowWhenEmpty(), juce::Colours::transparentBlack);
    if (editor.getFont() != style->font) editor.applyFontToAllText (style->font, true);
    if (! editor.getLocalBounds().isEmpty())
    {
        const auto frame = style->bounds.getSmallestIntegerContainer().getIntersection (editor.getLocalBounds());
        const juce::BorderSize<int> border (frame.getY(), frame.getX(),
            editor.getHeight() - frame.getBottom(), editor.getWidth() - frame.getRight());
        if (editor.getBorder() != border) editor.setBorder (border);
        editor.setIndents (0, 0);
        editor.setJustification (style->alignment);
    }
}

void JuceSkinAdapter::drawTextEditorOutline (
    juce::Graphics&, int, int, juce::TextEditor&)
{
    // Outline and focus are layers in the textEditor renderer.
}

void JuceSkinAdapter::drawLinearSlider (
    juce::Graphics& graphics, int x, int y, int width, int height,
    float, float, float,
    juce::Slider::SliderStyle, juce::Slider& slider)
{
    SkinRenderContext context;
    juce::ignoreUnused (x, y, width, height);
    context.bounds = slider.getLocalBounds().toFloat();
    context.contentBounds = context.bounds;
    context.state = stateFor (slider, slider.isMouseOver (true), slider.isMouseButtonDown());
    context.minimum = slider.getMinimum();
    context.maximum = slider.getMaximum();
    context.normalisedValue = slider.valueToProportionOfLength (slider.getValue());
    context.displayValue = slider.getTextFromValue (slider.getValue());
    drawBinding (graphics, slider, SkinRenderTarget::linearSlider, std::move (context));
}

void JuceSkinAdapter::drawRotarySlider (
    juce::Graphics& graphics, int x, int y, int width, int height,
    float sliderPos, float, float, juce::Slider& slider)
{
    SkinRenderContext context;
    juce::ignoreUnused (x, y, width, height);
    context.bounds = slider.getLocalBounds().toFloat();
    context.contentBounds = context.bounds;
    context.state = stateFor (slider, slider.isMouseOver (true), slider.isMouseButtonDown());
    context.minimum = slider.getMinimum();
    context.maximum = slider.getMaximum();
    context.normalisedValue = juce::jlimit (0.0, 1.0, static_cast<double> (sliderPos));
    context.displayValue = slider.getTextFromValue (slider.getValue());
    drawBinding (graphics, slider, SkinRenderTarget::rotarySlider, std::move (context));
}

void JuceSkinAdapter::drawProgressBar (
    juce::Graphics& graphics, juce::ProgressBar& progressBar,
    int width, int height, double progress, const juce::String& text)
{
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (progressBar, progressBar.isMouseOver (true), false);
    context.normalisedValue = juce::jlimit (0.0, 1.0, progress);
    context.displayValue = text;
    drawBinding (graphics, progressBar, SkinRenderTarget::progressBar, std::move (context));
}

juce::Slider::SliderLayout JuceSkinAdapter::getSliderLayout (juce::Slider& slider)
{
    const auto target = slider.isRotary() ? SkinRenderTarget::rotarySlider : SkinRenderTarget::linearSlider;
    if (const auto controls = controlStyleFor (slider, target); controls && controls->trackBounds)
    {
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = controls->trackBounds->toNearestInt();
        return layout;
    }
    return juce::LookAndFeel_V4::getSliderLayout (slider);
}

int JuceSkinAdapter::getSliderThumbRadius (juce::Slider& slider)
{
    const auto target = slider.isRotary() ? SkinRenderTarget::rotarySlider : SkinRenderTarget::linearSlider;
    if (const auto controls = controlStyleFor (slider, target); controls && controls->thumbRadius)
        return juce::roundToInt (*controls->thumbRadius);
    return juce::LookAndFeel_V4::getSliderThumbRadius (slider);
}

int JuceSkinAdapter::getDefaultScrollbarWidth()
{
    if (const auto controls = controlStyleFor (ancillaryProxy, SkinRenderTarget::scrollBar);
        controls && controls->thickness)
        return juce::roundToInt (*controls->thickness);
    return juce::LookAndFeel_V4::getDefaultScrollbarWidth();
}

int JuceSkinAdapter::getMinimumScrollbarThumbSize (juce::ScrollBar& scrollBar)
{
    SkinRenderContext context;
    context.scrollOrientation = scrollBar.isVertical() ? "vertical" : "horizontal";
    if (const auto controls = controlStyleFor (scrollBar, SkinRenderTarget::scrollBar, context);
        controls && controls->minThumbLength)
        return juce::roundToInt (*controls->minThumbLength);
    return juce::LookAndFeel_V4::getMinimumScrollbarThumbSize (scrollBar);
}

void JuceSkinAdapter::drawScrollbar (
    juce::Graphics& graphics, juce::ScrollBar& scrollBar, int x, int y,
    int width, int height, bool vertical, int thumbStart, int thumbSize,
    bool over, bool down)
{
    SkinRenderContext context;
    context.bounds = { static_cast<float> (x), static_cast<float> (y),
                       static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (scrollBar, over, down);
    context.scrollOrientation = vertical ? "vertical" : "horizontal";
    context.scrollPart = "bar";
    context.scrollThumbStart = thumbStart;
    context.scrollThumbSize = thumbSize;
    drawBinding (graphics, scrollBar, SkinRenderTarget::scrollBar,
                 std::move (context), "default");
}

void JuceSkinAdapter::drawScrollbarButton (
    juce::Graphics& graphics, juce::ScrollBar& scrollBar, int width, int height,
    int buttonDirection, bool vertical, bool over, bool down)
{
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (scrollBar, over, down);
    context.scrollOrientation = vertical ? "vertical" : "horizontal";
    context.scrollPart = buttonDirection == 0 || buttonDirection == 3
                       ? "buttonStart" : "buttonEnd";
    context.iconName = "direction" + juce::String (buttonDirection);
    drawBinding (graphics, scrollBar, SkinRenderTarget::scrollBar,
                 std::move (context), "default",
                 "button:" + juce::String (buttonDirection));
}

int JuceSkinAdapter::getPopupMenuBorderSize()
{
    // Menu outlines belong to the skin, including menus embedded in a parent.
    return 0;
}

juce::Font JuceSkinAdapter::getPopupMenuFont()
{
    SkinRenderContext context;
    context.menuPart = "item";
    if (const auto style = textStyleFor (ancillaryProxy, SkinRenderTarget::popupMenu, std::move (context)))
        return style->font;
    return juce::LookAndFeel_V4::getPopupMenuFont();
}

void JuceSkinAdapter::getIdealPopupMenuItemSize (const juce::String& text, bool separator,
                                                int standardHeight, int& width, int& height)
{
    juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text, separator, standardHeight, width, height);
    if (separator) return;
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.menuPart = "item";
    context.primaryText = text;
    if (const auto layout = rowLayoutFor (ancillaryProxy, SkinRenderTarget::popupMenu, context))
    {
        height = juce::jmax (height, static_cast<int> (std::ceil (layout->rowHeight)));
        context.bounds.setHeight (static_cast<float> (height));
        context.contentBounds = context.bounds;
    }
    if (const auto style = textStyleFor (ancillaryProxy, SkinRenderTarget::popupMenu, context))
    {
        width = juce::jmax (width, static_cast<int> (std::ceil (juce::GlyphArrangement::getStringWidth (style->font, text)
            + context.bounds.getWidth() - style->bounds.getWidth())));
        height = juce::jmax (height, static_cast<int> (std::ceil (style->font.getHeight()
            + context.bounds.getHeight() - style->bounds.getHeight())));
    }
}

juce::Rectangle<int> JuceSkinAdapter::getTooltipBounds (
    const juce::String& text, juce::Point<int> screenPosition, juce::Rectangle<int> parentArea)
{
    const auto fallback = juce::LookAndFeel_V4::getTooltipBounds (text, screenPosition, parentArea);
    SkinRenderContext context;
    context.bounds = parentArea.withPosition (0, 0).toFloat();
    context.contentBounds = context.bounds;
    context.primaryText = text;
    const auto controls = controlStyleFor (ancillaryProxy, SkinRenderTarget::tooltip, context);
    if (! controls || ! controls->maxWidth || ! controls->offsetX || ! controls->offsetY)
        return fallback;
    context.bounds.setWidth (juce::jmin (context.bounds.getWidth(), *controls->maxWidth));
    context.contentBounds = context.bounds;
    const auto style = textStyleFor (ancillaryProxy, SkinRenderTarget::tooltip, context);
    if (! style) return fallback;
    const auto horizontalInset = juce::jmax (0.0f, context.bounds.getWidth() - style->bounds.getWidth());
    const auto verticalInset = juce::jmax (0.0f, context.bounds.getHeight() - style->bounds.getHeight());
    const auto textWidth = juce::jmax (1.0f, context.bounds.getWidth() - horizontalInset);
    juce::AttributedString attributed;
    attributed.append (text, style->font, style->colour);
    juce::TextLayout layout;
    layout.createLayout (attributed, textWidth);
    const auto width = static_cast<int> (std::ceil (layout.getWidth() + horizontalInset));
    const auto height = static_cast<int> (std::ceil (
        juce::jmin (juce::jmax (style->font.getHeight(), layout.getHeight()),
                    style->font.getHeight() * static_cast<float> (style->maxLines)) + verticalInset));
    const auto offsetX = juce::roundToInt (*controls->offsetX);
    const auto offsetY = juce::roundToInt (*controls->offsetY);
    return juce::Rectangle<int> (
        screenPosition.x > parentArea.getCentreX() ? screenPosition.x - width - offsetX : screenPosition.x + offsetX,
        screenPosition.y > parentArea.getCentreY() ? screenPosition.y - height - offsetY : screenPosition.y + offsetY,
        width, height).constrainedWithin (parentArea);
}

void JuceSkinAdapter::drawPopupMenuBackground (
    juce::Graphics& graphics, int width, int height)
{
    popupItemIndices.clear();
    nextPopupItemIndex = 0;
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.menuPart = "background";
    drawBinding (graphics, ancillaryProxy, SkinRenderTarget::popupMenu,
                 std::move (context), "default", "background");
}

void JuceSkinAdapter::drawPopupMenuItem (
    juce::Graphics& graphics, const juce::Rectangle<int>& area,
    bool separator, bool active, bool highlighted, bool ticked, bool hasSubMenu,
    const juce::String& text, const juce::String& shortcut,
    const juce::Drawable*, const juce::Colour*)
{
    drawPopupMenuPart (graphics, area, separator, active, highlighted, ticked,
                       hasSubMenu, text, shortcut,
                       popupItemIndexFor (nullptr, separator));
}

void JuceSkinAdapter::drawPopupMenuItemWithOptions (
    juce::Graphics& graphics, const juce::Rectangle<int>& area, bool highlighted,
    const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&)
{
    const auto hasSubMenu = item.subMenu != nullptr
        && (item.itemID == 0 || item.subMenu->getNumItems() > 0);
    drawPopupMenuPart (graphics, area, item.isSeparator, item.isEnabled,
                       highlighted, item.isTicked, hasSubMenu, item.text,
                       item.shortcutKeyDescription,
                       popupItemIndexFor (&item, item.isSeparator));
}

int JuceSkinAdapter::popupItemIndexFor (
    const juce::PopupMenu::Item* item, bool separator)
{
    if (separator) return -1;
    if (item != nullptr)
    {
        const auto found = popupItemIndices.find (item);
        if (found != popupItemIndices.end()) return found->second;
        const auto index = nextPopupItemIndex++;
        popupItemIndices.emplace (item, index);
        return index;
    }
    return nextPopupItemIndex++;
}

void JuceSkinAdapter::drawPopupMenuPart (
    juce::Graphics& graphics, const juce::Rectangle<int>& area,
    bool separator, bool active, bool highlighted, bool ticked,
    bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
    int itemIndex)
{
    SkinRenderContext context;
    context.bounds = area.toFloat();
    context.contentBounds = context.bounds;
    context.state.enabled = active;
    context.state.hover = highlighted;
    context.state.selected = ticked;
    context.primaryText = text;
    context.secondaryText = shortcut;
    context.menuItemIndex = separator ? -1 : itemIndex;
    context.iconName = ticked ? "checked" : (hasSubMenu ? "submenu" : juce::String());
    context.menuPart = separator ? "separator" : "item";
    const auto itemKey = (separator ? "separator:" : "item:")
        + juce::String (itemIndex) + ":" + juce::String (area.getX())
        + ":" + juce::String (area.getY()) + ":" + text;
    drawBinding (graphics, ancillaryProxy, SkinRenderTarget::popupMenu,
                 std::move (context), "default", itemKey);
}

void JuceSkinAdapter::drawPopupMenuSectionHeader (
    juce::Graphics& graphics, const juce::Rectangle<int>& area,
    const juce::String& sectionName)
{
    SkinRenderContext context;
    context.bounds = area.toFloat();
    context.contentBounds = context.bounds;
    context.primaryText = sectionName;
    context.menuPart = "sectionHeader";
    drawBinding (graphics, ancillaryProxy, SkinRenderTarget::popupMenu,
                 std::move (context), "default", "section:" + sectionName);
}

void JuceSkinAdapter::drawPopupMenuUpDownArrow (
    juce::Graphics& graphics, int width, int height, bool isScrollUpArrow)
{
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.menuPart = "item";
    context.iconName = isScrollUpArrow ? "scrollUp" : "scrollDown";
    drawBinding (graphics, ancillaryProxy, SkinRenderTarget::popupMenu,
                 std::move (context), "default",
                 isScrollUpArrow ? "scrollUp" : "scrollDown");
}

void JuceSkinAdapter::drawPopupMenuColumnSeparatorWithOptions (
    juce::Graphics& graphics, const juce::Rectangle<int>& area,
    const juce::PopupMenu::Options&)
{
    SkinRenderContext context;
    context.bounds = area.toFloat();
    context.contentBounds = context.bounds;
    context.menuPart = "separator";
    drawBinding (graphics, ancillaryProxy, SkinRenderTarget::popupMenu,
                 std::move (context), "default", "columnSeparator");
}

void JuceSkinAdapter::drawTooltip (
    juce::Graphics& graphics, const juce::String& text, int width, int height)
{
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.primaryText = text;
    drawBinding (graphics, ancillaryProxy, SkinRenderTarget::tooltip,
                 std::move (context), "default");
}

void JuceSkinAdapter::drawTableHeaderBackground (
    juce::Graphics& graphics, juce::TableHeaderComponent& header)
{
    auto& owner = tableStyleOwner (header);
    SkinRenderContext context;
    context.bounds = header.getLocalBounds().toFloat();
    context.contentBounds = context.bounds;
    context.state = stateFor (header, header.isMouseOver (true), false);
    context.tableColumnCount = header.getNumColumns (true);
    if (const auto* table = dynamic_cast<const SkinTableListBox*> (&owner))
        if (const auto layout = table->resolvedRowLayout())
            if (const auto found = layout->slots.find ("header.surface"); found != layout->slots.end())
            {
                context.bounds = found->second;
                context.contentBounds = context.bounds;
            }
    if (! drawSlotBinding (graphics, owner, SkinRenderTarget::table,
                           "header.surface", SkinRenderTarget::surface, context,
                           "header:surface"))
        drawBinding (graphics, owner, SkinRenderTarget::table, std::move (context));
}

void JuceSkinAdapter::drawTableHeaderColumn (
    juce::Graphics& graphics, juce::TableHeaderComponent& header,
    const juce::String& name, int columnId, int width, int height,
    bool over, bool down, int)
{
    auto& owner = tableStyleOwner (header);
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (header, over, down);
    context.primaryText = name;
    context.textAlignment = columnId == 3 ? "right" : "left";
    context.tableColumnCount = header.getNumColumns (true);
    const auto textDrawn = drawSlotBinding (
        graphics, owner, SkinRenderTarget::table, "header.text",
        SkinRenderTarget::label, context, "header:" + juce::String (columnId));
    if (! textDrawn)
        drawBinding (graphics, owner, SkinRenderTarget::table, std::move (context));
}

void JuceSkinAdapter::drawLabel (juce::Graphics& graphics, juce::Label& label)
{
    if (dynamic_cast<juce::ComboBox*> (label.getParentComponent()) != nullptr)
        return;
    SkinRenderContext context;
    context.state = stateFor (label, label.isMouseOver (true), false);
    context.primaryText = label.getText();
    context.textAlignment = alignmentNameFor (label.getJustificationType());
    const auto componentId = label.getProperties().getWithDefault (
        SkinComponentBindings::componentIdProperty, {}).toString();
    if (const auto* descriptor = PlayerComponentCatalog::find (componentId))
        context.textAlignment = descriptor->textAlignment;
    drawBinding (graphics, label, SkinRenderTarget::label, std::move (context));
}

bool JuceSkinAdapter::drawCustomComponent (
    juce::Graphics& graphics, const juce::Component& component,
    SkinRenderTarget target, SkinRenderContext context,
    const juce::String& instanceKey)
{
    auto state = stateFor (component, component.isMouseOver (true),
                           component.isMouseButtonDown(), context.state.selected);
    state.dragging = context.state.dragging || state.dragging;
    // Child controls paint their own focus feedback.
    state.focused = component.hasKeyboardFocus (false);
    context.state = state;
    return drawBinding (graphics, component, target, std::move (context), {},
                        instanceKey);
}

bool JuceSkinAdapter::drawCustomSlot (
    juce::Graphics& graphics, const juce::Component& owner,
    SkinRenderTarget ownerTarget, juce::StringRef slotName,
    SkinRenderTarget slotTarget, SkinRenderContext context,
    const juce::String& instanceKey)
{
    return drawSlotBinding (graphics, owner, ownerTarget, slotName, slotTarget,
                            std::move (context), instanceKey);
}

std::optional<SkinResolvedRowLayout> JuceSkinAdapter::rowLayoutFor (
    const juce::Component& component, SkinRenderTarget target,
    SkinRenderContext context) const
{
    auto binding = bindingFor (component, target);
    if (binding.styleEntry == nullptr || ! binding.styleEntry->renderer.rowLayout)
        binding = bindingFor (component, target, "default");
    if (binding.styleEntry == nullptr || ! binding.styleEntry->renderer.rowLayout)
        return std::nullopt;

    if (context.bounds.isEmpty()) context.bounds = component.getLocalBounds().toFloat();
    if (context.contentBounds.isEmpty()) context.contentBounds = context.bounds;
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (component));
    SkinValidationReport report;
    auto layout = resolveSkinRowLayout (binding.styleEntry->renderer, context, report);
    if (binding.faultSink != nullptr)
        for (const auto& diagnostic : report.getDiagnostics())
            binding.faultSink->reportRenderFault (
                diagnostic, diagnostic.severity == SkinDiagnosticSeverity::warning
                    ? SkinRenderStatus::ok
                    : binding.requiredComponent ? SkinRenderStatus::fatalFault
                                                : SkinRenderStatus::recoverableFault);
    return layout;
}

std::optional<SkinResolvedControlStyle> JuceSkinAdapter::controlStyleFor (
    const juce::Component& component, SkinRenderTarget target,
    SkinRenderContext context) const
{
    auto binding = bindingFor (component, target);
    if (binding.styleEntry == nullptr || ! binding.styleEntry->renderer.controlStyle)
        binding = bindingFor (component, target, "default");
    if (binding.styleEntry == nullptr || ! binding.styleEntry->renderer.controlStyle)
        return std::nullopt;
    if (context.bounds.isEmpty()) context.bounds = component.getLocalBounds().toFloat();
    if (context.contentBounds.isEmpty()) context.contentBounds = context.bounds;
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (component));
    SkinValidationReport report;
    auto controls = resolveSkinControlStyle (binding.styleEntry->renderer, context, report);
    if (binding.faultSink != nullptr)
        for (const auto& diagnostic : report.getDiagnostics())
            binding.faultSink->reportRenderFault (
                diagnostic, diagnostic.severity == SkinDiagnosticSeverity::warning
                    ? SkinRenderStatus::ok
                    : binding.requiredComponent ? SkinRenderStatus::fatalFault
                                                : SkinRenderStatus::recoverableFault);
    return controls;
}

void JuceSkinAdapter::applySlotStyle (juce::Component& child, const juce::Component& owner,
                                     SkinRenderTarget ownerTarget, juce::StringRef slot) const
{
    const auto binding = bindingFor (owner, ownerTarget);
    if (binding.styleEntry == nullptr) return;
    const auto found = binding.styleEntry->slotStyles.find (juce::String (slot));
    const auto style = found != binding.styleEntry->slotStyles.end() ? found->second : juce::String ("default");
    if (child.getProperties().getWithDefault (styleProperty, {}).toString() == style) return;
    child.getProperties().set (styleProperty, style);
    child.resized();
    child.repaint();
}

void JuceSkinAdapter::configureListBox (juce::ListBox& list) const
{
    list.setOutlineThickness (0);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
}

void JuceSkinAdapter::configureListContentClip (juce::ListBox& list, SkinRenderTarget target) const
{
    for (auto* child : list.getChildren())
        if (child->getCachedComponentImage() == nullptr)
            child->setCachedComponentImage (new SkinListContentPainter (*child, list, target));
}

void JuceSkinAdapter::clipListContent (juce::Graphics& graphics, const juce::ListBox& list,
                                      const juce::Component& child, SkinRenderTarget target) const
{
    const auto binding = bindingFor (list, target);
    if (binding.styleEntry == nullptr || binding.styleEntry->renderer.contentClip.isEmpty()) return;
    SkinRenderContext context;
    context.bounds = context.contentBounds = list.getLocalBounds().toFloat();
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (list));
    context.state = stateFor (list, list.isMouseOver (true), false);
    context.state.focused = list.hasKeyboardFocus (false);
    if (auto* model = list.getListBoxModel()) context.tableRowCount = model->getNumRows();
    if (const auto* table = dynamic_cast<const juce::TableListBox*> (&list))
        context.tableColumnCount = table->getHeader().getNumColumns (true);
    context.extraLookup = [presentation = list.getProperties().getWithDefault (presentationProperty, "table").toString()]
        (std::string_view path) -> std::optional<SkinExpressionValue>
    {
        if (path == "table.presentation") return SkinExpressionValue (presentation);
        return std::nullopt;
    };
    const auto origin = child.getLocalPoint (&list, juce::Point<float> {});
    const auto xAxis = child.getLocalPoint (&list, juce::Point<float> { 1, 0 });
    const auto yAxis = child.getLocalPoint (&list, juce::Point<float> { 0, 1 });
    const juce::AffineTransform ownerToChild (
        xAxis.x - origin.x, yAxis.x - origin.x, origin.x,
        xAxis.y - origin.y, yAxis.y - origin.y, origin.y);
    auto& sink = binding.faultSink != nullptr ? *binding.faultSink : nullFaultSink();
    renderer.clipContent (graphics, binding.styleEntry->renderer, context, ownerToChild,
                          binding.requiredComponent, sink);
}

bool JuceSkinAdapter::drawListBoxBackground (
    juce::Graphics& graphics, juce::ListBox& list,
    int rowCount, int columnCount)
{
    SkinRenderContext context;
    context.state = stateFor (list, list.isMouseOver (true), false);
    context.tableRowCount = juce::jmax (0, rowCount);
    context.tableColumnCount = juce::jmax (0, columnCount);
    return drawBinding (graphics, list, SkinRenderTarget::table,
                        std::move (context));
}

void JuceSkinAdapter::drawListBoxRow (
    juce::Graphics& graphics, juce::ListBox& list, int rowNumber,
    int width, int height, bool selected, const juce::String& primaryText,
    const juce::String& locationText, const juce::String& durationText,
    const std::optional<ListBoxRowGeometry>& geometry, bool hovered, bool pressed,
    bool available, const juce::String& artist, const juce::String& album)
{
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (list, hovered, pressed, selected);
    context.primaryText = primaryText;
    context.secondaryText = locationText;
    context.extraLookup = [rowNumber, selected, available,
                           primaryText, locationText, artist, album,
                           durationText]
                          (std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "row.index")
            return SkinExpressionValue (static_cast<double> (rowNumber));
        if (path == "row.selected") return SkinExpressionValue (selected);
        if (path == "row.available") return SkinExpressionValue (available);
        if (path == "row.title") return SkinExpressionValue (primaryText);
        if (path == "row.location") return SkinExpressionValue (locationText);
        if (path == "row.artist") return SkinExpressionValue (artist);
        if (path == "row.album") return SkinExpressionValue (album);
        if (path == "row.duration") return SkinExpressionValue (durationText);
        return std::nullopt;
    };
    const auto rowKey = "row:" + juce::String (rowNumber);
    const auto rowWidth = static_cast<float> (width);
    auto surfaceContext = context;
    if (const auto* table = dynamic_cast<const SkinTableListBox*> (&list))
        if (const auto layout = table->resolvedRowLayout())
        {
            const auto found = layout->slots.find ("row.surface");
            surfaceContext.bounds = found != layout->slots.end()
                ? found->second : context.bounds.withHeight (layout->rowHeight);
            surfaceContext.contentBounds = surfaceContext.bounds;
        }
    const auto surfaceDrawn = drawSlotBinding (
        graphics, list, SkinRenderTarget::table, "row.surface",
        SkinRenderTarget::surface, surfaceContext, rowKey + ":surface");
    const auto cells = geometry.value_or (ListBoxRowGeometry {
        { 0.0f, 0.0f, rowWidth * 0.5f, static_cast<float> (height) },
        { rowWidth * 0.5f, 0.0f, rowWidth * 0.3f, static_cast<float> (height) },
        { rowWidth * 0.8f, 0.0f, rowWidth * 0.2f, static_cast<float> (height) }
    });
    const auto drawCell = [&] (juce::Rectangle<float> bounds,
                               const juce::String& columnId,
                               const juce::String& text,
                               const juce::String& alignment)
    {
        if (bounds.isEmpty()) return false;
        auto cellContext = context;
        cellContext.bounds = bounds;
        cellContext.contentBounds = bounds;
        cellContext.primaryText = text;
        cellContext.textAlignment = alignment;
        cellContext.extraLookup = [lookup = context.extraLookup, columnId] (
            std::string_view path) -> std::optional<SkinExpressionValue>
        {
            if (path == "column.id") return SkinExpressionValue (columnId);
            return lookup ? lookup (path) : std::nullopt;
        };
        return drawSlotBinding (
            graphics, list, SkinRenderTarget::table, "cell." + columnId,
            SkinRenderTarget::label, std::move (cellContext),
            rowKey + ":" + columnId);
    };
    const auto titleDrawn = drawCell (cells.title, "title", primaryText, "left");
    const auto locationDrawn = drawCell (cells.location, "location", locationText, "left");
    const auto durationDrawn = drawCell (cells.duration, "duration", durationText, "right");
    if (! surfaceDrawn && ! titleDrawn && ! locationDrawn && ! durationDrawn)
        drawBinding (graphics, list, SkinRenderTarget::table, std::move (context));
}

std::optional<JuceSkinAdapter::ArtworkImageStyle>
JuceSkinAdapter::artworkImageStyleFor (
    const juce::Component& component, SkinRenderContext context) const
{
    return artworkLayerStyleFor (component, std::move (context), "image-clip");
}

std::optional<JuceSkinAdapter::ArtworkImageStyle>
JuceSkinAdapter::artworkAmbienceStyleFor (
    const juce::Component& component, SkinRenderContext context) const
{
    return artworkLayerStyleFor (component, std::move (context), "image-ambience");
}

std::optional<JuceSkinAdapter::ArtworkImageStyle>
JuceSkinAdapter::artworkLayerStyleFor (
    const juce::Component& component, SkinRenderContext context, juce::StringRef layerId) const
{
    RuntimeBinding binding;
    try
    {
        binding = bindingFor (component, SkinRenderTarget::artwork);
    }
    catch (...)
    {
        return std::nullopt;
    }
    if (binding.styleEntry == nullptr)
        return std::nullopt;

    const auto found = std::find_if (
        binding.styleEntry->renderer.baseLayers.begin(),
        binding.styleEntry->renderer.baseLayers.end(),
        [layerId] (const SkinRenderLayer& layer) { return layer.id == layerId; });
    if (found == binding.styleEntry->renderer.baseLayers.end()
        || found->primitive != SkinPrimitive::roundedRectangle)
        return std::nullopt;

    if (context.bounds.isEmpty())
        context.bounds = component.getLocalBounds().toFloat();
    if (context.contentBounds.isEmpty())
        context.contentBounds = context.bounds;
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (component));
    auto state = stateFor (component, component.isMouseOver (true),
                           component.isMouseButtonDown(), context.state.selected);
    state.dragging = context.state.dragging || state.dragging;
    context.state = state;
    const auto layer = layerWithVariants (binding.styleEntry->renderer, *found, state);
    const auto expressionContext = expressionContextFor (context);
    auto bounds = resolvedFrameFor (layer, context, expressionContext);
    if (! std::isfinite (bounds.getX()) || ! std::isfinite (bounds.getY())
        || ! std::isfinite (bounds.getWidth()) || ! std::isfinite (bounds.getHeight())
        || bounds.isEmpty())
        return std::nullopt;

    auto radius = static_cast<float> (resolvedProperty<double> (
        layer, "radius", expressionContext, bounds).value_or (0.0));
    if (! std::isfinite (radius))
        return std::nullopt;
    radius = juce::jlimit (0.0f,
                           juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f,
                           radius);
    return ArtworkImageStyle {
        bounds, radius,
        resolvedProperty<bool> (layer, "clip", expressionContext, bounds).value_or (false),
        static_cast<float> (juce::jlimit (0.0, 1.0,
            resolvedProperty<double> (layer, "opacity", expressionContext, bounds).value_or (1.0))),
        resolvedProperty<bool> (layer, "visible", expressionContext, bounds).value_or (true)
    };
}
