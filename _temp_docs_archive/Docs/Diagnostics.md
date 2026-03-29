# Diagnostics

Current diagnostics output is JSON-only.

## Enabling Reports

- Client: run `Bomberman --net-diag`
- Server: run `Bomberman_Server --net-diag`

## Session Boundary

- Client reports cover one connect attempt through disconnect or failure.
- Disconnecting and reconnecting creates a new client report.
- A continuous server process writes one report when it shuts down cleanly.

## Report Files

- Client reports are written as `logs/diag_client_p<N>_<HHMMSS>.json`
- If a client disconnects before `Welcome`, the filename falls back to `logs/diag_client_u_<HHMMSS>.json`
- Server reports are written as `logs/diag_server_<HHMMSS>.json`

These names are made unique if another report with the same timestamp already exists.

## Viewer

- Open `./Tools/diag_viewer_launcher.sh`
- The viewer reads saved JSON files directly
- The viewer supports one-report overview and two-report comparison

## Report Version

- Both client and server JSON reports include `report_version`
- The current report version is `1`

## Notes

- Recent historical docs under `Docs/NetworkTests/` may still reference archived text artifacts captured before the JSON-only diagnostics workflow.
- Multiplayer client windows use manual presentation pacing instead of renderer vsync so local multi-client testing does not skew input-send timing between processes.
