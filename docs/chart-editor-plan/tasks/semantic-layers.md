# Task cards: Compose color views and semantic-layer UX

[Back to the task-card index](../tasks.md).

User request (2026-10-03): the semantic-layer UI should be easier to use.
Compose gets three note color **views** (Normal for layout key colors, Snap for
StepMania-style beat colors, Layer for semantic-layer colors) in their own
toolbar group. The toolbar is regrouped with separators, and tools become
icons. The paged layer buttons in the inspector become a dock panel listing the
layers, with a "No layer" row, a right-click menu on layer rows, and a "Layer ▸"
submenu in a note right-click menu.

User decisions (2026-10-03):

- "No layer" is a target that unassigns. While it is the target, Assign and
  layer paint remove notes from their layer. In Layer view, unassigned notes use
  its neutral swatch. It cannot be soloed or hidden.
- The layer list is a toggleable dock panel like the Sounds list. Rows scroll,
  so paging goes away.
- Two pop-ups: right-click on a layer row, and a "Layer ▸" submenu when
  right-clicking notes. No toolbar layer dropdown.
- One layer per note stays ([open question 10](../decisions/open-questions.md)).

Current state (2026-10-03): tile notes are already filled with their layer color
whenever the layer is visible, and arrow notes get a 4 px layer stroke
(`compose_note_style`, `draw_compose_arrow_outline` in
`src/ui/nuklear/compose_canvas.cpp`). Every endpoint already projects
`snap_class` and `key_color`. The toolbar is one flat template row
(`render_compose_toolbar` in `src/ui/nuklear/compose_toolbar.cpp`). The layer
UI is `render_semantic_layer_*` in `src/ui/nuklear/compose_inspector.cpp`.

The committed Sounds-tab work ([sounds-tab](sounds-tab.md)) edits
`compose_toolbar.cpp`, `compose_panel.cpp`, `compose_state.*`, `frontend.h` and
`vector_icons.h`. That work was checkpointed in `f256e99`. Apply the
stale-plan check (rule 9) first.

## T-336 Switch note colors between Normal, Snap and Layer views

- **Status:** done (2026-10-03)
- **Outcome:** a frontend-local Compose color view chooses note fills. Normal uses the lane key color, as today minus the layer fill. Snap uses `arrow_snap_color(snap_class)` for tile notes and hold bodies too. Layer fills each note with its layer color and uses a neutral grey for unassigned notes. Hover, selection outline, previews and ghosts look the same in every view.
- **Implementation notes (2026-10-03):** Normal/Snap/Layer are frontend-local views; hold snap classes are projected explicitly and frontend raster keys include the view. The fill matrix and transient-arrow outline tests, full frontend-state suite (124 cases), build, and affected-file clang-tidy passed with no new complexity violations. Isolated BMS-derived PNGs verified key, snap and layer fills on notes and a hold body.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`compose_note_style`, `draw_compose_arrow_outline`, hold body fill near `hold.semantic_color_visible`), `src/ui/nuklear/arrow_glyph.h`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/theme.h`.
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/theme.h`, `src/ui/nuklear/theme.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `tests/CMakeLists.txt` (ownership expansion approved 2026-10-03).
- **Steps:**
  1. Add `enum class worm_ui_nuklear_compose_color_view_t { normal, snap, layer }` and a `color_view` field (default `normal`) to the Compose frontend state. It survives chart switches and is not saved.
  2. Extract a pure `compose_note_base_fill(color_view, note)` helper that returns the key, snap or layer fill. `compose_note_style` and the hold-body fill call it, and the state-specific overrides (hover, previews, mine/fake kinds, tail) still apply on top.
  3. Arrow glyphs: Normal and Snap keep the snap fill. Layer fills with the layer color. Drop the 4 px layer stroke outside Layer view.
  4. Add the neutral unassigned color as a theme token, `semantic_layer_none`.
  5. State tests: the helper returns the expected fill for each view × {assigned, unassigned, hidden layer} × {tile, arrow}.
- **Invariants:** hit areas, core scene reuse keys and the document are unchanged; the frontend raster-cache key includes the color view. A hidden layer's notes draw as unassigned in Layer view.
- **Reject/failure behavior:** an unknown `snap_class` uses the existing "other" grey.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; `clang-tidy -p build src/ui/nuklear/compose_canvas.cpp`; isolated smoke PNGs of a BMS chart with one assigned layer in each view (set the default to each view temporarily, or use T-337 once it lands).
- **Out of scope:** toolbar buttons (T-337); the key-color theme table (T-270).

## T-337 Regroup the Compose toolbar with separators and a view group

- **Status:** done (2026-10-03)
- **Outcome:** the toolbar reads left to right as groups divided by 1 px vertical separators. Navigation: zoom out/in, reset. Tools: select/note paint, edit snap, timing focus, loop selection. View: a three-button segment (Normal, Snap, Layer) whose active button is checked. Panels: inspector, sounds list, layers list (T-339 adds the button), song bar. Audio offset. BPM snap. BGM columns. Every tool is an icon button with a tooltip.
- **Implementation notes (2026-10-03):** The toolbar now groups navigation, tools, color views, panels, audio offset, BPM snap and BGM columns with separators. Compact icon columns fit at 880 px; the offset and BGM labels hide below 900 px and the zoom readout below 1100 px. The template limit is 64. The harness verifies actual view-button clicks without document actions; integrated event-harness checks (14 cases), build and clang-tidy pass. Real-window PNGs at 1280 and 880 px were inspected.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp` (`render_compose_toolbar`, `render_compose_view_controls`), `src/ui/nuklear/chrome.h` (`IconButton`, `DrawnIconButton`, `layout_columns`), `src/ui/nuklear/vector_icons.h`, `src/ui/nuklear/lucide_icons.h`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/chrome.h`, `src/ui/nuklear/chrome.cpp`, `src/ui/nuklear/vector_icons.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/nuklear_config.h` (necessary supporting-file ownership approved 2026-10-03).
- **Steps:**
  1. Add `chrome::toolbar_separator(context)`: one 9 px template column that draws a 1 px line in the theme border color. Add a `layout_separator` helper next to `IconButton::layout_columns`.
  2. Split `render_compose_view_controls` into `render_navigation_group`, `render_tool_group`, `render_color_view_group` and `render_panel_group`, in that order, with a separator between groups. Each function stays at or below cognitive complexity 25.
  3. View group: draw a vector icon for each view (keys, a snap fraction mark, stacked layers). Clicking one sets `state.color_view`. Tooltips: "Normal view: layout colors", "Snap view: beat snap colors", "Layer view: semantic layer colors".
  4. Rebuild the template row in the same order. Below 900 px window width, drop the offset readout (as today) and then the BGM-column label.
  5. Event-harness case: clicking each view button changes `color_view` and leaves the document revision unchanged.
- **Invariants:** every existing toolbar action keeps its action kind and enabled conditions.
- **Reject/failure behavior:** disabled buttons stay disabled and show their tooltip.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; clang-tidy on `compose_toolbar.cpp`; isolated `scripts/smoke-compose-virtual-x.sh` PNGs at 1280 and 880 px wide.
- **Out of scope:** the left menu bar (T-226); keyboard shortcuts for views.

## T-338 Remove notes from whatever layer they belong to

- **Status:** done (2026-10-03)
- **Outcome:** a core `clear_semantic_layer_membership_action_t{notes}` removes each listed note from its current layer in one undo step. Already-unassigned notes are a no-op, and the whole action rejects atomically if any listed note is in a locked layer.
- **Implementation notes (2026-10-03):** appended the clear-membership action without changing existing variant indices; all selected notes and their current layers are checked before mutation. Mixed-layer clears use one undo entry, locked members reject atomically, and already-unassigned notes retain the existing no-op result. Regression cases cover undo/redo, gameplay preservation, locked layers, no-op clears, stale revisions, unknown chart/note IDs, duplicate selections, and selection caps.
- **Read first:** `include/wormedit/core/semantic_layer_editor.h`, `src/core/semantic_layer_editor.cpp`, `tests/core/semantic_layer_editor_tests.cpp`.
- **Files owned:** `include/wormedit/core/semantic_layer_editor.h`, `src/core/semantic_layer_editor.cpp`, `tests/core/semantic_layer_editor_tests.cpp`.
- **Steps:**
  1. Append the action to `semantic_layer_edit_action_t` (at the end, to keep variant indices).
  2. Implement it with the same revision, chart and note-ID checks as `remove_notes_from_semantic_layer_action_t`.
  3. Tests: mixed layers cleared in one revision; undo restores each note's original layer; a locked member rejects with the document unchanged; all notes unassigned → typed no-op rejection like the other membership actions.
- **Invariants:** gameplay fields and note IDs are unchanged; one history entry.
- **Reject/failure behavior:** a stale revision, an unknown note or a locked layer leaves the document and history untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (T-340).

## T-339 List semantic layers in a dock panel

- **Status:** done (2026-10-03)
- **Outcome:** a toolbar button (panel group) toggles a Layers dock panel beside the Compose canvas. It lists every layer of the chart in order. Each row has a color swatch, the name, the note count, an eye toggle and a lock toggle. Clicking a row makes it the target, and with notes selected it also assigns them, as today. The list scrolls instead of paging. The inspector keeps only the membership buttons, paint and solo for the current target.
- **Implementation notes (2026-10-03):** The dock uses an all-layer bounded scroll window (including hidden rows) with independent target projection, row-specific eye/lock actions and generated Lucide glyphs. The stale page-shape validator was replaced with scroll-window invariants, with a separate 256-row UI bound while legacy shortcut palette behavior stays unchanged. Build, 126 frontend/application checks, all 14 event-harness cases, explicit lock-history and core window tests, and affected-file clang-tidy passed with no new complexity violations. An isolated 30-layer PNG shows rows 9–30 after scrolling. Rename/color/reorder/filter controls migrate in T-341.
- **Read first:** `src/ui/nuklear/compose_sounds_panel.cpp` (dock panel pattern), `src/ui/nuklear/compose_inspector.cpp` (`render_semantic_layer_*`), `include/wormedit/core/semantic_layer_editor.h` (`make_semantic_layer_palette_window`), `src/sys/compose_view.cpp` (semantic layer projection).
- **Files owned:** new `src/ui/nuklear/compose_layers_panel.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/lucide_icons.h`, `src/ui/nuklear/lucide_icons.cpp`, `src/ui/nuklear/lucide_icon_pixels.h`, `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `src/sys/compose_actions.cpp`, `CMakeLists.txt`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/core/semantic_layer_editor.h`, `src/core/semantic_layer_editor.cpp`, `tests/core/semantic_layer_editor_tests.cpp`, `src/sys/compose_view.h`, `scripts/generate-lucide-icons.py`, `assets/icons/lucide/lock.svg`, `assets/icons/lucide/lock-open.svg`, `assets/icons/lucide/eye-off.svg`, `tests/sys/frame_timing_tests.cpp` (ownership expansion approved 2026-10-03).
- **Steps:**
  1. Project a row window from the panel's first visible row and row count (reuse `make_semantic_layer_palette_window`), and remove Previous/Next paging from the inspector.
  2. Add `WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK` and dispatch it through `update_semantic_layer_action_t`. The eye uses the existing visibility toggle for that row's layer ID.
  3. Add `lock`, `lock_open` and `eye_off` Lucide glyphs.
  4. Draw the panel with a fixed header ("Layers", count, an add-layer icon button) and scrolling rows. The target row is highlighted.
  5. Add a `layers_panel_visible` state field and its toolbar toggle (`Glyph::layers`).
  6. Tests: lock round trip and undo (application commands); the harness clicks a row and checks the target, then clicks the eye.
- **Invariants:** layer order and IDs are unchanged. The panel never edits the document except through existing actions.
- **Reject/failure behavior:** with a locked layer, rename, delete and assignment stay disabled, as today.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.|application_commands'`; `ctest --test-dir build -L event-harness`; clang-tidy on new and changed `.cpp`; isolated smoke PNG with 30 layers scrolled.
- **Out of scope:** the "No layer" row (T-340); the row menu (T-341).

## T-340 Add a "No layer" target that unassigns

- **Status:** done (2026-10-03)
- **Outcome:** the first Layers panel row is "No layer" with the neutral swatch and the count of unassigned notes. When it is the target, "Assign" (and clicking the row with notes selected) runs T-338's clear action, and layer paint clears each painted note's layer. It has no eye, lock, solo or row menu.
- **Implementation notes (2026-10-03):** the synthetic No layer target clears assignments and captured paint strokes atomically; successful/no-op assignments bind the target, while rejected clears preserve target and selection. Modern UI and command assignments replace membership, including source-lock checks, while legacy additive core actions remain available. Command registry and keyboard target setters now honor the three-way target. Indexed source-layer checks avoid selected-note × catalog scans. Build, 127 frontend/application checks, all 1191 core tests, focused helper and command-dispatch tests, and affected-file clang-tidy passed with no new complexity violations. Isolated Assign→No layer PNGs verified neutral fill, unchanged selection outline, correct counts and target highlight.
- **Read first:** `src/ui/nuklear/compose_layers_panel.cpp`, `src/sys/compose_actions.cpp` (`semantic_layer_target`, paint dispatch), `src/sys/compose_view.h`, `include/wormedit/core/semantic_layer_editor.h`.
- **Files owned:** `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/frontend.h`, `src/sys/compose_view.h`, `src/sys/compose_view.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`, `tests/sys/application_commands_tests.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose_semantic_layer_commands.h`, `include/wormedit/core/semantic_layer_editor.h`, `src/core/semantic_layer_editor.cpp`, `tests/core/semantic_layer_editor_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/main.cpp` (necessary supporting-file ownership approved 2026-10-03).
- **Steps:**
  1. Represent the target as a three-way choice: none chosen, "No layer", or a layer ID. Project it as `semantic_layer_target_none` plus the unassigned count.
  2. Route Assign, row click and layer paint to the clear action when the target is "No layer". Remove and Toggle stay disabled, with the hint "No layer only assigns".
  3. Tests: assign and paint with "No layer" clear mixed memberships in one undo step; a locked member rejects with a status message.
- **Invariants:** one layer per note.
- **Reject/failure behavior:** a rejected clear leaves the selection and target unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'application_commands|^frontend-state\.'`; isolated Xvfb: assign, then "No layer", then the note uses the neutral unassigned swatch in Layer view (consistent with T-336 and the recorded user decision).
- **Out of scope:** solo/hide for unassigned notes.

## T-341 Open a menu on right-click of a layer row

- **Status:** done (2026-10-03)
- **Outcome:** right-clicking a layer row opens a popup with Rename…, Color…, Show/Hide, Solo, Lock/Unlock, Select notes, Require filter, Exclude filter, Move up, Move down, Delete, and Delete + unlink. Each entry calls the existing action for that row's layer and is disabled with a reason when its action would reject. Rename and Color open inline editors in the panel. The inspector's rename, color, reorder and filter controls are removed.
- **Implementation notes (2026-10-03):** The captured-ID row menu and inline Rename/Color editors replace inspector catalog controls. Disabled entries show lock/gesture/order/delete reasons. A retained captured-layer snapshot keeps inline editors stable across target and scroll-window changes. Escape, outside press and release, stale revisions, panel hiding, tab/focus transitions, overlapping notifications and global/text shortcuts are handled without leaked canvas input or retained popup capture. Build, the final 30-case event harness, the full 1764-test integrated suite and affected-file clang-tidy passed with no new complexity violations. Isolated menu/Rename/Hide PNGs verified the popup, editor, eye state and neutral hidden-layer fills.
- **Read first:** `src/ui/nuklear/compose_recipe_menu.cpp` (popup pattern), `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp` (`render_semantic_layer_selected_actions`, `render_semantic_layer_color_editor`).
- **Files owned:** `src/ui/nuklear/compose_layers_panel.cpp/.h`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/frontend.cpp` (necessary supporting-file ownership approved 2026-10-03).
- **Steps:**
  1. Right-click makes the row the target, then opens the popup through the central popup guard (T-264).
  2. Move the rename field and color picker from the inspector into the panel, opened from the menu.
  3. Escape or a click outside closes the menu without an action.
  4. Harness case: right-click a row, choose Hide, and the layer's eye shows it hidden; Escape closes the menu with no action.
- **Invariants:** the menu acts on the right-clicked row's layer ID, captured with the document revision. A stale revision closes the menu.
- **Reject/failure behavior:** a disabled entry shows its reason and does nothing.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; clang-tidy.
- **Out of scope:** the note menu (T-342).

## T-342 Assign layers from a note right-click submenu

- **Status:** done (2026-10-03)
- **Outcome:** right-clicking a note opens a note menu with "Layer ▸". If the note is not selected, it becomes the only selection first. The submenu lists "No layer" and then every layer with its swatch. Choosing one assigns or clears the whole selection in one undo step. Locked layers are disabled with "Layer is locked". Right-click on empty space inside the area band still opens the area menu.
- **Implementation notes (2026-10-03):** Safe context selection opens the note popup before area/recipe handling, preserving existing multi-note selections. Its independently projected catalog contains at most 18 rows and no rows while closed; scrolling includes hidden layers and leaves the dock window unchanged. One popup surface contains the primary menu and scrollable Layer pane, with primitive swatches/chevron and disabled locked targets. Assignment/clear uses the atomic one-step commands and closes before reporting rejection. Stale capture, chart/project/focus changes, Escape, outside presses and notification overlap retire capture without canvas or shortcut leakage. Build, all 30 event-harness tests, application/malformed-view tests, production projection bounds/capture tests and the full 1764-test suite passed. Affected-file clang-tidy found no new complexity violations. Isolated PNGs verified the dock-hidden submenu, layer 30 scrolling, assignment and No layer clearing with selection preserved.
- **Read first:** `src/ui/nuklear/compose_area_menu.cpp` (right-click precedence), `src/ui/nuklear/compose_recipe_menu.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/sys/compose_input.cpp` (note hit testing).
- **Files owned:** new `src/ui/nuklear/compose_note_menu.cpp/.h`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `src/ui/frontend.h`, `src/sys/compose_actions.cpp`, `CMakeLists.txt`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose_view.cpp`, `src/sys/workspace_projection.h`, `src/sys/compose_view.h`, `tests/sys/application_commands_tests.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/compose_semantic_layer_commands.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp` (necessary supporting-file ownership approved 2026-10-03).
- **Steps:**
  1. Hit-test notes on a right press. A note hit opens the note menu, and the area band and recipe menus receive the press only when no note was hit.
  2. Add an action carrying the chosen layer ID or "No layer", dispatched to assign or clear.
  3. Draw the submenu as a separately scrollable pane to the right of the primary menu on one retained popup surface (Nuklear prohibits nested popups), clamped to the window, scrolling past 16 rows.
  4. Harness cases: right-click an unselected note → Layer → a layer assigns only that note; right-click on band empty space still opens the area menu.
- **Invariants:** left-click, drag and paint behavior are unchanged.
- **Reject/failure behavior:** a rejected assignment keeps the menu closed and reports through notifications.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`; full `ctest --test-dir build --output-on-failure`; clang-tidy.
- **Out of scope:** other note-menu entries (later cards add them here).
