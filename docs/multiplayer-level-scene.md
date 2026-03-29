# Multiplayer Level Scene

\defgroup multiplayer_level_scene Multiplayer Level Scene
\brief Client-side multiplayer match orchestration and presentation subsystem.

This subsystem is the client-side runtime for a multiplayer match. 
It extends the shared `LevelScene` gameplay scaffold with:
 - authoritative networking
 - prediction-aware local control
 - remote player 
 - world presentation 
 - the scene flow around match bootstrap, active play, results, and exit handling

In practice, this is the point where raw network state becomes the playable and visible multiplayer game.

---
## Structure

<div class="theme-image-swap">
  <img class="theme-image-light" src="multiplayer-level-scene-structure-light.svg" alt="Multiplayer Level Scene structure">
  <img class="theme-image-dark" src="multiplayer-level-scene-structure-dark.svg" alt="Multiplayer Level Scene structure">
</div>

## Runtime Flow

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="multiplayer-level-scene-runtime-flow-light.svg" alt="Multiplayer Level Scene runtime flow">
  <img class="theme-image-dark" src="multiplayer-level-scene-runtime-flow-dark.svg" alt="Multiplayer Level Scene runtime flow">
</div>

---
## Key Rules

- The local player may be driven by prediction.
- Remote players remain snapshot-driven.
- Corrections are applied before snapshot and event presentation.
- Reliable gameplay events are merged in authoritative tick order.
- Match flow, banners, and scene transitions are handled here.
