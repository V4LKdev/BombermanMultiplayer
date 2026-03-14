# Bomberman Multiplayer – Dev Log

## 2026-02-18 (e893f17) – Initial Linux setup + movement bugfix

### Goal
Get the provided Bomberman repo building + running on Arch Linux (CLion), then fix any platform/runtime blockers.

### Build/IDE setup
- Replaced legacy CMakeLists with modern target-based CMake using pkg-config for SDL2 libs.
- Configured CLion run working directory to project root so assets load correctly.

### Bug: Player/enemies don’t move
**Symptom**
- Menu works, bombs spawn, sprite flips on input, but player/enemies don’t translate.

**Diagnosis**
- Per-frame movement delta was quantized to an `int` using `floor()`.
- On Linux with high FPS, `delta` values were small (~1–4 ms), making movement < 1 px/frame and always floored to 0.

**Fix**
- Switched movement/positions to float/sub-pixel and only rounded for rendering.
- Normalized speed to pixels/sec (or tiles/sec) and converted delta to seconds with a clamp.

**Result**
- Frame-rate independent movement; consistent across platforms.
- Game runs correctly from CLion and terminal.

### Repo hygiene
- Added proper .gitignore for CLion/CMake.
- Removed Windows-only artifacts (x64/, packages/, .sln/.vcxproj) from version control.

## 2026-02-18 (17362f4, 9ff8772) – Cleanup + Collision Refactor

### Goal
Stabilize core runtime behavior and refactor collision handling for correctness and future networking.

### Engine safety + ownership
- Added scene safety guards to avoid null deref when no scene is active.
- Clamped `Scene::insertObject` indices to prevent out-of-range inserts.
- Switched `SceneManager` + `AssetManager` to `std::unique_ptr` ownership in `Game`.
- Added `Game` initialization guard to prevent running if SDL init fails.
- Fixed `SceneManager::removeScene` log message typo.

### Collision refactor
- Added `Util/Collision` module with centered scaling + AABB overlap checks.
- Added `Object::getRectF()` to use float positions for collision.
- Replaced all collision tests in `LevelScene` with float rects + centralized helpers.
- Introduced tunable hitbox scales for movement vs damage collisions.
- Updated build to include `Util/Collision.cpp`.

### Result
- Collisions behave consistently without offset artifacts.
- Code is cleaner and ready for deterministic network simulation.

## 2026-02-19 (5d165a0) – ENet Bootstrap (Dedicated Server + Handshake)

### Goal
Establish a real client/server networking baseline with ENet before gameplay sync work.

### Build and targets
- Added ENet dependency via pkg-config (`libenet`).
- Added separate `bomberman_server` executable target in CMake.
- Kept existing game executable as the client path.

### Protocol foundation
- Added `Net/NetCommon.h` with:
  - protocol constants and message enums (`Hello`, `Welcome`)
  - wire header schema (`type`, `payloadSize`, `sequence`, `tick`, `flags`)
  - explicit little-endian read/write helpers (`u16/u32`)
  - explicit serializers/deserializers for `PacketHeader`, `MsgHello`, and `MsgWelcome`
- Avoided raw struct memcpy as wire format to reduce cross-platform packing/alignment risk.

### Server prototype
- Implemented `server_main.cpp`:
  - ENet init/create host/event loop/cleanup lifecycle
  - connect/disconnect logging
  - receive path with defensive packet validation
  - parse `Hello`, validate protocol, send reliable `Welcome`

### Client bootstrap probe
- Added temporary startup handshake in `main.cpp`:
  - connect to localhost server
  - send `Hello`
  - receive/validate/log `Welcome`
  - disconnect and continue startup (offline fallback if handshake fails)

### Result
- Verified end-to-end handshake:
  - server logs connection + `Hello` parse + `Welcome` send
  - client logs connect + `Hello` send + `Welcome` receive
- Project now has a working dedicated-server network baseline for next steps (input stream + snapshots).

## 2026-02-20 (e538244) – Harden NetCommon protocol layer with validation + documentation

### Goal
Strengthen the network protocol implementation with defensive input validation, comprehensive documentation, and convenience helpers to ensure correctness before gameplay integration.

### Protocol hardening
- Added `kMaxPacketSize` constant (1400 bytes, below MTU) for bound checking.
- Reorganized size constants with computed expressions and assertion checks.
- Added `isValidMsgType()` validator to prevent invalid message types.
- Enhanced `deserializeHeader()` with multi-stage validation:
  - payload size bounds checking against `kMaxPacketSize`
  - full packet availability check before processing
- Improved `serializeMsgHello()` to zero-pad unused name bytes for determinism.
- Hardened `deserializeMsgHello()` with forced null-termination + zero-padding normalization.

### Documentation + Convenience
- Added comprehensive Doxygen-style comments to all structs, functions, and sections.
- Added `boundedStrLen()` helper for safe string length measurement.
- Added `setHelloName()` overloads (string_view + C-string) for safe name field initialization.
- Included detailed wire format comments inline with serialization code.
- Added `[[nodiscard]]` attributes to deserializer functions.
- Marked serialization functions `noexcept` for clarity.

### Organization
- Added `#include <string_view>` for string_view support.
- Reorganized file into logical sections with clear separators.

### Result
- Protocol layer is now defense-in-depth against malformed packets.
- Code is well-documented and easier to audit/extend.
- Convenience helpers reduce boilerplate in client/server code.

## 2026-02-20 (7d82276) – Fix Exit-Time Segmentation Fault

### Goal
Resolve crash on shutdown (menu exit, window close, Ctrl+C) and stabilize teardown order.

### Diagnosis
- Debug stacktrace pointed into `FT_Done_Face` during scene/text destruction.
- `Game` was shutting down SDL/TTF subsystems before owned scene/asset objects were fully released.

### Fix
- In `Game::~Game()`:
  - Explicitly reset `sceneManager` and `assetManager` first.
  - Destroy renderer before window.
  - Run SDL subsystem shutdown after owned resources are released.
- Normalized `main` signature to standard `char** argv` form.

### Result
- Clean shutdown across all tested exit paths.
- Removed exit-time segfault caused by resource/subsystem destruction order.

## 2026-02-20 (4323485) – Extract Client Handshake + Normalize Binary Naming

### Goal
Move temporary client handshake logic out of `main.cpp` into a dedicated networking class and make executable naming consistent.

### Changes
- Added `Net/NetClient.h` and `Net/NetClient.cpp`.
- Moved handshake/connect/send/receive/disconnect flow into `NetClient::handshake(...)`.
- Updated `main.cpp` to call `NetClient` instead of embedding ENet handshake logic directly.
- Kept behavior unchanged: attempt handshake, continue with offline gameplay if it fails.
- Set server binary output name to lowercase `bomberman_server` for consistency with `bomberman_client`.

### Result
- Cleaner separation between app entrypoint and networking responsibilities.
- Client/server executable naming is now consistent.

## 2026-02-20 (dd98ab2) – Fixed Timestep Client Loop

### Goal
Introduce a simulation loop shape suitable for authoritative networking and prediction work.

### Changes
- Added fixed simulation step processing in `Game::run()` using an accumulator.
- Added frame-delta clamp to avoid spiral-of-death after long frame stalls.
- Added max-steps-per-frame safety cap and warning log.
- Initialized timing state (`lastTickTime`, accumulator) at loop start.
- Kept render pass decoupled from simulation updates.

### Cleanup
- Moved timing constants from `Game.h` to `Game.cpp` internal scope.
- Fixed a malformed constructor doc comment in `Game.h`.

### Result
- Client simulation now advances on a stable 60 Hz step while rendering remains frame-rate driven.
- Loop structure is aligned with upcoming network input/snapshot architecture.

## 2026-02-26 (e9d6d49) – Add Packet Dispatcher To Server Receive Path

### Goal
Move server message handling from hardcoded `if` checks to a reusable dispatch model for protocol growth.

### Changes
- Added `PacketDispatcher` to `Net/NetCommon.h` with:
  - fixed lookup table (`EMsgType -> handler`)
  - explicit `bind(...)` and `dispatch(...)`
- Added minimum payload-size coherence checks in header deserialization path.
- Refactored `server_main.cpp` to:
  - define `ServerContext`
  - implement `onHello(...)` handler
  - bind handlers through dispatcher setup
  - dispatch validated packets via one receive entrypoint

### Result
- Server receive path is now structured for adding new message types without branching sprawl.
- Protocol validation and message routing responsibilities are cleaner.

## 2026-02-26 (f1f5304) – Refactor NetClient To Persistent Connection Lifecycle

### Goal
Evolve client networking from one-shot handshake probe into a reusable lifecycle object (`connect/pump/disconnect`).

### Changes
- Refactored `NetClient` to own ENet state via PIMPL (`Impl` with `ENetHost*` and `ENetPeer*`).
- Added lifecycle support:
  - `connect(host, port, playerName)`
  - `pump(timeoutMs)`
  - `disconnect()`
  - `isConnected()`
- Added tracked handshake results on client:
  - `clientId_`
  - `serverTickRate_`
- Moved startup path in `main.cpp` to use `connect(...)` and explicit disconnect.
- Improved disconnect robustness:
  - graceful disconnect attempt with short ack wait
  - force reset fallback

### Result
- Client now has a persistent connection model suitable for upcoming per-tick networking work.
- Runtime state needed for next features (input stream/snapshots) is available on `NetClient`.

## 2026-03-01 (acc5869) – Add Typed Packet Dispatch Module

### Goal
Separate packet dispatching from protocol definitions and introduce a typed dispatch layer shared by server/client receive paths.

### Changes
- Added new `Net/PacketDispatch.h` with:
  - `tryParsePacket(...)` helper for shared header + payload extraction
  - `PacketDispatcher<TContext>` (typed dispatch table, no `void*`)
  - `dispatchPacket(...)` convenience function (parse + dispatch + error logs)
- Updated `Net/NetCommon.h`:
  - moved legacy untyped dispatcher out
  - added channel constants:
    - `EChannel::Control`
    - `EChannel::GameState`
    - `kChannelCount`
- Refactored `server_main.cpp`:
  - server dispatcher now uses `PacketDispatcher<ServerContext>`
  - `onHello(...)` now receives typed context directly
  - receive path now calls `dispatchPacket(...)`
  - welcome send now uses `EChannel::Control`

### Result
- Shared packet parse/dispatch logic is now centralized and reusable.
- Server receive code is cleaner, safer, and ready for additional message handlers.

## 2026-03-01 (e193d3a) – Refactor NetClient To Dispatcher-Driven Handshake

### Goal
Unify client receive handling under the same dispatcher model and remove duplicated one-off handshake parsing.

### Changes
- Updated `NetClient::Impl`:
  - added `PacketDispatcher<NetClient> dispatcher`
  - added `handshakeComplete` flag
- Added client-side receive handlers:
  - `NetClient::handleWelcome(...)`
  - `NetClient::handleRemoteDisconnect()`
  - `Impl::onWelcome(...)` trampoline bound in constructor
- Updated `NetClient::pump(...)`:
  - routes packets through `dispatchPacket(...)`
  - handles remote disconnect with dedicated cleanup path
- Updated `NetClient::performHandshake(...)`:
  - sends `Hello` on `EChannel::Control`
  - waits for `Welcome` by polling `pump(...)` until `handshakeComplete`
  - resets handshake state before each attempt
- Updated `main.cpp`:
  - removed explicit `disconnect()` call and rely on `NetClient` destructor cleanup

### Result
- Client now uses one receive path for both handshake and future runtime messages.
- Handshake flow is cleaner and aligned with long-term message architecture.

## 2026-03-01 – Add Input Message Path + Client Smoke Test

### Goal
Implement the first gameplay-relevant message path after handshake: client `Input` intent transmission to server with validation and observability.

### Changes
- Extended `Net/NetCommon.h` protocol definitions:
  - added `EMsgType::Input`
  - added fixed payload struct `MsgInput` (`moveX`, `moveY`, `actionFlags`)
  - added `kMsgInputSize` and `minPayloadSize(EMsgType::Input)` coverage
  - added `serializeMsgInput(...)` / `deserializeMsgInput(...)` with axis validation
- Updated `server_main.cpp`:
  - added `ClientInputState` and per-client latest-input cache in `ServerContext`
  - implemented `onInput(...)` handler (parse + store + log)
  - bound `EMsgType::Input` in server dispatcher
  - cleaned disconnect handling to erase only that client's cached input
- Updated `NetClient`:
  - added public `sendInput(const MsgInput&, uint32_t clientTick)`
  - added per-session input sequence counter (`nextInputSequence`)
  - implemented packet construction + send path (Input on `EChannel::GameState`, unreliable)
  - added rate-limited input-send logging
  - reset input sequence state on disconnect/remote-disconnect
- Added temporary runtime smoke test in `main.cpp`:
  - after successful connect, send ~1 second of test input at negotiated tick rate
  - pump networking each tick for immediate flush/receive processing

### Verification
- Ran server/client binaries built from current targets (`Bomberman_Server`, `Bomberman`).
- Observed expected handshake plus Input stream:
  - server receives channel `1` packets of 15 bytes (12-byte header + 3-byte input payload)
  - sequence and tick increment correctly
  - alternating `moveX` values deserialize correctly on server

### Result
- The first end-to-end gameplay message path (`Input`) is now functional and observable.
- Handshake-only networking baseline is now extended to runtime traffic.
## 2026-03-01 (394b176) – Integrate Live Input Streaming Into Game Loop

### Goal
Replace the temporary startup smoke-test with real runtime input packet generation and sending during gameplay.

### Changes
- Updated `Game` constructor and ownership surface:
  - added optional non-owning `NetClient*` parameter to `Game`
  - stored pointer in `Game` for runtime networking access
- Updated `main.cpp`:
  - removed temporary one-second startup smoke-test loop
  - passed `&client` into `Game` construction
- Updated `Game::run()` fixed-step loop:
  - added monotonic `clientTick` incremented each simulation step
  - pumps `NetClient` only when connected
  - samples keyboard state each fixed step and builds `MsgInput`
  - maps movement from both arrow keys and WASD to signed axis intent
  - emits edge-triggered bomb action flag (single pulse on press)
  - sends input intent through `NetClient::sendInput(...)` each fixed step while connected
- Updated protocol convenience in `NetCommon.h`:
  - added `MsgInput::ActionFlag::PlaceBomb`

### Verification
- Confirmed end-to-end runtime behavior with server/client:
  - live movement intent updates (`moveX/moveY`) are received by server while playing
  - bomb action flag pulses are emitted on key press (edge-triggered), not continuously while held

### Result
- Input networking is now integrated into the real gameplay loop instead of a startup-only smoke path.

## 2026-03-02 (57c1000) – Add spdlog Logging Foundation

### Goal
Introduce a centralized logging layer and wire it into the build.

### Changes
- Added `Util/Log.h` and `Util/Log.cpp` with named subsystem loggers and `LOG_*` macros.
- Added spdlog dependency and compile definitions in CMake.
- Added logging-related ignore rules (`LOGGING_PLAN.md`, `logs/`) to `.gitignore`.

### Result
- Client/server/game/protocol logging now uses one shared infrastructure with configurable levels.

## 2026-03-03 (0929039, 5eec5ef) – Protocol/Dispatch Cleanup + NetClient Lifecycle Refactor

### Goal
Clean up protocol/dispatch utilities and make client connection state explicit.

### Changes
- Refactored `NetCommon` and `PacketDispatch` for clearer protocol helpers and structured comments.
- Added `Net/NetSend.h` to centralize reliable/unreliable ENet send helpers.
- Refactored `NetClient` connect flow to return `EConnectState` with specific failure reasons.
- Added connection-state naming helpers and improved cleanup paths (`releaseResources`, `resetState`).
- Updated `main.cpp` to use state-aware connect handling and offline fallback logging.

### Result
- Network transport code is cleaner and easier to extend, and connection failures are now explicit and diagnosable.

## 2026-03-04 (adea28d) – Integrate Persistent Bomb Command Input Flow

### Goal
Finalize runtime input path around persistent bomb command IDs.

### Changes
- Updated `Game` input polling to maintain and send persistent `bombCommandId`.
- Simplified per-tick input sending path and aligned comments/docs with project style.
- Updated server input handling to track latest input and deduplicate bomb requests by command ID.
- Added graceful signal-driven shutdown flow and improved server loop/log clarity.

### Result
- Bomb placement intent is now represented by convergent command IDs end-to-end in the live game/server loop.

## 2026-03-04 (fc7240f, 77cb586) – Add Runtime Log CLI Flags and Validate Build Gating

### Goal
Add practical runtime logging controls for client/server runs and verify compile-time log-level safety.

### Changes
- Added CLI parsing to `main.cpp` and `server_main.cpp`:
  - `--log-level <trace|debug|info|warn|error|critical>`
  - `--log-file <path>`
  - `--help`
- Wired parsed values into `bomberman::log::init(level, file)`.
- Added temporary probe logs in client startup to validate level behavior, then removed them after verification.

### Verification
- Confirmed invalid/missing CLI argument handling prints clear usage errors.
- Confirmed file logging writes to provided paths.
- Confirmed compile-time gating behavior:
  - Debug build emits debug (trace compiled out with current `SPDLOG_ACTIVE_LEVEL`).
  - Release build does not emit debug/trace even when requested via CLI.

### Result
- Logging verbosity and file output are now configurable per run while preserving release safety.

## 2026-03-04 (e885294) – Add `--port` CLI Override for Client and Server

### Goal
Allow runtime port override on both targets while keeping a default fallback port.

### Changes
- Added `--port <value>` parsing to both `main.cpp` and `server_main.cpp`.
- Added strict port validation (numeric, fully parsed, range `1..65535`).
- Wired parsed port into:
  - client connect target (`NetClient::connect(..., port, ...)`)
  - server bind address (`ENetAddress::port`)
- Updated usage output and missing/invalid value error paths.

### Result
- Both binaries now support a default-port workflow with optional per-run port overrides.

## 2026-03-04 (4cbcdf7) – Extract Shared CLI Parse Helpers

### Goal
Reduce duplicated CLI parsing code between client and server mains.

### Changes
- Added `Util/CliCommon.h` (header-only helper module).
- Moved shared parsing helpers into `bomberman::cli` namespace:
  - `parseLogLevel(...)`
  - `parsePort(...)`
- Updated `main.cpp` and `server_main.cpp` to use shared helpers while keeping target-specific `parseCli(...)` local.

### Result
- Client/server CLI behavior remains unchanged, with less duplicated parsing logic and cleaner mains.

## 2026-03-04 (a529a87) – Move NetClient To Async Connect/Handshake State Flow

### Goal
Remove the remaining blocking connect/handshake path from `NetClient` and drive connection progress through `pump(...)`.

### Changes
- Removed blocking `connect(...)` and `performHandshake(...)` flow.
- Added `beginConnect(...)` to initiate ENet connect and defer handshake progression to `pump(...)`.
- Added `cancelConnect()` to abort in-progress `Connecting`/`Handshaking` attempts cleanly.
- Added async timeout tracking (`connectStartTime`, `handshakeStartTime`) with separate connect/handshake timeouts.
- Updated event handling:
  - on `ENET_EVENT_TYPE_CONNECT`, send `Hello` and transition to `Handshaking`
  - on `Welcome`, transition to `Connected`
  - ignore unexpected `CONNECT` events outside `Connecting`
- Fixed protocol-mismatch handling to set terminal `FailedProtocol` state and release resources in the pump path.
- Cleared async pending fields in `resetState()` to avoid stale reconnect state.
- Updated `main.cpp` startup path to use `beginConnect(...)` and poll until state resolves.
- Reorganized `NetClient.h/.cpp` sections/comments to match repository conventions.

### Result
- Connection lifecycle is now state-driven and pump-based, which is compatible with moving connect progression into menu/UI logic.

## 2026-03-04 (132c5b5) – Add ConnectScene Form Scaffold + Menu Routing

### Goal
Move online setup UI out of `MenuScene` and prepare a clean connect form scene.

### Changes
- Added `Scenes/ConnectScene.h/.cpp` and wired it into CMake.
- Updated `MenuScene` to route `Online` into `ConnectScene`.
- Added non-functional connect form UI with focused fields for:
  - player name
  - server IP
  - read-only port display
- Added text helpers for content-sized rendering (`Text::fitToContent`) and empty-text safety.
- Passed client port into `Game` and through to `ConnectScene`.
- Added `kDefaultServerPort` in `NetCommon` and reused it in client/server defaults.

### Result
- The online flow now has a dedicated scene with working form UX, ready for connect logic wiring next.

## 2026-03-04 (40a5c79) – ConnectScene Async Connect Wiring + Live Status

### Goal
Hook the connect form into `NetClient` async connect flow and reflect state changes live in the scene.

### Changes
- Added `ConnectScene` helpers to keep connect flow concise:
  - effective player/host value accessors (with placeholder fallback)
  - `tryStartConnect()` input validation + async connect start
  - `setStatusText()` for centralized status UI updates
- Wired connect button activation (`Enter` / `Space`) to call `beginConnect(...)`.
- Added per-frame state polling in `ConnectScene::update(...)` and mapped `EConnectState` to user-facing status text/colors.
- Added `Esc` behavior to cancel in-progress connect attempts via `cancelConnect()` before returning to menu.
- Kept host validation on form submit and inline error messaging.

### Result
- The connect screen now drives async connection attempts and shows real-time status feedback end-to-end.

## 2026-03-08 – Extract Server Session/Handlers Modules + Integrate Server Tick Loop

### Goal
Reduce `server_main.cpp` complexity by moving networking and server-state responsibilities into dedicated server modules while keeping behavior unchanged.

### Changes
- Added server module stubs and integration:
  - `Server/ServerSession.h/.cpp`
  - `Server/ServerHandlers.h/.cpp`
  - `Server/ServerSnapshot.h/.cpp` (placeholder)
- Moved server-owned state (`ServerState`, `ServerContext`) into `ServerSession`.
- Moved packet handling and dispatch path (`onHello`, `onInput`, dispatcher, `handleEventReceive`) into `ServerHandlers`.
- Kept `server_main.cpp` focused on process orchestration:
  - CLI parsing
  - ENet host lifecycle
  - fixed-step server tick loop + event pump
- Wired new server source files into `Bomberman_Server` target in `CMakeLists.txt`.
- Consolidated server tick rate constant in server module and derived sim step from it in `server_main.cpp`.

### Result
- Server code is split by responsibility and easier to extend.
- `Bomberman_Server` and `Bomberman` both build cleanly after extraction.

## 2026-03-08 – Add MsgReject + Initial MsgState Wire Format

### Goal
Extend the protocol with explicit handshake failure reporting and a first authoritative server-state snapshot payload.

### Changes
- Added `MsgReject` and `EMsgType::Reject` to the protocol.
- Added `MsgState` and `EMsgType::State` with fixed-size player array (`kMaxPlayers`).
- Added payload size constants and minimum payload validation:
  - `kMsgRejectSize`
  - `kMsgStateSize`
  - `minPayloadSize(...)` coverage for both new message types
- Implemented serializers/deserializers:
  - `serializeMsgReject(...)` / `deserializeMsgReject(...)`
  - `serializeMsgState(...)` / `deserializeMsgState(...)`
- Added defensive parsing checks:
  - reject unknown `MsgReject` reason values
  - reject `MsgState` packets with `playerCount > kMaxPlayers`
  - reject `MsgState` packets with unknown player-flag bits
- Fixed `MsgState` wire packing stride to avoid player-entry overlap.

### Result
- Protocol now supports explicit handshake rejection signaling and a baseline full-state snapshot payload.
- Wire parsing for the new messages is deterministic and validated.

## 2026-03-08 (248e5cf) – Add Packet Builders + State Wire Validation Cleanup

### Goal
Finish packet-construction helpers for the new message types and tighten state payload validation behavior.

### Changes
- Added packet builders in `NetCommon`:
  - `makeRejectPacket(...)`
  - `makeInputPacket(...)`
  - `makeStatePacket(...)`
- Moved player-flag mask to `MsgState::PlayerState::kKnownFlags` near the enum declaration.
- Added explicit cast when serializing signed fixed-point state coords (`xQ`, `yQ`) to little-endian `u16`.
- Reduced unbound-message log noise by downgrading dispatcher miss logs from `WARN` to `TRACE`.

### Result
- Packet creation is now symmetric across control/input/state messages.
- State serialization and validation rules are clearer and more maintainable.

## 2026-03-08 (5a05bce) – Add Broadcast Send Helpers + Client Send Cleanup

### Goal
Centralize fanout send behavior and remove duplicated packet-header boilerplate on the client send path.

### Changes
- Added `broadcastReliable(...)` and `broadcastUnreliable(...)` to `NetSend`.
- Broadcast helpers now queue per peer and flush once per call.
- Updated `NetClient::sendInput(...)` to use `makeInputPacket(...)`.
- Added debug trace for duplicate `beginConnect(...)` calls.
- Capped disconnect drain loop iterations in `NetClient::disconnect()` to avoid unbounded wait.

### Result
- Send paths are cleaner, and broadcast fanout now lives in one transport helper layer.

## 2026-03-08 (ef87a72) – Harden Server Handshake + Broadcast Tick Snapshots

### Goal
Wire first server snapshot broadcast flow and harden server-side handshake/input gating.

### Changes
- Server handlers:
  - Added reject helper and reject-on-failure handshake flow (`ServerFull`, `VersionMismatch`).
  - Added duplicate-`Hello` guard and non-handshaked input guard via `peer->data` state.
  - Initialized per-client maps on successful handshake.
- Server session:
  - Added `nextStateSequence` to `ServerState`.
  - Added `buildStateSnapshot(...)` with compact fill and sorted client IDs.
  - Added per-tick `MsgState` packet build + `broadcastUnreliable(...)` send.
- Server main loop:
  - Bound max peers to protocol `kMaxPlayers`.
  - Reordered event drain before simulation step to reduce one-tick input delay.
  - Tightened service timeout and improved disconnect cleanup/logging semantics.

### Result
- Server now produces and broadcasts authoritative state snapshots each tick.
- Handshake and input handling are stricter and more predictable.

## 2026-03-09 (10b63bc) – Extract Shared Simulation Config + Deterministic Map Generation

### Goal
Centralize simulation constants and make movement/map generation shared between client and server.

### Changes
- Added `Sim/SimConfig.h` as a single source of truth for tick-rate and sim limits.
- Added `Sim/Movement.h` with shared tile-Q8 movement, collision, and coordinate conversion helpers.
- Added `Sim/TileMapGen.h` for deterministic tile-map generation from a seed.
- Cleaned up `Const.h` into a more explicit configuration header with typed tile definitions and namespaced constants.

### Result
- Simulation config, movement math, and map generation now live in shared code instead of being duplicated across gameplay and server code.

## 2026-03-10 (7a92f8f) – Extend Protocol + NetClient For LevelInfo And State

### Goal
Grow the runtime protocol beyond connect-only handshake and let the client receive seeded level setup plus authoritative state snapshots.

### Changes
- Added `MsgLevelInfo` and `EMsgType::LevelInfo` to the protocol.
- Added state snapshot helpers and receive-side validation paths in `NetCommon`.
- Extended `NetClient` to:
  - cache latest `MsgState`
  - cache `MsgLevelInfo`
  - expose `tryGetLatestState(...)`, `lastStateTick()`, and `tryGetMapSeed(...)`
  - handle `Reject`, `LevelInfo`, and `State` through the dispatcher
- Kept `LevelInfo` as part of the temporary connect-ready flow for now.

### Result
- The client can now receive both authoritative state and authoritative level seed information over the same validated runtime path.

## 2026-03-10 (3d6125b) – Make Server Authoritative Over Map Seed And Movement

### Goal
Move the server from “input logger + placeholder snapshot sender” to a real authority over map state and player movement.

### Changes
- Replaced separate per-client maps with a unified `ClientState`.
- Added server-side authoritative tile map + `mapSeed` to `ServerState`.
- Added `initServerState(...)` to generate or override a session seed and build the authoritative map.
- Updated `onHello(...)` to send `Welcome` plus `LevelInfo`, then initialize per-client authoritative state.
- Updated `simulateServerTick(...)` to advance player positions via the shared movement primitive and broadcast actual positions in snapshots.
- Updated `server_main.cpp` to initialize and clean up the unified server state.

### Result
- The server now owns the tile layout and authoritative player movement state for the session.

## 2026-03-11 (fd8e40c) – Unify Gameplay Movement Ownership Under Shared Sim

### Goal
Stop local player movement from diverging across singleplayer, multiplayer, and server code paths.

### Changes
- Removed local position integration from `Player`; it now only owns direction and animation.
- Moved canonical player position ownership into `LevelScene` as tile-Q8 state.
- Updated singleplayer movement to run through `sim::stepMovementWithCollision(...)`.
- Updated multiplayer `LevelScene` flow to apply authoritative server snapshots to the local player.
- Added seed-aware `LevelScene` construction and stage-to-level map-seed handoff.
- Added temporary in-game authoritative state debug overlay plus small scene/camera accessors to support it.
- Added a `--mute` client option to simplify testing.

### Result
- Client gameplay now uses the same shared movement primitive as the server, and multiplayer position application no longer depends on legacy local float movement.

## 2026-03-12 (38e6c86) – Reshape Protocol Around Batched Input, Snapshots, And Player IDs

### Goal
Move the multiplayer wire format from a prototype packet layout to a cleaner long-term foundation that can support authoritative simulation, future prediction, and cleaner session semantics.

### Changes
- Shrunk the common packet header to `type + payloadSize`.
- Expanded ENet traffic to three channels:
  - reliable control
  - reliable gameplay (reserved)
  - unreliable gameplay
- Replaced transport-facing `clientId` usage with protocol/game-facing `playerId`.
- Replaced the old single-input payload with a batched button-bitmask input message.
- Renamed public replicated state to `MsgSnapshot` and added a forward-looking `MsgCorrection` type.
- Updated `NetClient`, `Game`, and `LevelScene` call sites to use the new protocol shape.
- Cleaned up `NetClient` structure:
  - clearer helper ordering
  - explicit invalid `playerId` sentinel
  - proper reject handling
  - centralized transport teardown

### Result
- The protocol now has a clearer separation between transport framing, command input, public snapshots, and future owner-only correction data.
- Client/gameplay call sites match the new message model without relying on transport identity leakage.

## 2026-03-13 (4d9c1a2) – Fix Input Starvation With High-Resolution Timing And Server Diagnostics

### Goal
Resolve a runtime bug where multiplayer input would work briefly, then stop responding after the client drifted too far ahead of the server consume window.

### Symptoms
- Movement initially worked, then became unresponsive.
- Server logs showed `lastConsumedInputSeq` advancing while `lastReceivedInputSeq` eventually froze.
- Repeated ahead-drop and gap patterns appeared in the input pipeline.

### Diagnosis
- The protocol refactor itself was not the root failure.
- The real issue was timing drift caused by coarse fixed-step timing:
  - integer-millisecond accumulators
  - `1000 / 60 == 16` ms truncation
  - long-term cadence mismatch between client input generation and server input consumption
- Once the client moved too far ahead, whole batched input packets fell outside the server's acceptable ahead window and were dropped.

### Fix
- Switched both client and server fixed-step loops to `std::chrono::steady_clock`.
- Changed accumulators to high-resolution duration values instead of integer milliseconds.
- Increased input transport tolerance:
  - `kMaxInputBatchSize: 8 -> 16`
  - `kServerInputBufferSize: 16 -> 32`
- Reworked server-side input diagnostics:
  - aggregated late-drop counts
  - aggregated ahead-drop counts
  - input-gap counts
  - average receive-vs-consume lead
  - periodic summaries and repeated-problem warnings
- Updated server snapshot sending to use the queued-send helpers consistently.

### Validation
- Ran a 5-minute session with one client, then with two clients.
- No repeat of input starvation, freeze, or disconnect issues.
- Movement still shows expected server-authoritative latency because prediction is not implemented yet, but the catastrophic failure mode is gone.

### Result
- The multiplayer foundation is now stable enough to continue with prediction, reconciliation, and richer replicated gameplay.

## 2026-03-13 (9b871be, 236d05b) – Add Channel-Based Logging And Then Simplify Configuration

### Goal
Prepare the project for the upcoming telemetry/debugging work by making logs easier to filter by concern, while keeping the setup lightweight enough for day-to-day development.

### Changes
- Expanded the logging layer from coarse subsystem loggers to clearer channel-oriented logging:
  - `net.conn`
  - `net.packet`
  - `net.proto`
  - `net.input`
  - `net.snapshot`
  - plus reserved channels for diagnostics/perf/tests
- Updated networking call sites to use the new log channels so connection flow, packet transport, input handling, and snapshot traffic can be filtered independently.
- Added a default logging config file (`Configs/DefaultLogging.ini`) for project-level runtime defaults.
- Added shared CLI logging parsing through `Util/CliCommon.h` so client and server use the same `--log-level` and `--log-file` behavior.
- Simplified the first version of the config system after review:
  - removed explicit `--log-config`
  - removed config-file control over log-file output
  - removed redundant enable/disable state for file logging
  - kept the final precedence as:
    1. hardcoded defaults
    2. optional default config file
    3. CLI `--log-level`
    4. CLI `--log-file`

### Why This Was Worth Doing
- The networking layer has grown past the point where one broad `client/server/protocol` split is enough for useful debugging.
- At the same time, the original config layering was too heavy for the scope of the project, so it was cut back to a simpler default-file-plus-CLI model.

### Result
- Logs are now easier to filter by actual networking concern.
- The runtime configuration is simpler and less error-prone.
- Default config no longer risks turning on shared file logging for both client and server processes.

## 2026-03-13 – Make Default Logging Config Resolution Deterministic

### Goal
Remove the confusing difference between terminal launches and IDE launches when loading the default logging config.

### Changes
- Stopped resolving `Configs/DefaultLogging.ini` relative to the current working directory.
- Compiled the project source root into both targets and resolved the default logging config from that stable path.
- Kept the fallback model unchanged:
  - hardcoded defaults first
  - default config file if present
  - CLI overrides on top

### Why This Mattered
- A malformed config file would fail correctly from the project root, but silently be skipped when launched from a different working directory.
- That made logging behavior depend on how the executable was started rather than on the project itself.

### Result
- Terminal and CLion launches now behave the same.
- Malformed default config is detected consistently.
- The logging setup is less fragile and easier to reason about.

## 2026-03-14 – Add Server Diagnostics MVP And Refine Telemetry Semantics

### Goal
Lay the first real diagnostics foundation for the networking layer before prediction/reconciliation work begins.

### Changes
- Added `NetDiagnostics` and `NetDiagConfig` as a small reusable diagnostics core.
- Wired server-side session lifecycle hooks:
  - session begin
  - session end
  - text report output on shutdown
- Added packet accounting hooks for:
  - successful typed receives
  - successful queued sends
  - malformed receives before typed dispatch
- Added server-side input diagnostics for:
  - gaps / hold fallback
  - ahead-window drops
  - unknown input bits
- Added lifecycle notes for:
  - peer connected
  - player accepted
  - player disconnected
  - peer rejected
- Refined the telemetry model after smoke tests:
  - successful packet traffic is counted exactly but no longer floods recent-event history
  - resend overlap is no longer mislabeled as a `TooOld` anomaly
  - overlap is now tracked separately as `input_entries_redundant`
  - recent-event history is reserved for genuinely interesting events
- Added useful input transport summary fields:
  - `input_entries_received_total`
  - `input_entries_accepted`
  - `input_entries_redundant`
  - `input_redundancy_ratio`

### Validation
- Ran local smoke tests with one client and with several clients.
- Confirmed that:
  - packet counters look plausible
  - anomaly counts are now semantically correct
  - recent-event history is readable instead of being drowned by resend overlap

### Result
- The project now has a practical server-side telemetry baseline.
- Diagnostics are strong enough to explain real runtime behavior instead of just producing noise.

## 2026-03-14 – Send Neutral Input While Unfocused

### Goal
Stop client focus loss from turning into misleading server-side input gaps and stale held movement.

### Problem
- Under Hyprland, moving the mouse off a client window immediately removes focus.
- When that happened during testing, the client could stop producing fresh gameplay input while the server kept simulating.
- The server then logged long `Gap` / `hold` streaks that were mostly test-environment artifacts rather than network transport faults.

### Changes
- Added explicit keyboard-focus tracking in `Game`.
- On `SDL_WINDOWEVENT_FOCUS_LOST`, the client now:
  - marks itself unfocused
  - clears local held movement state in `LevelScene`
- While unfocused, `pollNetInput()` sends a neutral `0` button mask every simulation tick instead of relying on stale local key state.

### Result
- Unfocused multiplayer clients now fail safe by sending neutral input.
- The server no longer has to infer long hold streaks from missing fresh commands caused by local focus loss.
- Diagnostics remain useful even in multi-window local testing.

## 2026-03-14 – Fix Overflow Client Rejection Path

### Goal
Make overflow clients fail clearly and intentionally when all gameplay slots are already occupied.

### Problem
- The server ENet host was capped to the same count as gameplay slots.
- A 5th client could time out at the transport level before ever reaching the protocol `Hello`/`Reject` path.
- That produced the wrong UI result (`Connection timed out`) and no explicit reject note in diagnostics.

### Changes
- Increased transport peer capacity above gameplay slot capacity so overflow peers can connect far enough to receive `MsgReject::ServerFull`.
- Preserved the last explicit reject reason in `NetClient`.
- Updated the connect scene status text to distinguish:
  - `Server is full`
  - `Access denied`
  - `Connection rejected`
- Clarified diagnostics note wording from generic peer lifecycle labels to explicit transport-level wording.

### Result
- Overflow clients now fail through the intended protocol path instead of timing out.
- The UI shows the actual failure reason.
- Server diagnostics now record the reject lifecycle coherently.

## 2026-03-14 – Tighten Diagnostics Semantics And Add Peer Sampling

### Goal
Make the server diagnostics layer more truthful and more useful before expanding it further.

### Problem
- Header-valid packets were still being counted as successful receives even if typed payload handling failed later.
- Snapshot send accounting treated one broadcast operation as one sent packet, which undercounted the server’s main outbound traffic path.
- Packet and anomaly diagnostics were still too anonymous to answer which gameplay peer was responsible for a problem.
- The report already had packet aggregate data internally, but did not show it.
- Some old per-client rolling counters and the `tick(nowMs)` naming were leftover transitional artifacts from earlier iterations.

### Changes
- Tightened typed receive semantics so server packet accounting now distinguishes:
  - `Ok`
  - `Dropped`
  - `Rejected`
  - `Malformed`
- Threaded gameplay peer identity through packet and anomaly diagnostics APIs.
- Changed snapshot send accounting to count per-recipient queue attempts instead of one broadcast operation.
- Exposed per-message packet aggregates in the diagnostics report.
- Renamed diagnostics bookkeeping from `tick(nowMs)` to `advanceTick()` to match what it actually does.
- Added periodic ENet peer sampling for:
  - RTT
  - RTT variance
  - packet loss (converted to permille)
  - queued reliable data
  - queued unreliable data
- Removed the old rolling per-client summary counters that only existed for transitional periodic debug logs.

### Validation
- Ran local smoke tests after the semantics cleanup and after peer sampling was added.
- Confirmed that:
  - `latest_peer_samples` now shows sane localhost transport values
  - per-message packet aggregates match the expected flow (`Hello`, `Welcome`, `LevelInfo`, `Input`, `Snapshot`)
  - recent events now attribute anomalies to the responsible gameplay peer

### Result
- The diagnostics layer is now much closer to a trustworthy server telemetry baseline.
- Receive/send counters better reflect what the server actually processed and queued.
- Transport-health data is now available alongside gameplay-facing anomaly data.
