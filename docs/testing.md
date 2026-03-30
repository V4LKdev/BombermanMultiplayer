# Testing

[TOC]

This page presents the structured testing pass used to validate the multiplayer networking layer in normal play, 
under impairment, and across a small range of runtime tuning options.

It shows the networking behaviour tested deliberately, observed directly, and compared under controlled conditions.

## Testing Goals

The testing pass was built around four goals:

- verify that the core **client-server round flow** works reliably from lobby bootstrap through active play, round end, and return to lobby
- confirm that **authoritative state** remains usable under local, LAN, and impaired conditions
- compare **player-experience features** such as prediction, smoothing, input lead, and snapshot rate using concrete diagnostics data
- retain and visualise clear evidence in logs and JSON diagnostics

## Test Setup

Testing was conducted across a range of conditions, configurations, and platforms:

- localhost baseline testing
- LAN testing on the same box and across separate machines
- cross-platform LAN testing with Linux and Windows clients
- real remote play through a cloud-hosted dedicated server
- impairment testing using both `tc netem` and `Clumsy` on loopback and Ethernet
- A/B comparisons using CLI toggles for prediction, smoothing, input lead, and snapshot interval

The most relevant CLI options for the testing pass:

| Option                      | Description                                                        |
|:----------------------------|:-------------------------------------------------------------------|
| `--net-diag`                | enables diagnostic reporting as JSON exports                       |
| `--no-prediction`           | disables client-side prediction of local player movement           |
| `--no-remote-smoothing`     | disables client-side smoothing of remote player movement           |
| `--input-lead-ticks`        | sets the server-side input lead buffer in ticks (default `1`)      |
| `--snapshot-interval-ticks` | sets the server-side snapshot send interval in ticks (default `1`) |
| `--mute`                    | helps my ears when running multiple clients on the same machine    |

\note
This page focuses on outcomes and retained evidence.
The diagnostics pipeline itself is covered in [Diagnostics](diagnostics.md),
while the protocol and authority model are covered in [Networking](networking.md).

## Test Matrix

The test matrix is the living ledger for the testing pass.
It tracks what was tested, what was expected, what was observed, and what retained evidence supports the result.

### Curated Scenarios

| Scenario                           | Purpose                                                                      | Result                                                                                      |
|------------------------------------|------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------|
| 2-client normal round flow         | proves the baseline client-server loop from lobby through end-of-round       | passed cleanly on localhost with full round completion and return to lobby                  |
| Prediction on vs off               | demonstrates the clearest player-facing effect of the client netcode         | under delay, prediction-on remained responsive while prediction-off felt visibly delayed    |
| Diagnostics export and viewer load | proves observability is a functioning part of the workflow                   | JSON reports exported correctly and the viewer loaded them without error                    |
| Cross-platform LAN sanity          | proves the project was exercised outside one local-process topology          | Linux server/client plus Windows client completed a full round over LAN                     |
| Input lead comparison              | demonstrates deliberate buffering and tuning work rather than fixed defaults | `lead=1` gave the best tested balance of continuity and responsiveness                      |
| Snapshot interval comparison       | demonstrates measured bandwidth versus continuity tradeoffs                  | lower snapshot rates reduced traffic, but higher intervals degraded continuity under stress |

\note
The full matrix, including original test ids, artefact paths, and notes, is retained in the appendix below for traceability.

## Representative Results

This section keeps the strongest retained findings in the main reading path, with emphasis on player-facing behaviour, tuning tradeoffs, and diagnostic value.

### Lag Compensation And A/B Testing

Prediction and smoothing are deliberately exposed as runtime toggles so they can be compared under the same conditions.

The clearest qualitative result comes from the prediction comparison:

- with prediction enabled, the owning player remains immediately controllable under added delay
- with prediction disabled, the same delayed setup feels visibly sluggish and becomes effectively unplayable at higher impairment levels
- smoothing improves remote motion presentation, but remains subordinate to authoritative updates rather than replacing them

> Placeholder: visual comparing prediction on vs off under added delay.
>
> Placeholder: visual comparing remote smoothing on vs off.

### Input Lead Buffer

Input lead is tested as an explicit server-side tuning control.
Across the retained impairment comparisons, `--input-lead-ticks 1` remains the best current setting.

The strongest retained comparison shows:

- `lead=0` was clearly underbuffered under the tested impairment tier, causing many simulation gaps and correction mismatches
- `lead=1` reduced simulation gaps sharply and produced much fewer correction mismatches
- exploratory `lead=3` remained functional, but felt worse than `lead=1` and still showed more continuity problems than the preferred setting

Selected retained values from the archived diagnostics:

| Input lead | Server simulation gaps | Remote correction mismatches | Interpretation                           |
|-----------:|-----------------------:|-----------------------------:|------------------------------------------|
|        `0` |                 `2378` |                        `500` | underbuffered under this impairment tier |
|        `1` |                  `382` |                         `41` | best current tested balance              |
|        `3` |                  `651` |                         `73` | functional but worse than `lead=1`       |

That makes `lead=1` the current best-known setting for the tested impairment profile.

\warning
This outcome appears to be illogical and deserves a follow-up sanity check to confirm the result and rule out any test setup or measurement issues.
A higher lead should not cause more simulation gaps or correction mismatches than a lower lead.

> Placeholder: visual comparing `input lead = 0, 1, 3`.
>
> Placeholder: diagnostics viewer screenshot for the input-lead comparison.

### Snapshot Send Rate

Snapshot interval testing shows a clear tradeoff between bandwidth and gameplay continuity.
This is one of the strongest numeric comparisons in the project because the effect appears in both measured traffic and play quality.

Current conclusion:

- `snapshot interval = 1` gives the cleanest continuity
- `2` reduces traffic by about `35.7%` but causes more simulation gaps
- `3` remains acceptable only in lighter conditions and introduces noticeably worse continuity
- raising the interval reduces traffic, but under harsher impairment it also increases simulation gaps and correction mismatches

The strongest retained A/B comparison showed:

| Snapshot interval | Remote receive traffic | Remote receive attempts | Server simulation gaps | Remote correction mismatches | Interpretation                   |
|------------------:|-----------------------:|------------------------:|-----------------------:|-----------------------------:|----------------------------------|
|               `1` |           `5473.7 B/s` |                `79.0/s` |                  `125` |                          `2` | best continuity                  |
|               `2` |           `3520.5 B/s` |                `65.2/s` |                  `677` |                          `9` | lower traffic, worse continuity  |
|               `3` |           `2630.2 B/s` |                `50.1/s` |                 `1549` |                         `29` | lowest traffic, worst continuity |

For the harsher tested profile, `snapshot interval = 2` is the current best tradeoff between bandwidth reduction and acceptable continuity.

> Placeholder: visual comparing snapshot interval `1` vs `2` vs `3`.

### Capacity And Topology Coverage

The retained evidence covers a range of configurations, including:

- localhost baseline multiplayer flow
- reconnect and disconnect scenarios
- four-player capacity play
- Linux and Windows LAN play
- a real remote three-player session through a cloud-hosted dedicated server

The strongest retained LAN run showed:

- full round completion across Linux and Windows clients
- no visible seat corruption or flow break
- server diagnostics with `simulation_gaps = 0`
- Windows client average RTT around `8 ms`

> Placeholder: diagnostics viewer or log evidence for the LAN / internet test runs.

## Limits And Confidence

The testing pass gives strong confidence in the core networking goals:

- authoritative round flow works from lobby bootstrap through end-of-match return
- gameplay-relevant state stays synchronised across multiple clients
- diagnostics and logs make network behaviour observable and comparable
- prediction, buffering, and snapshot controls can be evaluated with retained evidence rather than guesswork

At the same time, this is still a small multiplayer extension rather than a production-hardened online game.
The remaining limits are mostly about edge-case handling and degradation tolerance rather than the basic client-server model.

Known limits:

- no mid-match reconnect support
- stronger impairment can still produce visible degradation and occasional dropped gameplay intent
- buffering and snapshot choices remain tradeoffs rather than universally best values

### Relevant Files

- `Net/NetDiagnostics.h`
- `Net/NetDiagnostics.cpp`
- `Net/ClientDiagnostics.h`
- `Net/ClientDiagnostics.cpp`
- `Net/NetDiagConfig.h`
- `Net/NetDiagShared.h`

<div class="section_buttons">

| Previous                                                                    |                          Next |
|:----------------------------------------------------------------------------|------------------------------:|
| <a href="group__multiplayer__level__scene.html">Multiplayer Level Scene</a> | [Diagnostics](diagnostics.md) |

</div>

---
## Appendix: Full Matrix

This appendix keeps the full archived matrix with original test ids, evidence paths, and notes for assessment traceability.

Each row tracks one scenario: what was tested, what was expected, what was observed, and what artifacts prove it.

### Core Flow

| ID  | Scenario                   | Config                                                         | Expected                                                                                                                                                                                         | Observed                                                                                                                                                                                                             | Evidence                                                                                                                                                                                                                                                                                                              | Status | Notes                                                                                                                                                                                                                                    |
|-----|----------------------------|----------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| T01 | 2-client normal round flow | Server + 2 clients, localhost, no delay                        | Lobby -> countdown -> match start -> play -> death/explosion -> winner declared -> end-of-match screen -> return to lobby                                                                        | 2-client localhost round completed through lobby, countdown, match start, explosion/death, winner, end-of-match, and return to lobby.                                                                                | `T01_debug_server.log`; `T01_debug_clientA.log`; `T01_debug_clientB.log`; `T01_debug_server_diag.json`; `T01_debug_clientA_diag.json`; `T01_debug_clientB_diag.json`                                                                                                                                                  | Pass   | Localhost on port 46001. `clientA` = player 0, `clientB` = player 1. Audio muted.                                                                                                                                                        |
| T02 | Second round reset         | T01 then immediately ready up again                            | Fresh round bootstrap: clean undamaged map generated from the new seed; positions, alive state, active bombs, temporary effects, and round-local state reset correctly; wins persist as intended | Two consecutive rounds completed in one session. Round 2 started from a clean bootstrap with reset round-local state, and win counts persisted correctly across the lobby return.                                    | `T02_debug_server.log`; `T02_debug_clientA.log`; `T02_debug_clientB.log`; `T02_debug_server_diag.json`; `T02_debug_clientA_diag.json`; `T02_debug_clientB_diag.json`                                                                                                                                                  | Pass   | Localhost on port 46002. `clientA` = player 0, `clientB` = player 1. Audio muted.                                                                                                                                                        |
| T03 | Lobby reconnect reclaim    | 2 clients, one disconnects in lobby, reconnects with same name | Reconnecting client reclaims same player ID and win count; lobby state broadcast reflects reclaim                                                                                                | A lobby-disconnected client reconnected with the same name and reclaimed the same seat/player ID and preserved win count. An additional same-process lobby reconnect also preserved the same reclaimed state.        | `T03_debug_server.log`; `T03_debug_clientA.log`; `T03_debug_clientB_initial.log`; `T03_debug_clientB_reconnect.log`; `T03_debug_server_diag.json`; `T03_debug_clientA_diag.json`; `T03_debug_clientB_initial_diag.json`; `T03_debug_clientB_reconnect_diag.json`; `T03_debug_clientB_sameprocess_reconnect_diag.json` | Pass   | Localhost on port 46003. `clientA` = player 0, `clientB` = player 1. Audio muted. Server logged two reclaim events for `T03B`.                                                                                                           |
| T04 | Mid-match disconnect       | 2 clients, one disconnects during active match                 | If the disconnect leaves the round decided, the server resolves win/end-of-match immediately; no extra gameplay tick changes the result; transition back to lobby remains clean                  | A mid-match disconnect that left one remaining player caused the server to resolve the round immediately, show end-of-match cleanly, and return to lobby without extra gameplay.                                     | `T04_debug_server.log`; `T04_debug_clientA.log`; `T04_debug_clientB.log`; `T04_debug_server_diag.json`; `T04_debug_clientA_diag.json`; `T04_debug_clientB_diag.json`                                                                                                                                                  | Pass   | Localhost on port 46004. `clientA` = player 0, `clientB` = player 1. Audio muted. Client B was closed during active match while both players were alive.                                                                                 |
| T05 | Lobby countdown cancel     | 3 clients ready, one un-readies before countdown expires       | Countdown cancels; no match starts; players that remain ready stay ready                                                                                                                         | With 3 clients, the lobby countdown started and then canceled when one participant unreadied before expiry; no match started. The unreadied player returned to not-ready while the other ready players stayed ready. | `T05_debug_server.log`; `T05_debug_clientA.log`; `T05_debug_clientB.log`; `T05_debug_clientC.log`; `T05_debug_server_diag.json`; `T05_debug_clientA_diag.json`; `T05_debug_clientB_diag.json`; `T05_debug_clientC_diag.json`                                                                                          | Pass   | Localhost on port 46005. `clientA` = player 0, `clientB` = player 1, `clientC` = player 2. Audio muted. Ready-state persistence for still-ready players appears intentional; full ready reset is associated with leave/disconnect paths. |

### Impairment And Netcode

| ID  | Scenario                        | Config                                                                                  | Expected                                                                                                                                      | Observed                                                                                                                                                                                                                                                | Evidence                                                                                                                                                                                               | Status      | Notes                                                                                                                                                                                                                                                                           |
|-----|---------------------------------|-----------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| T06 | Prediction enabled vs disabled  | Server + 2 clients, localhost, 100 ms loopback delay; one client prediction on, one off | Under added delay, the prediction-enabled client should remain responsive while the no-prediction client shows clearly delayed local movement | Under 100 ms loopback delay, the prediction-enabled client remained responsive while the no-prediction client showed clearly delayed local movement. The difference was clearly visible in the same round.                                              | `T06_debug_server.log`; `T06_debug_clientA_pred_on.log`; `T06_debug_clientB_pred_off.log`; `T06_debug_server_diag.json`; `T06_debug_clientA_pred_on_diag.json`; `T06_debug_clientB_pred_off_diag.json` | Pass        | Localhost on port 46006. `clientA` = player 0 with prediction on, `clientB` = player 1 with prediction off. Audio muted. Loopback impairment affected server and both clients equally.                                                                                          |
| T07 | 100 ms simulated delay          | Server + 2 clients, 100 ms one-way delay                                                | Match playable; prediction masks delay for local player; remote players interpolate smoothly; no desyncs                                      | Under 100 ms loopback delay, the match remained playable, local movement stayed workable, and remote motion looked smooth enough overall, but desync was still observed subjectively during the run.                                                    | `T07_debug_server.log`; `T07_debug_clientA.log`; `T07_debug_clientB.log`; `T07_debug_server_diag.json`; `T07_debug_clientA_diag.json`; `T07_debug_clientB_diag.json`                                   | Investigate | Localhost on port 46007. Audio muted. Loopback impairment affected server and both clients equally. Follow up on the reported desync before treating this scenario as fully passed.                                                                                             |
| T08 | 120 ms delay + jitter           | Server + 2 clients, 120 ms + 20 ms jitter                                               | Match completes; corrections applied visibly but without snap; diagnostics show correction events                                             | Under 120 ms loopback delay with 20 ms jitter, the match remained playable and completed. Corrections stayed mostly minor, with only very minor snap/teleport artifacts, but a real stale-brick client-state issue appeared on several destroyed tiles. | `T08_debug_server.log`; `T08_debug_clientA.log`; `T08_debug_clientB.log`; `T08_debug_server_diag.json`; `T08_debug_clientA_diag.json`; `T08_debug_clientB_diag.json`                                   | Investigate | Localhost on port 46008. Audio muted. Loopback impairment affected server and both clients equally. Real issue found: some destroyed bricks remained visible on one client while both players could walk through them, indicating stale visual/tile state under higher latency. |
| T09 | 120 ms delay + jitter + 3% loss | Server + 2 clients, 120 ms + 20 ms jitter + 3% packet loss                              | Match completes; unreliable snapshots/corrections recover; no hang or desync; reliable game events all land                                   | Under 120 ms loopback delay with 20 ms jitter and 3% loss, the round did complete and return cleanly, but play bordered on unplayable. Valid bomb presses were observed to drop, desync was visible, and the stale-brick issue became more pronounced.  | `T09_debug_server.log`; `T09_debug_clientA.log`; `T09_debug_clientB.log`; `T09_debug_server_diag.json`; `T09_debug_clientA_diag.json`; `T09_debug_clientB_diag.json`                                   | Fail        | Localhost on port 46009. Audio muted. Loopback impairment affected server and both clients equally. Reliable match flow completed, but gameplay quality/regression threshold was not met under this stress case.                                                                |

### Tooling And Build

| ID  | Scenario                           | Config                                          | Expected                                                                                                                            | Observed                                                                                                                                                                                                                                               | Evidence                                                                                                                                                             | Status | Notes                                                                                                                                           |
|-----|------------------------------------|-------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| T10 | Diagnostics export and viewer load | Any completed session with `--net-diag` enabled | `diag_server_*.json` written on exit; `diag_viewer.html` loads file locally; summary and recent-events content render without error | The local diagnostics viewer opened successfully, loaded archived server diagnostics JSON, and rendered summary/recent-events content without error. A low-activity report loaded cleanly, and a fuller earlier gameplay report was also spot-checked. | `T05_debug_server_diag.json`; `Tools/diag_viewer.html`                                                                                                               | Pass   | Current viewer renders stats and recent events as text; it does not currently present a graphical timeline despite the original matrix wording. |
| T11 | Debug build sanity                 | Debug binary, Linux, server + 2 clients         | Builds cleanly; assertions active; full log output; round completes without crash or assert                                         | Build passed; full round completed and returned to lobby with no inconsistencies, crash, or assert.                                                                                                                                                    | `T11_debug_server.log`; `T11_debug_clientA.log`; `T11_debug_clientB.log`; `T11_debug_server_diag.json`; `T11_debug_clientA_diag.json`; `T11_debug_clientB_diag.json` | Pass   | Localhost on port 46011. `clientA` = player 0, `clientB` = player 1.                                                                            |
| T12 | Release build sanity               | Release binary, Linux, server + 2 clients       | Builds cleanly; runs without debug overhead; round completes correctly; log output matches expected level                           | Build passed; full round completed and returned to lobby with no crash or unexpected behavior.                                                                                                                                                         | `T12_release_server.log`; `T12_release_clientA.log`; `T12_release_clientB.log`; `T12_release_server_diag.json`                                                       | Pass   | Localhost on port 46012. Release client build does not expose `--net-diag`.                                                                     |

### Capacity And Variant Coverage

| ID  | Scenario                                  | Config                                                                                       | Expected                                                                                                                                 | Observed                                                                                                                                                                                                 | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                  | Status | Notes                                                                                                                                                                                             |
|-----|-------------------------------------------|----------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| T13 | 4-player capacity round                   | Server + 4 clients, localhost                                                                | All 4 seats assign cleanly, lobby/readies stay consistent, match starts, and a 4-player round completes without seat corruption or crash | All 4 supported seats assigned cleanly, the 4-player lobby stayed consistent, the match started correctly, and one full round completed without crash or seat corruption.                                | `T13_debug_server.log`; `T13_debug_clientA.log`; `T13_debug_clientB.log`; `T13_debug_clientC.log`; `T13_debug_clientD.log`; `T13_debug_server_diag.json`; `T13_debug_clientA_diag.json`; `T13_debug_clientB_diag.json`; `T13_debug_clientC_diag.json`; `T13_debug_clientD_diag.json`                                                                                                                                      | Pass   | Localhost on port 46013. `clientA` = player 0, `clientB` = player 1, `clientC` = player 2, `clientD` = player 3. Audio muted. Used self-elimination on three clients to finish the round quickly. |
| T14 | Overflow admission / `ServerFull`         | 4 seats already assigned, then a 5th client attempts `Hello`                                 | Extra client receives explicit `ServerFull` reject; existing lobby or match state remains unchanged                                      | With all 4 seats occupied, the extra client received an explicit full-server reject and did not disturb the existing lobby state. A same-process retry by the overflow client was also rejected cleanly. | `T14_debug_server.log`; `T14_debug_clientA.log`; `T14_debug_clientB.log`; `T14_debug_clientC.log`; `T14_debug_clientD.log`; `T14_debug_clientE_overflow.log`; `T14_debug_server_diag.json`; `T14_debug_clientA_diag.json`; `T14_debug_clientB_diag.json`; `T14_debug_clientC_diag.json`; `T14_debug_clientD_diag.json`; `T14_debug_clientE_overflow_diag.json`; `T14_debug_clientE_overflow_retry_diag.json`              | Pass   | Localhost on port 46014. Audio muted. Overflow client attempted to join after all 4 seats were already assigned and was rejected cleanly without perturbing the existing lobby.                   |
| T15 | Mid-match join / reclaim unsupported path | Active match in progress, then a new client or previously disconnected name tries to connect | Unsupported mid-match join/reclaim is handled cleanly and does not perturb the active match or soft-lock the flow                        | During an active match, both a fresh join attempt and a same-name reclaim attempt were rejected with game-in-progress, and the running match continued and completed cleanly.                            | `T15_debug_server.log`; `T15_debug_clientA.log`; `T15_debug_clientB.log`; `T15_debug_clientC.log`; `T15_debug_clientD_midjoin.log`; `T15_debug_clientE_reclaim.log`; `T15_debug_server_diag.json`; `T15_debug_clientA_diag.json`; `T15_debug_clientB_diag.json`; `T15_debug_clientC_diag.json`; `T15_debug_clientD_midjoin_diag.json`; `T15_debug_clientE_reclaim_diag.json`; `T15_debug_clientE_reclaim_retry_diag.json` | Pass   | Localhost on port 46015. Audio muted. New mid-match join and lobby-style reclaim were both rejected while the active match continued.                                                             |
| T16 | Fixed-seed repeatability                  | Server started with `--seed <n>` across repeated rounds or fresh boots                       | The same seed reproduces the same initial map layout and bootstrap state each time it is reused                                          | With server seed 424242 fixed, consecutive rounds in the same session reproduced the same initial map layout.                                                                                            | `T16_debug_server.log`; `T16_debug_clientA.log`; `T16_debug_clientB.log`; `T16_debug_server_diag.json`; `T16_debug_clientA_diag.json`; `T16_debug_clientB_diag.json`                                                                                                                                                                                                                                                      | Pass   | Localhost on port 46016. Fixed server seed 424242. Audio muted. Layout comparison was observed live; no screenshots were saved for this run.                                                      |
| T17 | No-powers mode                            | Server started with `--no-powers`; destroy many bricks                                       | No powerups reveal or apply; gameplay otherwise remains normal; diagnostics/config show powers disabled                                  | With `--no-powers` enabled, many bricks were destroyed without revealing any powerups, no pickups applied, and the round otherwise played normally.                                                      | `T17_debug_server.log`; `T17_debug_clientA.log`; `T17_debug_clientB.log`; `T17_debug_server_diag.json`; `T17_debug_clientA_diag.json`; `T17_debug_clientB_diag.json`                                                                                                                                                                                                                                                      | Pass   | Localhost on port 46017. Server started with `--no-powers`. Audio muted.                                                                                                                          |

### LAN And Cross-Platform

| ID  | Scenario                                         | Config                                                                                                                                                                                                                                                                                                         | Expected                                                                                                                                                                                   | Observed                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | Evidence                                                                                                                                                                     | Status | Notes                                                                                                                                                                                                                                                                                                                                                      |
|-----|--------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| T19 | LAN baseline cross-platform sanity               | Main Linux box: debug server + clean local debug client; one clean Windows client over real LAN; no impairment; default port; audio muted on both clients                                                                                                                                                      | Full lobby → match → end-of-match → back-to-lobby flow succeeds over real LAN; Windows client connects and renders correctly                                                               | Clean real-LAN cross-platform sanity passed. The Windows client connected, rendered, and played through one full round cleanly; the round ended in a draw and both clients returned to the lobby without visible issues. Server diagnostics showed zero simulation gaps; local Linux prediction stayed clean with zero mismatches; the Windows client had only one tiny correction mismatch (`maxCorrectionDeltaQ=6`) and otherwise remained stable.                                                                         | `docs/assets/testing/TestMatrix/TestMatrix03_26/T19-A1/`                                                                                                                     | Pass   | This was a 2-client cross-platform LAN sanity run using the main Linux box plus one Windows machine, not the originally noted Linux-laptop-plus-Windows-laptop pairing. Networking stayed effectively clean: server `simulation_gaps=0`, Windows avg RTT `8 ms`, and no loss was observed.                                                                 |
| T20 | Input lead A/B under real LAN impairment         | Main Linux box: server + clean local debug client; one remote Linux debug client impaired via `tc netem delay 60ms 16ms`; compared `--input-lead-ticks 0`, `1`, and exploratory `3` on the default port and default snapshot interval                                                                          | Lead=1 visibly smoother than lead=0; fewer corrections and dropped inputs; minimal added input delay; no correctness regressions                                                           | Under real LAN impairment on the remote Linux client, `lead=1` was clearly better than `lead=0`: server gap warnings dropped sharply, server-side simulation gaps fell from `2378` to `382` (about `-83.9%`), and remote client correction mismatches fell from `500` to `41` (about `-91.8%`). Exploratory `lead=3` remained functional but felt worse than `lead=1` and regressed remote mismatches to `73` while still not eliminating server-side gaps.                                                                  | `docs/assets/testing/TestMatrix/TestMatrix03_26/T20-A1/`; `docs/assets/testing/TestMatrix/TestMatrix03_26/T20-A2/`; `docs/assets/testing/TestMatrix/TestMatrix03_26/T20-A3/` | Pass   | Partial coverage only: this was a Linux-only LAN comparison, not the full Linux+Windows topology originally planned. Current best-known setting for this impairment tier is `lead=1`; `lead=0` underbuffers and `lead=3` appears overbuffered.                                                                                                             |
| T21 | Stale-brick regression check on LAN              | Main Linux box: server + clean local debug client; remote Linux debug client impaired with `tc netem delay 60ms 16ms`; Windows client exercised with several `Clumsy` profiles in a long LAN session, ending with one clearly impaired but still playable 3-client round                                       | Destroyed bricks never remain visible/solid on any client; prediction stays consistent across platforms                                                                                    | Stale bricks did not reproduce in the kept LAN report. Across the selected 3-client round, brick destruction stayed visually and physically consistent on Linux and Windows even under obvious impairment. Several harsher Windows impairment passes caused disconnects or very rough play, but the retained report still completed a playable round with visible impairment, no stale-brick mismatch, and only the already-known occasional dropped bomb commands under stress.                                             | `docs/assets/testing/TestMatrix/TestMatrix03_26/T21-A1/`                                                                                                                     | Pass   | This closes the stale-brick LAN follow-up for the currently tested profiles. The old stale-brick symptom from `T08`/`T09` did not reappear here. Windows impairment can still provoke disconnects or dropped bomb commands under harsher `Clumsy` settings, but that is separate from the stale-brick issue.                                               |
| T22 | Snapshot interval tradeoff on LAN                | Main Linux box: debug server + clean local debug client; remote Linux debug client impaired with `tc netem delay 60ms 16ms`; Windows client additionally stressed with `Clumsy` 50 ms inbound/outbound delay and 5% drop; tested `--snapshot-interval-ticks 2` with `--input-lead-ticks 1` on the default port | Higher snapshot interval remains playable and smooth enough on all tested clients; no new desync, stale-brick, or event-stream failures; diagnostics stay coherent under mixed impairment  | `snapshot_interval=2` remained acceptable in the mixed Linux+Windows LAN setup. Local Linux stayed effectively perfect (`corrections_mismatched=0`), the impaired remote Linux client remained smooth with only `5` mismatches, and the Windows client stayed playable despite much harsher impairment (`avg_rtt_ms≈128.6`, `avg_loss_permille≈1454`, `corrections_mismatched=4`). Server-side buffering remained active (`buffered_deadline_recoveries=693`) with one clean round completion and no stale-brick recurrence. | `docs/assets/testing/TestMatrix/TestMatrix03_26/T22-A1/`                                                                                                                     | Pass   | This is a practical acceptability check, not a tightly controlled A/B against `snapshot_interval=1` with fixed seed and identical traffic. The observed tradeoff was slightly more inter-client delay, but motion stayed mostly smooth and no new correctness regressions appeared.                                                                        |
| T23 | Controlled snapshot interval A/B on 2-client LAN | Main Linux box: debug server + clean local debug client; one remote Linux debug client impaired with `tc netem delay 80ms 16ms loss 5%`; fixed `--seed 12345`; compared `--snapshot-interval-ticks 1` vs `2` with `--input-lead-ticks 1`                                                                       | Similar round shape and duration; compare normalized traffic and prediction quality directly; determine whether lower snapshot rate is worth the quality tradeoff under harsher impairment | With near-matched remote play time (`29.44s` vs `29.30s`), raising the snapshot interval from `1` to `2` reduced remote receive traffic from `5473.7 B/s` to `3520.5 B/s` (about `-35.7%`) and receive attempts from `79.0/s` to `65.2/s` (about `-17.5%`). The tradeoff was worse input continuity in the `snapshot=2` run: server simulation gaps rose from `125` to `677`, and remote correction mismatches rose from `2` to `9`.                                                                                         | `docs/assets/testing/TestMatrix/TestMatrix03_26/T23-A1/`; `docs/assets/testing/TestMatrix/TestMatrix03_26/T23-A2/`                                                           | Pass   | This is the cleaner traffic comparison than `T22` because topology, seed, and impairment were controlled. `snapshot=2` clearly saves traffic, but under this harsher profile `snapshot=1` produced better gameplay continuity. The `T23-A1` remote client log was not retained, but the diagnostics are present and sufficient for the traffic comparison. |

---

Retained logs and diagnostics are stored under:

- `docs/assets/testing/TestMatrix/`
