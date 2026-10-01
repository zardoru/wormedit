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

## T-71 Remove nonempty measures by deleting their contents

- **Status:** ready
- **Outcome:** removing a measure that still contains objects deletes its notes, holds that start in it, and BGM/BGA events in it (after a confirmation that lists counts), moves timing events located inside it to its start row, and shifts everything after it earlier by the measure's length, as one undoable command (user decision 2026-09-30).
- **Read first:** `include/wormedit/core/measure_editor.h` (`delete_measure_action_t`, `apply_measure_edit`), `src/core/measure_editor.cpp`, `docs/chart-editor-plan/document-model.md` (measure ownership paragraph), T-70.
- **Files owned:** `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp`, `tests/core/measure_editor_tests.cpp`, `src/sys/compose_actions.cpp`, `src/ui/nuklear/compose_inspector.cpp` (confirmation only).
- **Steps:**
  1. Proposal function returning the counts of objects to delete and timing events to move, without mutating.
  2. Command: delete contained objects; holds that start in the measure are deleted, holds that start earlier and end inside are shortened to the measure start; timing events inside move to row 0 of the measure (keeping source order) before the measure is removed; later content keeps its musical position relative to the following measures (stable measure IDs make this automatic).
  3. Confirmation in the UI shows the counts; Remove on an empty measure needs no confirmation (T-70 behavior).
  4. Tests: delete with notes, with a hold crossing in, with a BPM change inside; undo restores everything.
- **Invariants:** stable IDs of untouched objects; BMS measure-number projections repaired by core.
- **Reject/failure behavior:** stale revision or a branch-owned object inside the measure rejects with the reason.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** merging measures.

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

- **Status:** done (user decision 2026-09-30: question 7 → preserve only the previous anchor, the following grid moves; question 5 → notes move with the beat grid by default, keeping audio time is an explicit option. Follow-ups: T-283, T-280.)
- **Outcome:** answers recorded in `docs/chart-editor-plan/decisions/open-questions.md`.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. Recorded. 2. Follow-up cards T-283 and T-280 added.
- **Invariants:** none.
- **Reject/failure behavior:** none.
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

## T-235 Toggle the metronome on and off

- **Status:** done
- **Outcome:** a metronome toggle (status-bar icon button, command `transport.toggle_metronome`, default off) enables or silences metronome clicks immediately, including during playback, without rebuilding the preview schedule.
- **Read first:** `src/sys/chart_preview_state.cpp` (`include_metronome = !metronome_bindings.empty()`), `include/wormedit/audio/preview_bridge.h` (`metronome_head_`), `src/sys/audio_source_registration.cpp` (internal metronome sources), `src/ui/nuklear/frontend.cpp` (`render_status_bar`).
- **Files owned:** `include/wormedit/audio/preview_bridge.h`, `src/audio/preview_bridge.cpp`, `tests/core/audio_preview_bridge_tests.cpp`, `src/sys/chart_preview_state.cpp/.h`, `src/sys/main.cpp` (state and dispatch only), `src/core/command_registry.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`; user-approved immediate-silence and wiring extensions: `include/wormedit/audio/transport.h`, `src/audio/transport.cpp`, `include/wormedit/audio/preview_session.h`, `src/audio/preview_session.cpp`, `src/sys/application_commands.h`, `src/sys/application_commands.cpp`, `src/sys/application_frame_projection.cpp`, `src/sys/compose_view.h`, `src/sys/application_command_dispatch.cpp`, `src/sys/transport_player_actions.cpp`.
- **Steps:**
  1. Always compile the metronome queue; add an atomic `metronome_enabled` flag to the bridge. While disabled, the bridge advances the metronome head without scheduling voices, so re-enabling resumes on the next beat rather than replaying missed clicks.
  2. Test: disabled → zero metronome voices over 4 measures; enable mid-play → first click lands exactly on the next beat frame.
  3. Status-bar button (Lucide `audio-lines` or a metronome-like glyph from the vendored subset) and registry command; the state persists for the session. T-51's Timing Settings panel reuses the same flag.
- **Invariants:** music, keysound, and BGM scheduling unchanged; Stop semantics unchanged.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual toggle during playback. Integrated build and full suite 957/957 passed (50.71s, `/tmp/wormedit-t235-t248-integrated-ctest.log`); literal core/audio prefix 849/849 passed (21.21s, `/tmp/wormedit-t235/core-audio-prefix.log`). Real isolated Xvfb/SDL-dummy UI confirmed default off, toggle on/off during playback, selected icon retained after Stop, and the corrected one-row Compose footer/minimap (`/tmp/wormedit-t235/ui-corrected-compose.png`). Actual parser/registry/command and icon dispatcher harness passed unavailable rejection, checked state and exactly two callbacks (`/tmp/wormedit-t235/commands`). Actual SDL postmix capture passed at 1x, 0.1x and 0.1x with keep pitch: disable tails were zero frames within measured device/stretcher buffer budgets; disabled music stayed at approximately 0.1 amplitude and re-enable restored clicks. Logs `/tmp/wormedit-t235/postmix.log`, `/tmp/wormedit-t235/postmix-slow-final.log`, `/tmp/wormedit-t235/postmix-slow-pitch.log`; raw resampling has measured finite-sinc ringing at rectangular click edges, with no music silence. User-authorized isolated PCM verification replaces physical listening; physical listening remains unperformed. Production/new-test clang-tidy checks passed; old test complexity warnings are unchanged.
- **Out of scope:** metronome volume and accent patterns (T-51).

## T-236 Estimate BPM and phase from taps

- **Status:** done
- **Outcome:** a headless `tap_tempo_estimator_t` turns tap times (in audio source frames of the playing song) into a BPM, a beat-zero phase (the audio frame of a downbeat), and a confidence, using the last 4 measures of taps.
- **Read first:** `include/wormedit/core/timing.h` (frames/seconds conversions), `include/wormedit/core/rational.h` (exact float→rational helper), `docs/chart-editor-plan/playback-audio.md` (beat-fit modes).
- **Files owned:** new `include/wormedit/core/tap_tempo.h`, new `src/core/tap_tempo.cpp`, new `tests/core/tap_tempo_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Window: the last `4 × beats_per_measure` taps (16 in 4/4) held in a fixed-size array; no heap growth.
  2. Estimate with a least-squares line through (beat index, tap frame), not an average of intervals: the slope is frames per beat, the intercept is the phase. Keep running sums (Σi, Σt, Σi², Σit) updated as taps enter and leave the window. Also offer a "whole session" mode that keeps only the sums, with no window.
  3. Beat indices: the first tap is beat 0; each later tap's index advances by the nearest integer number of estimated beats, so a skipped tap does not halve the tempo. The first 4 taps use raw intervals to seed the estimate.
  4. Outliers: after the seed, infer the nearest positive integer beat advance first, then ignore an interval per advanced beat under 0.5× or over 1.75× the current estimate. Rejected taps retain the last accepted anchor; 2 seconds without an accepted tap resets the estimator (also exposed through an explicit expiration check). This skipped-beat clarification was approved by the user on 2026-09-30.
  5. Outputs: BPM (double and exact rational from the helper), suggestions rounded to 1 and 0.5 BPM, phase frame, residual standard deviation in ms, tap count, and "stable" when at least 8 taps and std-dev under 12 ms.
  6. Tests with synthetic taps: exact 120 BPM → 120.000; ±10 ms jitter → within 0.3 BPM; one skipped tap; one double tap; tempo change resets through the window; the downbeat phase falls within 5 ms of the true value.
- **Invariants:** deterministic; no allocation after construction. Meters of 1–32 configured beats use a fixed 128-entry maximum array; zero rate or an unsupported meter rejects taps. The first tap is assigned beat zero; confidence is coverage up to eight taps multiplied by a linear residual score that reaches zero at 24 ms.
- **Reject/failure behavior:** fewer than 4 taps → no estimate.
- **Verify:** integrated build passed; `ctest --test-dir build --output-on-failure -R '^core\.'` passed 726/726, including 10/10 tap-tempo cases; the full suite passed 913/913. Exact 120 BPM, ±10 ms jitter within 0.3 BPM, skipped and double taps, four-measure tempo adaptation, phase within 5 ms, whole-session sums, two-second expiry, invalid input, long source offsets, and no allocation after construction are covered. Private isolated compilation also passed 10 cases / 65 assertions. Clang-tidy is clean for both new sources.
- **Out of scope:** UI (T-237), audio onset detection (T-80).

## T-237 Add an osu!-style tap timing assistant

- **Status:** done
- **Outcome:** a modeless "Tap timing" panel shows a circle divided into N segments (N = beats per measure of the current meter, default 4) that light in turn on the estimated beat while the song plays, with a bright flash on the downbeat; the tap key (T, or Space while the panel has focus) records taps at the event-time estimate of the audible music source frame; the panel shows BPM, ms per beat, offset ms, deviation ms, and taps n/16, with Reset.
- **Read first:** `include/wormedit/core/tap_tempo.h`, `src/sys/audio_transport_frame.cpp` (current source frame), `src/ui/nuklear/player_preview.cpp` (a modeless overlay to copy the window pattern from), `src/core/command_registry.cpp`.
- **Files owned:** new `src/ui/nuklear/tap_timing_panel.cpp/.h`, new `src/sys/tap_timing_runtime.cpp/.h`, `src/ui/frontend.h`, `src/sys/main.cpp` (ownership, dispatch, and view only), `src/core/command_registry.cpp`, `CMakeLists.txt`, `src/ui/nuklear/frontend.cpp`, `src/sys/application_commands.h/.cpp`, new `tests/sys/tap_timing_runtime_tests.cpp`, `tests/CMakeLists.txt`, `tests/sys/application_commands_tests.cpp`, `tests/core/command_registry_tests.cpp` (scope extensions approved by the user).
- **Steps:**
  1. The runtime owns the estimator; SDL event timestamps are mapped onto the audible music source-frame estimate by checked rate-aware interpolation within an unchanged transport epoch. The backend has no historical sample-exact device-position record; seek, Stop, rate changes, and snapshots spanning a transition invalidate queued older events. No frame-start position is used.
  2. The circle is drawn with `nk_fill_arc`, one segment per beat, using the theme roles (accent for the current beat, selected for the downbeat).
  3. Tapping works only while the song is playing; otherwise show "Play the song, then tap on the beat".
  4. Registry commands `timing.tap_assistant` (open/close) and `timing.tap` (tap).
- **Invariants:** tapping never edits the document.
- **Reject/failure behavior:** stale transport sessions (seek/stop) reset the estimator.
- **Verify:** integrated application build passed; the full suite passed 936/936 in 33.39 seconds, including the tap runtime, parser, registry, and frontend guards. New production functions and new test cases pass the cognitive-complexity check. Controlled events over generated audio in isolated Xvfb with SDL dummy audio replace manual tapping as approved: T records ten taps; focused Space records five after Reset; 0.5× playback with one-second taps estimates 120.656 BPM with 3.278 ms deviation (stable); seek to 19 seconds, rate changes, pause, and Stop clear the estimate. The complete panel, accent segment, and downbeat flash were inspected. Saving the native fixture preserves all canonical document fields after the existing revision-zero load normalization; a separate held-key/open-panel/release/close-panel control permits two subsequent lane-note edits, proving release routing remains usable. Evidence: `/tmp/wormedit-t237/visual/`, `/tmp/wormedit-t237/final-production-tidy.log`, `/tmp/wormedit-t237-t264-final-ctest.log`.
- **Out of scope:** clicks (T-238), applying the result (T-239).

## T-238 Click on the estimated beat while tapping

- **Status:** done
- **Outcome:** while the assistant is open, the metronome clicks on the **estimated** grid (not the chart's timing) so the user can hear whether the estimate matches the music; the accent click falls on the estimated downbeat; the T-235 toggle controls it.
- **Read first:** `include/wormedit/audio/preview_bridge.h`, `src/audio/preview_bridge.cpp`, `src/sys/tap_timing_runtime.h`.
- **Files owned:** `include/wormedit/audio/preview_bridge.h`, `src/audio/preview_bridge.cpp`, `tests/core/audio_preview_bridge_tests.cpp`, `src/sys/tap_timing_runtime.cpp`, `src/sys/tap_timing_runtime.h`, `include/wormedit/audio/preview_session.h`, `src/audio/preview_session.cpp`, `src/sys/main.cpp` (assistant publication only; these four extensions explicitly approved by the user).
- **Steps:**
  1. Add a bounded "assist click" queue to the bridge that the control thread refills with the next few predicted beat frames (lookahead about 200 ms) from the current estimate.
  2. While it is active, chart metronome clicks are muted so the two grids never sound together.
  3. Test: with a 120 BPM estimate, clicks land exactly on the predicted frames; changing the estimate replaces only clicks not yet scheduled.
- **Invariants:** voices are scheduled on the control thread only; `render()` unchanged.
- **Reject/failure behavior:** no estimate → no assist clicks.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; user-authorized isolated generated-audio PCM and Xvfb checks substitute for physical listening.
- **Out of scope:** count-in.

Verification (2026-09-30): seven bridge regressions pass (62 assertions), including exact 120 BPM frames and meter accents, retained handed voices, replacement of unscheduled predictions, the 0.1x exclusive lookahead boundary, invalid-grid rejection, and no clicks beyond music EOF. The actual audio-prefix command passes 110/110; the integrated suite passes 984/984. The session publishes on the control thread under its mutex; main caches unchanged publications and republishes on playback epochs. Real runtime/session/SDL dummy postmix checks show no clicks without an estimate or after Reset/toggle-off, an accented estimated downbeat, chart-grid restoration when hidden, and uninterrupted generated music in every phase. Isolated current-app Xvfb checks cover no estimate, tapping, Reset and closing. Physical listening remains unperformed under the approved substitution. Evidence is in `/tmp/wormedit-t238/`.

## T-239 Apply the tapped BPM and offset to the chart

- **Status:** blocked-on T-237
- **Outcome:** "Apply" in the assistant sets the chart's initial BPM (raw, 1-rounded, or 0.5-rounded, chosen in the panel) and moves the audio offset so the estimated downbeat nearest the chart start becomes beat zero, as one undoable command; the result is previewed as a ghost grid on the waveform before Apply, like the existing offset drag.
- **Read first:** `include/wormedit/core/bpm_editor.h` (`apply_bpm_edit`), `include/wormedit/core/audio_offset_editor.h` (`apply_audio_offset_edit`, offset drag preview), `include/wormedit/core/command.h`.
- **Files owned:** new `include/wormedit/core/tap_timing_apply.h`, new `src/core/tap_timing_apply.cpp`, new `tests/core/tap_timing_apply_tests.cpp`, `src/sys/tap_timing_runtime.cpp`, `src/ui/nuklear/tap_timing_panel.cpp`, `src/sys/compose_waveform_overlays.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Core: one command that updates the row-0 BPM and the signed beat-zero offset together (exact rational offset from the phase frame), revision-checked.
  2. If the chart already has later BPM changes, Apply only sets the initial segment (user decision 2026-09-30); later BPM changes keep their musical positions. Say so in the panel.
  3. Ghost preview reuses the offset-drag overlay path.
  4. Tests: apply on an empty chart; undo restores both values in one step; timing lock rejects.
- **Invariants:** notes keep their beat positions (they move with the grid), as with the existing offset edit.
- **Reject/failure behavior:** a stale revision or unstable estimate (not "stable") disables Apply.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`; manual tap-and-apply on a fixture song.
- **Out of scope:** multi-segment tempo detection.

## T-283 Ctrl-drag a single beat to fit the audio (local fit)

- **Status:** ready
- **Outcome:** Ctrl-dragging an unlocked beat marker in the Compose rail pins that beat to the pointer's audio frame by changing only the BPM segment from the previous anchor (or previous BPM change) to the dragged beat; the grid after it moves with the beat (user decision 2026-09-30, question 7); preview and commit work like the existing interval fit.
- **Read first:** `include/wormedit/core/bpm_editor.h` (`begin_beat_interval_drag`, `update_beat_interval_drag_to_bpm`, `commit_beat_interval_drag`), `include/wormedit/core/timing_anchor_editor.h`, `src/sys/compose_input.cpp` (existing rail drag), `docs/chart-editor-plan/playback-audio.md` (beat-fit modes).
- **Files owned:** `include/wormedit/core/bpm_editor.h`, `src/core/bpm_editor.cpp`, `tests/core/bpm_editor_tests.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`.
- **Steps:** 1. Core solver: insert or update one BPM change at the previous anchor so the dragged beat lands on the target frame exactly (rational). 2. Reuse the interval-fit preview path. 3. Tests: fit with and without an existing BPM change at the previous anchor; locked anchor between them rejects; undo.
- **Invariants:** anchors before the previous anchor never move; locked anchors after the dragged beat block the drag with a message instead of moving.
- **Reject/failure behavior:** non-positive or overflowing BPM rejects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`; manual drag.
- **Out of scope:** preserving both neighbors.

## T-280 Add a time-anchoring policy for BPM edits

- **Status:** ready
- **Outcome:** BPM and timing edits keep every object's musical position by default (objects move in time with the grid, user decision 2026-09-30, question 5); a per-edit option "Keep audio time of notes" re-positions playable notes, BGM, and audio-time markers so their audio times stay fixed, shown in the edit preview.
- **Read first:** `include/wormedit/core/bpm_editor.h` (`apply_bpm_edit`), `include/wormedit/core/timing.h` (`musical_position_at`), `docs/chart-editor-plan/document-model.md` (positioning domain paragraph), `include/wormedit/core/quantize.h` (exact repositioning helpers).
- **Files owned:** `include/wormedit/core/bpm_editor.h`, `src/core/bpm_editor.cpp`, `tests/core/bpm_editor_tests.cpp`, `src/sys/timing_edit_runtime.cpp`, `src/ui/nuklear/compose_inspector.cpp`.
- **Steps:** 1. Policy enum on timing edit commands (default `musical`). 2. `audio` policy: compute each affected object's audio time before, its exact row after (inverse lookup), quantize only if the resolution cannot represent it (report count). 3. Preview counts; checkbox in the timing editor. 4. Tests for both policies with an osu!mania import.
- **Invariants:** default behavior unchanged from today.
- **Reject/failure behavior:** positions that cannot be represented exactly are reported and the edit is rejected unless the user allows quantization.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** per-object anchoring flags.
