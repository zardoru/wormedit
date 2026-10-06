# Frontend event harness (T-41)

[Back to building](building.md) · [Testing strategy](chart-editor-plan/testing.md) · [Task card T-41](chart-editor-plan/tasks/frontend-shell.md)

The harness drives the **real** Nuklear frontend with scripted SDL events and
no window, GPU, or display server. Use it to test input routing and UI
workflows: wheel/viewport input, shortcuts, tab clicks, modal dialogs.

It does not take screenshots (use `scripts/smoke-compose-virtual-x.sh` for
that), and it does not test IME. Events → actions establishes input routing, not
document mutation, dispatcher execution, OS input delivery, or GPU rendering.

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
4. `worm_ui_frontend_compose(&view)`, returning the borrowed production action
5. `worm_ui_frontend_render(width, height)`, which clears the frame
6. Copy the action into an owning snapshot before any subsequent compose

Each frame sets the explicitly supplied SDL modifier state for input and compose,
then restores the caller's previous state with a scope guard. Keyboard event
`key.mod` is also replaced by the supplied modifiers. No event queue is pumped,
and key-down does not implicitly carry modifiers into later frames.

No test-side action dispatcher exists. The snapshot preserves the action values
the application dispatcher would receive. For example, Delete arrives as a
shortcut event (`shortcut_events[0].hid_usage`, `repeat`, focus flags), not as a
`DELETE_SELECTION` action; the command registry resolves it later.

## API

| Call | Meaning |
| --- | --- |
| `nuklear_event_harness_t h(w, h)` | Default 960x720. Check `h.ready()` first. |
| `h.view()` | The `WormUiWorkspaceView` fed to every frame. Edit it to set up state (a Compose project is preloaded). |
| `h.frame(events, modifiers = SDL_KMOD_NONE)` | One frame with explicit modifier state. Returns `workspace_action_snapshot_t`. |
| `h.run_script(steps)` | One frame per `event_step_t`. Returns one owning snapshot per step; each step has `events` and `modifiers` (default none). |

`snapshot.get()` returns a const action reference. All eleven borrowed metadata
strings, the nested `project_chart.author`, `project_chart.rating_numerator`,
and `project_chart.rating_denominator` strings, and every search-tag string plus
its pointer array are owned by the snapshot. Null strings remain null; non-null
empty strings remain non-null and empty. Scalar fields and inline arrays are
copied by value. Copies share
immutable storage; copy/move construction, assignment, and vector relocation
preserve values beyond subsequent compose calls and frontend destruction. Keep
an owning snapshot alive while using `get()`; copying the raw C action from it
does not transfer ownership. A moved-from snapshot may only be destroyed or
assigned before use. Production actions retain their original next-compose
lifetime contract. New pointer-bearing action fields must extend this snapshot
and its ownership regression.

Rules:

- **Events apply on the frame they are fed.** Layout is computed at compose
  time, so always run one empty `h.frame()` first, before clicking a widget or
  opening a dialog, so the widget exists.
- A click is two steps (down, then up) so press and release edges stay FIFO and
  separate. Add a `SDL_EVENT_MOUSE_MOTION` to the same position in each step.
- Event and view pointer payloads remain caller-owned and must outlive the call; events are never retained.
- Specify `SDL_KMOD_LCTRL` on both the Ctrl-down step and every held-wheel step;
  specify `SDL_KMOD_NONE` on release and normal scrolling. Each step is an
  independent modifier snapshot, not a simulated physical keyboard state machine.
  Split steps if modifiers must change within a frame.
- Frontend state is process-global. One fixture per test, and let it destruct
  (it calls `worm_ui_frontend_shutdown`) before the next test. Do not overlap
  fixtures or run them concurrently in one process. SDL events initialization is
  reference-counted with `SDL_QuitSubSystem`, without quitting unrelated subsystems.
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
    CHECK(action.get().shortcut_event_count == 1);
}
```

Tag with `[frontend-state]`; CTest registers it as `frontend-state.<name>`.
Add new files to the `wormedit_frontend_event_harness_tests` target in
`tests/CMakeLists.txt`. Keep new helper functions under cognitive complexity 25.

## Running

The existing `headless` preset still has no SDL/frontend dependency. Opt in
without building the app using the dedicated preset. It needs CMake 4.2+, a
compiler, Ninja, zlib development headers, and the `third_party/libotoworm` submodule (or
`-DWORMEDIT_OTOWORM_SOURCE_DIR=/path/to/libotoworm`). CMake resolves pinned SDL3
and Nuklear sources:

```sh
cmake --preset headless-events
cmake --build --preset headless-events
ctest --preset headless-events --show-only=json-v1
env -u DISPLAY -u WAYLAND_DISPLAY ctest --preset headless-events -L event-harness --output-on-failure --no-tests=error
ctest --preset headless-events --output-on-failure --timeout 120
```

This sets `BUILD_TESTING=ON`, `WORMEDIT_BUILD_APP=OFF`, and
`WORMEDIT_BUILD_EVENT_HARNESS=ON`; X11/Wayland backends are disabled and
`SDL_UNIX_CONSOLE_BUILD=ON` allows that configuration. SDL's
dummy/offscreen video support can be compiled for linking, but the harness only
initializes events. The first case asserts video remains uninitialized. No
display, window, renderer, or GPU context is created.

Application builds also retain the harness:

```sh
cmake --build build --target wormedit_frontend_event_harness_tests
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build -L event-harness --output-on-failure --no-tests=error
```

CI uses `headless-events` only in the existing Linux/GCC lane, checks that at
least twelve harness cases are discovered, and runs them with the aggregate
suite. The other lanes retain `headless`; no new matrix entries or manual
Actions re-runs are needed. Each harness test has a 30-second timeout.

## Guidance for agents

- Use this windowless harness for input-routing checks, with explicit modifier
  steps to isolate SDL global state. Keep Xvfb for pixels.
- Do not invent a dispatcher in tests; assert on `WormUiWorkspaceAction` fields.
- If a widget cannot be reached without a window or renderer, report the gap
  rather than adding a test-only production branch.
- Never report a case as passing without running it and reading the output.

## Current cases

Wheel over Compose becomes viewport input; Delete press, release (ignored),
and repeat; tab click emits `SWITCH_TAB`; the New Project wizard suppresses
Delete, cancels and can reopen, and creates the default 7K layout.

Regressions additionally cover Ctrl-down/wheel/release versus ordinary scrolling,
restoration of ambient modifiers, every borrowed payload across ownership
operations (including nested project-chart strings, null and empty values), and
multiframe Metadata text history after later compose and shutdown.

The recursive action payload audit covers `project_chart`, `notifications`,
`options`, `tap_timing`, `external_player`, `compose_inputs`,
`compose_viewport_inputs`, `compose_recipe_form` (including its rational-text
members), `timing_target`, and `shortcut_events`. Only `project_chart` contains
nested borrowed pointers; the others contain scalars, enums, or inline arrays.
Together with the top-level metadata fields this is fourteen string pointers
plus the search-tag array and its elements. Future nested payload changes must
be included in the ownership audit, not only new top-level fields.
