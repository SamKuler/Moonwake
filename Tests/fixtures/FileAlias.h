#pragma once

#include <juce_core/juce_core.h>
#include "platform/FilesystemPath.h"

// These tests need two paths identifying the same physical file. Windows
// symbolic links require privileges that ordinary test runs may not have.
inline bool createTestFileAlias (const juce::File& target, const juce::File& alias)
{
   #if JUCE_WINDOWS
    std::error_code error;
    std::filesystem::create_hard_link (nativeFilesystem::toNativePath (target),
                                       nativeFilesystem::toNativePath (alias), error);
    return ! error;
   #else
    return target.createSymbolicLink (alias, true);
   #endif
}

inline bool createTestDirectoryAlias (const juce::File& target, const juce::File& alias)
{
   #if JUCE_WINDOWS
    // Directory junctions exercise canonical-path aliasing without requiring
    // administrator rights or changing Developer Mode on the test machine.
    juce::ChildProcess process;
    return process.start (juce::StringArray { "cmd.exe", "/c", "mklink", "/J",
                                             alias.getFullPathName(), target.getFullPathName() })
        && process.waitForProcessToFinish (5000) && process.getExitCode() == 0
        && alias.isDirectory();
   #else
    return target.createSymbolicLink (alias, false);
   #endif
}
