# Bomberman Multiplayer

Authoritative multiplayer networking layer for a Bomberman student project, with client prediction, dedicated server authority, diagnostics, and structured network testing.

![Multiplayer Screenshot][hero-image]

[![Watch Demo][demo-thumbnail]][demo-video-url]

## My Contribution

This project builds on a provided singleplayer Bomberman base. My work focuses on the multiplayer and networking layer added on top: dedicated server flow, protocol design, client prediction and correction, diagnostics, test methodology, and the accompanying documentation pass.

The goal of this repository is to present that networking work clearly and honestly, both as a student assessment artifact and as a portfolio project for gameplay and multiplayer engineering roles.

## Key Technical Highlights

- Authoritative dedicated server built on ENet
- Lobby, bootstrap, match start, and end-of-round flow
- Client prediction, reconciliation, and recovery handling
- Snapshot and owner-correction replication model
- Diagnostics logging and reproducible network-focused test runs

## System Snapshot

![Architecture Overview][architecture-diagram]

The client collects local input, predicts local movement, and communicates with an authoritative server over ENet. The server owns the match state, processes accepted input on fixed simulation ticks, and sends snapshots and corrections back to clients. Diagnostics and test tooling are used to inspect transport behavior, gameplay replication, and failure cases under controlled conditions.

## Responsibilities

| Area | My work |
| ---- | ------- |
| Networking | Protocol design, packet flow, ENet integration, client/server session lifecycle |
| Client netcode | Input batching, prediction, replay, recovery, presentation synchronization |
| Server authority | Lobby flow, bootstrap, simulation ownership, snapshots, corrections |
| Diagnostics and testing | Runtime diagnostics, log capture, test matrix, test methodology |
| Documentation | Architecture writeups, networking explanation, portfolio presentation |

## Why This Project Matters

This project showcases the kind of engineering work relevant to gameplay programming and multiplayer/networking roles: real-time client/server flow, authority boundaries, packet design, debugging under latency and loss, and clear technical communication around a non-trivial system.

## Documentation

- [Architecture](docs/architecture.md)
- [Networking](docs/networking.md)
- [Testing](docs/testing.md)
- [Reference](docs/reference.md)
- [Decisions And Limits](docs/project-notes.md)
- [Devlog](docs/devlog-summary.md)

## Build And Run

```bash
[build-and-run-placeholder]
```

## Current Limitations

- [limitation-placeholder-1]
- [limitation-placeholder-2]
- [limitation-placeholder-3]

## Media And Links

- Demo video: [demo-video-url]
- GitHub: [repo-link]
- Portfolio: [portfolio-link]

[hero-image]: [hero-image-placeholder]
[demo-thumbnail]: [demo-thumbnail-placeholder]
[demo-video-url]: [demo-video-placeholder]
[architecture-diagram]: [architecture-diagram-placeholder]
[repo-link]: [repo-link-placeholder]
[portfolio-link]: [portfolio-link-placeholder]
