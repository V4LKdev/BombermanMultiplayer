# Reference

[TOC]

## Overview

This page collects the practical build, run, configuration, and tuning surface of the project in one place.

It covers:
- client and server executables
- build variants and presets
- runtime options
- important tuning knobs and defaults
- logging and diagnostics paths

## Build System

### Executables And Targets

| Executable         | CMake target       | Purpose                                                                    | Presets                                                                                        |
|:-------------------|:-------------------|:---------------------------------------------------------------------------|:-----------------------------------------------------------------------------------------------|
| `Bomberman`        | `Bomberman`        | SDL2 client runtime, menus, multiplayer scene flow, and local presentation | `linux-client-debug`, `linux-client-release`, `windows-client-debug`, `windows-client-release` |
| `Bomberman_Server` | `Bomberman_Server` | Dedicated authoritative server runtime and fixed-tick simulation           | `linux-server-debug`, `linux-server-release`                                                   |

The client and server are built as separate executables.
That split keeps the authoritative server runtime independent from the SDL2 game client.

### Build Variants

| Preset            | Platform              | Build type | Notes                                                                                |
|:------------------|:----------------------|:-----------|:-------------------------------------------------------------------------------------|
| `linux-debug`     | Linux                 | Debug      | Main development preset; enables richer diagnostics and client debug netcode toggles |
| `linux-release`   | Linux                 | Release    | Leaner runtime build for Linux packaging and sanity checks                           |
| `windows-debug`   | Windows (MinGW cross) | Debug      | Cross-compiled Windows client build                                                  |
| `windows-release` | Windows (MinGW cross) | Release    | Cross-compiled Windows client packaging build                                        |

Debug and release builds differ in a few practical ways:

- Debug builds define `SPDLOG_ACTIVE_LEVEL=TRACE` and default to more developer-friendly logging.
- Release builds reduce the active log level and do not carry the same debug-oriented observability surface.
- `--net-diag` is available in client debug builds and server builds, but not in the client release build.
- `--no-prediction` and `--no-remote-smoothing` are client debug options only.

### Build Prerequisites

| Platform                   | Requirements                                                                          |
|:---------------------------|:--------------------------------------------------------------------------------------|
| all builds                 | CMake `3.24+`, Ninja                                                                  |
| Linux client               | GCC `12+` or Clang `15+`, SDL2, SDL2_image, SDL2_ttf, SDL2_mixer development packages |
| Linux server-only          | C++20 compiler; SDL packages are not required if `BOMBERMAN_BUILD_CLIENT=OFF`         |
| Windows client cross-build | MinGW-w64 toolchain plus SDL2 MinGW packages in the configured sysroot                |

All other third-party dependencies are vendored in `third_party/`, including ENet, spdlog, and nlohmann/json.

### CMake Options

| Option                    | Default                   | Meaning                                               |
|:--------------------------|:--------------------------|:------------------------------------------------------|
| `BOMBERMAN_BUILD_CLIENT`  | `ON`                      | Build the SDL2 client target                          |
| `BOMBERMAN_BUILD_SERVER`  | `ON`                      | Build the dedicated server target                     |
| `BOMBERMAN_BUNDLE_DLLS`   | `ON`                      | Bundle MinGW / SDL2 DLLs into Windows client packages |
| `BOMBERMAN_MINGW_SYSROOT` | `/usr/x86_64-w64-mingw32` | MinGW sysroot path used for Windows DLL bundling      |

### CMake Presets

#### Configure Presets

| Preset            | Purpose                                                     |
|:------------------|:------------------------------------------------------------|
| `linux-debug`     | Linux debug configure tree for client and server            |
| `linux-release`   | Linux release configure tree for client and server          |
| `windows-debug`   | Windows debug cross-compile configure tree for the client   |
| `windows-release` | Windows release cross-compile configure tree for the client |

#### Build Presets

| Preset                   | Target             |
|:-------------------------|:-------------------|
| `linux-client-debug`     | `Bomberman`        |
| `linux-client-release`   | `Bomberman`        |
| `linux-server-debug`     | `Bomberman_Server` |
| `linux-server-release`   | `Bomberman_Server` |
| `windows-client-debug`   | `Bomberman`        |
| `windows-client-release` | `Bomberman`        |

### Build And Run

Each configure preset maps to one build directory.
Run `cmake --preset <configure-preset>` once, then build the client or server target from that configured tree as needed.

#### Linux Client

```bash
cmake --preset linux-debug
cmake --build --preset linux-client-debug
./build/linux-debug/Bomberman
```

#### Linux Server

```bash
cmake --preset linux-debug
cmake --build --preset linux-server-debug
./build/linux-debug/Bomberman_Server
```

#### Windows Client Cross-Build

```bash
cmake --preset windows-release
cmake --build --preset windows-client-release
```

#### Useful Configure Overrides

```bash
# Server-only Linux build
cmake --preset linux-debug -DBOMBERMAN_BUILD_CLIENT=OFF

# Client-only build
cmake --preset linux-debug -DBOMBERMAN_BUILD_SERVER=OFF

# Disable Windows DLL bundling
cmake --preset windows-release -DBOMBERMAN_BUNDLE_DLLS=OFF
```

Binaries are written to `build/<configure-preset>/`.

#### Packaging

Run packaging from the relevant build directory after building:

```bash
cd build/linux-release
cpack --config CPackConfig.cmake
```

Generated package formats:

| Build dir               | Package format | Contents                             |
|:------------------------|:---------------|:-------------------------------------|
| `build/linux-release`   | `TGZ`          | client and server component archives |
| `build/windows-release` | `ZIP`          | Windows client package               |

On Windows packages, SDL2 and MinGW runtime DLLs are bundled from `${BOMBERMAN_MINGW_SYSROOT}/bin` when `BOMBERMAN_BUNDLE_DLLS=ON`.

## CLI Options

| Option                              | Applies to     | Purpose                                      | Notes                                                      |
|:------------------------------------|:---------------|:---------------------------------------------|:-----------------------------------------------------------|
| `--help`                            | client, server | Print usage and exit                         | Local to each executable                                   |
| `--port <port>`                     | client, server | Override the default server port             | Valid range: `1-65535`                                     |
| `--log-level <level>`               | client, server | Override the base log level                  | Accepted values: `trace`, `debug`, `info`, `warn`, `error` |
| `--log-file <path>`                 | client, server | Write logs to an explicit file path          | Shared logging option                                      |
| `--net-diag`                        | client, server | Enable structured diagnostics JSON capture   | Build-gated on the client                                  |
| `--mute`                            | client         | Disable audio playback                       | Useful for local multi-instance testing                    |
| `--no-prediction`                   | client         | Disable local player prediction              | Debug build only                                           |
| `--no-remote-smoothing`             | client         | Disable remote-player smoothing              | Debug build only                                           |
| `--seed <value>`                    | server         | Override the match seed                      | Useful for controlled comparisons                          |
| `--input-lead-ticks <value>`        | server         | Override the authoritative input lead buffer | Range: `0-kMaxBufferedInputLead`                           |
| `--snapshot-interval-ticks <value>` | server         | Override snapshot send interval in ticks     | Range: `1-60`                                              |
| `--no-powers`                       | server         | Disable powerup spawning for the session     | Server-side gameplay toggle                                |

## Runtime Tuning Knobs

### Network And Session Tuning

| Knob                                                  | Surface | Default                                 | Purpose                                                       |
|:------------------------------------------------------|:--------|:----------------------------------------|:--------------------------------------------------------------|
| `inputLeadTicks` / `--input-lead-ticks`               | server  | `1`                                     | Buffers authoritative input slightly ahead of consume time    |
| `snapshotIntervalTicks` / `--snapshot-interval-ticks` | server  | `1`                                     | Controls snapshot send cadence (`1` = every tick / 60 Hz)     |
| prediction enabled                                    | client  | on                                      | Enables local player prediction                               |
| remote smoothing enabled                              | client  | on                                      | Smooths remote-player presentation                            |

These are the most important tunables used during testing and A/B comparison work.
Deeper protocol and simulation detail lives in the subsystem pages.

### Gameplay And Round Tuning

| Knob                        | Surface                         | Default                                 | Purpose                                                                 |
|:----------------------------|:--------------------------------|:----------------------------------------|:------------------------------------------------------------------------|
| map seed                    | server / shared map generation  | generated per session unless overridden | Drives deterministic tile map generation on client and server           |
| brick spawn randomize       | shared gameplay config          | `10`                                    | Controls how often grass tiles are promoted to bricks during generation |
| powerups per round          | server / shared gameplay config | `4`                                     | Number of hidden powerups seeded under bricks when enabled              |
| powerup duration            | shared gameplay config          | `10 s`                                  | Default lifetime for temporary multiplayer powerup effects              |
| speed boost movement target | shared gameplay config          | `5.5 tiles/s`                           | Movement speed while the speed boost is active                          |
| bomb range boost amount     | shared gameplay config          | `+1`                                    | Additional blast radius during the range boost                          |
| boosted max bombs           | shared gameplay config          | `2`                                     | Bomb-cap target while the max-bombs boost is active                     |

## Logging And Diagnostics Paths

| Path                                     | Purpose                                                 |
|:-----------------------------------------|:--------------------------------------------------------|
| `Configs/DefaultLogging.ini`             | Default named-channel logging configuration             |
| `logs/`                                  | Runtime output directory for diagnostics JSON reports   |
| `Tools/diag_viewer.html`                 | Browser-based diagnostics viewer shell                  |
| `Tools/diag_viewer.js`                   | Viewer logic for report parsing and comparison          |
| `Tools/diag_viewer_launcher.sh`          | Convenience launcher for opening the diagnostics viewer |
| `Tools/NetworkTesting/netem_launcher.sh` | Linux helper for `tc netem`-based impairment testing    |
| `docs/assets/testing/TestMatrix/`        | Retained test-matrix logs and diagnostics evidence      |

Diagnostics reports are written as:

- `logs/diag_server_<timestamp>.json`
- `logs/diag_client_p<N>_<timestamp>.json`
- `logs/diag_client_u_<timestamp>.json` before player assignment is known

If `--log-file` is not provided, logging follows the default config resolution path and console output setup.

## Important Defaults

### Network Defaults

| Default                      | Value    | Source                 | Meaning                                              |
|:-----------------------------|:---------|:-----------------------|:-----------------------------------------------------|
| Protocol version             | `9`      | `Net/NetCommon.h`      | Handshake compatibility gate                         |
| Default server port          | `12345`  | `Net/NetCommon.h`      | Shared client/server port default                    |
| Max players                  | `4`      | `Net/NetCommon.h`      | Maximum supported gameplay player count              |
| Simulation tick rate         | `60 Hz`  | `Sim/SimConfig.h`      | Fixed-tick client/server simulation basis            |
| Default server input lead    | `1 tick` | `Sim/SimConfig.h`      | Authoritative input scheduling buffer                |
| Default snapshot interval    | `1 tick` | `Sim/SimConfig.h`      | Snapshot every server tick by default                |
| Max frame clamp              | `250 ms` | `Sim/SimConfig.h`      | Fixed-step accumulator clamp                         |
| Max steps per frame          | `8`      | `Sim/SimConfig.h`      | Fixed-step catch-up ceiling                          |
| Server peer session capacity | `8`      | `Server/ServerState.h` | `4` gameplay peers plus `4` overflow transport peers |

Powerups are enabled by default on the server unless `--no-powers` is supplied.

### Gameplay Defaults

| Default                   | Value         | Source                | Meaning                                                        |
|:--------------------------|:--------------|:----------------------|:---------------------------------------------------------------|
| Brick spawn randomize     | `10`          | `Const.h`             | Shared brick-density knob used by deterministic map generation |
| Tile map size             | `31 x 13`     | `Const.h`             | Generated gameplay tile dimensions                             |
| Default player max bombs  | `1`           | `Sim/SimConfig.h`     | Starting bomb capacity                                         |
| Default player bomb range | `1`           | `Sim/SimConfig.h`     | Starting explosion radius                                      |
| Default bomb fuse         | `1.5 s`       | `Sim/SimConfig.h`     | Authoritative bomb lifetime                                    |
| Powerup types             | `4`           | `Sim/PowerupConfig.h` | Speed boost, invincibility, bomb range boost, max bombs boost  |
| Powerups per round        | `4`           | `Sim/PowerupConfig.h` | One hidden placement of each configured powerup type           |
| Default powerup duration  | `10 s`        | `Sim/PowerupConfig.h` | Shared temporary-effect lifetime                               |
| Speed boost target        | `5.5 tiles/s` | `Sim/PowerupConfig.h` | Boosted player movement speed                                  |
| Bomb range boost amount   | `+1`          | `Sim/PowerupConfig.h` | Additional bomb blast radius                                   |
| Boosted max bombs         | `2`           | `Sim/PowerupConfig.h` | Boosted bomb ownership cap                                     |

## Related Pages

- [Networking](networking.md)
- [Diagnostics](diagnostics.md)
- [Testing](testing.md)
- [Architecture](architecture.md)

<div class="section_buttons">

| Previous                      |                                                      Next |
|:------------------------------|----------------------------------------------------------:|
| [Diagnostics](diagnostics.md) | [Decisions And Development](decisions-and-development.md) |

</div>
