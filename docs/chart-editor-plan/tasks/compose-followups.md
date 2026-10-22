# Task cards: Compose follow-ups (palette, docks, BGM layers, overview)

[Back to the task-card index](../tasks.md).

User request (2026-10-03), after the [semantic-layer UX cards](semantic-layers.md)
and the [keysound listbox (T-335)](sounds-tab.md):

1. Show a ghost note at the pointer while note drawing is armed (T-343).
2. Alt+digit does not pick the tool from the palette (T-344).
3. Bind Select to Alt+1 and shift the note kinds to Alt+2–Alt+8 (T-345).
4. Offer "place note and advance to the next sound" in the Sounds dock (T-346).
5. Add a BGA dock like the keysound dock (T-347).
6. Let BGM events join a semantic layer (T-348 core, T-349 UI).
7. Show measure, beat and time in a tooltip over the seek bar and song overview (T-350).
8. Keep related sounds together in name order (snd01, snd02, …) and show how many BMS IDs are left (T-351 core, T-352 docks).
9. Group related sounds, suggest groups at import with a review step, and filter the dock by group and layer (T-353–T-357).
10. Support bulk file drops in BMS slots mode as well as automatic mode (T-359, user request 2026-10-04).
11. Add a timeline lane for BGA editing in Compose (T-360, user request 2026-10-04).

Findings (2026-10-03):

- Alt+1–7 are registered as `edit.note_kind_*` in `src/core/command_registry.cpp`
  and only call `arm_note_kind`. Clicking a palette button also turns on note
  paint mode (`render_compose_note_palette_column` in
  `src/ui/nuklear/compose/compose_toolbar.cpp`). The shortcut therefore changes the
  armed kind but leaves the Select tool active.
- Paint previews (`paint_preview` in `src/core/editor_scene.cpp`) exist only
  for the cells of an active drag. Nothing marks the cell under an idle pointer.
- "Auto next after place" exists only in the inspector resource drawer
  (`render_auto_increment` in `src/ui/nuklear/compose/compose_resources.cpp`,
  `worm_ui_nuklear_advance_resource_picker`). The Sounds dock arms the same
  `armed_resource_id` but has no toggle, and the next sound is chosen in
  catalog order, not the dock's filtered order.
- `bgm_event_t` in `include/wormedit/core/document.h` has no `semantic_layers`
  field. Only `note_t` has one.
- T-230 (Images panel) assumed the T-228/T-229 dock, which T-335 replaced with
  its own column. T-230 is merged into T-347 (user decision 2026-10-03).
- In BMS, each sound and image has a two-character ID (`#WAV01 kick.wav`), and
  the chart data refers to these IDs. User clarification (2026-10-03): which
  exact ID a file gets does not matter. What matters is (a) related sounds stay
  grouped, sorted by name the way authors number them (`snd01`, `snd02`, …,
  `snd10`), and (b) capacity: a chart has at most 1,295 IDs (`01`–`ZZ`), or
  3,843 in base 62. Bulk import (`plan_sound_import` in
  `src/sys/compose/actions/compose_sound_actions.cpp`) already sorts by folded name before
  allocating, but compares plain text, so `snd10` sorts before `snd2`.
  Other editors (iBMSC, BMSE) list sounds in ID order, so a group that gets
  one unbroken run of IDs also stays together for people who open the chart
  there. Allocating each group as a contiguous run is a goal in its own right
  (2026-10-03). T-351 and T-352 cover automatic assignment. User correction
  (2026-10-04): the sound picker must also offer optional BMS slots mode with
  direct assignment to a chosen ID for compatibility with existing workflows
  (T-358). Automatic assignment remains the default; ID-order sorting alone
  does not satisfy slots mode. The supported codec currently uses base 36.
- Sound groups (user decisions 2026-10-03): with about 1,200 sounds, the dock
  needs groups and filters. A group is a flat, song-wide set of sounds; there is
  no nesting, and a sound is in at most one group or ungrouped. Groups exist only
  when the user accepts or creates them, never as one entry per file. Authors
  often import everything at once (for example sliced stems), so groups are
  proposed from file names, and the user must be able to reject a proposal,
  fix it, or leave files ungrouped. Manual grouping in the dock is the fallback
  when the guess is wrong. Each group gets one contiguous ID run (T-351). Tags
  (T-324) stay as loose labels, and semantic layers stay per chart.

Apply the stale-plan check (rule 9) first.

## T-343 Show a ghost note under the pointer while note paint is armed

- **Status:** done
- **Outcome:** with note paint armed and no gesture in progress, hovering over a playable lane draws a translucent note of the armed kind (the head only for hold/roll) in the exact snapped cell that a click would create. Over an occupied cell it uses the erase-preview color. It is not drawn when the pointer leaves the field, while a drag preview is active, or in Select mode.
- **Read first:** `src/sys/compose/views/compose_view.cpp` (`hover_note_id` projection), `src/sys/compose/input/compose_input.cpp` (paint click cell resolution, `edit_snap_denominator`), `src/ui/nuklear/compose/compose_canvas.cpp` (`compose_note_style`, `draw_compose_note_glyph`), `src/ui/frontend.h`.
- **Files owned:** `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/views/compose_view.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_canvas.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Ownership expansion:** user authorized supporting files on 2026-10-03; `tests/sys/frame_timing_tests.cpp` exercises retained scene reuse with an idle paint ghost.
- **Steps:**
  1. Extract the function that turns a pointer position into the paint cell (lane, snapped row, valid/occupied), so clicks and the ghost share it.
  2. Project `paint_ghost_visible`, its bounds, glyph, armed kind and `occupied` from the last hover pointer, following the hover-note path.
  3. Draw it after the notes at 45% alpha, in the active color view (T-336), with no outline.
  4. Tests: the ghost cell equals the cell that a click at the same pointer inserts, for snap on and off, in upscroll and downscroll.
- **Invariants:** no document change. The scene reuse key ignores the ghost, so it never invalidates the cached scene.
- **Reject/failure behavior:** a lane that rejects the armed kind draws the invalid-preview color.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frame-timing\.|^frontend-state\.'`; clang-tidy on changed `.cpp`; isolated Xvfb PNG with paint armed and the pointer over a lane.
- **Verification evidence:** 2026-10-03: application build passed; `100% tests passed out of 296` for frame/frontend state. Snap on/off and up/down click-cell agreement, occupied-head alignment and retained scene reuse pass. Isolated Xvfb ghost PNG inspected at `/tmp/followups-ghost-final.png`. Clang-tidy reports no new functions above 25; existing violations remain unchanged.
- **Out of scope:** ghosts for resource placement (T-346 may reuse this).

## T-344 Make Alt+digit pick the palette tool, as a click does

- **Status:** done
- **Outcome:** pressing an Alt+digit note-kind shortcut in Compose arms that kind and switches to the note paint tool, so the palette button lights up exactly as after a click. Pressing it again while that kind is already armed keeps paint mode on.
- **Read first:** `src/sys/commands/application_command_dispatch.cpp` (`note_kind_command`), `src/ui/nuklear/compose/compose_toolbar.cpp` (`render_compose_note_palette_column`), `src/core/command_registry.cpp` (note-kind chords), `docs/frontend-event-harness.md`.
- **Files owned:** `src/sys/commands/application_command_dispatch.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Steps:**
  1. Harness repro first: send Alt+2 to a focused Compose lane, then assert the dispatched command and that `note_paint_mode_active` stays false. If the chord never reaches dispatch, record where it is dropped (text-entry owner, Alt menu mnemonic, SDL keymod) and fix that path instead.
  2. In `note_kind_command`, enable note paint mode after a successful `arm_note_kind`, using the same core call the toolbar toggle uses.
  3. Tests: Alt+N from Select arms kind N and enables paint mode; an Alt+N kind the profile disallows rejects and leaves the tool unchanged.
- **Invariants:** a disallowed kind never changes the armed kind or the tool.
- **Reject/failure behavior:** as today, with a status message.
- **Verify:** `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^frame-timing\.'`.
- **Verification evidence:** 2026-10-03: the initial event-harness repro confirmed that Alt+2 reached dispatch while Select remained active. After the fix: `100% tests passed out of 39` (event harness), `100% tests passed out of 140` (frame timing/application dispatch), including repeated activation and disallowed-kind rejection.
- **Out of scope:** renumbering (T-345).

## T-345 Bind Select to Alt+1 and shift note kinds to Alt+2–Alt+8

- **Status:** done
- **Outcome:** Alt+1 selects the Select tool (leaves note paint mode). Tap, Hold, Mine, Invisible, Lift, Fake and Roll move to Alt+2 through Alt+8. Palette tooltips and the command list show the new chords.
- **Read first:** `src/core/command_registry.cpp`, `src/sys/commands/application_commands.cpp` (`parse_application_command`), `src/ui/nuklear/compose/compose_toolbar.cpp` (palette labels), `src/sys/app/main.cpp` (note-kind command state).
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/app/main.cpp`, `src/ui/nuklear/compose/compose_toolbar.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Steps:**
  1. Add an `edit.tool_select` command (Alt+1) that turns note paint mode off. Its checked state mirrors `!note_paint_mode_active`.
  2. Move the seven note-kind chords to digit_2…digit_8.
  3. Update the palette tooltips ("Select (Alt+1)", "Tap (Alt+2)", …).
  4. Tests: chord table (no duplicate Alt+digit in Compose scope); harness case for Alt+1 after Alt+3.
- **Invariants:** command IDs for note kinds are unchanged, so saved user keybindings keep their targets.
- **Reject/failure behavior:** Alt+1 with no Compose chart rejects like the other edit commands.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'command|palette Alt digits|Note kind|Select shortcut'`; `ctest --test-dir build -L event-harness`.
- **Verification evidence:** 2026-10-03: `100% tests passed out of 41` (command/shortcut checks), `100% tests passed out of 39` (event harness). All eight chords are unique; Alt+3 followed by Alt+1 returns to Select.
- **Out of scope:** user-editable keybindings UI.

## T-346 Place and advance to the next sound from the Sounds dock

- **Status:** done
- **Outcome:** the Sounds dock toolbar has an "Advance after place" toggle (an icon with a tooltip). While it is on, each successful placement of the armed sound arms the next row in the dock's current visible order (search filter and sort applied), scrolls it into view, and highlights it. After the last row, paint mode turns off.
- **Read first:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/resources/keysound_list_state.h`, `src/ui/nuklear/compose/compose_state.cpp` (`worm_ui_nuklear_advance_resource_picker`), `src/sys/compose/input/compose_input.cpp` (`next_eligible_placement_resource` callers).
- **Files owned:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/compose_state.cpp/.h`, `tests/ui/keysound_list_state_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `src/ui/nuklear/compose/compose_resources.cpp`, `src/ui/frontend.h`, `src/sys/compose/actions/compose_actions.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Ownership expansion:** user authorized supporting files on 2026-10-03; sound selection transfers active note painting to resource placement through a revision-checked frontend action. Event-harness coverage checks that transfer and advancement from an existing selection. Resource drawer arming clears dock provenance, and Compose state tests cover selection and rejected-placement behavior.
- **Steps:**
  1. Add a pure `next_visible_keysound(list_state, placed_id)` that returns the next row ID in visible order, skipping missing sounds.
  2. When the placed sound came from the dock, the frontend uses that helper instead of the catalog-order `next_resource_id`. The drawer path is unchanged.
  3. Share the `auto_increment_on_place` flag with the drawer toggle, so both show the same state.
  4. Tests: filtered order, missing-row skip, end of list, and no advance after a rejected placement.
- **Invariants:** advancing only changes the armed sound, never the document. Multi-row selection is replaced by the single advanced row.
- **Reject/failure behavior:** a rejected or undone placement does not advance.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.|keysound'`; isolated Xvfb: place three notes with a search filter active and confirm three different sounds.
- **Verification evidence:** 2026-10-03: `100% tests passed out of 162` (frontend state/keysounds). Isolated Xvfb placed filtered sound IDs 01, 02 and 04 in order and stopped painting after the last row; `/tmp/followups-advance-checked.png` inspected. Clang-tidy reports no new functions above 25.
- **Regression verification (2026-10-03):** selecting a sound while note painting, or enabling Advance with a sound selected, activates sound placement and preserves audition. Advancement replaces the primary selection before scrolling. Real-window Xvfb check placed filtered WAV IDs 01, 02 and 04; `/tmp/sound-paint-from-note.png` inspected. Build passed; `100% tests passed out of 1805` (117.14 s); new functions pass clang-tidy complexity checks.
- **Out of scope:** stroke painting of sounds (T-335 out of scope).

## T-347 Add a BGA dock beside the keysound dock (absorbs T-230)

- **Status:** done
- **Outcome:** a toolbar toggle shows a BGA column that works like the Sounds dock. It has a sticky toolbar (Paint, Add, Remove unused, Select all, Clear), search, and a virtualized multi-select list of image/video resources with a 32 px thumbnail for visible rows, BMS ID, file name, use count and missing state. Selecting a row arms it for BGA placement. Images dropped on the column are added in one undo step.
- **Read first:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/resources/keysound_list_state.h`, `src/sys/sound_list_projection.h`, `src/sys/compose/actions/compose_sound_actions.h`, `include/wormedit/visual/image_job.h`.
- **Files owned:** new `src/ui/nuklear/compose_bga_panel.cpp/.h`, new `src/sys/bga_list_projection.cpp/.h`, new `src/sys/compose_bga_actions.cpp/.h`, `src/ui/nuklear/keysound_list_state.cpp/.h` (generalize to a resource list, if needed), `src/ui/nuklear/compose/compose_panel.cpp`, `src/ui/nuklear/compose/compose_toolbar.cpp`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/frontend.h`, `src/sys/app/main.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, new `tests/sys/bga_list_projection_tests.cpp`, new `tests/sys/bga_actions_tests.cpp`, new `src/sys/bga_dock_runtime.cpp/.h`, `src/sys/application_frame_services.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/nuklear/frontend.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/ui/nuklear/compose/compose_resources.cpp`, `tests/formats/fixture_validation_tests.cpp`.
- **Ownership expansion:** user authorized supporting files on 2026-10-03; the dedicated dock runtime owns borrowed rows and asynchronous thumbnails, frame services route dropped images, and the Compose dispatcher connects catalog actions. Frontend wiring owns renderer texture lifecycle and the windowed list request. The resource drawer must accept armed resources beyond its window. Full-suite verification also revealed that the prior DP5 fixture was missing from the validation harness’s explicit profile map; the mapping is corrected.
- **Steps:**
  1. Project BGA/image resources (visible window only), following `sound_list_projection`.
  2. Reuse or generalize the list state (selection, search, scroll) instead of copying it.
  3. Bulk image import and remove-unused through the existing catalog commands, one history step each.
  4. Thumbnails through the existing image job, requested only for visible rows and cached by resource ID.
  5. Tests: projection window, search, use counts, and import/remove atomicity.
- **Invariants:** the BMS IDs of existing images never change.
- **Reject/failure behavior:** an unreadable image is skipped and reported, and the batch keeps the readable ones, as the Sounds dock does.
- **Verify:** `cmake --build build`; full `ctest --test-dir build --output-on-failure`; clang-tidy on new `.cpp`; isolated Xvfb PNG on a BMS fixture with `#BMP` entries.
- **Verification evidence:** 2026-10-03: application build passed; `100% tests passed out of 1804` (full suite; 117.54 s). Import/remove atomicity, a 1,200-resource window, and painting beyond the drawer window pass. Selected BGA remains armed when scrolled out of the visible window. Isolated Xvfb BMP dock PNG inspected at `/tmp/followups-bga-reviewed.png`. Clang-tidy reports no new functions above 25.
- **Out of scope:** video thumbnails; BGA layer channels beyond placement; direct BMP ID assignment. Sound slots mode is separate (T-358).

## T-348 Let BGM events belong to one semantic layer

- **Status:** done (2026-10-04)
- **Outcome:** `bgm_event_t` carries `semantic_layers` (at most one, matching notes). Assign, remove, toggle and clear (T-338) accept BGM event IDs next to note IDs. Deleting a layer with unlink also unlinks BGM events. Sound layer defaults (T-335) apply to placed BGM events. Native projects save and load it, and exporters ignore it.
- **Read first:** `include/wormedit/core/document.h` (`note_t`, `bgm_event_t`), `include/wormedit/core/semantic_layer_editor.h`, `src/formats/native_project.cpp` (note `semantic_layers`, `sound_layer_defaults`), `tests/core/semantic_layer_editor_tests.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/semantic_layer_editor.h`, `src/core/semantic_layer_editor.cpp`, `src/core/resource_assignment.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/core/semantic_layer_editor_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:**
  1. Append the field to `bgm_event_t`. `validate_document` rejects unknown layer IDs and more than one layer.
  2. Add a `bgm_events` ID list to each membership action (appended field). An ID found in neither list rejects the whole action.
  3. Serialize the field only when non-empty, as `sound_layer_defaults` does. If older readers reject unknown keys, go through the migration table instead.
  4. Tests: mixed note+BGM assignment in one undo step; locked layer rejection; delete-with-unlink; native round trip; BMS/BMSON export byte-identical with and without layers.
- **Invariants:** exported charts are unchanged; one layer per object ([open question 10](../decisions/open-questions.md)).
- **Reject/failure behavior:** stale revision, unknown ID or locked layer leaves the document and history untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|native_project'`.
- **Verification evidence:** application build passed; core/native gate passed 1,308/1,308 (`/tmp/wormedit-bgm-core-gates-passed.log`), including native round-trip and byte-identical BMS/BMSON export. Mixed edits, stale/locked rejection, unlink, placement defaults and membership validation passed. Owned production files passed clang-tidy without new cognitive-complexity warnings.
- **Out of scope:** UI (T-349).

## T-349 Color, assign and filter BGM events by semantic layer in Compose

- **Status:** done (2026-10-04)
- **Outcome:** in Layer view BGM events fill with their layer color (unassigned grey). Selected BGM events count toward the layer actions (panel row click, "Layer ▸" note menu, No layer), and layer paint and solo/hide/require/exclude filters treat BGM events like notes.
- **Read first:** `src/sys/compose/views/compose_view.cpp` (BGM event projection, layer filters), `src/sys/compose/actions/compose_actions.cpp` (semantic layer dispatch), `src/ui/nuklear/compose/compose_canvas.cpp` (BGM event drawing), `src/ui/nuklear/compose/compose_note_menu.cpp`.
- **Files owned:** `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_canvas.cpp`, `src/ui/nuklear/compose/compose_note_menu.cpp`, `src/ui/nuklear/compose/compose_layers_panel.cpp`, `tests/sys/application_commands_tests.cpp`, `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose/compose_semantic_layer_commands.h`, `src/ui/nuklear/compose/compose_object_input.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, `tests/core/editor_scene_bgm_layers_tests.cpp`, `tests/ui/compose_bgm_layer_tests.cpp`, `tests/CMakeLists.txt`, `src/ui/nuklear/compose/compose_inspector.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Supporting scope:** Coordinator-approved (2026-10-04): shared scene geometry/filtering and paint gesture contracts must carry BGM memberships; the semantic-action helper and pointer route must pass BGM IDs. Dedicated core, application and frontend regressions avoid overlapping test ownership. The inspector count/action labels also require `compose_inspector.cpp` so BGM selections receive the same enablement. Existing layer-dock harness cases in `tests/ui/nuklear_event_harness_tests.cpp` also need updated mixed-count labels; their workflow assertions remain intact. Shared UI overlaps remain coordinator-controlled.
- **Steps:**
  1. Project `semantic_color` for BGM events and draw it through the T-336 color helper.
  2. Pass selected BGM event IDs into the membership actions, and count them in the panel and inspector ("3 notes, 2 BGM").
  3. Apply layer filters and layer paint hit-testing to BGM lanes.
  4. Tests: assign a mixed selection from the panel; solo hides other BGM events.
- **Invariants:** BGM column layout and event order are unchanged.
- **Reject/failure behavior:** as T-348.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'Compose semantic-layer assignment|^frontend-state\.'`; isolated Xvfb PNG in Layer view with mixed assignments.
- **Verification evidence:** Full application suite 2,025/2,025; required frontend/application gate 235/235; seven core BGM scene/filter/paint regressions; isolated Layer-view blue/red/unassigned-grey BGM capture at `/tmp/wormedit-bgm-hover-groups-batch/layer-colored.png`. Mixed note/BGM counts and pointer paint workflows pass. Affected new functions pass cognitive-complexity checks.
- **Out of scope:** BGA events.

## T-350 Show measure, beat and time when hovering the seek bar or song overview

- **Status:** done (2026-10-04)
- **Outcome:** hovering the transport seek bar or the song overview shows a tooltip such as "Measure 12 · beat 3 · 1:23.456" for the position under the pointer. It uses exact timing, including BPM changes, stops and the audio offset, and matches where a click would seek.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_transport_seek_bar`, `render_status_minimap`), `src/ui/nuklear/compose/compose_panel.cpp` (`worm_ui_nuklear_render_compose_bgm_bar`), `src/sys/compose/views/compose_view.cpp` (seek/overview projection), `include/wormedit/core/timing.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, `src/ui/frontend.h`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/app/main.cpp`, `tests/sys/application_commands_tests.cpp`, `src/sys/compose_hover_timing.cpp/.h`, `tests/sys/compose_hover_timing_tests.cpp`, `tests/ui/compose_hover_timing_event_harness_tests.cpp`, `src/sys/application_frame_projection.cpp/.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/ui/nuklear/chrome/chrome.h`.
- **Supporting scope:** Coordinator-approved supporting scope: isolate exact hover conversion and regression coverage; pass context-bound hover input through application frame projection and register the new sources/tests. Coordinator-approved chrome helper renders timing and overview instructions in separate rows and bounds the timing tooltip within the window; the real-window check showed stock Nuklear newline rendering clipped the timing text.
- **Steps:**
  1. The frontend reports the hovered source frame (or none) in the action each frame. The application converts it with the timing index into measure, beat (1-based, by the measure's meter) and seconds, and projects it on the next frame.
  2. Draw the tooltip only while the projected frame matches the hovered frame, so it never shows a stale value.
  3. Tests: frame-to-label conversion across a BPM change, a stop and a non-4/4 meter; negative time before the offset prints with a minus sign.
- **Invariants:** hovering never seeks or changes the document. No allocation per frame beyond the fixed label buffer.
- **Reject/failure behavior:** with no timing or no audio, no tooltip.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'Compose hover'`; `ctest --test-dir build --output-on-failure -L hover-timing`; isolated Xvfb with xdotool hover over the seek bar, PNG shows the tooltip.
- **Verification evidence:** Application build passed; exact formatter regressions 5/5 and real frontend hover routes 3/3 passed, including BPM/stop/meter/offset, fractional padded seek agreement, stale context rejection and virtual audio tails. Final helper/frontend complexity checks pass. Isolated real-window captures show the seek tooltip at the right edge and the overview tail label with wrapped instructions: `/tmp/wormedit-bgm-hover-groups-batch/tooltip-seek-edge-final.png` and `tooltip-overview-final.png`. The visual fixture was corrected to include its missing initial BPM.
- **Out of scope:** hover tooltips on the Compose canvas rail.

## T-351 Number BMS IDs in natural name order and report capacity

- **Status:** done (2026-10-04; natural ordering, atomic contiguous/fragmented import, subset/all-kind renumber and base-36 capacity)
- **Scope approval (user, 2026-10-04):** The atomic bulk-import step may also edit `src/core/sound_import.cpp`, `include/wormedit/core/sound_import.h`, and `tests/core/sound_import_tests.cpp`.
- **Decision (user, 2026-10-04):** Capacity uses the codec's supported base-36 namespace (1,295 usable IDs, excluding `00`). Base-62 codec support is separate; lowercase IDs are not evidence of an expanded namespace because the current codec folds them to uppercase.
- **Outcome:** a shared natural name order (`snd2` before `snd10`, case-insensitive) drives bulk import. Each group in an import (T-355; ungrouped files count as one batch) gets one contiguous run of IDs in that order, placed in the first gap large enough to hold the whole batch, otherwise after the highest used ID. It is never scattered across small gaps, so editors that list by ID (iBMSC, BMSE) show the group together. A new `apply_bms_identifier_renumber` command gives the chosen resources (or all of one kind) one contiguous run in that order, in one undo step. A capacity query reports used and total IDs per namespace (WAV, BMP).
- **Read first:** `src/sys/compose/actions/compose_sound_actions.cpp` (`plan_sound_import`, `folded_name`), `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp` (`allocate_bms_identifier` path), `include/wormedit/core/document.h` (`target_resource_identity_t`).
- **Files owned:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, new `include/wormedit/core/natural_order.h`, new `src/core/natural_order.cpp`, `src/sys/compose/actions/compose_sound_actions.cpp`, `tests/core/resource_assignment_tests.cpp`, new `tests/core/natural_order_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/core/sound_import.cpp`, `include/wormedit/core/sound_import.h`, `tests/core/sound_import_tests.cpp`.
- **Steps:**
  1. Add `natural_name_less` (digit runs compare by value, then case-folded text, then raw bytes as the tiebreak, so the order is total and deterministic).
  2. Add `find_bms_identifier_run(document, namespace, count)`: the first free run of `count` consecutive IDs, else the run after the highest used ID. It returns nothing when no run fits.
  3. Use both in `plan_sound_import`: sort the batch by natural name and allocate it as one run. If no run fits but enough IDs are free in total, import into the fewest runs (largest gaps first, order kept) and report "Group split across N ID ranges; Renumber by name can rejoin it".
  4. Add `renumber_bms_identifiers_action_t{namespace, resources (empty = all)}`: sort by natural name and move the set into one run, counting the set's own current IDs as free. Resources outside the set keep their IDs. Renumbering all of one kind packs them from `01`.
  5. Add `bms_identifier_capacity(document, namespace)` → used, total (base 36, per the 2026-10-04 decision), and the largest free run.
  6. Import that would exceed total capacity rejects before any change, naming how many IDs are left.
  7. Tests: natural order (`snd2`/`snd10`, `a1b2`/`a1b10`, equal names); a batch of 5 skips a 3-ID gap and fills a later 5-ID gap; no fitting run appends after the highest ID; fragmented-only space splits with the report; renumber of a subset into one run; undo; capacity counts; over-capacity import rejects; BMS export writes the new IDs with unchanged notes.
- **Invariants:** resource IDs and note references are unchanged; renumbering changes only BMS IDs; a group's IDs follow its natural name order.
- **Reject/failure behavior:** a stale revision or an over-capacity import leaves the document untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'` passed 1,242/1,242. Final focused BMS allocation/order/renumber/export checks passed 14/14 after strengthening largest-gap and unselected-ID coverage. Affected-source clang-tidy found no new complexity violations.
- **Out of scope:** dock UI (T-352); optional direct sound ID assignment/slots mode (T-358). Automatic assignment here must coexist with that mode.

## T-352 Sort the docks by name and show BMS ID capacity

- **Status:** done (2026-10-04; natural/ID dock sorting, complete capacity footers and confirmed renumbering)
- **Scope approval (user, 2026-10-04):** Supporting application wiring may also edit `src/sys/compose_view.cpp/.h`, `src/sys/app/main.cpp`, `src/sys/compose/actions/compose_actions.cpp`, and their existing action/request headers as needed for dock sorting and confirmed renumbering. Caller audit additions also approved: `src/sys/workspace_projection.cpp`, `src/sys/resources/bga_dock_runtime.cpp`, `src/ui/nuklear/frontend.cpp`. Projection input wiring also approved: `src/sys/workspace_projection.h`, `src/sys/application_frame_projection.cpp`.
- **Outcome:** the Sounds and BGA docks list rows in natural name order by default, with a toggle back to ID order, and each row shows its ID. The dock footer shows capacity ("412 / 1,295 IDs · largest free run 40"), turning amber above 90%. The toolbar has "Renumber by name", which applies T-351 to the selected rows (or all) after a one-line confirmation. For formats without IDs (BMSON, osu!, StepMania), the ID column, capacity and Renumber are hidden.
- **Read first:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/compose/compose_bga_panel.cpp`, `src/ui/nuklear/resources/keysound_list_state.h`, `src/sys/sound_list_projection.h`, `include/wormedit/core/natural_order.h`.
- **Files owned:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/compose/compose_bga_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/sys/sound_list_projection.cpp/.h`, `src/sys/bga_list_projection.cpp/.h`, `src/sys/compose/actions/compose_sound_actions.cpp`, `src/sys/compose/actions/compose_bga_actions.cpp`, `src/ui/frontend.h`, `tests/sys/sound_list_projection_tests.cpp`, `tests/sys/bga_list_projection_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`. Supporting scope approved 2026-10-04: `src/sys/compose_view.cpp/.h`, `src/sys/app/main.cpp`, `src/sys/compose/actions/compose_actions.cpp` and existing action/request headers (including `src/ui/nuklear/compose/compose_state.h`), `src/sys/workspace_projection.cpp`, `src/sys/resources/bga_dock_runtime.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/workspace_projection.h`, `src/sys/application_frame_projection.cpp`.
- **Steps:**
  1. Projection sorts matching rows by natural name or by ID, before windowing; it also projects the ID and the capacity.
  2. Add the sort toggle and the capacity footer.
  3. Add "Renumber by name" with confirmation, dispatched through T-351.
  4. Tests: window order under both sorts with a search filter; capacity values; Renumber of a selection (harness).
- **Invariants:** sorting is view-only; IDs change only through Renumber or import.
- **Reject/failure behavior:** a rejected renumber reports through notifications and leaves the list unchanged.
- **Verify:** `cmake --build build`; full `ctest --test-dir build --output-on-failure`; isolated Xvfb PNG of the Sounds dock on a BMS fixture with `snd1`…`snd12`.
- **Out of scope:** optional BMS sound slots mode and direct assignment, tracked by T-358. The ID-order toggle here is sorting, not slots mode.

## T-353 Add flat sound groups to the resource catalog

- **Status:** done (2026-10-04; atomic flat groups, empty-group cleanup, native persistence and unchanged BMS/BMSON exports)
- **Scope approval (user, 2026-10-04):** `src/core/resource_assignment.cpp` is added to Files owned so ordinary sound deletion removes any group it empties in the same undo step; coverage stays in the owned sound-group tests. Also approved: `src/core/command.cpp` and `include/wormedit/core/command.h` for a command option that preserves retained export source flags during native-only group edits.
- **Outcome:** the song has a list of sound groups (stable ID, name), and each sound resource has an optional group ID. Commands create a group from sounds, add sounds to a group, ungroup sounds, rename a group and delete a group (its sounds become ungrouped), each in one undo step. A group left with no sounds is removed in the same command. Native projects save groups, and exporters ignore them.
- **Read first:** `include/wormedit/core/document.h` (`resource_entry_t`, `song_document_t`), `include/wormedit/core/resource_assignment.h` (`apply_resource_sound_metadata`), `src/formats/native_project.cpp` (`sound_kind`/`tags` serialization), `tests/core/resource_assignment_tests.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, new `include/wormedit/core/sound_groups.h`, new `src/core/sound_groups.cpp`, new `tests/core/sound_groups_tests.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/core/resource_assignment.cpp` (user-approved deletion integration, 2026-10-04), `src/core/command.cpp`, `include/wormedit/core/command.h` (user-approved native-only export preservation, 2026-10-04).
- **Steps:**
  1. Add `sound_group_t{id, name}`, `song_document_t::sound_groups`, and `resource_entry_t::sound_group` (optional, appended). `validate_document` rejects unknown group IDs, duplicate names (case-insensitive), and empty groups.
  2. Add the commands as one `apply_sound_group_edit` variant, as `semantic_layer_editor` does. Names are trimmed, 1–64 bytes.
  3. Serialize groups only when non-empty, so existing files round-trip byte-identically. If that needs a schema bump, **STOP** and ask.
  4. Tests: create, add, move between groups, ungroup, rename collision, delete group, auto-removal of an emptied group, undo, round trip, BMS/BMSON export unchanged.
- **Invariants:** BMS IDs, tags, layers and note references are unchanged by group edits.
- **Reject/failure behavior:** an unknown sound or group, a duplicate name or a stale revision leaves the document untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|native_project'`.
- **Out of scope:** UI; ID renumbering when grouping (T-356).

## T-354 Propose sound groups from file names

- **Status:** done (2026-10-04; deterministic filename group proposals)
- **Outcome:** a pure function `propose_sound_groups(names, existing_groups)` returns proposed groups and ungrouped names. Names that share a stem after removing a trailing number and separators (`kick_001`…`kick_040` → "kick", `drums-0001.wav` → "drums", `snd01` → "snd") form a proposal when at least two match. A stem equal to an existing group's name proposes joining that group, and everything else stays ungrouped. The result is deterministic.
- **Read first:** `include/wormedit/core/natural_order.h` (T-351), `include/wormedit/core/sound_groups.h` (T-353).
- **Files owned:** new `include/wormedit/core/sound_group_proposal.h`, new `src/core/sound_group_proposal.cpp`, new `tests/core/sound_group_proposal_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Stem rule: drop the extension, then a trailing digit run, then trailing `_ - . space`, and compare case-insensitively. A name without a trailing number is its own stem and is never grouped by itself.
  2. Order proposals and their members by natural name order (T-351).
  3. Tests: sliced stems from several sources mixed in one import; `snd1`…`snd12` with `snd_fx`; a single numbered file stays ungrouped; joining an existing group; names that differ only in case; 1,300 names finish under 10 ms in a release build.
- **Invariants:** pure function of the passed names and groups.
- **Reject/failure behavior:** none (empty input gives an empty result).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'sound_group_proposal'`.
- **Verification (2026-10-04):** Application build passed; proposal filter: `100% tests passed out of 6`; full suite: `100% tests passed out of 1914`. Release benchmark (`-O2 -DNDEBUG`): 100 runs of 1,300 names, slowest 0.99831 ms (limit 10 ms). Source and tests checked with `clang-tidy -p build`, with no new cognitive-complexity violations.
- **Out of scope:** fuzzy or similarity-based grouping.

## T-355 Review proposed groups before an import commits

- **Status:** done (2026-10-04; retained import review and atomic group creation)
- **Outcome:** when an import (dock drop, Add, or the Sounds tab) gets at least one group proposal, the import dialog adds a Groups step. Each proposal shows a checkbox (accepted by default), an editable name, and its member count, which expands to the file list. Members can be moved to another proposal or to Ungrouped, and "Leave all ungrouped" rejects every proposal. Confirming imports the files and creates the accepted groups in one undo step, each group with its own contiguous ID run (T-351). Imports with no proposals skip the step.
- **Read first:** `src/ui/nuklear/resources/sound_import_state.h`, `src/sys/compose/actions/compose_sound_actions.cpp` (`plan_sound_import`), `include/wormedit/core/sound_group_proposal.h`, `include/wormedit/core/sound_groups.h`.
- **Files owned:** `src/ui/nuklear/sound_import_state.cpp/.h`, new `src/ui/nuklear/sound_import_dialog.cpp/.h`, `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/sys/compose_sound_actions.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/audio/audio_panel.cpp`, `src/ui/nuklear/audio/audio_state.h`, new `src/ui/nuklear/sound_import_runtime.cpp/.h`, `src/sys/workspace_projection.cpp`, `src/sys/app/main.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/app/application_frame_services.cpp`, `include/wormedit/core/sound_import.h`, `src/core/sound_import.cpp`, `tests/core/sound_import_tests.cpp`, `tests/ui/nuklear_event_harness.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/sound_import_state_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** `include/wormedit/core/sound_import.h`, `src/core/sound_import.cpp` and `tests/core/sound_import_tests.cpp` extend the existing atomic import command with groups, per-group ID runs and the supported capacity. `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/audio/audio_panel.cpp`, `src/ui/nuklear/audio/audio_state.h`, `src/sys/app/main.cpp`, `src/sys/compose/actions/compose_actions.cpp` and `src/sys/app/application_frame_services.cpp` route live Add/drop requests through one retained review before committing. New `src/ui/nuklear/sound_import_runtime.cpp/.h` own retained staging, picker callbacks and borrowed commit payloads; `src/sys/workspace_projection.cpp` publishes the target identity on all tabs. `tests/ui/nuklear_event_harness.cpp` and `tests/ui/nuklear_event_harness_tests.cpp` own borrowed-payload snapshots and actual UI workflow coverage. `tests/CMakeLists.txt` links the existing dispatcher into the frame test target for atomic reviewed-import integration checks. Shared frontend contract edits and event-harness edits belong to the coordinator; T-367 retains BGA state ownership.
- **Steps:**
  1. Add a `review_groups` phase to the import state, holding proposals, accept flags, edited names and member moves (pure, tested).
  2. Draw the step with one row per proposal and an Ungrouped section; the member list is virtualized.
  3. Commit: one core command that imports, creates groups and allocates runs together, or rejects as a whole. Raise the 1,000-file import bound to the chart's ID capacity.
  4. Tests: reject one proposal, rename one, move a file to Ungrouped, and "Leave all ungrouped"; then check the document groups, ID runs, and a single undo restoring the pre-import state.
- **Invariants:** nothing is written before Confirm; Cancel leaves the document untouched.
- **Reject/failure behavior:** a duplicate group name or over-capacity import keeps the dialog open with the reason.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.|application_commands'`; isolated Xvfb PNG of the Groups step for 3 sliced stems plus loose files.
- **Out of scope:** grouping sounds already in the song (T-356).

- **Verification (2026-10-04):** `cmake --build build` passed (final tail: `Built target wormedit`); `ctest --test-dir build --output-on-failure -R '^frontend-state\.|application_commands'`: `100% tests passed out of 198`; full suite: `100% tests passed out of 1935`. Group allocation also verifies the first fitting gap specified by T-351 while placing larger groups first. Reviewed-command tests verify names/member moves, existing-group joins, fragmented loose-file notices, atomic rejection and single undo. Pure state tests retain over-capacity batches for correction. The actual isolated window reviewed three numbered-file groups plus one loose file from both picker and drops; rejecting a group, editing a name, moving a member to Ungrouped, Confirm, Save and one undo were verified against native document data. Ctrl+A name replacement, Leave all ungrouped and Escape cancellation also passed. Captures: `/tmp/wormedit-import-bga-batch/groups-drop.png`, `name-replaced-real.png`, `leave-ungrouped.png`, `import-saved.png`, `import-undo.png`.

## T-356 Group and ungroup sounds by hand in the Sounds dock

- **Status:** done (2026-10-04)
- **Outcome:** with sounds selected in the dock, the right-click menu and toolbar offer "Group…" (new name, or pick an existing group), "Ungroup", and "Suggest groups…" (runs T-354 on the selected ungrouped sounds and opens the T-355 review without importing). Grouping offers "Keep IDs together" (on by default), which renumbers the group into one contiguous run (T-351) in the same undo step.
- **Read first:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/resources/keysound_list_state.h`, `include/wormedit/core/sound_groups.h`, `src/ui/nuklear/resources/sound_import_dialog.h`.
- **Files owned:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/resources/sound_import_dialog.cpp`, `src/sys/compose_sound_actions.cpp/.h`, `src/ui/frontend.h`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/core/sound_groups.h`, `src/core/sound_groups.cpp`, `tests/core/sound_groups_tests.cpp`, `tests/sys/manual_sound_group_command_tests.cpp`, `tests/ui/manual_sound_group_event_harness_tests.cpp`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/app/main.cpp`, `src/sys/workspace_projection.cpp/.h`, `src/sys/application_frame_projection.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/ui/nuklear/resources/sound_import_dialog.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, `tests/sys/frame_projection_arena_tests.cpp`, `src/sys/sound_list_projection.cpp/.h`, `tests/ui/nuklear_event_harness.cpp`, `tests/ui/inspector_resource_workflow_tests.cpp`.
- **Supporting scope:** Coordinator-approved supporting scope: extend the existing group command so grouping and contiguous ID allocation commit atomically; retain existing-resource review identity, wire scalar actions and group choices, and register focused regression tests. The dialog header exposes retained review rendering; Compose action routing and canvas modal guards prevent input leaking through the review. Arena projection regression verifies group choices and stable membership survive source mutation and frame moves. Sound list projection carries group IDs while it already visits resources, avoiding an extra quadratic scan. The harness snapshot owns transient group assignment payloads so workflow assertions remain valid after later frames. Existing Sounds/inspector workflow tests use actual text bounds after the added toolbar changes row positions. Coordinator owns shared frontend/projection contracts and build registration; timing-hover owns its separate frontend/main sections. Verification uses the actual discovered regression names; the old application_commands regex matched no CTest names.
- **Steps:**
  1. Wire Group/Ungroup through T-353, plus the optional renumber, as one command.
  2. Reuse the review step for "Suggest groups…" with existing sounds.
  3. Tests: group 5 scattered sounds with Keep IDs together and check one run plus a single undo; ungroup; suggest groups and reject all.
- **Invariants:** with "Keep IDs together" off, no BMS ID changes.
- **Reject/failure behavior:** as T-353.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'Manual grouping|Manual group|Batch grouping|Ungrouping selected|group without a contiguous'`; `ctest --test-dir build -L event-harness`.
- **Verification evidence:** Full application suite 2,025/2,025; command gate 7/7; event harness 97/97. Isolated toolbar and right-click group review, saved contiguous WAV IDs and single-undo restoration passed; artifacts `/tmp/wormedit-bgm-hover-groups-batch/group-final-*.png`. Suggestion rejection leaves the document unchanged. Affected new functions pass cognitive-complexity checks.
- **Out of scope:** drag-and-drop between groups.

## T-357 Show group sections and group/layer filters in the Sounds dock

- **Status:** done (2026-10-04; windowed sections and stable-ID filters)
- **Outcome:** the dock lists sounds under collapsible group headers (name, count), followed by an "Ungrouped" section, each in natural name order. A filter row narrows the list by group (one or several) and by default layer (any layer, a specific layer, or no layer), combined with the existing search. Collapsed state and filters last for the session, per song.
- **Read first:** `src/sys/sound_list_projection.h`, `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/resources/keysound_list_state.h`, `include/wormedit/core/sound_groups.h`.
- **Files owned:** `src/sys/sound_list_projection.cpp/.h`, `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/frontend.h`, `tests/sys/sound_list_projection_tests.cpp`, `tests/ui/keysound_list_state_tests.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/application_frame_projection.cpp`, `src/sys/workspace_projection.cpp/.h`, new `tests/ui/sound_filter_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `tests/sys/frame_projection_arena_tests.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** Frontend request retention and application/workspace projection wiring carry group/layer filters before windowing. Dedicated event-harness tests verify interactions; the coordinator-owned arena regression verifies windowed group labels, complete ID-only order and compact off-page selection survive source mutation and projection moves. Coordinator owns build registration and shared contract coordination. Verification uses discovered test names; the old filename-based regex did not select projection cases. Automatic mode uses group sections; slots mode keeps direct BMS-ID order. The filter layer picker uses a bounded stable-ID/name window independent of the first-64 mask palette; selected filter validity/name is resolved independently so off-window layers are retained and deleted layers are dropped. Select all includes listed expanded matching sounds; collapsed members are excluded. Advance ordering remains matching sound order independent of headers and viewport paging.
- **Steps:**
  1. Projection: group headers as rows in the visible window (collapsed groups contribute one row), with the filters applied before windowing.
  2. Filter row: a group multi-select popup, a layer popup (including "No layer"), and Clear.
  3. Selection across sections; "Select all" selects only the rows that are listed.
  4. Tests: window over collapsed and expanded groups; group+layer+search combination; 1,200 sounds stay windowed (only visible rows built).
- **Invariants:** filters and collapse never change the document.
- **Reject/failure behavior:** a filter naming a deleted group or layer drops that entry.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'keysound|sound list|sound properties|sound layer conflicts|BMS slot|^frontend-state\.'`; isolated Xvfb PNG with three groups, one collapsed.
- **Out of scope:** tag and usage filters; the BGA dock (can reuse this later).

- **Verification evidence (2026-10-04):** Application build and full suite passed 2,045/2,045, including all 104 input-harness cases. Projection tests cover filtering before paging, complete ID-only advance/selection order, collapse, large catalogs/selections and layer IDs beyond 64; arena tests cover source lifetime, moves and capacity failure. The real isolated window shows three groups with one collapsed, combined group/layer/search filters, slots mode and paged layer 65 filtering to the correct sound. Popup open/selection/Escape/outside/paging own their release without auditioning covered rows. Affected-source tidy reports no new complexity violations. Artifacts: `/tmp/wormedit-filters-bga-minimap-batch/`; suite log: `/tmp/wormedit-filters-full-suite-passed.log`.

## T-358 Restore optional BMS sound slots mode with direct ID assignment

- **Status:** done (2026-10-04)
- **Decision (user, 2026-10-04):** The sound picker must retain a slots mode that lets authors choose a BMS ID directly for backwards compatibility. Automatic natural-order assignment remains the default. This supersedes the earlier statement that a slot view was not planned.
- **Outcome:** the BMS Sounds dock switches between the automatic resource list and a virtualized slots view of all usable WAV IDs, including empty slots. Choosing a slot and assigning a file uses that exact ID; occupied slots require explicit replacement confirmation. Other formats keep their resource list.
- **Read first:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/sys/sound_list_projection.h`, `include/wormedit/core/resource_assignment.h`, `src/sys/compose/actions/compose_sound_actions.cpp`, `docs/references/bms-command-memo.md`.
- **Files owned:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `src/sys/sound_list_projection.cpp/.h`, `src/sys/compose_sound_actions.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/frontend.h`, `tests/core/resource_assignment_tests.cpp`, `tests/sys/sound_list_projection_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/workspace_projection.cpp/.h`, `src/sys/application_frame_projection.cpp`, `src/sys/app/main.cpp`.
- **Supporting scope (user approved, 2026-10-04):** `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/workspace_projection.cpp/.h`, `src/sys/application_frame_projection.cpp`, and `src/sys/app/main.cpp` are owned for retained slots state, projection requests, and selected-slot file-drop routing.
- **Steps:**
  1. Add a revision-checked assign-to-slot command for an exact supported WAV ID. An empty slot creates a resource; confirmed replacement relinks the occupied resource in one undo step, retaining its ID and note references.
  2. Project a bounded visible slot window with ID, empty/occupied state, file name, uses and missing state; reserve 00 and use supported base-36 capacity.
  3. Add the automatic/slots mode switch and selected-slot file assignment through the normal resource import flow; expose replacement confirmation and arm occupied slots for note placement.
  4. Test exact assignment into an empty slot, confirmed occupied replacement, rejection/cancellation, undo, unchanged unrelated IDs and note links, slot windowing and mode switching through the frontend harness.
- **Invariants:** automatic allocation stays the default; selecting or viewing slots never renumbers resources; direct assignment never silently invokes automatic ID allocation or Renumber by name.
- **Reject/failure behavior:** invalid/reserved IDs, stale revisions, invalid files and unconfirmed replacements leave the document unchanged.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|sound_list_projection'`; `ctest --test-dir build -L event-harness --output-on-failure`; isolated Xvfb assignment to a chosen empty slot and confirmed replacement on a BMS fixture.
- **Verification (2026-10-04):** Application build passed; core/projection filter passed 1,267/1,267; event harness passed 58/58; full application suite passed 1,892/1,892. Affected sources were checked with `clang-tidy -p build`, with no new cognitive-complexity violations. Isolated Xvfb saved an exact empty WAV Z9 assignment, preserved the document on occupied-slot cancellation, and saved a confirmed WAV 01 replacement retaining resource 31, every chart/note link, unrelated resources and existing audio assets.
- **Out of scope:** base-62 codec support; BMP/BGA slots mode; automatic group allocation (T-351).

## T-359 Import multiple sounds starting at the selected BMS WAV slot

- **Status:** done (2026-10-04; reviewed atomic exact-slot bulk import)
- **Request (user, 2026-10-04):** Bulk drag-and-drop onto the Sounds dock must also be supported in slots mode; T-358 currently rejects multiple files.
- **Outcome:** dropping multiple audio files onto the BMS Sounds dock, or choosing multiple files with Add, assigns them in natural filename order to successive WAV IDs starting at the selected slot, in one undo step; a review explicitly confirms any occupied-slot replacements.
- **Read first:** `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/sys/app/application_frame_services.cpp`, `src/sys/compose/actions/compose_sound_actions.cpp`, `include/wormedit/core/resource_assignment.h`, `include/wormedit/core/natural_order.h`.
- **Files owned:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `src/sys/compose_sound_actions.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/application_frame_services.cpp/.h`, `src/sys/app/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_sounds_panel.cpp`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/frontend.cpp`, `tests/core/resource_assignment_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, new `src/ui/nuklear/sound_slot_batch.cpp/.h`, new `src/sys/sound_drop_transaction.cpp/.h`, new `tests/sys/sound_drop_transaction_tests.cpp`, new `tests/ui/sound_slot_batch_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Supporting scope (coordinator approved, 2026-10-04):** New `src/ui/nuklear/sound_slot_batch.cpp/.h` isolate retained, scrollable exact-slot review. New `src/sys/sound_drop_transaction.cpp/.h` and `tests/sys/sound_drop_transaction_tests.cpp` retain one SDL transaction and its origin across frames before staging it. `CMakeLists.txt` and `tests/CMakeLists.txt` register these units. New `tests/ui/sound_slot_batch_event_harness_tests.cpp` checks the retained review payload, rejection and cancellation through actual frontend events. Shared frontend/state/application wiring belongs to the coordinator; core resource-assignment files are serialized with T-360.
- **Steps:**
  1. Add a revision-checked bulk exact-slot command that validates the complete batch, sorts by the existing natural filename comparator, and maps files to selected-slot + index without skipping occupied IDs or wrapping beyond ZZ. Reuse T-358's exact assignment semantics and commit the complete result once.
  2. Collect the entire SDL drop transaction through its completion event before importing; retain all paths and the starting project/chart/revision/slot snapshot. Enable multi-file Add in slots mode. Support up to the 1,295-slot base-36 capacity without silently truncating at the current 1,000-file limit; retain automatic mode's existing allocation policy.
  3. Stage a bounded, scrollable review of destination IDs, incoming filenames and existing occupants. Require explicit confirmation for replacements; Cancel, Escape, a stale revision or a mode/project/chart change discards the batch. Wire the confirmed batch through the standard action path, with no fallback to automatic allocation.
  4. Add core and event-harness coverage for natural ordering, non-01 starts, occupied/empty mixtures, cancellation, invalid files, stale targets, reserved/overflow IDs, 1,200-file batches, one-step undo/redo and drop events spread across frames. Verify picker and drop use the same batch path.
- **Invariants:** 00 stays reserved; occupied resources retain their stable resource IDs, metadata and note/BGM links; unrelated resources and IDs remain unchanged; all accepted files are assigned atomically. One-file slots assignment and default automatic mode continue to work.
- **Reject/failure behavior:** an unreadable/undecodable file, duplicate input path, oversized batch, insufficient range through ZZ, ambiguous occupied ID or unconfirmed replacement rejects the whole batch with a reason. No partial import, silent truncation, skipped occupied slots or automatic reassignment.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^frame-timing\.'`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^documentation.chart_editor_state$'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb bulk drop into empty and mixed occupied slots, saved-document comparison of exact IDs and preserved note links, cancellation and one-step undo.
- **Out of scope:** base-62 codec support; BMP/BGA slots; named-group proposals/review (T-354–T-356); renumbering existing sounds outside the destination range.

- **Verification (2026-10-04):** `cmake --build build` passed. Core/frame gates: 1,455/1,455; frontend/documentation gates: 223/223; event harness: 86/86; complete application suite: 1,978/1,978. Affected sources passed configured clang-tidy with no new cognitive-complexity violations. Core coverage includes natural filename ordering, 1,200-file allocation with one-step undo/redo, complete rejection and occupied-resource metadata preservation; the retained SDL transaction tests keep all paths across polling frames. Isolated picker/drop review, an 80-file exact allocation, mixed occupied/empty slots, cancellation and single undo passed. The final occupied-slot saved check retained the authored sample name, ID, provenance, roles and all note/BGM links while assigning WAV01 through WAV07. Artifacts: `/tmp/wormedit-slots-bga-batch/`; gate logs: `/tmp/wormedit-batch-*-gates.log`, `/tmp/wormedit-batch-harness-passed.log` and `/tmp/wormedit-batch-full-passed.log`.

## T-360 Add a BGA event editing lane to the Compose timeline

- **Status:** done (2026-10-04; selectable and editable Compose BGA lane)
- **Request (user, 2026-10-04):** Add a lane for BGA editing, in addition to the existing BGA resource dock.
- **Follow-up:** [T-373](editing-sounds-followups.md#t-373-move-the-bga-timeline-lane-between-timing-and-playable-notes) moves the lane between timing and playable notes so docks do not obstruct it.
- **Outcome:** a dedicated, labeled BGA timeline lane shows background image/video events at their chart positions and allows placing the dock's armed resource, selecting, moving, replacing and deleting events through normal Compose gestures.
- **Read first:** `include/wormedit/core/editor_scene.h`, `include/wormedit/core/interaction.h`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose/compose_canvas.cpp`, `src/ui/nuklear/compose/compose_bga_panel.cpp`.
- **Files owned:** `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/core/interaction_hit_test.cpp`, `src/core/interaction_selection.cpp`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `src/sys/compose_view.cpp/.h`, `src/sys/compose_input.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/workspace_projection.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp/.h`, `src/ui/nuklear/compose/compose_panel.cpp`, `src/ui/nuklear/compose/compose_toolbar.cpp`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/compose/compose_layers_panel.cpp`, `src/ui/nuklear/compose_bga_panel.cpp/.h`, `tests/core/editor_scene_tests.cpp`, `tests/core/interaction_tests.cpp`, `tests/core/resource_assignment_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/ui/nuklear/compose_object_input.cpp/.h`, `src/ui/nuklear/compose/compose_panel_internal.h`, `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/ui/nuklear/compose/compose_input_internal.h`, `src/ui/nuklear/compose/compose_keyboard_input.cpp`, new `tests/ui/bga_lane_event_harness_tests.cpp`, new `tests/sys/bga_lane_workflow_tests.cpp`, `tests/ui/bga_advance_workflow_tests.cpp`, `tests/ui/inspector_resource_workflow_tests.cpp`, `tests/CMakeLists.txt`, `src/sys/application_frame_projection.cpp/.h`, `src/sys/app/main.cpp`, `src/ui/nuklear/frontend.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** `src/ui/nuklear/compose_object_input.cpp/.h`, `src/ui/nuklear/compose/compose_panel_internal.h` and `src/ui/nuklear/compose/compose_input_panel.cpp` integrate lane geometry with the existing pointer capture and resource-placement paths; `src/ui/nuklear/compose/compose_input_internal.h` declares the shared BGA gesture handler. `src/ui/nuklear/compose/compose_keyboard_input.cpp` cancels retained BGA drag capture on Escape before a later release can commit. New `tests/ui/bga_lane_event_harness_tests.cpp` and `tests/sys/bga_lane_workflow_tests.cpp` isolate workflow coverage from T-359; `tests/CMakeLists.txt` registers them. `src/ui/nuklear/compose/compose_bga_panel.h` declares the dense-overlap event picker so all imported events remain selectable. `src/sys/application_frame_projection.cpp/.h`, `src/sys/app/main.cpp` and `src/ui/nuklear/frontend.cpp` pass stamped retained drag input to the core-backed preview projection and expose the actual canvas viewport to the event harness; this verifies mapped gestures without relying on text drawn through custom SDL geometry. `tests/ui/bga_advance_workflow_tests.cpp` updates the existing placement fixture to target the dedicated BGA lane, preserving advance-after-place coverage under the new lane boundary. `tests/ui/inspector_resource_workflow_tests.cpp` and existing harness fixtures are included to keep compact dock access and toolbar/menu workflows verified after adding the BGA lane control. `src/ui/nuklear/compose/compose_layers_panel.cpp` is included to prevent a disabled layer-menu item from replaying its release onto the covered layer row; the corrected popup fixture exposed this shared ownership defect. Frontend popup ownership also blocks canvas input while BGA renumber confirmation owns the gesture. The coordinator serializes shared frontend/state contracts, application dispatch and resource-assignment ownership across this batch. T-359 has released `include/wormedit/core/resource_assignment.h` and `src/core/resource_assignment.cpp` to T-360 for identity-preserving BGA move/replace commands; T-359 retains its test-file ownership.
- **Steps:**
  1. Apply the stale-plan check to background BGA event projection, hit testing and resource-pointer placement; reuse existing commands and gesture paths rather than introducing a second editing system.
  2. Project a dedicated BGA background lane with event IDs, resource names/BMS BMP IDs, selection and missing-resource state. Share the field's beat mapping, snap, zoom and scroll direction, including virtual BMS measures and charts without playable lanes.
  3. Render the lane header and visible event markers with a lane visibility control; keep the lane distinct from the resource dock and playable/BGM lanes. Overlapping imported events must remain identifiable rather than being silently hidden or merged.
  4. Route lane hit tests through normal selection/move/delete and the existing armed BGA resource place/replace commands. Placement affects only the BGA lane; preserve event identity when moving or replacing, and cancel previews on Escape without committing.
  5. Add scene, command and event-harness tests for placement, hit selection, snapped movement, replacement, deletion, overlap ambiguity, missing resources, undo/redo, upscroll/downscroll and lane-less BMS charts. Check imported BGA events and saved/exported positions in the real isolated window.
- **Invariants:** drawing, visibility and selection do not edit the document; note/BGM events and resource/BMS identities remain unchanged by BGA lane edits; each completed gesture is one undo step. Existing poor/layer/video/storyboard event kinds remain lossless.
- **Reject/failure behavior:** stale targets, incompatible resources and ambiguous replacements leave the document unchanged and report the reason; cancelled gestures restore their original positions.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^frame-timing\.'`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^documentation.chart_editor_state$'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb BMS fixture showing imported and newly edited BGA markers, saved-position/export comparison and undo verification.
- **Out of scope:** BGA playback preview; video decoding/thumbnails; new visual event types; separate poor/layer/storyboard editing lanes; direct BMP slots and ID renumbering.

- **Verification (2026-10-04):** `cmake --build build` passed. Core/frame gates: 1,455/1,455; frontend/documentation gates: 223/223; event harness: 86/86; complete application suite: 1,978/1,978. Configured affected-source clang-tidy found no new cognitive-complexity violations. Tests cover typed move/replace/delete and undo/redo, authored/virtual snap-on/off drag previews in both scroll directions, lane-less BMS geometry, dock-mapped hit testing, all 50 dense-picker rows and popup dismissal without click-through. Isolated real-window checks verified visible-marker hit alignment, move and Escape cancellation, selected deletion/undo, placement/replacement with stable ID/kind/provenance, lane visibility and unchanged playable notes. A 64-event overlap picker reached and moved its final event; the other 63 events and all six notes stayed unchanged, and one undo restored every position. Saved existing/new positions exported as `#00004:00010300` with unchanged BMP definitions and validated reimport; the export used the existing warning policy for authored meter grouping. Cached BGA geometry now shifts with the painted region. Popup closure occurs after leaving its nested list; disabled layer-menu releases no longer select covered rows. The isolated final smoke command passed and its PNG was inspected. Artifacts: `/tmp/wormedit-slots-bga-batch/`; full log: `/tmp/wormedit-batch-full-passed.log`.
