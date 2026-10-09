#include "StateRestoreService.h"

#include "library/LibraryRestore.h"
#include "state/ListeningPresetStore.h"

#include <exception>
#include <utility>

StateRestoreService::StateRestoreService (juce::File artworkCacheRoot)
    : StateRestoreService (
          [cacheRoot = std::move (artworkCacheRoot)] (
              const juce::File& stateFile, const juce::File& pluginListFile,
              const std::atomic_bool* cancelled)
          {
              return prepare (stateFile, pluginListFile,
                              cacheRoot, cancelled);
          })
{
}

StateRestoreService::StateRestoreService (Preparer preparerToUse)
    : juce::Thread ("Moonwake session restore"),
      preparer (std::move (preparerToUse))
{
}

StateRestoreService::~StateRestoreService()
{
    lifecycle->alive.store (false);
    cancel();
    static_cast<void> (waitForPendingRestore());
}

bool StateRestoreService::start (juce::File stateFile,
                                 juce::File pluginListFile,
                                 CompletionCallback completion)
{
    if (stateFile == juce::File() || ! completion || ! preparer
        || lifecycle->pending.load())
        return false;
    if (isThreadRunning() && ! waitForThreadToExit (1000))
        return false;

    bool expected = false;
    if (! lifecycle->pending.compare_exchange_strong (expected, true))
        return false;

    pendingStateFile = std::move (stateFile);
    pendingPluginListFile = std::move (pluginListFile);
    pendingCompletion = std::move (completion);
    lifecycle->cancelled.store (false);
    if (! startThread())
    {
        pendingCompletion = {};
        lifecycle->pending.store (false);
        return false;
    }
    return true;
}

void StateRestoreService::cancel()
{
    lifecycle->cancelled.store (true);
}

bool StateRestoreService::isRunning() const noexcept
{
    return lifecycle->pending.load();
}

bool StateRestoreService::waitForPendingRestore (int timeoutMilliseconds)
{
    return ! isThreadRunning() || waitForThreadToExit (timeoutMilliseconds);
}

StateRestoreResult StateRestoreService::prepare (
    const juce::File& stateFile, const juce::File& pluginListFile,
    const juce::File& artworkCacheRoot, const std::atomic_bool* cancelled)
{
    StateRestoreResult result;
    const auto isCancelled = [cancelled]
    {
        return cancelled != nullptr && cancelled->load();
    };
    if (isCancelled())
    {
        result.cancelled = true;
        return result;
    }

    result.presetFile = stateFile.getSiblingFile ("presets.xml");
    if (result.presetFile.existsAsFile() || ListeningPresetStore::backupFileFor (result.presetFile).existsAsFile())
    {
        ListeningPresetLibrary library;
        juce::String message;
        if (ListeningPresetStore::readFromFile (result.presetFile, library, message))
            result.presets = std::move (library);
        if (message.isNotEmpty()) result.warnings.add (message);
    }
    else
        result.presets.emplace();

    if (pluginListFile.existsAsFile())
    {
        result.pluginCache = juce::XmlDocument::parse (pluginListFile);
        if (result.pluginCache == nullptr
            || ! result.pluginCache->hasTagName ("KNOWNPLUGINS"))
        {
            result.pluginCache.reset();
            result.warnings.add ("Plug-in cache could not be restored: "
                                 + pluginListFile.getFullPathName());
        }
    }

    if (! isCancelled() && stateFile.existsAsFile())
    {
        PlayerAppState state;
        if (AppStateStore::readFromFile (stateFile, state, result.errorMessage)
            && prepareLibraryRestore (state.library, state.playlist, artworkCacheRoot,
                                      cancelled, result.warnings, state.musicDirectories))
            result.state = std::move (state);
    }
    if (isCancelled())
    {
        result.cancelled = true;
        result.state.reset();
        result.presets.reset();
        result.pluginCache.reset();
    }
    return result;
}

void StateRestoreService::run()
{
    auto result = std::make_shared<StateRestoreResult>();
    try
    {
        *result = preparer (pendingStateFile, pendingPluginListFile,
                            &lifecycle->cancelled);
    }
    catch (const std::exception& error)
    {
        result->errorMessage = "Could not restore the saved session: "
                             + juce::String (error.what());
    }
    catch (...)
    {
        result->errorMessage = "Could not restore the saved session: unknown error.";
    }

    const auto delivery = lifecycle;
    auto callback = std::move (pendingCompletion);
    if (! juce::MessageManager::callAsync (
            [delivery, completion = std::move (callback), result] () mutable
            {
                if (! delivery->alive.load())
                    return;
                if (delivery->cancelled.load())
                {
                    result->cancelled = true;
                    result->state.reset();
                    result->presets.reset();
                    result->pluginCache.reset();
                }
                completion (std::move (*result));
                delivery->pending.store (false);
            }))
        delivery->pending.store (false);
}
