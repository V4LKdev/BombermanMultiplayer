# Architecture Notes

## Lobby Player-Id Reassignment

Current dedicated-server behavior supports one narrow reconnect path:

- if a player disconnects and later rejoins with the same name
- and the original `playerId` is still free
- the server restores that same seat and carried win count

This is bounded seat reclaim only. It does not reorder or compact the
remaining lobby seats while other players stay connected.

Current multiplayer lobby seats are keyed directly by authoritative `playerId`, and that same `playerId` is also the active session identity used by:

- `Welcome` / local client identity
- lobby seat ordering and local-seat ownership
- player colors and spawn slots
- match player state, winner ids, bomb ownership, and player masks

Because of that, reassigning `playerId` live in the lobby after a disconnect is not a simple UX tweak. It would require changing the identity/seat model across both protocol and runtime state.

Example:

- `P1`, `P2`, `P3` join
- `P2` disconnects
- remaining players appear as `P1` and `P3`

That is currently accepted as a UX limitation. The correct long-term fix would be to separate:

- stable session/player identity
- lobby presentation order / seat labeling

This was not changed in the current refactor because doing it correctly would be a broader model redesign, not a low-risk cleanup.
