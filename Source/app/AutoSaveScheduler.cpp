#include "AutoSaveScheduler.h"

#include <algorithm>

bool AutoSaveScheduler::observe (uint64_t revision, double nowSeconds)
{
    nowSeconds = std::max (0.0, nowSeconds);
    if (lastObservedTime >= 0.0 && nowSeconds < lastObservedTime)
    {
        firstDirtyAt = revision == savedRevision ? -1.0 : nowSeconds;
        lastChangeAt = firstDirtyAt;
        retryAt = -1.0;
        saveInFlight = false;
    }
    lastObservedTime = nowSeconds;

    if (revision != observedRevision)
    {
        observedRevision = revision;
        lastChangeAt = nowSeconds;
        if (observedRevision == savedRevision)
            firstDirtyAt = -1.0;
        else if (firstDirtyAt < 0.0)
            firstDirtyAt = nowSeconds;
    }
    if (saveInFlight)
        return false;
    if (retryAt >= 0.0)
    {
        if (nowSeconds < retryAt)
            return false;
        retryAt = -1.0;
        saveInFlight = true;
        return true;
    }
    if (observedRevision != savedRevision
        && lastChangeAt >= 0.0
        && (nowSeconds - lastChangeAt >= debounceSeconds
            || (firstDirtyAt >= 0.0
                && nowSeconds - firstDirtyAt >= periodicSeconds)))
    {
        saveInFlight = true;
        return true;
    }
    return false;
}

void AutoSaveScheduler::acknowledge (uint64_t revision,
                                     double nowSeconds,
                                     bool success)
{
    nowSeconds = std::max (0.0, nowSeconds);
    saveInFlight = false;
    if (success)
    {
        savedRevision = revision;
        retryAt = -1.0;
        if (savedRevision == observedRevision)
        {
            firstDirtyAt = -1.0;
            lastChangeAt = -1.0;
        }
    }
    else
    {
        retryAt = nowSeconds + retrySeconds;
    }
}
