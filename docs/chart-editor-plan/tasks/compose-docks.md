# Compose docks, measure gestures, BMSON stems and macOS chrome (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-05. Settled choices: Alt+drag a bar line to change a
measure's length, with snapping. On BMSON stem lanes, a click adds a continue point,
Shift+click adds a restart point, and clicking an existing point toggles it. On
macOS, the native title bar and the global menu bar replace the in-window menu and
window buttons.

## T-397 Measure list dock with mass-set meter

- **Status:** done (2026-10-06; Shift+Alt+drag moves only the bar line (`move_bar_line_action_t`, `WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE`): the measure before it and the one after trade length, every other line and object row stays; follow-up: the dock lists virtual tail measures too (BMS up to #999), and every meter edit (dock, bar-line drag, cursor meter) now uses `resize_measure_keep_rows_action_t`, so objects keep their rows from the start of the song, virtual measures are materialized on demand and native charts regrow over their audio; Ctrl+A is not bound, the dock has Select all and Clear buttons instead)
- **Outcome:** a Measures dock lists every measure with its index, meter and length, as the Sounds dock lists sounds. Selecting several rows (with Shift, Ctrl or Ctrl+A) and choosing a meter sets it on all of them in one undo step.
- **Read first:** `src/ui/nuklear/compose_sounds_panel.cpp` (list, selection and toolbar pattern), `src/ui/nuklear/keysound_list_state.cpp`, `src/ui/nuklear/compose_inspector.cpp` (`render_compose_meter_control`), `src/sys/compose_measure_commands.cpp`.
- **Files owned:** new `src/ui/nuklear/compose_measures_panel.cpp`, new `src/ui/nuklear/compose_measures_panel.h`, `src/ui/nuklear/compose_panel.cpp` (dock layout), `src/ui/nuklear/compose_toolbar.cpp` (dock toggle), `src/ui/nuklear/compose_state.h`, `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `src/sys/compose_measure_commands.cpp`, `src/sys/compose_measure_commands.h`, core meter command and test under `src/core/` and `tests/core/`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Add a core command that sets one meter on a list of measures atomically, with a revision check. Test it.
  2. Project bounded measure rows into the Compose view.
  3. Build the dock: a virtualized list, the shared multi-select state, a meter field and Apply. A row click jumps the cursor there.
  4. Add the dock toggle next to the others, and add event-harness cases.
- **Invariants:** notes keep their absolute time order. A meter change keeps note positions as the single-measure command does.
- **Reject/failure behavior:** an invalid meter or a stale revision changes no measure.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.'`; `ctest --test-dir build -L event-harness`; `clang-tidy -p build` on new sources.
- **Out of scope:** the bar-line drag gesture (T-398).

## T-398 Alt+drag a bar line to change the measure's length

- **Status:** done (2026-10-06; the gesture is UI-side: begin, preview and commit live in `compose_measure_resize_input.cpp`, and the release sends the existing mass-meter action instead of new input kinds. Supporting files added: `src/ui/nuklear/measure_resize.{h,cpp}`, `src/ui/nuklear/compose_measure_resize_input.{h,cpp}`, `tests/ui/measure_resize_tests.cpp`, `tests/ui/compose_measure_resize_tests.cpp`, `tests/CMakeLists.txt`, `CMakeLists.txt`)
- **Outcome:** Alt+dragging the line where a measure ends moves it in steps of the active grid snap. A ghost line and a "7/8"-style label show the result, and releasing sets that measure's meter in one undo step. Escape cancels.
- **Read first:** `src/ui/nuklear/compose_viewport_input.cpp` (`start_compose_beat_fit_drag`, a gesture with the same shape), `src/sys/compose_waveform_overlays.h`, `src/sys/compose_view.h` (`beat_fit_drag`), `src/sys/compose_measure_commands.cpp`.
- **Files owned:** `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/frontend.h` (new input kinds), `src/sys/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose_view.cpp`, `src/sys/compose_measure_commands.cpp`, `src/ui/nuklear/compose_canvas.cpp` (ghost line), `tests/ui/nuklear_event_harness_tests.cpp`, the matching `tests/sys/` test, this card.
- **Steps:**
  1. Hit-test bar lines within 4 px while Alt is held, and show a resize cursor.
  2. Send begin, update and commit inputs. While dragging, compute the length in snap units, with a minimum of one snap and a maximum of 4 measures, and reduce the meter to lowest terms with a power-of-two denominator.
  3. Draw the ghost line and label, and commit through the meter command.
  4. Add event-harness cases for shortening, lengthening and Escape.
- **Invariants:** Alt+drag on notes keeps its current behavior, and bar lines take precedence only within the hit band. The beat-fit drag is unchanged.
- **Reject/failure behavior:** a meter that cannot be represented, for example under a non-power-of-two snap, shows the reason in the label and does not commit.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`; `clang-tidy -p build src/ui/nuklear/compose_viewport_input.cpp`.
- **Out of scope:** the measure list dock (T-397).

## T-399 Show BMS branch nodes as a tree, with node text limited to unparsed statements

- **Status:** done (2026-10-06; rows gained `node_id`, `raw_statement` and `choice_block` in `src/ui/frontend.h` and `src/sys/workspace_projection.cpp`; the harness test is in `tests/ui/nuklear_event_harness_tests.cpp`)
- **Outcome:** the branch panel shows `#RANDOM`/`#SWITCH` blocks and their `#IF`/`#CASE` arms as an expandable tree of nodes. Selecting a node shows only the statements the chart parser did not understand.
- **Read first:** `src/ui/nuklear/bms_branch_panel.cpp`, `src/ui/frontend.h` (`WormUiComposeBranchRowView`, `WormUiComposeBranchChoiceView`), the view projection that fills them (`grep -rn branch_rows src/sys`).
- **Files owned:** `src/ui/nuklear/bms_branch_panel.cpp`, `src/ui/nuklear/bms_branch_panel.h`, `src/ui/frontend.h` (branch view fields), the branch projection in `src/sys/`, `tests/ui/` branch panel test, this card.
- **Steps:**
  1. Add node rows (kind, value and arm) to the projection, and filter statement rows down to the unparsed ones.
  2. Render the nodes with `nk_tree` and choice controls on each block node.
  3. Test with a fixture that has nested `#RANDOM`/`#IF` blocks and one unknown statement.
- **Invariants:** branch serialization round-trips byte for byte. Raw statement editing keeps its limits.
- **Reject/failure behavior:** truncated projections still show the truncation notice.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'branch'`.
- **Out of scope:** dock placement (T-400).

## T-400 Dock the BMS branch panel to the left of the field and push the field over

- **Status:** done (2026-10-06; the dock hides below 900 px, and the Compose projection narrows its viewport by the dock through the `compose_left_dock_pixels` hook in `application_command_state_t`, wired in `src/sys/main.cpp` and `src/sys/application_frame_projection.cpp`. Harness tests cover the shift and the narrow-window hide. No smoke capture with a branching BMS chart yet: the `compose` smoke fixture has no branches)
- **Outcome:** the branch panel is a fixed left dock, not a floating one, and the field and BGM area start to its right. The BGM area scrolls horizontally inside its narrower space, and nothing overlaps at 1280 px wide.
- **Read first:** `src/ui/nuklear/compose_panel.cpp` (`ComposeDockLayout`, `render_compose_side_docks`, `compose_bgm_scroll_valid`), `src/ui/nuklear/compose_toolbar.cpp`.
- **Files owned:** `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_panel_internal.h`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_inspector.cpp` (remove the branch call), `src/ui/nuklear/compose_state.h`, `src/sys/compose_bgm_bar.cpp` (if scroll bounds come from there), `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Add a left dock width to the layout and shift the field origin by it.
  2. Clamp BGM horizontal scroll to the new visible width, and test that the last BGM column is reachable.
  3. Add a toolbar toggle, shown only for BMS charts with branches.
  4. Take smoke captures with the dock open and closed.
- **Invariants:** canvas hit-testing stays correct after the shift, and the note under the pointer is the note clicked.
- **Reject/failure behavior:** below the minimum width, the dock hides rather than overlapping.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`; `scripts/smoke-compose-virtual-x.sh /tmp/branch-dock.png compose`.
- **Out of scope:** the right-hand docks.

## T-401 Right-align the dock toggles and move px/measure to the bottom

- **Status:** done (2026-10-05; Xvfb captures at 1280 and 900 px inspected; full headless suite passes)
- **Result:** at a window width of 1100 px or more the five dock toggles close the toolbar row at the right edge, after the BGM scroll bar's spacer. The full toolbar needs about 1,090 px, so below 1100 px the toggles **stay in their old middle position**, where moving them to the end would clip them (a deviation from "always at the right edge", kept to avoid hiding them at 900 px). `dock_toggles_at_end` in `compose_toolbar.cpp` decides for both the row template and the render order. The "N px/measure" label and its 160 px column left the toolbar; `render_status_bar` in `frontend.cpp` now draws it after "Snap 1/N" in Compose when the window is at least 900 px wide (it was shown at 1100 px or more before). The tooltip tests that hovered fixed icon positions were moved to the new ones in `nuklear_event_harness_tests.cpp`. Captures: 1280 px shows the toggles at the right and the label in the bottom row; 900 px shows them in the middle and the label in the bottom row. "Bottom status row of the field" was read as the existing bottom status bar, since the field has no row of its own.
- **Outcome:** in the Compose toolbar, the five dock toggles sit at the right edge, and the "N px/measure" readout moves to the bottom status row of the field.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp` (`layout_compose_toolbar`, the px/measure label), `src/ui/nuklear/compose_panel.cpp`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_panel.cpp`, `tests/ui/` toolbar layout tests, this card.
- **Steps:**
  1. Move the panel-icon group to the end of the template, after the dynamic spacer.
  2. Draw px/measure in the bottom status row, and remove its toolbar column.
  3. Take smoke captures at 1280 px and 900 px.
- **Invariants:** toolbar actions and their tooltips are unchanged.
- **Reject/failure behavior:** not applicable.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; smoke capture.
- **Out of scope:** transport buttons (T-402).

## T-402 Make Keep pitch an icon toggle

- **Status:** done (2026-10-05; Xvfb capture of the transport row inspected; harness cases and the full headless suite pass)
- **Result:** `music-2` (Lucide, same pinned commit) was added to `assets/icons/lucide`, `scripts/generate-lucide-icons.py`, `Glyph::music_2` and the regenerated `lucide_icon_pixels.h` (the generator needed `rsvg-convert` and Pillow, installed for this run). `render_transport_rate` now draws an `IconButton` with the same availability gate and `SET_KEEP_PITCH` payload; its status-bar column went from 100 px to one icon column. Tooltip: "Preserve pitch at reduced speed", or "Keep pitch is unavailable without audio output" when disabled. The capture shows the off state only; the on state uses the shared `IconButton` selected style. Tests: `tests/ui/keep_pitch_event_harness_tests.cpp`.
- **Outcome:** the transport "Keep pitch" text button becomes an icon toggle with the tooltip "Preserve pitch at reduced speed", showing its on and off states like the other transport toggles.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_transport_rate`), `src/ui/nuklear/chrome.h` (`IconButton`, `mode_button`), `scripts/generate-lucide-icons.py`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/lucide_icons.h`, `src/ui/nuklear/lucide_icon_pixels.h`, `src/ui/nuklear/lucide_icons.cpp`, `scripts/generate-lucide-icons.py` (icon list only), this card.
- **Steps:**
  1. Add a Lucide icon such as `audio-waveform` or `music-2` through the generator.
  2. Replace the `mode_button` with an active-state icon toggle.
- **Invariants:** the action and its availability gate are unchanged.
- **Reject/failure behavior:** when unavailable, the icon is disabled and its tooltip says so.
- **Verify:** `cmake --build build`; smoke capture of the transport.
- **Out of scope:** other transport controls.

## T-403 Use the native macOS title bar and global menu bar

- **Status:** ready (manual check needs a macOS host)
- **Outcome:** on macOS, the window keeps the system title bar and traffic lights, and File/Edit/View become a global `NSMenu` that runs the same registry commands. The in-window menubar row and the custom minimize, maximize and close buttons are not drawn. Linux and Windows are unchanged.
- **Read first:** `src/sys/window.cpp` (`window_hit_test`, `SDL_SetWindowBordered`), `src/ui/nuklear/frontend.cpp` (menubar and window buttons near the `SDL_WINDOW_MAXIMIZED` check), `src/core/command_registry.cpp`, `src/sys/platform_shortcut_profile.cpp`.
- **Files owned:** `src/sys/window.cpp`, new `src/sys/macos_menu.mm`, new `src/sys/macos_menu.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/main.cpp` (command dispatch hook), `CMakeLists.txt` (Apple-only source and `-framework AppKit`), this card.
- **Steps:**
  1. On `__APPLE__`, skip the borderless and hit-test setup.
  2. Build the `NSMenu` from the same menu model the frontend draws: titles, shortcuts and enabled state refreshed each frame. Selecting an item queues the command ID.
  3. Skip the menubar row and the window buttons on macOS, and give the space back to the content.
  4. Keep the in-window path behind a runtime flag for headless tests.
- **Invariants:** the command IDs and shortcut profile are shared, with no macOS-only command.
- **Reject/failure behavior:** if `NSMenu` setup fails, fall back to the in-window menubar.
- **Verify:** `cmake --build build` and the full suite on Linux (unchanged); on a macOS host, a manual check of the menu, the traffic lights and fullscreen. Record who ran it.
- **Out of scope:** Windows title bar changes.

## T-404 Draw BMSON stem waveforms in the BGM lanes

- **Status:** ready
- **Outcome:** each BMSON sound channel (stem) lane draws its audio waveform along chart time, as BMSONE does, with slices visibly divided at their restart and continue points. The selected stem is no longer the only one drawn.
- **Read first:** `src/sys/bmson_waveform_projection.h`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/waveform_analysis_runtime.cpp`, `src/ui/nuklear/compose_canvas.cpp` (BGM lane drawing).
- **Files owned:** `src/sys/bmson_waveform_projection.cpp`, `src/sys/bmson_waveform_projection.h`, `src/sys/waveform_analysis_runtime.cpp`, `src/sys/waveform_analysis_runtime.h`, `src/ui/frontend.h` (per-lane waveform view), `src/ui/nuklear/compose_canvas.cpp`, `tests/sys/` projection test, this card.
- **Steps:**
  1. Queue waveform analysis for each visible stem's asset, bounded by a configured count and reusing cached results.
  2. Project per-lane peak columns for the visible time range only.
  3. Draw them in the BGM lanes under the event markers.
  4. Test the projection: two stems with a continue point give contiguous peaks, and a restart point restarts the source offset.
- **Invariants:** audio playback is unchanged. Projection work stays bounded per frame.
- **Reject/failure behavior:** an asset that is missing or still decoding shows a placeholder band and does not block the frame.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'waveform|bmson'`; smoke capture of a BMSON fixture.
- **Out of scope:** gestures (T-405).

## T-405 Click and Shift-click restart and continue points on BMSON stem lanes

- **Status:** blocked-on T-404
- **Outcome:** on a stem lane, a click adds a continue point at the snapped row, Shift+click adds a restart point, and a click on an existing point toggles it between the two. The waveform split updates at once, and each gesture is one undo step.
- **Read first:** `include/wormedit/core/bmson_sound_channel_editor.h` (`toggle_bmson_sound_channel_restart`, `set_bmson_sound_channel_restart`), `src/ui/nuklear/compose_object_input.cpp`, `src/sys/compose_input.cpp`, the razor mode in `src/ui/nuklear/compose_resources.cpp` (or the BMSON tab after T-390).
- **Files owned:** `src/ui/nuklear/compose_object_input.cpp`, `src/sys/compose_input.cpp`, `src/ui/frontend.h` (input kinds), `src/core/bmson_sound_channel_editor.cpp` (only if an insert-point command is missing), `tests/core/` and `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Decide how this relates to razor mode. If they overlap, record on this card that the click gesture replaces razor and remove razor in the BMSON tab.
  2. Send the click and Shift+click through the standard wiring pattern.
  3. Add event-harness cases for add continue, add restart, toggle, and undo.
- **Invariants:** stored BMSON events round-trip through export unchanged except for the edited flag.
- **Reject/failure behavior:** a click outside a stem lane or at an occupied row of another kind does nothing.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`; `ctest --test-dir build -R '^core\..*bmson'`.
- **Out of scope:** copy and paste (T-406).

## T-406 Copy and paste BMSON restart and continue points

- **Status:** ready
- **Outcome:** selecting stem points and pressing Ctrl+C then Ctrl+V at another row pastes the points with their restart or continue flag, sound channel and relative timing intact. The paste is one undo step.
- **Read first:** `src/sys/compose_clipboard_selection.h`, `include/wormedit/core/editor_scene.h` (`bmson_restart`), the core clipboard command (`grep -rn clipboard include/wormedit/core`).
- **Files owned:** `src/sys/compose_clipboard_selection.h`, the core clipboard source and header, `tests/core/` clipboard tests, this card.
- **Steps:**
  1. Stale check: copy a selection of stem points today and record what is lost.
  2. Carry `bmson_restart` and the channel ID in the clipboard payload.
  3. Test a round trip (copy, paste, export) and pasting into a chart without that channel, which rejects atomically.
- **Invariants:** the clipboard format stays backward compatible with existing payloads.
- **Reject/failure behavior:** a paste that targets a missing channel changes nothing and says why.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'clipboard'`.
- **Out of scope:** pasting across formats.
