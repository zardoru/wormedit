# Task cards: editing depth and performance (stage 6)

[Back to the task-card index](../tasks.md).

Stage 6 from [later work](../stages/later.md). These are independent of each
other unless a card says otherwise. Apply the stale-plan check (rule 9) first.

## T-140 Mirror and shuffle lanes of a selection

- **Status:** ready
- **Outcome:** **Mirror** reverses the lane order of selected playable notes within their stage; **Remap…** applies an explicit lane permutation; both are one command each.
- **Read first:** `include/wormedit/core/interaction.h` (lane-move proposal), `src/core/interaction.cpp`, `include/wormedit/core/lane_profiles.h`.
- **Files owned:** new `include/wormedit/core/lane_transform.h`, new `src/core/lane_transform.cpp`, new `tests/core/lane_transform_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Core proposal reusing lane-move collision validation. 2. Tests: 7K mirror keeps scratch lane fixed; permutation with collision rejects; undo. 3. STOP before adding UI; UI is T-141.
- **Invariants:** rows, durations, sounds, labels, layers unchanged.
- **Reject/failure behavior:** notes whose target lane is missing reject all.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** random shuffle (needs a seed policy).

## T-141 Add a Transform menu for selections

- **Status:** blocked-on T-140
- **Outcome:** Compose has a Transform popup with Mirror, Remap, and Shift rows (T-142) acting on the selection.
- **Read first:** `include/wormedit/core/lane_transform.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`.
- **Steps:** standard pattern; one menu entry per available transform.
- **Invariants:** disabled with an empty selection.
- **Reject/failure behavior:** core messages in the status line.
- **Verify:** standard build/test.
- **Out of scope:** new transforms.

## T-142 Shift a selection or everything after the cursor by N rows

- **Status:** ready
- **Outcome:** a core command shifts selected notes/BGM (or every object after the cursor, optionally including timing events) by an exact signed row count.
- **Read first:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `docs/chart-editor-plan/document-model.md` (positioning-domain paragraph).
- **Files owned:** new `include/wormedit/core/time_shift.h`, new `src/core/time_shift.cpp`, new `tests/core/time_shift_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Proposal with exact cross-measure arithmetic. 2. Tests: shift past chart start rejects; shift across a measure-length change is exact; timing-included variant moves BPM events too.
- **Invariants:** audio-time-anchored objects are not moved (report their count).
- **Reject/failure behavior:** any collision rejects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (in T-141).

## T-143 Save quantization presets and show a report

- **Status:** blocked-on T-56
- **Outcome:** named quantization settings (grid, tie policy, hold scope) persist in the native project; applying one shows a per-row report of moved and colliding objects.
- **Read first:** `include/wormedit/core/quantize.h`, `src/formats/native_project.cpp`, `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/sys/compose_actions.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. Add preset list to the song document (schema migration via T-122). 2. Preset picker + Save/Delete. 3. Report list (bounded 256 rows).
- **Invariants:** older projects load with an empty list.
- **Reject/failure behavior:** duplicate preset names reject.
- **Verify:** standard build/test.
- **Out of scope:** global presets outside the project.

## T-144 Search notes by label

- **Status:** ready
- **Outcome:** a search field selects every note whose label contains the text (case-insensitive) and jumps to the first; Next/Previous cycle.
- **Read first:** `include/wormedit/core/note_label_editor.h`, `src/ui/nuklear/compose_inspector.cpp` (note label field), `src/sys/compose_actions.cpp`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/note_label_editor_tests.cpp`.
- **Steps:** 1. Core helper `find_notes_by_label(chart, text) -> ids` with tests. 2. Wire field and buttons.
- **Invariants:** no document mutation.
- **Reject/failure behavior:** no match → status message.
- **Verify:** standard build/test.
- **Out of scope:** regex.

## T-145 Add large-document performance benchmarks

- **Status:** ready
- **Outcome:** a benchmark test builds a synthetic 50,000-note, 400-measure chart with a 10-minute generated audio source and asserts budgets for scene composition, hit-test index build, timing index build, and waveform pyramid completion.
- **Read first:** `docs/references/ui-backend-boundary.md` (performance paragraph), `tests/core/editor_scene_tests.cpp`, `tests/core/audio_waveform_tests.cpp`.
- **Files owned:** new `tests/core/performance_budget_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Generate data in-test (no fixture files). 2. Time each phase with `std::chrono::steady_clock`; budgets start at 4× the measured Release value and are listed in the test. 3. Tag `[.performance]` so it is opt-in.
- **Invariants:** tests only.
- **Reject/failure behavior:** a missed budget becomes a new card.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'performance'` (or run the test binary with `[.performance]`).
- **Out of scope:** optimizations; scheduler priorities/work stealing (only if this card shows need).

## T-146 Report progress and allow cancel for chart import

- **Status:** ready
- **Outcome:** opening a large chart shows a progress bar and a Cancel button; cancel leaves the previous document open.
- **Read first:** `src/sys/project_open.cpp`, `include/wormedit/core/async_job.h`, `docs/async-task-scheduling.md`.
- **Files owned:** `src/sys/project_open.cpp/.h`, `src/sys/main.cpp` (status wiring), `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `tests/sys/project_open_tests.cpp`.
- **Steps:** 1. Run parse off the frame thread with a stop token and coarse progress (bytes read / total). 2. Publish only on success for the current generation. 3. Test that a cancelled open publishes nothing.
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
