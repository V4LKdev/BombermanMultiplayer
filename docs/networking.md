# Networking

[TOC]

This page presents the networking layer added on top of the provided singleplayer Bomberman base.

The project uses an **authoritative client-server model**: clients send gameplay intent, 
the server owns the shared match state, and both sides stay compatible 
through one explicit wire contract defined in `NetCommon.h`.

This page explains the system-level contract and message flow:
- how authority is divided between client and server
- what kinds of messages move across the wire
- how bootstrap, active play, and correction fit together
- where to look next for deeper integration details

\note
For deeper implementation detail, see
@ref authoritative_server "Authoritative Server",
@ref net_client "Client Multiplayer Netcode",
and @ref multiplayer_level_scene "Multiplayer Level Scene".

## Core Design

The networking layer is built around three deliberate decisions:

- the **server is authoritative**
- the client sends **input intent** instead of owning shared world state
- the protocol stays **explicit, fixed-size, and easy to validate**

At a high level, the system is split into a small number of clear responsibilities.

`Net/NetCommon.h` defines the shared wire contract used by both client and server.
It contains the packet header, protocol version gate, message identifiers, channel assignments, payload sizes, 
and the helpers used to serialize and validate network messages.

On the client side, `Net/Client/NetClient.h` owns the transport endpoint, handshake flow, and cached authoritative state.
`Net/Client/ClientPrediction.h` exists to keep the owning player responsive under latency by predicting local movement and reconciling once authoritative correction arrives.

On the server side, `Server/ServerSimulation.cpp` and the surrounding server flow code accept input, advance the match on authoritative simulation ticks, and replicate the resulting world state back to clients through snapshots, corrections, and reliable gameplay events.

The design goal is not only to make multiplayer function.
It is to keep the networking model **readable, testable, and explainable** while still supporting responsive moment-to-moment gameplay.

\remark
The protocol is intentionally strict. 
Message ids, payload sizes, and channel roles stay visible in one place so the networking layer remains easier to validate, debug, and explain.

## Shared Protocol Contract

`Net/NetCommon.h` is the single shared protocol file used by both client and server.
It acts as the source of truth for the wire contract and keeps the most important protocol rules in one place.

It defines:

- the protocol version gate
- the packet header
- message identifiers
- ENet channel assignments
- fixed payload sizes
- wire structs
- serializers and deserializers
- packet construction and validation helpers

This is intentionally dense code.
For protocol work, explicitness is a strength: wire layout, validation rules, and packet construction stay close together.

### Channel Model

The protocol uses five ENet channels, each with one clear responsibility:

| Channel                | ID | Purpose                                         | Direction         |
|------------------------|----|-------------------------------------------------|-------------------|
| `ControlReliable`      | 0  | Reliable non-gameplay events                    | Client and Server |
| `GameplayReliable`     | 1  | Reliable gameplay events that must stay ordered | Client and Server |
| `InputUnreliable`      | 2  | Client input stream                             | Client to Server  |
| `SnapshotUnreliable`   | 3  | Authoritative world snapshots                   | Server to Client  |
| `CorrectionUnreliable` | 4  | Owner-local authoritative corrections           | Server to Client  |

This split makes reliability and ordering decisions visible at the protocol level instead of hiding them inside higher-level gameplay code.
That keeps message behaviour easier to reason about during implementation, debugging, and testing.

---
## Message Catalogue

\note
All current payloads are fixed-size on the wire.  
That keeps packet validation cheap and predictable, because message size can be checked before any payload is deserialized.

These are the current payloads defined by the protocol:

| Payload type                                                     | Channel                | Payload size (v9) | Purpose                                                                  |
|------------------------------------------------------------------|------------------------|------------------:|--------------------------------------------------------------------------|
| @ref bomberman::net::MsgHello "MsgHello"                         | `ControlReliable`      |              18 B | Client handshake initiation with protocol version and player name        |
| @ref bomberman::net::MsgWelcome "MsgWelcome"                     | `ControlReliable`      |               5 B | Server acceptance, assigned player id, and server tick rate              |
| @ref bomberman::net::MsgReject "MsgReject"                       | `ControlReliable`      |               3 B | Explicit handshake rejection with reason and expected protocol version   |
| @ref bomberman::net::MsgLevelInfo "MsgLevelInfo"                 | `ControlReliable`      |               8 B | Announces authoritative `matchId` and `mapSeed` for round bootstrap      |
| @ref bomberman::net::MsgLobbyState "MsgLobbyState"               | `ControlReliable`      |              76 B | Current lobby roster, ready states, and lobby phase                      |
| @ref bomberman::net::MsgLobbyReady "MsgLobbyReady"               | `ControlReliable`      |               1 B | Client ready toggle for its current seat                                 |
| @ref bomberman::net::MsgMatchLoaded "MsgMatchLoaded"             | `ControlReliable`      |               4 B | Client acknowledgement that the round scene is loaded                    |
| @ref bomberman::net::MsgMatchStart "MsgMatchStart"               | `ControlReliable`      |              12 B | Reliable round start edge with authoritative timing ticks                |
| @ref bomberman::net::MsgMatchCancelled "MsgMatchCancelled"       | `ControlReliable`      |               4 B | Cancels round bootstrap and returns to the lobby                         |
| @ref bomberman::net::MsgMatchResult "MsgMatchResult"             | `ControlReliable`      |              22 B | Final authoritative round result                                         |
| @ref bomberman::net::MsgInput "MsgInput"                         | `InputUnreliable`      |              21 B | Batched client input stream keyed by sequence number                     |
| @ref bomberman::net::MsgSnapshot "MsgSnapshot"                   | `SnapshotUnreliable`   |             111 B | Authoritative replicated world state                                     |
| @ref bomberman::net::MsgCorrection "MsgCorrection"               | `CorrectionUnreliable` |              20 B | Owner-local correction for reconciliation                                |
| @ref bomberman::net::MsgBombPlaced "MsgBombPlaced"               | `GameplayReliable`     |              16 B | Reliable gameplay event for bomb creation                                |
| @ref bomberman::net::MsgExplosionResolved "MsgExplosionResolved" | `GameplayReliable`     |             497 B | Reliable gameplay event for blast resolution and resulting world changes |

\remark
The message set is intentionally small and specialised.
Each payload has one clear role in the round lifecycle, which makes the protocol easier to evolve without blurring responsibilities.

### Key Protocol Fields

These fields are especially important for understanding how the protocol keeps client and server aligned:

| Field or concept                         | Description                                                                       |
|------------------------------------------|-----------------------------------------------------------------------------------|
| `kProtocolVersion`                       | Hard compatibility gate used during the initial handshake                         |
| `PacketHeader::type`                     | Selects the payload parser, expected channel, and size-validation path            |
| `PacketHeader::payloadSize`              | Enables fast fixed-size validation before any payload is decoded                  |
| `matchId`                                | Prevents stale round data from leaking across lobby and match transitions         |
| `serverTick`                             | Anchors snapshots, corrections, and reliable gameplay events on the same timeline |
| `baseInputSeq` / `lastProcessedInputSeq` | Connects client input history to authoritative server acknowledgement             |
| channel assignment                       | Encodes the reliability and ordering strategy directly into the protocol          |

---
## Authority Model

The networking layer uses an asymmetric authority split.

The **server owns the authoritative match state**:
player positions, bomb state, explosions, round flow, and the final outcome of gameplay decisions all resolve on the server timeline.

The **client owns local input intent** and may temporarily predict the owning player's movement for responsiveness.
That prediction improves local feel, but it does not replace server authority.

At a high level:

- the client predicts only what it must for immediate local responsiveness
- the server decides what actually happened in shared gameplay state
- snapshots replicate authoritative world state back to clients
- corrections repair the owning client's local prediction when it diverges from the server result

This split is the core design decision behind the networking layer.
It keeps the shared match state consistent across all players while still allowing the local player to feel responsive under latency.

\remark
Prediction is treated as a presentation and responsiveness tool, not as a source of gameplay truth.

---
## Runtime Message Flow

This section gives the high-level flow of a multiplayer round, from bootstrap into active play.

### Match Bootstrap

The transition from lobby into live gameplay is explicit and synchronised on the server timeline.

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="networking-bootstrap-flow-light.svg" alt="Bootstrap message flow from connect and hello through lobby state, readying, level info, match loaded, and match start">
  <img class="theme-image-dark" src="networking-bootstrap-flow-dark.svg" alt="Bootstrap message flow from connect and hello through lobby state, readying, level info, match loaded, and match start">
</div>
The server controls admission, lobby synchronisation, and round start through explicit control messages.
`MsgLevelInfo`, `MsgMatchLoaded`, and `MsgMatchStart` ensure both sides agree on the current `matchId` and timing before gameplay unlocks.

### Active Match

Once a round is active, the client and server exchange a small number of defined streams:

- client input batches
- authoritative snapshots
- owner-local corrections
- reliable gameplay events such as bomb placement and explosion resolution

These streams feed authoritative simulation on the server and prediction-aware presentation on the client.

### Prediction And Correction

Prediction exists to keep the owning player responsive under latency.

The client records and sends local input, may predict immediate local movement, and later reconciles once authoritative correction arrives.
If replay cannot be trusted, the client falls back to recovery behaviour instead.

The deeper client-side behaviour lives in @ref net_client "Client Multiplayer Netcode".

---
## Inspectability And Testing

The networking layer is designed to be inspectable, not just functional.

That includes:

- transport and session diagnostics
- client prediction telemetry
- log capture during test runs
- cross-checking behaviour under different network conditions

The detailed methodology lives in [Testing](testing.md), but the important point here is that observability is part of the networking design itself.

---
## Example Wire Layout

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="networking-packet-correction-light.svg" alt="PacketHeader and MsgCorrection wire layout showing the fixed header followed by match id, server tick, processed input sequence, quantized position, and player flags">
  <img class="theme-image-dark" src="networking-packet-correction-dark.svg" alt="PacketHeader and MsgCorrection wire layout showing the fixed header followed by match id, server tick, processed input sequence, quantized position, and player flags">
</div>

`MsgCorrection` is a good example of the protocol style used across the networking layer:
it combines the shared header with authoritative timing, acknowledged input progress, and owner-local repair state in one compact fixed-size packet.

Including the shared `PacketHeader`, the full packet is `23 B` on the wire:
`3 B` of header plus a `20 B` correction payload.

<div class="section_buttons">

| Previous                        |                                                             Next |
|:--------------------------------|-----------------------------------------------------------------:|
| [Architecture](architecture.md) | <a href="group__net__client.html">Client Multiplayer Netcode</a> |

</div>
