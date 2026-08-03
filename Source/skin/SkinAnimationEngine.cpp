#include "SkinAnimationEngine.h"

#include <algorithm>
#include <cmath>
#include <set>

struct SkinAnimationEngine::TransitionState
{
    SkinCompiledRenderValue from = 0.0;
    SkinCompiledRenderValue to = 0.0;
    double startMs = 0.0;
    int durationMs = 0;
    SkinAnimationEasing easing = SkinAnimationEasing::linear;
    bool interpolable = true;
    bool respectReduceMotion = true;
    std::shared_ptr<const SkinAssetCache> fromResource;
    std::shared_ptr<const SkinAssetCache> toResource;
};

struct SkinAnimationEngine::TargetState
{
    SkinAnimationInput input;
    SkinAnimationPropertyMap values;
    SkinAnimationResourceMap resources;
    bool initialised = false;
};

namespace
{
juce::String activeKey (const juce::String& owner, const juce::String& property)
{
    return owner + juce::String::charToString (0x1f) + property;
}

double srgbToLinear (double value)
{
    value = juce::jlimit (0.0, 1.0, value);
    return value <= 0.04045 ? value / 12.92
                            : std::pow ((value + 0.055) / 1.055, 2.4);
}

double linearToSrgb (double value)
{
    value = juce::jlimit (0.0, 1.0, value);
    return value <= 0.0031308 ? value * 12.92
                              : 1.055 * std::pow (value, 1.0 / 2.4) - 0.055;
}

SkinCompiledRenderValue displayedValue (
    const SkinAnimationEngine::TransitionState& transition, double time)
{
    if (transition.durationMs <= 0 || time >= transition.startMs + transition.durationMs)
        return transition.to;
    if (time <= transition.startMs) return transition.from;
    const auto raw = (time - transition.startMs) / transition.durationMs;
    return SkinAnimationEngine::interpolateValue (
        transition.from, transition.to,
        transition.interpolable
            ? SkinAnimationEngine::applyEasing (transition.easing, raw)
            : (raw >= 1.0 ? 1.0 : 0.0));
}

std::shared_ptr<const SkinAssetCache> displayedResource (
    const SkinAnimationEngine::TransitionState& transition, double time)
{
    return transition.durationMs <= 0
            || time >= transition.startMs + transition.durationMs
        ? transition.toResource : transition.fromResource;
}

bool stateValue (const SkinControlState& state, SkinAnimationTrigger trigger)
{
    switch (trigger)
    {
        case SkinAnimationTrigger::hover: return state.hover;
        case SkinAnimationTrigger::pressed: return state.pressed;
        case SkinAnimationTrigger::selected: return state.selected;
        case SkinAnimationTrigger::focused: return state.focused;
        case SkinAnimationTrigger::disabled: return ! state.enabled;
        case SkinAnimationTrigger::value:
        case SkinAnimationTrigger::workspaceEnter:
        case SkinAnimationTrigger::previewEnter:
            break;
    }
    return false;
}

bool isEntry (SkinAnimationTrigger trigger)
{
    return trigger == SkinAnimationTrigger::workspaceEnter
        || trigger == SkinAnimationTrigger::previewEnter;
}

bool renderValuesEqual (const SkinCompiledRenderValue& first,
                        const SkinCompiledRenderValue& second)
{
    if (first.index() != second.index()) return false;
    if (const auto* value = std::get_if<double> (&first))
        return juce::exactlyEqual (*value, std::get<double> (second));
    if (const auto* value = std::get_if<bool> (&first))
        return *value == std::get<bool> (second);
    if (const auto* value = std::get_if<juce::String> (&first))
        return *value == std::get<juce::String> (second);
    if (const auto* value = std::get_if<juce::Colour> (&first))
        return *value == std::get<juce::Colour> (second);
    if (const auto* value = std::get_if<SkinFontValue> (&first))
    {
        const auto& other = std::get<SkinFontValue> (second);
        return value->source == other.source && value->families == other.families
            && value->weight == other.weight && value->italic == other.italic
            && juce::exactlyEqual (value->tracking, other.tracking);
    }
    const auto& value = std::get<SkinGradientValue> (first);
    const auto& other = std::get<SkinGradientValue> (second);
    if (value.type != other.type || ! juce::exactlyEqual (value.angle, other.angle)
        || ! juce::exactlyEqual (value.centreX, other.centreX)
        || ! juce::exactlyEqual (value.centreY, other.centreY)
        || ! juce::exactlyEqual (value.radius, other.radius)
        || value.stops.size() != other.stops.size())
        return false;
    for (size_t i = 0; i < value.stops.size(); ++i)
        if (! juce::exactlyEqual (value.stops[i].position, other.stops[i].position)
            || value.stops[i].colour != other.stops[i].colour)
            return false;
    return true;
}
} // namespace

SkinAnimationEngine::SkinAnimationEngine (Clock clockToUse)
    : clock (std::move (clockToUse))
{
    if (! clock)
        clock = [] { return juce::Time::getMillisecondCounterHiRes(); };
}

SkinAnimationEngine::~SkinAnimationEngine() = default;

SkinAnimationPropertyMap SkinAnimationEngine::resolve (
    const juce::String& key, const SkinRenderProgram& program,
    const SkinAnimationInput& input, const SkinAnimationPropertyMap& computedValues)
{
    SkinAnimationResourceMap ignoredResources;
    return resolve (key, program, input, computedValues, {}, ignoredResources);
}

SkinAnimationPropertyMap SkinAnimationEngine::resolve (
    const juce::String& key, const SkinRenderProgram& program,
    const SkinAnimationInput& input, const SkinAnimationPropertyMap& computedValues,
    const SkinAnimationResourceMap& computedResources,
    SkinAnimationResourceMap& displayedResources)
{
    const auto time = now();
    auto& previous = targets[key];

    std::vector<SkinAnimationTrigger> triggered;
    if (input.entryTrigger && isEntry (*input.entryTrigger)
        && (! previous.initialised
            || previous.input.entryTrigger != input.entryTrigger
            || previous.input.entrySequence != input.entrySequence))
        triggered.push_back (*input.entryTrigger);
    if (previous.initialised)
    {
        for (const auto trigger : { SkinAnimationTrigger::selected,
                                    SkinAnimationTrigger::hover,
                                    SkinAnimationTrigger::pressed,
                                    SkinAnimationTrigger::focused,
                                    SkinAnimationTrigger::disabled })
            if (stateValue (previous.input.state, trigger)
                != stateValue (input.state, trigger))
                triggered.push_back (trigger);
        if (std::abs (previous.input.normalisedValue - input.normalisedValue) > 1.0e-12)
            triggered.push_back (SkinAnimationTrigger::value);
    }

    const auto currentFor = [&] (const juce::String& property,
                                 const SkinCompiledRenderValue& fallback)
    {
        const auto activeTransition = active.find (activeKey (key, property));
        if (activeTransition != active.end())
            return displayedValue (activeTransition->second, time);
        const auto old = previous.values.find (property);
        return old != previous.values.end() ? old->second : fallback;
    };
    const auto currentResourceFor = [&] (
        const juce::String& property,
        std::shared_ptr<const SkinAssetCache> fallback)
    {
        const auto activeTransition = active.find (activeKey (key, property));
        if (activeTransition != active.end())
            return displayedResource (activeTransition->second, time);
        const auto old = previous.resources.find (property);
        return old != previous.resources.end() ? old->second : std::move (fallback);
    };

    std::set<juce::String> retargetedChannels;
    for (const auto trigger : triggered)
    {
        const auto transitionIt = program.transitions.find (trigger);
        if (transitionIt == program.transitions.end()) continue;
        const auto& transition = transitionIt->second;
        const auto collapse = reduceMotion && transition.respectReduceMotion;
        for (const auto& property : transition.properties)
        {
            const auto target = computedValues.find (property);
            if (target == computedValues.end()) continue;
            auto from = currentFor (property, target->second);
            const auto targetResource = computedResources.find (property);
            auto fromResource = currentResourceFor (
                property, targetResource != computedResources.end()
                              ? targetResource->second : nullptr);
            if (isEntry (trigger))
            {
                const auto explicitFrom = input.entryFromValues.find (property);
                if (explicitFrom != input.entryFromValues.end()) from = explicitFrom->second;
                const auto explicitResource = input.entryFromResources.find (property);
                if (explicitResource != input.entryFromResources.end())
                    fromResource = explicitResource->second;
            }

            const auto channelKey = activeKey (key, property);
            if (collapse || transition.durationMs == 0
                || (active.size() >= 128 && ! active.contains (channelKey)))
            {
                active.erase (channelKey);
                continue;
            }
            bool interpolable = false;
            const auto descriptor = program.propertyDescriptors.find (property);
            if (descriptor != program.propertyDescriptors.end() && descriptor->second != nullptr)
                interpolable = descriptor->second->interpolable;
            else
                interpolable = std::holds_alternative<double> (target->second)
                            || std::holds_alternative<juce::Colour> (target->second);
            active.insert_or_assign (channelKey,
                TransitionState { std::move (from), target->second,
                                  time + transition.delayMs, transition.durationMs,
                                  transition.easing, interpolable,
                                  transition.respectReduceMotion,
                                  std::move (fromResource),
                                  targetResource != computedResources.end()
                                      ? targetResource->second : nullptr });
            retargetedChannels.insert (channelKey);
        }
    }

    const auto prefix = key + juce::String::charToString (0x1f);
    for (auto it = active.begin(); it != active.end();)
    {
        if (! it->first.startsWith (prefix) || retargetedChannels.contains (it->first))
        {
            ++it;
            continue;
        }
        const auto property = it->first.substring (prefix.length());
        const auto target = computedValues.find (property);
        const auto targetResource = computedResources.find (property);
        const auto resource = targetResource != computedResources.end()
            ? targetResource->second : nullptr;
        if (target == computedValues.end()
            || ! renderValuesEqual (it->second.to, target->second)
            || it->second.toResource != resource)
            it = active.erase (it);
        else
            ++it;
    }

    previous.input = input;
    previous.values = computedValues;
    previous.resources = computedResources;
    previous.initialised = true;

    auto displayed = computedValues;
    displayedResources = computedResources;
    std::vector<juce::String> finished;
    for (const auto& [channelKey, transition] : active)
    {
        if (! channelKey.startsWith (prefix)) continue;
        const auto property = channelKey.substring (prefix.length());
        displayed[property] = displayedValue (transition, time);
        if (const auto resource = displayedResource (transition, time))
            displayedResources.insert_or_assign (property, resource);
        else
            displayedResources.erase (property);
        if (time >= transition.startMs + transition.durationMs)
            finished.push_back (channelKey);
    }
    for (const auto& channelKey : finished) active.erase (channelKey);
    return displayed;
}

void SkinAnimationEngine::setReduceMotionEnabled (bool enabled)
{
    reduceMotion = enabled;
    if (enabled)
    {
        for (auto it = active.begin(); it != active.end();)
            if (it->second.respectReduceMotion)
                it = active.erase (it);
            else
                ++it;
    }
}

bool SkinAnimationEngine::isReduceMotionEnabled() const noexcept
{
    return reduceMotion;
}

void SkinAnimationEngine::cancelAll()
{
    active.clear();
    targets.clear();
}

size_t SkinAnimationEngine::activeTransitionCount() const noexcept
{
    return active.size();
}

bool SkinAnimationEngine::isTimerNeeded() const noexcept
{
    return ! active.empty();
}

bool SkinAnimationEngine::isTimerNeededFor (const juce::String& key) const
{
    const auto prefix = key + juce::String::charToString (0x1f);
    const auto nestedPrefix = key + ":";
    return std::any_of (active.begin(), active.end(), [&] (const auto& channel)
    {
        return channel.first.startsWith (prefix)
            || channel.first.startsWith (nestedPrefix);
    });
}

double SkinAnimationEngine::applyEasing (SkinAnimationEasing easing, double progress)
{
    const auto x = juce::jlimit (0.0, 1.0, progress);
    switch (easing)
    {
        case SkinAnimationEasing::linear: return x;
        case SkinAnimationEasing::easeIn: return x * x;
        case SkinAnimationEasing::easeOut: return 1.0 - (1.0 - x) * (1.0 - x);
        case SkinAnimationEasing::easeInOut:
            return x < 0.5 ? 2.0 * x * x
                           : 1.0 - std::pow (-2.0 * x + 2.0, 2.0) / 2.0;
        case SkinAnimationEasing::springGentle:
        {
            if (x <= 0.0 || x >= 1.0) return x;
            const auto value = 1.0 - std::exp (-7.0 * x) * std::cos (10.0 * x);
            return juce::jlimit (0.0, 1.0, value);
        }
    }
    return x;
}

SkinCompiledRenderValue SkinAnimationEngine::interpolateValue (
    const SkinCompiledRenderValue& from, const SkinCompiledRenderValue& to,
    double progress)
{
    const auto x = juce::jlimit (0.0, 1.0, progress);
    if (const auto* fromNumber = std::get_if<double> (&from))
        if (const auto* toNumber = std::get_if<double> (&to))
            return *fromNumber + (*toNumber - *fromNumber) * x;

    if (const auto* fromColour = std::get_if<juce::Colour> (&from))
        if (const auto* toColour = std::get_if<juce::Colour> (&to))
        {
            const auto a0 = static_cast<double> (fromColour->getFloatAlpha());
            const auto a1 = static_cast<double> (toColour->getFloatAlpha());
            const auto alpha = a0 + (a1 - a0) * x;
            std::array<double, 3> components {};
            const std::array<double, 3> fromSrgb {
                fromColour->getFloatRed(), fromColour->getFloatGreen(),
                fromColour->getFloatBlue()
            };
            const std::array<double, 3> toSrgb {
                toColour->getFloatRed(), toColour->getFloatGreen(),
                toColour->getFloatBlue()
            };
            for (size_t i = 0; i < components.size(); ++i)
            {
                const auto premultiplied = srgbToLinear (fromSrgb[i]) * a0 * (1.0 - x)
                                         + srgbToLinear (toSrgb[i]) * a1 * x;
                components[i] = alpha > 0.0 ? linearToSrgb (premultiplied / alpha) : 0.0;
            }
            return juce::Colour::fromFloatRGBA (
                static_cast<float> (components[0]), static_cast<float> (components[1]),
                static_cast<float> (components[2]), static_cast<float> (alpha));
        }

    return x >= 1.0 ? to : from;
}

double SkinAnimationEngine::now() const
{
    return clock();
}
