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
    void update (const PlayerViewState&, double monotonicSeconds);
    void clear();

private:
    std::unique_ptr<NowPlayingBridge> bridge;
    std::optional<NowPlayingSnapshot> lastPublished;
    double lastPublishedAt = 0.0;

    [[nodiscard]] static NowPlayingSnapshot project (const PlayerViewState&);
    [[nodiscard]] bool shouldPublish (const NowPlayingSnapshot&,
                                      double monotonicSeconds) const;
};
