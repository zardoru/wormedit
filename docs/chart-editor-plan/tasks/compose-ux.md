# Task cards: Compose UX, rendering, and performance

[Back to the task-card index](../tasks.md).

Requests from the 2026-09-30 review. Findings that shaped these cards were
reproduced in the real window (Xvfb) on the `matched-timing.sm` fixture:

- Semantic layers work in core and in the UI, but look broken: the new layer is
  not targeted, the success status is overwritten by "Compose navigation
  canceled.", membership buttons are disabled without a hint, and the section
  sits collapsed at the bottom of a long inspector (T-207).
- The recovery panel takes ~250 px above Compose even when nothing is
  actionable (T-208); note tooltips print the lane ID ("lane 13") in a 4K chart
  (T-209).
- The retained area/Space selection interval exists in
  `editor_interaction_state_t::retained_selection_range` but is never drawn
  (T-200).
- Waveform analysis uses a fixed 256 frames per bin
  (`src/sys/audio/waveform_analysis_runtime.cpp`) and has no sample-level path, so
  zoom beyond ~2× adds no detail (T-210).
- During Ctrl+Space playback, `sdl_preview_output_t::service()` runs
  `session_.pump()` (decode refill) on the frame thread, and playhead following
  changes scroll every frame (T-211–T-213).
- osu! hitsound flags survive only inside note provenance text; there is no
  typed model (T-215).

Apply the stale-plan check (rule 9) first.

## T-200 Draw the selected area and give it an action menu

- **Status:** done
- **Outcome:** after an area drag or Space selection, the exact row interval stays visible as a translucent band across note, timing, and BGM lanes, with draggable top/bottom edges; right-clicking inside it opens a menu whose entries call existing commands (Quantize selection, Create section, Loop playback, Delete contents, Add SV recipe, Add SPEEDS span).
- **Read first:** `include/wormedit/core/interaction.h` (`retained_selection_range`, `retained_canvas_selection_range`), `src/sys/compose/views/compose_view.cpp` (the block that projects `retained_selection_range`), `src/ui/nuklear/compose_canvas.cpp`, `docs/chart-editor-plan/editing/interactions.md` (area selection).
- **Files owned:** `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, new `src/ui/nuklear/compose_area_menu.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `CMakeLists.txt`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, new `src/sys/compose_area_actions.cpp/.h`, `tests/CMakeLists.txt`, `src/ui/nuklear/frontend.cpp` (shared action/test and Escape-routing scope extended by user approval on 2026-09-30).
- **Steps:**
  1. Project the retained range as absolute top/bottom rows into the view (the begin/end fields already exist for sections; add pixel-independent rows).
  2. Draw the band under notes, above grid; edges highlight on hover.
  3. Edge drag snaps to the active grid and updates the retained range (frontend state, not a document edit).
  4. Context menu with only the entries whose commands exist today; disabled entries show why. Later cards add their entries here.
  5. Escape or a click outside clears the band.
- **Invariants:** the band never changes the document; selection of notes is unchanged by edge drags unless the user re-selects.
- **Reject/failure behavior:** a range made stale by an edit (measure removed) clears itself, as the projection already does.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure` (838/838 passed, parent rerun 2026-09-30); clang-tidy on affected C++ sources (no new complexity violations). Isolated Xvfb scripted area drag, edge drag and Escape restore, then context-menu capture inspected: `/tmp/wormedit-t200/parent-cancel.png`, `/tmp/wormedit-t200/final-menu.png`. Existing deletion supports notes and BGM; areas with timing/BGA or other unsupported objects disable Delete contents with an explanation. Loop playback and Compose SV/SPEEDS creation remain unavailable and explained in the menu. Logs and incremental patch: `/tmp/wormedit-t200/`.
- **Out of scope:** new area commands beyond menu wiring.

## T-201 Show SV recipes as span objects in the Compose SV lane

- **Status:** done (completed through T-313–T-323)
- **Completion:** Compose projects each recipe as an exact selectable span with a bounded curve,
  supports snapped edge/body drag, exposes the selected recipe form and unapplied preview in the
  inspector, and creates recipes from the retained-area menu. Recipe-area projection and frame
  dispatch tests cover selection, move/resize, Delete, area creation, stale input and form preview.
  The Timing pane referenced below was retired by T-321; do not execute these historical steps.
- **Outcome:** each recipe draws as a bar spanning its exact interval in the Compose SV lane (with its curve drawn inside); clicking it selects it and opens an "SV recipe" section at the top of the inspector with the same fields as the Timing pane recipe form; its ends and body drag with the existing snapped range-drag controller; "Add SV recipe" from the area menu creates one over the band.
- **Read first:** `include/wormedit/core/sv_recipe.h` (`sv_recipe_draft_t::propose`, `sv_recipe_range_drag_t::propose`, `apply_proposal`), `src/ui/nuklear/timing_panel.cpp` (recipe form, `cancel_timing_recipe_range_drag`), `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_timing_lanes`), `src/sys/compose/views/compose_view.cpp`.
- **Files owned:** `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, new `src/ui/nuklear/compose_recipe_inspector.cpp/.h`, `CMakeLists.txt`.
- **Steps:**
  1. Project recipe spans (id, top row, bottom row, enabled, selected, bounded curve polyline) for visible rows.
  2. Draw bars in the SV lane; widen the lane on hover if it is too narrow to grab.
  3. Selection state for "selected recipe"; the inspector section appears first when a recipe is selected.
  4. Move the recipe form widgets from `timing_panel.cpp` into `compose_recipe_inspector.cpp` so both tabs call one implementation (pure move first, then reuse).
  5. Reuse the core range-drag functions for handle dragging.
- **Invariants:** one command per Apply or drag release; Escape cancels a drag.
- **Reject/failure behavior:** core rejection text in the status line; the draft stays open.
- **Verify:** standard build/test; manual: create, drag, edit, undo.
- **Out of scope:** Lua curves (T-191).

## T-202 Show and create SPEEDS as span objects in Compose

- **Status:** done (2026-10-09)
- **Outcome:** each SPEEDS transition draws in the SPD lane as a span from its row over its transition length (beats or seconds, using the existing evaluator spans); "Add SPEEDS span" from the area menu creates a speed event at the band start whose duration equals the band length in beats; dragging the span's end edits the duration.
- **Read first:** `src/sys/timing/timing_view.cpp` (`append_speed_spans_to_cache`), `include/wormedit/core/scroll_speed_editor.h` (`apply_scroll_speed_edit`), `include/wormedit/core/timing_lane_controller.h`.
- **Files owned:** `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/scroll_speed_editor_tests.cpp`.
- **Steps:** 1. Reuse the span computation from `timing_view.cpp` (move it to a shared helper if needed). 2. Draw spans. 3. Create-from-band command. 4. End-handle drag → duration update (one command).
- **Invariants:** seconds-mode spans keep their seconds duration when timing changes.
- **Reject/failure behavior:** a band crossing a warp in seconds mode shows the evaluator's diagnostic.
- **Verify:** standard build/test.
- **Out of scope:** SCROLLS recipes (T-201 covers them).
- **Result (2026-10-09):** audit found spans already drawn (`draw_compose_speed_spans`), the end-edge drag already writing the duration exactly (`speed_transition_between` in `compose_input.cpp`: beats = rows*4/divisions as a reduced rational, or the timing index's elapsed seconds for seconds-mode events), and "Add SPEEDS span" a disabled placeholder. Now `WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN` (area menu, enabled unless the area is virtual) inserts a speed event at the band start with `transition_beats` = band length in beats (neutral 1x multiplier, edited afterwards) through `apply_scroll_speed_edit`, one undo step (`add_speed_span_over_area`, `compose_area_actions.cpp`); no area or an empty one rejects with a warning and changes nothing. Tests in `compose_action_notification_tests.cpp` (2 beats for half a measure, undo, no-area rejection). No menu-level harness test: no precedent for driving the area context menu there. Xvfb capture: `sweep/t202-menu2.png` (menu) and `sweep/t202-after.png` (marker and span in the SPD lane, "SPEEDS span added over the selected area."). The end-drag exactness is by code reading only; it has no new test.


## T-203 Retire the Timing tab once Compose has parity

- **Status:** done (superseded by T-313–T-323; T-321 removed the Timing screen)
- **Completion:** The later Compose-timing lane provided full editing parity and removed the Timing
  screen rather than retaining the provisional hidden-tab fallback described below. F3 opens Compose
  timing focus, and the obsolete Timing projection/action fields were swept afterwards. This
  historical card requires no separate implementation.
- **Outcome:** a parity checklist in this card is all checked, then the Timing tab is hidden behind View → "Show Timing tab" (off by default).
- **Read first:** `src/ui/nuklear/timing_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/core/workspace.cpp` (tab list).
- **Files owned:** `src/core/workspace.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, this card.
- **Steps:**
  1. List every Timing-tab-only feature (grep its actions in `src/ui/frontend.h`). Known today: meter-change form, recipe form, waveform fit view. Check each against Compose.
  2. STOP and show the user any feature without a Compose equivalent; either add a card for it or get approval to drop it.
  3. Hide the tab; keep the code (the command `workspace.open_timing` still works).
- **Invariants:** no timing feature disappears silently.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; smoke PNGs.
- **Out of scope:** deleting `timing_panel.cpp`.

## T-204 Draw real arrow notes for StepMania-style lanes

- **Status:** done
- **Outcome:** lanes with arrow glyphs (`note.glyph` 2–5 in `draw_compose_note_glyph`) draw a filled, outlined arrow shape (StepMania "note" silhouette) rotated per lane, instead of a chevron stroked over a bar tile; fill color follows the note's snap class; hold heads use the same arrow with the body behind it.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_note_glyph`, `compose_note_style`), `src/sys/compose/views/compose_view.cpp` (glyph projection), `docs/chart-editor-plan/editing/layout.md` (snap colors), `docs/chart-editor-plan/decisions/open-questions.md` (question 14).
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, new `src/ui/nuklear/arrow_glyph.cpp/.h`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, new `tests/ui/arrow_glyph_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Pure function returning the arrow polygon (about 7–9 points) for a direction and a square tile, tested for bounds and orientation.
  2. Draw with `nk_fill_polygon` + `nk_stroke_polygon`; keep a square tile (width = lane width) for arrow lanes.
  3. Project a snap class per note (4th, 8th, 12th, 16th, 24th, 32nd, other) and color with the user-confirmed palette of question 14 (red, blue, purple, yellow, pink, orange, grey); make the palette one table so the answer can change it.
  4. Selection, hover, semantic-layer color: draw as an outline ring so the snap fill stays visible.
- **Invariants:** BMS tile lanes and hit-testing unchanged; the player preview can reuse the polygon function later.
- **Reject/failure behavior:** tiles smaller than 10 px fall back to the current tile.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure` (848/848 passed in the parent rerun on 2026-09-30); clang-tidy on all affected C++ files (no new complexity violations). Isolated Xvfb SM captures inspected: `/tmp/wormedit-t204/matched.png` (four orientations), `palette.png` (seven confirmed snap colors and hold head/body/tail), `semantic-selected.png` (snap fill preserved inside selection and semantic-layer rings). Logs and incremental patch: `/tmp/wormedit-t204/`.
- **Out of scope:** textured skins, the player preview.

## T-205 Add a note-kind palette to Compose

- **Status:** done
- **Outcome:** the toolbar shows the note kinds the chart's profile allows (tap, hold, mine, invisible, lift, fake, roll) as toggle buttons with Alt+number-row shortcuts (Alt+1–7; plain digits retain lane entry and Ctrl/Primary+digits retain semantic-layer selection, user decision 2026-09-30); click, lane-key, and paint placement create the armed kind.
- **Read first:** `include/wormedit/core/document.h` (`note_kind_t`), `include/wormedit/core/interaction.h` (placement actions), `src/ui/nuklear/compose_toolbar.cpp`, `src/core/command_registry.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/core/interaction_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/app/main.cpp` (enabled/checked state), `tests/sys/application_commands_tests.cpp`, this card (scope extended by user approval 2026-09-30).
- **Steps:** 1. Armed kind in interaction state (default tap). 2. Per-profile allowed set (BMS: tap, hold, invisible, mine; SM: tap, hold, roll, mine, lift, fake; osu!/Quaver: tap, hold, mine). 3. Registry commands `edit.note_kind_<kind>`. 4. Toolbar buttons with icons. User decision 2026-09-30: clicking Hold/Roll begins the existing hold gesture; painting with Hold/Roll asks the user to use that gesture instead of inventing a duration.
- **Invariants:** hold entry gestures still create holds regardless of the armed tap-like kind (roll when T-170 lands).
- **Reject/failure behavior:** switching to a chart whose profile lacks the armed kind resets to tap.
- **Verify:** parent `cmake --build build` passed; full suite 863/863 passed. Clang-tidy found no new project complexity violations. Isolated Xvfb captures `/tmp/wormedit-next/mine-click.png` and `hold-click.png` inspected: Alt+3 creates a mine and Alt+2 completes a two-click hold. Logs and independent staged-cut syntax checks: `/tmp/wormedit-next/`, `/tmp/wormedit-t205/`.
- **Out of scope:** converting existing notes' kind (add later as a selection command).

## T-206 Redesign the inspector's resource list

- **Status:** done
- **Final verification:** normal/Release builds, 3,321/3,321 full CTest checks and 445 production event cases (6,173 assertions) pass; affected default clang-tidy, documentation and whitespace checks pass. Evidence: `/tmp/sounds-complete-{build,release,ctest,events}.log`.
- **Completion (2026-10-11):** The live Sounds/Images docks use compact ID/Name/Uses/State tables with separated gutters, bounded scrolling and row actions. Single click selects; double click arms on release. Missing rows cannot arm. Sound audition, protected deletion and existing stem drag/drop share the production paths.
- **Coordinator scope (2026-10-11):** T-228 is landed; implement directly in `compose_sounds_panel.cpp`/`compose_bga_panel.cpp` with associated state helpers/tests. Extend live row/request contracts, `sound_list_projection.cpp/.h`, workspace projection, shared resource counts and relevant tests; new `tests/ui/live_resource_table_event_harness_tests.cpp` registered in `tests/CMakeLists.txt`. Root owns shared frontend/sys wiring; workers own distinct UI and projection paths.
- **Outcome:** the Samples/BGM/BGA list becomes a compact table with columns ID (BMS index or slot), Name, Uses, State (ok/missing/unused), plus a per-row audition button; single click selects, double click arms, the filter field from T-103 sits in the header, and the list keeps a fixed height with its own scroll instead of stretching the inspector.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose/views/compose_resource_presenter.cpp`, `docs/chart-editor-plan/document-model.md` (resource list paragraph).
- **Files owned:** `src/ui/nuklear/compose_resources.cpp/.h`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/frontend.h`, `tests/core/compose_resource_presenter_tests.cpp`. Supporting ownership (2026-10-11): live Sounds/Images panel/state paths approved above, `tests/ui/live_resource_table_event_harness_tests.cpp`, existing inspector/advance/stem event fixtures updated to double-click and rendered text truncation, and `tests/CMakeLists.txt`.
- **Steps:** 1. Project uses count and state per row (shared with T-101). 2. Table layout with `nk_layout_row` fixed column ratios and a `nk_group` scroll area of about 12 rows. 3. Audition button calls the existing audition request path.
- **Invariants:** armed-resource behavior unchanged.
- **Reject/failure behavior:** missing rows show the missing state and cannot be armed.
- **Verify:** standard build/test; smoke PNG with a BMS fixture that has many `#WAV` entries.
- **Out of scope:** drag-and-drop from the list (later card). This table is the non-slot view that T-229/T-230 ([panels-menus](panels-menus.md)) move into the Sounds and Images dock panels; if T-228 has landed, build it there directly.

## T-207 Fix semantic-layer usability

- **Status:** done
- **Outcome:** after "Add semantic layer" the new layer is the target and the status says "Semantic layer added"; with a target and a note selection, "Add selected notes to layer" is enabled and labeled that way; clicking a layer while notes are selected offers one-click assignment; the section opens by default and sits above the resource drawer.
- **Read first:** `src/sys/compose/actions/compose_actions.cpp` (`add_semantic_layer`, `semantic_layer_edit_status`, `dispatch_compose_view_action`), `src/ui/nuklear/compose_inspector.cpp` (`render_semantic_layer_page_controls`), the status-message path that writes "Compose navigation canceled.".
- **Files owned:** `src/sys/compose/actions/compose_actions.cpp`, `src/sys/app/main.cpp` (status precedence only), `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_panel.cpp` (section order only), `tests/sys/application_commands_tests.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp` (shared status precedence helper).
- **Steps:**
  1. Find why a later "navigation canceled" status replaces a command's success message in the same frame (grep that string); give command results precedence. If T-222 (notifications) has landed, this step is already done; otherwise make the minimal fix here and T-222 will replace it.
  2. On successful insert, set `semantic_layer_target` to the new layer ID.
  3. Rename the membership buttons: "Add selected notes", "Remove selected notes", "Toggle selected notes", "Select notes in layer". When disabled, show one line saying what is missing ("Select notes first" / "Choose a layer").
  4. Open the section by default and move it above Samples/BGM/BGA.
  5. Regression test for the status precedence.
- **Invariants:** no core change; undo behavior unchanged.
- **Reject/failure behavior:** as today.
- **Verify:** standard build/test; manual repro on `tests/fixtures/stepmania/matched-timing.sm` converted to a native project: add layer, select note, assign, note turns the layer color.
- **Out of scope:** multi-layer notes (open question 10).

## T-208 Hide the recovery panel when nothing is actionable

- **Status:** done
- **Outcome:** with no actionable recovery candidate, no recovery rows render above the workspace; a one-line status remains in the status bar.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (recovery chrome rule), `src/ui/nuklear/frontend.cpp` (recovery rows), `include/wormedit/core/recovery_controller.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/sys/project/workspace_frame_view.cpp`, `src/ui/frontend.h`, `src/sys/workspace_projection.cpp` (recovery availability and bounded projection only).
- **Steps:** 1. Expose "has actionable candidate" in the view. 2. Skip the panel when false.
- **Invariants:** actionable candidates always show.
- **Reject/failure behavior:** none.
- **Verify:** smoke PNG of `compose` shows the toolbar at the top of the content area.
- **Out of scope:** recovery logic.

## T-209 Show display lane numbers in note tooltips

- **Status:** done
- **Outcome:** tooltips say "lane 2" (1-based display order within the chart layout) instead of the internal lane ID.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (tooltip helpers), `src/sys/compose/views/compose_view.cpp` (hover projection).
- **Files owned:** `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`.
- **Steps:** project the display index next to the lane ID and print it.
- **Invariants:** hit-testing unchanged.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test.
- **Out of scope:** lane names.

## T-210 Add waveform detail at high zoom

- **Status:** done
- **Outcome:** zooming Compose past the base level shows finer detail: a 32-frames-per-bin level is generated on demand for the visible range, and below 1 frame per pixel the waveform draws PCM samples from the decoded block cache.
- **Read first:** `include/wormedit/audio/waveform.h` (`waveform_level_for_zoom`, `project_waveform`), `src/sys/audio/waveform_analysis_runtime.cpp` (fixed `256`), `include/wormedit/audio/block_cache.h`, `src/sys/waveform_view_projection.cpp`.
- **Files owned:** `include/wormedit/audio/waveform.h`, `src/audio/waveform.cpp`, `src/sys/waveform_analysis_runtime.cpp/.h`, `src/sys/waveform_view_projection.cpp/.h`, `tests/core/audio_waveform_tests.cpp`, `src/audio/aligned_waveform.cpp`, `include/wormedit/audio/aligned_waveform.h`, `src/sys/application_frame_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp` (visible-range and PCM wiring scope extended by user approval on 2026-09-30).
- **Steps:**
  1. Test first: at 4 frames/pixel the projection returns per-column min/max from a finer source than 256.
  2. Visible-range fine analysis job (bounded, cancelable, keyed by source and range).
  3. Sample-level path: read the visible block(s) from the cache, emit one polyline point per frame.
- **Invariants:** memory bounded by the visible range; the frame thread never decodes.
- **Reject/failure behavior:** while fine data is pending, draw the coarse level.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^audio\.'` (93/93 passed); parent full suite (849/849 passed, 2026-09-30). Clang-tidy on affected C++ sources: no new complexity warnings. Isolated Xvfb zoom captures inspected: `/tmp/wormedit-t210/visual/base.png` (32-frame detail at approximately 4.59 frames/pixel), `zoom.png` (signed stereo PCM at approximately 0.456 frames/pixel). Added short-source/clipped-end regression so source clipping cannot falsely select the sample path. Logs and incremental patch: `/tmp/wormedit-t210/`.
- **Out of scope:** spectrograms.

## T-211 Measure frame time during Ctrl+Space playback

- **Status:** done
- **Outcome:** a View → "Frame timing" overlay shows the last 240 frame times and per-phase averages (event handling, audio service/pump, workspace projection, Compose scene, Nuklear build, render/present); a headless test reproduces play-follow projection on a 50,000-note chart and prints the same phases.
- **Read first:** `src/sys/app/main.cpp` (main loop), `src/sys/audio/sdl_preview_output.cpp` (`service`, `session_.pump()`), `src/sys/compose/views/compose_view.cpp` (scene cache rebuild), `src/sys/app/window.cpp` (VSync).
- **Files owned:** new `src/sys/frame_timing.cpp/.h`, `src/sys/app/main.cpp` (timer calls only), `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, new `tests/sys/frame_timing_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/sys/application_frame_projection.cpp` (timer calls only), `src/sys/compose/views/compose_view.cpp` (timer calls only).
- **Steps:** 1. Fixed ring buffer of phase timings (`SDL_GetPerformanceCounter`). 2. Overlay toggle. 3. Report isolated Xvfb playback measurements and the test environment to the user (user approved replacing the real 144 Hz run on 2026-09-30).
- **Invariants:** zero allocation per frame when the overlay is hidden.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; include the measured table in the report. Implementation, 824 tests, and isolated Xvfb playback/overlay checks passed on 2026-09-30. The user approved isolated measurements instead of real 144 Hz playback verification; automated windows stay outside the user's Niri session. A 50,000-note fixture at 256 notes/measure reproduced the backend's 16-bit vertex-index assertion; the verified 48-notes/measure fixture retains all 50,000 notes. The renderer limit is separate follow-up work.
- **Out of scope:** fixes (T-212, T-213).

Measured isolated playback window (240 frames): Xvfb/X11, SDL GPU renderer,
dummy audio device, 1280×980 window, 50,000-note StepMania chart at 48 notes per
measure, local 48 kHz mono WAV. These are observations in this environment,
not a 144 Hz display benchmark. Audio covers aggregate transport service/pump,
not the decode pump alone.

| Phase | Average (ms) | p99 (ms) |
|---|---:|---:|
| Frame | 51.801 | 54.126 |
| Event handling | 0.064 | 0.710 |
| Audio service/pump | 0.233 | 1.451 |
| Workspace projection | 18.773 | 20.122 |
| Compose scene | 4.385 | 4.926 |
| Nuklear build | 0.701 | 0.774 |
| Render/present | 27.641 | 29.162 |

The aggregate audio p99 does not prove that decode refill alone exceeds T-212's
threshold. Compose scene is not the dominant CPU phase in this run; T-213's
conditional optimization remains unproven.

## T-212 Move preview decode refill off the frame thread

- **Status:** done
- **Profiling evidence:** T-243 final isolated 240-frame playback trace reports decode-pump p99 1.312390 ms, satisfying the greater-than-1-ms gate (`/tmp/wormedit-t243/after-retained-report.log`).
- **Outcome:** only if T-243's split timers show the decode pump alone costs more than 1 ms at p99 (T-211 measured 1.451 ms p99 for service + pump together): the preview session's refill runs on a worker; the frame thread only publishes positions and reads status.
- **Read first:** `src/sys/audio/sdl_preview_output.cpp`, `include/wormedit/audio/preview_session.h` (control-thread contract), `docs/async-task-scheduling.md`.
- **Files owned:** `src/sys/sdl_preview_output.cpp/.h`, `src/audio/preview_session.cpp`, `tests/sys/sdl_preview_output_tests.cpp`.
- **Steps:** 1. Dedicated refill thread with the existing generation guards. 2. Hard Stop still silent on the next render (existing test must pass).
- **Invariants:** `render()` stays allocation-free; Stop semantics unchanged.
- **Reject/failure behavior:** refill failure reports through the existing status path.
- **Verify:** application and SDL test targets built successfully; final full suite passed 919/919 (32.06 s). The continuous-music seek stress passed 30/30 repetitions; its initial one-shot fixture legitimately completed after seeks beyond the sole note and was corrected to keep audio across all seek positions. Frozen staged syntax checks passed 2/2. Required `ctest --test-dir build --output-on-failure -R '^audio\.|sdl_preview'` passed 101/101; the actual SDL namespace `-R '^sdl-audio\.'` passed 9/9, including playback beyond prime windows without frame service, silent next render after hard Stop, repeated seek/Stop/reopen, and an unread refill failure delivered after session completion. New C++ functions pass the complexity gate; the original first SDL test's existing complexity violation is unchanged. Same isolated Xvfb/SDL-dummy T-211 fixture, final 240-frame playing blocks: main-thread pump average/p99 0.141201/1.312390 ms before, result delivery 0.000775/0.001420 ms after; total main audio average/p99 0.163309/1.338880 → 0.022827/0.040530 ms. AFTER measures only main-thread worker-result delivery, not worker decode cost or total decode speed. Dense Compose PNG inspected. Raw tables, report, PNG and summary: `/tmp/wormedit-t212/`; test logs: `/tmp/wormedit-t212-worker-{build,tests,sdl-tests}.log`.
- **Out of scope:** audition output.

## T-213 Reuse the Compose scene while only the scroll changes

- **Status:** done
- **Outcome:** after T-243 removes the larger workspace cost, Compose scene (4.385 ms average in T-211, 63 % of a 144 Hz frame) is the next CPU phase to cut: during playhead following, a scroll change reuses the cached scene for a margin of rows above/below and translates it, rebuilding only when the viewport leaves the cached window or the revision changes.
- **Read first:** `src/sys/compose/views/compose_view.cpp` (`compose_scene_cache_t`, rebuild conditions), `src/core/editor_scene.cpp`.
- **Files owned:** `src/sys/compose_view.cpp/.h`, `src/core/editor_scene.cpp`, `include/wormedit/core/editor_scene.h`, `tests/core/editor_scene_tests.cpp` (core API scope extension approved by user, 2026-09-30).
- **Steps:** 1. Compose one screen of extra rows each side. 2. Rebuild when fewer than a quarter screen remains. 3. Test that translated primitives equal a fresh build at the same scroll.
- **Invariants:** identical pixels to a fresh build.
- **Reject/failure behavior:** any mismatch falls back to rebuilding.
- **Verify:** integrated build passed; three actual-helper fresh-composition tests and ten frame-timing tests passed (13/13). Both scroll directions, fractional scroll, layout transitions, long holds, BGM, cursor changes, invalidation, and primitive-limit rejection are covered. Coordinated isolated Xvfb/dummy-audio capture reduced scene time from 4.058 ms to 0.0617 ms; combined CPU time is about 4.9196 ms. A three-screen window rebuilds at a quarter-screen remaining margin; transient previews and comparison/grouping requests use fresh composition. Projected failures or primitive-limit excess fall back to fresh composition. No new clang-tidy complexity violations.
- **Decision:** user approved advancing T-213 in parallel with T-243 on 2026-09-30; T-243 workspace optimization continues independently.
- **Out of scope:** GPU-side caching.

## T-214 Launch an external previewer (mBMplay and others)

- **Status:** done
- **Outcome:** Preferences hold a list of players with a path and three argument templates (from start, from here, stop) using `<apppath>`, `<filename>`, and `<measure>`; Play/Play here/Stop commands write a temporary BMS of the active chart beside the source (so relative `#WAV` paths resolve) and launch the player with an argument vector. Defaults copy iBMSC 3.5.5.16 (`MainWindow.vb` `pArgsInit`): mBMplay `"<filename>"`, `-s <measure> "<filename>"`, `-t`; uBMplay and o2play `-P -N0 "<filename>"`, `-P -N<measure> "<filename>"`, `-S`.
- **Read first:** `src/formats/bms_writer.cpp` / `include/wormedit/formats/bms_writer.h` (active-projection export), `src/sys/app/main.cpp` (preferences path from the shortcut profile), `src/core/command_registry.cpp`.
- **Files owned:** new `src/sys/external_player.cpp/.h`, new `tests/sys/external_player_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/app/main.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/ui/frontend.h`, new `src/ui/nuklear/external_player_panel.cpp/.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/sys/application_commands.h/.cpp`, `src/sys/compose/views/compose_view.h`, `src/ui/nuklear/frontend.cpp` (scope extension approved by user, 2026-09-30).
- **Steps:**
  1. Pure template expansion into an argument vector (split the template on spaces outside quotes, then substitute; never build a shell string). Tests for quoting and `<measure>` from the cursor's source measure number.
  2. Temp file name `___wormedit_preview.bms` (or `.bme`/`.pms` matching the source) in the chart directory; remember and delete temp files on exit, as iBMSC does.
  3. Launch with `SDL_CreateProcess` (SDL3) without waiting; report failures in the status bar.
  4. On Linux/macOS allow the path to be a wrapper (for example `wine`) with the `.exe` in the argument template.
  5. Register `transport.external_play_start`, `transport.external_play_here`, `transport.external_stop`.
- **Invariants:** the real chart file is never overwritten; no shell is invoked. Preview files are reserved exclusively and cleanup checks file identity; a running player keeps its preview busy. Native BMS projects use their authoritative asset directory when the imported basename is unavailable.
- **Reject/failure behavior:** a missing player path opens the preferences panel; export errors block launch.
- **Verify:** parent full build and 863/863 tests passed; final `ctest --test-dir build --output-on-failure -R 'external_player'` passed 6/6 after test-helper extraction. The actual Python logging stub verified quoted paths, source measure 37, active-branch BME export, busy/collision protection, foreign replacement preservation, and Stop arguments. Isolated Xvfb menu/preferences capture `/tmp/wormedit-next/external-preferences.png` inspected. Changed production/test functions pass clang-tidy complexity checks. Logs: `/tmp/wormedit-next/`, `/tmp/wormedit-t214/`.
- **Out of scope:** non-BMS previewers (add templates per format later).

## T-215 Model osu!-style hitsounds on notes

- **Status:** done (2026-10-20)
- **Verification:** Typed flags/sets/index/volume/filename import, byte-stable unchanged export, edited export and optional native persistence pass. GATE passes 4,318/4,318, whitespace and changed-C++ complexity (`/tmp/wormedit-gate.aCMmTI`).
- **Outcome:** notes carry an optional typed `osu_hitsample_t` (additions whistle/finish/clap, normal set, addition set, custom index, volume, filename); the osu!mania codec reads and writes it instead of relying on provenance text; Quaver `HitSound` flags map to the same type later.
- **Supporting ownership (2026-10-20):** `include/wormedit/core/osu_hitsample.h` owns the small semantic value separately so frontend presentation and audio resolution can use it without importing document storage. The optional native field is backwards-compatible (absent = none), requiring no schema bump.
- **Read first:** `src/formats/osu_mania.cpp` (`build_osu_note_sample`, `osu_note_sample_t`), `include/wormedit/core/document.h` (`note_t`), `docs/references/quaver-qua-format.md` (HitSound flags).
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/osu_mania.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/osu_mania_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. Type + validation. 2. Reader fills it; writer prefers it, falling back to provenance only for untouched notes (byte-stable). 3. Native persistence with migration (absent = none). 4. Round-trip test with every flag and set.
- **Invariants:** unedited osu! files re-export byte-identically.
- **Reject/failure behavior:** invalid set/index values reject on import with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'osu|native_project'`.
- **Out of scope:** UI (T-216), playback (T-217).

## T-216 Add osu!-style hitsound controls to the left note palette

- **Status:** done (2026-10-20)
- **Verification:** Mixed selection edits, one-step Undo, default capture for taps/paint/holds, right-click sample-set choices, shortcut dispatch and native enable/edit/save/reopen tests pass. GATE passes 4,318/4,318 (`/tmp/wormedit-gate.aCMmTI`). Isolated Xvfb captures inspected: osu! palette `/tmp/wormedit-hitsound-palette.png`, non-osu native project enabled through the command menu `/tmp/wormedit-native-hitsound-palette.png`. The workflow and generated shortcuts are in the manual.
- **Mockup:** [hitsound-palette.svg](../mockups/hitsound-palette.svg) (reference: match its structure and hierarchy)
- **Placement (user decision 2026-10-05):** in the left note palette column, below the note-kind buttons and after a separator, not in the toolbar or the Sounds dock. The palette has plenty of vertical room, and people rarely use the Sounds dock and osu! hitsounding at the same time. Users who want custom osu! samples use the Sounds dock's grouped view.
- **Outcome:** on osu!mania and Quaver charts, the palette shows a hitsound group of 30 px square tiles with tooltips:
  - three addition toggles, Whistle, Finish and Clap (whistle/cymbal/clapping-hands icons with persistent off/on/mixed checkbox marks, accent-filled when on);
  - two sample-set selectors, Normal set and Addition set, with music-note/layers icons, `A`, `N`, `S` or `D` (Auto, Normal, Soft, Drum), and a dropdown chevron. A click cycles the set, and a right-click opens a `ContextMenu` to pick one directly.

  The tiles show the current selection, with a mixed state drawn as a dash. With notes selected, a click edits every selected note in one command. With nothing selected, the tiles set what the next placed notes get (the T-412 Draw tool). Keyboard: W/E/R toggle the additions, Shift+Q/W/E/R set the normal set, Alt+Q/W/E/R set the addition set (the osu! editor convention, remappable).
- **Read first:** `include/wormedit/core/document.h` (`osu_hitsample_t` from T-215), `src/ui/nuklear/compose_toolbar.cpp` (`render_compose_note_palette_column`, `compose_note_palette_has_buttons`), `src/ui/nuklear/context_menu.h`, `src/core/command_registry.cpp`, `docs/chart-editor-plan/editing/interactions.md` (hitsound painting paragraph).
- **Files owned:** new `include/wormedit/core/hitsound_editor.h`, new `src/core/hitsound_editor.cpp`, new `tests/core/hitsound_editor_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_panel.cpp` (palette visibility only), `tests/ui/nuklear_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Add a core batch command over a bounded note selection.
  2. Register the commands with the default chords. They apply only in Compose focus, so they don't clash with lane keys (check `diagnose_shortcut_conflicts`).
  3. Project the selection's hitsound state (on, off or mixed for each field) and the "next note" defaults.
  4. Draw the palette group, shown for osu! and Quaver charts and for any chart when the user enables it. Keep the palette visible when it has only hitsound tiles.
  5. Add event-harness cases: toggle Whistle on a mixed selection (all on), cycle the normal set, pick a set from the right-click menu, and set defaults with nothing selected and then draw.
- **Invariants:** BMS keysound assignment (T-54) stays separate. This edits osu! samples, not resources.
- **Reject/failure behavior:** a stale revision rejects. With nothing selected, a click changes only the next-note defaults and never the document.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'hitsound'`; `ctest --test-dir build -L event-harness`; an Xvfb capture of the palette on an osu!mania fixture.
- **Out of scope:** sample playback (T-217) and custom-sample management in the Sounds dock.
- **Supporting ownership (menu lifecycle):** `src/ui/nuklear/context_menu.cpp` implements the existing state owner's shared palette-menu retirement and abandonment next to the native popup functions, keeping state-only test targets free of renderer linkage.
- **Supporting ownership (native workflow/manual):** `tests/sys/application_commands_tests.cpp` covers enabling the palette on a native chart, selection versus placement-default edits, and native persistence. `docs/manual/chapters/sounds-and-layers.adoc` documents that workflow explicitly, including native projects with no osu! import.
- **Supporting ownership (generated reference):** `docs/manual/generated/shortcuts.adoc` is regenerated from the command registry for the hitsound shortcuts and palette command.
- **Owner feedback follow-up (2026-10-20):** `src/ui/nuklear/vector_icons.h` supplies original whistle, cymbal and clap drawings; the existing palette now uses native icon cells with persistent checkbox marks for addition toggles and separate music/layers sample-set selectors with value and chevron. `tests/ui/hitsound_palette_event_harness_tests.cpp` targets the state-aware tooltip identities instead of letter labels; the manual describes the new controls.
- **Supporting ownership (note readability follow-up, 2026-10-20):** `src/ui/hitsound_icons.h` owns shared backend-neutral stroke geometry; `src/ui/frontend/hitsounds.h`, `src/ui/frontend/compose.h` and `src/sys/compose/views/compose_scene_view.cpp` own arena-backed visible-note hitsound presentation. `src/ui/nuklear/compose_canvas.cpp` draws contrast-aware set/addition badges and extended hover details. `tests/sys/frame_timing_tests.cpp` checks arena lifetime; `tests/ui/compose_stem_lane_render_tests.cpp` checks light/dark contrast. Imported custom-set materialization and shared authored voices are deliberately deferred to T-745–T-748, including BMSON layering.
- **Supporting ownership (2026-10-20):** `include/wormedit/core/osu_hitsample.h`, `include/wormedit/core/interaction_state.h`, `include/wormedit/core/interaction_gestures.h`, `src/core/interaction.cpp`, `src/core/interaction_internal.h`, `src/core/interaction_gesture_lifecycle.cpp`, `src/core/note_placement.cpp`: own pure field/change values, next-note defaults, and capture/apply them through existing placement. `src/ui/frontend/hitsounds.h`, `src/ui/frontend/compose.h`, `src/ui/frontend/actions.h`, `src/ui/nuklear/compose_toolbar.h`, `src/ui/nuklear/compose_state.h/.cpp`, `src/ui/nuklear/compose_input_queries.h`, `src/ui/nuklear/frontend.cpp`: typed views/payloads and shared palette-menu lifecycle. `src/sys/app/application.cpp`, `src/sys/application_commands.h/.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/compose/actions/compose_actions.h`, `tests/ui/hitsound_palette_event_harness_tests.cpp`: command capability/dispatch and feature-specific production-input tests.

## T-217 Play osu! hitsounds in preview

- **Status:** done (2026-10-20)
- **Verification:** Map/skin/bundle priority, explicit missing-file fallback, timing-context inheritance, multi-voice gain, audible native-note PCM and capacity rejection pass. GATE passes 4,318/4,318 (`/tmp/wormedit-gate.aCMmTI`). Isolated fixture playback runs with dummy audio (`/tmp/wormedit-hitsound-playback.png`); actual loudspeaker listening was not performed. Bundled samples are original generated CC0 PCM, with the generator and provenance checked in.
- **Outcome:** preview plays normal and addition samples for each note: wormedit ships its own small CC0 default set (normal/soft/drum × hitnormal/whistle/finish/clap), and Options can point to an osu! skin folder whose files override the bundled ones; per-map custom samples always win (user decision 2026-09-30).
- **Read first:** `docs/chart-editor-plan/playback-audio.md`, `include/wormedit/audio/preview_bridge.h`, `src/sys/audio/audio_source_registration.cpp` (internal metronome sources: same pattern for bundled samples), `include/wormedit/core/preferences.h` (T-253).
- **Files owned:** new `assets/hitsounds/` (samples + `LICENSE.md` stating CC0 and provenance), new `src/audio/hitsound_resolver.cpp/.h`, new `tests/core/audio_hitsound_resolver_tests.cpp`, `src/sys/audio/audio_source_registration.cpp`, `src/audio/preview_bridge.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Bundled samples: synthesize them (short procedurally generated clicks/claps written by a checked-in generator script) so they are CC0 by construction; do not copy osu! skin files.
  2. Resolver order: map custom sample → skin folder (`soft-hitwhistle.wav` naming) → bundled default.
  3. Schedule hitsound voices for notes with `osu_hitsample_t` (T-215) through the preview bridge.
  4. Tests for the resolver order and missing-file fallback.
- **Invariants:** BMS keysounds unaffected.
- **Reject/failure behavior:** missing samples fall back silently with one diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual playback of an osu!mania fixture.
- **Out of scope:** skin browsing UI beyond the folder option.
- **Supporting ownership (inheritance):** `include/wormedit/core/osu_hitsample.h`, `include/wormedit/formats/osu_mania.h`, `src/formats/osu_mania.cpp` resolve Auto sets, zero index and zero volume from current-position BPM/SV sample context through one prebuilt index. `tests/support/temporary_directory.h` supplies isolated map/skin directories for resolver-order tests.
- **Supporting ownership (preferences verification):** `tests/core/preferences_tests.cpp` covers the persisted skin directory alongside existing audio options.
- **Supporting ownership (click/live monitoring follow-up, 2026-10-20):** `src/sys/compose/actions/compose_audition_requests.cpp`, `src/sys/project_audio_sources.h/.cpp`, `src/audio/audition_resolver.cpp`, `src/audio/object_audition.cpp` and their public headers reuse resolved hitsound voices for click audition without weakening stem validation. `include/wormedit/audio/preview_bridge.h`, `src/audio/preview_bridge.cpp`, transport and preview-session owners tag/gate hitsound voices independently of BGM, metronome and click audition. Preferences, command registry/dispatch, options runtime, workspace/menu presentation and generated shortcut reference expose persisted Play Hitsounds on Playback, default on; offline export keeps the authored mix. Existing core/audio, SDL, application-command and menu harness tests cover these consumers. No alternate voice scheduler or source-cache invalidation is introduced.
- **Supporting ownership (2026-10-20):** `include/wormedit/audio/hitsound_resolver.h` is the resolver's public header; `scripts/generate-hitsounds.py` generates original bundled PCM. `src/core/timing.cpp`, `include/wormedit/audio/audition_resolver.h`, `src/audio/audition_resolver.cpp`, `src/sys/project_audio_sources.h/.cpp`, `src/sys/audio/chart_preview_state.cpp` extend the existing schedule and binding owners for typed samples without a resource ID. `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/ui/frontend/options_notifications.h`, `src/ui/nuklear/preferences/options_window.cpp`, `src/sys/preferences/options_runtime.cpp`, `src/sys/app/application.cpp` persist and apply the skin-folder option. Existing bridge multi-voice scheduling is reused rather than introducing a second voice scheduler.

## T-240 Stop high-zoom waveform flicker during playback

- **Status:** done
- **Outcome:** at zoom levels below 256 frames per pixel, the Compose waveform keeps one drawing style and a stable data level while the song plays with the view following the playhead; no frame-to-frame alternation between the PCM polyline, the 32-frame level, and the 256-frame level.
- **Cause (found by code reading, 2026-09-30):**
  1. `service_compose_detail` in `src/sys/audio/waveform_analysis_runtime.cpp` keys the detail analysis on the exact visible window `[begin_frame, end_frame)`. While following the playhead the window moves every frame, so the key changes every frame; the code cancels `detail_job_` and resets `detail_analysis_` each time, and a finished job rarely matches the already-moved key. Detail appears for a frame and disappears again.
  2. `populate_compose_waveform_projection` in `src/sys/waveform_view_projection.cpp` chooses all-or-nothing per frame: the PCM path needs every visible block in the runtime's own `decoded_blocks_` cache (which the cancelled job never fills), and the 32-frame level must cover the whole window; otherwise the entire waveform falls back to the 256-frame level, drawn in a different style (peak/RMS envelope instead of sample polyline). Crossing into a new block flips the whole waveform.
- **Read first:** `src/sys/audio/waveform_analysis_runtime.cpp` (`service_compose_detail`), `src/sys/waveform_view_projection.cpp` (`populate_compose_waveform_projection`), `src/audio/aligned_waveform.cpp` (`aligned_waveform_window`, `project_aligned_waveform_samples`), `include/wormedit/audio/block_cache.h`.
- **Files owned:** `src/sys/waveform_analysis_runtime.cpp/.h`, `src/sys/waveform_view_projection.cpp/.h`, `src/audio/aligned_waveform.cpp`, `include/wormedit/audio/aligned_waveform.h`, `src/sys/application_frame_projection.cpp` (playback direction/rate and cache connection), `src/sys/app/main.cpp` (preview-cache wiring; scope extensions approved by user), `tests/core/audio_aligned_waveform_tests.cpp`.
- **Steps:**
  1. Confirm first: with the frame-timing overlay (T-211) or a debug counter, log per frame which path was used (PCM / 32 / 256). Record a 5-second playback at high zoom in the report; it should show the alternation.
  2. Stable detail window: key the detail job on a chunk-aligned range (align to 65,536 frames) that covers the visible window plus one screen of margin behind and two screens ahead in the playback direction. Keep the current result while it still covers the visible window; request the next range when the view passes the middle of the ahead margin. Never reset `detail_analysis_` until its replacement covers the view.
  3. Style follows zoom only: pick the drawing mode from `frames_per_pixel` with hysteresis (enter sample mode below 0.9, leave above 1.1; enter detail level below 230, leave above 280). Availability must not change the mode.
  4. Partial composition: build rows per pixel span from the best level available for that span (PCM blocks, then 32, then 256), converted to the chosen style. In sample mode, spans without PCM draw the 32-frame min/max as the same polyline style.
  5. PCM prefetch: during playback, request the blocks for the ahead margin in the waveform cache; if the preview cache already holds blocks for the same source identity and revision, read them from there instead of decoding twice.
  6. Regression test: a fake job/cache and a window advancing 3 pixels per frame for 600 frames; after the first ahead range completes, assert the chosen level and style never change from one frame to the next, and that no completed result is discarded while it still covers the view.
- **Invariants:** bounded memory (the padded range is bounded by the existing projection column cap); the frame thread never decodes; idle (not playing) behavior matches today.
- **Reject/failure behavior:** if prefetch cannot keep up (for example at very high zoom and 1× rate), keep drawing the chosen style from the best available level; never switch style.
- **Verify:** Parent full build and 881/881 tests passed; audio 98/98 passed. Affected C++ functions pass the complexity check; six selectively staged translation units independently pass syntax compilation. **Verification adjustment (user, 2026-09-30):** a generated 20-second PCM fixture in isolated Xvfb with SDL dummy audio replaces the real-song playback check. Five-second instrumented playback at approximately 0.459 frames/pixel: before, 122 frames (109 PCM, 13 coarse-256) and 26 path transitions; after, 116 frames, all sample style with chosen 32-frame detail level, zero style/level transitions. The final trace includes 2,470 PCM spans, zero detail spans and 19 coarse fallback spans, without changing the chosen style. Mean debugger-observed frame intervals were 41.20 ms before and 43.37 ms after; these are instrumented observations. Final waveform PNG inspected. Per-frame logs, trace summary and syntax results: `/tmp/wormedit-t240/`.
- **Out of scope:** scene reuse during following (T-213), waveform colors.

## T-241 Seek the audio by clicking the status-bar minimap

- **Status:** done
- **Outcome:** a click on the minimap moves the song position to that exact audio frame: while playing, playback continues from there (the following view jumps with it); while paused, a plain click cancels the paused playhead (the pause and its resume point are discarded, as Stop does) and the view jumps, while Ctrl+click (⌘ on macOS) instead moves the paused playhead there and keeps the pause, so Resume continues from the clicked point; a preference swaps which of the two a plain click does; while paused or stopped, the Compose view and edit cursor move to that position, so the next Play from cursor starts there. Dragging keeps today's view scrubbing and seeks once on release; Shift+click only scrolls the view (today's behavior).
- **Current behavior (2026-09-30):** `seek_compose_waveform` in `src/sys/compose/input/compose_input.cpp` (input `WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK`) converts the click fraction to a frame and only scrolls the view to center it. It never touches the transport, and during playback the playhead-following scroll overrides it at once, so a click appears to do nothing.
- **Read first:** `src/ui/nuklear/compose_input_panel.cpp` (`process_compose_overview_input`), `src/sys/compose/input/compose_input.cpp` (`seek_compose_waveform`), `src/sys/audio/chart_preview_state.cpp` (`chart_preview_t::seek`, `resume_frame`, `paused`), `src/sys/app/main.cpp` (`state.transport_seek`), `src/sys/audio/transport_player_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/ui/frontend.h`, `src/sys/compose/input/compose_input.cpp`, `src/sys/chart_preview_state.cpp/.h`, `src/sys/app/main.cpp` (callback wiring, preference projection/preservation), `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `src/sys/compose/views/compose_view.h`, `src/ui/nuklear/frontend.cpp`, new `src/sys/compose_minimap_navigation.cpp/.h`, `CMakeLists.txt`, `tests/CMakeLists.txt` (scope extensions approved by user).
- **Steps:**
  1. Frontend: separate click from drag with the existing 4-pixel threshold; a click emits a new input `WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_CLICK` with the fraction and the Shift and primary-modifier states; a drag keeps emitting `…_OVERVIEW_SEEK` for view scrolling and emits one `…_OVERVIEW_CLICK` at release.
  2. Mapping: frame = `round(fraction × (total_frames − 1))` on the waveform's sample rate. Confirm that the preview schedule clock uses the same frame numbering as the music source (both use the normalized output rate); if not, convert through the timing index and say so in the report.
  3. Playing: call `application_state.transport_seek(frame)`; the following view catches up on the next frame.
  4. Paused (user decisions 2026-09-30; both behaviors are wanted):
     - **Cancel and jump** (plain click by default): stop through the existing path (`application_state.transport_stop` → `chart_preview_t::stop` → `reset_transport()`, which clears `paused` and `resume_frame`), then continue as in step 5.
     - **Move the paused playhead** (primary-modifier click by default): call `application_state.transport_seek(frame)`; `chart_preview_t::seek` keeps `paused = true` and sets `resume_frame`. Also move the view and cursor as in step 5 so what the user sees matches where Resume will start.
     - A boolean preference `minimap_click_moves_paused_playhead` (default false) swaps the two gestures; store it with the other UI preferences (T-228's preferences file when it exists, otherwise session-only).
  5. Stopped (including after step 4): no transport call. Move the view so the frame sits at the playhead anchor, and move the edit cursor to the latest active-snap row at or before that frame, so Play from cursor starts there. No schedule compilation is needed. **Audio-tail decision (user, 2026-09-30):** beyond authored timing, keep view/cursor at the latest valid authored row and retain the exact audio frame for the next Play from cursor, bound to chart, revision, and cursor. Cursor movement or chart/revision changes discard it; Play from Start ignores it. In-range clicks still start from the active snapped cursor.
  6. Shift+click: keep the current `seek_compose_waveform` view-only behavior.
  7. Frames past the chart's compiled end clamp to the end; frames before beat zero seek to the first audible frame (≥ 0).
  8. Tests: click-vs-drag threshold (state test); seek while playing calls the transport once; a plain click while paused stops the transport (no resume frame remains) and moves the cursor; a modifier click while paused keeps the pause with `resume_frame` equal to the clicked frame; the preference swaps both; a click while stopped moves only view and cursor; a drag produces exactly one seek at release.
- **Invariants:** no document change; clicking never starts playback by itself.
- **Reject/failure behavior:** if the transport seek fails while playing, keep playing and show the existing message; view and cursor still jump.
- **Verify:** application build passed; parent full suite 881/881 passed; changed production C++ clang-tidy has no project complexity warnings. Isolated native-chart Xvfb smoke inspected the popup, session preference toggle, plain paused cancel, primary paused move, and drag release. Actual SDL dummy harness passed playing seek frame 352800, paused seek/Resume 264600, Stop clearing pause/resume, exact-tail start 352800, from-start 0, and genuine missing-future-source seek failure restoring active playback at prior frame 0. Logs/screenshots: `/tmp/wormedit-t241/`. Isolated SDL dummy-device measurements for playing seek, paused seek/resume, Stop clearing the pause, exact audio-tail start, and Play from Start. **Verification adjustment (user, 2026-09-30):** isolated Xvfb and SDL dummy measurements replace physical hearing; do not open desktop windows or play physical audio.
- **Out of scope:** hover time tooltip on the minimap (can be a follow-up), scrubbing audio while dragging.

## T-242 Bind lane keys on imported StepMania and osu!mania charts

- **Status:** done
- **Outcome:** pressing 1–4 on a 4K StepMania chart (and the number row on osu!mania/other imported charts up to 10 lanes) places or toggles notes at the edit cursor exactly as on new charts; charts that cannot get a default binding say so in Compose instead of silently ignoring keys.
- **Cause (verified 2026-09-30):** importing `tests/fixtures/stepmania/matched-timing.sm` with the real parser yields `input_profile.profile_id == ""` and **0 bindings**. `src/formats/stepmania.cpp` and `src/formats/osu_mania.cpp` never set `chart.input_profile`; only new charts (`make_number_row_input_profile` in `src/sys/app/application_defaults.cpp`), BMS (`src/formats/bms.cpp`), and BMSON set one. Compose emits `WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS` only for keys found in `chart.input_profile.bindings` (`src/sys/compose/views/compose_view.cpp`), so every lane key is dropped. Native projects saved from such imports persist the empty profile, so the fix must also cover loading. The note-kind palette (T-205) uses Alt+digits and does not conflict.
- **Read first:** `include/wormedit/core/lane_profiles.h` (`make_number_row_input_profile`), `src/sys/app/application_defaults.cpp` (`make_playable_profile`), `src/formats/stepmania.cpp`, `src/formats/osu_mania.cpp`, `src/core/document.cpp` (`validate_input_profile`).
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `tests/core/lane_profile_tests.cpp`, `src/formats/stepmania.cpp`, `src/formats/osu_mania.cpp`, `tests/formats/stepmania_tests.cpp`, `tests/formats/osu_mania_tests.cpp`, `src/sys/project/project_open.cpp`, `tests/sys/project_open_tests.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp` (hint only).
- **Steps:**
  1. Core helper `default_input_profile_for(const lane_layout_t&) -> std::optional<input_profile_t>` that returns the number-row profile (the same bindings new charts get) for single-stage layouts of 1–10 playable lanes, and the existing split-stage helper for split layouts. Tests for 4K, 7K, 10K, 11K (none).
  2. StepMania and osu!mania readers assign it to each chart whose layout qualifies; the profile id records its origin (for example `number-row-default`). Re-import/export must stay semantically equal (the profile is editor data, not written to `.sm`/`.ssc`/`.osu`).
  3. On opening any project or chart (`src/sys/project/project_open.cpp`), fill an **empty** profile (both ID and bindings empty; a named unbound profile is intentional) with the default through a revision-free load normalization (not an undoable edit, and not marking the document dirty); never overwrite a non-empty profile.
  4. When a chart still has no bindings, the lane-key legend / Compose hint shows "No lane keys are bound for this chart" instead of nothing.
  5. Tests: SM and osu! imports have bindings; a native project with an empty profile opens with bindings and is not dirty; a BMS chart keeps its imported profile.
- **Invariants:** existing non-empty profiles are untouched; exported chart bytes unchanged.
- **Reject/failure behavior:** layouts with more than 10 single-stage lanes get no default and show the hint.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stepmania|osu|project_open|lane_profile'`; manual: open an `.sm`, place the cursor, press 2 and 3, notes appear in lanes 2 and 3.
- **Out of scope:** remapping lane keys per chart (T-59 covers shortcuts), StepMania's DFJK-style profiles (add as a named choice later).

## T-243 Cut workspace projection cost during Ctrl+Space playback

- **Status:** done
- **Outcome:** with the T-211 fixture (50,000-note StepMania chart, Compose visible, following playback), "Workspace projection" drops from 18.8 ms average to under 3 ms, and the combined CPU phases (events + audio + workspace + scene + Nuklear) fit in a 6.94 ms (144 Hz) budget except render/present.
- **Evidence (T-211, 2026-09-30):** phases are exclusive (`frame_timing_t::enter` stops the previous phase). Average ms: workspace 18.773, render/present 27.641, scene 4.385, Nuklear 0.701, audio 0.233, events 0.064. CPU without render ≈ 24.2 ms, so 144 Hz is impossible regardless of GPU until workspace projection shrinks.
- **Known waste (code reading):** `project_application_frame` (`src/sys/application_frame_projection.cpp`) forces `flattened.view.timing.visible = true` whenever Compose is visible and calls `populate_timing_view`, which builds the **whole-chart** Timing-tab projection every frame: `populate_speed_cache`, `populate_timing_grid_and_speeds` (grid lines and speed spans for the entire chart), an uncached `build_measure_index(chart)`, `populate_timing_markers` for every timing event, and the recipe/stage views; then it sets `visible = false`. The Compose frontend reads only `chart_id`, `document_revision`, `initial_bpm_*`, `stage_*`, and `sv_recipe` from that view (grep `timing_view` in `src/ui/nuklear/compose_*.cpp`).
- **Read first:** `src/sys/application_frame_projection.cpp`, `src/sys/timing/timing_view.cpp` (`populate_timing_view`), `src/sys/frame_timing.h/.cpp`, `src/sys/project/workspace_frame_view.cpp`, `src/sys/workspace_projection.cpp` (`flatten_workspace`).
- **Files owned:** `src/sys/application_frame_projection.cpp/.h`, `src/sys/timing_view.cpp/.h`, `src/sys/frame_timing.h/.cpp`, `src/ui/nuklear/frontend.cpp` (overlay rows only), `src/ui/frontend.h` (timing-overlay contract), `src/sys/app/main.cpp` (audio, workspace preparation, action and Follow timers; retained Compose geometry wiring), `src/sys/audio/sdl_audition_output.cpp` and `src/sys/audio/sdl_preview_output.cpp` (pump timer scopes only; user-approved extensions, 2026-09-30), `src/audio/aligned_waveform.cpp` (affine timing cursor and coarse-envelope bin reuse; user-approved extension, 2026-09-30), `src/sys/compose/views/compose_view.cpp` (revision-cached Controls measure geometry; user-approved extension, 2026-09-30), `src/sys/compose/compose_playhead_cursor.h`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp` (revision-tagged retained playhead geometry; user-approved extensions, 2026-09-30), `src/sys/waveform_view_projection.cpp` and `src/sys/compose/views/compose_view.h` (overview-cache storage and projection), `tests/CMakeLists.txt` (projection-test linkage; user-approved extensions, 2026-09-30), `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_timing_state_tests.cpp`.
- **Steps:**
  1. Split the workspace phase into sub-phases shown indented in the overlay and the `WORMEDIT_FRAME_TIMING_REPORT` file: flatten, frame view (player preview presenter), metadata, audio waveform views, timing view, compose waveform detail + projection, waveform overlays, source playhead, BMSON projection. Also split "Audio service/pump" into service and pump (feeds T-212). Rerun the T-211 fixture and put the before table in the report.
  2. Add `populate_timing_view_for_compose` that fills only the fields Compose reads (chart id, revision, initial BPM, stage, recipe draft/preview) and skips speed cache, grid, speed spans, markers, and the measure index. Call it instead of the full function when the Timing tab is not active. Keep the full path for the Timing tab.
  3. Cache `build_measure_index` in `timing_scene_cache_t` by document revision for the Timing tab path.
  4. Fix the next-largest sub-phase from step 1 if it exceeds 1 ms average; typical suspects and their fixes: player preview presenter rebuilding its scene per frame when hidden (skip when not visible); `project_waveform_overview` walking the whole song per frame (cache per waveform + revision, only the visible-range marker moves); PCM sample projection calling `musical_position_at` per sample (compute the row of the first sample, then step with the local frames-per-row inside one timing segment).
  5. Test: a state test that Compose-mode projection does not produce timing grid lines/markers and still exposes stage/recipe/BPM fields; the T-211 fixture run's after table.
- **Invariants:** the Timing tab renders exactly as before; Compose inline timing editing, SV recipe drafts, and the initial BPM tile work unchanged.
- **Reject/failure behavior:** none.
- **Verify:** integrated build passed; full suite 913/913 passed, including retained-Follow equivalence against real cursor-jump/scroll dispatch, lane transitions, snap/tail bounds, stale chart/revision and blocked gestures. Changed C++ functions pass the complexity check. Isolated Xvfb T-211 fixture, final 240-frame playing blocks: workspace 15.992 → 1.809387 ms; Timing view 6.669 → 0.001838 ms; Compose waveform 6.007 → 0.751883 ms. CPU excluding render is 3.288593 ms: both the under-3-ms workspace and 6.94-ms CPU outcomes are met. Retained geometry cuts Follow from 1.945129 to 0.056079 ms. Valid dense Compose and full Timing-tab PNGs inspected. AFTER includes separately completed T-213 scene reuse, T-244's 32-bit draw adapter, T-233 with Keep pitch off, and T-246 telemetry. Worktree traces retain the same preexisting source-playhead changes. Before/final tables, raw logs, PNGs and `final-summary.json`: `/tmp/wormedit-t243/`.
- **Out of scope:** render/present cost (T-244, T-245), scene reuse (T-213).

## T-244 Use 32-bit Nuklear draw indices

- **Status:** done
- **Outcome:** dense frames (the 50,000-note fixture at 256 notes per measure that T-211 found tripping the 16-bit vertex-index assertion) render without assertions or missing geometry.
- **Cause:** `include/wormedit/nuklear_config.h` does not define `NK_UINT_DRAW_INDEX`, so `nk_draw_index` is 16-bit and a single draw list overflows past 65,535 vertices; `src/ui/nuklear/frontend.cpp` also builds custom quads with `nk_draw_index`.
- **Read first:** `include/wormedit/nuklear_config.h`, `src/ui/nuklear/frontend.cpp` (the custom quad code near `static_cast<nk_draw_index>` and the geometry submission), `src/sys/app/window.cpp` (renderer).
- **Files owned:** `include/wormedit/nuklear_config.h`, `src/ui/nuklear/frontend.cpp`, `tests/ui/nuklear_player_preview_texture_tests.cpp` or a new `tests/ui/nuklear_draw_index_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Define `NK_UINT_DRAW_INDEX` before including Nuklear everywhere (the config header is the single include point; grep for other `#include <nuklear.h>`). 2. Make the geometry submission pass 4-byte indices to SDL (`SDL_RenderGeometryRaw` with `size_indices = 4`). 3. A test that converts a synthetic command buffer with more than 70,000 vertices and checks index values.
- **Invariants:** identical output for ordinary frames.
- **Reject/failure behavior:** none.
- **Verify:** integrated application build passed; the synthetic conversion test passed with more than 70,000 vertices, in-range indices, and values above 65,535. The original 50,000-note, 256-notes-per-measure native fixture and ordinary Compose frame rendered successfully in isolated Xvfb with SDL dummy audio; both PNGs were inspected. Clang-tidy found no new complexity violations. The single config enables 32-bit indices; a frontend-local adapter forwards their actual width to SDL without changing the bundled backend.
- **Out of scope:** reducing vertex count (T-245).

## T-245 Measure render/present on a real 144 Hz display

- **Status:** done (user decision 2026-09-30: the user cannot run this measurement; render work is judged by T-246's counters instead — vertex count, draw calls, clip/texture changes, convert/submit time — with targets set in T-248–T-250, plus isolated Xvfb timings. Present/VSync wait is not used as an acceptance metric.)
- **Outcome:** replaced by counter-based targets.
- **Read first:** `docs/chart-editor-plan/tasks/rendering.md` (T-246).
- **Files owned:** this card.
- **Steps:** none.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** none.
- **Out of scope:** real-display measurements.

## T-266 Stop the status bar from flickering during playback

- **Status:** done (2026-10-11; overview isolation, stable status layout and native acceptance verified)
- **Outcome:** Playback keeps a stable status row and reserved song-overview row. An invalid overview cannot replace the editable workspace with an error screen; unavailable overview data uses the existing placeholder and rejects pointer interaction.
- **Read first:** src/ui/nuklear/frontend.cpp (status/seek/validation); src/ui/nuklear/compose_panel.cpp (scene and overview validity); src/audio/aligned_waveform.cpp; src/sys/waveform_view_projection.cpp; tests/core/audio_aligned_waveform_tests.cpp.
- **Files owned:** src/ui/nuklear/frontend.cpp (status/seek/validation regions only; font setup belongs to the parallel typography worker); src/ui/nuklear/compose_panel.cpp; src/audio/aligned_waveform.cpp; src/sys/waveform_view_projection.cpp; tests/core/audio_aligned_waveform_tests.cpp; tests/ui/nuklear_compose_state_tests.cpp. Coordinator-approved supporting extensions: src/ui/nuklear/compose_panel.h (independent overview validator exports), tests/ui/playback_status_event_harness_tests.cpp and tests/CMakeLists.txt (dedicated production workflow coverage). Only this task section is owned in the shared card.
- **Cause confirmed (2026-10-11):** The old conditional minimap-column diagnosis was stale: the current overview is already a reserved row with a placeholder. Native playback on a two-second decoded music asset with a 24-measure chart stayed healthy through ten seconds (`/tmp/t266-native-baseline{1,3,6,10}.png`, coordinator inspection); the original past-audio-end `invalid_timing` failure was not reproduced. Production harness injection did confirm that a single malformed overview fraction (`visible_end_fraction=1.00001`) invalidated the entire Compose workspace (`/tmp/t266-before.log`). The status template also conditionally allocated its notification-badge column, allowing clock/artist positions to shift when the badge appeared.
- **Steps:**
  1. Confirm native playback and isolate validation predicates before editing.
  2. Keep status/clock/artist columns stable and elide long UTF-8 text within its allocated space.
  3. Verify existing overview projection clamps views past the audio end and treats off-audio beat zero as hidden.
  4. Audit existing overview identity cache; add a transient fallback only if an actual projection failure warrants it.
  5. Isolate bounded overview validation from chart-scene validation and log the first failing predicate once per distinct cause in Debug builds.
  6. Verify production publication failures, status geometry, text elision, native minimap navigation and complete integration checks.
- **Done so far:** Completed the native/baseline audit above and replaced overview coupling with an independent validator for fractions, metadata, counts, pointers, waveform rows and BGM overview rows/markers. Malformed overview publications no longer reject the editable workspace; overview rendering/input validates its own data and does not publish invalid viewport fractions. Debug diagnostics name exact scene predicates and distinct overview timing/row/window/bar causes once each. Reserved a responsive notification-badge slot and fixed artist/difficulty width per window; clock width stays fixed, and status/metadata use UTF-8-boundary-aware elision. Extracted the cohesive status transport-button helper to keep affected complexity within the limit. Existing project_waveform_overview and cached viewport-fraction projection already clamp to audio bounds; the overview cache already preserves waveform rows by source identity/key while recomputing timing-dependent fractions. No speculative cache or snapping changes were added. All affected production TUs compile using main dependency objects (`/tmp/t266-final-compile.log`, `/tmp/t266-final-frontend-compile.log`). Dedicated production event tests pass 63 assertions / 4 cases (`/tmp/t266-events-final.log`): malformed overview/BGM arrays remain bounded and isolated, valid/absent overview plus notification transitions preserve clock/artist positions, and mixed Japanese text elides. Existing audio-end clamp regression passes 18 assertions / 1 case (`/tmp/t266-audio-clamp.log`). Final affected production clang-tidy complexity checks report no violations (`/tmp/t266-tidy-final.log`); `git diff --check` passes.
- **Final acceptance (2026-10-11):** Normal/release builds, full CTest 3,293/3,293 and all 432 event cases / 6,048 assertions pass. Post-change authenticated TCP Xvfb playback stays healthy from one through ten seconds on the two-second audio fixture; reserved overview and status/clock/artist columns remain visible (`/tmp/t266-native-after{1,3,6,10}.png`). Native stopped overview click moves the viewport and clock from zero to 0:22 without blanking Compose (`/tmp/t266-minimap-{before,after}.png`). Existing clamp and independent malformed-publication event gates pass; original audio-end diagnosis remains unconfirmed. Final integrated frontend/main tidy has no new or increased complexity findings.
- **Remaining:** None.
- **Invariants:** Preserve minimap click/drag/beat-zero behavior (T-15/T-241), authored chart/timing, measure-local snapping and Undo. Do not reuse stale timing-dependent fractions across waveform identity or revision changes.
- **Reject/failure behavior:** Invalid or unavailable overview data disables only overview display/input. Invalid chart-scene contracts still reject the workspace with a diagnostic; debug logs distinguish their exact first failing predicate from overview failures.
- **Verify:** Dedicated `[playback-status]` production event cases; existing audio-end clamp test; affected clang-tidy; documentation checker; normal build/full CTest; authenticated isolated native Ctrl+Space past the audio end and minimap navigation.
- **Out of scope:** Notification-system redesign, tap-tempo panel/runtime contracts, font setup, and speculative new overview caches without a reproduced failure.

## T-269 Give every lane a key color class in its profile

- **Status:** done (2026-10-08 stale-plan audit: steps 1–4 landed per the implementation notes; `assign_lane_key_colors` in `src/core/lane_profiles.cpp`, native JSON `key_color`, Compose projection; Verify passes in the full Debug suite 2,809/2,809 at `6a4ecfe`)
- **Implementation notes (2026-10-01):** steps 1, 2 and 4 are done headless: `lane_key_color_t` and `lane_definition_t::key_color` (appended to the struct), `assign_lane_key_colors(layout)` in `lane_profiles.h` (applied by every BMS/PMS/BMSON-generic built-in profile; osu!/StepMania stay `generic`), native JSON stores `key_color`, files saved without it derive it from the profile id and roles, and an unknown value rejects. PMS 5K stays `generic` because the card only names the 9-button sequence. Tests in `lane_profile_tests.cpp` and `native_project_tests.cpp`. Step 3 (projecting `key_color` into the lane and note views in `compose_view.cpp`) remains.
- **Implementation notes (2026-10-02):** step 3 landed earlier in PR #16 (`compose_key_color` in `src/ui/nuklear/theme.cpp`, per-lane and per-note `key_color` in `compose_view.cpp`). User decision: 10K osu!mania-style lanes (`osu-mania-10k` and `bms-10k-scratchless`) follow osu!mania's canonical `wbwbgybwbw` (keys 5 and 6 pms_green/pms_yellow); `assign_lane_key_colors` applies it and the osu!mania importer now calls it. Core test `osu!mania style 10K uses the canonical wbwbgybwbw key colors` passes.
- **Outcome:** each lane definition carries a `lane_key_color_t` (white, black, scratch, foot_pedal, free_zone, pms_white, pms_yellow, pms_green, pms_blue, pms_red, generic) set by the built-in profiles, and Compose views expose it per lane and per note, so colors follow the instrument instead of display-order parity.
- **Current behavior (2026-09-30):** `compose_lane_color` in `src/ui/nuklear/compose_canvas.cpp` picks lane backgrounds from the role plus `display_order & 1` ("alternate"), which does not match white/black keys once a scratch lane shifts the order, and has no PMS button colors; note fills ignore the lane entirely (`compose_note_style` uses one light blue, or cyan when selected).
- **Read first:** `include/wormedit/core/document.h` (`lane_definition_t`, `lane_role_t`), `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp` (every `make_*_profile`), `src/formats/native_project.cpp` (lane definition JSON), `src/sys/compose/views/compose_view.cpp` (note and lane projection).
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `tests/core/lane_profile_tests.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`.
- **Steps:**
  1. Add the enum and field (default `generic`); native projects store it, older projects derive it on load from the profile id when known, otherwise from role (scratch, foot pedal, free zone) and leave the rest `generic`.
  2. Set it in every built-in profile: BMS/BME 5K/7K/10K/14K keys 1, 3, 5, 7 white and 2, 4, 6 black, scratch lanes scratch, pedals foot_pedal, free zones free_zone; PMS 9K buttons 1–9 white, yellow, green, blue, red, blue, green, yellow, white; generic 24K/48K piano banks white/black by note name; osu!mania and StepMania lanes `generic`.
  3. Project `key_color` into lane and note views.
  4. Tests: 7K with scratch on the left and on the right gives the expected sequence; PMS 9K sequence; native round trip; older project load.
- **Invariants:** exported charts unchanged; detection and lane identities unchanged.
- **Reject/failure behavior:** unknown values in native JSON reject like other enums.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'lane_profile|native_project|^core\.'`.
- **Out of scope:** drawing (T-270).

## T-270 Color notes by their lane and select with a green outline

- **Status:** done (T-269 done 2026-10-08)
- **Result (2026-10-08):** theme file gained `scales` (red, yellow, green, blue, amber, cyan, slate, neutral), a `key_colors` table of 11 entries (note, note_border, lane, hold_body; hold_body is #rrggbbaa) and a `selection_outline` role; the loader reads them with generic fallback and reports malformed JSON on stderr. Tiles fill with their key note color, lane backgrounds use the key lane color without display-order parity, selected tiles get a 2 px green ring outside the tile, the semantic layer color is a 3 px left stripe outside the Layer view, and hold bodies use hold_body. `note_accents` is kept because timing lanes still use it. The new-project 7K is osu-mania 7K, which `color_stage` leaves generic by design, so its lanes stay grey; the 10K capture shows the per-lane key colors (W B W B G Y B W B W) and key-colored notes with a green selection ring. Tests: see the verification run below.
- **Implementation notes (2026-10-02, uncompiled here: no SDL3):** selected notes and hold bodies keep their key-color fill and get a 2 px green outline (`compose_selection_outline_color`, `compose_note_shows_selection_outline` in `compose_canvas.cpp`) instead of turning cyan. The theme-file tokens and the 3 px semantic-layer stripe (steps 1, 3 partly) are still open; confirm with a local build and `scripts/smoke-compose-virtual-x.sh`.
- **Outcome:** tile notes (BMS/BMSON/osu!/PMS lanes) fill with their lane's key color: scratch notes a strong red matching the scratch lane, black-key notes a deeper blue, white-key notes light neutral, PMS buttons their button color; each lane background is a dark shade of the same hue so note and lane read as a pair; selected notes keep their fill and get a 2 px green outline (iBMSC style) instead of turning cyan. Arrow lanes keep the snap colors from T-204.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`compose_note_style`, `compose_lane_color`, hold body drawing), `assets/themes/field/wormedit.json`, `src/ui/nuklear/theme.cpp`, `docs/chart-editor-plan/tasks/panels-menus.md` (T-225 design reference: Tailwind-style scales from the octet.design palette).
- **Files owned:** `assets/themes/field/wormedit.json`, `src/ui/nuklear/theme.cpp/.h`, `src/ui/nuklear/compose_canvas.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:**
  1. Theme: replace `lane_backgrounds` and `note_accents` with one entry per key color holding `note` (about shade 400), `note_border` (about 700), `lane` (about 950), `lane_alt` (about 900), and `hold_body` (note color at reduced alpha), expressed as `$scale.shade` tokens; add `red`, `yellow`, and `green` scales to the theme file if missing; add a `selection_outline` role (green, about shade 400) and keep the existing hover color.
  2. `compose_lane_color` uses `lane`/`lane_alt` by key color, dropping the display-order parity rule.
  3. `compose_note_style`: fill from the note's key color; selected → same fill plus a 2 px `selection_outline` ring drawn outside the tile; hover keeps its ring; semantic-layer color, when visible, becomes a 3 px inner stripe on the left edge so lane color and layer both stay readable (T-207 still works).
  4. Hold bodies use the same hue.
  5. State test for the color lookup per key color and selection state.
- **Invariants:** hit areas unchanged; arrow glyph lanes unchanged; colors come only from the theme file.
- **Reject/failure behavior:** a theme missing an entry falls back to `generic`.
- **Verify:** standard build/test; smoke PNGs of a 7K BMS fixture (scratch left), a PMS fixture, and an SM chart, with a few notes selected.
- **Out of scope:** the field renderer move (T-248 will reuse these colors).


## T-279 Focus inline timing values on double-click and cancel on blur

- **Status:** done
- **Outcome:** double-clicking a Compose BPM or SV point opens a focused value field; typing immediately replaces its value, Enter applies it, and losing focus closes it without applying the draft.
- **Read first:** `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/chrome.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/chrome.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_popup_combo_tests.cpp`, `scripts/smoke-compose-virtual-x.sh`.
- **Steps:**
  1. Focus the upcoming inline text widget through a shared helper and select its value when first opened.
  2. Share the inline timing confirm/cancel lifecycle; cancel on text-field blur, popup dismissal, Escape, and window focus loss. Preserve invalid drafts when requesting focus again; the inspector must preserve already pending inline actions, and frame input ownership must survive popup dismissal so Escape reaches timing cancellation before global shortcuts.
  3. Add real Nuklear regressions for immediate typing, Enter, and deactivation, plus BPM/SV state regressions for confirm, invalid input, blur, and cancel.
- **Invariants:** Enter remains the explicit apply action; losing focus never applies a draft; Escape retains its existing priority; existing audio and waveform work stays unchanged.
- **Reject/failure behavior:** invalid input keeps the editor open and retains the typed draft for correction; dismissal cancels the staged edit without changing the document.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; `clang-tidy -p build src/ui/nuklear/compose_panel.cpp`; `clang-tidy -p build src/ui/nuklear/compose_state.cpp`; `clang-tidy -p build src/ui/nuklear/compose_inspector.cpp`; `clang-tidy -p build src/ui/nuklear/compose_input_panel.cpp`; `clang-tidy -p build src/ui/nuklear/frontend.cpp`; `clang-tidy -p build tests/ui/nuklear_compose_state_tests.cpp`; `clang-tidy -p build tests/ui/nuklear_popup_combo_tests.cpp`; `scripts/smoke-compose-virtual-x.sh /tmp/wormedit-t279-compose.png compose`, then inspect the PNG. Any automated window interaction uses isolated Xvfb and SDL dummy audio, never the user's Niri desktop.
- **Evidence:** application/state/popup build passed; frontend-state 44/44 passed. Isolated Xvfb/SDL-dummy verification checked new and existing BPM/SV immediate typing, Enter, invalid-draft correction, popup blur, window focus loss, Escape, and the first subsequent Project-menu click; saved native values and counts matched. Literal Compose smoke PNG inspected after repairing its stale Create-button hit coordinate and selecting only the test process window. All new functions pass the cognitive complexity gate; unchanged legacy and third-party test warnings remain. Logs, native fixture, scripts, and PNGs: `/tmp/wormedit-t279/`; smoke PNG: `/tmp/wormedit-t279-compose.png`.
- **Out of scope:** timing values, snap behavior, other text fields, audio, waveform rendering, preferences.


## Immediate UX priority (user, 2026-10-01)

T-296–T-303 are short-term priority work, before the ordinary backlog. They supersede historical Space-selection and pause descriptions; completed historical verification remains evidence of the earlier behavior.

## T-296 Use Shift navigation for ranges and Space for Play/Stop

- **Status:** done (Shift exact inclusive range/reversal/cancellation; Space Play/hard Stop with held-key suppression; real Xvfb text-owner and Stop/reset gates, full 1162/1162)
- **Outcome:** Shift plus cursor navigation selects the exact inclusive area range; Space starts playback or invokes hard Stop, never Pause. Focused tap timing uses T; text/IME and modal input retain ownership.
- **Read first:** `include/wormedit/core/interaction.h`, `src/sys/compose/input/compose_input.cpp`, `src/core/command_registry.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/application_commands.h/.cpp`, `src/sys/application_command_dispatch.cpp/.h`, `src/sys/transport_player_actions.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/core/command_registry.cpp`, `src/sys/app/main.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/core/interaction_tests.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 1. Replace Space range recognition with Shift plus keyboard cursor movement, preserving the shared exact range controller. 2. Route Space through Play/hard Stop and suppress repeated toggles. 3. Verify range reversal, cancellation, text focus and immediate audio stop in isolated tests.
- **Invariants:** Shift mouse dragging keeps its offset behavior; navigation alone never edits document data.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

## T-297 Edit the audio offset inline on double click

- **Status:** done
- **Outcome:** Double-clicking the displayed audio offset opens a focused inline numeric editor; Enter commits one exact undoable offset edit, Escape cancels, and losing focus closes it.
- **Read first:** `src/sys/compose/views/compose_view.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `include/wormedit/core/audio_offset_editor.h`.
- **Files owned:** `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 1. Reuse the inline numeric editor and offset command. 2. Focus and select its text on double click and close it on blur. 3. Verify signed values, invalid input, undo, keyboard ownership and isolated actual typing.
- **Invariants:** Audio offset sign, units and exact representation retain the existing command semantics.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.
- **Evidence:** Authoritative application build passed; full suite 1175/1175 and frontend-state 61/61 passed. Configured clang-tidy passed for all six changed C++ files with no new complexity violations. Actual isolated Xvfb/SDL-dummy checks verified immediate selected-text focus, signed exact edits (-1/8 and 11/4), one undo to 1/3, invalid fraction retention/correction, Escape, ordinary/native blur and unchanged chart payload apart from offset. Final covered-control gate, typing/Escape and clean title-bar shutdown passed; literal Compose smoke passed and its PNG was inspected (`/tmp/wormedit-t297/final-smoke.png`).

## T-298 Lock audio offset changes beside its tools

- **Status:** done
- **Outcome:** A visible padlock beside the offset tools prevents offset changes from numeric edits, nudges and drag gestures while locked.
- **Read first:** `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_toolbar.cpp`.
- **Files owned:** `src/ui/nuklear/metadata/metadata_panel.cpp` (accurate offset-lock explanation only), `src/sys/app/main.cpp` (offset-only lock/capture boundaries), `src/sys/application_frame_projection.cpp` (offset control projection only), `src/ui/nuklear/compose_input_panel.cpp` (offset capture guard only), `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/input/compose_input.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Add one shared offset-lock state and display its padlock near the offset tools. 2. Enforce it at offset action and gesture boundaries, cancelling any pending offset preview when locking. 3. Verify every editing entry point rejects without mutation.
- **Invariants:** Locking offset does not prevent navigation, BPM editing or playback; unlocking does not replay a rejected edit.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.
- **Evidence:** Authoritative application build passed and full suite passed 1180/1180 (63.24 s). Configured clang-tidy passed for all nine changed C++ files with no source-owned complexity violations. Real isolated Xvfb/SDL-dummy checks verified the padlock, rejected numeric/nudge/calibrated waveform and offset-fit changes, retained exact saved 1/3, allowed BPM fitting and playback while locked, and restored fresh exact 1/4 editing without replay on unlock. Runtime regression covers locking before a simultaneous captured release, offset preview cancellation and BPM preview retention. Locking cancels an open inline edit; Metadata explains the independent offset lock correctly. Chart payload comparison, clean shutdown and literal Compose smoke passed; PNGs were inspected (`/tmp/wormedit-t298/final-smoke.png`).

## T-299 Use the upper progress bar only when no BGM exists

- **Status:** done
- **Outcome:** The progress bar above the toolbar replaces the lower minimap only for charts without BGM; charts with BGM retain the lower minimap as the authoritative overview.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/compose/views/compose_view.cpp`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:** 1. Project actual BGM presence independently of viewport visibility. 2. Select exactly one overview location with stable layout. 3. Verify empty/populated/offscreen BGM charts, navigation and audio-tail seeking in isolated captures.
- **Invariants:** An offscreen or hidden BGM event still requires the minimap; existing exact seek behavior is preserved.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

- **Result:** Full cached BGM presence chooses a stable upper bar without BGM or a lower minimap with BGM; missing audio displays a disabled lower diagnostic. Isolated Xvfb/SDL-dummy captures verified empty, populated, offscreen and horizontally hidden BGM, playback/navigation and 47/49-second audio-tail seeks. Configured tidy on all three changed C++ files passed without owned warnings; coordinated app build passed, focused tests 15/15 and full suite 1188/1188 passed (55.79 s). The separate 35-percent anchor scene-validation gap is tracked by T-306.

## T-300 Add Tab-modified measure navigation and double-speed wheel scrolling

- **Status:** done (real measure navigation, fractional wheel modifier and inclusive Shift selection verified)
- **Outcome:** Tab plus arrows or Page Up/Down moves the cursor one measure and repeats every 200 ms; Tab plus wheel doubles ordinary scrolling. Tab alone does not navigate.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/sys/compose/input/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_state.h`.
- **Steps:** 1. Add Tab-modified one-measure keyboard navigation and apply the user-confirmed double-speed wheel modifier once per event. 2. Use monotonic 200 ms keyboard repeat timing independent of OS key-repeat rate. 3. Verify deterministic held/released/focus-loss traces, both modifier press orders, fractional wheel deltas and Shift range navigation.
- **Invariants:** Text widgets retain Tab ownership; no queued repeat survives focus loss or a chart change.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

- **Result:** Tab plus all four arrows or Page Up/Down emits one-measure steps with independent monotonic 200 ms repeats; left/right use chart order and vertical keys respect downscroll. Tab alone does not navigate. Tab-wheel snapshots double ordinary fractional scrolling once; beat-fit and modifier zoom retain their existing behavior. Isolated real SDL/Xvfb gates measured 48 px ordinary versus 96 px with Tab in both directions, held cursor 7→9 (upscroll) and 5→3 (downscroll) over 440 ms, stable after release, and exact inclusive nine-note Shift ranges. Configured tidy introduces no complexity violations; coordinated final build and full suite 1288/1288 passed (85.12 s).

## T-301 Fix slow macOS trackpad navigation

- **Status:** done (isolated SDL/Xvfb verification; physical macOS confirmation pending)
- **Outcome:** Trackpad navigation retains precise fractional wheel units without integer-boundary speed jumps or artificial low-frequency gating; screen-space movement respects macOS natural scrolling consistently in upscroll and downscroll, without double direction inversion. Per-event chart reconstruction remains in the paused performance backlog.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/app/frame_timing.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`.
- **Steps:** 1. Trace wheel precision, SDL flipped-direction normalization, upscroll/downscroll screen movement, event delivery and navigation work before changing behavior. 2. Correct the measured wheel-scaling discontinuity using shared navigation paths. 3. Verify fractional/rapid synthetic event traces and report isolated measurements; actual macOS confirmation remains required evidence when a Mac is available.
- **Invariants:** No guessed speed multiplier; normalize device direction once, preserve intuitive screen-space movement, cursor bounds and input ownership.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

- **Result:** SDL wheel deltas now retain fractional wheel units through one shared normalizer instead of switching integer events to 48 px and fractional events to raw pixels. Actual isolated SDL input at magnitudes 0.99/1/1.01 moved common projected notes 47.52000046/48/48.47999954 px in upscroll and downscroll; normal/flipped reversal was verified. No event coalescing or per-event chart-reconstruction performance gain is claimed. Configured tidy and final full suite 1288/1288 passed (85.12 s). Physical macOS trackpad confirmation remains pending because no Mac was available.

## T-302 Support macOS trackpad pinch zoom

- **Status:** done (isolated SDL/Xvfb verification; physical macOS confirmation pending)
- **Outcome:** SDL pinch gestures zoom the chart around the gesture focus using the same bounded zoom transform as existing controls.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/compose_scroll_geometry.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/compose_scroll_geometry.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_input_panel.h`.
- **Steps:** 1. Handle available SDL pinch begin/update/end events with version-compatible builds. 2. Reuse existing zoom bounds and coordinate anchoring, with focus/gesture cancellation. 3. Test scale accumulation, reversal, invalid scales and input occlusion; document actual macOS confirmation separately.
- **Invariants:** Pinch does not edit notes or offset; missing pinch support in older SDL keeps existing zoom controls.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

- **Result:** SDL >=3.4 pinch BEGIN/UPDATE/END queues bounded positive incremental ratios around a begin-latched pointer anchor; older-header builds retain existing zoom controls behind the version guard. Shared cancellation retires gestures on ownership, chart/revision, focus or capacity changes. Zoom uses the existing bounded transform and cancels prior viewport motion. Isolated actual SDL gates in both directions measured 192→232.32001007→192.00001153 px/measure and verified emitted anchors match the invariant source point (226.31451612903226 upscroll, 305.68548387096774 downscroll). Production-linked tests verify anchor preservation, momentum/autoscroll cancellation and invalid-input rejection. Configured tidy and final full suite 1288/1288 passed (85.12 s). Physical macOS pinch confirmation remains pending; no older-SDL compilation was performed.

## T-303 Prioritize the Compose note canvas without a focus click

- **Status:** done (shared canvas ownership and FIFO key edges; real isolated first-key, wheel, pan and paint checks passed; IME-only ownership retained)
- **Outcome:** while Compose is visible, the note-editing canvas accepts the first wheel/scroll, pan, object-insertion and edit gesture without requiring previous panel focus; keyboard navigation and insertion target the canvas when no actual text/IME/dialog owner is active.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/sys/compose/input/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_options_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Reproduce first-gesture loss after focusing toolbar/inspector/another panel. 2. Share one input-ownership decision that prioritizes the note canvas independently of stale toolkit panel focus. 3. Verify first wheel, pan and object insertion, keyboard navigation, matching releases and text/modal occlusion.
- **Invariants:** the note canvas has highest editing priority; actual text/IME entry and occluding dialogs retain their input. The first gesture is neither dropped nor applied twice, and hidden Compose does not consume other tabs' events.
- **Reject/failure behavior:** blocked or invalid gestures leave the document and selection unchanged; focus/capture loss retires held inputs.
- **Verify:** standard build/test; configured clang-tidy; isolated real-window checks after focusing inspector and toolbar, plus text/Options/history occlusion regressions.
- **Out of scope:** changing note-editing command semantics.

## T-306 Honor the configured playhead anchor in scene validation

- **Status:** done
- **Outcome:** A valid configured playhead anchor such as 35 percent renders a valid Compose scene during playback, Stop and audio-tail seeking instead of reporting viewport scroll outside chart content.
- **Read first:** `src/core/editor_scene.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose/compose_playhead_cursor.h`, `src/sys/compose/views/compose_view.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `src/sys/compose/compose_playhead_cursor.h`, `src/sys/compose_view.cpp/.h`, `tests/core/interaction_tests.cpp`, `tests/core/editor_scene_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Reproduce configured-anchor following against fresh and cached scene validation, including a short chart with a longer audio tail. 2. Share the validated viewport margin contract between following and scene consumers, retaining default 20 percent behavior and rejecting invalid parameters. 3. Verify exact cursor/scroll and fresh-versus-cached primitives during Play, Stop, seeking and resize in upscroll/downscroll.
- **Invariants:** Existing chart/document data and selection stay unchanged; scene validation remains bounded and rejects invalid scroll; default callers remain compatible.
- **Reject/failure behavior:** Invalid parameters reject without mutation; cached and fresh scene paths agree on valid bounds.
- **Verify:** standard build/test; configured clang-tidy; isolated Xvfb/SDL-dummy short-chart audio-tail playback and Stop checks with 35 percent anchor.
- **Evidence:** Authoritative application build passed; full suite passed 1207/1207 (54.63 s), and focused checkpoint passed 49/49, including same-window margin changes. Configured clang-tidy found no new complexity violations. Fresh and cached scenes agree at default 20 percent, configured 35 percent and 10 percent end margins in both directions; invalid margins and out-of-bounds scroll reject. Real isolated Xvfb/SDL-dummy checks at 35 percent verified Play, short-chart audio-tail seeking, Stop and resize in upscroll/downscroll without chart mutation. Literal Compose smoke passed and its PNG was inspected. Reproduction, logs, scripts and inspected PNGs: `/tmp/wormedit-t306/`; final smoke: `/tmp/wormedit-t306/final-smoke.png`.
- **Out of scope:** T-272 measure-index borrowing, overview placement, unrelated rendering optimizations.

## T-485 Keep the Compose field alive when the selection holds deleted objects

- **Status:** done (2026-10-08)
- **Found by:** the 2026-10-08 real-window sweep. Removing a measure deleted its notes but left their IDs selected, and the next field-scene build rejected the view ("field-scene index, view, or geometry is invalid", "Chart field is unavailable") until Undo. The T-71 path now clears its selection, but any other command that deletes selected objects can hit the same failure.
- **Outcome:** after any document change, selection state only refers to objects that still exist, and the field-scene build never fails because of a stale selected ID.
- **Read first:** the field-scene check `view.selected_notes.size() <= index.notes.size()` and its neighbours (grep "field-scene index, view, or geometry is invalid"), `include/wormedit/core/interaction.h` (`editor_interaction_state_t` selection fields), `src/sys/compose/views/compose_view.cpp`.
- **Files owned:** `src/core/interaction.cpp`, `include/wormedit/core/interaction.h`, `tests/core/interaction_tests.cpp`, the file holding the field-scene check and its test, `src/sys/compose/views/compose_view.cpp`, `tests/sys/compose_measure_commands_tests.cpp`, this card.
- **Steps:**
  1. Write a failing test: select notes, delete them through a path other than measure removal (an undo of their creation, or a core edit), and build the field scene.
  2. Prune selected notes, objects and retained ranges against the current document once per document revision change, in one place, and drop IDs that no longer exist.
  3. Make the field-scene build skip unknown selected IDs instead of rejecting the whole view, so a missed prune degrades gracefully.
  4. Remove the special-case clear in the T-71 path if the general prune covers it, keeping its test.
- **Invariants:** selection of surviving objects is preserved across edits and Undo/Redo. Pruning is O(selection), not a whole-chart scan per frame.
- **Reject/failure behavior:** none; pruning never mutates the document.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`; full suite.
- **Out of scope:** selection persistence across sessions.
- **Result (2026-10-08):** `prune_stale_selection(chart, revision, state)` (`interaction.cpp`, `selection_checked_revision` in the state) drops selected notes/objects of every family and a retained span whose measures are gone; it runs once per revision change from `inspect_compose_interaction` (the one place), scanning only the families actually selected, and is a no-op on steady frames. The field-scene validity check no longer compares the selection size with the note count, so an unknown selected ID is ignored. Step 4 deliberately keeps the T-71 clear: it also resets canvas-measure selection and canvas spans, which the general prune does not cover. Tests: scene test (failed before), prune test in `interaction_tests.cpp`. Full suite 2874/2874, event-harness 255/255.

## T-745 Specify one authored multivoice sound model

- **Status:** deferred (was: ready; owner requested a separate later design pass, 2026-10-20)
- **Outcome:** a reviewed contract specifies one persistent authored voice owner shared by native, osu! and BMSON workflows, with exact integration ownership and no unresolved implementation defaults.
- **Read first:** `docs/chart-editor-plan/editing/metadata-interop.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/document.h`, `src/audio/audition_resolver.cpp`, `src/core/bmson_sound_channel_editor.cpp`.
- **Files owned:** `docs/chart-editor-plan/editing/metadata-interop.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `docs/chart-editor-plan/tasks/compose-ux.md`, `docs/architecture.md`.
- **Steps:**
  1. Trace `note_t::sound`, `resource_entry_t`, native codecs, assignment/usage/deletion, clipboard, audition/preview bindings and retained BMSON fusion; record the existing owners and exact consuming paths in the contract.
  2. Specify bounded persistent voice identity/order, resource/slice/gain ownership and old single-sound compatibility; distinguish authored data from runtime decoded-source bindings and retained format backing.
  3. Specify sound-layer add/remove/select/assignment UX and target compatibility, including osu! inherited custom sets and BMSON continuation/restart; record alternatives under question 25 without selecting an unapproved product default.
  4. STOP before implementation until question 25 is answered; record the approved model and refine T-746–T-748 exact ownership from the trace, splitting independent implementation slices if required.
- **Invariants:** imported BMSON fused layers already work and stay supported; no generated composite workaround, hidden BMSON-as-generic model, duplicate writable graph or whole-custom-set migration.
- **Reject/failure behavior:** unresolved design questions leave implementation blocked and native documents unchanged; this card is code-free.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`; review each named consumer against the recorded owner and question 25.
- **Out of scope:** runtime model/lifecycle implementation (T-746), osu! materialization (T-747), BMSON authoring integration (T-748); current hitsound palette/readability/playback polish remains the T-216/T-217 follow-up.

## T-746 Integrate approved authored voices with Sounds and audio

- **Status:** deferred (was: blocked-on T-745; question 25 must be answered)
- **Outcome:** the approved multivoice value persists and participates in Sounds lifecycle, exact per-voice audition/playback/preview export and explicit target compatibility without losing legacy single-sound behavior.
- **Read first:** `docs/chart-editor-plan/editing/metadata-interop.md`, `docs/architecture.md`, `include/wormedit/core/document.h`, `src/core/resource_assignment.cpp`, `src/audio/audition_resolver.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `src/core/chart_clipboard.cpp`, `src/core/chart_compare.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.h`, `src/formats/native_project_migrations.cpp`, `src/formats/conversion.cpp`, `src/audio/audition_resolver.cpp`, `include/wormedit/audio/audition_resolver.h`, `src/core/timing.cpp`, `src/sys/resources/sound_usage_view.cpp`, `src/sys/resources/sound_usage_actions.cpp`, `src/sys/compose/actions/compose_audition_requests.cpp`, `src/sys/audio/project_audio_sources.cpp`, `src/sys/audio/preview_audio_export.cpp`, `tests/core/resource_assignment_tests.cpp`, `tests/core/chart_clipboard_tests.cpp`, `tests/core/chart_compare_tests.cpp`, `tests/core/audio_preview_export_tests.cpp`, `tests/formats/native_project_tests.cpp`, `tests/formats/conversion_loss_matrix_tests.cpp`, `tests/sys/sound_usage_projection_tests.cpp`, `tests/sys/sound_usage_actions_tests.cpp`, this card; T-745 must approve exact additional model/UI/codec consumers before implementation.
- **Steps:**
  1. STOP until T-745's model is approved and question 25 is answered; use its traced ownership to split this integration into smaller ordered cards if it exceeds one reviewable change.
  2. Define and validate the approved bounded voice values on their single owner, preserving old single-sound reads and using a reviewed native migration where needed.
  3. Route assignment, usage/deletion, clipboard and comparison through that owner; expose layer editing through the T-745-approved Sounds workflow without per-format parallel state.
  4. Resolve per-voice resource/slice/gain through existing audio services once for click audition, live playback and preview.ogg; give unsupported target conversion a typed loss diagnostic/refusal until its codec is implemented.
  5. Test old/native multivoice reopen, one-step Undo/Redo, resource removal/usage, copy/paste, exact layer audio/count/gain and unsupported-output gates; reject invalid or over-cap bindings atomically.
- **Invariants:** one logical note retains its identity; each voice's gain applies once; ordinary resources/decoded sources are shared; no flattened generated file, hidden retained-source authoring or silent layer loss.
- **Reject/failure behavior:** invalid bindings, missing assets, capacity or unsupported output leave the authored document and destination files unchanged through existing transaction/publication owners.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -R 'native_project|resource_assignment|sound_usage|chart_clipboard|chart_compare|preview.export|conversion'`; inspect actual mixed PCM for two distinct authored voices.
- **Out of scope:** osu! custom-set migration (T-747), BMSON retained-channel authoring/cutover (T-748); new export representations require exact ownership approved by T-745 rather than implicit lossy flattening.

## T-747 Materialize only edited osu custom-set notes into Sounds

- **Status:** deferred (was: blocked-on T-746; question 25 must be answered)
- **Outcome:** an edit to an imported custom-set note creates/reuses ordinary Sounds resources and approved authored voices only for affected notes, while untouched notes retain faithful osu! resolution.
- **Read first:** `docs/chart-editor-plan/editing/metadata-interop.md`, `src/core/hitsound_editor.cpp`, `src/audio/hitsound_resolver.cpp`, `src/formats/osu_mania.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `include/wormedit/core/hitsound_editor.h`, `src/core/hitsound_editor.cpp`, `include/wormedit/audio/hitsound_resolver.h`, `src/audio/hitsound_resolver.cpp`, `include/wormedit/formats/osu_mania.h`, `src/formats/osu_mania.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/actions/compose_actions.h`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend/hitsounds.h`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/core/hitsound_editor_tests.cpp`, `tests/core/audio_hitsound_resolver_tests.cpp`, `tests/formats/osu_mania_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/hitsound_palette_event_harness_tests.cpp`, `docs/manual/chapters/sounds-and-layers.adoc`, this card.
- **Steps:**
  1. STOP until T-746 supplies the approved voice owner and question 25 is answered; trace every hitsound edit entry, including empty-selection placement defaults, before adding the conversion boundary.
  2. Resolve affected notes' effective normal/addition sets, custom index, volume and actual source/slice identities using the existing timing-context/path owners; preserve the normal and each enabled addition separately.
  3. Stage ordinary Sounds resource reuse/creation, affected-note voice binding and requested edit in the same transaction; retain unchanged source semantics on all unedited notes and retire only the converted note's competing writable osu! fields under the approved model.
  4. Show imported custom-set references as read-only until an edit is accepted; expose converted resources through ordinary Sounds and document the boundary without suggesting the shared imported set was changed.
  5. Test two notes sharing one set with only one edited, inherited timing context, explicit custom filename, additions, volume applied once, no-op/selection/playback non-conversion, save/reopen, Undo/Redo and loss-aware osu! export.
- **Invariants:** only affected notes migrate; no shared set/timing rewrite, silent default substitution, addition loss, duplicate gain or generated composite sample; native-created hitsound notes still work.
- **Reject/failure behavior:** unavailable required sources, stale input, resource limits or unrepresentable edits reject the complete transaction without partial resources or converted notes.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -R 'hitsound|osu.mania|application.commands'`; isolated Xvfb check that editing one shared-set note exposes its Sounds while the other remains imported.
- **Out of scope:** general voice model/resource lifecycle (T-746), BMSON layer editing (T-748), editing global osu! custom sample-set definitions (not authorized by the affected-note-only request).

## T-748 Integrate BMSON layered authoring with the shared voice model

- **Status:** deferred (was: blocked-on T-746; question 25 must be answered)
- **Outcome:** BMSON layered sounds can be authored through the approved shared model while preserving existing retained-channel fusion, exact slice audio and faithful BMSON output.
- **Read first:** `docs/chart-editor-plan/editing/metadata-interop.md`, `src/core/bmson_sound_channel_editor.cpp`, `src/core/sound_slice.cpp`, `src/audio/audition_resolver.cpp`, `src/formats/bmson_writer.cpp`.
- **Files owned:** `include/wormedit/core/bmson_sound_channel_editor.h`, `src/core/bmson_sound_channel_editor.cpp`, `src/core/bmson_sound_channel_editor_internal.h`, `src/core/sound_slice.cpp`, `src/core/note_move.cpp`, `src/core/note_to_bgm.cpp`, `src/formats/bmson.cpp`, `src/formats/bmson_writer.cpp`, `src/audio/audition_resolver.cpp`, `src/sys/compose/actions/compose_resource_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/sys/audio/bmson_waveform_view.cpp`, `tests/core/bmson_sound_channel_editor_tests.cpp`, `tests/core/bmson_slice_reset_tests.cpp`, `tests/core/sound_slice_rules_tests.cpp`, `tests/formats/bmson_tests.cpp`, `tests/formats/bmson_writer_tests.cpp`, `tests/ui/bmson_lane_mode_event_harness_tests.cpp`, `docs/manual/chapters/sounds-and-layers.adoc`, this card; T-745 supplies exact approved frontend/command ownership for new layer controls before editing.
- **Steps:**
  1. STOP until T-746 and question 25 settle authoring/retained ownership; capture existing retained BMSON coincident-layer audition and slice behavior as the compatibility baseline, not as missing functionality.
  2. Adapt imported layer/channel/event attribution into the approved authoring view without making a second writable mirror or encoding generic native voices as hidden BMSON source objects.
  3. Add the approved layer assignment/add/remove workflow using ordinary Sounds and shared commands; preserve exact source spans, continuation/restart and logical note identity through move, copy, deletion and Undo.
  4. Serialize supported authored layers to BMSON channels/events and reimport them; diagnose unsupported mappings rather than omitting layers or baking composite audio.
  5. Compare imported-unedited and authored-layer round trips, coincident voices, continuation/restart slices, cross-chart clipboard and mixed native/BMSON projects against the same audition/playback/preview-export owner.
- **Invariants:** existing retained BMSON fusion remains supported; unedited channel/event provenance stays attributable; audio and export do not duplicate voices, change slice bounds or apply gain twice.
- **Reject/failure behavior:** ambiguous attribution, collisions, unsupported mappings and capped edits leave both authored bindings and retained channel backing unchanged atomically.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -R 'bmson|sound.slice|audition|preview.export'`; isolated Xvfb layer-edit/Undo workflow with actual decoded slice PCM comparison.
- **Out of scope:** replacing BMSON hybrid-lane/stem geometry or slicing policy (existing T-457–T-464 and slice rules govern), osu! materialization (T-747), automatic migration of every retained channel or generated composite flattening (not authorized).


