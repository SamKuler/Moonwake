#include "SkinLayoutHost.h"

#include "JuceSkinAdapter.h"
#include "SkinTableListBox.h"
#include "skin/SkinRenderer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
struct WorkspaceEntryState
{
    bool consume (const juce::String& animationKey)
    {
        return consumedAnimationKeys.insert (animationKey).second;
    }

    std::set<juce::String> consumedAnimationKeys;
};

class LayoutLayer final : public juce::Component
{
public:
    LayoutLayer()
    {
        setInterceptsMouseClicks (false, true);
    }

    void focusOfChildComponentChanged (FocusChangeType) override
    {
        repaint();
    }
};

class LayoutViewport final : public juce::Viewport,
                             private juce::FocusChangeListener
{
public:
    explicit LayoutViewport (const juce::String& name)
        : juce::Viewport (name), wheelListener (*this)
    {
        setWantsKeyboardFocus (false);
        setInterceptsMouseClicks (true, true);
        juce::Desktop::getInstance().addFocusChangeListener (this);
        juce::Desktop::getInstance().addGlobalMouseListener (&wheelListener);
    }

    ~LayoutViewport() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener (&wheelListener);
        juce::Desktop::getInstance().removeFocusChangeListener (this);
    }

    void watchNestedViewports()
    {
        wheelListener.clear();
        if (auto* content = getViewedComponent()) wheelListener.observe (*content);
    }

private:
    class NestedWheelListener final : public juce::MouseListener,
                                      private juce::ComponentListener
    {
    public:
        explicit NestedWheelListener (LayoutViewport& viewport) : owner (viewport) {}
        ~NestedWheelListener() override { clear(); }
        void clear()
        {
            for (const auto& observed : contents)
                if (observed.component != nullptr) observed.component->removeComponentListener (this);
            contents.clear();
        }
        void observe (juce::Component& component)
        {
            if (auto* viewport = dynamic_cast<juce::Viewport*> (&component))
                if (auto* content = viewport->getViewedComponent())
                {
                    contents.push_back ({ content, -1 });
                    content->addComponentListener (this);
                }
            for (auto* child : component.getChildren()) observe (*child);
        }
        bool movedDuringWheel (const juce::Viewport& viewport) const
        {
            // Global listeners run after the target. A wheel event that moved the
            // inner viewport to its limit must not also move the outer page.
            for (const auto& observed : contents)
                if (observed.component == viewport.getViewedComponent())
                    return observed.wheel == juce::Desktop::getInstance().getMouseWheelMoveCounter();
            return false;
        }
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            owner.forwardWheelAtBoundary (event, wheel);
        }
    private:
        void componentMovedOrResized (juce::Component& component, bool moved, bool) override
        {
            if (moved)
                for (auto& observed : contents)
                    if (observed.component == &component)
                        observed.wheel = juce::Desktop::getInstance().getMouseWheelMoveCounter();
        }
        struct ObservedContent
        {
            juce::Component::SafePointer<juce::Component> component;
            int wheel;
        };
        LayoutViewport& owner;
        std::vector<ObservedContent> contents;
    };

    static juce::Point<int> wheelDirection (juce::Viewport& viewport,
                                           const juce::MouseEvent& event,
                                           const juce::MouseWheelDetails& wheel)
    {
        const auto horizontal = viewport.getHorizontalScrollBar().isVisible();
        const auto vertical = viewport.getVerticalScrollBar().isVisible();
        const auto direction = [] (float delta) { return delta > 0.0f ? -1 : delta < 0.0f ? 1 : 0; };
        if (horizontal && vertical && wheel.deltaX != 0.0f && wheel.deltaY != 0.0f)
            return { direction (wheel.deltaX), direction (wheel.deltaY) };
        if (horizontal && (wheel.deltaX != 0.0f || event.mods.isShiftDown() || ! vertical))
            return { direction (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY), 0 };
        return { 0, vertical ? direction (wheel.deltaY) : 0 };
    }

    static bool canMove (const juce::Viewport& viewport, juce::Point<int> direction)
    {
        const auto* content = viewport.getViewedComponent();
        if (content == nullptr) return false;
        const auto view = viewport.getViewArea();
        return (direction.x < 0 && view.getX() > 0)
            || (direction.x > 0 && view.getRight() < content->getWidth())
            || (direction.y < 0 && view.getY() > 0)
            || (direction.y > 0 && view.getBottom() < content->getHeight());
    }

    void forwardWheelAtBoundary (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        if (! isShowing() || ! isParentOf (event.eventComponent)
            || event.mods.isAltDown() || event.mods.isCtrlDown() || event.mods.isCommandDown()) return;
        auto* nested = event.eventComponent;
        while (nested != nullptr && dynamic_cast<juce::Viewport*> (nested) == nullptr)
            nested = nested->getParentComponent();
        if (nested == nullptr || nested == this) return;
        auto* page = nested->getParentComponent();
        while (page != nullptr && dynamic_cast<LayoutViewport*> (page) == nullptr)
            page = page->getParentComponent();
        if (page != this) return;

        // Native viewports consume wheel events at their limits. Route only that
        // blocked case; ordinary bubbling and a child's remaining range stay native.
        bool blocked = false;
        for (auto* current = nested; current != this; current = current->getParentComponent())
            if (auto* viewport = dynamic_cast<juce::Viewport*> (current))
            {
                const auto direction = wheelDirection (*viewport, event, wheel);
                if (direction.isOrigin()) continue;
                if (wheelListener.movedDuringWheel (*viewport)) return;
                if (canMove (*viewport, direction)) return;
                blocked = true;
                break;
            }
        if (! blocked) return;
        for (juce::Component* current = this; current != nullptr; current = current->getParentComponent())
            if (auto* viewport = dynamic_cast<juce::Viewport*> (current))
                if (canMove (*viewport, wheelDirection (*viewport, event, wheel)))
                {
                    viewport->mouseWheelMove (event.getEventRelativeTo (viewport), wheel);
                    return;
                }
    }

    NestedWheelListener wheelListener;

    void globalFocusChanged (juce::Component* focused) override
    {
        if (focused == nullptr || ! isShowing() || ! isParentOf (focused)) return;
        auto* nearest = focused->getParentComponent();
        while (nearest != nullptr && dynamic_cast<LayoutViewport*> (nearest) == nullptr)
            nearest = nearest->getParentComponent();
        if (nearest != this) return;

        // Reveal inner content first so outer viewports use its updated position.
        for (auto* ancestor = nearest; ancestor != nullptr; ancestor = ancestor->getParentComponent())
            if (auto* viewport = dynamic_cast<LayoutViewport*> (ancestor))
                viewport->reveal (*focused);
    }

    void reveal (juce::Component& focused)
    {
        const auto* content = getViewedComponent();
        if (content == nullptr) return;
        const auto target = content->getLocalArea (&focused, focused.getLocalBounds());
        const auto view = getViewArea();
        const auto offset = [] (int start, int size, int viewStart, int viewSize)
        {
            if (start < viewStart || size > viewSize) return start;
            if (start + size > viewStart + viewSize) return start + size - viewSize;
            return viewStart;
        };
        setViewPosition (offset (target.getX(), target.getWidth(), view.getX(), view.getWidth()),
                         offset (target.getY(), target.getHeight(), view.getY(), view.getHeight()));
    }
};

class LayoutFocusTraverser final : public juce::KeyboardFocusTraverser
{
public:
    std::vector<juce::Component*> getAllComponents (juce::Component* parent) override
    {
        auto components = juce::KeyboardFocusTraverser::getAllComponents (parent);
        const auto order = [parent] (const juce::Component* component)
        {
            for (auto* current = component; current != nullptr && current != parent;
                 current = current->getParentComponent())
                if (current->getProperties().contains (SkinComponentBindings::componentIdProperty))
                {
                    const auto declared = current->getExplicitFocusOrder();
                    return declared > 0 ? declared : std::numeric_limits<int>::max();
                }
            return std::numeric_limits<int>::max();
        };
        // Keep JUCE's order inside each public composite control.
        std::stable_sort (components.begin(), components.end(), [&] (auto* first, auto* second)
        {
            return order (first) < order (second);
        });
        return components;
    }

    juce::Component* getDefaultComponent (juce::Component* parent) override
    {
        const auto components = getAllComponents (parent);
        return components.empty() ? nullptr : components.front();
    }

    juce::Component* getNextComponent (juce::Component* current) override
    {
        const auto components = getAllComponents (current->findKeyboardFocusContainer());
        const auto found = std::find (components.begin(), components.end(), current);
        return found != components.end() && std::next (found) != components.end()
                   ? *std::next (found) : nullptr;
    }

    juce::Component* getPreviousComponent (juce::Component* current) override
    {
        const auto components = getAllComponents (current->findKeyboardFocusContainer());
        const auto found = std::find (components.begin(), components.end(), current);
        return found != components.end() && found != components.begin()
                   ? *std::prev (found) : nullptr;
    }
};

class PaintLayer final : public juce::Component,
                         private juce::Timer
{
public:
    PaintLayer (std::shared_ptr<SkinSession> sessionToUse,
                const SkinRenderStyleEntry& styleToUse,
                float scaleFactorToUse,
                juce::String animationKeyToUse,
                std::shared_ptr<WorkspaceEntryState> workspaceEntryStateToUse)
        : session (std::move (sessionToUse)),
          style (&styleToUse),
          scaleFactor (scaleFactorToUse),
          animationKey (std::move (animationKeyToUse)),
          renderer (styleToUse.assets),
          workspaceEntryState (std::move (workspaceEntryStateToUse))
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& graphics) override
    {
        if (session == nullptr || style == nullptr) return;
        SkinRenderContext context;
        context.bounds = getLocalBounds().toFloat();
        context.contentBounds = context.bounds;
        context.scaleFactor = scaleFactor;
        if (! workspaceEntryChecked)
        {
            workspaceEntryChecked = true;
            if (workspaceEntryState != nullptr
                && workspaceEntryState->consume (animationKey))
            {
                context.entryTrigger = SkinAnimationTrigger::workspaceEnter;
                context.entrySequence = 1;
            }
        }
        renderer.setAssetCache (style->assets);
        renderer.draw (graphics, style->renderer, context, false, *session,
                       &session->animations(), animationKey);
        if (session->animations().isTimerNeededFor (animationKey))
        {
            if (! isTimerRunning()) startTimerHz (60);
        }
        else
        {
            stopTimer();
        }
    }

    [[nodiscard]] bool isAnimationDriveActive() const noexcept
    {
        return isTimerRunning();
    }

private:
    void timerCallback() override
    {
        if (session != nullptr
            && session->animations().isTimerNeededFor (animationKey))
            repaint();
        else
            stopTimer();
    }

    std::shared_ptr<SkinSession> session;
    const SkinRenderStyleEntry* style = nullptr;
    float scaleFactor = 1.0f;
    juce::String animationKey;
    SkinRenderer renderer;
    std::shared_ptr<WorkspaceEntryState> workspaceEntryState;
    bool workspaceEntryChecked = false;
};

juce::Rectangle<int> relativeBounds (juce::Rectangle<float> absolute,
                                     juce::Point<float> parentOrigin)
{
    return absolute.translated (-parentOrigin.x, -parentOrigin.y)
        .getSmallestIntegerContainer();
}

bool sameRectangle (juce::Rectangle<float> first, juce::Rectangle<float> second)
{
    return std::abs (first.getX() - second.getX()) < 0.01f
        && std::abs (first.getY() - second.getY()) < 0.01f
        && std::abs (first.getWidth() - second.getWidth()) < 0.01f
        && std::abs (first.getHeight() - second.getHeight()) < 0.01f;
}

juce::Component* commonAncestor (const std::vector<juce::Component*>& parents)
{
    if (parents.empty() || parents.front() == nullptr) return nullptr;
    std::vector<juce::Component*> candidates;
    for (auto* candidate = parents.front(); candidate != nullptr;
         candidate = candidate->getParentComponent())
        candidates.push_back (candidate);

    for (auto* candidate : candidates)
    {
        const auto containsCandidate = [candidate] (juce::Component* parent)
        {
            for (auto* current = parent; current != nullptr;
                 current = current->getParentComponent())
                if (current == candidate) return true;
            return false;
        };
        if (std::all_of (parents.begin(), parents.end(), containsCandidate))
            return candidate;
    }
    return nullptr;
}
} // namespace

class SkinLayoutHost::SkinSceneComponent final : public juce::Component
{
public:
    SkinSceneComponent()
    {
        setName ("Declarative skin scene");
        setInterceptsMouseClicks (false, true);
    }

    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override
    {
        return std::make_unique<LayoutFocusTraverser>();
    }
};

struct SkinLayoutHost::OriginalComponentState
{
    juce::Component* parent = nullptr;
    int childIndex = -1;
    juce::Rectangle<int> bounds;
    bool visible = true;
    float alpha = 1.0f;
    int focusOrder = 0;
    bool hadStyle = false;
    juce::var style;
    bool hadScaleFactor = false;
    juce::var scaleFactor;
    bool hadPresentation = false;
    juce::var presentation;
    std::optional<juce::Slider::SliderStyle> sliderStyle;
};

struct SkinLayoutHost::Runtime
{
    struct Placement
    {
        juce::String id;
        juce::Component* component = nullptr;
        juce::Component* wrapper = nullptr;
        juce::String style;
        juce::String presentation;
        float alpha = 1.0f;
        float scaleFactor = 1.0f;
        int focusOrder = 0;
        bool visible = true;
    };

    Runtime (std::shared_ptr<SkinSession> sessionToUse,
             SkinComponentBindings& bindingsToUse,
             SkinResolvedLayout layoutToUse,
             SkinLayoutEntry entryToUse,
             juce::Rectangle<int> boundsToUse,
             float scaleFactorToUse,
             std::map<juce::String, juce::Point<int>> previousScrollOffsets,
             std::shared_ptr<WorkspaceEntryState> workspaceEntryStateToUse)
        : session (std::move (sessionToUse)),
          bindings (bindingsToUse),
          layout (std::move (layoutToUse)),
          entry (entryToUse),
          bounds (boundsToUse),
          scaleFactor (scaleFactorToUse),
          savedScrollOffsets (std::move (previousScrollOffsets)),
          workspaceEntryState (std::move (workspaceEntryStateToUse)),
          root (std::make_unique<LayoutLayer>())
    {
        root->setBounds (0, 0, bounds.getWidth(), bounds.getHeight());
    }

    ~Runtime()
    {
        for (auto& [id, viewport] : scrollViewports)
        {
            juce::ignoreUnused (id);
            viewport->setViewedComponent (nullptr, false);
        }
    }

    bool prepare (SkinValidationReport& report)
    {
        if (session == nullptr || session->compiledSkin() == nullptr)
        {
            report.addError ("RUNTIME007", {}, {},
                             "Active skin session is unavailable for layout.");
            return false;
        }
        buildNode (layout.root, *root, bounds.getPosition().toFloat(),
                   layout.root.clipBounds, true, 1.0f, report);
        return ! failed;
    }

    std::map<juce::String, juce::Point<int>> scrollOffsets() const
    {
        std::map<juce::String, juce::Point<int>> result;
        for (const auto& [id, viewport] : scrollViewports)
        {
            const auto base = scrollBasePositions.contains (id)
                                ? scrollBasePositions.at (id) : juce::Point<int>();
            result.emplace (id, viewport->getViewPosition() - base);
        }
        return result;
    }

    std::shared_ptr<SkinSession> session;
    SkinComponentBindings& bindings;
    SkinResolvedLayout layout;
    SkinLayoutEntry entry;
    juce::Rectangle<int> bounds;
    float scaleFactor = 1.0f;
    juce::Component* hostParent = nullptr;
    std::map<juce::String, juce::Point<int>> savedScrollOffsets;
    std::map<juce::String, juce::Point<int>> scrollBasePositions;
    std::shared_ptr<WorkspaceEntryState> workspaceEntryState;
    std::unique_ptr<LayoutLayer> root;
    std::vector<std::unique_ptr<juce::Component>> layers;
    std::map<juce::String, std::unique_ptr<juce::Viewport>> scrollViewports;
    std::map<juce::String, std::unique_ptr<juce::Component>> scrollContents;
    std::vector<Placement> placements;
    std::set<juce::String> componentIds;
    bool failed = false;

    [[nodiscard]] bool isAnimationDriveActive() const noexcept
    {
        return std::any_of (layers.begin(), layers.end(), [] (const auto& layer)
        {
            if (const auto* paintLayer = dynamic_cast<const PaintLayer*> (layer.get()))
                return paintLayer->isAnimationDriveActive();
            return false;
        });
    }

private:
    juce::Component* addLayer (std::unique_ptr<juce::Component> layer,
                               juce::Component& parent,
                               juce::Rectangle<int> layerBounds,
                               bool visible,
                               float alpha)
    {
        auto* raw = layer.get();
        raw->setBounds (layerBounds);
        raw->setVisible (visible);
        raw->setAlpha (juce::jlimit (0.0f, 1.0f, alpha));
        parent.addChildComponent (raw);
        layers.push_back (std::move (layer));
        return raw;
    }

    void fail (SkinValidationReport& report, juce::String code,
               const juce::String& pointer, juce::String message)
    {
        if (failed) return;
        failed = true;
        report.addError (std::move (code), "layout", pointer, std::move (message));
    }

    void addSurface (const SkinResolvedNode& node,
                     juce::Component& parent,
                     juce::Point<float> parentOrigin,
                     bool visible,
                     float alpha,
                     SkinValidationReport& report)
    {
        if (node.style.isEmpty()) return;
        const auto skin = session->compiledSkin();
        const auto* style = skin->resolveStyle (SkinRenderTarget::surface,
                                                node.style);
        if (style == nullptr)
        {
            fail (report, "LAYOUT013", node.nodeId,
                  "Resolved container style is unavailable in the active skin.");
            return;
        }
        auto layer = std::make_unique<PaintLayer> (
            session, *style, scaleFactor,
            "layout:" + node.nodeId + ":" + node.style,
            workspaceEntryState);
        static_cast<void> (addLayer (
            std::move (layer), parent, relativeBounds (node.bounds, parentOrigin),
            visible, alpha));
    }

    void addComponent (const SkinResolvedNode& node,
                       juce::Component& parent,
                       juce::Point<float> parentOrigin,
                       bool visible,
                       float alpha,
                       SkinValidationReport& report)
    {
        auto* component = bindings.find (node.componentId);
        if (component == nullptr)
        {
            fail (report, "COMPONENT007", node.componentId,
                  "The active layout references a Component ID with no runtime binding.");
            return;
        }
        if (! componentIds.insert (node.componentId).second)
        {
            fail (report, "COMPONENT007", node.componentId,
                  "The active layout resolves the same Component ID more than once.");
            return;
        }

        const auto* descriptor = PlayerComponentCatalog::find (node.componentId);
        if (descriptor == nullptr)
        {
            fail (report, "COMPONENT001", node.componentId,
                  "The active layout references an unknown Component ID.");
            return;
        }
        auto targetName = descriptor->defaultRendererTarget;
        if (node.presentation.isNotEmpty())
            if (const auto found = descriptor->rendererTargetByPresentation.find (
                    node.presentation);
                found != descriptor->rendererTargetByPresentation.end())
                targetName = found->second;
        const auto target = skinRenderTargetFromName (targetName);
        if (! target || session->compiledSkin()->resolveStyle (*target, node.style) == nullptr)
        {
            fail (report, "LAYOUT013", node.nodeId,
                  "Resolved component style is unavailable in the active skin.");
            return;
        }

        auto wrapper = std::make_unique<LayoutLayer>();
        auto* wrapperPointer = addLayer (
            std::move (wrapper), parent, relativeBounds (node.bounds, parentOrigin),
            visible, 1.0f);
        placements.push_back ({ node.componentId, component, wrapperPointer,
                                node.style, node.presentation,
                                juce::jlimit (0.0f, 1.0f, alpha), scaleFactor,
                                node.focusOrder, visible });
    }

    void addScroll (const SkinResolvedNode& node,
                    juce::Component& parent,
                    juce::Point<float> parentOrigin,
                    bool visible,
                    float alpha,
                    SkinValidationReport& report)
    {
        if (node.children.empty()) return;
        auto viewport = std::make_unique<LayoutViewport> (node.nodeId);
        auto content = std::make_unique<LayoutLayer>();
        auto* viewportPointer = viewport.get();
        auto* contentPointer = content.get();
        viewportPointer->setBounds (relativeBounds (node.scrollViewportBounds,
                                                    parentOrigin));
        viewportPointer->setVisible (visible);
        viewportPointer->setAlpha (1.0f);
        viewportPointer->setScrollBarsShown (node.scrollVertical,
                                             ! node.scrollVertical);
        viewportPointer->setScrollBarThickness (
            juce::roundToInt (layout.scrollBarWidth));
        contentPointer->setSize (
            juce::jmax (1, juce::roundToInt (node.scrollContentBounds.getWidth())),
            juce::jmax (1, juce::roundToInt (node.scrollContentBounds.getHeight())));
        viewportPointer->setViewedComponent (contentPointer, false);
        parent.addChildComponent (viewportPointer);

        const auto base = juce::Point<int> (
            juce::roundToInt (node.scrollViewportBounds.getX()
                              - node.scrollContentBounds.getX()),
            juce::roundToInt (node.scrollViewportBounds.getY()
                              - node.scrollContentBounds.getY()));
        scrollBasePositions.insert_or_assign (node.nodeId, base);
        const auto saved = savedScrollOffsets.contains (node.nodeId)
                               ? savedScrollOffsets.at (node.nodeId)
                               : juce::Point<int>();
        viewportPointer->setViewPosition (base + saved);
        scrollContents.emplace (node.nodeId, std::move (content));
        scrollViewports.emplace (node.nodeId, std::move (viewport));

        const auto viewportClip = node.clipBounds.getIntersection (
            node.scrollViewportBounds);
        buildNode (node.children.front(), *contentPointer,
                   node.scrollContentBounds.getPosition(), viewportClip,
                   visible, alpha, report);
    }

    void buildNode (const SkinResolvedNode& node,
                    juce::Component& parent,
                    juce::Point<float> parentOrigin,
                    juce::Rectangle<float> inheritedClip,
                    bool inheritedVisible,
                    float inheritedAlpha,
                    SkinValidationReport& report)
    {
        if (failed) return;
        const auto visible = inheritedVisible && node.visible;
        const auto alpha = juce::jlimit (0.0f, 1.0f,
                                        inheritedAlpha * node.opacity);

        auto* contentParent = &parent;
        auto contentOrigin = parentOrigin;
        const auto isContainer = node.type != SkinLayoutNodeType::component
                              && node.type != SkinLayoutNodeType::visual
                              && node.type != SkinLayoutNodeType::scroll;
        if (isContainer && ! sameRectangle (node.clipBounds, inheritedClip))
        {
            auto clipLayer = std::make_unique<LayoutLayer>();
            contentParent = addLayer (
                std::move (clipLayer), parent,
                relativeBounds (node.bounds, parentOrigin), visible, 1.0f);
            contentOrigin = node.bounds.getPosition();
        }

        if (node.type == SkinLayoutNodeType::component)
        {
            addComponent (node, *contentParent, contentOrigin,
                          visible, alpha, report);
            return;
        }

        addSurface (node, *contentParent, contentOrigin, visible, alpha, report);
        if (failed || node.type == SkinLayoutNodeType::visual) return;
        if (node.type == SkinLayoutNodeType::scroll)
        {
            addScroll (node, *contentParent, contentOrigin,
                       visible, alpha, report);
            return;
        }
        for (const auto& child : node.children)
            buildNode (child, *contentParent, contentOrigin, node.clipBounds,
                       visible, alpha, report);
    }
};

SkinLayoutHost::SkinLayoutHost (SkinManager& managerToUse,
                                SkinComponentBindings& bindingsToUse)
    : manager (managerToUse), bindings (bindingsToUse),
      scene (std::make_unique<SkinSceneComponent>())
{
}

SkinLayoutHost::~SkinLayoutHost()
{
    clear();
}

void SkinLayoutHost::captureOriginalStates()
{
    for (const auto& id : bindings.ids())
    {
        auto* component = bindings.find (id);
        if (component == nullptr || originalStates.contains (component)) continue;
        auto& properties = component->getProperties();
        OriginalComponentState state;
        state.parent = component->getParentComponent();
        state.childIndex = state.parent != nullptr
                             ? state.parent->getIndexOfChildComponent (component) : -1;
        state.bounds = component->getBounds();
        state.visible = component->isVisible();
        state.alpha = component->getAlpha();
        state.focusOrder = component->getExplicitFocusOrder();
        state.hadStyle = properties.contains (JuceSkinAdapter::styleProperty);
        if (state.hadStyle) state.style = properties[JuceSkinAdapter::styleProperty];
        state.hadScaleFactor = properties.contains (JuceSkinAdapter::scaleFactorProperty);
        if (state.hadScaleFactor)
            state.scaleFactor = properties[JuceSkinAdapter::scaleFactorProperty];
        state.hadPresentation = properties.contains (
            JuceSkinAdapter::presentationProperty);
        if (state.hadPresentation)
            state.presentation = properties[JuceSkinAdapter::presentationProperty];
        if (auto* slider = dynamic_cast<juce::Slider*> (component))
            state.sliderStyle = slider->getSliderStyle();
        originalStates.emplace (component, std::move (state));
    }
}

void SkinLayoutHost::restoreOriginalStates (const Runtime* next)
{
    for (const auto& [component, state] : originalStates)
    {
        juce::ignoreUnused (state);
        if (component->getParentComponent() != nullptr)
            component->getParentComponent()->removeChildComponent (component);
    }

    std::vector<std::pair<juce::Component*, const OriginalComponentState*>> ordered;
    ordered.reserve (originalStates.size());
    for (const auto& [component, state] : originalStates)
        ordered.push_back ({ component, &state });
    std::stable_sort (ordered.begin(), ordered.end(), [] (const auto& first,
                                                           const auto& second)
    {
        if (first.second->parent != second.second->parent)
            return std::less<juce::Component*>() (first.second->parent,
                                                   second.second->parent);
        return first.second->childIndex < second.second->childIndex;
    });

    for (const auto& [component, state] : ordered)
    {
        if (state->parent != nullptr)
            state->parent->addChildComponent (component, state->childIndex);
        bool keepsPresentation = false;
        auto* table = dynamic_cast<SkinTableListBox*> (component);
        if (table != nullptr)
        {
            keepsPresentation = next != nullptr
                && std::any_of (next->placements.begin(), next->placements.end(),
                                [component] (const auto& placement)
                                {
                                    return placement.component == component;
                                });
        }
        if (! keepsPresentation)
        {
            component->setBounds (state->bounds);
            if (table != nullptr) table->resetPresentation();
        }
        component->setAlpha (state->alpha);
        component->setExplicitFocusOrder (state->focusOrder);
        auto& properties = component->getProperties();
        if (state->hadStyle)
            properties.set (JuceSkinAdapter::styleProperty, state->style);
        else
            properties.remove (JuceSkinAdapter::styleProperty);
        if (state->hadScaleFactor)
            properties.set (JuceSkinAdapter::scaleFactorProperty,
                            state->scaleFactor);
        else
            properties.remove (JuceSkinAdapter::scaleFactorProperty);
        if (state->hadPresentation)
            properties.set (JuceSkinAdapter::presentationProperty,
                            state->presentation);
        else
            properties.remove (JuceSkinAdapter::presentationProperty);
        if (state->sliderStyle)
            if (auto* slider = dynamic_cast<juce::Slider*> (component))
                slider->setSliderStyle (*state->sliderStyle);
        component->setVisible (state->visible);
    }
}

void SkinLayoutHost::detachRuntime (Runtime& value)
{
    for (const auto& placement : value.placements)
        if (placement.component != nullptr
            && placement.component->getParentComponent() != nullptr)
            placement.component->getParentComponent()->removeChildComponent (
                placement.component);
    if (value.root->getParentComponent() != nullptr)
        value.root->getParentComponent()->removeChildComponent (value.root.get());
}

bool SkinLayoutHost::attachRuntime (Runtime& value,
                                    SkinValidationReport& report)
{
    try
    {
        if (value.hostParent == nullptr)
        {
            report.addError ("COMPONENT007", "layout", {},
                             "Bound components do not share a host parent.");
            return false;
        }
        if (scene->getParentComponent() != value.hostParent)
        {
            if (scene->getParentComponent() != nullptr)
                scene->getParentComponent()->removeChildComponent (scene.get());
            value.hostParent->addAndMakeVisible (*scene);
        }
        scene->setBounds (value.bounds);
        value.root->setBounds (0, 0, value.bounds.getWidth(), value.bounds.getHeight());
        scene->addAndMakeVisible (*value.root);

        restoreOriginalStates (&value);
        for (const auto& id : bindings.ids())
            if (auto* component = bindings.find (id)) component->setVisible (false);

        for (const auto& placement : value.placements)
        {
            if (placement.component == nullptr || placement.wrapper == nullptr)
                throw std::runtime_error ("invalid component placement");
            placement.wrapper->addAndMakeVisible (*placement.component);
            placement.component->setAlpha (placement.alpha);
            placement.component->setExplicitFocusOrder (placement.focusOrder);
            auto& properties = placement.component->getProperties();
            if (placement.style.isNotEmpty())
                properties.set (JuceSkinAdapter::styleProperty, placement.style);
            else
                properties.remove (JuceSkinAdapter::styleProperty);
            properties.set (JuceSkinAdapter::scaleFactorProperty,
                            placement.scaleFactor);
            auto* table = dynamic_cast<SkinTableListBox*> (placement.component);
            const auto presentation = table != nullptr && placement.presentation.isEmpty()
                ? juce::String ("table") : placement.presentation;
            if (presentation.isNotEmpty())
                properties.set (JuceSkinAdapter::presentationProperty,
                                presentation);
            else
                properties.remove (JuceSkinAdapter::presentationProperty);
            if (table != nullptr) table->setPresentation (presentation);
            placement.component->setBounds (placement.wrapper->getLocalBounds());
            if (auto* slider = dynamic_cast<juce::Slider*> (placement.component))
            {
                if (placement.presentation == "rotary")
                    slider->setSliderStyle (
                        juce::Slider::RotaryHorizontalVerticalDrag);
                else
                    slider->setSliderStyle (juce::Slider::LinearHorizontal);
            }
            if (auto* editor = dynamic_cast<juce::TextEditor*> (
                    placement.component))
                if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (
                        &editor->getLookAndFeel()))
                    adapter->configureTextEditor (*editor);
            placement.component->resized();
            if (auto* list = dynamic_cast<juce::ListBox*> (placement.component))
                if (auto* adapter = dynamic_cast<JuceSkinAdapter*> (&list->getLookAndFeel()))
                    if (const auto* descriptor = PlayerComponentCatalog::find (placement.id))
                        if (const auto target = skinRenderTargetFromName (descriptor->defaultRendererTarget))
                            adapter->configureListContentClip (*list, *target);
            placement.component->setVisible (placement.visible);
        }
        for (const auto& [id, viewport] : value.scrollViewports)
        {
            juce::ignoreUnused (id);
            static_cast<LayoutViewport*> (viewport.get())->watchNestedViewports();
        }
        value.root->toFront (false);
        return true;
    }
    catch (...)
    {
        report.addError ("RUNTIME007", "layout", {},
                         "The resolved component tree could not be committed atomically.");
        return false;
    }
}

bool SkinLayoutHost::apply (SkinLayoutEntry entry,
                            juce::Rectangle<int> bounds,
                            float scaleFactor,
                            SkinValidationReport& report,
                            std::optional<WorkspaceMode> workspace, SkinLayoutState state)
{
    const auto session = manager.activeSession();
    const auto skin = session != nullptr ? session->compiledSkin() : nullptr;
    if (skin == nullptr || ! skin->layouts.contains (entry))
    {
        report.addError ("RUNTIME007", "layout", {},
                         "The active skin does not provide the requested layout.");
        return false;
    }

    auto resolved = SkinLayoutEngine::resolve (
        skin->layouts.at (entry), bounds.toFloat(), scaleFactor, report, workspace, skin.get(), state);
    if (! resolved) return false;
    captureOriginalStates();

    std::map<juce::String, juce::Point<int>> scrollOffsets;
    if (runtime) scrollOffsets = runtime->scrollOffsets();
    auto workspaceEntryState = runtime != nullptr
                                    && runtime->entry == entry
                                    && runtime->session == session
                                 ? runtime->workspaceEntryState
                                 : std::make_shared<WorkspaceEntryState>();
    std::unique_ptr<Runtime> prepared;
    try
    {
        prepared = std::make_unique<Runtime> (
            session, bindings, std::move (*resolved), entry, bounds,
            scaleFactor, std::move (scrollOffsets),
            std::move (workspaceEntryState));
        if (! prepared->prepare (report)) return false;
    }
    catch (...)
    {
        report.addError ("RUNTIME007", "layout", {},
                         "The resolved component tree could not be prepared safely.");
        return false;
    }

    std::vector<juce::Component*> parents;
    for (const auto& placement : prepared->placements)
        if (const auto found = originalStates.find (placement.component);
            found != originalStates.end())
            parents.push_back (found->second.parent);
    prepared->hostParent = commonAncestor (parents);
    if (prepared->hostParent == nullptr)
    {
        report.addError ("COMPONENT007", "layout", {},
                         "All components in a layout must share a component-tree ancestor.");
        return false;
    }

    const juce::Component::SafePointer<juce::Component> focused (
        scene->isParentOf (juce::Component::getCurrentlyFocusedComponent())
            ? juce::Component::getCurrentlyFocusedComponent() : nullptr);
    const auto restoreFocus = [&]
    {
        if (focused != nullptr && scene->isParentOf (focused.getComponent())
            && focused->isShowing() && focused->isEnabled())
            focused->grabKeyboardFocus();
    };
    auto previous = std::move (runtime);
    if (previous) detachRuntime (*previous);
    if (attachRuntime (*prepared, report))
    {
        runtime = std::move (prepared);
        restoreFocus();
        return true;
    }

    detachRuntime (*prepared);
    if (previous != nullptr)
    {
        SkinValidationReport rollbackReport;
        if (attachRuntime (*previous, rollbackReport))
            runtime = std::move (previous);
        else
        {
            restoreOriginalStates();
            if (scene->getParentComponent() != nullptr)
                scene->getParentComponent()->removeChildComponent (scene.get());
        }
    }
    else
    {
        restoreOriginalStates();
        if (scene->getParentComponent() != nullptr)
            scene->getParentComponent()->removeChildComponent (scene.get());
    }
    restoreFocus();
    return false;
}

std::unique_ptr<juce::ComponentTraverser> SkinLayoutHost::createKeyboardFocusTraverser() const
{
    return scene->createKeyboardFocusTraverser();
}

juce::String SkinLayoutHost::ancillaryStyleFor (SkinRenderTarget target) const
{
    if (runtime == nullptr) return {};
    const auto& styles = runtime->layout.ancillaryStyles;
    const auto found = styles.find (skinRenderTargetName (target));
    return found != styles.end() ? found->second : juce::String{};
}

void SkinLayoutHost::clear()
{
    if (runtime) detachRuntime (*runtime);
    restoreOriginalStates();
    runtime.reset();
    if (scene != nullptr && scene->getParentComponent() != nullptr)
        scene->getParentComponent()->removeChildComponent (scene.get());
}

bool SkinLayoutHost::isAnimationDriveActive() const noexcept
{
    return runtime != nullptr && runtime->isAnimationDriveActive();
}
