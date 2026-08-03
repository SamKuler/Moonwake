#pragma once

#include "skin/SkinAnimationEngine.h"
#include "skin/SkinRenderer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <vector>

class JuceSkinAdapter final : public juce::LookAndFeel_V4,
                              private juce::Timer
{
public:
    struct RuntimeBinding
    {
        const SkinRenderStyleEntry* styleEntry = nullptr;
        SkinAnimationEngine* animations = nullptr;
        SkinRuntimeFaultSink* faultSink = nullptr;
        bool requiredComponent = false;
    };

    using StyleProvider = std::function<RuntimeBinding (
        const juce::Component&, SkinRenderTarget, const juce::String& style)>;

    JuceSkinAdapter (StyleProvider, SkinRenderer&);

    static const juce::Identifier styleProperty;
    static const juce::Identifier scaleFactorProperty;
    [[nodiscard]] bool isAnimationDriveActive() const noexcept
    {
        return isTimerRunning();
    }

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted,
                         bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted,
                           bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height,
                                   juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height,
                                juce::TextEditor&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height,
                          double progress, const juce::String&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y,
                        int width, int height, bool vertical, int thumbStart,
                        int thumbSize, bool over, bool down) override;
    void drawScrollbarButton (juce::Graphics&, juce::ScrollBar&, int width,
                              int height, int buttonDirection, bool vertical,
                              bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&,
                            bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcut, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawPopupMenuItemWithOptions (
        juce::Graphics&, const juce::Rectangle<int>& area, bool highlighted,
        const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeader (juce::Graphics&,
                                     const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;
    void drawPopupMenuUpDownArrow (juce::Graphics&, int width, int height,
                                   bool isScrollUpArrow) override;
    void drawPopupMenuColumnSeparatorWithOptions (
        juce::Graphics&, const juce::Rectangle<int>&,
        const juce::PopupMenu::Options&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text,
                      int width, int height) override;
    void drawTableHeaderBackground (juce::Graphics&,
                                    juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&,
                                const juce::String& name, int columnId,
                                int width, int height, bool over, bool down,
                                int flags) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    void configureListBox (juce::ListBox&) const;
    void configureTextEditor (juce::TextEditor&) const;
    struct ListBoxRowGeometry
    {
        juce::Rectangle<float> title;
        juce::Rectangle<float> location;
        juce::Rectangle<float> duration;
    };

    bool drawListBoxBackground (juce::Graphics&, juce::ListBox&,
                                int rowCount = 0, int columnCount = 3);
    void drawListBoxRow (juce::Graphics&, juce::ListBox&, int rowNumber,
                         int width, int height, bool selected,
                         const juce::String& primaryText,
                         const juce::String& locationText = {},
                         const juce::String& durationText = {},
                         const ListBoxRowGeometry& geometry = {},
                         bool hovered = false, bool pressed = false,
                         bool available = true);

    bool drawCustomComponent (juce::Graphics&, const juce::Component&,
                              SkinRenderTarget, SkinRenderContext,
                              const juce::String& instanceKey = {});
    bool drawCustomSlot (juce::Graphics&, const juce::Component& owner,
                         SkinRenderTarget ownerTarget, juce::StringRef slotName,
                         SkinRenderTarget slotTarget, SkinRenderContext,
                         const juce::String& instanceKey = {});

private:
    [[nodiscard]] RuntimeBinding bindingFor (const juce::Component&, SkinRenderTarget,
                                             const juce::String& forcedStyle = {}) const;
    bool drawBinding (juce::Graphics&, const juce::Component&, SkinRenderTarget,
                      SkinRenderContext, const juce::String& forcedStyle = {},
                      const juce::String& instanceKey = {});
    bool drawSlotBinding (juce::Graphics&, const juce::Component&,
                          SkinRenderTarget ownerTarget, juce::StringRef slotName,
                          SkinRenderTarget slotTarget, SkinRenderContext,
                          const juce::String& instanceKey = {});
    [[nodiscard]] static SkinControlState stateFor (const juce::Component&, bool over,
                                                    bool down, bool selected = false);
    [[nodiscard]] static juce::String animationKeyFor (
        const juce::Component&, SkinRenderTarget, const juce::String&,
        const juce::String& instanceKey = {});
    void watchAnimations (const juce::Component&, SkinRenderTarget,
                          const juce::String& style,
                          const juce::String& instanceKey);
    void timerCallback() override;
    void drawPopupMenuPart (juce::Graphics&, const juce::Rectangle<int>&,
                            bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcut, int itemIndex);
    int popupItemIndexFor (const juce::PopupMenu::Item*, bool separator);

    struct AnimationWatch
    {
        juce::Component::SafePointer<juce::Component> component;
        SkinRenderTarget target = SkinRenderTarget::surface;
        juce::String style;
        juce::String instanceKey;
    };

    StyleProvider styleProvider;
    SkinRenderer& renderer;
    juce::Component ancillaryProxy;
    std::vector<AnimationWatch> animationWatches;
    std::map<const juce::PopupMenu::Item*, int> popupItemIndices;
    int nextPopupItemIndex = 0;
};
