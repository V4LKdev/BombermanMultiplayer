# Architecture

[TOC]

## Project Overview

This project extends a provided singleplayer Bomberman base in SDL2 with a client-server multiplayer architecture built on ENet.

The multiplayer layer is added on top of the existing gameplay scaffold rather than replacing it with a full rewrite.
That keeps the original game foundation intact, while introducing a dedicated server runtime, 
a shared wire protocol, client netcode, multiplayer scene integration, and a diagnostics/testing layer around it.

This page gives the broad system view:
- how the project is split at runtime
- what the main ownership boundaries are
- what comes from the provided base versus what was added for multiplayer

## System Overview

<div class="theme-image-swap">
  <img class="theme-image-light" src="architecture-system-overview-light.svg" alt="High-level architecture showing the client runtime, shared protocol and simulation layers, dedicated server runtime, diagnostics, and test tooling">
  <img class="theme-image-dark" src="architecture-system-overview-dark.svg" alt="High-level architecture showing the client runtime, shared protocol and simulation layers, dedicated server runtime, diagnostics, and test tooling">
</div>

At the highest level, the project is split into two runtime sides:

- a **client process** starting in `main.cpp`, responsible for input, local presentation, client netcode, and multiplayer scene flow
- a **dedicated server process** starting in `server_main.cpp`, responsible for authoritative match state, round flow, and **fixed-tick simulation**

Between them sits a shared contract centered on `Net/NetCommon.h`.
That shared layer defines the packet protocol used by both client and server, including message types, payload sizes, and channel assignments.

The multiplayer gameplay layer sits above that protocol work:
the client consumes authoritative updates from the server, applies client-side prediction and correction where needed, and turns the resulting state into the playable match presentation.

## Runtime Boundaries

The project is split so that each layer owns a small and clear part of the system.

| Area                  | Ownership                                                                                                                     |
|:----------------------|:------------------------------------------------------------------------------------------------------------------------------|
| **Client**            | input collection, local presentation, connection state, prediction, smoothing, and client-side diagnostics                    |
| **Server**            | authoritative match state, lobby/bootstrap/match flow, simulation ticks, snapshots, corrections, and reliable gameplay events |
| **Shared**            | protocol structs, message ids, payload sizes, channel assignments, and shared diagnostics schema                              |
| **Scene integration** | client-side multiplayer scene code that turns authoritative state and client-side prediction into the visible match           |

This keeps the architecture readable:
the client is responsible for responsiveness and presentation, the server is responsible for gameplay truth, and the shared layer keeps the protocol explicit with a single source of truth.

## Inherited Base Vs Added Systems

<div class="theme-image-swap">
  <img class="theme-image-light" src="architecture-base-vs-added-light.svg" alt="Split between the provided singleplayer Bomberman base and the added multiplayer systems, including the dedicated server, shared protocol, client netcode, multiplayer scene integration, and diagnostics">
  <img class="theme-image-dark" src="architecture-base-vs-added-dark.svg" alt="Split between the provided singleplayer Bomberman base and the added multiplayer systems, including the dedicated server, shared protocol, client netcode, multiplayer scene integration, and diagnostics">
</div>

That split shows where the project work is concentrated: 
the original local gameplay foundation remains in place, while the main extension is the multiplayer architecture built around it.

## End-To-End Runtime Flow

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="architecture-runtime-flow-light.svg" alt="End-to-end multiplayer runtime flow from local player input through NetClient, authoritative server simulation, snapshots and corrections, and client-side scene presentation">
  <img class="theme-image-dark" src="architecture-runtime-flow-dark.svg" alt="End-to-end multiplayer runtime flow from local player input through NetClient, authoritative server simulation, snapshots and corrections, and client-side scene presentation">
</div>

At a high level, the multiplayer loop works like this:

1. the client collects local player input
2. that input is sent to the server through the shared protocol
3. the server advances the authoritative match state on fixed simulation ticks
4. the server sends snapshots, corrections, and reliable gameplay events back to connected clients
5. the client merges that authoritative data into the multiplayer scene and updates the visible match state

This is the central architectural path through the project:
local input starts on the client, gameplay truth resolves on the server, and the client presents the result.

## Design Rationale

The project is split this way for a few practical reasons:

- **server authority** keeps shared gameplay state consistent across players
- **an explicit protocol** keeps the wire contract easier to validate, debug, and evolve
- **client-side prediction and smoothing** improve responsiveness and presentation without replacing server truth
- **integrated diagnostics** make the system easier to test and reason about under different network conditions

The result is a project structure that stays understandable even as multiplayer features are added on top of the original singleplayer base.

### Relevant Areas

- `main.cpp`
- `server_main.cpp`
- `Net/`
- `Server/`
- `Scenes/MultiplayerLevelScene/`
- selected shared `Sim/` and config files used by both sides

<div class="section_buttons">

| Previous               |                        Next |
|:-----------------------|----------------------------:|
| [README](../README.md) | [Networking](networking.md) |

</div>
