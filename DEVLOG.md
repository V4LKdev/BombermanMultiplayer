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
- Split `NetClient` into connection, runtime, and protocol implementation files under `Net/Client` to make the client multiplayer netcode easier to understand and explain.
- Simplified client-side cached authority state with `std::optional`, cleaned up reset/disconnect naming, and tightened a few misleading `const` helper signatures.
- Grouped `NetClient`, `ClientPrediction`, and `ClientDiagnostics` into a dedicated Doxygen topic with short portfolio-style presentation pages.
- Added net-client subsystem diagrams, light/dark SVG variants, and a shared click-to-expand image viewer in the generated Doxygen output.
- Switched the Doxygen site fully onto the doxygen-awesome Interactive TOC layout, aligned the sidebar width with the theme, and added page-to-page navigation buttons so the docs read like a guided portfolio walkthrough.
- Refined the shared landing page structure so the README keeps a cleaner portfolio shape while staying usable as the Doxygen main page.
- Reworked the networking overview page into the first fully finished portfolio-quality subsystem page, covering authority, channel layout, message catalogue, bootstrap/runtime flow, diagnostics, and concrete protocol examples.
- Added dedicated networking diagrams for bootstrap flow and correction packet structure, with light/dark variants integrated into the generated site.
- Tightened `NetCommon.h` comments so the shared protocol file reads more clearly as the single client/server wire contract.
- Reorganized and clarified `ServerState.h` comments and sectioning so the authoritative server state model is easier to review before the server-flow documentation pass.
- Finalized the three core topic pages (`networking`, `net-client`, and `multiplayer-level-scene`) so they read consistently as short portfolio-facing subsystem overviews.
- Added the new `server.md` topic page plus server-state / server-flow diagrams to complete the authoritative-server side of the documentation story.
- Polished the Doxygen presentation with light/dark logo swapping and title-area sizing tweaks so the site header fits the wider sidebar layout cleanly.
- Finished the shared `README.md` landing page as a portfolio-first project overview, with contribution framing, goals, technical highlights, stack summary, guided reading, build instructions, limitations, and media/link placeholders that stay compatible with both GitHub and Doxygen.

## 2026-03-30

- Tightened the remaining shared networking support helpers so packet parsing, typed dispatch, and ENet channel sends read more clearly without changing the protocol model.
- Finished the server-side cleanup pass across session, receive/dispatch, simulation, snapshots, and powerup helpers to reduce comment noise, remove broad namespace imports, and keep the authoritative runtime easier to review.
- Folded the shared packet infrastructure (`PacketDispatch` and `NetSend`) back into the networking overview page so they remain visible as support infrastructure without becoming separate portfolio topics.
- Polished the remaining entry-point and runtime-surface files (`Game.h`, `main.cpp`, `server_main.cpp`) to improve parameter docs, comments, and explicit namespace usage.
- Tightened the topic-page reading order so `networking -> server -> net-client -> multiplayer-level-scene` now flows more naturally through the generated docs.
- Finalized the shared `README.md` landing page for both GitHub and Doxygen with clearer goals, contribution framing, third-party library summary, guided reading links, platform-aware build instructions, and placeholders for final media and external links.
