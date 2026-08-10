#include "StateSaveService.h"

#include <utility>

StateSaveService::StateSaveService()
    : StateSaveService ([] (const PlayerAppState& state,
                            const juce::File& target,
                            juce::String& error)
      {
          return AppStateStore::writeAtomically (state, target, error);
      })
{
}

StateSaveService::StateSaveService (Writer writerToUse)
    : juce::Thread ("Moonwake state writer"),
      writer (std::move (writerToUse))
{
}

StateSaveService::~StateSaveService()
{
    static_cast<void> (waitForPendingWrite());
}

bool StateSaveService::start (PlayerAppState state,
                              juce::File targetFile,
                              CompletionCallback completion)
{
    if (targetFile == juce::File() || ! completion || ! writer)
        return false;

    if (completionPending->load())
        return false;

    if (isThreadRunning() && ! waitForThreadToExit (1000))
        return false;

    bool expected = false;
    if (! completionPending->compare_exchange_strong (expected, true))
        return false;

    pendingState = std::make_unique<PlayerAppState> (std::move (state));
    pendingTarget = std::move (targetFile);
    pendingCompletion = std::move (completion);
    if (! startThread())
    {
        pendingState.reset();
        pendingTarget = juce::File{};
        pendingCompletion = {};
        completionPending->store (false);
        return false;
    }
    return true;
}

bool StateSaveService::isBusy() const noexcept
{
    return completionPending->load();
}

bool StateSaveService::waitForPendingWrite (int timeoutMilliseconds)
{
    return ! isThreadRunning() || waitForThreadToExit (timeoutMilliseconds);
}

void StateSaveService::run()
{
    auto state = std::move (pendingState);
    auto target = std::move (pendingTarget);
    auto completionToDeliver = std::move (pendingCompletion);
    juce::String writeError;
    const auto success = state != nullptr && writer (*state, target, writeError);
    const auto lifecycle = completionPending;
    juce::MessageManager::callAsync (
        [lifecycle, callback = std::move (completionToDeliver), success,
         errorMessage = std::move (writeError)] () mutable
        {
            if (callback)
                callback (success, std::move (errorMessage));
            lifecycle->store (false);
        });
}
