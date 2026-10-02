#pragma once

#include "skin/SkinComponentBindings.h"
#include "skin/SkinLayoutEngine.h"
#include "skin/SkinManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

class SkinLayoutHost
{
public:
    SkinLayoutHost (SkinManager&, SkinComponentBindings&);
    ~SkinLayoutHost();

    bool apply (SkinLayoutEntry, juce::Rectangle<int> bounds, float scaleFactor,
                SkinValidationReport&, std::optional<WorkspaceMode> workspace = {});
    void clear();
    [[nodiscard]] bool isAnimationDriveActive() const noexcept;
    [[nodiscard]] juce::String ancillaryStyleFor (SkinRenderTarget) const;

private:
    class SkinSceneComponent;
    struct OriginalComponentState;
    struct Runtime;

    void captureOriginalStates();
    void restoreOriginalStates (const Runtime* next = nullptr);
    bool attachRuntime (Runtime&, SkinValidationReport&);
    void detachRuntime (Runtime&);

    SkinManager& manager;
    SkinComponentBindings& bindings;
    std::unique_ptr<SkinSceneComponent> scene;
    std::map<juce::Component*, OriginalComponentState> originalStates;
    std::unique_ptr<Runtime> runtime;
};
