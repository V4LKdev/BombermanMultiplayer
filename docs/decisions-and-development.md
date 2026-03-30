# Decisions And Development

[TOC]

This page records the main development decisions, deliberate scope cuts, and the major milestones that shaped the multiplayer extension.

## Key Development Decisions

The project was guided by a few stable decisions:

- keep the multiplayer model authoritative, with the server owning gameplay truth and the client sending intent
- keep the protocol explicit and easy to validate in `NetCommon.h`
- split client transport and prediction, server authority, and multiplayer scene presentation into separate modules
- build diagnostics, logs, and retained test evidence into the project from the start
- extend the provided singleplayer base instead of rewriting the entire game scaffold

These decisions kept the project small enough to finish in a month and explain clearly,
while still supporting a real client-server architecture with a complete multiplayer gameplay loop.

## Development Loop

Most of the multiplayer work followed a repeating loop: 
implement one slice, test it, find the weak points, simplify the boundaries, improve observability, and only then move on.

<div class="theme-image-swap theme-image-swap--small">
  <img class="theme-image-light" src="decisions-development-loop-light.svg" alt="Iterative development loop showing a slice being implemented, tested, refined, instrumented, documented, and then carried into the next slice">
  <img class="theme-image-dark" src="decisions-development-loop-dark.svg" alt="Iterative development loop showing a slice being implemented, tested, refined, instrumented, documented, and then carried into the next slice">
</div>

## Deferred And Cut Features

Some features were left out or cut during development because they would have required a much broader redesign than the assessment needed:

- no mid-match reconnect path
- no live seat reshuffle after players occupy lobby seats
- no matchmaking or server-browser layer
- no full-world mid-match rebuild for late join or reconnect
- no broader identity system beyond the bounded same-name lobby reclaim path
- no full remote-player interpolation / lockstep layer
- no full realtime diagnostics output
- no simple client-hosted model or listen-server path
- no support for dynamic max player counts
- no enemy AI or stages in the multiplayer scene
- no client-side prediction of gameplay events
- no authentication, encryption, or advanced anti-cheat measures

## Known Issues And Tradeoffs

The multiplayer layer is stable for the chosen scope, but it still makes visible tradeoffs:

- harsher impairment profiles still produce noticeable degradation even when the game remains playable
- input lead and snapshot interval are tuning tradeoffs rather than universal wins
- reconnect reclaim is bounded to a lobby-only case
- very long-lived input sequence wraparound is not handled
- the input lead buffer is behaving oddly on higher value settings

This project is feature-complete for its intended scope, not production-hardened for every edge case.

## Development History

The development history is easiest to understand in 5 phases:

<div class="theme-image-swap">
  <img class="theme-image-light" src="decisions-phase-timeline-light.svg" alt="High-level multiplayer development timeline showing foundations, client runtime, gameplay authority, feature lock, and evidence and presentation phases">
  <img class="theme-image-dark" src="decisions-phase-timeline-dark.svg" alt="High-level multiplayer development timeline showing foundations, client runtime, gameplay authority, feature lock, and evidence and presentation phases">
</div>

## What I Would Improve Next

If the project continued beyond the current scope, the next improvements would be:

- broaden identity and seat ownership before attempting true mid-match reconnect
- add a more polished interpolation layer for remote-player presentation
- expand test coverage under harsher impairment, real conditions, and mixed topologies
- continue reducing comment noise and preserving the clearer subsystem boundaries established in the final pass
- review the input lead buffer behaviour and tuning to understand the oddities at higher values

<div class="section_buttons">

| Previous                  | |
|:--------------------------|:|
| [Reference](reference.md) | |

</div>
