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

- **Status:** done (headless projection; pixel view moves to T-314)
- **Outcome:** A single bounded projection lists every Stop, Delay and Warp as a dead span with its row range, duration and kind, consumed by both the canvas and the waveform.
- **Read first:** `include/wormedit/core/document.h`, `include/wormedit/core/timing.h`, `src/sys/timing/timing_view.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/ui/frontend.h`.
- **Files owned:** `src/sys/compose_dead_spans.h/.cpp` (new), `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/sys/compose_dead_spans_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Grep for an existing projection first (rule 9). 2. Define `WormUiComposeDeadSpanView` (kind stop/delay/warp, begin/end row, begin/end seconds, selected, object id). 3. Build it from `timing_index_t` and `warp_event_t::span` with a fixed capacity and a clipped visible window. 4. Test overlapping, adjacent, negative-stop-as-warp and empty charts.
- **Invariants:** Pure projection; the document is never mutated. Stop/Delay occupy zero rows, Warp occupies rows but zero seconds; both are rational-exact until the final pixel conversion.
- **Reject/failure behavior:** Invalid or oversize input yields an empty list and a diagnostic, never partial spans.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Result:** `project_dead_spans` in `src/sys/compose_dead_spans.*` lists stops, delays (merged per row and kind, time taken from the timing index) and zero-time half-open warps, ordered and bounded, with `row_is_in_warp`; 6 tests in `tests/sys/compose_dead_spans_tests.cpp` pass and clang-tidy cognitive complexity is clean. The `frontend.h` view struct and pixel mapping are deferred to T-314, which is their only consumer.

## T-314 Draw Stop and Delay as dead spans that block note placement

- **Status:** done (2026-10-09; waveform gap verified by pixel sampling of `t314-gap-after.png`: the band shows background, not waveform)
- **Remaining:** none.
- **Outcome:** In Compose a Stop or Delay appears as a hatched block (fixed pixel height, label with duration) in the field and waveform; notes cannot be placed, pasted or dragged into it, and the waveform shows the span as eaten time.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/sys/compose/compose_dead_spans.h` (T-313), `src/ui/compose_mode.h`.
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose_waveform_overlays.cpp`, `src/ui/compose_mode.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 0. Add the `frontend.h` dead-span view struct and pixel mapping from the T-313 projection. 1. Draw the block and a waveform gap using the T-313 spans. 2. Reject note insert, paint, paste and move targets inside a span. Edge validity follows the format's discontinuity-order profile in `docs/timing-preview-scheduling.md` (canonical: BPM, delay, activation, then stop; BMSON 1.0: note/BGA, BPM, then stop): a note at a Delay row occurs after the delay, a note at a Stop row before the stop, and a warp's half-open `[begin, end)` rows are blocked. 3. Test placement rejection and unchanged document. 4. Isolated Xvfb check with `scripts/smoke-compose-virtual-x.sh`.
- **Invariants:** Existing notes keep their rows; validity at a Stop/Delay edge comes only from the discontinuity-order profile, never a hard-coded side. Blocking is a placement rule, not a data change.
- **Reject/failure behavior:** A rejected placement leaves document, selection and undo stack unchanged and reports a notification.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Progress (2026-10-02):** Done and compiled in the full app build: `WormUiComposeDeadSpanView` and `dead_spans` in `frontend.h`; per-frame projection in `compose_view.cpp` from the T-313 projection (stop band after its row, delay band before it, 14 px; warp covers its rows; downscroll aware); validation in `compose_panel.cpp`; hatched, labeled bands drawn over the waveform and under notes in `compose_canvas.cpp`; pointer placement blocked inside a band (`hit_target_at` and `position_at_compose_y` in `compose_input.cpp`; timing-marker drags stay unrestricted). Unit-tested: band geometry for up/downscroll and scrolling (`tests/sys/compose_dead_spans_tests.cpp`). Full app suite passed except one load-sensitive performance test that passes alone.
- **Visual check (2026-10-02):** a StepMania fixture with a delay, stop and warp opened in isolated Xvfb shows hatched bands beside the delay and stop rows and over the warp's rows, drawn under notes. (An earlier "Player preview view data is invalid" screen was a stale build mixing objects compiled against two `frontend.h` layouts; a forced rebuild fixed it. The Debug build still asserts in the Compose toolbar at `nk_layout_row_template_push_static`; that predates this card.)
- **Result (2026-10-09):** audited the card against the code. Pointer placement, paste and drag already rejected positions inside Stop/Delay bands (`position_at_compose_y`, `hit_target_at`); tests now prove it with controls (`tests/sys/compose_notification_tests.cpp`: paste and note drop with the pointer in a Stop band leave revision, undo stack and notes unchanged, while the same actions away from the band apply). Two gaps found and fixed: a lane key at a cursor inside a Warp placed a playable head (now rejected in `compose_input.cpp` with "Notes cannot be placed inside a Warp.", using the core `head_blocked_by_warp` rule: warp interior only, first/last row stay valid, holds already pending may end there), and pasting playable heads into a Warp was accepted (`block_paste_heads_in_warps` in `compose_view.cpp` marks the proposal invalid, so the ghost turns red and commit is rejected). Waveform: no separate overlay was needed; the aligned waveform maps audio by time, so the Stop's time collapses into its single row under the hatched band and audio on both sides stays continuous (isolated Xvfb capture with a 10 s tone and a 1.5 s Stop: `sweep/t314-wave.png`). Full suite 3027/3027, event-harness 287/287.
- **Result (2026-10-09, waveform gap):** the song, stem and channel waveforms are now drawn only in the strips outside Stop and Delay bands (`draw_compose_waveforms_outside_pauses` in `compose_canvas.cpp`, strip math in `src/ui/nuklear/compose_waveform_gaps.h`, clip-based, no change to audio row mapping), so the hatched band reads as eaten time like the Warp gap. Test: `waveform strips leave out Stop and Delay bands` in `tests/ui/nuklear_event_harness_tests.cpp`. Capture with the 1.5 s Stop fixture: `sweep/t314-gap-after.png` (before: `sweep/before-t314-wave.png`). Supersedes the earlier note that no overlay was needed.

## T-315 Skip warp audio visually and in playback

- **Status:** done (already implemented: src/audio/aligned_waveform.cpp:56, row mapping by exact time; regression test added)
- **Outcome:** A Warp's span shows as a blank gap in the waveform and the audio is omitted there; playback and seeking never map real audio time into the warped rows.
- **Read first:** `src/sys/compose_waveform_overlays.cpp`, `src/audio/aligned_waveform.cpp`, `src/audio/channel_waveform.cpp`, `src/sys/audio/audio_transport_frame.cpp`, `src/sys/compose/compose_dead_spans.h` (T-313).
- **Files owned:** `src/sys/compose_waveform_overlays.cpp`, `src/audio/aligned_waveform.cpp`, `src/audio/channel_waveform.cpp`, `src/sys/audio/audio_transport_frame.cpp`, `tests/audio/*waveform*_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Reproduce: warp rows currently map to audio. 2. A warp consumes zero time: draw its N-pulse (rational) span as a blank waveform gap because no audio time maps to those rows; the audio on both sides stays contiguous. 3. Playback and the playhead jump from the last pre-warp beat to the post-warp beat with no elapsed time; seeking into a warp row resolves to the post-warp time. 4. Test exact PCM contiguity around a span, seek into a span, and overlapping warps.
- **Invariants:** Mapping stays rational-exact; BPM fitting and the audio offset are unaffected by the omission.
- **Reject/failure behavior:** Invalid spans leave the waveform unchanged and report a diagnostic.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Result:** The aligned waveform already maps audio columns to rows through `musical_position_at`, and a warp consumes no time, so its rows receive no columns and show as a blank gap with contiguous audio on both sides. `tests/core/audio_aligned_waveform_tests.cpp` now locks this (rows 482–958 empty, rows before and after populated; the same chart without the warp is populated). Playback and seeking use the same timing index. The hatched warp band is drawn by T-314.

## T-316 Project Kiai on/off events as merged span pseudonotes

- **Status:** done (headless projection; drawing moves to T-317)
- **Outcome:** Kiai is drawn in Compose as a rectangle covering its whole active area, with head and tail, merging overlaps; the data model keeps point on/off events.
- **Read first:** `include/wormedit/core/document.h`, `include/wormedit/core/marker_editor.h`, `src/sys/timing/timing_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`.
- **Files owned:** `src/sys/compose_kiai_spans.h/.cpp` (new), `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `CMakeLists.txt`, `tests/sys/compose_kiai_spans_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Derive merged intervals from on/off pairs; an open interval ends at the chart end. 2. Draw the rectangle with selectable head and tail handles. 3. Test overlapping, duplicate, leading-off and unmatched events.
- **Invariants:** Projection only: import/export and native schema are unchanged and byte-identical.
- **Reject/failure behavior:** Malformed event lists project to the best valid intervals without mutation.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Result:** `project_kiai_spans` in `src/sys/compose_kiai_spans.*` merges on/off events into disjoint half-open spans (open Kiai extends to the chart end, duplicate on/off ignored, unordered storage tolerated); 5 tests pass and clang-tidy cognitive complexity is clean. Canvas drawing and handles moved into T-317.

## T-317 Edit Kiai areas by dragging head and tail

- **Status:** done (Esc cancel of an in-flight drag not implemented; releasing outside the lane still commits)
- **Outcome:** Dragging the head or tail of a Kiai area, or creating one with the Kiai gesture, issues one undoable command that rewrites the on/off events and merges overlapped neighbors.
- **Read first:** `src/core/marker_editor.cpp`, `include/wormedit/core/marker_editor.h`, `src/sys/compose/compose_kiai_spans.h` (T-316), `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `src/core/marker_editor.cpp`, `include/wormedit/core/marker_editor.h`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`, `tests/core/marker_editor_tests.cpp`, user-approved verification scope extension (2026-10-02): `tests/sys/frame_timing_tests.cpp` (real press-anchor/CREATE gesture payloads and explicit 1/8 snap).
- **Steps:** 0. Add the `frontend.h` view struct and draw the T-316 rectangle with head and tail handles on the canvas. 1. Add a revision-checked core command `set_kiai_span` (insert, resize, delete, merge). 2. Wire it through the standard action pattern; the preview shows the dragged area and overlaps merge only on release. 3. Test merge, split-by-shrink, undo, and retained effect bits.
- **Invariants:** `retained_effect_bits` and provenance of surviving events are preserved.
- **Reject/failure behavior:** A stale revision or inverted span rejects atomically.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Progress (2026-10-02):** Core done and tested headless: `kiai_spans_of` and `apply_kiai_span_edit` (`include/wormedit/core/kiai_spans.h`, `src/core/kiai_spans.cpp`) and the revision-checked `edit_kiai_span_action_t` in `apply_marker_edit` (create, resize, move, delete; overlaps and touching areas merge in the same command; a dragged head or tail keeps its event, id and effect bits; redundant events are dropped; stale revision, empty or out-of-chart rows, unknown head and cap overflow reject without a revision; re-applying the same area is a no-op; one undo step). The T-316 projection now delegates to the same core function. 4 core tests in `tests/core/marker_editor_tests.cpp`.
- **Drawn (2026-10-02):** `WormUiComposeKiaiSpanView` (`frontend.h`), per-frame projection in `compose_view.cpp`, validation in `compose_panel.cpp`, and the amber area with solid head and tail edges inside the Kiai lane in `compose_canvas.cpp`. An Xvfb capture of the osu! kiai fixture shows the area between its On and Off markers.
- **Remaining:** the drag gesture on the head and tail edges (preview while dragging, merge applied on release through this command), with state tests and an Xvfb check.

- **Result:** Kiai is edited as one area in the Kiai lane. Pressing a head or tail edge and dragging shows the resulting area while dragging; releasing sends one `KIAI_SPAN_EDIT` that calls the core command (overlaps merge at that point, the dragged edge crossing the fixed one swaps them, snapping follows the pointer row). Dragging on empty lane creates an area (movement under 4 px is a click), a click on an area body selects it and Delete removes it (`kiai_selected_head`). Stale revision/chart and locked timing reject with a status line. The individual On/Off timing markers are no longer drawn or hit-tested in Compose. Tests: 4 core tests plus 2 sys tests in `tests/sys/frame_timing_tests.cpp` (tail drag, head drag crossing the tail, select and Delete, stale, locked, creation merging into an existing area and undo). An Xvfb capture shows the preview following the pointer and the area shrunk after release (status "Kiai area edited.", document dirty). Full app suite 1456/1456; no new complexity warnings.

## T-318 Add SV impulses in Compose and show a step graph of relative values

- **Status:** done (impulse insertion already implemented: double-click on the SV lane stages an event with an inline value field)
- **Outcome:** The Compose SV lane inserts impulse events and draws a line graph of the relative value that keeps the discrete step shape (horizontal hold, vertical jump) for non-recipes, with the 1.0x line marked.
- **Read first:** `src/sys/timing/timing_view.cpp`, `src/ui/nuklear/timing_panel.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `include/wormedit/core/timing_lane_controller.h`.
- **Files owned:** `src/sys/compose_sv_graph.h/.cpp` (new), `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose/input/compose_input.cpp`, `CMakeLists.txt`, `tests/sys/compose_sv_graph_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Grep how the Timing screen inserts SV/scroll events and reuse its core commands. 2. Project (row, value) breakpoints with a bounded count. 3. Draw latched steps with vertical jumps inside the area on the SV lane, normalized to the visible value range (1.0x marked when in range). 4. Test insertion and projection.
- **Invariants:** Existing SV semantics and exports stay identical.
- **Reject/failure behavior:** Invalid values reject without mutation.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Progress (2026-10-02):** Pure projection done and tested headless: `project_sv_steps` (latched scroll steps by row, last event on a row wins, bounded) and `build_sv_graph` (horizontal holds with vertical jumps, normalized to the values in force in the visible window, a single value centers, 1.0x reference position) in `src/sys/compose_sv_graph.*`; 3 tests in `tests/sys/compose_sv_graph_tests.cpp`.
- **Drawn (2026-10-02):** `WormUiComposeSvGraphPointView` (`frontend.h`), per-frame projection, validation and the cyan step line with the 1.0x reference inside the SV lane. An Xvfb capture of the osu! fixture (SV 2x then 1x) shows the jump, hold and return. Full app suite 1454/1454.
- **Remaining:** SV impulse insertion from the Compose SV lane (reuse the Timing screen's scroll command) and its state test.

- **Result:** The SV lane shows the latched step graph (horizontal holds, vertical jumps, normalized to the visible range, 1.0x reference) and inserting an impulse already works from Compose: a double-click on the lane stages the event and shows an inline value field ("Enter to apply · Esc to cancel"), confirmed by Xvfb capture. The graph was confirmed in a capture of the osu! kiai fixture (2x then 1x). 3 projection tests. Recipe areas, their persistence and their graph are T-319.

## T-319 Keep unmaterialized SV recipes as areas that can be materialized later

- **Status:** done (recipe parameter editing moves to T-320; Lua evaluation is T-189)
- **Outcome:** A recipe applied without materializing persists as a Compose area (range, parameters, sampling) that can be edited and materialized into SV events by an explicit command.
- **Read first:** `include/wormedit/core/document.h`, `src/ui/nuklear/timing_panel.cpp`, `src/sys/timing/timing_edit_runtime.h`, `src/sys/compose/compose_sv_graph.h` (T-318).
- **Files owned:** `src/sys/compose_recipe_areas.h/.cpp` (new), `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `CMakeLists.txt`, `tests/sys/compose_recipe_areas_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:** 1. Reuse `sv_recipe_t` as a persistent document entity (user decision), including native schema round trip. 2. Draw each area on the lane with its resolved value line graph (T-318), normalized to the visible range. 3. Add Materialize and Delete actions, one undo step each. 4. Test save/load round trip and materialization equality with the immediate path.
- **Invariants:** Materializing later yields exactly the same events as materializing immediately.
- **Reject/failure behavior:** A stale revision or overlap policy violation rejects atomically.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Result (2026-10-02):** Recipes were already persistent document entities (`chart.sv_recipes`, native project, undo history); what was missing was the Compose side. Now each stored recipe is an area in the SV lane (SPD lane for SPEEDS output) with its own relative value drawn as a continuous line normalized to the visible range (`project_recipe_areas`, `build_recipe_graph`; disabled recipes are muted, an unevaluable one shows a diagonal instead of a graph). Dragging an edge resizes and dragging the body moves the whole span, both through the core snapped range drag as one undo step with a live preview; clicking selects and Delete removes; right-click opens Disable/Enable, Materialize into events, Delete. The area menu's "Add SV recipe" now creates a linear 1x to 2x recipe over the retained area. New core command `materialize_sv_recipe_action_t` replaces a recipe with the ordinary scroll/speed events export baking would emit for it alone (disabled recipes included, other recipes keep composing, one undo step, a non-evaluable recipe rejects). **Native schema is now 2**: `migrate_v1_to_v2` adds `expression_source` to every recipe, schema 1 files load and recovery payloads of either schema load. **Lua design hook:** `sv_recipe_curve_t::lua_expression` plus an inline `expression_source` (1 to 16384 bytes, no NUL, empty for every other curve) are stored, validated, round-tripped and shown, while sampling, materialization and export reject them as unsupported until T-189; the spec and T-189 describe the additive schema 3 that adds API version, entry point, parameters, hashes and seed. Tests: 4 core materialize/Lua tests, native schema/migration/round-trip tests, 4 projection tests, 2 sys input tests; Xvfb captures show the areas and graphs, the edge drag, the context menu and the materialized events. Full app suite 1467/1467; no new complexity warnings.
- **Remaining:** editing a recipe's curve, values and sampling from Compose (the Timing screen's recipe form is ported by T-320), drag preview for recipes in both lanes is drawn only for the lane under the pointer.

## T-320 Audit the Timing screen and port the remaining functions to Compose

- **Status:** done (SPEEDS and recipe curve editing split into T-322 and T-323 at the user's request)
- **Outcome:** A checked list maps every control of the Timing screen (tap tempo, BPM table, anchors, SPEEDS, recipe form, anchors) to a Compose location; each missing one is ported or explicitly removed.
- **Read first:** `src/ui/nuklear/timing_panel.cpp`, `src/ui/nuklear/tap_timing_panel.cpp`, `src/ui/compose_mode.h`, `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `docs/chart-editor-plan/tasks/compose-timing.md`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/compose_mode.h`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:** 1. List every widget and action of the Timing panel in this card's Result. 2. Port each unported one, one commit each. 3. Verify each with a state test. 4. List every Timing test and where it now lives (ported or superseded).
- **Invariants:** Everything survives in Compose: every Timing screen action maps to a Compose action. Timing tests are ported, not dropped.
- **Reject/failure behavior:** Failures leave the Timing screen untouched until T-321.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.

- **Result (2026-10-02):** Audit of every control of the Timing screen (`timing_panel.cpp`, `tap_timing_panel.cpp`) and where it lives in Compose:

  | Timing screen control | In Compose |
  | --- | --- |
  | Tap tempo panel | Unchanged: a global modeless panel from the Edit menu, independent of the Timing tab. |
  | Active grid, Previous/Next grid | The Compose snap selector and its shortcuts (status line shows the active snap). |
  | Initial BPM and Set initial BPM | Double-click the initial BPM marker in the BPM lane; the Timing section of the inspector keeps its Set initial BPM button. |
  | Counts summary | Two summary lines at the top of the inspector's Timing section. |
  | Meter at measure, Apply meter, Suggest | **Ported:** the Meter row and Suggest buttons in the inspector's "Measure at cursor" section (`WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER`, `apply_cursor_measure_meter`), applied to the cursor measure. Suggestion logic and its test moved to `meter_state.*` / `nuklear_meter_state_tests.cpp`. |
  | Marker drag, double-click edit or insert, staged event editor, delete event | Already in Compose (lane markers, inline editor, inspector Timing event section). |
  | BPM, SV, stop, delay, warp, kiai, anchor lanes | Already in Compose; Kiai is now an area (T-316/T-317), SV has the step graph (T-318), stop/delay/warp are dead spans (T-313/T-314/T-315). |
  | New SV recipe, Cancel selection, range drag, curve drawing | Compose: Add SV recipe in the area menu, recipe areas with value graphs and range drag (T-319). |
  | SV recipe form (output, curve, values, sampling, Apply/Cancel) | **Not ported: T-323.** |
  | SPEEDS markers, hover span, transition fields | **Not ported: T-322** (the staged event editor already offers the SPEEDS transition fields). |

  Tests: 2 new core tests for the meter command (set, no-op, undo, rejected text) and the suggestion test moved to its own file. Full app suite 1469/1469; no new complexity warnings (the existing `dispatch_compose_view_action_group` warning predates this card).

## T-321 Remove the Timing screen

- **Status:** done
- **Outcome:** The Timing tab, `timing_panel.*`, `timing_state.*` and their tests are gone; the app builds and Compose timing focus still works.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/timing_panel.h`, `src/ui/nuklear/timing_state.h`, `CMakeLists.txt`.
- **Files owned:** `src/ui/nuklear/timing_panel.*`, `src/ui/nuklear/timing_state.*`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/*timing*`, `docs/chart-editor-plan/tasks/*.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. T-320, T-322 and T-323 are done. `timing_panel.cpp` also owns shared pieces Compose still calls (`worm_ui_nuklear_render_timing_stage_editor`, the recipe form): move them into Compose files before deleting the screen. 2. Remove the tab and its code. Tests that still apply to behavior now in Compose must exist before deletion (ported per T-320); delete only tests of the removed screen itself. 3. Update plan and docs links that name the screen. 4. Full build, `documentation.` tests and Compose smoke.
- **Invariants:** Document, formats and Compose timing focus are byte-identical in behavior.
- **Reject/failure behavior:** If any check fails, stop and report; do not delete tests to get green.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check of Compose when UI changes.
- **Out of scope:** the other cards in this file and unrelated rendering work.
- **Results (2026-10-02):** `timing_panel.*` is gone; the shared stage editor and view validation live in `src/ui/nuklear/timing_stage_editor.*`, and `timing_state.*` keeps only the stage-editor buffers (its reset test remains). The Timing tab is hidden (`workspace_tab_enabled(timing)` is false, so core rejects switching to it; the `workspace_tab_t::timing` enumerator stays so saved state and the command registry ids keep their numbering). `workspace.open_timing` (F3) now opens Compose with timing focus. Removed from `main.cpp`: the SV recipe draft, meter and grid dispatches and the recipe draft state in `timing_edit_t`. `workspace_tests` and `sv_recipe_tests` now use Metadata/Compose where they used the Timing tab; the Timing-only meter, grid and recipe-draft behavior is covered by the Compose tests added in T-318..T-320. `scripts/smoke-compose-virtual-x.sh` lost its `recipe` modes and clicks Compose in the six-tab row; `timing` mode presses F3. Full app suite 1482/1482; no cognitive-complexity warnings in `main.cpp`, `frontend.cpp`, `timing_stage_editor.cpp` or `application_command_dispatch.cpp`; Xvfb capture shows five tabs and timing focus lanes after F3.
- **Dead-code sweep (2026-10-02):** removed the legacy Timing-tab projection (`populate_timing_view`, marker/grid/speed/recipe-point projections and caches in `timing_view.*`, `flattened.timing` storage), the unused `WORM_UI_TIMING_RECIPE_ACTION_*`, grid and meter action fields and the recipe draft view in `frontend.h`, and `timing_recipe_range_hits` in `main.cpp`. Seven Timing-screen arena projection tests in `frame_projection_arena_tests.cpp` were deleted with the projection they covered; the Compose timing projection tests remain. Suite 1475/1475.

## Deferred osu!mania sampleset lane

Owner request (2026-10-20): eventually allow adding a Compose lane to control the sampleset and addition sampleset when editing osu!mania charts. Plan authoring, undo/redo and import/export preservation when this feature is picked up. This remains future work; see the [specialized-lane extension requirements](extension-friction.md#current-extension-costs-and-acceptance-criteria).

## T-322 Show and edit SPEEDS in Compose

- **Status:** done (the focus-mode inline editor changes the multiplier only; transitions are edited in the full-mode inspector)
- **Outcome:** StepMania SPEEDS appear in the SPD lane as spans with their exact extent and transition, can be inserted, dragged and edited from Compose, and SPEEDS-output recipes keep their areas there.
- **Read first:** `src/ui/nuklear/timing_panel.cpp` (`draw_speed_markers`, `draw_hovered_speed_span`, stage editor transition fields), `include/wormedit/core/timing_lane_controller.h`, `src/sys/timing/timing_view.cpp` (speed projection), `src/ui/nuklear/compose_canvas.cpp`.
- **Files owned:** `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `tests/sys/frame_timing_tests.cpp`, new `src/sys/compose_speed_spans.h/.cpp` and its test, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Project each SPEEDS event to its affected span (from its row to the next event, with the transition length) as a pure, tested function. 2. Draw spans in the SPD lane with hover detail. 3. Make markers and the staged editor accept transition seconds/beats exactly as the Timing screen does. 4. State and projection tests; Xvfb capture.
- **Invariants:** exact rational multipliers and transitions; StepMania export unchanged.
- **Reject/failure behavior:** invalid transitions reject without a revision.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check.
- **Out of scope:** recipe curve editing (T-323), removing the Timing screen (T-321).

- **Result (2026-10-02):** `project_speed_spans` (`src/sys/compose_speed_spans.*`) evaluates SPEEDS through the exact core evaluator and returns each event's transition extent in absolute rows (fractional for seconds transitions), its target multiplier, transition length and unit, and whether the next event clips it; 3 tests. Compose draws every span as a faint band in the SPD lane, stronger under the pointer, and the SPD marker tooltip now reads "Speed 2 · transition 2 beats" (or seconds). Inserting and dragging SPEEDS markers already worked through the lane gestures (a double-click stages the event with the inline value field, confirmed by an Xvfb capture), and the staged event editor in the full-mode inspector already has the transition fields (T-320 audit). Tests: 3 projection tests and one sys test checking the view spans and the marker detail; full app suite 1473/1473; no new complexity warnings.
- **Remaining:** the focus-mode inline editor shows only the multiplier, so a transition is set from the inspector (full mode).

## T-323 Edit SV recipe curve, values and sampling in Compose

- **Status:** done (full-mode inspector; the focus mode has no inspector)
- **Outcome:** selecting a recipe in Compose opens an editor for its output, curve (constant, linear, exponential, bump), exact values, sampling mode and counts, with a live preview of the raw and composed value, Apply as one undo step and Cancel.
- **Read first:** `src/ui/nuklear/timing_panel.cpp` (`render_timing_recipe_form`, recipe draft handling), `include/wormedit/core/sv_recipe.h` (`sv_recipe_draft_t`, `preview_sv_recipe_draft`, `update_sv_recipe_action_t`), `src/sys/main.cpp` (`dispatch_timing_recipe_action`), `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp`, new `src/ui/nuklear/compose_recipe_form.cpp/.h`, `src/ui/frontend.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose_view.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `CMakeLists.txt`.
- **Steps:** 1. Reuse the core draft and preview API (no new model). 2. Port the form fields (exact rationals typed as text, validation messages) into a Compose inspector section for the selected recipe (`application.recipe_selected`). 3. Show the raw candidate and composed preview in the lane while editing. 4. A `lua_expression` recipe shows its source read-only until T-189. 5. Tests for form state and the apply/cancel/stale paths; Xvfb capture.
- **Invariants:** exact rational values; Apply is one undoable command; Cancel and stale revisions leave the document unchanged.
- **Reject/failure behavior:** invalid parameter combinations stay editable and are reported by preview and Apply.
- **Verify:** standard build/test; configured clang-tidy on changed C++; `ctest --test-dir build -R '^documentation\.'`; isolated Xvfb check.
- **Out of scope:** the Lua evaluator (T-189), SPEEDS (T-322), removing the Timing screen (T-321).

- **Result (2026-10-02):** Selecting a recipe in Compose (click its area) shows an "SV recipe" form at the top of the inspector: output (SV, Scrolls, Speeds), curve (constant, linear, exponential, bump) with its exact fractions, sampling (fixed by point count or step rows, or adaptive with tolerance and max points) and, for Speeds, the transition length and unit. Every change previews the candidate in the lane as an amber line without writing; Apply stores it as one undo step through `update_sv_recipe_action_t` (the core validator's message is shown for invalid values and nothing is written), Revert drops the edits and the preview. A Lua expression recipe shows its source read-only and keeps its source and parameters; only its sampling and transition can change. The text/recipe conversion that lived in `main.cpp` and `timing_view.cpp` is now `src/sys/recipe_form.*` (`recipe_form_from_recipe`, `recipe_from_form`, `propose_recipe_form`, `apply_recipe_form`), shared by both screens. `WormUiTimingRecipeCurve` gained `LUA_EXPRESSION` so a Lua recipe can cross the frontend boundary. Tests: 8 core tests (round trip, edits, SPEEDS transition, step rows, malformed text, Lua keeps its source, preview without writing, apply/undo/reject) and one sys test for the form view, the preview graph and dropping a stale preview. Xvfb captures show the form for a selected recipe, the amber bump preview after cycling the curve, and the applied recipe (document dirty, graph now stored).
- **Remaining:** none for this card. The form lives in the inspector, which the timing focus mode hides; focus mode still selects, drags, materializes and deletes recipes.
