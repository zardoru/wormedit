# Frontend event harness (T-41)

[Back to building](building.md) · [Testing strategy](chart-editor-plan/testing.md) · [Task card T-41](chart-editor-plan/tasks/frontend-shell.md)

The harness drives the **real** Nuklear frontend with scripted SDL events and
no window, GPU, or display server. Use it to test input routing and UI
workflows: wheel/viewport input, shortcuts, tab clicks, modal dialogs.

It does not take screenshots (use `scripts/smoke-compose-virtual-x.sh` for
that), and it does not test IME.

## How it works

Files: `tests/ui/nuklear_event_harness.h/.cpp` (fixture) and
`tests/ui/nuklear_event_harness_tests.cpp` (cases, the examples to copy).

`nuklear_event_harness_t` initializes the production frontend through
`worm_ui_frontend_init_headless(width, height)` (a fixed font, a null renderer,
and only `SDL_INIT_EVENTS`; SDL video stays uninitialized). Each **frame** runs
the same calls as the live loop:

1. `worm_ui_frontend_begin_input()`
2. `worm_ui_frontend_handle_event()` for each scripted `SDL_Event`
3. `worm_ui_frontend_end_input()`
4. `worm_ui_frontend_compose(&view)`, returning the `WormUiWorkspaceAction`
5. `worm_ui_frontend_render(width, height)`, which clears the frame

No test-side action dispatcher exists. The action you get back is exactly what
the application dispatcher would receive. For example, Delete arrives as a
shortcut event (`shortcut_events[0].hid_usage`, `repeat`, focus flags), not as a
`DELETE_SELECTION` action; the command registry resolves it later.

## API

| Call | Meaning |
| --- | --- |
| `nuklear_event_harness_t h(w, h)` | Default 960x720. Check `h.ready()` first. |
| `h.view()` | The `WormUiWorkspaceView` fed to every frame. Edit it to set up state (a Compose project is preloaded). |
| `h.frame(events)` | One frame with the given events. Returns that frame's action. |
| `h.run_script(steps)` | One frame per `event_step_t`. Returns one action per step. |

Rules:

- **Events apply on the frame they are fed.** Layout is computed at compose
  time, so always run one empty `h.frame()` first, before clicking a widget or
  opening a dialog, so the widget exists.
- A click is two steps (down, then up) so press and release edges stay FIFO and
  separate. Add a `SDL_EVENT_MOUSE_MOTION` to the same position in each step.
- Event pointer payloads must outlive the call; events are never retained.
- Frontend state is process-global. One fixture per test, and let it destruct
  (it calls `worm_ui_frontend_shutdown`) before the next test.
- Coordinates are window pixels. Hard-coded widget positions (tab at 80,54;
  wizard buttons at y=406) are brittle: when a layout changes, update the
  numbers and the comment explaining them.

## Writing a test

```cpp
TEST_CASE("Headless frontend ...", "[frontend-state][frontend][event-harness]") {
    nuklear_event_harness_t harness;
    REQUIRE(harness.ready());
    static_cast<void>(harness.frame());   // lay out widgets first

    const auto press = key_event(SDL_EVENT_KEY_DOWN, SDLK_DELETE, SDL_SCANCODE_DELETE);
    const auto action = harness.frame(std::span{&press, 1});
    CHECK(action.shortcut_event_count == 1);
}
```

Tag with `[frontend-state]`; CTest registers it as `frontend-state.<name>`.
Add new files to the `wormedit_frontend_event_harness_tests` target in
`tests/CMakeLists.txt`. Keep new helper functions under cognitive complexity 25.

## Running

The target exists only when the application is configured
(`WORMEDIT_BUILD_APP` and SDL3 available), but needs no display at runtime:

```sh
cmake --build build --target wormedit_frontend_event_harness_tests
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build --output-on-failure -R '^frontend-state\.Headless'
```

Unsetting the display variables is the proof that no window is used; the first
case also asserts `SDL_WasInit(SDL_INIT_VIDEO) == 0`.

### Building on a machine without X11/Wayland headers

Add `-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_UNIX_CONSOLE_BUILD=ON` when
configuring. The build also needs the sibling checkout `../libotoworm` and
CMake 4.2 or newer (libotoworm's requirement).

## Guidance for agents

- Prefer this harness over launching a window for input-routing checks; it is
  fast (about 10 ms per case) and deterministic. Keep Xvfb for pixels.
- Do not invent a dispatcher in tests; assert on `WormUiWorkspaceAction` fields.
- If a widget cannot be reached without a window or renderer, report the gap
  rather than adding a test-only production branch.
- Never report a case as passing without running it and reading the output.

## Current cases

Wheel over Compose becomes viewport input; Delete press, release (ignored),
and repeat; tab click emits `SWITCH_TAB`; the New Project wizard suppresses
Delete, cancels and can reopen, and creates the default 7K layout.
