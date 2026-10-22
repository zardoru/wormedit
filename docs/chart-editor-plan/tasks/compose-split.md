# Task cards: Compose panel split

[Back to the task-card index](../tasks.md).

`src/ui/nuklear/compose_panel.cpp` is about 4,700 lines and mixes four
responsibilities. These cards move code without changing behavior, one
responsibility at a time. Do them in order; each leaves a green build. Line
numbers below are approximate and were taken from the file as of 2026-09-29, so
locate functions by name.

Shared rules for every card in this file:

- Pure move. No renaming, no signature changes, no logic edits, no reformatting of moved bodies.
- Moved `static` helpers become `static` in the new `.cpp`. Only functions the remaining file still calls go in the new header, in namespace-free C style matching `compose_markers.h`.
- Add each new `.cpp` beside `src/ui/nuklear/compose_markers.cpp` in the source list in `CMakeLists.txt` (near line 315). If the new file has state-only logic covered by `tests/ui`, also add it to `wormedit_frontend_state_tests` in `tests/CMakeLists.txt`.
- The architecture test `architecture.ui_frontend_boundary` must still pass: no Nuklear types outside `src/ui/nuklear`.
- Shared file-scope values (`state`, `compose_measure_rail_width`, paint/toolbar/inspector context structs) move to a private header `compose_panel_internal.h` in the first card that needs them.

## T-01 Extract the Compose canvas painter

- **Status:** done (2026-09-29; `compose_panel.cpp` 4,659 to 3,497 lines)
- **Outcome:** all `draw_compose_*` scene painting lives in `compose_canvas.cpp`; `compose_panel.cpp` shrinks by roughly 1,200 lines and the app renders identically.
- **Read first:** `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_markers.h`, `src/ui/nuklear/compose_state.h`, `CMakeLists.txt`.
- **Files owned:** `src/ui/nuklear/compose_panel.cpp`, new `src/ui/nuklear/compose_canvas.cpp`, new `src/ui/nuklear/compose_canvas.h`, new `src/ui/nuklear/compose_panel_internal.h`, `CMakeLists.txt`.
- **Steps:**
  1. Create `compose_panel_internal.h` and move in `ComposePaintContext`, `compose_measure_rail_width`, and the declaration of the `state` accessor usage (`worm_ui_nuklear_compose_state()`), unchanged.
  2. Create `compose_canvas.cpp` and move, in original order, every function from `compose_screen_rect` through `draw_compose_scene_canvas` (approx. lines 354–1520), plus `compose_lane_color`, the tooltip helpers, and `compose_editor_status_hint`.
  3. Declare only `draw_compose_scene_canvas` (and any function still called from the remaining file) in `compose_canvas.h`.
  4. Add the new `.cpp` to `CMakeLists.txt` and include the new headers in `compose_panel.cpp`.
  5. Build; fix only include/link errors.
- **Invariants:** hit-testing and drawing keep using the same geometry helpers. No change to draw order.
- **Reject/failure behavior:** if any moved function needs a file-scope variable not covered in step 1, STOP and report its name rather than duplicating it.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^(frontend-state|architecture|documentation)\.'`; `clang-tidy -p build src/ui/nuklear/compose_canvas.cpp` reports no new cognitive-complexity warning versus the pre-move function; `scripts/smoke-compose-virtual-x.sh /tmp/t01.png compose` produces a PNG showing the Compose field.
- **Out of scope:** toolbar, inspector, input, validation functions, any behavior change.

## T-02 Extract the Compose toolbar

- **Status:** done (2026-09-30; toolbar and exclusive helpers moved without behavior changes)
- **Outcome:** `render_compose_toolbar`, `render_compose_view_controls`, and `render_compose_bgm_scroll_controls` live in `compose_toolbar.cpp`.
- **Read first:** `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_panel_internal.h`, `src/ui/nuklear/chrome.h`.
- **Files owned:** `src/ui/nuklear/compose_panel.cpp`, new `compose_toolbar.cpp`, new `compose_toolbar.h`, `compose_panel_internal.h`, `CMakeLists.txt`.
- **Steps:**
  1. Move `ComposeToolbarContext` into `compose_panel_internal.h`.
  2. Move the three toolbar functions (approx. lines 1538–1615) and any helper only they call.
  3. Declare `render_compose_toolbar` in `compose_toolbar.h`; update includes and CMake.
- **Invariants:** the toolbar stays sticky, and recovery chrome stays absent when non-actionable.
- **Reject/failure behavior:** as in T-01.
- **Verify:** same commands as T-01, capture `/tmp/t02.png`.
- **Out of scope:** any new toolbar control.

## T-03 Extract the Compose inspector

- **Status:** done (2026-09-30; inspector functions moved without behavior changes)
- **Outcome:** every `render_*` inspector function (BPM, timing-lane visibility, BMSON regions, marker/section, note label, semantic layer palette) lives in `compose_inspector.cpp`.
- **Read first:** `src/ui/nuklear/compose_panel.cpp`, `compose_panel_internal.h`, `compose_resources.h`.
- **Files owned:** `compose_panel.cpp`, new `compose_inspector.cpp`, new `compose_inspector.h`, `compose_panel_internal.h`, `CMakeLists.txt`.
- **Steps:**
  1. Move `ComposeInspectorContext` and all functions between `render_initial_bpm_control` and `render_semantic_layer_target_buttons` (approx. lines 1615–2090), preserving the forward declarations that `render_semantic_layer_membership_details` needs.
  2. Declare only the entry points `compose_panel.cpp` calls.
  3. Update includes and CMake.
- **Invariants:** revision-checked actions still emitted through the same `WormUiWorkspaceAction` fields.
- **Reject/failure behavior:** as in T-01.
- **Verify:** as T-01, capture `/tmp/t03.png` and confirm the right inspector renders.
- **Out of scope:** narrowing widgets (see T-05).

## T-04 Extract Compose pointer and keyboard input

- **Status:** done (2026-09-30; input and gesture helpers moved with identical bodies; user authorized moving the snap-step constant)
- **Outcome:** `append_compose_input`, key/wheel/pointer processing, beat-fit drag, and overview input live in `compose_input_panel.cpp`; `compose_panel.cpp` keeps only `worm_ui_nuklear_*` entry points and view validation.
- **Read first:** `compose_panel.cpp`, `compose_panel_internal.h`, `src/sys/compose/input/compose_input.h` (do not edit; only avoid a name clash).
- **Files owned:** `compose_panel.cpp`, new `compose_input_panel.cpp`, new `compose_input_panel.h`, `compose_panel_internal.h`, `CMakeLists.txt`.
- **Steps:**
  1. Move the key-processing group (approx. lines 2681–2890), pointer-focus and viewport-cursor group (approx. 3077–3210), beat-fit group (3214–3310), and overview input group (3402–3520).
  2. Keep `worm_ui_nuklear_reset_compose_panel`, `..._queue_compose_middle_click`, `..._handle_focus_lost`, and `..._discard_frame_input` in `compose_panel.cpp`, delegating.
  3. Update includes and CMake.
- **Invariants:** gestures are cancelled on focus loss, revision change, and capacity loss exactly as before.
- **Reject/failure behavior:** as in T-01.
- **Verify:** as T-01, plus `ctest --test-dir build -R '^core\.'`; capture `/tmp/t04.png`.
- **Out of scope:** new modes such as momentum or autoscroll wiring, which belong to interaction work.
