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

- **Status:** ready
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
- **Verify:** standard build/test; smoke PNG of `compose` with an SM fixture (compare with the chevron capture).
- **Out of scope:** textured skins, the player preview.

## T-205 Add a note-kind palette to Compose

- **Status:** ready
- **Outcome:** the toolbar shows the note kinds the chart's profile allows (tap, hold, mine, invisible, lift, fake, roll) as toggle buttons with number-row shortcuts (1–7); click, lane-key, and paint placement create the armed kind.
- **Read first:** `include/wormedit/core/document.h` (`note_kind_t`), `include/wormedit/core/interaction.h` (placement actions), `src/ui/nuklear/compose_toolbar.cpp`, `src/core/command_registry.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/core/interaction_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`.
- **Steps:** 1. Armed kind in interaction state (default tap). 2. Per-profile allowed set (BMS: tap, hold, invisible, mine; SM: tap, hold, roll, mine, lift, fake; osu!/Quaver: tap, hold, mine). 3. Registry commands `edit.note_kind_<kind>`. 4. Toolbar buttons with icons.
- **Invariants:** hold entry gestures still create holds regardless of the armed tap-like kind (roll when T-170 lands).
- **Reject/failure behavior:** switching to a chart whose profile lacks the armed kind resets to tap.
- **Verify:** standard build/test.
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

- **Status:** ready
- **Outcome:** zooming Compose past the base level shows finer detail: a 32-frames-per-bin level is generated on demand for the visible range, and below 1 frame per pixel the waveform draws PCM samples from the decoded block cache.
- **Read first:** `include/wormedit/audio/waveform.h` (`waveform_level_for_zoom`, `project_waveform`), `src/sys/waveform_analysis_runtime.cpp` (fixed `256`), `include/wormedit/audio/block_cache.h`, `src/sys/waveform_view_projection.cpp`.
- **Files owned:** `include/wormedit/audio/waveform.h`, `src/audio/waveform.cpp`, `src/sys/waveform_analysis_runtime.cpp/.h`, `src/sys/waveform_view_projection.cpp/.h`, `tests/core/audio_waveform_tests.cpp`, `src/audio/aligned_waveform.cpp`, `include/wormedit/audio/aligned_waveform.h`, `src/sys/application_frame_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp` (visible-range and PCM wiring scope extended by user approval on 2026-09-30).
- **Steps:**
  1. Test first: at 4 frames/pixel the projection returns per-column min/max from a finer source than 256.
  2. Visible-range fine analysis job (bounded, cancelable, keyed by source and range).
  3. Sample-level path: read the visible block(s) from the cache, emit one polyline point per frame.
- **Invariants:** memory bounded by the visible range; the frame thread never decodes.
- **Reject/failure behavior:** while fine data is pending, draw the coarse level.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual zoom check.
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

- **Status:** blocked-on T-211
- **Outcome:** only if T-211 shows the audio pump costs more than 1 ms at p99: the preview session's refill runs on a worker; the frame thread only publishes positions and reads status.
- **Read first:** `src/sys/sdl_preview_output.cpp`, `include/wormedit/audio/preview_session.h` (control-thread contract), `docs/async-task-scheduling.md`.
- **Files owned:** `src/sys/sdl_preview_output.cpp/.h`, `src/audio/preview_session.cpp`, `tests/sys/sdl_preview_output_tests.cpp`.
- **Steps:** 1. Dedicated refill thread with the existing generation guards. 2. Hard Stop still silent on the next render (existing test must pass).
- **Invariants:** `render()` stays allocation-free; Stop semantics unchanged.
- **Reject/failure behavior:** refill failure reports through the existing status path.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|sdl_preview'`; T-211 overlay before/after numbers.
- **Out of scope:** audition output.

## T-213 Reuse the Compose scene while only the scroll changes

- **Status:** blocked-on T-211
- **Outcome:** only if T-211 shows scene rebuild dominates: during playhead following, a scroll change reuses the cached scene for a margin of rows above/below and translates it, rebuilding only when the viewport leaves the cached window or the revision changes.
- **Read first:** `src/sys/compose_view.cpp` (`compose_scene_cache_t`, rebuild conditions), `src/core/editor_scene.cpp`.
- **Files owned:** `src/sys/compose_view.cpp/.h`, `src/core/editor_scene.cpp`, `tests/core/editor_scene_tests.cpp`.
- **Steps:** 1. Compose one screen of extra rows each side. 2. Rebuild when fewer than a quarter screen remains. 3. Test that translated primitives equal a fresh build at the same scroll.
- **Invariants:** identical pixels to a fresh build.
- **Reject/failure behavior:** any mismatch falls back to rebuilding.
- **Verify:** standard build/test; T-211 numbers before/after.
- **Out of scope:** GPU-side caching.

## T-214 Launch an external previewer (mBMplay and others)

- **Status:** ready
- **Outcome:** Preferences hold a list of players with a path and three argument templates (from start, from here, stop) using `<apppath>`, `<filename>`, and `<measure>`; Play/Play here/Stop commands write a temporary BMS of the active chart beside the source (so relative `#WAV` paths resolve) and launch the player with an argument vector. Defaults copy iBMSC 3.5.5.16 (`MainWindow.vb` `pArgsInit`): mBMplay `"<filename>"`, `-s <measure> "<filename>"`, `-t`; uBMplay and o2play `-P -N0 "<filename>"`, `-P -N<measure> "<filename>"`, `-S`.
- **Read first:** `src/formats/bms_writer.cpp` / `include/wormedit/formats/bms_writer.h` (active-projection export), `src/sys/main.cpp` (preferences path from the shortcut profile), `src/core/command_registry.cpp`.
- **Files owned:** new `src/sys/external_player.cpp/.h`, new `tests/sys/external_player_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `src/ui/frontend.h`, new `src/ui/nuklear/external_player_panel.cpp/.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Pure template expansion into an argument vector (split the template on spaces outside quotes, then substitute; never build a shell string). Tests for quoting and `<measure>` from the cursor's source measure number.
  2. Temp file name `___wormedit_preview.bms` (or `.bme`/`.pms` matching the source) in the chart directory; remember and delete temp files on exit, as iBMSC does.
  3. Launch with `SDL_CreateProcess` (SDL3) without waiting; report failures in the status bar.
  4. On Linux/macOS allow the path to be a wrapper (for example `wine`) with the `.exe` in the argument template.
  5. Register `transport.external_play_start`, `transport.external_play_here`, `transport.external_stop`.
- **Invariants:** the real chart file is never overwritten; no shell is invoked.
- **Reject/failure behavior:** a missing player path opens the preferences panel; export errors block launch.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'external_player'`; manual run with a stub script that logs its arguments.
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

- **Status:** blocked-on-user
- **Outcome:** preview plays the right normal/addition samples for each note.
- **Read first:** `docs/chart-editor-plan/playback-audio.md`, `include/wormedit/audio/preview_bridge.h`.
- **Files owned:** decided after the STOP.
- **Steps:** 1. STOP: ask where default samples come from (user-supplied skin folder, bundled free samples, or silence when absent). 2. Split into resolver and playback cards.
- **Invariants:** custom per-map samples always win over defaults.
- **Reject/failure behavior:** missing samples are silent with one diagnostic.
- **Verify:** defined by the split cards.
- **Out of scope:** skin browsing.
