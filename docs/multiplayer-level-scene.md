# Multiplayer Level Scene

\defgroup multiplayer_level_scene Multiplayer Level Scene
\brief Client-side multiplayer match orchestration and presentation subsystem.

This subsystem is the client-side runtime for a multiplayer match.

It extends the shared `LevelScene` gameplay scaffold with:

- authoritative networking
- prediction-aware local control
- remote player presentation
- world presentation
- the scene flow around match bootstrap, active play, results, and exit handling

In practice, this is the point where raw network state becomes the playable and visible multiplayer game.

This page covers the layer above client netcode: `NetClient` and `ClientPrediction` provide the data and correction inputs, while `MultiplayerLevelScene` turns them into visible match state.

## Ownership

<div class="theme-image-swap">
  <img class="theme-image-light" src="multiplayer-level-scene-structure-light.svg" alt="Multiplayer level scene code layout showing the scene core, authority merge, presentation slices, telemetry, and its links to NetClient and ClientPrediction">
  <img class="theme-image-dark" src="multiplayer-level-scene-structure-dark.svg" alt="Multiplayer level scene code layout showing the scene core, authority merge, presentation slices, telemetry, and its links to NetClient and ClientPrediction">
</div>

The multiplayer scene is the integration point where authority handling, prediction-aware local control, and presentation logic meet.

## Runtime Flow

<div class="theme-image-swap theme-image-swap--medium">
  <img class="theme-image-light" src="multiplayer-level-scene-runtime-flow-light.svg" alt="Multiplayer level scene runtime flow from frame start through net validation, correction, event collection, authority merge, presentation update, and telemetry">
  <img class="theme-image-dark" src="multiplayer-level-scene-runtime-flow-dark.svg" alt="Multiplayer level scene runtime flow from frame start through net validation, correction, event collection, authority merge, presentation update, and telemetry">
</div>

Each frame applies local correction first, then merges authoritative state, then updates the visible match state and presentation.

## Ownership Rules

- the local player may be driven by prediction
- remote players remain snapshot-driven
- corrections are applied before snapshot and event presentation
- reliable gameplay events are merged in authoritative tick order
- match flow, banners, and scene transitions are handled here

Relevant code:
- `Scenes/MultiplayerLevelScene/`
- `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.cpp`
- `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Authority.cpp`
- `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Presentation.cpp`
- `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.RemotePresentation.cpp`
- `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.WorldPresentation.cpp`
- `Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Telemetry.cpp`

<div class="section_buttons">

| Previous                                                         |                  Next |
|:-----------------------------------------------------------------|----------------------:|
| <a href="group__net__client.html">Client Multiplayer Netcode</a> | [Testing](testing.md) |

</div>