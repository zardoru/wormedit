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
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Stale-plan audit (2026-10-03):** No crash-marker backend or startup crash flag exists. The required existing atomic temporary/write/flush/validate/rename helper is private `publish_prevalidated_bytes` in unowned `src/formats/native_project_storage.cpp`; public storage APIs serialize native projects/recovery envelopes rather than marker bytes. Reusing that backend needs an approved raw-byte publication seam in `include/wormedit/formats/native_project_storage.h` and its implementation, rather than disguising a marker as a project or copying atomic-publication code.
- **Outcome:** the app creates a crash marker file in the per-user recovery directory at startup and removes it on clean shutdown; a marker left over from a previous run is reported to the startup recovery scan.
- **Read first:** `docs/references/recovery-design.md` (crash-marker paragraphs), `src/sys/platform_recovery.cpp`, `include/wormedit/core/startup_session.h`, `tests/core/startup_session_tests.cpp`.
- **Files owned:** `src/sys/platform_recovery.cpp/.h`, `include/wormedit/core/startup_session.h`, `src/core/startup_session.cpp`, `tests/core/startup_session_tests.cpp`, `src/sys/main.cpp` (create/remove calls only). User-approved supporting scope (2026-10-03): `src/formats/native_project_storage.cpp`, `include/wormedit/formats/native_project_storage.h`.
- **Steps:**
  0. Scope extension approved by the user (2026-10-03).
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

- **Status:** done (2026-10-01; `src/formats/native_project_migrations.{h,cpp}`, dispatcher call in `parse_native_project`; five migration tests in `tests/formats/native_project_tests.cpp`)
- **Implementation notes:** the dispatcher walks `document.schema_version` up to the reader's version through a table of `{from_version, migrate}` entries, working on a copy so a failing step publishes nothing. The table was empty while schema 1 was the only release; schema 2 (2026-10-02, T-319) added its first step, `migrate_v1_to_v2`, which gives every SV recipe an empty `expression_source`. The recovery envelope accepts schema 1 or 2 payloads because the payload is a native project that migrates on load. `format_version` of the envelope stays strict. A non-integer or missing schema version stays `malformed_json`; a newer or unmigratable one is `unsupported_version`; a failing step is `invalid_document`. Tests inject a fake v0→v1 step through `parse_native_project_with_migrations` and check chain order directly with a two-step table.
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

- **Status:** ready
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

- **Status:** done
- **Outcome:** a core proposal maps notes from the current lane profile to a chosen one (explicit lane-to-lane table, default by display order), previews dropped/merged notes, and commits atomically.
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (key-count selector paragraph), `include/wormedit/core/lane_profiles.h`, `include/wormedit/core/document.h` (`lane_layout_t`).
- **Files owned:** new `include/wormedit/core/lane_migration.h`, new `src/core/lane_migration.cpp`, new `tests/core/lane_migration_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Proposal type with mapping table and counts. 2. Apply command. 3. Tests: 7K→5K default drops lanes 6–7 notes (counted), explicit mapping merges with collision rejection, undo.
- **Invariants:** never treats a count change as a relabel; unmapped notes are listed, not silently deleted, unless the user confirms "drop".
- **Reject/failure behavior:** collisions reject.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (T-127).

Verified 2026-10-03: fourteen core regression cases cover confirmed drops and undo across layout segments, noncolliding merges, inclusive hold collisions, immutable revision/song-bound previews, ambiguous defaults, bounded collision examples, retained BMSON pairs and unlinked events, and parsed BMS branch channel identities. The core prefix passes 1,123/1,123 and the complete suite passes 1,655/1,655; the application builds and both new C++ units pass the configured complexity check. The target input profile replaces the chart's bindings explicitly; collision examples are capped at 64 while the conflict count remains available.

## T-127 Add the key-count/profile selector to Metadata

- **Status:** ready
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Outcome:** per-chart selector showing only counts supported by the chart's format family plus "Custom", with the preview counts and a confirm button.
- **Read first:** `include/wormedit/core/lane_migration.h`, `src/ui/nuklear/metadata_panel.cpp`, `src/sys/metadata_actions.cpp`.
- **Files owned:** `src/sys/metadata_actions.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp`. User-approved supporting scope (2026-10-03): `src/sys/metadata_actions.h`.
- **Implementation audit (2026-10-03):** no migration selector exists. Family choices and a real generic Custom target can be built in owned files; Custom must not reuse the osu!mania 1–18 factory or become a no-op for the current layout. The sealed migration preview/count/conflict state needs shared dispatch/projection access declared in unowned `src/sys/metadata_actions.h`, the same ownership gap as T-96. The user approved this scope extension on 2026-10-03; no implementation was started.
- **Steps:**
  0. Scope extension approved by the user (2026-10-03).
  1. Wire the complete selector/preview/confirmation through the standard pattern.
- **Invariants:** as T-126.
- **Reject/failure behavior:** as T-126.
- **Verify:** standard build/test.
- **Out of scope:** per-lane mapping editor (default mapping only).

## T-128 Apply one metadata edit to several difficulties

- **Status:** done
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Implementation notes (2026-10-01):** the core batch command already exists as `apply_batch_chart_metadata_edit` in `metadata_editor.h` (added with T-260); only the Metadata multi-select UI (step 2) is open.
- **Outcome:** Metadata offers a multi-select of difficulties; applying author or rating edits writes all selected charts in one command.
- **Read first:** `include/wormedit/core/metadata_editor.h`, `src/sys/metadata_actions.cpp`, `src/ui/nuklear/metadata_panel.cpp`.
- **Files owned:** `include/wormedit/core/metadata_editor.h`, `src/core/metadata_editor.cpp`, `tests/core/metadata_editor_tests.cpp`, `src/sys/metadata_actions.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp`. User-approved supporting scope (2026-10-03): `src/sys/metadata_view_projection.cpp`, `src/ui/nuklear/metadata_state.h/.cpp` as needed for resettable selection state.
- **Steps:** 0. Scope extension approved by the user (2026-10-03). 1. Core batch command over a bounded chart-ID list. 2. UI checkboxes per difficulty.
- **Invariants:** one undo step.
- **Reject/failure behavior:** any invalid chart rejects all.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** format fields.
- **Supporting scope (coordinator, 2026-10-08):** `src/ui/nuklear/metadata_difficulty_section.cpp` and `tests/sys/metadata_notification_tests.cpp`.
- **Result (2026-10-08):** Metadata's Difficulty section now lists a checkbox per difficulty (selection in `metadata_state.h`, reset with the metadata state, pruned of vanished ids) with Apply Mapper / Apply Rating buttons that send `WORM_UI_METADATA_EDIT_BATCH_CHART`; `dispatch_batch_chart_edit` writes them through `apply_batch_chart_metadata_edit` as one undo step. The view carries up to 256 chart choices (`WormUiMetadataChartChoice`). New test in `metadata_notification_tests.cpp` (two charts, one undo step, stale revision rejects). Full suite 2838/2838, event-harness 253/253.


## T-129 Add typed per-format metadata forms

- **Status:** done
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Implementation notes (2026-10-01):** steps 1 and 2 are done: `format_field_schema.h` has the typed table (`format_field_schemas`, `find_format_field_schema`) and `validate_format_field_value` with plain-ASCII integer/decimal parsing, case-insensitive choices (StepMania `DIFFICULTY`) and single-line text. Stored names are the exact `ordered_fields` spellings: BMS tokens without `#`, StepMania tags, osu! `Difficulty.HPDrainRate`/`Difficulty.OverallDifficulty`/`Metadata.Version`, BMSON `info.level`/`info.judge_rank`/`info.total`. Tests in `tests/core/format_field_schema_tests.cpp`. Step 3 (the typed widgets) remains.
- **Outcome:** Metadata's format section shows named typed fields for BMS (`#PLAYER`, `#RANK`, `#TOTAL`, `#PLAYLEVEL`, `#DIFFICULTY`), StepMania (`#CREDIT`, `#DIFFICULTY`, `#METER`), osu! (`HPDrainRate`, `OverallDifficulty`, `Version`), and BMSON (`level`, `judge_rank`, `total`) that read/write the existing ordered format fields.
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (Metadata tab paragraph), `include/wormedit/core/metadata_editor.h`, `src/ui/nuklear/metadata_panel.cpp`.
- **Files owned:** new `include/wormedit/core/format_field_schema.h`, new `src/core/format_field_schema.cpp`, new `tests/core/format_field_schema_tests.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/nuklear/metadata_panel.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`. User-approved supporting scope (2026-10-03): `src/ui/nuklear/metadata_format_fields.cpp/.h`, `src/ui/nuklear/metadata_state.h`.
- **Steps:** 0. Scope extension approved by the user (2026-10-03). 1. Static schema table (format, key, type int/decimal/text, range). 2. Validation function with tests. 3. Render typed widgets that edit through the existing format-field insert/replace commands.
- **Invariants:** unknown fields remain in the generic list; nothing is reordered.
- **Reject/failure behavior:** out-of-range values show an inline error and are not applied.
- **Verify:** standard build/test; smoke PNG of `metadata`.
- **Out of scope:** new fields not listed here.
- **Supporting scope (coordinator, 2026-10-08):** `tests/sys/metadata_notification_tests.cpp`.
- **Result (2026-10-08):** `metadata_format_fields.cpp` now drives typed widgets from `format_field_schemas`: a generic "Typed fields" block renders every schema field without a bespoke editor (BMS `PLAYER`, all StepMania, all osu!mania, BMSON `info.level`) and edits through the existing insert/replace/erase actions (existing field keeps its scope; a new one goes to the song group unless only the chart has one). The bespoke BMS/BMSON numeric checks now call `validate_format_field_value` where the schema covers the field (RANK, TOTAL, PLAYLEVEL, DIFFICULTY, judge_rank, total), so ranges come from one table; invalid values show the schema's inline message and Apply stays disabled, so nothing is dispatched. New test per family in `metadata_notification_tests.cpp` (insert and replace write the ordered field; out-of-range fails the validator the UI gates on). The dispatcher itself does not re-validate ranges, since the generic ordered-field editor also uses it. Manual smoke PNG of `metadata` not taken.


## T-295 Validate every opened document before using it

- **Status:** done (2026-09-30; whole-document open validation, conservative storage repair, and contextual diagnostics)
- **Outcome:** after any chart or project is parsed, the whole document is validated before it becomes the editing document; an invalid document is either repaired by a named, safe normalization (with a warning listing what changed) or refused with an error that names the chart, measure, and object; the app never opens a document in which every edit will be rejected.
- **Cause (user session 2026-09-30, gdb):** a StepMania `.sm` with a hold crossing a measure boundary was imported by an older parser that stored the hold in its tail's measure while its position named the head's measure (fixed in commit `d7cdda7`, with a regression test in `tests/formats/stepmania_tests.cpp`). Because `src/sys/project_open.cpp` does not validate parsed documents, the app opened it; every later command then failed whole-document validation with "note is stored outside the measure named by its position" — in a different difficulty from the one being edited — so note placement, tab switches, and zoom silently did nothing while the cursor appeared stuck.
- **Read first:** `src/sys/project_open.cpp` (`take_parsed_document`, `normalize_empty_input_profiles`), `include/wormedit/core/document.h` (graph validation entry point and diagnostics), `src/core/document.cpp`, `include/wormedit/core/command.h` (`commit_document_command` validation).
- **Files owned:** `src/sys/project_open.cpp/.h`, `tests/sys/project_open_tests.cpp`, new `tests/formats/fixture_validation_tests.cpp`, `tests/CMakeLists.txt`, `src/core/document.cpp` (diagnostic text only).
- **Steps:**
  1. After parsing and load normalizations, run the same document validation commands use. On failure: if every diagnostic is in a repairable class (for example a note stored in the wrong measure chunk but with a valid position), re-home those objects into the measure their position names and report "Repaired N objects stored in the wrong measure"; otherwise refuse to open and show the first diagnostics with chart name, measure number, lane, and row.
  2. Make validation diagnostics name the chart (difficulty name) and the measure number, not only ids, so a command rejected because of another difficulty is understandable.
  3. Fixture-wide test: every positive fixture under `tests/fixtures/` (all formats) parses into a document that passes validation, and one command (place a tap) succeeds on each chart.
  4. Regression test: a hand-built document with a note in the wrong measure chunk is repaired on open; an unrepairable one is refused with a readable message.
- **Invariants:** valid documents open exactly as before; repairs never change positions, only storage.
- **Reject/failure behavior:** a refused document leaves the current session unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'project_open|fixture_validation|^core\.'`.
- **Out of scope:** UI for browsing all diagnostics (notifications T-224 cover that).

- **Verification evidence (2026-09-30):** integrated application build passed; literal Verify reported `100% tests passed out of 819` (11.90 s). The full integrated suite passed 1050/1050 before notification integration. Configured clang-tidy found no new complexity violations in the four affected C++ files (existing dependency warnings remain). Positive chart fixtures use explicit encoding/profile choices; the deliberately unmapped custom-channel BME fixture is validated first, then explicitly given an authoring layout to test new tap placement without interpreting preserved raw channels. Repair regressions compare serialized documents and preserve positions; unrepaired failures preserve active history.
