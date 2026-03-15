# Network Baseline - 2026-03-15

Context
- Recorded before client-side prediction and reconciliation work.
- Project state at capture time:
  - server telemetry MVP in place
  - multiplayer scene split done
  - remote players rendered from snapshots
- Test setup:
  - one local server
  - one local client
  - Linux `tc netem` launcher on loopback (`lo`)

Saved reports
- `server_diag_control.txt`
- `server_diag_delay100.txt`
- `server_diag_delay200.txt`
- `server_diag_delay120_jitter40.txt`
- `server_diag_delay120_jitter30_loss2.txt`

Scenario notes
- `server_diag_control.txt`
  - Impairment: none
  - Feel: instant, smooth, effectively singleplayer-like
  - Report: only a few late input packets and gaps; clean baseline
- `server_diag_delay100.txt`
  - Impairment: 100 ms delay
  - Feel: smooth but clearly delayed local movement response
  - Report: RTT increased as expected; late input packets and gaps rose noticeably
- `server_diag_delay200.txt`
  - Impairment: 200 ms delay
  - Feel: much worse local response delay, still consistent
  - Report: RTT roughly doubled again; latency pain very visible without prediction
- `server_diag_delay120_jitter40.txt`
  - Impairment: 120 ms delay, 40 ms jitter
  - Feel: movement became uneven and jittery
  - Report: jitter produced less consistent response and noisier gap behavior
- `server_diag_delay120_jitter30_loss2.txt`
  - Impairment: 120 ms delay, 30 ms jitter, 2% loss
  - Feel: worst of the set; snappier and more erratic under loss
  - Report: light packet loss amplified late input packets and hold/gap behavior

Comparison summary
- The impairment workflow is behaving credibly:
  - added delay maps cleanly to higher RTT
  - added jitter shows up in RTT variance and uneven movement feel
  - light loss makes the current no-prediction path much worse
- The current replication baseline is now easy to demonstrate:
  - delay hurts responsiveness
  - jitter hurts consistency
  - loss amplifies hold/gap behavior
- The server diagnostics are useful enough as a before-prediction baseline.
- One remaining clarity issue is event density:
  - exact counters stay useful
  - repeated gap and late-packet events can dominate the recent-event ring under impairment
