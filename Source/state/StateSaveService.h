#pragma once

#include "state/AppStateStore.h"

#include <atomic>
#include <functional>
#include <memory>

class StateSaveService final : private juce::Thread
{
public:
    using CompletionCallback = std::function<void (bool, juce::String)>;
    using Writer = std::function<bool (const PlayerAppState&,
                                       const juce::File&,
                                       juce::String&)>;

    StateSaveService();
    explicit StateSaveService (Writer);
    ~StateSaveService() override;

    bool start (PlayerAppState,
                juce::File targetFile,
                CompletionCallback);
    [[nodiscard]] bool isBusy() const noexcept;
    bool waitForPendingWrite (int timeoutMilliseconds = -1);

private:
    void run() override;

    Writer writer;
    std::unique_ptr<PlayerAppState> pendingState;
    juce::File pendingTarget;
    CompletionCallback pendingCompletion;
    std::shared_ptr<std::atomic_bool> completionPending {
        std::make_shared<std::atomic_bool> (false)
    };
};
