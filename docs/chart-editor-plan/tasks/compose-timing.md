# Task cards: Compose timing (retire the Timing screen)

[Back to the task-card index](../tasks.md).

User request, 2026-10-02: remove the Timing screen now that Compose has a timing
focus mode, moving all of its functions back into Compose (nothing may be lost).
User decisions: Kiai stays point events with a derived span pseudonote, an open
Kiai extends to the chart end, and overlaps merge on release; dead spans (Stop,
Delay, Warp) are one shared projection and block notes; Warp consumes zero time
(frame N-1 → beat N-1 → time N-1, frame N → beat N+warp → time N), so the audio
has no mapping in its rows; recipes persist in the document as areas until
materialized, with the SV graph normalized to the visible range and drawn inside
the area in the lane; impulses draw as latched steps with vertical jumps. Order: T-313 → T-314/T-315 → T-316 → T-317 →
T-318 → T-319 → T-320 → T-321. Apply the stale-plan check (rule 9) first.

## T-313 Project dead spans (Stop, Delay, Warp) as one shared view model

- **Status:** ready
- **Outcome:** A single bounded projection lists every Stop, Delay and Warp as a dead span with its row range, duration and kind, consumed by both the canvas and the waveform.
- **Read first:** `include/wormedit/core/document.h`, `include/wormedit/core/timing.h`, `src/sys/timing_view.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/ui/frontend.h`.
- **Files owned:** `src/sys/compose_dead_spans.h/.cpp` (new), `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/sys/compose_dead_spans_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Grep for an existing projection first (rule 9). 2. Define `WormUiComposeDeadSpanView` (kind stop/delay/warp, begin/end row, begin/end seconds, selected, object id). 3. Build it from `timing_index_t` and `warp_event_t::span` with a fixed capacity and a clipped visible window. 4. Test overlapping, adjacent, negative-stop-as-warp and empty charts.
- **Invariants:** Pure projection; the document is never mutated. Stop/Delay occupy zero rows, Warp occupies rows but zero seconds; both are rational-exact until the final pixel conversion.
- **Reject/failure behavior:** Invalid or oversize input yields an empty list and a diagnostic, never partial spans.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-314 Draw Stop and Delay as dead spans that block note placement

- **Status:** blocked-on T-313
- **Outcome:** In Compose a Stop or Delay appears as a hatched block (fixed pixel height, label with duration) in the field and waveform; notes cannot be placed, pasted or dragged into it, and the waveform shows the span as eaten time.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/sys/compose_dead_spans.h` (T-313), `src/ui/compose_mode.h`.
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/ui/compose_mode.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 1. Draw the block and a waveform gap using the T-313 spans. 2. Reject note insert, paint, paste and move targets inside a span. Edge validity follows the format's discontinuity-order profile in `docs/timing-preview-scheduling.md` (canonical: BPM, delay, activation, then stop; BMSON 1.0: note/BGA, BPM, then stop): a note at a Delay row occurs after the delay, a note at a Stop row before the stop, and a warp's half-open `[begin, end)` rows are blocked. 3. Test placement rejection and unchanged document. 4. Isolated Xvfb check with `scripts/smoke-compose-virtual-x.sh`.
- **Invariants:** Existing notes keep their rows; validity at a Stop/Delay edge comes only from the discontinuity-order profile, never a hard-coded side. Blocking is a placement rule, not a data change.
- **Reject/failure behavior:** A rejected placement leaves document, selection and undo stack unchanged and reports a notification.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-315 Skip warp audio visually and in playback

- **Status:** blocked-on T-313
- **Outcome:** A Warp's span shows as a blank gap in the waveform and the audio is omitted there; playback and seeking never map real audio time into the warped rows.
- **Read first:** `src/sys/compose_waveform_overlays.cpp`, `src/audio/aligned_waveform.cpp`, `src/audio/channel_waveform.cpp`, `src/sys/audio_transport_frame.cpp`, `src/sys/compose_dead_spans.h` (T-313).
- **Files owned:** `src/sys/compose_waveform_overlays.cpp`, `src/audio/aligned_waveform.cpp`, `src/audio/channel_waveform.cpp`, `src/sys/audio_transport_frame.cpp`, `tests/audio/*waveform*_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Reproduce: warp rows currently map to audio. 2. A warp consumes zero time: draw its N-pulse (rational) span as a blank waveform gap because no audio time maps to those rows; the audio on both sides stays contiguous. 3. Playback and the playhead jump from the last pre-warp beat to the post-warp beat with no elapsed time; seeking into a warp row resolves to the post-warp time. 4. Test exact PCM contiguity around a span, seek into a span, and overlapping warps.
- **Invariants:** Mapping stays rational-exact; BPM fitting and the audio offset are unaffected by the omission.
- **Reject/failure behavior:** Invalid spans leave the waveform unchanged and report a diagnostic.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-316 Project Kiai on/off events as merged span pseudonotes

- **Status:** ready
- **Outcome:** Kiai is drawn in Compose as a rectangle covering its whole active area, with head and tail, merging overlaps; the data model keeps point on/off events.
- **Read first:** `include/wormedit/core/document.h`, `include/wormedit/core/marker_editor.h`, `src/sys/timing_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`.
- **Files owned:** `src/sys/compose_kiai_spans.h/.cpp` (new), `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `CMakeLists.txt`, `tests/sys/compose_kiai_spans_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Derive merged intervals from on/off pairs; an open interval ends at the chart end. 2. Draw the rectangle with selectable head and tail handles. 3. Test overlapping, duplicate, leading-off and unmatched events.
- **Invariants:** Projection only: import/export and native schema are unchanged and byte-identical.
- **Reject/failure behavior:** Malformed event lists project to the best valid intervals without mutation.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-317 Edit Kiai areas by dragging head and tail

- **Status:** blocked-on T-316
- **Outcome:** Dragging the head or tail of a Kiai area, or creating one with the Kiai gesture, issues one undoable command that rewrites the on/off events and merges overlapped neighbors.
- **Read first:** `src/core/marker_editor.cpp`, `include/wormedit/core/marker_editor.h`, `src/sys/compose_kiai_spans.h` (T-316), `src/sys/compose_input.cpp`.
- **Files owned:** `src/core/marker_editor.cpp`, `include/wormedit/core/marker_editor.h`, `src/sys/compose_input.cpp`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `tests/core/marker_editor_tests.cpp`.
- **Steps:** 1. Add a revision-checked core command `set_kiai_span` (insert, resize, delete, merge). 2. Wire it through the standard action pattern; the preview shows the dragged area and overlaps merge only on release. 3. Test merge, split-by-shrink, undo, and retained effect bits.
- **Invariants:** `retained_effect_bits` and provenance of surviving events are preserved.
- **Reject/failure behavior:** A stale revision or inverted span rejects atomically.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-318 Add SV impulses in Compose and show a step graph of relative values

- **Status:** ready
- **Outcome:** The Compose SV lane inserts impulse events and draws a line graph of the relative value that keeps the discrete step shape (horizontal hold, vertical jump) for non-recipes, with the 1.0x line marked.
- **Read first:** `src/sys/timing_view.cpp`, `src/ui/nuklear/timing_panel.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `include/wormedit/core/timing_lane_controller.h`.
- **Files owned:** `src/sys/compose_sv_graph.h/.cpp` (new), `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_input.cpp`, `CMakeLists.txt`, `tests/sys/compose_sv_graph_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Grep how the Timing screen inserts SV/scroll events and reuse its core commands. 2. Project (row, value) breakpoints with a bounded count. 3. Draw latched steps with vertical jumps inside the area on the SV lane, normalized to the visible value range (1.0x marked when in range). 4. Test insertion and projection.
- **Invariants:** Existing SV semantics and exports stay identical.
- **Reject/failure behavior:** Invalid values reject without mutation.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-319 Keep unmaterialized SV recipes as areas that can be materialized later

- **Status:** blocked-on T-318
- **Outcome:** A recipe applied without materializing persists as a Compose area (range, parameters, sampling) that can be edited and materialized into SV events by an explicit command.
- **Read first:** `include/wormedit/core/document.h`, `src/ui/nuklear/timing_panel.cpp`, `src/sys/timing_edit_runtime.h`, `src/sys/compose_sv_graph.h` (T-318).
- **Files owned:** `src/sys/compose_recipe_areas.h/.cpp` (new), `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `CMakeLists.txt`, `tests/sys/compose_recipe_areas_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Reuse `sv_recipe_t` as a persistent document entity (user decision), including native schema round trip. 2. Draw each area on the lane with its resolved value line graph (T-318), normalized to the visible range. 3. Add Materialize and Delete actions, one undo step each. 4. Test save/load round trip and materialization equality with the immediate path.
- **Invariants:** Materializing later yields exactly the same events as materializing immediately.
- **Reject/failure behavior:** A stale revision or overlap policy violation rejects atomically.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-320 Audit the Timing screen and port the remaining functions to Compose

- **Status:** blocked-on T-314, T-315, T-317, T-319
- **Outcome:** A checked list maps every control of the Timing screen (tap tempo, BPM table, anchors, SPEEDS, recipe form, anchors) to a Compose location; each missing one is ported or explicitly removed.
- **Read first:** `src/ui/nuklear/timing_panel.cpp`, `src/ui/nuklear/tap_timing_panel.cpp`, `src/ui/compose_mode.h`, `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `docs/chart-editor-plan/tasks/compose-timing.md`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/compose_mode.h`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:** 1. List every widget and action of the Timing panel in this card's Result. 2. Port each unported one, one commit each. 3. Verify each with a state test. 4. List every Timing test and where it now lives (ported or superseded).
- **Invariants:** Everything survives in Compose: every Timing screen action maps to a Compose action. Timing tests are ported, not dropped.
- **Reject/failure behavior:** Failures leave the Timing screen untouched until T-321.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

## T-321 Remove the Timing screen

- **Status:** blocked-on T-320
- **Outcome:** The Timing tab, `timing_panel.*`, `timing_state.*` and their tests are gone; the app builds and Compose timing focus still works.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/timing_panel.h`, `src/ui/nuklear/timing_state.h`, `CMakeLists.txt`.
- **Files owned:** `src/ui/nuklear/timing_panel.*`, `src/ui/nuklear/timing_state.*`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/*timing*`, `docs/chart-editor-plan/tasks/*.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Confirm T-320 is done. 2. Remove the tab and its code. Tests that still apply to behavior now in Compose must exist before deletion (ported per T-320); delete only tests of the removed screen itself. 3. Update plan and docs links that name the screen. 4. Full build, `documentation.` tests and Compose smoke.
- **Invariants:** Document, formats and Compose timing focus are byte-identical in behavior.
- **Reject/failure behavior:** If any check fails, stop and report; do not delete tests to get green.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.
