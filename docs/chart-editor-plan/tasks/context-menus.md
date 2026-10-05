# Context menus

## T-380 Release Compose input after the area or SV recipe menu opens

- **Status:** done (2026-10-04; regression test and full suite pass)
- **Outcome:** right-clicking a selected Compose area keeps its menu on screen, and dismissing it returns pointer input to the canvas.
- **Read first:** `src/ui/nuklear/compose_panel.cpp` (`render_compose_editor_canvas`), `src/ui/nuklear/compose_area_menu.cpp`, `src/ui/nuklear/compose_recipe_menu.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`.
- **Cause:** `f2b35bd` made the occlusion gate in `render_compose_editor_canvas` discard Compose input while `compose_area_menu_consumed()` or `compose_recipe_menu_consumed()` held, but both menus are only processed behind that gate, from `collect_compose_viewport_inputs`. The menu drew once on its opening frame, then could neither render nor close, so every later canvas click was discarded. A live process showed `menu_open` and `consumed` set, no popup window, and `process_compose_area_menu` never called.
- **Files owned:** root: `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Service area and recipe menus that already own input before the occlusion gate, as the note menu does, and keep that frame occluded.
  2. Let `process_compose_area_menu` close an open menu when its range disappears, rather than returning before `render_menu`.
  3. Add an event-harness regression: the menu stays drawn after its opening frame, and an outside click closes it and releases the canvas.
- **Invariants:** an open menu still blocks canvas input; the closing gesture is still consumed; edge dragging of the area band is serviced while captured.
- **Reject/failure behavior:** stale project, chart or revision closes the menu without emitting an action.
- **Verify:** `cmake --build build`; `env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build --output-on-failure`; `clang-tidy -p build src/ui/nuklear/compose_panel.cpp src/ui/nuklear/compose_area_menu.cpp`.
- **Out of scope:** menu appearance and the shared menu component (T-381).

- **T-380 verification:** the new case fails on the unfixed source (menu not drawn, `menu_open` and `consumed` stuck) and passes with the fix. Full suite: 2,256/2,256.

## T-381 One shared context menu matching the menubar

- **Status:** done (2026-10-04; full suite and isolated real-window check pass)
- **Outcome:** every right-click action menu (Compose area, SV recipe, note, Layers panel, Sound group actions) uses one lifecycle component and draws like the File/Edit/View menus: the same rounded shadowed panel, padding, 25 px flat rows, and dimmed disabled rows that explain themselves on hover.
- **Read first:** `src/ui/nuklear/popup_lifecycle.h`, `src/ui/nuklear/chrome.h` (`menu_panel`, `apply_dropdown_theme`), `src/ui/nuklear/frontend.cpp` (`begin_menu_sized`, `end_menu`), the five menu sources below.
- **Files owned:** root: new `src/ui/nuklear/context_menu.h`, new `src/ui/nuklear/context_menu.cpp`, `src/ui/nuklear/chrome.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `src/ui/nuklear/compose_recipe_menu.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, new `tests/ui/context_menu_tests.cpp`, existing event-harness tests that read the moved state, this card, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Extract the menubar surface (panel paint and transparent window background) into `chrome.h` and use it from `begin_menu_sized`/`end_menu`.
  2. Add `ContextMenu`: open at an anchor with a context stamp, clamp into the parent content region, close on stale stamp, outside press or Escape, keep the release latch, retire a popup closed from outside its frame, and draw menubar-style items.
  3. Migrate the five menus one at a time, rebuilding and running the event harness after each.
  4. Add unit tests for the component lifecycle and item behavior.
- **Invariants:** menu actions, stamps and enable conditions are unchanged; closing gestures never reach covered widgets; the note layer submenu still pages its layer window.
- **Reject/failure behavior:** a disabled item never emits an action.
- **Verify:** `cmake --build build`; `env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build --output-on-failure`; `clang-tidy -p build <changed-source-file>`; `scripts/smoke-compose-virtual-x.sh` for a visual check of an open context menu.
- **Out of scope:** dialogs and pickers that are not action menus (renumber/replace confirmations, Go to measure/bookmark, bookmark rename, timing value, minimap preferences, BGA stack picker, sound note layers, filter popups).

- **Design notes:** `chrome::menu_surface_begin/end` and `chrome::menu_separator` now serve both the menubar and `ContextMenu`, so the two cannot drift apart. Disabled reasons moved from wrapped labels under each row to hover tooltips on the dimmed row, as in the menubar; the area menu shrinks from 340x390 to 220x186. Menus that open from list rows (Layers, Sound group actions) call `context_menu_update` before their rows so the opening press survives to the first `context_menu_begin`. Sound group reviews still open only after the requesting gesture is released. Popup identity uses the parent's hashed popup title, since Nuklear popup windows carry no name.
- **T-381 verification:** `cmake --build build` passes; all 2,262/2,262 CTest cases pass, including 151 event-harness cases (six new `Context menu` component cases covering opening press, clamping, outside/Escape/stale dismissal, the release latch, disabled dimming and non-activation, closing outside the frame and from a nested group). `clang-tidy -p build` reports no cognitive-complexity violations in any changed source; `git diff --check` is clean. An isolated Xvfb run of the real window drew the area menu with the menubar surface and dimmed disabled rows, kept it open after the right-click, closed it on an outside click, and the next canvas click cleared the selection.
