# Task cards: measures and timing

[Back to the task-card index](../tasks.md).

Stage 3 measure, layout, timing-navigation, and beat-fitting leftovers from
[authoring/export](../stages/authoring-export.md). The Timing pane already has
marker drag/delete, kiai/anchor lanes, and a meter-change form
(`timing_meter_*` in `src/sys/main.cpp`); do not rebuild them. Apply the
stale-plan check (rule 9) first.

## T-70 Add Insert/Remove measure controls

- **Status:** ready
- **Outcome:** Compose's measure-rail context offers **Insert measure before**, **Insert measure after**, and **Remove empty measure**, each one undoable command through `apply_measure_edit`.
- **Read first:** `include/wormedit/core/measure_editor.h`, `src/sys/main.cpp` (existing meter form handler calling `apply_measure_edit`), `src/ui/nuklear/compose_canvas.cpp` (measure rail), `src/sys/compose_actions.cpp`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/measure_editor_tests.cpp`.
- **Steps:**
  1. Add three Compose actions through the standard pattern, targeting the measure that contains the cursor.
  2. Insert copies the cursor measure's definition; Remove uses `delete_measure_action_t`.
  3. Show the core rejection message in the status line when Remove fails because the measure is not empty.
- **Invariants:** stable measure IDs; BMS measure-number projections repaired by core, not by the UI.
- **Reject/failure behavior:** nonempty or referenced measures reject with no history entry.
- **Verify:** standard build/test; manual insert, undo, remove on a BMS fixture.
- **Out of scope:** removing nonempty measures (T-71), virtual BMS measures (already materialize on demand).

## T-71 Define nonempty-measure removal

- **Status:** blocked-on-user
- **Outcome:** a recorded policy for removing a measure that contains objects (delete its objects, shift later content earlier, or refuse), then a follow-up card.
- **Read first:** `include/wormedit/core/measure_editor.h`, `docs/chart-editor-plan/document-model.md` (measure ownership paragraph).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:**
  1. STOP: ask the user which policy, and whether timing events inside the measure move or are deleted.
  2. Record the answer as a new open-question entry marked answered; write the follow-up card.
- **Invariants:** none.
- **Reject/failure behavior:** until answered, core keeps rejecting.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-72 Add a global redivision control

- **Status:** ready
- **Outcome:** Metadata's chart section offers a resolution field (default 960, also 192 etc.); Apply runs exact global redivision as one command and reports how many positions moved, or rejects if any position is inexact.
- **Read first:** `include/wormedit/core/document.h` (`redivide_chart_exact`), `include/wormedit/core/command.h`, `src/ui/nuklear/metadata_panel.cpp`, `src/sys/metadata_actions.cpp`.
- **Files owned:** `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/sys/metadata_actions.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp`, `tests/core/command_tests.cpp`.
- **Steps:**
  1. Core: `apply_redivision_command(history, revision, chart, new_resolution)` wrapping `redivide_chart_exact`.
  2. Tests: 960→192 with only 1/4 notes succeeds; with a 1/5 note rejects untouched.
  3. Wire the field and Apply through the standard pattern.
- **Invariants:** exact-only; never rounds.
- **Reject/failure behavior:** an inexact position rejects with the first offending object's measure/row.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** snapping to make redivision possible (that is quantization).

## T-73 Suggest time signatures from a measure length

- **Status:** ready
- **Outcome:** when a measure length is entered, the meter form lists candidate signatures (for example length 3/4 → 3/4, 6/8) and fills the chosen one.
- **Read first:** `docs/chart-editor-plan/document-model.md` (measure length paragraph), `include/wormedit/core/measure.h`, `src/ui/nuklear/timing_panel.cpp` (meter form).
- **Files owned:** `include/wormedit/core/measure.h`, `src/core/measure.cpp`, `tests/core/measure_tests.cpp`, `src/sys/timing_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/timing_panel.cpp`.
- **Steps:**
  1. Core: `suggest_meters(rational length) -> bounded list` using denominators 4, 8, 16 in that order.
  2. Tests for 1, 3/4, 5/4, 7/8, 1/3 (no suggestion).
  3. Show suggestions as buttons in the meter form.
- **Invariants:** suggestions never change the length.
- **Reject/failure behavior:** no candidate → no buttons.
- **Verify:** standard build/test.
- **Out of scope:** automatic detection on import.

## T-74 Add a lane-layout change command at a measure boundary

- **Status:** ready
- **Outcome:** a core command inserts, updates, or removes a `lane_layout_segment_t` at a measure start, validating that every note after it maps to an existing lane.
- **Read first:** `include/wormedit/core/document.h` (`lane_layout_t`, `lane_layout_segment_t`), `docs/chart-editor-plan/decisions/open-questions.md` (question 6), `include/wormedit/core/lane_profiles.h`.
- **Files owned:** new `include/wormedit/core/lane_layout_editor.h`, new `src/core/lane_layout_editor.cpp`, new `tests/core/lane_layout_editor_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Use the provisional answer to question 6 (measure boundaries only) behind a policy enum; say so in the report.
  2. Insert/update/delete actions with revision checks.
  3. Tests: 7K→5K segment with notes in lanes 6–7 afterwards rejects; empty region succeeds; undo restores.
- **Invariants:** notes are never moved implicitly.
- **Reject/failure behavior:** unmapped notes reject with their count.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (T-75), mid-measure changes.

## T-75 Expose lane-layout changes in Compose

- **Status:** blocked-on T-74
- **Outcome:** right side of the measure rail shows layout segments; an inspector form inserts or edits one at the cursor measure.
- **Read first:** `include/wormedit/core/lane_layout_editor.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_canvas.cpp`.
- **Steps:** 1. Project segments. 2. Form with profile picker (from `built_in_bms_lane_profiles` or the chart's family). 3. Wire through the standard pattern.
- **Invariants:** field geometry already follows segments; do not change the scene composer.
- **Reject/failure behavior:** core rejection text shown in the status line.
- **Verify:** standard build/test; smoke PNG.
- **Out of scope:** per-chart key-count migration (T-126).

## T-76 Use the active snap for Timing-pane insertion

- **Status:** ready
- **Outcome:** double-clicking an empty Timing-lane row snaps to the active Compose snap instead of the fixed quarter beat, falling back to one measure only if the resolution cannot represent it.
- **Read first:** `src/sys/timing_view.cpp`, `src/sys/timing_edit_runtime.cpp`, `include/wormedit/core/grid.h`.
- **Files owned:** `src/sys/timing_view.cpp`, `src/sys/timing_edit_runtime.cpp`, `tests/ui/nuklear_timing_state_tests.cpp`.
- **Steps:** 1. Find the fixed quarter-beat snap constant. 2. Replace it with the workspace's active snap. 3. Test both a supported and an unsupported snap.
- **Invariants:** Compose snap state is read, never changed, by the Timing pane.
- **Reject/failure behavior:** unsupported snap uses the documented measure fallback.
- **Verify:** standard build/test.
- **Out of scope:** Up/Down navigation inside the Timing pane.

## T-77 Project audio-time bookmarks onto beats for navigation

- **Status:** ready
- **Outcome:** Previous/Next marker navigation also stops at audio-time bookmarks and section boundaries, converted to exact rows through the timing index.
- **Read first:** `include/wormedit/core/marker_navigation.h`, `include/wormedit/core/timing.h` (`musical_position_at`), `tests/core/marker_navigation_tests.cpp`.
- **Files owned:** `include/wormedit/core/marker_navigation.h`, `src/core/marker_navigation.cpp`, `tests/core/marker_navigation_tests.cpp`, `src/sys/main.cpp` (enablement condition only).
- **Steps:**
  1. Accept an optional timing index; when present, include audio-time markers by inverse lookup (warp side = entry).
  2. Tests: marker inside a stop maps to the stop row; out-of-range marker is skipped with a diagnostic.
  3. Enable the navigation commands when any marker kind exists.
- **Invariants:** beat-anchored behavior unchanged when no timing index is passed.
- **Reject/failure behavior:** timing index failure falls back to beat markers only.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** converting a marker's anchor type.

## T-78 Decide local beat-drag and time-anchored note behavior

- **Status:** blocked-on-user
- **Outcome:** answers to open questions 5 and 7 recorded, then follow-up cards for local Ctrl-drag beat fitting, downstream locked-anchor compensation, and BPM edits on time-anchored notes.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/chart-editor-plan/playback-audio.md` (beat-fit modes).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP: ask questions 5 and 7. 2. Record answers. 3. Add cards for the local fit solver and the timing-edit policy.
- **Invariants:** none.
- **Reject/failure behavior:** unanswered → no cards.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-79 Preview audio-timed objects during beat fitting

- **Status:** ready
- **Outcome:** during offset and interval-fit drags, audio-time bookmarks/sections stay fixed on the waveform while beat-anchored objects move; the preview labels the two groups differently.
- **Read first:** `src/sys/compose_waveform_overlays.cpp`, `src/sys/compose_input.cpp` (fit drag), `memory/chart-editor-status/implemented/editing-frontend.md` (fit preview paragraph).
- **Files owned:** `src/sys/compose_waveform_overlays.cpp/.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/frontend.h`.
- **Steps:** 1. Draw audio-time markers from their sample frame, not from rows, during the drag. 2. Use the existing committed-marker color for them and amber only for moving beat objects.
- **Invariants:** no document change until commit.
- **Reject/failure behavior:** cancel restores the prior overlay exactly.
- **Verify:** standard build/test; manual drag with one audio-time bookmark.
- **Out of scope:** onset snapping (T-80).

## T-80 Detect waveform onsets for fit snapping

- **Status:** ready
- **Outcome:** a core function returns onset frames for a source range from the existing peak/RMS pyramid, deterministic for a given input.
- **Read first:** `include/wormedit/audio/waveform.h`, `include/wormedit/audio/aligned_waveform.h`, `tests/core/audio_waveform_tests.cpp`.
- **Files owned:** new `include/wormedit/audio/onset.h`, new `src/audio/onset.cpp`, new `tests/core/audio_onset_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. RMS-rise detector with fixed window and threshold parameters. 2. Tests with a generated click track (onsets within ±1 window) and silence (none).
- **Invariants:** bounded output count; no allocation proportional to source length beyond the result.
- **Reject/failure behavior:** incomplete waveform ranges return "not ready", not partial onsets.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`.
- **Out of scope:** UI (T-81).

## T-81 Snap fit drags to the nearest onset

- **Status:** blocked-on T-80
- **Outcome:** holding the snap modifier during an interval-fit drag snaps the target frame to the nearest onset within 30 ms.
- **Read first:** `include/wormedit/audio/onset.h`, `src/sys/compose_input.cpp` (fit drag update), `src/sys/compose_waveform_overlays.cpp`.
- **Files owned:** `src/sys/compose_input.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/ui/nuklear/compose_input_panel.cpp`.
- **Steps:** 1. Query onsets for the visible range once per drag. 2. Snap and draw a tick at the chosen onset.
- **Invariants:** without the modifier, behavior is unchanged.
- **Reject/failure behavior:** no onset in range → unsnapped.
- **Verify:** standard build/test; manual drag.
- **Out of scope:** local Ctrl-drag fit (T-78 follow-up).
