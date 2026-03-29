# Authoritative Server

\defgroup authoritative_server Authoritative Server
\brief Dedicated authoritative server runtime, session flow, and fixed-tick match simulation.

This is the gameplay-truth side of the multiplayer architecture.

It owns:

- client admission and release
- lobby and round flow
- authoritative player, bomb, powerup, and tile state
- fixed-tick match simulation
- snapshot, correction, and reliable gameplay event production

The important design boundary:
the **server owns gameplay truth**, while the client only sends intent and presents authoritative results.

## Ownership

The server is responsible for deciding what actually happened in shared gameplay state.

That includes who is connected, which players are accepted into the lobby, when a round starts and ends, how the match advances on each simulation tick, and what authoritative state is replicated back to clients.

This page focuses on that ownership boundary and the high-level server split.
The packet contract itself is explained in [Networking](networking.md).

## State And Session

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="server-state-player-layers-light.svg" alt="Server-side player state layers showing PeerSession, PlayerSlot, MatchPlayerState, and reconnect reclaim">
  <img class="theme-image-dark" src="server-state-player-layers-dark.svg" alt="Server-side player state layers showing PeerSession, PlayerSlot, MatchPlayerState, and reconnect reclaim">
</div>

The server distinguishes transport session state, accepted lobby state, and active in-match state instead of collapsing them into one structure.

## Server Split

| Slice                    | What it owns                                                                | Representative files                                                                                              |
|--------------------------|-----------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| **State and Session**    | Connected peers, accepted seats, active match player state                  | `Server/ServerState.h`, `Server/ServerSession.cpp`                                                                |
| **Receive and Dispatch** | ENet events, packet validation, control/input routing                       | `Server/ServerEvents.cpp`, `Server/ServerHandlers.cpp`, `Server/ServerInputHandlers.cpp`                          |
| **Flow Control**         | Lobby countdown, match bootstrap, round start/end, return to lobby          | `Server/ServerFlow.cpp`, `Server/ServerLobbyFlow.cpp`, `Server/ServerMatchFlow.cpp`                               |
| **Match Simulation**     | Fixed-tick gameplay state, snapshots, corrections, reliable gameplay events | `Server/ServerSimulation.cpp`, `Server/ServerSnapshot.cpp`, `Server/ServerBombs.cpp`, `Server/ServerPowerups.cpp` |

This split keeps the server readable: one layer answers who is connected, one answers what arrived, one answers which phase the server is in, and one answers what happens on each authoritative tick.

## Match Flow

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="server-flow-state-light.svg" alt="Authoritative server flow states from lobby through countdown, bootstrap, active match, and end-of-match return">
  <img class="theme-image-dark" src="server-flow-state-dark.svg" alt="Authoritative server flow states from lobby through countdown, bootstrap, active match, and end-of-match return">
</div>

The server owns the phase machine for lobby readiness, bootstrap, active match, and end-of-round return to lobby.

## Ownership Rules

- the server admits and releases peers
- the server owns lobby state and round phase transitions
- the server advances the authoritative match on fixed ticks
- the server decides authoritative player, bomb, powerup, and tile outcomes
- the server produces snapshots, corrections, and reliable gameplay events for clients

Relevant code:
- `Server/ServerState.h`
- `Server/ServerSession.cpp`
- `Server/ServerFlow.h`
- `Server/ServerLobbyFlow.cpp`
- `Server/ServerMatchFlow.cpp`
- `Server/ServerSimulation.cpp`
- `Server/ServerSnapshot.h`
- `Server/ServerBombs.h`
- `Server/ServerPowerups.h`

<div class="section_buttons">

| Previous                    |                                                             Next |
|:----------------------------|-----------------------------------------------------------------:|
| [Networking](networking.md) | <a href="group__net__client.html">Client Multiplayer Netcode</a> |

</div>