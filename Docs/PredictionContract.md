# Prediction Contract
## Scope

- Local player prediction only.
- Server remains authoritative.
- Remote players remain snapshot-authoritative.
- Bombs, deaths, combat resolution, and remote prediction are out of scope.

## Tick and Input Model

- Client and server both run the fixed simulation tick.
- The client generates one local input sequence per local simulation tick.
- `MsgInput` remains the resend-batched client-to-server input stream.
- The server processes local inputs strictly in sequence order.
- The server keeps a configurable input lead, default `1` tick.
- The server uses a fixed per-client consume lead of `N` ticks instead of a variable reprime/hold policy.
- Later packets may backfill older missing sequences only until that sequence reaches its consume deadline.
- Once a sequence reaches consume deadline, the server either uses the exact input or gaps/falls back and moves on permanently.
- The input lead is scheduling delay only, not rollback.
- Input sequence arithmetic currently assumes a session never approaches `uint32_t` wraparound; long-lived gameflow should reset or rotate the sequence domain before that becomes relevant.

## Ownership

- When prediction is enabled, the client moves the local player immediately.
- The server remains the source of truth for the local player.
- When prediction is enabled, the local player is no longer positioned from `MsgSnapshot`.
- Remote players continue to be positioned from `MsgSnapshot`.

## Message Roles

- `MsgSnapshot`: broadcast world snapshot for remote players and later replicated world state.
- `MsgCorrection`: owner-only authoritative local correction/ack message.
- `MsgCorrection` carries:
  - authoritative server tick
  - highest input sequence the server has processed for that player
  - authoritative local player position at that point

## Reconciliation

- The client stores local input history by sequence.
- The client stores predicted local post-state history by sequence.
- On each newer `MsgCorrection`, the client compares the authoritative state to its predicted state at the acknowledged sequence.
- If they differ, the client resets to the authoritative state and replays all local inputs after that sequence.
- First version reconciles on any real mismatch, with no correction threshold.
- If replay history is incomplete, the client snaps back to the authoritative correction, suspends local prediction, and keeps recording future local inputs.
- While recovery is active, the client's local presentation state is authoritative rather than predicted.
- Prediction resumes only after authoritative corrections have caught up through the unknown input suffix and the retained buffered inputs can be replayed safely again.

## Deferred

- Do not lower snapshot rate until remote smoothing/interpolation exists.
