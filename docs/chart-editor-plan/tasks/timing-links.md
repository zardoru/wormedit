# Task cards: granular timing links

[Back to the task-card index](../tasks.md).

Owner decisions and the audited design are in
[shared timing categories](../document-model.md#shared-timing-categories).
T-730 is the planning owner. Complete T-124 and T-125 before this sequence.
Each card records supporting ownership before extending it.

## T-751 Store timing-category links and exact source mappings

- **Status:** done (2026-10-22; schema 13, category source identities and exact snapshot validation)
- **Verified (2026-10-22):** GATE and Linux Clang each pass **4,512/4,512** (`/tmp/t751-gate.log`, `/tmp/t751-clang.log`); whitespace and **5-TU** complexity pass without increased allowances. Focused timing-link tests pass **47 assertions / 10 cases** (`/tmp/t751-focused-current.log`): unrelated bar boundaries/resolutions, exclusive ends, inexact/overflowing grids, missing/self/cyclic sources, ambiguous identities, effective-snapshot corruption, local overrides, recipes, stop/delay isolation and schema-12 migration. Existing export-byte regressions pass in both full suites. Active source graphs use a bounded nonrecursive walk. Runtime propagation and UI remain the following cards.
- **Supporting ownership (coordinator, 2026-10-22):** `memory/chart-editor-status/resume.md` tracks the resumed sequence. `include/wormedit/core/measure.h` adds value equality for measure definitions; new `include/wormedit/core/timing_links_types.h` keeps category/link vocabulary lightweight for the later frontend; `include/wormedit/core/document.h` adds value equality on timing records for one category comparison owner. Persist source-to-target object/recipe/measure identity mappings with each source association so propagation retains local identities. A detached override remembers its source for explicit relinking.
- **Outcome:** native documents store per-category designated-chart links, validate acyclic same-project sources, and resolve exact musical positions across unrelated measure IDs and resolutions.
- **Read first:** `docs/chart-editor-plan/document-model.md`; `include/wormedit/core/document.h`; `include/wormedit/core/timing.h`; `include/wormedit/core/timing_lane_catalog.h`; `src/formats/native_project_migrations.cpp`.
- **Files owned:** new `include/wormedit/core/timing_links_types.h`, `include/wormedit/core/measure.h`, `memory/chart-editor-status/resume.md`, new `include/wormedit/core/timing_links.h`, new `src/core/timing_links.cpp`, new `tests/core/timing_links_tests.cpp`, `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card.
- **Steps:** 1. Introduce one exhaustive category catalog covering the ten design categories and recipe targets. 2. Store optional source chart IDs per category with same-project identity and cycle validation. 3. Implement exact musical-beat mapping for positions, spans and row-relative durations using existing checked arithmetic/index owners. 4. Add the next native schema migration from the actual current schema to unlinked categories and verify resolved snapshot consistency on load. 5. Add native round-trip, unknown/cyclic source and cross-resolution exact/rejected mapping regressions.
- **Invariants:** no rounded positions, shared mutable vectors or silent source substitution; offset and pause units remain exact; older documents remain unlinked.
- **Reject/failure behavior:** invalid source, cycle, grid overflow or inexact mapping rejects without modifying a document or existing file.
- **Verify:** GATE; Linux Clang build and full tests; native links/overrides round-trip; unchanged exports for unlinked documents.
- **Out of scope:** live transaction detachment (T-752), measure/content remapping and clock compatibility (T-753), UI (T-754), final publication proof (T-755).

## T-752 Resolve timing links through the shared transaction

- **Status:** ready (T-751 complete 2026-10-22)
- **Outcome:** accepted local edits detach only affected linked categories while source edits atomically propagate to all compatible dependents, with complete Undo/Redo.
- **Read first:** `docs/architecture.md`; `include/wormedit/core/command.h`; `src/core/command.cpp`; `include/wormedit/core/proposal.h`; `src/core/interaction_edit_commit.h`.
- **Files owned:** `include/wormedit/core/timing_links.h`, `src/core/timing_links.cpp`, `tests/core/timing_links_tests.cpp`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/core/chart_editor.cpp`, `tests/core/chart_editor_tests.cpp`, `tests/core/command_tests.cpp`, `docs/architecture.md`, this card.
- **Steps:** 1. Audit every publication route, detached preview and chart deletion/duplication owner. 2. Detect actual category changes at the shared boundary; preserve links for no-op/failed/preview/cancelled work. 3. Add stamped link/detach/relink operations with explicit discard confirmation and fresh local identities. 4. Resolve dependencies and validate once before outer publication; recipe target changes detach both affected categories. 5. Guard source deletion and preserve duplication links without self-links. 6. Prove propagation, chained sources, multi-category isolation, stale/cancel rejection and one-step Undo/Redo.
- **Invariants:** domain edits contain no repeated link lifecycle code; source propagation and initiating edit share one journal/Undo publication; unrelated categories and charts remain unchanged.
- **Reject/failure behavior:** rejected dependent resolution, unconfirmed local-data loss or deleting a live source rolls back the entire outer transaction.
- **Verify:** GATE; Linux Clang build and full tests; direct command, gesture, compound transaction and detached preview coverage.
- **Out of scope:** measure/audio-clock compatibility (T-753), frontend controls (T-754), file publication proof (T-755).

## T-753 Preserve local content when linked measure layouts and clocks change

- **Status:** blocked-on T-752
- **Outcome:** effective timing snapshots preserve local objects through exact linked measure-layout changes and diagnose incompatible source clocks/provenance before publication.
- **Read first:** `src/core/measure_editor.cpp`; `src/core/chart_position_storage.h`; `src/core/timing_reposition.cpp`; `include/wormedit/core/timing.h`; `docs/chart-editor-plan/document-model.md`.
- **Files owned:** `include/wormedit/core/timing_links.h`, `src/core/timing_links.cpp`, `tests/core/timing_links_tests.cpp`, `src/core/measure_editor.cpp`, `src/core/chart_position_storage.h`, `src/core/timing.cpp`, `tests/core/measure_editor_tests.cpp`, `tests/core/timing_tests.cpp`, this card.
- **Steps:** 1. Reuse/extract the existing complete position traversal rather than adding a partial event list. 2. Map local notes, holds, BGM, BGA, sound channels, local timing, layout segments and beat markers to changed measure boundaries; rehome notes/BGM through the existing helper. 3. Check sample-clock anchors, retained BMS projections/control flow and osu! absolute-time grid compatibility explicitly. 4. Verify BPM, scroll/SPEEDS recipes, Kiai, stop/delay semantics, warps, anchors, offsets and layouts each link independently. 5. Compare production timing indexes, prepared recipes, preview and playback against the resolved snapshot after source edits and Undo.
- **Invariants:** target row resolution and local object identities survive; audio-time navigation markers stay fixed; no clipping, implicit flattening or rounding.
- **Reject/failure behavior:** insufficient layout extent, collision, incompatible clock, unsupported provenance or failed exact remapping rejects source and dependent changes together.
- **Verify:** GATE; Linux Clang build and full tests; cross-grid endpoint, pause-boundary, offset, anchor, recipe and retained-source regressions.
- **Out of scope:** UI (T-754), final recovery/export publication proof (T-755), automatic lossy compatibility transforms (existing explicit conversion workflow).

## T-754 Expose category links and inherit selected links for new charts

- **Status:** blocked-on T-753
- **Outcome:** Metadata shows each category's source/local status, explicit link/detach/relink actions and discard confirmation; new difficulties inherit the selected timing links.
- **Read first:** `src/sys/metadata/metadata_actions.cpp`; `src/sys/metadata/metadata_view.cpp`; `src/ui/frontend/metadata_audio.h`; `src/ui/nuklear/metadata/metadata_difficulty_section.cpp`; `src/core/chart_editor.cpp`.
- **Files owned:** `src/sys/metadata/metadata_actions.cpp`, `src/sys/metadata/metadata_view.cpp`, `src/ui/frontend/metadata_audio.h`, `src/ui/frontend/actions.h`, new `src/ui/nuklear/metadata/metadata_timing_links_section.cpp/.h`, `src/ui/nuklear/metadata/metadata_panel.cpp`, `src/ui/nuklear/metadata/metadata_state.h`, `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`, `src/sys/project/project_view.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/core/chart_editor_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/manual/chapters/timing.adoc`, this card.
- **Steps:** 1. Project stable chart/category IDs and resolved labels through retained C++ views. 2. Add source choices and local/link state per category. 3. Route explicit stamped link/detach/relink operations through the shared command finisher; require deliberate confirmation before discarding local category data. 4. Carry the user's selected category/source links into the existing new-difficulty workflow; unrelated new projects stay independent. 5. Add event-harness source/selection/stale/cancel/Undo checks and isolated native link, local override, relink and new-chart checks.
- **Invariants:** source/category IDs cross the UI boundary, never row positions; read-only/locked/stale contexts reject; inherited categories follow current source data.
- **Reject/failure behavior:** cancelling confirmation, stale context or incompatible source preserves local data and links and displays the core diagnostic.
- **Verify:** GATE; Linux Clang build and full tests; event harness; isolated Xvfb screenshots plus saved-document and one-Undo inspection.
- **Out of scope:** event-level merges (not requested), cross-project sources (not requested), final file publication regressions (T-755).

## T-755 Verify linked timing through recovery and transactional exports

- **Status:** blocked-on T-754
- **Outcome:** save/reopen, journal crash recovery and supported interchange exports agree with the same resolved timing snapshot and retain failure atomicity.
- **Read first:** `src/formats/native_project_storage.cpp`; `src/core/command_journal.cpp`; `src/formats/conversion.cpp`; `src/formats/conversion_publication.cpp`; `docs/references/recovery-design.md`.
- **Files owned:** `tests/core/timing_links_tests.cpp`, `tests/core/command_journal_tests.cpp`, `tests/formats/native_project_tests.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_publication_tests.cpp`, `docs/references/recovery-design.md`, `docs/chart-editor-plan/document-model.md`, `memory/chart-editor-status/resume.md`, this card. Production fixes require a recorded supporting extension tied to a demonstrated regression.
- **Steps:** 1. Save/reopen linked and locally overridden categories and verify source identity plus resolved data. 2. Journal source updates, local detachment, relink and Undo; test torn final records and autosave cursors. 3. Export compatible BMS/BMSON/SM/SSC/osu! timing and compare reimported effective clocks and recipes; ensure links stay native-only. 4. Prove unsupported targets, incompatible grids, failed asset/file publication and stale results preserve existing outputs and live links. 5. Repeat native crash/restart and full verification, then mark the sequence complete with actual evidence.
- **Invariants:** every consumer sees one resolved snapshot; no automatic primary overwrite, silent target loss or export-dependent source mutation.
- **Reject/failure behavior:** any codec, compatibility or publication failure retains prior outputs, recovery artifacts and the live source/link graph.
- **Verify:** GATE; Linux Clang build and full tests; isolated native crash/restart and export/reimport proof; documentation checks and whitespace.
- **Out of scope:** new target format support (existing format capability cards), event-level merge semantics (not requested).
