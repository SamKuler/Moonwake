#include "NowPlayingBridge.h"

namespace
{
class NullNowPlayingBridge final : public NowPlayingBridge
{
public:
    void setCommandHandler (CommandHandler) override {}
    void publish (const NowPlayingSnapshot&) override {}
    void clear() override {}
};
} // namespace

std::unique_ptr<NowPlayingBridge> createSystemNowPlayingBridge()
{
    return std::make_unique<NullNowPlayingBridge>();
}
