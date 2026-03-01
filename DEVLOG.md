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
