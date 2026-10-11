#pragma once

#include <juce_core/juce_core.h>

namespace WindowsApplicationIdentity
{
// Call on the message thread before creating the application's native windows.
juce::Result initialise();

// Explicit destinations also let native integration tests use their own identity
// without replacing the user's Moonwake shortcut.
juce::Result configure (const juce::String& appId, const juce::String& displayName,
                        const juce::File& executable, const juce::File& shortcut);
}
