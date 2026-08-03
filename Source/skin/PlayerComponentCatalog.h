#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace juce { class Path; }

enum class SkinComponentKind
{
    label, textButton, toggleButton, comboBox, textEditor, linearSlider,
    slider, progressBar, table, artwork, effectChain, outputStage,
    channelMap, pluginList, skinList
};

enum class SkinLayoutEntry { studio, listening, effects, settings };

struct SkinComponentDescriptor
{
    struct Slot
    {
        juce::String name;
        juce::String rendererTarget;
    };

    juce::String id;
    SkinComponentKind kind = SkinComponentKind::label;
    juce::String defaultRendererTarget;
    std::map<juce::String, juce::String> rendererTargetByPresentation;
    std::vector<Slot> slots;
    std::set<SkinLayoutEntry> requiredIn;
    bool interactive = false;
    juce::StringArray presentations;
    juce::String textAlignment { "left" };
    juce::String iconName;
    juce::String iconPresentation;
};

struct SkinComponentDimensions
{
    float minimumWidth = 0.0f, minimumHeight = 0.0f;
    float preferredWidth = 0.0f, preferredHeight = 0.0f;
};

class PlayerComponentCatalog
{
public:
    [[nodiscard]] static const std::vector<SkinComponentDescriptor>& descriptors();
    [[nodiscard]] static const SkinComponentDescriptor* find (const juce::String& id);
    [[nodiscard]] static juce::String fallbackIconPathData (juce::StringRef iconName);
    [[nodiscard]] static std::shared_ptr<const juce::Path> fallbackIconPath (
        juce::StringRef iconName);
    [[nodiscard]] static SkinComponentDimensions dimensionsForTarget (juce::StringRef target);
};
