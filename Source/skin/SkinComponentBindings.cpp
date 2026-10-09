#include "SkinComponentBindings.h"

const juce::Identifier SkinComponentBindings::componentIdProperty { "skinComponentId" };
const juce::Identifier SkinComponentBindings::iconNameProperty { "skinIconName" };
const juce::Identifier SkinComponentBindings::iconPresentationProperty { "skinIconPresentation" };

namespace
{
bool matchesJuceType (SkinComponentKind kind, juce::Component& component)
{
    switch (kind)
    {
        case SkinComponentKind::label:
            return dynamic_cast<juce::Label*> (&component) != nullptr;
        case SkinComponentKind::textButton:
            return dynamic_cast<juce::TextButton*> (&component) != nullptr;
        case SkinComponentKind::toggleButton:
            return dynamic_cast<juce::ToggleButton*> (&component) != nullptr;
        case SkinComponentKind::comboBox:
            return dynamic_cast<juce::ComboBox*> (&component) != nullptr;
        case SkinComponentKind::textEditor:
            return dynamic_cast<juce::TextEditor*> (&component) != nullptr;
        case SkinComponentKind::linearSlider:
        case SkinComponentKind::slider:
            return dynamic_cast<juce::Slider*> (&component) != nullptr;
        case SkinComponentKind::progressBar:
            return dynamic_cast<juce::ProgressBar*> (&component) != nullptr;
        case SkinComponentKind::table:
            return dynamic_cast<juce::TableListBox*> (&component) != nullptr;
        case SkinComponentKind::artwork:
        case SkinComponentKind::effectChain:
        case SkinComponentKind::outputStage:
        case SkinComponentKind::channelMixer:
        case SkinComponentKind::channelMap:
        case SkinComponentKind::pluginList:
        case SkinComponentKind::skinList:
        case SkinComponentKind::directoryList:
        case SkinComponentKind::smartPlaylistEditor:
        case SkinComponentKind::smartPlaylistRuleList:
        case SkinComponentKind::playlistGrid:
            return true;
    }
    return false;
}
} // namespace

bool SkinComponentBindings::bind (const juce::String& id, juce::Component& component,
                                  SkinValidationReport& report)
{
    const auto* descriptor = PlayerComponentCatalog::find (id);
    if (descriptor == nullptr)
    {
        report.addError ("COMPONENT001", {}, id,
                         "Runtime binding references an unknown Component ID.");
        return false;
    }
    if (bindings.contains (id))
    {
        report.addError ("COMPONENT007", {}, id,
                         "Runtime Component ID is already bound.");
        return false;
    }
    if (! matchesJuceType (descriptor->kind, component))
    {
        report.addError ("COMPONENT007", {}, id,
                         "JUCE component type does not match the catalog kind.");
        return false;
    }
    bindings.emplace (id, &component);
    component.getProperties().set (componentIdProperty, id);
    component.getProperties().set (iconNameProperty, descriptor->iconName);
    component.getProperties().set (iconPresentationProperty, descriptor->iconPresentation);
    return true;
}

juce::Component* SkinComponentBindings::find (const juce::String& id) const
{
    const auto found = bindings.find (id);
    return found == bindings.end() ? nullptr : found->second;
}

juce::StringArray SkinComponentBindings::ids() const
{
    juce::StringArray result;
    for (const auto& [id, ignored] : bindings)
    {
        juce::ignoreUnused (ignored);
        result.add (id);
    }
    return result;
}
