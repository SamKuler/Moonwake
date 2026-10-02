#include "JuceSkinAdapter.h"

#include "skin/SkinAssetCache.h"
#include "skin/SkinFontResolver.h"
#include "skin/SkinComponentBindings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

const juce::Identifier JuceSkinAdapter::styleProperty { "skinStyle" };
const juce::Identifier JuceSkinAdapter::scaleFactorProperty { "skinScaleFactor" };
const juce::Identifier JuceSkinAdapter::presentationProperty { "skinPresentation" };

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

juce::Rectangle<float> iconBoundsFor (const juce::Button& button,
                                      const juce::String& presentation)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto requested = bounds.getHeight() >= 46.0f ? 20.0f : 18.0f;
    const auto size = juce::jmax (12.0f, juce::jmin (
        requested, juce::jmin (bounds.getWidth(), bounds.getHeight()) - 12.0f));
    const auto y = bounds.getCentreY() - size * 0.5f;
    if (presentation == "leading")
        return { bounds.getX() + 10.0f, y, size, size };
    return { bounds.getCentreX() - size * 0.5f, y, size, size };
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
    iconContext.bounds = iconBoundsFor (
        button, button.getProperties()
                    .getWithDefault (
                        SkinComponentBindings::iconPresentationProperty, "only")
                    .toString());
    iconContext.contentBounds = iconContext.bounds;
    iconContext.state = state;
    iconContext.iconName = iconName;
    iconContext.iconPathData = iconPath;
    iconContext.iconPath = PlayerComponentCatalog::fallbackIconPath (iconName);
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
    context.primaryText = box.getText();
    drawBinding (graphics, box, SkinRenderTarget::comboBox, std::move (context));
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
    SkinRenderContext context;
    context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (editor, editor.isMouseOver (true), false);
    // Editable text, selection and the caret remain native JUCE content. The
    // declarative renderer owns only the editor chrome.
    context.primaryText = {};
    drawBinding (graphics, editor, SkinRenderTarget::textEditor, std::move (context));
}

void JuceSkinAdapter::configureTextEditor (juce::TextEditor& editor) const
{
    RuntimeBinding binding;
    try
    {
        binding = bindingFor (editor, SkinRenderTarget::textEditor);
    }
    catch (...)
    {
        return;
    }
    if (binding.styleEntry == nullptr)
        return;

    const auto* label = findLayer (
        binding.styleEntry->renderer.baseLayers, "label");
    if (label == nullptr)
        return;
    const auto colour = literalProperty<juce::Colour> (*label, "color");
    if (colour)
    {
        editor.setColour (juce::TextEditor::textColourId, *colour);
        editor.setColour (juce::TextEditor::highlightColourId,
                          colour->withAlpha (0.18f));
        editor.setColour (juce::TextEditor::highlightedTextColourId, *colour);
        editor.setColour (juce::CaretComponent::caretColourId, *colour);
        editor.setTextToShowWhenEmpty (
            editor.getTextToShowWhenEmpty(), colour->withAlpha (0.58f));
        editor.applyColourToAllText (*colour, true);
    }

    const auto fontValue = literalProperty<SkinFontValue> (*label, "font");
    const auto fontSize = literalProperty<double> (*label, "fontSize");
    if (fontValue && fontSize && *fontSize > 0.0)
    {
        auto font = binding.styleEntry->assets != nullptr
                      ? binding.styleEntry->assets->makeFont (
                            *fontValue, static_cast<float> (*fontSize))
                      : SkinFontResolver::makeFallbackFont (
                            *fontValue, static_cast<float> (*fontSize));
        editor.setFont (font);
        editor.applyFontToAllText (font, true);
    }
}

void JuceSkinAdapter::drawTextEditorOutline (
    juce::Graphics&, int, int, juce::TextEditor&)
{
    // Outline and focus are layers in the textEditor renderer.
}

void JuceSkinAdapter::drawLinearSlider (
    juce::Graphics& graphics, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    juce::Slider::SliderStyle, juce::Slider& slider)
{
    SkinRenderContext context;
    context.bounds = { static_cast<float> (x), static_cast<float> (y),
                       static_cast<float> (width), static_cast<float> (height) };
    context.contentBounds = context.bounds;
    context.state = stateFor (slider, slider.isMouseOver (true), slider.isMouseButtonDown());
    context.minimum = slider.getMinimum();
    context.maximum = slider.getMaximum();
    context.normalisedValue = std::abs (maxSliderPos - minSliderPos) > 1.0e-6f
        ? juce::jlimit (0.0, 1.0,
            static_cast<double> ((sliderPos - minSliderPos) / (maxSliderPos - minSliderPos)))
        : 0.0;
    context.displayValue = slider.getTextFromValue (slider.getValue());
    drawBinding (graphics, slider, SkinRenderTarget::linearSlider, std::move (context));
}

void JuceSkinAdapter::drawRotarySlider (
    juce::Graphics& graphics, int x, int y, int width, int height,
    float sliderPos, float, float, juce::Slider& slider)
{
    SkinRenderContext context;
    context.bounds = { static_cast<float> (x), static_cast<float> (y),
                       static_cast<float> (width), static_cast<float> (height) };
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
    context.state = stateFor (header, header.isMouseOver (true), false);
    context.tableColumnCount = header.getNumColumns (true);
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

void JuceSkinAdapter::configureListBox (juce::ListBox& list) const
{
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
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
    const auto surfaceDrawn = drawSlotBinding (
        graphics, list, SkinRenderTarget::table, "row.surface",
        SkinRenderTarget::surface, context, rowKey + ":surface");
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
        [] (const SkinRenderLayer& layer) { return layer.id == "image-clip"; });
    if (found == binding.styleEntry->renderer.baseLayers.end()
        || found->primitive != SkinPrimitive::roundedRectangle)
        return std::nullopt;

    if (context.bounds.isEmpty())
        context.bounds = component.getLocalBounds().toFloat();
    if (context.contentBounds.isEmpty())
        context.contentBounds = context.bounds;
    context.scaleFactor = juce::jmax (0.01f, renderingScaleFactorFor (component));
    const auto expressionContext = expressionContextFor (context);
    auto bounds = resolvedFrameFor (*found, context, expressionContext);
    if (! std::isfinite (bounds.getX()) || ! std::isfinite (bounds.getY())
        || ! std::isfinite (bounds.getWidth()) || ! std::isfinite (bounds.getHeight())
        || bounds.isEmpty())
        return std::nullopt;

    auto radius = static_cast<float> (resolvedProperty<double> (
        *found, "radius", expressionContext, bounds).value_or (0.0));
    if (! std::isfinite (radius))
        return std::nullopt;
    radius = juce::jlimit (0.0f,
                           juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f,
                           radius);
    return ArtworkImageStyle {
        bounds, radius,
        resolvedProperty<bool> (*found, "clip", expressionContext, bounds)
            .value_or (false)
    };
}
