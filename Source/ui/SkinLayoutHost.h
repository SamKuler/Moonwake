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
                SkinValidationReport&);
    void clear();
    [[nodiscard]] bool isAnimationDriveActive() const noexcept;

private:
    class SkinSceneComponent;
    struct OriginalComponentState;
    struct Runtime;

    void captureOriginalStates();
    void restoreOriginalStates();
    bool attachRuntime (Runtime&, SkinValidationReport&);
    void detachRuntime (Runtime&);

    SkinManager& manager;
    SkinComponentBindings& bindings;
    std::unique_ptr<SkinSceneComponent> scene;
    std::map<juce::Component*, OriginalComponentState> originalStates;
    std::unique_ptr<Runtime> runtime;
};
