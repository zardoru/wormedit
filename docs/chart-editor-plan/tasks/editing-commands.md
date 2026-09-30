# Task cards: editing commands

[Back to the task-card index](../tasks.md).

Stage 3 editing leftovers from [authoring/export](../stages/authoring-export.md).
The command registry seeds six commands that `src/sys/main.cpp` still hard-codes
as disabled (search `set("…", false)` near the command-state update). The first
cards enable them one at a time. Apply the stale-plan check (rule 9) first.

## T-50 Enable Insert BPM and Insert Scroll Velocity at the cursor

- **Status:** ready
- **Outcome:** `timing.insert_bpm` and `timing.insert_sv` are enabled when a chart is open and start the existing timing pseudo-note at the exact Compose cursor row, seeded with the effective value there.
- **Read first:** `include/wormedit/core/timing_lane_controller.h` (`begin_timing_lane_pseudonote`), `src/sys/main.cpp` (`set("timing.insert_bpm", false)`), `src/sys/application_commands.cpp`, `src/sys/timing_edit_runtime.cpp`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `src/sys/timing_edit_runtime.cpp/.h`, `tests/sys/application_commands_tests.cpp`.
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

- **Status:** ready
- **Outcome:** `document.new_chart` adds an empty difficulty (same lane profile and resolution as the active chart) to the open song as one undoable command and selects it.
- **Read first:** `include/wormedit/core/command.h`, `include/wormedit/core/document.h` (`chart_document_t`), `src/sys/main.cpp` (`set("document.new_chart", false)`), `src/sys/application_command_dispatch.cpp`.
- **Files owned:** new `include/wormedit/core/chart_editor.h`, new `src/core/chart_editor.cpp`, new `tests/core/chart_editor_tests.cpp`, `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Core: `apply_add_chart_command(history, expected_revision, template_chart_id)` creates a chart with a fresh stable ID, copied lane layout/resolution/offset, one measure, and no notes.
  2. Tests: new chart validates; undo removes it; stale revision rejects.
  3. Dispatch the command and select the new chart in the workspace.
- **Invariants:** existing charts and output groups unchanged; IDs never reused.
- **Reject/failure behavior:** stale revision or missing template rejects without a history entry.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** deleting or reordering difficulties.

## T-53 Enable New Project from the command registry

- **Status:** ready
- **Outcome:** `document.new_project` opens the same new-project flow as the landing page (`create_new_project_session` in `src/sys/main.cpp`).
- **Read first:** `src/sys/main.cpp` (`create_new_project_session`, `set("document.new_project", false)`), `src/sys/application_command_dispatch.cpp`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`.
- **Steps:**
  1. When the current document is dirty, route through the existing unsaved-changes prompt if one exists; if none exists, STOP and ask whether to add one.
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

- **Status:** ready
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

- **Status:** ready
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

- **Status:** blocked-on T-61
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

- **Status:** blocked-on-user
- **Outcome:** answers recorded for open questions 8 (5K/6K arrows), 10 (multi-layer notes), 11 (10K/14K naming), and 14 (snap colors); each "yes" answer gets its own follow-up card in this file.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:**
  1. STOP: ask the user the four questions verbatim.
  2. Record answers; add follow-up cards with the next free IDs.
- **Invariants:** none.
- **Reject/failure behavior:** unanswered questions stay open.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.
