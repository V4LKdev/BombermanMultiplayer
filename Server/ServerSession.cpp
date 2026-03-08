#include "ServerSession.h"

namespace bomberman::server
{
    void simulateServerTick(ServerState& state)
    {
        ++state.serverTick;
    }

} // namespace bomberman::server
