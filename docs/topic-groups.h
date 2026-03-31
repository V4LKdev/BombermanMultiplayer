/**
 * \defgroup authoritative_server Authoritative Server
 * \brief Dedicated authoritative server runtime, session flow, and fixed-tick match simulation.
 *
 * This is the gameplay-truth side of the multiplayer architecture.
 *
 * It owns:
 *
 * - client admission and release
 * - lobby and round flow
 * - authoritative player, bomb, powerup, and tile state
 * - fixed-tick match simulation
 * - snapshot, correction, and reliable gameplay event production
 *
 * The important design boundary:
 * the **server owns gameplay truth**, while the client only sends intent and presents authoritative results.
 *
 * ## Ownership
 *
 * The server is responsible for deciding what actually happened in shared gameplay state.
 *
 * That includes who is connected, which players are accepted into the lobby, when a round starts and ends, how the match advances on each simulation tick, and what authoritative state is replicated back to clients.
 *
 * This page focuses on that ownership boundary and the high-level server split.
 * The packet contract itself is explained in [Networking](networking.md).
 *
 * ## State And Session
 *
 * <div class="theme-image-swap theme-image-swap--medium">
 *   <img class="theme-image-light" src="server-state-player-layers-light.svg" alt="Server-side player state layers showing PeerSession, PlayerSlot, MatchPlayerState, and reconnect reclaim">
 *   <img class="theme-image-dark" src="server-state-player-layers-dark.svg" alt="Server-side player state layers showing PeerSession, PlayerSlot, MatchPlayerState, and reconnect reclaim">
 * </div>
 *
 * The server distinguishes transport session state, accepted lobby state, and active in-match state instead of collapsing them into one structure.
 *
 * ## Server Split
 *
 * | Slice                    | What it owns                                                                | Representative files                                                                                              |
 * |--------------------------|-----------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
 * | **State and Session**    | Connected peers, accepted seats, active match player state                  | `Server/ServerState.h`, `Server/ServerSession.cpp`                                                                |
 * | **Receive and Dispatch** | ENet events, packet validation, control/input routing                       | `Server/ServerEvents.cpp`, `Server/ServerHandlers.cpp`, `Server/ServerInputHandlers.cpp`                          |
 * | **Flow Control**         | Lobby countdown, match bootstrap, round start/end, return to lobby          | `Server/ServerFlow.cpp`, `Server/ServerLobbyFlow.cpp`, `Server/ServerMatchFlow.cpp`                               |
 * | **Match Simulation**     | Fixed-tick gameplay state, snapshots, corrections, reliable gameplay events | `Server/ServerSimulation.cpp`, `Server/ServerSnapshot.cpp`, `Server/ServerBombs.cpp`, `Server/ServerPowerups.cpp` |
 *
 * This split keeps the server readable: one layer answers who is connected, one answers what arrived, one answers which phase the server is in, and one answers what happens on each authoritative tick.
 *
 * ## Match Flow
 *
 * <div class="theme-image-swap theme-image-swap--medium">
 *   <img class="theme-image-light" src="server-flow-state-light.svg" alt="Authoritative server flow states from lobby through countdown, bootstrap, active match, and end-of-match return">
 *   <img class="theme-image-dark" src="server-flow-state-dark.svg" alt="Authoritative server flow states from lobby through countdown, bootstrap, active match, and end-of-match return">
 * </div>
 *
 * The server owns the phase machine for lobby readiness, bootstrap, active match, and end-of-round return to lobby.
 *
 * ## Ownership Rules
 *
 * - the server admits and releases peers
 * - the server owns lobby state and round phase transitions
 * - the server advances the authoritative match on fixed ticks
 * - the server decides authoritative player, bomb, powerup, and tile outcomes
 * - the server produces snapshots, corrections, and reliable gameplay events for clients
 *
 * Relevant code:
 * - `Server/ServerState.h`
 * - `Server/ServerSession.cpp`
 * - `Server/ServerFlow.h`
 * - `Server/ServerLobbyFlow.cpp`
 * - `Server/ServerMatchFlow.cpp`
 * - `Server/ServerSimulation.cpp`
 * - `Server/ServerSnapshot.h`
 * - `Server/ServerBombs.h`
 * - `Server/ServerPowerups.h`
 *
 * <div class="section_buttons">
 *
 * | Previous                    |                                                             Next |
 * |:----------------------------|-----------------------------------------------------------------:|
 * | [Networking](networking.md) | <a href="group__net__client.html">Client Multiplayer Netcode</a> |
 *
 * </div>
 */

/**
 * \defgroup net_client Client Multiplayer Netcode
 * \brief Client-side connection, prediction, correction, and diagnostics layer for multiplayer play.
 *
 * This is the core of the client networking work.
 *
 * It is the layer that turns server-driven multiplayer state into something the game can actually use:
 *
 * - `NetClient` handles transport, handshake, message intake, and cached authoritative state
 * - `ClientPrediction` handles owner-local prediction, replay, and recovery after correction
 * - `ClientDiagnostics` records what happened so networking behaviour can be inspected and tested
 *
 * The important design boundary is simple:
 * this subsystem owns multiplayer netcode behaviour on the client, while `MultiplayerLevelScene` owns presentation and match scene flow.
 *
 * ## Ownership
 *
 * The client has to do three things at once:
 *
 * - stay connected to an authoritative server
 * - feel responsive for the owning player
 * - stay debuggable when prediction or delivery goes wrong
 *
 * That is why transport, prediction, and diagnostics are kept adjacent but distinct.
 *
 * ## System View
 *
 * <div class="theme-image-swap theme-image-swap--medium">
 *   <img class="theme-image-light" src="net-client-boundaries-light.svg" alt="Client multiplayer netcode boundaries between scenes, NetClient, prediction, diagnostics, and the authoritative server">
 *   <img class="theme-image-dark" src="net-client-boundaries-dark.svg" alt="Client multiplayer netcode boundaries between scenes, NetClient, prediction, diagnostics, and the authoritative server">
 * </div>
 *
 * The scene reads authoritative state from `NetClient`, while prediction and diagnostics stay in adjacent client-netcode helpers.
 *
 * ## Code Layout
 *
 * <div class="theme-image-swap">
 *   <img class="theme-image-light" src="net-client-structure-light.svg" alt="Client multiplayer netcode code layout showing NetClient split into connection, runtime, and protocol responsibilities">
 *   <img class="theme-image-dark" src="net-client-structure-dark.svg" alt="Client multiplayer netcode code layout showing NetClient split into connection, runtime, and protocol responsibilities">
 * </div>
 *
 * `NetClient` stays one class, but its implementation is split by concern: connection, runtime, and protocol/cache handling.
 *
 * Relevant code:
 * - `Net/Client/NetClient.h`
 * - `Net/Client/NetClient.cpp`
 * - `Net/Client/NetClient.Connection.cpp`
 * - `Net/Client/NetClient.Runtime.cpp`
 * - `Net/Client/NetClient.Protocol.cpp`
 * - `Net/Client/ClientPrediction.h`
 * - `Net/Client/ClientPrediction.cpp`
 * - `Net/ClientDiagnostics.h`
 * - `Net/ClientDiagnostics.cpp`
 *
 * <div class="section_buttons">
 *
 * | Previous                                             |                                                                        Next |
 * |:-----------------------------------------------------|----------------------------------------------------------------------------:|
 * | <a href="group__authoritative__server.html">Authoritative Server</a> | <a href="group__multiplayer__level__scene.html">Multiplayer Level Scene</a> |
 *
 * </div>
 */

/**
 * \defgroup multiplayer_level_scene Multiplayer Level Scene
 * \brief Client-side multiplayer match orchestration and presentation subsystem.
 *
 * This subsystem is the client-side runtime for a multiplayer match.
 *
 * It extends the shared `LevelScene` gameplay scaffold with:
 *
 * - authoritative networking
 * - prediction-aware local control
 * - remote player presentation
 * - world presentation
 * - the scene flow around match bootstrap, active play, results, and exit handling
 *
 * In practice, this is the point where raw network state becomes the playable and visible multiplayer game.
 *
 * This page covers the layer above client netcode: `NetClient` and `ClientPrediction` provide the data and correction inputs, while `MultiplayerLevelScene` turns them into visible match state.
 *
 * ## Ownership
 *
 * <div class="theme-image-swap">
 *   <img class="theme-image-light" src="multiplayer-level-scene-structure-light.svg" alt="Multiplayer level scene code layout showing the scene core, authority merge, presentation slices, telemetry, and its links to NetClient and ClientPrediction">
 *   <img class="theme-image-dark" src="multiplayer-level-scene-structure-dark.svg" alt="Multiplayer level scene code layout showing the scene core, authority merge, presentation slices, telemetry, and its links to NetClient and ClientPrediction">
 * </div>
 *
 * The multiplayer scene is the integration point where authority handling, prediction-aware local control, and presentation logic meet.
 *
 * ## Runtime Flow
 *
 * <div class="theme-image-swap theme-image-swap--medium">
 *   <img class="theme-image-light" src="multiplayer-level-scene-runtime-flow-light.svg" alt="Multiplayer level scene runtime flow from frame start through net validation, correction, event collection, authority merge, presentation update, and telemetry">
 *   <img class="theme-image-dark" src="multiplayer-level-scene-runtime-flow-dark.svg" alt="Multiplayer level scene runtime flow from frame start through net validation, correction, event collection, authority merge, presentation update, and telemetry">
 * </div>
 *
 * Each frame applies local correction first, then merges authoritative state, then updates the visible match state and presentation.
 *
 * ## Ownership Rules
 *
 * - the local player may be driven by prediction
 * - remote players remain snapshot-driven
 * - corrections are applied before snapshot and event presentation
 * - reliable gameplay events are merged in authoritative tick order
 * - match flow, banners, and scene transitions are handled here
 *
 * Relevant code:
 * - `Scenes/MultiplayerLevelScene/`
 * - `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.cpp`
 * - `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Authority.cpp`
 * - `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Presentation.cpp`
 * - `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.RemotePresentation.cpp`
 * - `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.WorldPresentation.cpp`
 * - `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Telemetry.cpp`
 *
 * <div class="section_buttons">
 *
 * | Previous                                                         |                  Next |
 * |:-----------------------------------------------------------------|----------------------:|
 * | <a href="group__net__client.html">Client Multiplayer Netcode</a> | [Testing](testing.md) |
 *
 * </div>
 */
