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
- **Out of scope:** menu appearance and a shared menu component.

- **T-380 verification:** the new case fails on the unfixed source (menu not drawn, `menu_open` and `consumed` stuck) and passes with the fix. Full suite: 2,256/2,256.
