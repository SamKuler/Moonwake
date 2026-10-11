#pragma once

#include "NowPlayingBridge.h"
#include "PlayerViewState.h"

#include <memory>
#include <optional>

class NowPlayingCoordinator
{
public:
    explicit NowPlayingCoordinator (std::unique_ptr<NowPlayingBridge>);

    void setCommandHandler (NowPlayingBridge::CommandHandler);
    void setNativeWindowHandle (void*);
    [[nodiscard]] juce::String getLastError() const;
    void update (const PlayerViewState&, double monotonicSeconds,
                 bool acceptDeferredCommands = false);
    void clear();

private:
    std::unique_ptr<NowPlayingBridge> bridge;
    std::optional<NowPlayingSnapshot> lastPublished;
    double lastPublishedAt = 0.0;
    void* nativeWindowHandle = nullptr;

    [[nodiscard]] static NowPlayingSnapshot project (const PlayerViewState&, bool acceptDeferredCommands);
    [[nodiscard]] bool shouldPublish (const NowPlayingSnapshot&,
                                      double monotonicSeconds) const;
};
