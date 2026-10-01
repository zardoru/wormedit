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
  (`src/sys/waveform_analysis_runtime.cpp`) and has no sample-level path, so
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
- **Read first:** `include/wormedit/core/interaction.h` (`retained_selection_range`, `retained_canvas_selection_range`), `src/sys/compose_view.cpp` (the block that projects `retained_selection_range`), `src/ui/nuklear/compose_canvas.cpp`, `docs/chart-editor-plan/editing/interactions.md` (area selection).
- **Files owned:** `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, new `src/ui/nuklear/compose_area_menu.cpp/.h`, `src/sys/compose_actions.cpp`, `CMakeLists.txt`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, new `src/sys/compose_area_actions.cpp/.h`, `tests/CMakeLists.txt`, `src/ui/nuklear/frontend.cpp` (shared action/test and Escape-routing scope extended by user approval on 2026-09-30).
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

- **Status:** blocked-on T-200
- **Outcome:** each recipe draws as a bar spanning its exact interval in the Compose SV lane (with its curve drawn inside); clicking it selects it and opens an "SV recipe" section at the top of the inspector with the same fields as the Timing pane recipe form; its ends and body drag with the existing snapped range-drag controller; "Add SV recipe" from the area menu creates one over the band.
- **Read first:** `include/wormedit/core/sv_recipe.h` (`begin_sv_recipe_draft`, `begin_sv_recipe_range_drag`, `apply_sv_recipe_draft`), `src/ui/nuklear/timing_panel.cpp` (recipe form, `cancel_timing_recipe_range_drag`), `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_timing_lanes`), `src/sys/compose_view.cpp`.
- **Files owned:** `src/sys/compose_view.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, new `src/ui/nuklear/compose_recipe_inspector.cpp/.h`, `CMakeLists.txt`.
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

- **Status:** blocked-on T-200
- **Outcome:** each SPEEDS transition draws in the SPD lane as a span from its row over its transition length (beats or seconds, using the existing evaluator spans); "Add SPEEDS span" from the area menu creates a speed event at the band start whose duration equals the band length in beats; dragging the span's end edits the duration.
- **Read first:** `src/sys/timing_view.cpp` (`append_speed_spans_to_cache`), `include/wormedit/core/scroll_speed_editor.h` (`apply_scroll_speed_edit`), `include/wormedit/core/timing_lane_controller.h`.
- **Files owned:** `src/sys/compose_view.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/scroll_speed_editor_tests.cpp`.
- **Steps:** 1. Reuse the span computation from `timing_view.cpp` (move it to a shared helper if needed). 2. Draw spans. 3. Create-from-band command. 4. End-handle drag → duration update (one command).
- **Invariants:** seconds-mode spans keep their seconds duration when timing changes.
- **Reject/failure behavior:** a band crossing a warp in seconds mode shows the evaluator's diagnostic.
- **Verify:** standard build/test.
- **Out of scope:** SCROLLS recipes (T-201 covers them).

## T-203 Retire the Timing tab once Compose has parity

- **Status:** blocked-on T-201, T-202, T-51
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
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_note_glyph`, `compose_note_style`), `src/sys/compose_view.cpp` (glyph projection), `docs/chart-editor-plan/editing/layout.md` (snap colors), `docs/chart-editor-plan/decisions/open-questions.md` (question 14).
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, new `src/ui/nuklear/arrow_glyph.cpp/.h`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, new `tests/ui/arrow_glyph_tests.cpp`, `tests/CMakeLists.txt`.
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
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/core/interaction_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/application_commands.h`, `src/sys/application_commands.cpp`, `src/sys/application_command_dispatch.cpp`, `src/sys/main.cpp` (enabled/checked state), `tests/sys/application_commands_tests.cpp`, this card (scope extended by user approval 2026-09-30).
- **Steps:** 1. Armed kind in interaction state (default tap). 2. Per-profile allowed set (BMS: tap, hold, invisible, mine; SM: tap, hold, roll, mine, lift, fake; osu!/Quaver: tap, hold, mine). 3. Registry commands `edit.note_kind_<kind>`. 4. Toolbar buttons with icons. User decision 2026-09-30: clicking Hold/Roll begins the existing hold gesture; painting with Hold/Roll asks the user to use that gesture instead of inventing a duration.
- **Invariants:** hold entry gestures still create holds regardless of the armed tap-like kind (roll when T-170 lands).
- **Reject/failure behavior:** switching to a chart whose profile lacks the armed kind resets to tap.
- **Verify:** parent `cmake --build build` passed; full suite 863/863 passed. Clang-tidy found no new project complexity violations. Isolated Xvfb captures `/tmp/wormedit-next/mine-click.png` and `hold-click.png` inspected: Alt+3 creates a mine and Alt+2 completes a two-click hold. Logs and independent staged-cut syntax checks: `/tmp/wormedit-next/`, `/tmp/wormedit-t205/`.
- **Out of scope:** converting existing notes' kind (add later as a selection command).

## T-206 Redesign the inspector's resource list

- **Status:** ready
- **Outcome:** the Samples/BGM/BGA list becomes a compact table with columns ID (BMS index or slot), Name, Uses, State (ok/missing/unused), plus a per-row audition button; single click selects, double click arms, the filter field from T-103 sits in the header, and the list keeps a fixed height with its own scroll instead of stretching the inspector.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose_resource_presenter.cpp`, `docs/chart-editor-plan/document-model.md` (resource list paragraph).
- **Files owned:** `src/ui/nuklear/compose_resources.cpp/.h`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/frontend.h`, `tests/core/compose_resource_presenter_tests.cpp`.
- **Steps:** 1. Project uses count and state per row (shared with T-101). 2. Table layout with `nk_layout_row` fixed column ratios and a `nk_group` scroll area of about 12 rows. 3. Audition button calls the existing audition request path.
- **Invariants:** armed-resource behavior unchanged.
- **Reject/failure behavior:** missing rows show the missing state and cannot be armed.
- **Verify:** standard build/test; smoke PNG with a BMS fixture that has many `#WAV` entries.
- **Out of scope:** drag-and-drop from the list (later card). This table is the non-slot view that T-229/T-230 ([panels-menus](panels-menus.md)) move into the Sounds and Images dock panels; if T-228 has landed, build it there directly.

## T-207 Fix semantic-layer usability

- **Status:** done
- **Outcome:** after "Add semantic layer" the new layer is the target and the status says "Semantic layer added"; with a target and a note selection, "Add selected notes to layer" is enabled and labeled that way; clicking a layer while notes are selected offers one-click assignment; the section opens by default and sits above the resource drawer.
- **Read first:** `src/sys/compose_actions.cpp` (`add_semantic_layer`, `semantic_layer_edit_status`, `dispatch_compose_view_action`), `src/ui/nuklear/compose_inspector.cpp` (`render_semantic_layer_page_controls`), the status-message path that writes "Compose navigation canceled.".
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/main.cpp` (status precedence only), `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_panel.cpp` (section order only), `tests/sys/application_commands_tests.cpp`, `src/sys/application_commands.h`, `src/sys/application_commands.cpp` (shared status precedence helper).
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
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/sys/workspace_frame_view.cpp`, `src/ui/frontend.h`, `src/sys/workspace_projection.cpp` (recovery availability and bounded projection only).
- **Steps:** 1. Expose "has actionable candidate" in the view. 2. Skip the panel when false.
- **Invariants:** actionable candidates always show.
- **Reject/failure behavior:** none.
- **Verify:** smoke PNG of `compose` shows the toolbar at the top of the content area.
- **Out of scope:** recovery logic.

## T-209 Show display lane numbers in note tooltips

- **Status:** done
- **Outcome:** tooltips say "lane 2" (1-based display order within the chart layout) instead of the internal lane ID.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (tooltip helpers), `src/sys/compose_view.cpp` (hover projection).
- **Files owned:** `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`.
- **Steps:** project the display index next to the lane ID and print it.
- **Invariants:** hit-testing unchanged.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test.
- **Out of scope:** lane names.

## T-210 Add waveform detail at high zoom

- **Status:** done
- **Outcome:** zooming Compose past the base level shows finer detail: a 32-frames-per-bin level is generated on demand for the visible range, and below 1 frame per pixel the waveform draws PCM samples from the decoded block cache.
- **Read first:** `include/wormedit/audio/waveform.h` (`waveform_level_for_zoom`, `project_waveform`), `src/sys/waveform_analysis_runtime.cpp` (fixed `256`), `include/wormedit/audio/block_cache.h`, `src/sys/waveform_view_projection.cpp`.
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
- **Read first:** `src/sys/main.cpp` (main loop), `src/sys/sdl_preview_output.cpp` (`service`, `session_.pump()`), `src/sys/compose_view.cpp` (scene cache rebuild), `src/sys/window.cpp` (VSync).
- **Files owned:** new `src/sys/frame_timing.cpp/.h`, `src/sys/main.cpp` (timer calls only), `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, new `tests/sys/frame_timing_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/sys/application_frame_projection.cpp` (timer calls only), `src/sys/compose_view.cpp` (timer calls only).
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
- **Read first:** `src/sys/sdl_preview_output.cpp`, `include/wormedit/audio/preview_session.h` (control-thread contract), `docs/async-task-scheduling.md`.
- **Files owned:** `src/sys/sdl_preview_output.cpp/.h`, `src/audio/preview_session.cpp`, `tests/sys/sdl_preview_output_tests.cpp`.
- **Steps:** 1. Dedicated refill thread with the existing generation guards. 2. Hard Stop still silent on the next render (existing test must pass).
- **Invariants:** `render()` stays allocation-free; Stop semantics unchanged.
- **Reject/failure behavior:** refill failure reports through the existing status path.
- **Verify:** application and SDL test targets built successfully; final full suite passed 919/919 (32.06 s). The continuous-music seek stress passed 30/30 repetitions; its initial one-shot fixture legitimately completed after seeks beyond the sole note and was corrected to keep audio across all seek positions. Frozen staged syntax checks passed 2/2. Required `ctest --test-dir build --output-on-failure -R '^audio\.|sdl_preview'` passed 101/101; the actual SDL namespace `-R '^sdl-audio\.'` passed 9/9, including playback beyond prime windows without frame service, silent next render after hard Stop, repeated seek/Stop/reopen, and an unread refill failure delivered after session completion. New C++ functions pass the complexity gate; the original first SDL test's existing complexity violation is unchanged. Same isolated Xvfb/SDL-dummy T-211 fixture, final 240-frame playing blocks: main-thread pump average/p99 0.141201/1.312390 ms before, result delivery 0.000775/0.001420 ms after; total main audio average/p99 0.163309/1.338880 → 0.022827/0.040530 ms. AFTER measures only main-thread worker-result delivery, not worker decode cost or total decode speed. Dense Compose PNG inspected. Raw tables, report, PNG and summary: `/tmp/wormedit-t212/`; test logs: `/tmp/wormedit-t212-worker-{build,tests,sdl-tests}.log`.
- **Out of scope:** audition output.

## T-213 Reuse the Compose scene while only the scroll changes

- **Status:** done
- **Outcome:** after T-243 removes the larger workspace cost, Compose scene (4.385 ms average in T-211, 63 % of a 144 Hz frame) is the next CPU phase to cut: during playhead following, a scroll change reuses the cached scene for a margin of rows above/below and translates it, rebuilding only when the viewport leaves the cached window or the revision changes.
- **Read first:** `src/sys/compose_view.cpp` (`compose_scene_cache_t`, rebuild conditions), `src/core/editor_scene.cpp`.
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
- **Read first:** `src/formats/bms_writer.cpp` / `include/wormedit/formats/bms_writer.h` (active-projection export), `src/sys/main.cpp` (preferences path from the shortcut profile), `src/core/command_registry.cpp`.
- **Files owned:** new `src/sys/external_player.cpp/.h`, new `tests/sys/external_player_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `src/ui/frontend.h`, new `src/ui/nuklear/external_player_panel.cpp/.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/sys/application_commands.h/.cpp`, `src/sys/compose_view.h`, `src/ui/nuklear/frontend.cpp` (scope extension approved by user, 2026-09-30).
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

- **Status:** ready
- **Outcome:** notes carry an optional typed `osu_hitsample_t` (additions whistle/finish/clap, normal set, addition set, custom index, volume, filename); the osu!mania codec reads and writes it instead of relying on provenance text; Quaver `HitSound` flags map to the same type later.
- **Read first:** `src/formats/osu_mania.cpp` (`build_osu_note_sample`, `osu_note_sample_t`), `include/wormedit/core/document.h` (`note_t`), `docs/references/quaver-qua-format.md` (HitSound flags).
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/osu_mania.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/osu_mania_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. Type + validation. 2. Reader fills it; writer prefers it, falling back to provenance only for untouched notes (byte-stable). 3. Native persistence with migration (absent = none). 4. Round-trip test with every flag and set.
- **Invariants:** unedited osu! files re-export byte-identically.
- **Reject/failure behavior:** invalid set/index values reject on import with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'osu|native_project'`.
- **Out of scope:** UI (T-216), playback (T-217).

## T-216 Add an osu!-style hitsound panel with shortcuts

- **Status:** blocked-on T-215
- **Outcome:** a Compose toolbar group shows Whistle/Finish/Clap toggles and Normal-set/Addition-set pickers (Auto/Normal/Soft/Drum) reflecting the selection (mixed state shown as a dash); clicking or pressing W/E/R toggles the addition on every selected note, Shift+Q/W/E/R sets the normal set, Alt+Q/W/E/R sets the addition set (osu! editor convention, remappable); each change is one command. On osu!/Quaver charts the same controls apply to notes placed next.
- **Read first:** `include/wormedit/core/document.h` (`osu_hitsample_t` from T-215), `src/core/command_registry.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `docs/chart-editor-plan/editing/interactions.md` (hitsound painting paragraph).
- **Files owned:** new `include/wormedit/core/hitsound_editor.h`, new `src/core/hitsound_editor.cpp`, new `tests/core/hitsound_editor_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Core batch command over a bounded note selection. 2. Registry commands with the default chords; the chords only apply in Compose focus so they do not clash with lane keys (check `diagnose_shortcut_conflicts`). 3. Toolbar group shown for osu!/Quaver charts, and for any chart when the user enables it.
- **Invariants:** BMS keysound assignment (T-54) stays separate: this edits osu! samples, not resources.
- **Reject/failure behavior:** stale revision or empty selection rejects.
- **Verify:** standard build/test.
- **Out of scope:** sample playback (T-217).

## T-217 Play osu! hitsounds in preview

- **Status:** blocked-on T-216
- **Outcome:** preview plays normal and addition samples for each note: wormedit ships its own small CC0 default set (normal/soft/drum × hitnormal/whistle/finish/clap), and Options can point to an osu! skin folder whose files override the bundled ones; per-map custom samples always win (user decision 2026-09-30).
- **Read first:** `docs/chart-editor-plan/playback-audio.md`, `include/wormedit/audio/preview_bridge.h`, `src/sys/audio_source_registration.cpp` (internal metronome sources: same pattern for bundled samples), `include/wormedit/core/preferences.h` (T-253).
- **Files owned:** new `assets/hitsounds/` (samples + `LICENSE.md` stating CC0 and provenance), new `src/audio/hitsound_resolver.cpp/.h`, new `tests/core/audio_hitsound_resolver_tests.cpp`, `src/sys/audio_source_registration.cpp`, `src/audio/preview_bridge.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Bundled samples: synthesize them (short procedurally generated clicks/claps written by a checked-in generator script) so they are CC0 by construction; do not copy osu! skin files.
  2. Resolver order: map custom sample → skin folder (`soft-hitwhistle.wav` naming) → bundled default.
  3. Schedule hitsound voices for notes with `osu_hitsample_t` (T-215) through the preview bridge.
  4. Tests for the resolver order and missing-file fallback.
- **Invariants:** BMS keysounds unaffected.
- **Reject/failure behavior:** missing samples fall back silently with one diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual playback of an osu!mania fixture.
- **Out of scope:** skin browsing UI beyond the folder option.

## T-240 Stop high-zoom waveform flicker during playback

- **Status:** done
- **Outcome:** at zoom levels below 256 frames per pixel, the Compose waveform keeps one drawing style and a stable data level while the song plays with the view following the playhead; no frame-to-frame alternation between the PCM polyline, the 32-frame level, and the 256-frame level.
- **Cause (found by code reading, 2026-09-30):**
  1. `service_compose_detail` in `src/sys/waveform_analysis_runtime.cpp` keys the detail analysis on the exact visible window `[begin_frame, end_frame)`. While following the playhead the window moves every frame, so the key changes every frame; the code cancels `detail_job_` and resets `detail_analysis_` each time, and a finished job rarely matches the already-moved key. Detail appears for a frame and disappears again.
  2. `populate_compose_waveform_projection` in `src/sys/waveform_view_projection.cpp` chooses all-or-nothing per frame: the PCM path needs every visible block in the runtime's own `decoded_blocks_` cache (which the cancelled job never fills), and the 32-frame level must cover the whole window; otherwise the entire waveform falls back to the 256-frame level, drawn in a different style (peak/RMS envelope instead of sample polyline). Crossing into a new block flips the whole waveform.
- **Read first:** `src/sys/waveform_analysis_runtime.cpp` (`service_compose_detail`), `src/sys/waveform_view_projection.cpp` (`populate_compose_waveform_projection`), `src/audio/aligned_waveform.cpp` (`aligned_waveform_window`, `project_aligned_waveform_samples`), `include/wormedit/audio/block_cache.h`.
- **Files owned:** `src/sys/waveform_analysis_runtime.cpp/.h`, `src/sys/waveform_view_projection.cpp/.h`, `src/audio/aligned_waveform.cpp`, `include/wormedit/audio/aligned_waveform.h`, `src/sys/application_frame_projection.cpp` (playback direction/rate and cache connection), `src/sys/main.cpp` (preview-cache wiring; scope extensions approved by user), `tests/core/audio_aligned_waveform_tests.cpp`.
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
- **Current behavior (2026-09-30):** `seek_compose_waveform` in `src/sys/compose_input.cpp` (input `WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK`) converts the click fraction to a frame and only scrolls the view to center it. It never touches the transport, and during playback the playhead-following scroll overrides it at once, so a click appears to do nothing.
- **Read first:** `src/ui/nuklear/compose_input_panel.cpp` (`process_compose_overview_input`), `src/sys/compose_input.cpp` (`seek_compose_waveform`), `src/sys/chart_preview_state.cpp` (`chart_preview_t::seek`, `resume_frame`, `paused`), `src/sys/main.cpp` (`state.transport_seek`), `src/sys/transport_player_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/ui/frontend.h`, `src/sys/compose_input.cpp`, `src/sys/chart_preview_state.cpp/.h`, `src/sys/main.cpp` (callback wiring, preference projection/preservation), `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `src/sys/compose_view.h`, `src/ui/nuklear/frontend.cpp`, new `src/sys/compose_minimap_navigation.cpp/.h`, `CMakeLists.txt`, `tests/CMakeLists.txt` (scope extensions approved by user).
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
- **Cause (verified 2026-09-30):** importing `tests/fixtures/stepmania/matched-timing.sm` with the real parser yields `input_profile.profile_id == ""` and **0 bindings**. `src/formats/stepmania.cpp` and `src/formats/osu_mania.cpp` never set `chart.input_profile`; only new charts (`make_number_row_input_profile` in `src/sys/application_defaults.cpp`), BMS (`src/formats/bms.cpp`), and BMSON set one. Compose emits `WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS` only for keys found in `chart.input_profile.bindings` (`src/sys/compose_view.cpp`), so every lane key is dropped. Native projects saved from such imports persist the empty profile, so the fix must also cover loading. The note-kind palette (T-205) uses Alt+digits and does not conflict.
- **Read first:** `include/wormedit/core/lane_profiles.h` (`make_number_row_input_profile`), `src/sys/application_defaults.cpp` (`make_playable_profile`), `src/formats/stepmania.cpp`, `src/formats/osu_mania.cpp`, `src/core/document.cpp` (`validate_input_profile`).
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `tests/core/lane_profile_tests.cpp`, `src/formats/stepmania.cpp`, `src/formats/osu_mania.cpp`, `tests/formats/stepmania_tests.cpp`, `tests/formats/osu_mania_tests.cpp`, `src/sys/project_open.cpp`, `tests/sys/project_open_tests.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp` (hint only).
- **Steps:**
  1. Core helper `default_input_profile_for(const lane_layout_t&) -> std::optional<input_profile_t>` that returns the number-row profile (the same bindings new charts get) for single-stage layouts of 1–10 playable lanes, and the existing split-stage helper for split layouts. Tests for 4K, 7K, 10K, 11K (none).
  2. StepMania and osu!mania readers assign it to each chart whose layout qualifies; the profile id records its origin (for example `number-row-default`). Re-import/export must stay semantically equal (the profile is editor data, not written to `.sm`/`.ssc`/`.osu`).
  3. On opening any project or chart (`src/sys/project_open.cpp`), fill an **empty** profile (both ID and bindings empty; a named unbound profile is intentional) with the default through a revision-free load normalization (not an undoable edit, and not marking the document dirty); never overwrite a non-empty profile.
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
- **Read first:** `src/sys/application_frame_projection.cpp`, `src/sys/timing_view.cpp` (`populate_timing_view`), `src/sys/frame_timing.h/.cpp`, `src/sys/workspace_frame_view.cpp`, `src/sys/workspace_projection.cpp` (`flatten_workspace`).
- **Files owned:** `src/sys/application_frame_projection.cpp/.h`, `src/sys/timing_view.cpp/.h`, `src/sys/frame_timing.h/.cpp`, `src/ui/nuklear/frontend.cpp` (overlay rows only), `src/ui/frontend.h` (timing-overlay contract), `src/sys/main.cpp` (audio, workspace preparation, action and Follow timers; retained Compose geometry wiring), `src/sys/sdl_audition_output.cpp` and `src/sys/sdl_preview_output.cpp` (pump timer scopes only; user-approved extensions, 2026-09-30), `src/audio/aligned_waveform.cpp` (affine timing cursor and coarse-envelope bin reuse; user-approved extension, 2026-09-30), `src/sys/compose_view.cpp` (revision-cached Controls measure geometry; user-approved extension, 2026-09-30), `src/sys/compose_playhead_cursor.h`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp` (revision-tagged retained playhead geometry; user-approved extensions, 2026-09-30), `src/sys/waveform_view_projection.cpp` and `src/sys/compose_view.h` (overview-cache storage and projection), `tests/CMakeLists.txt` (projection-test linkage; user-approved extensions, 2026-09-30), `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_timing_state_tests.cpp`.
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
- **Read first:** `include/wormedit/nuklear_config.h`, `src/ui/nuklear/frontend.cpp` (the custom quad code near `static_cast<nk_draw_index>` and the geometry submission), `src/sys/window.cpp` (renderer).
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

- **Status:** ready
- **Outcome:** during Ctrl+Space playback the status bar keeps a fixed layout: status text, minimap, clock, and artist/difficulty never jump; the minimap stays visible (or shows a placeholder) instead of appearing and disappearing.
- **Cause (code reading, 2026-09-30; user screenshot shows the bar without a minimap while playing):** `render_status_bar` in `src/ui/nuklear/frontend.cpp` adds the minimap column only `if (minimap_available)`. When that flag changes between frames, the whole row is laid out differently, so the status text, clock, and minimap jump each frame. `status_minimap_available` needs `waveform_overview_row_count != 0` **and** `worm_ui_nuklear_compose_view_valid(&view)`, which rejects the entire Compose view if any per-frame value is out of range, including `compose_waveform_overview_valid` (visible fractions must stay in `[0, 1]`). Likely trigger: when the following view extends past the end of the audio, `project_waveform_overview` (`src/audio/aligned_waveform.cpp`) either fails on `frame_at(..., total_frames)` (`invalid_timing`, so zero overview rows) or yields an end fraction above 1; either way the minimap and the row layout toggle. Confirm in step 1 before fixing.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_status_bar`, `status_minimap_available`, `workspace_status_line`), `src/ui/nuklear/compose_panel.cpp` (`worm_ui_nuklear_compose_view_valid`, `compose_waveform_overview_valid`), `src/audio/aligned_waveform.cpp` (`project_waveform_overview`), `src/sys/waveform_view_projection.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/audio/aligned_waveform.cpp`, `src/sys/waveform_view_projection.cpp`, `tests/core/audio_aligned_waveform_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:**
  1. Confirm: temporarily count, per frame, which predicate makes `status_minimap_available` false (overview rows empty, overview projection status, or which clause of the view validation) while playing past the end of the audio and at the start; record the result in the report.
  2. Stable layout: always reserve the minimap column while Compose is visible; when data is missing, draw an empty overview background. Give the status text a fixed share of the row and elide long text with "…" instead of letting it resize neighbors.
  3. Overview projection: clamp the visible range to the audio (end fraction ≤ 1, begin ≥ 0) instead of failing when the viewport extends past the last audio frame; keep beat-zero outside `[0, 1]` as "not visible" rather than invalid.
  4. Keep the last successful overview rows for the same waveform key and document revision so a transient failure does not blank the minimap.
  5. Validation scope: make overview problems invalidate only the minimap, not the whole Compose view; in debug builds, log the first failing clause of `worm_ui_nuklear_compose_view_valid` once per distinct cause.
  6. Tests: a viewport extending past the audio end returns a projected overview with end fraction 1.0; the status-bar template has the same column count with and without overview data (state test).
- **Invariants:** minimap click/drag behavior (T-15, T-241) unchanged.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|^frontend-state\.'`; manual: play to the end of the song with Ctrl+Space and watch the status bar.
- **Out of scope:** the notification system (T-218–T-224), which later replaces the single status line.

## T-269 Give every lane a key color class in its profile

- **Status:** ready
- **Outcome:** each lane definition carries a `lane_key_color_t` (white, black, scratch, foot_pedal, free_zone, pms_white, pms_yellow, pms_green, pms_blue, pms_red, generic) set by the built-in profiles, and Compose views expose it per lane and per note, so colors follow the instrument instead of display-order parity.
- **Current behavior (2026-09-30):** `compose_lane_color` in `src/ui/nuklear/compose_canvas.cpp` picks lane backgrounds from the role plus `display_order & 1` ("alternate"), which does not match white/black keys once a scratch lane shifts the order, and has no PMS button colors; note fills ignore the lane entirely (`compose_note_style` uses one light blue, or cyan when selected).
- **Read first:** `include/wormedit/core/document.h` (`lane_definition_t`, `lane_role_t`), `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp` (every `make_*_profile`), `src/formats/native_project.cpp` (lane definition JSON), `src/sys/compose_view.cpp` (note and lane projection).
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `tests/core/lane_profile_tests.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`.
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

- **Status:** blocked-on T-269
- **Outcome:** tile notes (BMS/BMSON/osu!/PMS lanes) fill with their lane's key color: scratch notes a strong red matching the scratch lane, black-key notes a deeper blue, white-key notes light neutral, PMS buttons their button color; each lane background is a dark shade of the same hue so note and lane read as a pair; selected notes keep their fill and get a 2 px green outline (iBMSC style) instead of turning cyan. Arrow lanes keep the snap colors from T-204.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`compose_note_style`, `compose_lane_color`, hold body drawing), `assets/themes/compose-field.json`, `src/ui/nuklear/theme.cpp`, `docs/chart-editor-plan/tasks/panels-menus.md` (T-225 design reference: Tailwind-style scales from the octet.design palette).
- **Files owned:** `assets/themes/compose-field.json`, `src/ui/nuklear/theme.cpp/.h`, `src/ui/nuklear/compose_canvas.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`.
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
- **Read first:** `include/wormedit/core/interaction.h`, `src/sys/compose_input.cpp`, `src/core/command_registry.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose_input.cpp`, `src/sys/application_commands.h/.cpp`, `src/sys/application_command_dispatch.cpp/.h`, `src/sys/transport_player_actions.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/core/command_registry.cpp`, `src/sys/main.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/core/interaction_tests.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 1. Replace Space range recognition with Shift plus keyboard cursor movement, preserving the shared exact range controller. 2. Route Space through Play/hard Stop and suppress repeated toggles. 3. Verify range reversal, cancellation, text focus and immediate audio stop in isolated tests.
- **Invariants:** Shift mouse dragging keeps its offset behavior; navigation alone never edits document data.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

## T-297 Edit the audio offset inline on double click

- **Status:** done
- **Outcome:** Double-clicking the displayed audio offset opens a focused inline numeric editor; Enter commits one exact undoable offset edit, Escape cancels, and losing focus closes it.
- **Read first:** `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `include/wormedit/core/audio_offset_editor.h`.
- **Files owned:** `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/compose_actions.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 1. Reuse the inline numeric editor and offset command. 2. Focus and select its text on double click and close it on blur. 3. Verify signed values, invalid input, undo, keyboard ownership and isolated actual typing.
- **Invariants:** Audio offset sign, units and exact representation retain the existing command semantics.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.
- **Evidence:** Authoritative application build passed; full suite 1175/1175 and frontend-state 61/61 passed. Configured clang-tidy passed for all six changed C++ files with no new complexity violations. Actual isolated Xvfb/SDL-dummy checks verified immediate selected-text focus, signed exact edits (-1/8 and 11/4), one undo to 1/3, invalid fraction retention/correction, Escape, ordinary/native blur and unchanged chart payload apart from offset. Final covered-control gate, typing/Escape and clean title-bar shutdown passed; literal Compose smoke passed and its PNG was inspected (`/tmp/wormedit-t297/final-smoke.png`).

## T-298 Lock audio offset changes beside its tools

- **Status:** done
- **Outcome:** A visible padlock beside the offset tools prevents offset changes from numeric edits, nudges and drag gestures while locked.
- **Read first:** `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_toolbar.cpp`.
- **Files owned:** `src/ui/nuklear/metadata_panel.cpp` (accurate offset-lock explanation only), `src/sys/main.cpp` (offset-only lock/capture boundaries), `src/sys/application_frame_projection.cpp` (offset control projection only), `src/ui/nuklear/compose_input_panel.cpp` (offset capture guard only), `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Add one shared offset-lock state and display its padlock near the offset tools. 2. Enforce it at offset action and gesture boundaries, cancelling any pending offset preview when locking. 3. Verify every editing entry point rejects without mutation.
- **Invariants:** Locking offset does not prevent navigation, BPM editing or playback; unlocking does not replay a rejected edit.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.
- **Evidence:** Authoritative application build passed and full suite passed 1180/1180 (63.24 s). Configured clang-tidy passed for all nine changed C++ files with no source-owned complexity violations. Real isolated Xvfb/SDL-dummy checks verified the padlock, rejected numeric/nudge/calibrated waveform and offset-fit changes, retained exact saved 1/3, allowed BPM fitting and playback while locked, and restored fresh exact 1/4 editing without replay on unlock. Runtime regression covers locking before a simultaneous captured release, offset preview cancellation and BPM preview retention. Locking cancels an open inline edit; Metadata explains the independent offset lock correctly. Chart payload comparison, clean shutdown and literal Compose smoke passed; PNGs were inspected (`/tmp/wormedit-t298/final-smoke.png`).

## T-299 Use the upper progress bar only when no BGM exists

- **Status:** ready
- **Outcome:** The progress bar above the toolbar replaces the lower minimap only for charts without BGM; charts with BGM retain the lower minimap as the authoritative overview.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/compose_view.cpp`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_view.cpp/.h`, `src/ui/frontend.h`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:** 1. Project actual BGM presence independently of viewport visibility. 2. Select exactly one overview location with stable layout. 3. Verify empty/populated/offscreen BGM charts, navigation and audio-tail seeking in isolated captures.
- **Invariants:** An offscreen or hidden BGM event still requires the minimap; existing exact seek behavior is preserved.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

## T-300 Repeat Tab navigation every 200 milliseconds while held

- **Status:** ready
- **Outcome:** Holding Tab repeats its existing cursor displacement at a 200 ms interval after the initial action; releasing it, focus loss or an input-owning dialog stops repetition.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose_input.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/sys/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:** 1. Retain the current Tab action and direction. 2. Use monotonic repeat timing independent of OS key-repeat rate. 3. Verify deterministic held/released/focus-loss traces, including Shift range navigation.
- **Invariants:** Text widgets retain Tab ownership; no queued repeat survives focus loss or a chart change.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

## T-301 Fix slow macOS trackpad navigation

- **Status:** ready
- **Outcome:** Trackpad navigation retains precise scroll deltas and responds without avoidable per-event chart reconstruction or artificial low-frequency gating; screen-space movement respects macOS natural scrolling consistently in upscroll and downscroll, without double direction inversion.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose_input.cpp`, `src/sys/frame_timing.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Trace wheel precision, SDL flipped-direction normalization, upscroll/downscroll screen movement, event delivery and navigation work before changing behavior. 2. Correct the measured bottleneck using shared navigation paths. 3. Verify fractional/rapid synthetic event traces and report isolated measurements; actual macOS confirmation remains required evidence when a Mac is available.
- **Invariants:** No guessed speed multiplier; normalize device direction once, preserve intuitive screen-space movement, cursor bounds and input ownership.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

## T-302 Support macOS trackpad pinch zoom

- **Status:** ready
- **Outcome:** SDL pinch gestures zoom the chart around the gesture focus using the same bounded zoom transform as existing controls.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_scroll_geometry.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_scroll_geometry.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Handle available SDL pinch begin/update/end events with version-compatible builds. 2. Reuse existing zoom bounds and coordinate anchoring, with focus/gesture cancellation. 3. Test scale accumulation, reversal, invalid scales and input occlusion; document actual macOS confirmation separately.
- **Invariants:** Pinch does not edit notes or offset; missing pinch support in older SDL keeps existing zoom controls.
- **Reject/failure behavior:** invalid or stale input leaves the document and selection unchanged; unsupported input is diagnosed.
- **Verify:** standard build/test; configured clang-tidy on changed C++; isolated Xvfb/SDL-dummy checks for UI/audio behavior.
- **Out of scope:** unrelated rendering and preferences work.

## T-303 Prioritize the Compose note canvas without a focus click

- **Status:** done (shared canvas ownership and FIFO key edges; real isolated first-key, wheel, pan and paint checks passed; IME-only ownership retained)
- **Outcome:** while Compose is visible, the note-editing canvas accepts the first wheel/scroll, pan, object-insertion and edit gesture without requiring previous panel focus; keyboard navigation and insertion target the canvas when no actual text/IME/dialog owner is active.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/sys/compose_input.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/sys/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_options_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Reproduce first-gesture loss after focusing toolbar/inspector/another panel. 2. Share one input-ownership decision that prioritizes the note canvas independently of stale toolkit panel focus. 3. Verify first wheel, pan and object insertion, keyboard navigation, matching releases and text/modal occlusion.
- **Invariants:** the note canvas has highest editing priority; actual text/IME entry and occluding dialogs retain their input. The first gesture is neither dropped nor applied twice, and hidden Compose does not consume other tabs' events.
- **Reject/failure behavior:** blocked or invalid gestures leave the document and selection unchanged; focus/capture loss retires held inputs.
- **Verify:** standard build/test; configured clang-tidy; isolated real-window checks after focusing inspector and toolbar, plus text/Options/history occlusion regressions.
- **Out of scope:** changing note-editing command semantics.

## T-306 Honor the configured playhead anchor in scene validation

- **Status:** ready
- **Outcome:** A valid configured playhead anchor such as 35 percent renders a valid Compose scene during playback, Stop and audio-tail seeking instead of reporting viewport scroll outside chart content.
- **Read first:** `src/core/editor_scene.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose_playhead_cursor.h`, `src/sys/compose_view.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `src/sys/compose_playhead_cursor.h`, `src/sys/compose_view.cpp/.h`, `tests/core/interaction_tests.cpp`, `tests/core/editor_scene_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Reproduce configured-anchor following against fresh and cached scene validation, including a short chart with a longer audio tail. 2. Share the validated viewport margin contract between following and scene consumers, retaining default 20 percent behavior and rejecting invalid parameters. 3. Verify exact cursor/scroll and fresh-versus-cached primitives during Play, Stop, seeking and resize in upscroll/downscroll.
- **Invariants:** Existing chart/document data and selection stay unchanged; scene validation remains bounded and rejects invalid scroll; default callers remain compatible.
- **Reject/failure behavior:** Invalid parameters reject without mutation; cached and fresh scene paths agree on valid bounds.
- **Verify:** standard build/test; configured clang-tidy; isolated Xvfb/SDL-dummy short-chart audio-tail playback and Stop checks with 35 percent anchor.
- **Out of scope:** T-272 measure-index borrowing, overview placement, unrelated rendering optimizations.
