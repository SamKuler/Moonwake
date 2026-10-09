#pragma once

#include "skin/SkinExpression.h"

#include <optional>
#include <string_view>

namespace SettingsSkinContext
{
inline std::optional<SkinExpressionValue> directoryListValue (
    std::string_view path, int index, const juce::String& id,
    const juce::String& name, const juce::String& folder, bool automatic)
{
    if (path == "directory.index") return SkinExpressionValue (static_cast<double> (index));
    if (path == "directory.id") return SkinExpressionValue (id);
    if (path == "directory.name") return SkinExpressionValue (name);
    if (path == "directory.path") return SkinExpressionValue (folder);
    if (path == "directory.automatic") return SkinExpressionValue (automatic);
    return std::nullopt;
}

inline std::optional<SkinExpressionValue> channelMapValue (
    std::string_view path, int index,
    const juce::String& sourceLabel, const juce::String& deviceLabel,
    bool connected)
{
    if (path == "channel.index")
        return SkinExpressionValue (static_cast<double> (index));
    if (path == "channel.sourceLabel") return SkinExpressionValue (sourceLabel);
    if (path == "channel.deviceLabel") return SkinExpressionValue (deviceLabel);
    if (path == "channel.connected") return SkinExpressionValue (connected);
    return std::nullopt;
}

inline std::optional<SkinExpressionValue> pluginListValue (
    std::string_view path, int index,
    const juce::String& name, const juce::String& manufacturer,
    const juce::String& format, bool enabled)
{
    if (path == "plugin.index")
        return SkinExpressionValue (static_cast<double> (index));
    if (path == "plugin.name") return SkinExpressionValue (name);
    if (path == "plugin.manufacturer") return SkinExpressionValue (manufacturer);
    if (path == "plugin.format") return SkinExpressionValue (format);
    if (path == "plugin.enabled") return SkinExpressionValue (enabled);
    return std::nullopt;
}

inline std::optional<SkinExpressionValue> skinListValue (
    std::string_view path, int index,
    const juce::String& id, const juce::String& name,
    const juce::String& author, const juce::String& version,
    bool builtIn, bool active, bool previewed)
{
    if (path == "skin.index")
        return SkinExpressionValue (static_cast<double> (index));
    if (path == "skin.id") return SkinExpressionValue (id);
    if (path == "skin.name") return SkinExpressionValue (name);
    if (path == "skin.author") return SkinExpressionValue (author);
    if (path == "skin.version") return SkinExpressionValue (version);
    if (path == "skin.builtin") return SkinExpressionValue (builtIn);
    if (path == "skin.active") return SkinExpressionValue (active);
    if (path == "skin.previewed") return SkinExpressionValue (previewed);
    return std::nullopt;
}
} // namespace SettingsSkinContext
