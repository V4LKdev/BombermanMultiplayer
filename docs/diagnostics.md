# Diagnostics

[TOC]

This page covers the observability layer built around the multiplayer system.

Diagnostics are part of the architecture rather than an afterthought.
They exist to make packet flow, authority, prediction, and runtime anomalies visible during debugging and structured testing.

## Diagnostics Goals

The diagnostics layer is built around these goals:

- make multiplayer behaviour visible while the game is running
- retain structured evidence for later comparison and review
- support lightweight live tracing during development and gameplay testing
- support controlled testing under normal and impaired conditions
- stay out of the way for regular gameplay and release builds

## Logs Vs Diagnostics

The project uses two different observability surfaces:

| Surface         | Purpose                                                    | Output                        |
|:----------------|:-----------------------------------------------------------|:------------------------------|
| **Logs**        | readable live tracing during runtime                       | console and optional log file |
| **Diagnostics** | structured retained session data for review and comparison | JSON report files             |

### Logs

Logs are human-readable runtime traces built around `spdlog`, and have been very valuable for early development and debugging.

They are used for things like:

- connection lifecycle
- packet and protocol tracing
- gameplay and simulation events
- diagnostics-related warnings and summaries

The main logging channels include:

- `client`
- `server`
- `game`
- `net.conn`
- `net.packet`
- `net.proto`
- `net.input`
- `net.snapshot`
- `net.diag`

Log behaviour is configured through a combination of:

- `Configs/DefaultLogging.ini`
- `--log-level`
- `--log-file`
- build-time availability for verbose logs

### Diagnostics

Diagnostics are structured JSON session reports.

They are used to retain metrics, compare runs, and review behaviour after a session has ended.

At a high level:

- the server writes `diag_server_<timestamp>.json`
- the client writes `diag_client_*.json`
- reports use a versioned schema
- the diagnostics viewer can load one or two reports for inspection and comparison

\remark
Logs are for readable live tracing.
Diagnostics are for retained metrics, summaries, and comparison across runs.

## Server Diagnostics

Server-side diagnostics are built around `NetDiagnostics.h`.

They record the authoritative side of the session, including:

- session config such as protocol version, tick rate, input lead, snapshot interval, powers enabled, and max players
- packet totals and malformed receive counts
- input continuity metrics such as simulation gaps, recoveries, stale input, and late input
- authoritative gameplay counts such as bombs placed, bricks destroyed, and round outcomes
- recent per-peer transport samples
- recent event history
- current server flow state

This makes the server side of the architecture measurable rather than opaque.
Flow, continuity, and authoritative gameplay outcomes can all be inspected after a run.

## Client Diagnostics

Client-side diagnostics are built around `ClientDiagnostics.h`.

They record the player-facing side of the session, including:

- session config such as protocol version, prediction state, smoothing state, assigned player id, and server tick rate
- connection lifecycle timing
- transport summary values such as RTT, variance, and loss
- prediction and correction behaviour
- replay and recovery behaviour
- stale snapshot or stale correction handling
- recent event history

This is where prediction and smoothing become measurable rather than purely subjective.
The client-side experience can be compared against retained transport and correction data instead of being judged only by feel.

## Sampling And Noise Control

The observability layer is designed to stay useful without becoming spammy.

For that reason, diagnostics and logs include built-in noise control features such as:

- bounded recent-event buffers
- deduplicated repeated events
- sampled high-frequency logging
- centralised tuning values for diagnostics cadence and warning thresholds

Diagnostics are only useful if the important signals remain visible.

## Diagnostics Viewer

The repository includes a lightweight browser-based diagnostics viewer.

It is used to:

- load one or two JSON reports
- show summary metrics and configuration values
- inspect recent event history
- compare a curated set of high-signal metrics side by side

<div class="docs-media docs-media--medium">
  <img src="diagnostics-viewer-single-report-cropped.png" alt="Diagnostics viewer single-report view showing summary metrics and configuration for one retained report">
</div>

<div class="docs-media docs-media--medium">
  <img src="diagnostics-viewer-comparison-table.png" alt="Diagnostics viewer comparison table contrasting two retained reports across selected metrics">
</div>

<div class="docs-action-row">
  <a class="docs-link-button" href="diag_viewer.html" target="_blank" rel="noopener noreferrer">Open Diagnostics Viewer</a>
</div>

\remark
The viewer is focused on the most useful retained signals rather than trying to visualise every raw field in the report.

## Tooling And Impairment

The repository also includes a helper around network impairment: `netem_launcher.sh`.

That tooling is used to:

- apply temporary delay, jitter, or loss during local and LAN testing
- launch the client under controlled impairment
- clean up the temporary network configuration afterwards

On Linux, this is handled through a `tc netem` helper script in the repository.
On Windows, impairment during testing was applied with external tools such as Clumsy.

## Config Surface

Reports record the session configuration such as protocol version, tick rate, 
and prediction and smoothing state in order to allow for easier comparison and debugging.

The main observability-related runtime surface includes:

- `--net-diag`
- `--log-level`
- `--log-file`

Some gameplay-facing toggles also affect how diagnostics should be interpreted:

- `--no-prediction`
- `--no-remote-smoothing`

The full CLI surface is listed in [Reference](reference.md).

### Relevant Files

- `Net/NetDiagnostics.h`
- `Net/NetDiagnostics.cpp`
- `Net/ClientDiagnostics.h`
- `Net/ClientDiagnostics.cpp`
- `Net/NetDiagConfig.h`
- `Net/NetDiagShared.h`
- `Util/Log.h`
- `Util/Log.cpp`
- `Util/CliCommon.h`
- `Configs/DefaultLogging.ini`
- `Tools/diag_viewer.html`
- `Tools/diag_viewer.js`
- `Tools/diag_viewer_launcher.sh`
- `Tools/NetworkTesting/netem_launcher.sh`

<div class="section_buttons">

| Previous              |                      Next |
|:----------------------|--------------------------:|
| [Testing](testing.md) | [Reference](reference.md) |

</div>
