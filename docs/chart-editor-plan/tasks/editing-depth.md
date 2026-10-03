# Task cards: editing depth and performance (stage 6)

[Back to the task-card index](../tasks.md).

Stage 6 from [later work](../stages/later.md). These are independent of each
other unless a card says otherwise. Apply the stale-plan check (rule 9) first.

## T-140 Mirror and shuffle lanes of a selection

- **Status:** done (2026-10-01; `include/wormedit/core/lane_transform.h`, `src/core/lane_transform.cpp`, `tests/core/lane_transform_tests.cpp`, five tests)
- **Implementation notes:** `apply_lane_mirror` reverses the playable lanes of each stage of the layout active at a note (scratch, free-zone and pedal lanes stay); `apply_lane_remap` takes an explicit bijection. Both go through one assignment routine that checks the layout at the note's head and tail, rejects any same-lane overlap, and commits through `commit_document_command`. A transform that moves nothing creates no history entry. The lane-move collision check in `interaction.cpp` is private, so the overlap check is a small local copy.
- **Outcome:** **Mirror** reverses the lane order of selected playable notes within their stage; **Remap…** applies an explicit lane permutation; both are one command each.
- **Read first:** `include/wormedit/core/interaction.h` (lane-move proposal), `src/core/interaction.cpp`, `include/wormedit/core/lane_profiles.h`.
- **Files owned:** new `include/wormedit/core/lane_transform.h`, new `src/core/lane_transform.cpp`, new `tests/core/lane_transform_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Core proposal reusing lane-move collision validation. 2. Tests: 7K mirror keeps scratch lane fixed; permutation with collision rejects; undo. 3. STOP before adding UI; UI is T-141.
- **Invariants:** rows, durations, sounds, labels, layers unchanged.
- **Reject/failure behavior:** notes whose target lane is missing reject all.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** random shuffle (needs a seed policy).

## T-141 Add a Transform menu for selections

- **Status:** done (2026-10-02; `src/sys/compose_selection_transform.cpp/.h`, `tests/sys/compose_selection_transform_tests.cpp`)
- **Implementation notes (2026-10-02, Claude):** a collapsible **Transform** section in the Compose inspector (not a popup) with Mirror lanes, Rotate lanes left/right (a one-place cyclic remap per stage, built by `rotate_lane_permutation`) and Shift earlier/later by one active-snap step. A custom remap editor is not part of this card. Disabled with an empty selection; core messages go to the status line.
- **Outcome:** Compose has a Transform popup with Mirror, Remap, and Shift rows (T-142) acting on the selection.
- **Read first:** `include/wormedit/core/lane_transform.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`.
- **Steps:** standard pattern; one menu entry per available transform.
- **Invariants:** disabled with an empty selection.
- **Reject/failure behavior:** core messages in the status line.
- **Verify:** standard build/test.
- **Out of scope:** new transforms.

## T-142 Shift a selection or everything after the cursor by N rows

- **Status:** done (2026-10-01; `include/wormedit/core/time_shift.h`, `src/core/time_shift.cpp`, `tests/core/time_shift_tests.cpp`, five tests)
- **Implementation notes:** `apply_shift_selection` moves named notes and BGM events; `apply_shift_from_cursor` moves everything beat-anchored at or after the cursor (notes, BGM, BGA, lane-layout segments, beat bookmarks and sections) and, with `include_timing`, BPM, pause, warp, scroll, speed, kiai and timing-anchor events. Rows are the chart's own division grid counted across measures, so a shift across a measure-length change is exact; a target outside the materialized measures rejects (it never creates measures). Audio-time bookmarks and sections stay and are counted in `audio_anchored_skipped`. Charts with retained BMS control flow or projections, objects linked to BMSON sound-channel events, and SV recipes with `include_timing` reject with `unsupported_feature`.
- **Outcome:** a core command shifts selected notes/BGM (or every object after the cursor, optionally including timing events) by an exact signed row count.
- **Read first:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `docs/chart-editor-plan/document-model.md` (positioning-domain paragraph).
- **Files owned:** new `include/wormedit/core/time_shift.h`, new `src/core/time_shift.cpp`, new `tests/core/time_shift_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Proposal with exact cross-measure arithmetic. 2. Tests: shift past chart start rejects; shift across a measure-length change is exact; timing-included variant moves BPM events too.
- **Invariants:** audio-time-anchored objects are not moved (report their count).
- **Reject/failure behavior:** any collision rejects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (in T-141).

## T-143 Save quantization presets and show a report

- **Status:** ready
- **Outcome:** named quantization settings (grid, tie policy, hold scope) persist in the native project; applying one shows a per-row report of moved and colliding objects.
- **Read first:** `include/wormedit/core/quantize.h`, `src/formats/native_project.cpp`, `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/sys/compose_actions.cpp`, `tests/formats/native_project_tests.cpp`; user-approved preset/report extensions (2026-10-03): `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`.
- **Implementation audit (2026-10-03):** T-56 is complete, so the prerequisite is satisfied. No persisted quantization presets exist. The document and native-project files can store presets and migrate old projects, but the UI needs preset settings/actions in unowned `src/ui/frontend.h` and moved/colliding row projection in unowned `src/sys/compose_view.cpp`. Existing projection exposes only aggregate counts. The user approved those files and `tests/sys/compose_quantize_preview_tests.cpp`; implementation can proceed.
- **Steps:** 1. Add preset list to the song document (schema migration via T-122). 2. Preset picker + Save/Delete. 3. Report list (bounded 256 rows).
- **Invariants:** older projects load with an empty list.
- **Reject/failure behavior:** duplicate preset names reject.
- **Verify:** standard build/test.
- **Out of scope:** global presets outside the project.

## T-144 Search notes by label

- **Status:** done (2026-10-02; `src/sys/compose_label_search.cpp/.h`, `tests/sys/compose_label_search_tests.cpp`)
- **Implementation notes (2026-10-02, Claude):** inspector **Find label** field with Select (selects every match and jumps to the earliest), Previous and Next (move the cursor to the match before/after it, wrapping). No match gives a status message. Search text travels in `compose_note_label`.
- **Implementation notes (2026-10-01):** step 1 is done: `find_notes_by_label(chart, text)` in `include/wormedit/core/note_label_editor.h` (ASCII case-insensitive, chart order, empty text matches nothing) with a test in `tests/core/note_label_editor_tests.cpp`. Step 2 (field, Next/Previous, status message) remains and touches the compose files.
- **Outcome:** a search field selects every note whose label contains the text (case-insensitive) and jumps to the first; Next/Previous cycle.
- **Read first:** `include/wormedit/core/note_label_editor.h`, `src/ui/nuklear/compose_inspector.cpp` (note label field), `src/sys/compose_actions.cpp`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/note_label_editor_tests.cpp`.
- **Steps:** 1. Core helper `find_notes_by_label(chart, text) -> ids` with tests. 2. Wire field and buttons.
- **Invariants:** no document mutation.
- **Reject/failure behavior:** no match → status message.
- **Verify:** standard build/test.
- **Out of scope:** regex.

## T-145 Add large-document performance benchmarks

- **Status:** done (2026-10-02; opt-in Release benchmark in `tests/core/performance_budget_tests.cpp`)
- **Outcome:** a benchmark test builds a synthetic 50,000-note, 400-measure chart with a 10-minute generated audio source and asserts budgets for scene composition, hit-test index build, timing index build, and waveform pyramid completion.
- **Read first:** `docs/references/ui-backend-boundary.md` (performance paragraph), `tests/core/editor_scene_tests.cpp`, `tests/core/audio_waveform_tests.cpp`.
- **Files owned:** new `tests/core/performance_budget_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Generate data in-test (no fixture files). 2. Time each phase with `std::chrono::steady_clock`; budgets start at 4× the measured Release value and are listed in the test. 3. Tag `[.performance]` so it is opt-in.
- **Invariants:** tests only.
- **Reject/failure behavior:** a missed budget becomes a new card.
- **Verify:** `cmake -S . -B build/performance -G Ninja -DCMAKE_BUILD_TYPE=Release -DWORMEDIT_BUILD_APP=OFF -DWORMEDIT_ENABLE_LUA=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5`; `cmake --build build/performance --target wormedit_performance_tests -j 4`; `build/performance/tests/wormedit_performance_tests '[.performance]'` (93 assertions, all pass). The separate target is intentionally not registered with CTest; ordinary suites do not run the benchmark.
- **Out of scope:** optimizations; scheduler priorities/work stealing (only if this card shows need).

Implementation evidence: 400 measures, 125 distinct notes per measure, eight lanes and one 160 BPM event per measure (600 seconds total). The viewport shows four measures in the middle of the chart. A generated 44.1 kHz stereo square wave supplies 600 seconds of PCM without fixtures or filesystem writes. Each phase takes the maximum of five runs; fresh waveform caches prevent cache-hit measurements. Setup and result assertions are outside the timed intervals; scene-index preparation is outside the scene-composition budget. Correctness checks cover note counts, exact duration, visible geometry, generated status and complete pyramid coverage. PCM setup temporarily holds about 404 MiB in two copies.

Release reference (2026-10-02, GCC 14.2, AMD EPYC 9V74 cloud VM, five vCPUs, no concurrent build): hit-test index 6.54204 ms → 26.16816 ms budget; timing index 0.145229 ms → 0.580916 ms; scene composition 0.099119 ms → 0.396476 ms; waveform pyramid 126.289 ms → 505.156 ms. These are machine-specific CPU regression budgets, not UI, GPU, codec throughput or physical-device validation. No production optimization is included.

## T-146 Report progress and allow cancel for chart import

- **Status:** blocked-on-user
- **Stale-plan audit (2026-10-03):** Project opening is synchronous (`src/sys/project_open.cpp:427` and `src/sys/main.cpp:2381`); the existing Cancel only dismisses BMS encoding selection. Worker and frontend changes fit their owned files, but the owned main slice permits only status wiring. Actual progress/cancel needs job submission, persistent per-open state, cancel dispatch, frame polling, and success-only document publication in main/application services. Do not hide lifecycle/publication changes behind a global callback to evade that qualifier.
- **Outcome:** opening a large chart shows a progress bar and a Cancel button; cancel leaves the previous document open.
- **Read first:** `src/sys/project_open.cpp`, `include/wormedit/core/async_job.h`, `docs/async-task-scheduling.md`.
- **Files owned:** `src/sys/project_open.cpp/.h`, `src/sys/main.cpp` (status wiring), `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `tests/sys/project_open_tests.cpp`.
- **Steps:** 0. **STOP:** wait for explicit ownership expansion covering main/application job lifecycle, dispatch, polling, and publication. 1. Run parse off the frame thread with a stop token and coarse progress (bytes read / total). 2. Publish only on success for the current generation. 3. Test that a cancelled open publishes nothing.
- **Invariants:** current document untouched until publication.
- **Reject/failure behavior:** errors shown; previous document kept.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'project_open'`.
- **Out of scope:** export/validation progress (T-147).

## T-147 Report progress and allow cancel for export

- **Status:** blocked-on T-146
- **Outcome:** Export and Preview export (T-110) show progress per artifact and can be cancelled without leaving partial output.
- **Read first:** `src/sys/compose_actions.cpp` (export worker), `include/wormedit/formats/conversion_publication.h`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, the Nuklear unit rendering the export button.
- **Steps:** 1. Thread a stop token through conversion and publication (check between artifacts). 2. UI progress/cancel.
- **Invariants:** publication stays atomic; cancel before swap leaves the destination unchanged.
- **Reject/failure behavior:** cancel after swap reports "completed".
- **Verify:** standard build/test.
- **Out of scope:** validation progress.
