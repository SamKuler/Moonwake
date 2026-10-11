#pragma once

#include <juce_core/juce_core.h>

// Windows may briefly retain a delete-pending entry while an external reader
// releases it. Only fixture teardown retries; persistent open handles still fail.
inline bool deleteTestDirectory (const juce::File& directory)
{
   #if JUCE_WINDOWS
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    do
    {
        if (directory.deleteRecursively()) return true;
        juce::Thread::sleep (20);
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
   #endif
    return directory.deleteRecursively();
}
