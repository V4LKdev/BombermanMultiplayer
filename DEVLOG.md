# Devlog

## 2026-03-29

- Archived the previous documentation material into `_temp_docs_archive/` to clear the repo for a cleaner documentation pass.
- Added a minimal `docs/` structure for architecture, networking, testing, reference, project notes, and a condensed devlog page.
- Added a first Doxygen setup using `README.md` as the shared landing page for GitHub and the generated site.
- Integrated the local Doxygen Awesome theme assets already available in `~/Scratch/Doxy-Preview/`.
- Added a custom Doxygen header with the darkmode toggle and simplified the sidebar-first layout.
- Reduced the landing page back down after testing Markdown features so the baseline stays clean.
- Reorganized `MultiplayerLevelScene` into a dedicated subsystem folder, split its logic into authority, presentation, remote/world presentation, and telemetry files, and cleaned up the header/documentation surface.
- Added a dedicated `docs/multiplayer-level-scene.md` page with structure/runtime diagrams, plus reusable light/dark SVG swapping support for future Doxygen charts.
- Reduced third-party analysis noise by treating external includes as `SYSTEM` and adding a project-local `.clang-tidy` configuration.
