#pragma once

#include "SkinRenderer.h"

#include <functional>
#include <map>
#include <optional>

using SkinAnimationPropertyMap = std::map<juce::String, SkinCompiledRenderValue>;
using SkinAnimationResourceMap =
    std::map<juce::String, std::shared_ptr<const SkinAssetCache>>;

struct SkinAnimationInput
{
    SkinControlState state;
    double normalisedValue = 0.0;
    std::optional<SkinAnimationTrigger> entryTrigger;
    uint64_t entrySequence = 0;
    SkinAnimationPropertyMap entryFromValues;
    SkinAnimationResourceMap entryFromResources;
};

class SkinAnimationEngine final
{
public:
    using Clock = std::function<double()>;

    explicit SkinAnimationEngine (Clock = {});
    ~SkinAnimationEngine();

    [[nodiscard]] SkinAnimationPropertyMap resolve (
        const juce::String& key, const SkinRenderProgram&, const SkinAnimationInput&,
        const SkinAnimationPropertyMap& computedValues);
    [[nodiscard]] SkinAnimationPropertyMap resolve (
        const juce::String& key, const SkinRenderProgram&, const SkinAnimationInput&,
        const SkinAnimationPropertyMap& computedValues,
        const SkinAnimationResourceMap& computedResources,
        SkinAnimationResourceMap& displayedResources);

    void setReduceMotionEnabled (bool enabled);
    [[nodiscard]] bool isReduceMotionEnabled() const noexcept;
    void cancelAll();
    [[nodiscard]] size_t activeTransitionCount() const noexcept;
    [[nodiscard]] bool isTimerNeeded() const noexcept;
    [[nodiscard]] bool isTimerNeededFor (const juce::String& key) const;

    [[nodiscard]] static double applyEasing (SkinAnimationEasing, double progress);
    [[nodiscard]] static SkinCompiledRenderValue interpolateValue (
        const SkinCompiledRenderValue& from, const SkinCompiledRenderValue& to,
        double progress);

    struct TransitionState;
    struct TargetState;

private:

    [[nodiscard]] double now() const;

    Clock clock;
    bool reduceMotion = false;
    std::map<juce::String, TransitionState> active;
    std::map<juce::String, TargetState> targets;
};
