#include <juce_core/juce_core.h>

#include "app/AutoSaveScheduler.h"

namespace
{
class AutoSaveSchedulerTests final : public juce::UnitTest
{
public:
    AutoSaveSchedulerTests() : UnitTest ("AutoSaveScheduler", "app") {}

    void runTest() override
    {
        beginTest ("debounces revisions and acknowledges successful saves");
        {
            AutoSaveScheduler scheduler;
            expect (! scheduler.observe (0, 0.0));
            expect (! scheduler.observe (1, 0.1));
            expect (! scheduler.observe (2, 0.8));
            expect (! scheduler.observe (2, 1.79));
            expect (scheduler.observe (2, 1.8));
            expect (! scheduler.observe (2, 2.0));
            scheduler.acknowledge (2, 2.0, true);
            expect (! scheduler.observe (2, 2.1));
        }

        beginTest ("retries failures without rewriting clean state");
        {
            AutoSaveScheduler scheduler;
            expect (! scheduler.observe (1, 0.0));
            expect (scheduler.observe (1, 1.0));
            scheduler.acknowledge (1, 1.0, false);
            expect (! scheduler.observe (1, 5.99));
            expect (scheduler.observe (1, 6.0));
            scheduler.acknowledge (1, 6.0, true);
            expect (! scheduler.observe (1, 21.0));
        }

        beginTest ("caps save latency while changes continue");
        {
            AutoSaveScheduler scheduler;
            expect (! scheduler.observe (1, 10.0));
            expect (! scheduler.observe (2, 10.8));
            expect (! scheduler.observe (3, 18.0));
            expect (! scheduler.observe (4, 24.9));
            expect (scheduler.observe (4, 25.0));
        }

        beginTest ("clock rollback resets scheduling safely");
        {
            AutoSaveScheduler scheduler;
            expect (! scheduler.observe (4, 100.0));
            expect (! scheduler.observe (4, 10.0));
            expect (scheduler.observe (4, 11.0));
        }
    }
};

AutoSaveSchedulerTests autoSaveSchedulerTests;
} // namespace
