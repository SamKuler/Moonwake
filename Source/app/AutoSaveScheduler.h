#pragma once

#include <cstdint>

class AutoSaveScheduler
{
public:
    [[nodiscard]] bool observe (uint64_t revision, double nowSeconds);
    void acknowledge (uint64_t savedRevision, double nowSeconds, bool success);

private:
    static constexpr double debounceSeconds = 1.0;
    static constexpr double periodicSeconds = 15.0;
    static constexpr double retrySeconds = 5.0;

    uint64_t observedRevision = 0;
    uint64_t savedRevision = 0;
    double firstDirtyAt = -1.0;
    double lastChangeAt = -1.0;
    double retryAt = -1.0;
    double lastObservedTime = -1.0;
    bool saveInFlight = false;
};
