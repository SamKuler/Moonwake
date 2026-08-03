#pragma once

#include <juce_core/juce_core.h>

enum class WorkspaceMode
{
    studio,
    listening
};

inline juce::String toString (WorkspaceMode mode)
{
    return mode == WorkspaceMode::studio ? "studio" : "listening";
}

inline WorkspaceMode workspaceModeFromString (const juce::String& value)
{
    return value == "studio" ? WorkspaceMode::studio : WorkspaceMode::listening;
}
