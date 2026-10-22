# Compose cleanup: navigation repeats, retired inspector sections, labels and bookmarks (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-05. The user settled two choices here: note labels get an
inline editor that opens on double-click, replacing the inspector editor, and BMSON
tools move into the Sounds dock ([T-390](#t-390-move-bmson-tools-into-a-sounds-dock-tab)).
Retirement cards remove UI only. Stored data and actions stay unless a card says otherwise.

## T-385 Repeated Previous Bookmark while playing keeps stepping back

- **Status:** done (2026-10-05; new unit tests and the full headless suite pass)
- **Result:** a repeat press within 300 ms (`playing_seek_repeat_window`) starts from the recorded seek target (`last_playing_bookmark_seek`). The record is cleared when navigating while stopped, a failed seek, and is ignored for another chart or after the window. A manual seek is covered by the window only. Test: `repeated previous playing bookmark within 300 ms ...`. Review fix (2026-10-05): the record is also cleared by chooser jumps, wheel skips, minimap and seek-bar seeks, and stop, play and restart (`application_command_state_t::invalidate_playing_seek_records`), with mixed-navigation tests.
- **Outcome:** while playing, pressing Previous Bookmark twice within 300 ms of reaching bookmark B moves to the bookmark before B, not to B again. An event-harness or system test shows this with three bookmarks.
- **Read first:** `src/sys/compose_bookmark_commands.cpp` (`navigate_beat_bookmark`, `playing_bookmark_origin`), `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/commands/application_command_dispatch.cpp` (around `navigation_previous_bookmark`).
- **Files owned:** `src/sys/compose_bookmark_commands.cpp`, `src/sys/compose_bookmark_commands.h`, `src/sys/commands/application_commands.h` (state field only), `tests/sys/` bookmark navigation test beside its sibling, this card, `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Record the last seek target row and a steady-clock time in `application_command_state_t` when a playing bookmark seek succeeds.
  2. In `playing_bookmark_origin`, for `previous` within 300 ms of that seek (a named constant), use the recorded target row as the origin, so `navigate_markers` returns the bookmark strictly before it.
  3. Clear the record on stop, on a manual seek and on a chart switch.
  4. Add a test that injects the clock: a second press within 300 ms reaches A, and a press after 300 ms reaches B.
- **Invariants:** stopped-cursor navigation is unchanged. Next Bookmark is unchanged. The playhead row is never written to the document.
- **Reject/failure behavior:** with no earlier bookmark, the command reports "No previous bookmark." and does not seek.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'bookmark'`; `clang-tidy -p build src/sys/compose_bookmark_commands.cpp`.
- **Out of scope:** wheel seeking (T-386), and Previous Marker (bookmark or section), which can reuse the helper later.

## T-386 Notched wheel steps while playing keep moving past the row just reached

- **Status:** done (2026-10-05; new unit tests and the full headless suite pass)
- **Result:** confirmed cause: the live playhead had not advanced to the first notch's target, so each notch recomputed the same row. `skip_playing_playhead` now keeps `last_playhead_skip`; a same-direction repeat within the same 300 ms window starts from it, and a reversal uses the live playhead. Test: `repeated playing skips within 300 ms ...`.
- **Outcome:** while playing, three fast notches of a notched wheel move the playhead three snap rows, not one row three times. A test feeds three coarse wheel events within 300 ms.
- **Read first:** `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_playhead_skip.cpp`, `src/sys/compose/input/compose_input.cpp` (wheel handling near `compose_wheel_scroll_delta`).
- **Files owned:** `src/sys/compose/input/compose_playhead_skip.cpp`, `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_input.cpp` (wheel-while-playing branch only), the matching test under `tests/sys/`, this card.
- **Steps:**
  1. Confirm the cause: the second notch reads the playhead from before the first seek takes effect, or from just after it, so `playhead_skip_target_row` gives the same row again. Record which in the card.
  2. Keep the last skip target and its time. Within 300 ms (the same constant as T-385), a further skip in the same direction starts from that target.
  3. Add a test with an injected clock for the forward and backward directions.
- **Invariants:** smooth-pixel (touchpad) wheel scrolling is unchanged. A direction reversal starts from the live playhead.
- **Reject/failure behavior:** a failed seek clears the record and keeps playing, as now.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'playhead_skip|wheel'`; `clang-tidy -p build src/sys/compose/input/compose_playhead_skip.cpp`.
- **Out of scope:** wheel behavior while stopped.

## T-387 Outline the lane header numbers in black

- **Status:** done (2026-10-05; 7K Xvfb capture inspected, headless suite passes)
- **Result:** the outline is drawn in the compose canvas (`field_text_outlined`: black text at the eight 1 px offsets, then near-white), so `renderer_t::text` is unchanged and both the tile and Nuklear text paths use it. The 7K capture from `scripts/smoke-compose-virtual-x.sh` shows 1–7 readable on the dark lanes. Not captured: an 8K layout and the red scratch lane, because the smoke script only creates the default 7K project.
- **Mockup:** [field-move-collision.svg](../mockups/field-move-collision.svg) (reference: match its structure and hierarchy)
- **Outcome:** the lane numbers at the top of each play lane (`1`–`8` in the lane header strip) are drawn in white with a thin black outline, and stay readable on every lane color: white, grey, dark, and the red scratch or special lanes. Smoke captures of a 7K BMS layout and an 8K layout show it.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_play_lane_titles`, `field_text`), `src/ui/field/field_renderer.cpp` (`renderer_t::text`).
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/ui/field/field_renderer.cpp`, `src/ui/field/field_renderer.h`, `tests/ui/` field renderer test if one exists, this card.
- **Cause:** the numbers are fixed grey `rgb(122,133,153)` on a `rgb(37,43,55)` box, whatever the lane color underneath.
- **Steps:**
  1. Add an outlined text call to the renderer. It draws the glyph run in black at the eight 1 px offsets, then in the text color, batched into the same layer.
  2. In `draw_compose_play_lane_titles`, drop the background box and draw the number in near-white (`rgb(240,244,250)`) with the black outline.
  3. Take Xvfb smoke captures of a 7K and an 8K layout and inspect the header strip.
- **Invariants:** the visibility conditions (`lane_key_legend_visible`, minimum lane size) are unchanged. Other field text is unchanged.
- **Reject/failure behavior:** with no font texture, nothing is drawn, as now.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; `scripts/smoke-compose-virtual-x.sh /tmp/lane-numbers.png compose`, then inspect the image.
- **Out of scope:** sound labels on notes and timing-lane titles (they can reuse the outlined call later).

## T-388 Ctrl+A in a focused Sounds or BGA list selects the whole list

- **Status:** done (2026-10-05; harness cases and the full headless suite pass)
- **Result:** Ctrl+A reached `selection.select_all` through the shortcut queue in `queue_shortcut_key_event` (`frontend.cpp`). It now checks first (`select_all_in_hovered_dock_list`): with the pointer over the Sounds or BGA dock list, and no text field active, it selects the shown rows and consumes the key; an empty list selects nothing and also consumes it. Ownership is the hovered list, not a tracked last-clicked list. `bga_shown` was added to the compose state. Harness cases: Sounds, BGA (including empty) and canvas fallback in `tests/ui/dock_select_all_event_harness_tests.cpp`; they set the BGA bounds by hand because that dock is not seeded in the harness.
- **Outcome:** with the pointer or focus on the Sounds or BGA dock list, Ctrl+A selects every shown row and does not select chart notes. An event-harness case checks both docks and the canvas fallback.
- **Read first:** `src/ui/nuklear/compose_state.cpp` (around `worm_ui_keysound_select_all`), `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/commands/application_command_dispatch.cpp` (`selection_select_all`), [frontend event harness](../../frontend-event-harness.md).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Find where Ctrl+A becomes `selection.select_all` before the docks see it.
  2. Track which list owns keyboard focus (last clicked or hovered list). When a list owns it, consume Ctrl+A there and call `worm_ui_keysound_select_all` with the shown rows.
  3. Add event-harness cases: Sounds owner, BGA owner, and canvas owner (selects notes).
- **Invariants:** text fields still get Ctrl+A as text select-all.
- **Reject/failure behavior:** an empty list selects nothing and does not fall through to the canvas.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Out of scope:** the Layers dock list.

## T-389 Retire the inspector "Samples / BGM / BGA" resource list

- **Status:** done (2026-10-05; user decisions below; full headless suite passes; no real-window check)
- **User decisions (2026-10-05):** "Create BGM from waveform" and "Create BGA from selected image" are retired. The Replace pointer mode is retired; replacing a note should become the default when drawing over an existing one, which belongs to the Draw tool cards (T-412 and later). For the export group the user was unsure of its use (it writes the selected output group's converted chart files, with "Replace existing chart files" overwriting files already there), so it was **kept** in the inspector under an "Export" section until the user decides.
- **Result:** `compose_resources.cpp` now only keeps `update_selection_state` (arming that follows the selected object) and the export group; the filter/search, armed status, Assign selected, Disarm, Place at cursor, Place/Replace modes, Auto next, Create, link/Browse and the resource rows are removed. Per-resource copies stay in Metadata. Tests that drove the retired widgets were deleted or trimmed in `tests/ui/inspector_resource_workflow_tests.cpp`.
- **Follow-ups left on purpose:** `ASSIGN_RESOURCE_TO_SELECTION`, `PLACE_RESOURCE`, `CREATE_RESOURCE` and `IMPORT_RESOURCE_ASSET` are no longer reachable from this panel (the last stays in Metadata). The `replace` branch in `compose_object_input.cpp` and `compose_canvas.cpp`, and the state fields `resource_search`, `resource_import_path`, `resource_import_error`, `auto_increment_on_place` and the file-dialog plumbing in `compose_state.cpp`, are now unused or mostly unused and were not deleted, to avoid touching unowned input code.
- **Outcome:** the inspector no longer shows "Samples / BGM / BGA". It loses "Link audio as BGM", "Place BGM at cursor", "Assign selected", "Disarm" and the filter/search row with it. Music is linked in Metadata, the BGM start is set by the inline offset, and sounds are assigned from the Sounds dock.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_inspector.cpp` (call to `worm_ui_nuklear_render_compose_resources`), `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`.
- **Files owned:** `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_resources.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_state.h`, `CMakeLists.txt`, affected tests under `tests/ui/`, this card.
- **Steps:**
  1. Write a table on this card listing each control in `compose_resources.cpp` and its replacement (Metadata, inline offset, Sounds dock, BGA dock, or BMSON tab from T-390). **STOP** if any control other than those the user named, such as the export group or export copy, has no replacement.
  2. Remove the inspector call and the retired render functions. Delete `compose_resources.cpp` if nothing remains.
  3. Remove frontend-only state that is now unused. Keep the action enumerators that other docks still emit.
  4. Update or delete tests that drove the retired widgets.
- **Invariants:** the documents, resource catalog and armed-placement actions emitted by the docks are unchanged.
- **Reject/failure behavior:** not applicable (UI removal).
- **Verify:** `cmake --build build`; `env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build --output-on-failure`; smoke capture showing the inspector without the section.
- **Out of scope:** adding new features to the Sounds or BGA docks.

### Control table (step 1, 2026-10-05)

Every control `worm_ui_nuklear_render_compose_resources` draws today (`compose_resources.cpp`), and where the same action lives afterwards.

| Control | Action | Replacement |
| --- | --- | --- |
| Export group popup, "Replace existing chart files", "Export selected group", output path label | `EXPORT_OUTPUT_GROUP` | **None.** Only this list emits it. |
| BGM / BGA filter buttons and search | none (frontend state) | Sounds dock and BGA dock searches. |
| "Armed: …" status line | none | Sounds and BGA docks show the armed row. |
| Assign selected, Disarm | `ASSIGN_RESOURCE_TO_SELECTION` | Named by the user as retired. No dock emits `ASSIGN_RESOURCE_TO_SELECTION` today (grep of the two docks), so confirm that the action may become unreachable from the UI. |
| Place BGM / BGA at cursor, Place and Replace pointer modes, Auto next after place | `PLACE_RESOURCE` and pointer modes | "Place BGM at cursor" is named by the user. The BGA dock has a Place pointer mode; the Sounds dock has paint mode. No dock has Replace mode or a place-at-cursor button, and the user did not name those. |
| "Create BGM from waveform", "Create BGA from selected image" | `CREATE_RESOURCE` | **None.** Only this list emits it. |
| External file path, Browse, "Link audio as BGM" / "Link image as BGA" | `IMPORT_RESOURCE_ASSET` | Metadata audio section (user: music is linked in Metadata) and the BGA dock import. |
| Per-resource "Copy source into project" | `COPY_RESOURCE_TO_PROJECT` | `metadata_audio_section.cpp` and `audio_panel.cpp`. |
| Per-resource "Copy source into export folder" and its destination label | `COPY_RESOURCE_TO_EXPORT` | `metadata_audio_section.cpp`. |
| Resource rows (identity, usage, arm) and help text | arming | Sounds dock and BGA dock rows. |

**STOP reached:** "Export selected group", "Create BGM from waveform / BGA from selected image" and the Replace pointer mode are not among the controls the user named and have no replacement, so step 2 has not started. The code is unchanged.


## T-390 Move BMSON tools into a Sounds dock tab

- **Status:** done (2026-10-05; harness cases, app build and the full headless suite pass; no real-window or clang-tidy check)
- **Result:** the Sounds dock draws a Sounds/BMSON tab strip (`render_sounds_dock_tabs`, `chrome::mode_button`) only when the new `WormUiComposeView::bmson_tools_available` is set (`chart.source_family == bmson`, filled in `compose_view.cpp`). The selected tab is `sounds_dock_bmson_tab` in compose state, and a non-BMSON chart resets it to Sounds. The BGM object and marker buttons, razor modes, staged commit/cancel, slice Restart/Continue and the staged-region audition list moved unchanged into `compose_bmson_tab.cpp` (`worm_ui_nuklear_render_compose_bmson_tab`) and were deleted from `compose_resources.cpp` and `compose_inspector.cpp`; actions and payloads are the same code. Until T-389 retires the rest of "Samples / BGM / BGA", the inspector still shows that list without the BMSON controls. Harness cases: tab visibility by chart type, and the razor toggle from the tab. Review fixes (2026-10-05): razor eligibility (selected source lane, no note, layer or timing tool) is enforced in the input handlers, not only while the tab draws, and the tab content sits in a scrollable group so lower staged-region actions are reachable in short windows (`tests/ui/bmson_tab_event_harness_tests.cpp`).
- **Outcome:** the Sounds dock has two tabs, Sounds and BMSON. The BMSON tab holds the object and marker actions, razor mode, staged regions and selected-slice controls that live in the inspector today. The tab shows only for BMSON charts.
- **Read first:** `src/ui/nuklear/compose_resources.cpp` (`render_bmson_controls` and its helpers), `src/ui/nuklear/compose_inspector.cpp` (`render_staged_bmson_regions`, `render_selected_bmson_slice_controls`), `src/ui/nuklear/compose_sounds_panel.cpp`.
- **Files owned:** `src/ui/nuklear/compose_sounds_panel.cpp`, new `src/ui/nuklear/compose_bmson_tab.cpp`, new `src/ui/nuklear/compose_bmson_tab.h`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_state.h`, `CMakeLists.txt`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Add a tab strip to the Sounds dock and store the selected tab in compose state.
  2. Move the BMSON render functions unchanged into `compose_bmson_tab.cpp`, and call them from the BMSON tab.
  3. Remove the moved calls from the inspector and from `compose_resources.cpp`.
  4. Add an event-harness case: the razor toggle works from the tab.
- **Invariants:** the emitted actions and their payloads are byte-identical.
- **Reject/failure behavior:** on a non-BMSON chart, the BMSON tab is hidden and the dock falls back to Sounds.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`; `clang-tidy -p build src/ui/nuklear/compose_bmson_tab.cpp`.
- **Out of scope:** stem waveforms and restart/continue gestures (T-404, T-405).

## T-391 Edit note labels inline on double-click

- **Status:** done (2026-10-05; harness cases, app build and the full headless suite pass; no real-window or clang-tidy check)
- **Result:** the existing flow (note double-click selects the note, then `focus_note_label_editor` reaches the UI) now opens an inline popup instead of focusing the inspector field. `compose_panel.cpp` opens it with `begin_compose_note_label_inline` and consumes the flag. It reuses `bookmark_rename_editor_t` with a new `kind` (so keyboard, pointer and Escape capture, release handling and the stale-revision close are the T-371 ones), and `compose_bookmark_rename.cpp` was extended (supporting file, approved by the coordinator within this outcome) to pick bounds and commit action by kind. It is anchored above the double-click position, not the note rectangle, so `compose_view.cpp` is untouched. The field opens with its text selected, so typing replaces the prefilled label. Enter sends `APPLY_NOTE_LABEL`, or `CLEAR_NOTE_LABEL` when empty (an empty string would be a retained authored label); Escape, blur and a stale revision apply nothing. `render_compose_note_label_editor` is removed and the search heading reads "Find note label". `state.note_label` and its sync in `compose_toolbar.cpp` are now unused and left for a later cleanup. Tests: `tests/ui/note_label_inline_event_harness_tests.cpp`; two inspector workflow tests that looked for "Label (0)" now look for "Find note label". The harness seeds the view by hand, so the sys-to-UI hand-off is not covered end to end.
- **Outcome:** double-clicking a note opens a text field over the note, prefilled with its label. Enter applies the label to the selected notes, and Escape or blur cancels. The inspector "Label (n)" editor is gone, and "Find label" stays, renamed "Find note label".
- **Read first:** `src/ui/nuklear/compose_inspector.cpp` (`render_compose_note_label_editor`, `render_compose_label_search`), `src/ui/nuklear/compose_bookmark_rename.cpp` (inline editor pattern from T-371), `src/ui/nuklear/compose_panel.cpp` (rename overlay call sites), `include/wormedit/core/note_label_editor.h`.
- **Files owned:** new `src/ui/nuklear/compose_note_label_inline.cpp`, new `src/ui/nuklear/compose_note_label_inline.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_state.h`, `src/sys/compose/views/compose_view.cpp` (note rectangle projection, if needed), `CMakeLists.txt`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Find what double-click does now (`focus_note_label_editor`), and send it to the inline editor instead.
  2. Build the editor in the T-371 style: position it over the note's rectangle, commit once with `WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL`, and let an empty commit clear the label.
  3. Remove the inspector label editor and rename the search heading.
  4. Add event-harness cases: double-click, type, Enter (applied, one undo); Escape (unchanged); click outside (unchanged).
- **Invariants:** the label data model and size limits are unchanged. Typing never reaches the canvas while the field is focused.
- **Reject/failure behavior:** a stale revision closes the editor without applying anything.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`; `clang-tidy -p build src/ui/nuklear/compose_note_label_inline.cpp`.
- **Out of scope:** drawing labels on the canvas.

## T-392 Remove or fix the empty quantize "Last applied report"

- **Status:** done (2026-10-05; new tests pass in the headless and app builds; full headless suite passes; no real-window check)
- **Result:** step 1 showed the cache **is** filled: `dispatch_compose_area_with_quantize_report` records the rows after an apply that changes the document (test `Compose area quantize records the applied report rows`, in the app build only because that file needs the audio decoder), and nothing clears it. The real cause of the empty heading was in the inspector: `render_quantize_report_rows` called `nk_tree_push` from one call site for both "Preview rows" and "Last applied report", and Nuklear keys a tree's open state by call site, so the applied report shared the collapsed state of the preview and always looked empty. Both trees now use distinct ids (`nk_tree_push_id`). Because the data is real and differs from the preview (the preview shows 0 moved after an apply), the section was repaired instead of removed: it draws only when rows were recorded and opens expanded. No dead view fields were removed. Harness tests: `tests/ui/quantize_report_event_harness_tests.cpp`.
- **Outcome:** after Apply quantization, the inspector either lists the moved rows or no longer shows a "Last applied report" heading.
- **Read first:** `src/ui/nuklear/compose_inspector.cpp` (`render_compose_quantize_section`), `src/sys/compose/views/compose_view.cpp` (around `quantize_applied_row_count`).
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/views/compose_view.h`, `src/ui/frontend.h` (applied-row fields only), related tests, this card.
- **Steps:**
  1. Reproduce in the event harness: apply quantization, then read `quantize_applied_row_count`. Record whether the cache is ever filled.
  2. If it is never filled, or is cleared on the next frame, remove the section and the dead view fields. Do not repair it, because the preview rows already show the same data.
- **Invariants:** quantize preview and apply behavior are unchanged.
- **Reject/failure behavior:** not applicable.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'quantize'`.
- **Out of scope:** quantize presets.

## T-393 Remove the inspector "Semantic layers" section

- **Status:** done (2026-10-05; harness cases, app build and the full headless suite pass; no real-window check)
- **Result:** the section and its five render helpers are gone from `compose_inspector.cpp`, `compose_inspector.h` and `compose_panel.cpp`. Button map (step 1):

  | Inspector button | Layers dock |
  | --- | --- |
  | Select notes in layer | row menu "Select notes" (existing) |
  | Assign selected objects | clicking a layer row with a selection (existing) |
  | Toggle selected objects | row menu **"Add/remove selected notes"** (added) |
  | Remove selected objects | none; see below |
  | Paint: Armed/Off | none, by user decision (2026-10-05): redundant, because painting uses the armed layer and "selection + click the layer row" assigns |
  | Solo: On/Off | row menu "Solo" (existing) |
  | Clear target | row menu **"Stop targeting this layer"** (inspector name "Clear target") (added) |

  Right-clicking a layer row already makes it the target, and the two added entries ("Add/remove selected notes" (renamed from "Toggle selected" at the user's request: it toggles membership) and "Stop targeting this layer" (renamed from "Clear target")) act on the target the same way "Solo" does. "Add/remove selected notes" is disabled without a selection. The user judged the inspector's Paint toggle redundant, so no "Paint" entry was added (it was added first and then removed). **Finding:** `WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER` is not dispatched anywhere (`compose_actions.cpp` returns nothing for it), so the inspector's "Remove selected objects" never did anything; it was not carried over and the enumerator is untouched. Tests: `Layer row menu carries the retired inspector layer buttons`, `... disables Toggle selected without a selection`, and an inspector check in `quantize_report_event_harness_tests.cpp` (weak: the old section sat below the visible part of the inspector in the harness).
- **Outcome:** the inspector no longer has "Semantic layers". Selecting the notes in a layer, assigning, removing, toggling and paint mode are all available from the Layers dock.
- **Read first:** `src/ui/nuklear/compose_inspector.cpp` (`render_semantic_layer_*`), `src/ui/nuklear/compose_panel.cpp` (`render_compose_inspector_contents`), `src/ui/nuklear/compose_layers_panel.cpp`.
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_inspector.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, affected tests, this card.
- **Steps:**
  1. List each inspector button and its Layers dock equivalent on this card. Add any missing one to the dock row's context menu.
  2. Remove the section and its render helpers.
- **Invariants:** the layer action enumerators and payloads are unchanged.
- **Reject/failure behavior:** not applicable.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Out of scope:** layer color views.

## T-394 Split the markers controls into Bookmarks and Sections with icon buttons

- **Status:** done (2026-10-05; harness cases, app build, clang-tidy (all new functions at 23 or below) and the full headless suite pass; Xvfb capture of the expanded section inspected)
- **STOP cleared:** the user reviewed [bookmarks-sections.svg](../mockups/bookmarks-sections.svg) and approved it with one change: the group title's left margin must match the buttons' right margin. `generate.py` now puts the title at the row-highlight edge (it was inset by 16 px) and the SVG was regenerated; the same rule is now in the T-436 list-group line.
- **Result:** the "Markers / sections" block is "Bookmarks and sections" with four groups: Bookmarks, Sections, Audio-time bookmarks, Audio-time sections. Each header has a title and ‹ › + delete icon buttons with tooltips (a disabled one says why), and the list below shows label and position. Rows: a click selects, and on beat bookmarks it also navigates (`GO_TO_BOOKMARK` with the document stamps); a double-click opens the inline editor anchored at the row, for bookmarks (`RENAME_BOOKMARK`, T-371) and for beat sections (`UPDATE_BEAT_SECTION_LABEL`, a new `section_label` kind of the shared editor). "+" adds an unnamed bookmark or a section at the cursor or selected span, and the trash deletes the selected row. The combo and text-field forms are gone. Audio-time groups keep a compact label and time form under the list ("+" adds from the typed values, an Apply button updates the selected row).
- **Deviations to review:** (1) The beat ‹ › in both groups call the existing marker navigation (bookmark or section edge); there is no bookmark-only or section-only navigation action, and the tooltips say so. (2) "Move selected bookmark to cursor" and "Set start/end to cursor" are not in the mockup but are kept as small buttons under the list when a row is selected, so those actions stay reachable. (3) Section rows do not navigate on click (no go-to-section action exists). (4) En dashes and ellipses are written as "-" and "..." because the UI font lacks them. (5) The Xvfb run needed the app killed by PID: with the software Vulkan driver the app keeps rendering and ignores SIGTERM after the first edit, which is not tied to this change. Tests: `tests/ui/markers_event_harness_tests.cpp`.
- **Future work (user, 2026-10-05):** audio-time bookmarks and beat bookmarks (and likewise audio-time and beat sections) should become one single marker concept in the UI instead of two parallel groups with separate lists and forms. Not started and not planned as a card yet. It will need a decision on how one list shows both anchors and how "add" picks beat or audio time.
- **Mockup:** [bookmarks-sections.svg](../mockups/bookmarks-sections.svg) (reference: match its structure and hierarchy)
- **Outcome:** the "Markers / sections" block becomes two compact groups, Bookmarks and Sections. Each has icon buttons for previous, next, add and delete with tooltips, and a list of entries. The combo-plus-text-field forms are gone.
- **Read first:** `src/ui/nuklear/compose_inspector.cpp` (`render_compose_marker_section`), `src/ui/nuklear/compose_markers.cpp`, `src/ui/nuklear/render/lucide_icons.h`, `src/ui/nuklear/chrome/chrome.h` (`IconButton`).
- **Files owned:** `src/ui/nuklear/compose_markers.cpp`, `src/ui/nuklear/compose_markers.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/render/lucide_icons.h`, `src/ui/nuklear/render/lucide_icon_pixels.h` (via `scripts/generate-lucide-icons.py`), `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. **STOP** and show the user an ASCII mockup of both groups before coding.
  2. Build the Bookmarks group, with a clickable list where a click navigates and a double-click renames (the T-371 editor).
  3. Build the Sections group the same way, showing start and end.
  4. Apply the same grouping to the audio-time bookmark and section forms.
- **Invariants:** the marker actions and stamps are unchanged.
- **Reject/failure behavior:** a disabled icon shows why in its tooltip.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`; smoke capture.
- **Out of scope:** canvas marker drawing.

## T-395 Explain the beat-bookmark failure past the last measure in plain words

- **Status:** done (2026-10-05; new tests and the full headless suite pass)
- **Result:** extending measures is not part of bookmark add, so both messages (`add_beat_bookmark_here` and `edit_beat_bookmark`) now say "Bookmarks can only go inside existing measures. Add measures up to the cursor first." The document and revision stay unchanged. Test: `adding a bookmark past the last measure ...`. The beat-section message still says "materialized" (out of scope).
- **Outcome:** adding a beat bookmark with the cursor past the last measure either creates the measures needed, as note placement does, or says "Bookmarks can only go inside existing measures. Add measures up to the cursor first." The words "materialized cursor measure" no longer appear.
- **Read first:** `src/sys/compose_bookmark_commands.cpp` (around `virtual_bms_cursor_row`), `src/sys/compose/actions/compose_actions.cpp` (near line 1586), and how note placement handles `virtual_bms_cursor_row`.
- **Files owned:** `src/sys/compose_bookmark_commands.cpp`, `src/sys/compose/actions/compose_actions.cpp`, the matching test under `tests/sys/`, this card.
- **Steps:**
  1. If note placement extends measures within one undo step, reuse that path for bookmarks.
  2. Otherwise, replace both messages with the plain wording above.
  3. Test the virtual-row case.
- **Invariants:** no partial measure creation without the bookmark.
- **Reject/failure behavior:** a rejected add leaves the document and its revision unchanged.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'bookmark'`.
- **Out of scope:** the BMSON audition "materialized" message.

## T-396 Double-clicking a bookmark on the canvas opens its rename field

- **Status:** done (2026-10-05; new tests and the full headless suite pass)
- **Result:** cause: rename was only reachable by a single press in a 36 px strip at the right edge of the playable field, and no double-click path existed, so a double-click on the line or label elsewhere did nothing. `process_compose_double_clicks` now calls `begin_compose_bookmark_rename_at_double_click` (whole line within 6 px, plus an 18 px label zone above it on the right half) unless a note is under the pointer. Event-harness regressions fail without the fix. Review fix (2026-10-05): an exact line hit now beats any label zone, and label zones belong only to named bookmarks (nearest line wins, lower ID on ties), with order-independence unit tests. Isolated Xvfb check with xdotool not run in this session.
- **Follow-up (2026-10-05, Zar's feedback):** the hit band now spans the full viewport width (the label zone is no longer limited to the right half, and it no longer stops at the playable field), the old single-press rename strip at the right edge is removed, and a hovered bookmark (beat or audio) draws a faint band over exactly that hit area with a thicker line (`compose_bookmark_at` is shared by hover and double-click). Unit test `bookmark hit band spans the full width` added. Dragging a beat bookmark on its band (4 px threshold, no modifiers, not over a note or other element) now moves it: the UI sends `WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE` with the release y, and `move_compose_bookmark` in `compose_input.cpp` converts it through the Compose geometry and the shared snap row into one `update_bookmark_action_t` (one undo step). A press-release without movement falls through as an ordinary field click; Escape or a stale revision cancels; a ghost line follows the pointer. Audio-time bookmarks do not drag. The drag has no automated test (`compose_input.cpp` and the harness do not build in this environment). The Nuklear drawing code was not compiled in this session (needs Zar's SDL build); the event harness could not be configured here either.
- **Outcome:** in the real window, double-clicking a beat bookmark's line or label opens the T-371 inline rename field. An event-harness case drives the same click sequence the app receives.
- **Read first:** `src/ui/nuklear/compose_bookmark_rename.cpp` (`nearest_bookmark`), `src/ui/nuklear/compose_panel.cpp` (rename call sites), `src/ui/nuklear/compose_object_input.cpp`, `scripts/smoke-compose-virtual-x.sh` (copy it with a STEPS hook for xdotool clicks).
- **Files owned:** `src/ui/nuklear/compose_bookmark_rename.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Reproduce in isolated Xvfb with xdotool double-click on a bookmark label, and record what consumes the click (for example a note or selection handler first, or a hit area limited to `playable_field_right_pixels`).
  2. Fix the cause and record it on this card.
  3. Add an event-harness regression that fails before the fix.
- **Invariants:** a double-click on a note still opens the note label editor (T-391).
- **Reject/failure behavior:** a double-click on empty space does nothing.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L event-harness`; isolated Xvfb double-click check.
- **Out of scope:** audio-time bookmarks on the waveform, unless the same cause applies.
