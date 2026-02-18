# Bomberman Multiplayer – Dev Log

## 2026-02-18 – Initial Linux setup + movement bugfix

### Goal
Get the provided Bomberman repo building + running on Arch Linux (CLion), then fix any platform/runtime blockers.

### Build/IDE setup
- Replaced legacy CMakeLists with modern target-based CMake using pkg-config for SDL2 libs.
- Configured CLion run working directory to project root so assets load correctly.

### Bug: Player/enemies don’t move
**Symptom**
- Menu works, bombs spawn, sprite flips on input, but player/enemies don’t translate.

**Diagnosis**
- Per-frame movement delta was quantized to an `int` using `floor()`.
- On Linux with high FPS, `delta` values were small (~1–4 ms), making movement < 1 px/frame and always floored to 0.

**Fix**
- Switched movement/positions to float/sub-pixel and only rounded for rendering.
- Normalized speed to pixels/sec (or tiles/sec) and converted delta to seconds with a clamp.

**Result**
- Frame-rate independent movement; consistent across platforms.
- Game runs correctly from CLion and terminal.

### Repo hygiene
- Added proper .gitignore for CLion/CMake.
- Removed Windows-only artifacts (x64/, packages/, .sln/.vcxproj) from version control.

## 2026-02-18 – Cleanup + Collision Refactor

### Goal
Stabilize core runtime behavior and refactor collision handling for correctness and future networking.

### Engine safety + ownership
- Added scene safety guards to avoid null deref when no scene is active.
- Clamped `Scene::insertObject` indices to prevent out-of-range inserts.
- Switched `SceneManager` + `AssetManager` to `std::unique_ptr` ownership in `Game`.
- Added `Game` initialization guard to prevent running if SDL init fails.
- Fixed `SceneManager::removeScene` log message typo.

### Collision refactor
- Added `Util/Collision` module with centered scaling + AABB overlap checks.
- Added `Object::getRectF()` to use float positions for collision.
- Replaced all collision tests in `LevelScene` with float rects + centralized helpers.
- Introduced tunable hitbox scales for movement vs damage collisions.
- Updated build to include `Util/Collision.cpp`.

### Result
- Collisions behave consistently without offset artifacts.
- Code is cleaner and ready for deterministic network simulation.
