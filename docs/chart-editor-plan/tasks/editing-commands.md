# Task cards: editing commands

[Back to the task-card index](../tasks.md).

Stage 3 editing leftovers from [authoring/export](../stages/authoring-export.md).
The command registry seeds six commands that `src/sys/main.cpp` still hard-codes
as disabled (search `set("…", false)` near the command-state update). The first
cards enable them one at a time. Apply the stale-plan check (rule 9) first.

## T-50 Enable Insert BPM and Insert Scroll Velocity at the cursor

- **Status:** done
- **Stale-plan audit (2026-10-02):** Mouse insertion, `begin_timing_lane_pseudonote`, and the inline value editor already exist. The two registry commands remained disabled, lacked dispatcher handlers, and targeted the retired Timing tab. This cut reuses the existing staging and confirmation paths; the user approved retargeting to Compose and connecting command-created stages to the existing inline editor.
- **Implementation notes (2026-10-02):** The existing timing controller stages exact effective BPM/SV at the Compose cursor; the existing inline editor reviews it and confirms or cancels. The registry now targets Compose, and command states honor the timing lock. Dispatcher tests live in the application frame target; pure command tests retain their core target. Isolated custom-shortcut checks showed BPM 175 and SV 11/4 before mutation; Escape canceled cleanly, typing 7/2 inserted one SV and one Undo restored the clean document. A separate BPM edit saved one event at measure 2 row 240 with exact value 375/2, revision +1, and unchanged notes/SV events (`/tmp/wormedit-t50/saved-document-check.txt`). No default shortcuts were added. Final build and full tests passed 1,568/1,568 (`/tmp/wormedit-final-all-tests2.log`); affected-file clang-tidy reported no new complexity warnings.
- **Outcome:** `timing.insert_bpm` and `timing.insert_sv` are enabled when a chart is open and start the existing timing pseudo-note at the exact Compose cursor row, seeded with the effective value there.
- **Read first:** `include/wormedit/core/timing_lane_controller.h` (`begin_timing_lane_pseudonote`), `src/sys/main.cpp` (`set("timing.insert_bpm", false)`), `src/sys/application_commands.cpp`, `src/sys/timing_edit_runtime.cpp`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `src/sys/timing_edit_runtime.cpp/.h`, `tests/sys/application_commands_tests.cpp`; user-approved scope extensions (2026-10-02): `src/core/command_registry.cpp`, `tests/core/command_registry_tests.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. In the dispatcher, handle `timing_insert_bpm` / `timing_insert_sv` by calling the same path that double-clicking an empty Timing-lane row uses, with the cursor row and `timing_lane_initial_value_source_t` "effective value".
  2. Replace the two `false` states with "chart open and timing not locked".
  3. Add a test that invoking the command yields an open pseudo-note preview and no history change until Apply.
- **Invariants:** no document mutation until the user confirms; timing lock still rejects.
- **Reject/failure behavior:** locked timing or no chart leaves the command disabled.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^(core|frontend-state)\.'` and the `application_commands` tests.
- **Out of scope:** the Timing Settings panel (T-51).

## T-51 Build the modeless Timing Settings panel

- **Status:** ready
- **Outcome:** `timing.open_settings` toggles a small modeless window, reachable from Timing and Compose, holding metronome enable/volume/count-in and the per-lane timing visibility toggles.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (Timing Settings paragraph), `src/ui/nuklear/compose_inspector.cpp` (timing-lane visibility widgets), `src/sys/chart_preview_state.h` (metronome state), `src/sys/main.cpp` (`timing.open_settings`).
- **Files owned:** new `src/ui/nuklear/timing_settings_panel.cpp/.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/sys/main.cpp`, `src/sys/chart_preview_state.cpp/.h`, `CMakeLists.txt`.
- **Steps:**
  1. Create the panel as an `nk_begin` window with a close button; its visibility is frontend state, not document state.
  2. Move (do not copy) the timing-lane visibility toggles from the inspector into the panel; leave a one-line "Timing Settings…" button in their place.
  3. Add metronome enable, volume (0–100 %), and count-in measures (0–4) bound to existing preview state; if a field has no backing state, STOP and report it.
  4. Enable `timing.open_settings` and make it toggle the panel.
- **Invariants:** opening the panel does not change tool, selection, cursor, or shortcut focus.
- **Reject/failure behavior:** none; purely presentational plus existing state.
- **Verify:** standard build/test; smoke PNGs of `compose` with the panel open and closed.
- **Out of scope:** accent/subdivision patterns beyond what the preview already supports.

## T-52 Enable New Chart

- **Status:** done (active-template New selects one empty difficulty; core 780/780 and isolated Project/shortcut saved-document proof passed)
- **Outcome:** `document.new_chart` adds an empty difficulty (same lane profile and resolution as the active chart) to the open song as one undoable command and selects it.
- **Read first:** `include/wormedit/core/command.h`, `include/wormedit/core/document.h` (`chart_document_t`), `src/sys/main.cpp` (`set("document.new_chart", false)`), `src/sys/application_command_dispatch.cpp`.
- **Files owned:** new `include/wormedit/core/chart_editor.h`, new `src/core/chart_editor.cpp`, new `tests/core/chart_editor_tests.cpp`, `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Core: `apply_add_chart_command(history, expected_revision, template_chart_id)` creates a chart with a fresh stable ID, copied lane layout/resolution/offset, one measure, and no notes.
  2. Tests: new chart validates; undo removes it; stale revision rejects.
  3. Dispatch the command and select the new chart in the workspace.
- **Invariants:** existing charts and output groups unchanged; IDs never reused.
- **Reject/failure behavior:** stale revision or missing template rejects without a history entry.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'` passed 780/780 in 12.05 seconds (`/tmp/wormedit-t260/template-core-prefix.log`). Full suite 1016/1016 passed; configured affected-file complexity checks passed. Isolated GUI Project New and Ctrl+Shift+N selected empty four-lane charts, and native saving confirmed the active profile, resolution 3840, and exact offset -7/20 (`/tmp/wormedit-t260/template-exact-values.log`).
- **Out of scope:** deleting or reordering difficulties.

## T-53 Enable New Project from the command registry

- **Status:** ready
- **Outcome:** `document.new_project` opens the same new-project flow as the landing page (`create_new_project_session` in `src/sys/main.cpp`).
- **Read first:** `src/sys/main.cpp` (`create_new_project_session`, `set("document.new_project", false)`), `src/sys/application_command_dispatch.cpp`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`.
- **Steps:**
  1. When the current document is dirty, show a Save / Discard / Cancel dialog (user decision 2026-09-30); add a shared `confirm_unsaved_changes` helper (a normal Nuklear window, not a popup, see T-264) that New project, Open, Close project, and Quit all use.
  2. Enable the command and invoke the landing-page flow.
- **Invariants:** the current document is never discarded without an explicit choice.
- **Reject/failure behavior:** cancel keeps the current session untouched.
- **Verify:** standard build/test; manual run.
- **Out of scope:** templates.

## T-54 Enable hitsound/keysound painting

- **Status:** ready
- **Outcome:** `audio.paint_hitsound` arms a paint mode: dragging across notes assigns the armed catalog resource to each note touched, committed as one undo step on release.
- **Read first:** `docs/chart-editor-plan/editing/interactions.md` (hitsound painting paragraph), `include/wormedit/core/resource_assignment.h` (`apply_resource_assignment`), `src/ui/nuklear/compose_input_panel.cpp` (semantic-layer painting capture), `src/sys/main.cpp` (`audio.paint_hitsound`).
- **Files owned:** `src/sys/compose_input.cpp`, `src/sys/compose_actions.cpp`, `src/sys/main.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/frontend.h`, `tests/core/resource_assignment_tests.cpp`.
- **Steps:**
  1. Copy the structure of semantic-layer paint capture: collect touched note IDs during the drag (bounded, deduplicated).
  2. On release call `apply_resource_assignment` once with the collected set.
  3. Enable the command only when a Sample resource is armed; show the mode with the `audio-lines` icon.
- **Invariants:** one history entry per drag; Escape/focus loss cancels with no change.
- **Reject/failure behavior:** stale revision or a missing resource rejects the whole drag.
- **Verify:** standard build/test; manual paint over 10 notes then one undo restores all.
- **Out of scope:** BGM/BGA painting.

## T-55 Move mixed note and BGM selections together

- **Status:** ready
- **Outcome:** dragging a selection that contains playable notes and BGM events moves both by the same snapped row delta in one command.
- **Read first:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp` (note-move and BGM-move proposals), `src/sys/compose_input.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose_input.cpp`, `tests/core/interaction_tests.cpp`.
- **Steps:**
  1. Build one proposal that validates the note part and the BGM part with their existing validators, then publishes both in one command.
  2. Only vertical (row) movement for BGM; lanes of BGM events stay fixed.
  3. Tests: success, collision in either part rejects both, undo restores both.
- **Invariants:** event IDs preserved; linked BMSON markers keep their sound-channel path (reject if the path cannot move them atomically).
- **Reject/failure behavior:** any invalid part rejects the whole move.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** timing-event movement.

## T-56 Add a Quantize Selection command

- **Status:** done (2026-10-02; `src/sys/compose_quantize_preview.cpp/.h`, `tests/sys/compose_quantize_preview_tests.cpp`)
- **Implementation notes (2026-10-02, Claude):** the dry run is `preview_quantize_selection` (moved, unchanged, blocked counts and the first diagnostic), cached per chart, revision, selection and snap in `compose_scene_cache_t::quantize_preview`. The inspector shows the counts and a **Quantize to snap** button, disabled on any collision or when nothing would move; it dispatches the existing `WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE`. The area menu entry is unchanged.
- **Implementation notes (2026-10-01):** core `apply_quantize_command` and `quantize_chart_exact` already exist and are tested, so only the dry-run projection and inspector UI remain.
- **Outcome:** the Compose inspector offers **Quantize selection** to the active snap with a preview count of moved/colliding notes, then commits through `apply_quantize_command`.
- **Read first:** `include/wormedit/core/quantize.h`, `include/wormedit/core/command.h` (`apply_quantize_command`), `docs/chart-editor-plan/editing/interactions.md` (quantization paragraph), `src/ui/nuklear/compose_inspector.cpp`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/quantize_tests.cpp`.
- **Steps:**
  1. Project a dry-run `quantize_chart_exact` result (moved, collided, unchanged counts) for the current selection into the view.
  2. Add the button and the counts; Apply calls `apply_quantize_command`.
- **Invariants:** tie policy "earlier row" by default, per the interactions plan.
- **Reject/failure behavior:** any collision disables Apply and shows the count.
- **Verify:** standard build/test.
- **Out of scope:** reusable presets and reports (T-143).

## T-57 Show the lane-key binding legend

- **Status:** ready
- **Outcome:** a toggleable legend above the Compose lanes shows the physical key bound to each lane for the active input profile.
- **Read first:** `include/wormedit/core/lane_profiles.h` (input profiles), `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`.
- **Files owned:** `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_toolbar.cpp`.
- **Steps:**
  1. Project one short key label per visible lane into the view.
  2. Draw labels in the lane header row; add a toolbar toggle.
- **Invariants:** lane geometry unchanged.
- **Reject/failure behavior:** unbound lanes show an empty label.
- **Verify:** standard build/test; smoke PNG with the legend on.
- **Out of scope:** remapping (T-59), 48K focus (T-58).

## T-58 Add 48K stage focus

- **Status:** blocked-on T-57
- **Outcome:** on BMSON 48K charts, a stage selector limits lane-key input to one 24-key bank and highlights it.
- **Read first:** `include/wormedit/core/lane_profiles.h` (`make_bmson_generic_48k_profile`), `src/sys/compose_input.cpp`, `docs/chart-editor-plan/editing/interactions.md`.
- **Files owned:** `src/sys/compose_input.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/core/interaction_tests.cpp`.
- **Steps:**
  1. Add a focused-stage index to Compose state; lane-key events map through it.
  2. Toolbar buttons switch stages; the legend (T-57) follows.
- **Invariants:** other profiles unaffected.
- **Reject/failure behavior:** stage index clamps when the profile changes.
- **Verify:** standard build/test.
- **Out of scope:** 24K.

## T-59 Remap shortcuts in a preferences window and save them

- **Status:** ready
- **Outcome:** a Shortcuts window lists every registry command with its chord; the user can capture a new chord, see conflicts, and the profile is saved with `save_shortcut_profile_atomic`.
- **Read first:** `include/wormedit/core/command_registry.h` (`remap_command`, `diagnose_shortcut_conflicts`), `include/wormedit/core/shortcut_profile_storage.h`, `src/core/shortcut_profile_startup.cpp`.
- **Files owned:** new `src/ui/nuklear/shortcuts_panel.cpp/.h`, `src/ui/frontend.h`, `src/sys/main.cpp`, `CMakeLists.txt`, `tests/core/command_registry_tests.cpp`.
- **Steps:**
  1. Project the registry (id, label, chord text, conflict flag) into a bounded view.
  2. "Change" captures the next physical key batch as the chord; call `remap_command`.
  3. Save on every successful remap; show load/save errors in the status bar.
- **Invariants:** remapping never changes command behavior; a conflict is shown, not silently resolved.
- **Reject/failure behavior:** failed save keeps the in-memory mapping and reports the error.
- **Verify:** standard build/test; restart the app and confirm the remap persists.
- **Out of scope:** command palette (T-60).

## T-60 Add a searchable command menu

- **Status:** ready
- **Outcome:** a primary-modifier+Shift+P popup lists enabled commands filtered by typed text; Enter invokes the selected one through `invoke_command`.
- **Read first:** `include/wormedit/core/command_registry.h` (`invoke_command`), `src/sys/application_command_dispatch.cpp`, `src/ui/nuklear/frontend.cpp`.
- **Files owned:** new `src/ui/nuklear/command_menu.cpp/.h`, `src/core/command_registry.cpp` (default chord only), `src/ui/frontend.h`, `src/sys/main.cpp`, `CMakeLists.txt`.
- **Steps:**
  1. Register `workspace.command_menu` with the default chord.
  2. Popup with a text field and up to 20 filtered rows (case-insensitive substring on label).
  3. Enter dispatches through the same path as shortcuts.
- **Invariants:** disabled commands are shown greyed and cannot be invoked.
- **Reject/failure behavior:** Escape closes without invoking.
- **Verify:** standard build/test.
- **Out of scope:** fuzzy ranking.

## T-61 Copy and cut chart objects to an internal clipboard

- **Status:** done (exact rational note/BGM snippets, atomic cut/undo, Primary shortcuts; 847/847 core tests and actual isolated Copy/Cut/Undo passed)
- **Outcome:** copy/cut store the selected notes and BGM events as a row-relative, resolution-independent snippet; cut also deletes them in one command.
- **Read first:** `docs/chart-editor-plan/editing/interactions.md`, `include/wormedit/core/document.h`, `include/wormedit/core/interaction.h`.
- **Files owned:** new `include/wormedit/core/chart_clipboard.h`, new `src/core/chart_clipboard.cpp`, new `tests/core/chart_clipboard_tests.cpp`, `src/core/command_registry.cpp`, `src/sys/application_command_dispatch.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Snippet type: exact rational offsets from the earliest selected row, lane index, note kind/duration, resource ID, label, layer.
  2. Register `edit.copy` / `edit.cut` with primary+C / primary+X.
  3. Tests: copy of a hold across measures round-trips offsets exactly.
- **Invariants:** copy never mutates the document.
- **Reject/failure behavior:** empty selection disables both commands.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** OS clipboard text, paste (T-62), BMSE/iBMSC (T-63).

## T-62 Paste the internal clipboard with a ghost preview

- **Status:** done (exact owned ghost geometry, keyboard/click atomic commit, red collisions and Escape; full 1180/1180 plus isolated saved-object/undo gates passed)
- **Outcome:** `edit.paste` shows the snippet as ghosts at the cursor; click or Enter commits, Escape cancels; collisions show red ghosts and block commit.
- **Read first:** `include/wormedit/core/chart_clipboard.h`, `src/core/interaction.cpp` (move-proposal ghosts), `src/sys/compose_input.cpp`.
- **Files owned:** `src/core/chart_clipboard.cpp`, `include/wormedit/core/chart_clipboard.h`, `src/sys/compose_input.cpp`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `tests/core/chart_clipboard_tests.cpp`.
- **Steps:**
  1. Core proposal: place snippet at a row, remap resolution exactly, validate lanes/collisions.
  2. Frontend: ghost preview following the snapped cursor; commit as one command.
- **Invariants:** exact positions; incompatible resolution rejects rather than rounds.
- **Reject/failure behavior:** missing lanes in the target profile reject with a count.
- **Verify:** standard build/test.
- **Out of scope:** cross-chart resource remapping dialogs.

## T-63 Decode BMSE and iBMSC clipboard text

- **Status:** blocked-on T-61
- **Outcome:** pasting BMSE 3.0, BMSE legacy, iBMSC, or iBMSC xNT clipboard text produces the same snippet type as T-61, with exact rational rows.
- **Read first:** `docs/references/bms-editor-clipboard.md` (full grammar, cross-editor table, hazards), `tests/fixtures/bms/clipboard/README.md`, `include/wormedit/core/chart_clipboard.h`.
- **Files owned:** new `src/formats/bms_clipboard.cpp`, new `include/wormedit/formats/bms_clipboard.h`, new `tests/formats/bms_clipboard_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Tests first: each of the four `.clip` fixtures decodes to the ten-object scenario in the fixture README (rows exact, 192/7 recovered exactly from iBMSC's 15-digit text, legacy row 27 kept as 27).
  2. Detect the grammar by exact header line; decode records per the reference. Map BMSE channels and iBMSC columns to semantic lanes of the target profile.
  3. Snap decimal positions to the nearest rational with denominator at most 192 × 64 per measure; diagnose anything farther than 1e-9 measure.
  4. Negative tests from the reference's required list, including a comma-decimal payload and a one-record iBMSC payload.
- **Invariants:** bounded record count and bytes; unknown channels/columns produce diagnostics, never guesses.
- **Reject/failure behavior:** malformed text rejects the whole paste without touching the document.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms_clipboard'`.
- **Out of scope:** encoding wormedit selections into BMSE/iBMSC text (add a card after this one lands).

## T-64 Decide the remaining note-presentation questions

- **Status:** done (user decision 2026-09-30: question 8 → ship Pump 5K and Solo 6K; question 10 → one semantic layer per note (no change); question 11 → beatmania-style DP5/DP7 with scratches outside, plus a separate scratchless osu!mania-style 10K; question 14 → StepMania/ITG snap palette. Follow-ups: T-284, T-285, T-286.)
- **Outcome:** answers recorded in `docs/chart-editor-plan/decisions/open-questions.md`.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** recorded; follow-ups added.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-284 Add Pump 5K and Solo 6K arrow profiles

- **Status:** ready
- **Implementation notes (2026-10-01):** steps 2 and 3 are done and step 1 is half done. `lane_glyph_t` gained `arrow_up_left`, `arrow_up_right`, `arrow_down_left`, `arrow_down_right` and `arrow_center` appended after `arrow_right` (stored values 0-5 are unchanged; the native-project enum bound moved to `arrow_center`). `make_stepmania_pump_single_profile()`, `make_stepmania_dance_solo_profile()` and `stepmania_lane_specs()` live in `lane_profiles`; the importer takes lane names and glyphs from the same table, so `dance-solo` diagonals are now `arrow_up_left`/`arrow_up_right` instead of `bar`. Step-type mapping already worked through the `stepmania:<type>` profile id and the 5/6-column fallbacks. Paired fixtures `stepmania/pump-single.{sm,ssc}` and `dance-solo.{sm,ssc}` have import and re-export tests. **Remaining:** the arrow polygons in `src/ui/nuklear/arrow_glyph.cpp`, the `glyph >= 2 && glyph <= 5` range checks in `compose_canvas.cpp`, `compose_panel.cpp` and `player_preview.cpp`, the arrow switch in `compose_field_downscroll` (`src/sys/compose_view.cpp`, so Pump charts scroll up), and the smoke PNG. Until then the new glyphs draw as bars.
- **Outcome:** built-in lane profiles for pump-single (down-left, up-left, center, up-right, down-right) and dance-solo (left, up-left, down, up, up-right, right) with arrow glyphs, number-row default bindings (T-242), and SM/SSC step-type mapping (`pump-single`, `dance-solo`) (user decision 2026-09-30, question 8).
- **Read first:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `include/wormedit/core/document.h` (`lane_glyph_t`), `src/formats/stepmania.cpp` (step types), `src/ui/nuklear/arrow_glyph.h`.
- **Files owned:** `include/wormedit/core/document.h` (diagonal and center glyph values), `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `tests/core/lane_profile_tests.cpp`, `src/formats/stepmania.cpp`, `tests/formats/stepmania_tests.cpp`, `src/ui/nuklear/arrow_glyph.cpp/.h`, new fixtures under `tests/fixtures/stepmania/`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Steps:** 1. Glyph values for diagonals and center; polygons in `arrow_glyph`. 2. Profiles and step-type mapping. 3. Paired SM/SSC fixtures and round-trip tests.
- **Invariants:** existing 4K/8K behavior unchanged.
- **Reject/failure behavior:** unknown step types keep the current custom-map behavior.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stepmania|lane_profile'`; smoke PNG.
- **Out of scope:** pump-double, rolls (deferred T-170).

## T-285 Name BMS double-play DP5/DP7 and add a scratchless 10K

- **Status:** done (2026-10-01; `src/core/lane_profiles.cpp`, `include/wormedit/core/lane_profiles.h`, tests in `tests/core/lane_profile_tests.cpp` and `tests/core/bms_profile_detection_tests.cpp`, fixture `bms/profiles/bms-10k-scratchless.bms`)
- **Implementation notes:** display names are now "BMS DP5 / 10K (...)" and "BME DP7 / 14K (...)"; profile ids are unchanged. The default placement was already outside (1P scratch far left, 2P scratch far right, with the free-zone or pedal lane after it), and a test now pins it. `make_bms_10k_scratchless_profile()` (id `bms-10k-scratchless`, channels 11-15 and 21-25, lane ids equal to the other 10K profiles) is registered as a `special` profile, so detection offers it as a candidate when no scratch channel is used but never selects it without an explicit choice. The profile registry test, the fixture-backed channel-map test (`bms_tests.cpp`) and the profile fixture README were extended for the sixteenth profile.
- **Outcome:** BMS double-play profiles are named DP5 and DP7 in the UI with beatmania-style layout (1P scratch far left, 2P scratch far right), and a separate scratchless 10K profile (osu!mania-style, ten playable lanes, no scratch) exists for charts that do not use scratches (user decision 2026-09-30, question 11).
- **Read first:** `src/core/lane_profiles.cpp` (`make_bme_7k_profile`, 10K/14K profiles), `include/wormedit/core/bms_profile_detection.h`, `docs/references/bms-command-memo.md`.
- **Files owned:** `src/core/lane_profiles.cpp`, `include/wormedit/core/lane_profiles.h`, `tests/core/lane_profile_tests.cpp`, `src/core/bms_profile_detection.cpp`, `tests/core/bms_profile_detection_tests.cpp`.
- **Steps:** 1. Rename display names (ids unchanged for compatibility). 2. Default scratch placement outside. 3. New scratchless 10K profile; detection chooses it only when no scratch channels are used and the user confirms. 4. Tests.
- **Invariants:** profile ids stored in existing projects keep working.
- **Reject/failure behavior:** ambiguous detection still asks the user.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'lane_profile|bms_profile'`.
- **Out of scope:** PMS naming.

## T-286 Use the StepMania/ITG snap color palette

- **Status:** ready
- **Implementation notes (2026-10-02, Claude):** classification now includes 48 (cyan) and 64 (green) and the tests cover them (arrow tests compiled standalone against `arrow_glyph.cpp`, 162 assertions pass). The palette is still a constant table in `arrow_glyph.cpp`; moving it into `assets/themes/compose-field.json` and `theme.cpp` remains, so this card stays open for that step.
- **Outcome:** arrow notes use 4th red, 8th blue, 12th purple, 16th yellow, 24th pink, 32nd orange, 48th cyan, 64th green, other grey (user decision 2026-09-30, question 14); the table lives in the compose theme file.
- **Read first:** `src/ui/nuklear/arrow_glyph.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_view.cpp` (`snap_class` projection), `assets/themes/compose-field.json`.
- **Files owned:** `src/sys/compose_view.cpp`, `src/ui/frontend.h` (snap class values 48 and 64), `assets/themes/compose-field.json`, `src/ui/nuklear/theme.cpp/.h`, `src/ui/nuklear/compose_canvas.cpp`, `tests/ui/arrow_glyph_tests.cpp`.
- **Steps:** 1. Extend snap classification with 48 and 64. 2. Move the palette into the theme file. 3. Test classification of exact positions.
- **Invariants:** classification uses exact rational positions.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; smoke PNG of an SM chart with mixed snaps.
- **Out of scope:** BMS tile colors (T-270).

## T-304 Connect the internal chart clipboard to Compose commands

- **Status:** done (typed Primary+C/X/V and Edit menu; configured tidy, full 1180/1180, standalone cut syntax and isolated exact paste/undo/cancel gates passed)
- **Outcome:** Primary+C/X invoke the exact T-61 clipboard through typed application commands; the retained snippet survives selection changes, failures preserve it, and the T-62 paste preview is connected to Primary+V and the Edit menu.
- **Read first:** `include/wormedit/core/chart_clipboard.h`, `src/sys/application_command_dispatch.cpp`, `src/sys/compose_view.h`, `src/ui/nuklear/frontend.cpp`.
- **Files owned:** `src/core/command_registry.cpp` (Primary+V registration), `src/sys/application_commands.h/.cpp`, `src/sys/application_command_dispatch.cpp/.h`, `src/sys/compose_view.h/.cpp`, `src/sys/compose_input.cpp/.h`, `src/sys/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Add typed copy/cut commands and one retained internal clipboard, calling the T-61 core implementation; verify rejection and one-command cut/undo. 2. After T-62's core proposal exists, wire typed paste, ghost projection and commit/cancel input. 3. Verify actual copy/cut/paste and undo in isolated Xvfb using saved exact chart objects.
- **Invariants:** text/IME/modal owners retain standard text clipboard shortcuts; chart cut and paste are atomic; no partial copy silently loses selected data; resource references are never guessed across projects.
- **Reject/failure behavior:** empty/unsupported/stale selections preserve the prior clipboard and document; invalid paste retains a diagnostic preview and cannot commit.
- **Verify:** standard build/test; configured clang-tidy; isolated real-window saved-document copy/cut/paste and undo checks.
- **Out of scope:** OS text clipboard interoperability (T-63), resource remapping dialogs.
