#pragma once

#include <juce_core/juce_core.h>

namespace ProductPaths
{
[[nodiscard]] juce::File applicationDataRoot();
[[nodiscard]] juce::File installedSkinsRoot();
}
