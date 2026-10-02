#pragma once

#include "state/AppStateStore.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

struct StateRestoreResult
{
    std::optional<PlayerAppState> state;
    std::unique_ptr<juce::XmlElement> pluginCache;
    juce::String errorMessage;
    juce::StringArray warnings;
    bool cancelled = false;
};

class StateRestoreService final : private juce::Thread
{
public:
    using CompletionCallback = std::function<void (StateRestoreResult)>;
    using Preparer = std::function<StateRestoreResult (
        const juce::File&, const juce::File&, const std::atomic_bool*)>;

    explicit StateRestoreService (juce::File artworkCacheRoot);
    explicit StateRestoreService (Preparer);
    ~StateRestoreService() override;

    bool start (juce::File stateFile, juce::File pluginListFile,
                CompletionCallback);
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;
    bool waitForPendingRestore (int timeoutMilliseconds = -1);

    [[nodiscard]] static StateRestoreResult prepare (
        const juce::File& stateFile,
        const juce::File& pluginListFile,
        const juce::File& artworkCacheRoot,
        const std::atomic_bool* cancelled = nullptr);

private:
    struct Lifecycle
    {
        std::atomic_bool alive { true };
        std::atomic_bool cancelled { false };
        std::atomic_bool pending { false };
    };

    void run() override;

    Preparer preparer;
    juce::File pendingStateFile;
    juce::File pendingPluginListFile;
    CompletionCallback pendingCompletion;
    std::shared_ptr<Lifecycle> lifecycle { std::make_shared<Lifecycle>() };
};
