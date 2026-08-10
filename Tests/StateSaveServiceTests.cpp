#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "state/StateSaveService.h"

namespace
{
bool dispatchUntil (const std::function<bool()>& condition,
                    int timeoutMilliseconds = 2000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                        + timeoutMilliseconds;
    while (! condition()
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return condition();
}

class StateSaveServiceTests final : public juce::UnitTest
{
public:
    StateSaveServiceTests() : UnitTest ("StateSaveService", "state") {}

    void runTest() override
    {
        beginTest ("writes on a worker and remains busy through completion");
        {
            juce::WaitableEvent writerEntered;
            juce::WaitableEvent allowWriterToFinish;
            std::atomic_bool wroteOffMessageThread { false };
            StateSaveService service (
                [&] (const PlayerAppState&, const juce::File&,
                     juce::String&)
                {
                    wroteOffMessageThread.store (
                        ! juce::MessageManager::getInstance()
                              ->isThisTheMessageThread());
                    writerEntered.signal();
                    allowWriterToFinish.wait (-1);
                    return true;
                });

            bool completionCalled = false;
            bool busyDuringCompletion = false;
            expect (service.start (
                {}, juce::File ("/tmp/moonwake-state-writer-test.xml"),
                [&] (bool success, juce::String error)
                {
                    completionCalled = true;
                    busyDuringCompletion = service.isBusy();
                    expect (success);
                    expect (error.isEmpty());
                }));
            expect (writerEntered.wait (1000));
            expect (service.isBusy());
            expect (wroteOffMessageThread.load());
            expect (! service.start (
                {}, juce::File ("/tmp/moonwake-state-writer-overlap.xml"),
                [] (bool, juce::String) {}));

            allowWriterToFinish.signal();
            expect (service.waitForPendingWrite (1000));
            expect (dispatchUntil ([&] { return completionCalled; }));
            expect (busyDuringCompletion);
            expect (! service.isBusy());
        }

        beginTest ("propagates writer failures without touching the UI thread");
        {
            StateSaveService service (
                [] (const PlayerAppState&, const juce::File&,
                    juce::String& error)
                {
                    error = "fixture write failure";
                    return false;
                });
            bool completed = false;
            expect (service.start (
                {}, juce::File ("/tmp/moonwake-state-writer-failure.xml"),
                [&] (bool success, juce::String error)
                {
                    expect (! success);
                    expectEquals (error, juce::String ("fixture write failure"));
                    completed = true;
                }));
            expect (dispatchUntil ([&] { return completed; }));
        }
    }
};

StateSaveServiceTests stateSaveServiceTests;
} // namespace
