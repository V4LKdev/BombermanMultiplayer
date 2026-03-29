# Client Multiplayer Netcode

\defgroup net_client Client Multiplayer Netcode
\brief Client-side connection, prediction, correction, and diagnostics layer for multiplayer play.

This is the core of the client networking work.
It is the layer that turns server-driven multiplayer state into something the game can actually use:
 - `NetClient` handles transport, handshake, message intake, and cached authoritative state
 - `ClientPrediction` handles owner-local prediction, replay, and recovery after correction
 - `ClientDiagnostics` records what happened so networking behavior can be inspected and tested

The important design boundary to understand:
 - this subsystem owns multiplayer netcode behavior on the client 
 - `MultiplayerLevelScene` owns presentation and match scene flow.

---
## Design Rationale

The client has to do three things at once:
 - stay connected to an authoritative server
 - feel responsive for the owning player
 - stay debuggable when prediction or delivery goes wrong

## System View

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="net-client-boundaries-light.svg" alt="Client multiplayer netcode boundaries between scenes, NetClient, prediction, diagnostics, and the authoritative server">
  <img class="theme-image-dark" src="net-client-boundaries-dark.svg" alt="Client multiplayer netcode boundaries between scenes, NetClient, prediction, diagnostics, and the authoritative server">
</div>
The scene reads authoritative state from `NetClient`, while prediction and diagnostics stay in adjacent client-netcode helpers.

## Code Layout

<div class="theme-image-swap">
  <img class="theme-image-light" src="net-client-structure-light.svg" alt="Client multiplayer netcode code layout showing NetClient split into connection, runtime, and protocol responsibilities">
  <img class="theme-image-dark" src="net-client-structure-dark.svg" alt="Client multiplayer netcode code layout showing NetClient split into connection, runtime, and protocol responsibilities">
</div>
`NetClient` stays one class, but its implementation is split by concern: connection, runtime, and protocol/cache handling.

Relevant code:
- [Net/Client](/home/valk/Projects/University/Bomberman/Net/Client)
- [Net/ClientPrediction.h](/home/valk/Projects/University/Bomberman/Net/ClientPrediction.h)
- [Net/ClientDiagnostics.h](/home/valk/Projects/University/Bomberman/Net/ClientDiagnostics.h)
