# Task cards: persistence and metadata

[Back to the task-card index](../tasks.md).

Leftovers from stage 1 recovery/persistence ([foundation](../stages/foundation.md))
and stage 3 metadata editing ([authoring/export](../stages/authoring-export.md)).
The recovery contract lives in the
[recovery design reference](../../references/recovery-design.md). The Metadata tab
was recently reorganized into collapsible sections; apply the stale-plan check
(rule 9) first.

## T-120 Write the crash marker

- **Status:** ready
- **Outcome:** the app creates a crash marker file in the per-user recovery directory at startup and removes it on clean shutdown; a marker left over from a previous run is reported to the startup recovery scan.
- **Read first:** `docs/references/recovery-design.md` (crash-marker paragraphs), `src/sys/platform_recovery.cpp`, `include/wormedit/core/startup_session.h`, `tests/core/startup_session_tests.cpp`.
- **Files owned:** `src/sys/platform_recovery.cpp/.h`, `include/wormedit/core/startup_session.h`, `src/core/startup_session.cpp`, `tests/core/startup_session_tests.cpp`, `src/sys/main.cpp` (create/remove calls only).
- **Steps:**
  1. Marker contents: process ID and project ID, written through the existing atomic temporary+rename helper.
  2. Startup scan gains a `previous_run_crashed` flag.
  3. If the flag is set and the primary validates with no newer backup, create the `.bak` from the primary as the design requires.
  4. Tests for: no marker, stale marker, marker plus newer backup (backup not overwritten).
- **Invariants:** never overwrite a newer valid recovery artifact.
- **Reject/failure behavior:** marker I/O failure is a status warning, never a startup failure.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'recovery|autosave|startup'`.
- **Out of scope:** command journal (T-121).

## T-121 Add the checksummed command journal

- **Status:** blocked-on T-120
- **Outcome:** between autosaves, each committed command appends a checksummed record (revision, command kind, serialized post-state delta or full-document hash) so recovery can report how many edits were lost.
- **Read first:** `docs/references/recovery-design.md` (journal paragraphs), `include/wormedit/core/command.h`, `include/wormedit/core/autosave_loop.h`.
- **Files owned:** new `include/wormedit/core/command_journal.h`, new `src/core/command_journal.cpp`, new `tests/core/command_journal_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Records **replay** edits (user decision 2026-09-30): each record stores the serialized command (or the post-state delta) so recovery replays journaled edits on top of the last autosave, losing at most the last edit.
  2. Implement bounded append, truncation on successful autosave, and a reader that stops at the first bad checksum.
  3. Tests: torn final record, checksum mismatch, size cap.
- **Invariants:** journal writes are off the frame thread.
- **Reject/failure behavior:** a corrupt journal never blocks loading the backup.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'recovery|autosave'`.
- **Out of scope:** replay UI.

## T-122 Add native-project schema migrations

- **Status:** ready
- **Outcome:** the native-project reader dispatches on the schema version through a table of `migrate_vN_to_vN+1` JSON functions; version 1 passes through unchanged and an unknown future version rejects.
- **Read first:** `include/wormedit/formats/native_project.h`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`.
- **Files owned:** `src/formats/native_project.cpp`, new `src/formats/native_project_migrations.cpp/.h`, `tests/formats/native_project_tests.cpp`, `CMakeLists.txt`.
- **Steps:** 1. Extract the version check into the dispatcher. 2. Add a test-only fake `v0→v1` migration to prove the chain runs in order. 3. Future versions reject with a typed error.
- **Invariants:** existing v1 files load byte-identically.
- **Reject/failure behavior:** a failing migration publishes no document.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'native_project'`.
- **Out of scope:** packaging (T-123).

## T-123 Decide native-project packaging

- **Status:** done (user decision 2026-09-30: keep the native project as a single JSON file with assets in the song folder beside it; no folder manifest or archive work.)
- **Outcome:** answer recorded in `docs/chart-editor-plan/decisions/open-questions.md` (question 12).
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** recorded.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** none.

## T-124 Model metadata inheritance and overrides

- **Status:** blocked-on T-122
- **Outcome:** per-difficulty title/subtitle/artist/genre/music/preview fields can be "inherit from song" or an explicit override; export resolves them; native projects store the flag (schema v2 through T-122's migration).
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (metadata scope paragraphs), `include/wormedit/core/document.h`, `include/wormedit/core/metadata_editor.h`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/metadata_editor.h`, `src/core/metadata_editor.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/core/metadata_editor_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. Add `std::optional<std::string>` overrides per field on the chart. 2. `resolved_title(chart, song)` helpers used by writers. 3. v1→v2 migration sets every override to empty (inherit). 4. Commands to set/clear an override.
- **Invariants:** existing exports byte-identical when no overrides are set.
- **Reject/failure behavior:** stale revision rejects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|native_project|round.?trip'`.
- **Out of scope:** UI (T-125).

## T-125 Add inherit/override controls in Metadata

- **Status:** blocked-on T-124
- **Outcome:** each difficulty field shows an "Inherit" checkbox; unchecking reveals an editable override.
- **Read first:** `include/wormedit/core/metadata_editor.h`, `src/ui/nuklear/metadata_panel.cpp`, `src/sys/metadata_actions.cpp`.
- **Files owned:** `src/sys/metadata_actions.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp` (or T-42's section unit).
- **Steps:** standard pattern.
- **Invariants:** inherited values display the song value greyed.
- **Reject/failure behavior:** core rejections in the status line.
- **Verify:** standard build/test; smoke PNG of `metadata`.
- **Out of scope:** batch edits (T-128).

## T-126 Change a chart's key count or profile with a lane-migration preview

- **Status:** ready
- **Outcome:** a core proposal maps notes from the current lane profile to a chosen one (explicit lane-to-lane table, default by display order), previews dropped/merged notes, and commits atomically.
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (key-count selector paragraph), `include/wormedit/core/lane_profiles.h`, `include/wormedit/core/document.h` (`lane_layout_t`).
- **Files owned:** new `include/wormedit/core/lane_migration.h`, new `src/core/lane_migration.cpp`, new `tests/core/lane_migration_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Proposal type with mapping table and counts. 2. Apply command. 3. Tests: 7K→5K default drops lanes 6–7 notes (counted), explicit mapping merges with collision rejection, undo.
- **Invariants:** never treats a count change as a relabel; unmapped notes are listed, not silently deleted, unless the user confirms "drop".
- **Reject/failure behavior:** collisions reject.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (T-127).

## T-127 Add the key-count/profile selector to Metadata

- **Status:** blocked-on T-126
- **Outcome:** per-chart selector showing only counts supported by the chart's format family plus "Custom", with the preview counts and a confirm button.
- **Read first:** `include/wormedit/core/lane_migration.h`, `src/ui/nuklear/metadata_panel.cpp`, `src/sys/metadata_actions.cpp`.
- **Files owned:** `src/sys/metadata_actions.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp`.
- **Steps:** standard pattern.
- **Invariants:** as T-126.
- **Reject/failure behavior:** as T-126.
- **Verify:** standard build/test.
- **Out of scope:** per-lane mapping editor (default mapping only).

## T-128 Apply one metadata edit to several difficulties

- **Status:** ready
- **Outcome:** Metadata offers a multi-select of difficulties; applying author or rating edits writes all selected charts in one command.
- **Read first:** `include/wormedit/core/metadata_editor.h`, `src/sys/metadata_actions.cpp`, `src/ui/nuklear/metadata_panel.cpp`.
- **Files owned:** `include/wormedit/core/metadata_editor.h`, `src/core/metadata_editor.cpp`, `tests/core/metadata_editor_tests.cpp`, `src/sys/metadata_actions.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp`.
- **Steps:** 1. Core batch command over a bounded chart-ID list. 2. UI checkboxes per difficulty.
- **Invariants:** one undo step.
- **Reject/failure behavior:** any invalid chart rejects all.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** format fields.

## T-129 Add typed per-format metadata forms

- **Status:** ready
- **Outcome:** Metadata's format section shows named typed fields for BMS (`#PLAYER`, `#RANK`, `#TOTAL`, `#PLAYLEVEL`, `#DIFFICULTY`), StepMania (`#CREDIT`, `#DIFFICULTY`, `#METER`), osu! (`HPDrainRate`, `OverallDifficulty`, `Version`), and BMSON (`level`, `judge_rank`, `total`) that read/write the existing ordered format fields.
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (Metadata tab paragraph), `include/wormedit/core/metadata_editor.h`, `src/ui/nuklear/metadata_panel.cpp`.
- **Files owned:** new `include/wormedit/core/format_field_schema.h`, new `src/core/format_field_schema.cpp`, new `tests/core/format_field_schema_tests.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/nuklear/metadata_panel.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Static schema table (format, key, type int/decimal/text, range). 2. Validation function with tests. 3. Render typed widgets that edit through the existing format-field insert/replace commands.
- **Invariants:** unknown fields remain in the generic list; nothing is reordered.
- **Reject/failure behavior:** out-of-range values show an inline error and are not applied.
- **Verify:** standard build/test; smoke PNG of `metadata`.
- **Out of scope:** new fields not listed here.
