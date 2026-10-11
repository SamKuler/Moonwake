#pragma once

#include <juce_core/juce_core.h>

#include <filesystem>

namespace nativeFilesystem
{
[[nodiscard]] inline std::filesystem::path toNativePath (const juce::File& file)
{
#if JUCE_WINDOWS
    return std::filesystem::path (file.getFullPathName().toWideCharPointer());
#else
    return std::filesystem::path (file.getFullPathName().toRawUTF8());
#endif
}

[[nodiscard]] inline juce::File fromNativePath (const std::filesystem::path& path)
{
#if JUCE_WINDOWS
    return juce::File (juce::String (path.c_str()));
#else
    return juce::File (juce::String::fromUTF8 (path.c_str()));
#endif
}
} // namespace nativeFilesystem
