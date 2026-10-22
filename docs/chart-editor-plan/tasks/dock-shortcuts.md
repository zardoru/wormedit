# Dock and layer shortcuts

[Back to task cards](../tasks.md).

## T-498 Open Layers with a remappable shortcut

- **Status:** done
- **Stale-plan audit (2026-10-09):** toolbar dock toggles and the undoable Add layer action exist; no registry commands for these three behaviors exist. Timing Lock now uses Primary+Shift+T (`04fafad`).
- **Outcome:** Primary+L opens Layers, including narrow Compose layouts, without creating Undo entries.
- **Read first:** `src/core/command_registry.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/app/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `docs/chart-editor-plan/tasks/dock-shortcuts.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Supporting scope extension (coordinator):** `src/ui/nuklear/compose_panel.cpp` exposes its existing compact-layout decision to the dock callback so opening Layers reliably reveals it without duplicating width thresholds. `src/ui/nuklear/compose_layers_panel.cpp` reuses the existing target reveal scrolling when the shared Add action selects a newly appended layer, covering offscreen creation in large catalogs.
- **Shared integration scope:** these three sequential cards share registry/callback plumbing; the coordinator owns all edits.
- **Steps:**
  1. Register the typed remappable command and retain its frontend callback across project resets.
  2. Apply the dock request through the existing Compose state/action boundary, guarding input ownership and stale contexts.
  3. Verify dispatch, failure guards and production frontend events; record completion and update Brain.
- **Invariants:** platform Primary modifiers; exact modifiers; no held-key repetition; opening/toggling docks does not edit charts; creating layers uses the existing action and naming/color/target/Undo rules.
- **Reject/failure behavior:** inactive Compose, modal/text/IME input, pending gestures/paste and stale queued creation cannot mutate the chart or leak input.
- **Verify:** `cmake --build build -j 8`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness --output-on-failure`; `clang-tidy -p build` on affected C++ sources, no new/increased complexity above 25.
- **Out of scope:** grid colors, snap badge, navigation repeat changes, general command palette, Sounds tab redesign.

## T-499 Create a layer with a remappable shortcut

- **Status:** done
- **Stale-plan audit (2026-10-09):** toolbar dock toggles and the undoable Add layer action exist; no registry commands for these three behaviors exist. Timing Lock now uses Primary+Shift+T (`04fafad`).
- **Outcome:** Primary+Shift+L reveals Layers and queues the existing revision-checked Add semantic layer action for one Undo.
- **Read first:** `src/core/command_registry.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/app/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `docs/chart-editor-plan/tasks/dock-shortcuts.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Supporting scope extension (coordinator):** `src/ui/nuklear/compose_panel.cpp` exposes its existing compact-layout decision to the dock callback so opening Layers reliably reveals it without duplicating width thresholds. `src/ui/nuklear/compose_layers_panel.cpp` reuses the existing target reveal scrolling when the shared Add action selects a newly appended layer, covering offscreen creation in large catalogs.
- **Shared integration scope:** these three sequential cards share registry/callback plumbing; the coordinator owns all edits.
- **Steps:**
  1. Register the typed remappable command and retain its frontend callback across project resets.
  2. Apply the dock request through the existing Compose state/action boundary, guarding input ownership and stale contexts.
  3. Verify dispatch, failure guards and production frontend events; record completion and update Brain.
- **Invariants:** platform Primary modifiers; exact modifiers; no held-key repetition; opening/toggling docks does not edit charts; creating layers uses the existing action and naming/color/target/Undo rules.
- **Reject/failure behavior:** inactive Compose, modal/text/IME input, pending gestures/paste and stale queued creation cannot mutate the chart or leak input.
- **Verify:** `cmake --build build -j 8`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness --output-on-failure`; `clang-tidy -p build` on affected C++ sources, no new/increased complexity above 25.
- **Out of scope:** grid colors, snap badge, navigation repeat changes, general command palette, Sounds tab redesign.

## T-500 Toggle Sounds with a remappable shortcut

- **Status:** done
- **Stale-plan audit (2026-10-09):** toolbar dock toggles and the undoable Add layer action exist; no registry commands for these three behaviors exist. Timing Lock now uses Primary+Shift+T (`04fafad`).
- **Outcome:** Primary+Shift+K toggles the Sounds dock independently of Primary+K Go to sound.
- **Read first:** `src/core/command_registry.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/app/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `docs/chart-editor-plan/tasks/dock-shortcuts.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Supporting scope extension (coordinator):** `src/ui/nuklear/compose_panel.cpp` exposes its existing compact-layout decision to the dock callback so opening Layers reliably reveals it without duplicating width thresholds. `src/ui/nuklear/compose_layers_panel.cpp` reuses the existing target reveal scrolling when the shared Add action selects a newly appended layer, covering offscreen creation in large catalogs.
- **Shared integration scope:** these three sequential cards share registry/callback plumbing; the coordinator owns all edits.
- **Steps:**
  1. Register the typed remappable command and retain its frontend callback across project resets.
  2. Apply the dock request through the existing Compose state/action boundary, guarding input ownership and stale contexts.
  3. Verify dispatch, failure guards and production frontend events; record completion and update Brain.
- **Invariants:** platform Primary modifiers; exact modifiers; no held-key repetition; opening/toggling docks does not edit charts; creating layers uses the existing action and naming/color/target/Undo rules.
- **Reject/failure behavior:** inactive Compose, modal/text/IME input, pending gestures/paste and stale queued creation cannot mutate the chart or leak input.
- **Verify:** `cmake --build build -j 8`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness --output-on-failure`; `clang-tidy -p build` on affected C++ sources, no new/increased complexity above 25.
- **Out of scope:** grid colors, snap badge, navigation repeat changes, general command palette, Sounds tab redesign.

## Batch results (2026-10-09)

- Remappable Ctrl/Cmd+L opens Layers; Ctrl/Cmd+Shift+L creates through the existing Add semantic layer action; Ctrl/Cmd+Shift+K toggles Sounds. Ctrl/Cmd+K remains Go to sound, and Timing Lock remains Ctrl/Cmd+Shift+T. Both platform defaults have no conflicts; held keys cannot repeat dock operations. Project resets retain callbacks.
- Creation is stamped with project/chart/revision, uses the existing unique naming, palette colors, target selection and one-step Undo, and yields to modal/text/IME input or active gestures/paste. Stale requests are discarded. A newly opened list's read-only projection request cannot swallow creation. Appended targets scroll into view even beyond a 200-layer catalog. Compact layouts reveal Layers by closing Sounds.
- Normal application build completes at `[100%]`. Focused verification: core registry `748 assertions in 20 test cases`; application dispatcher and existing layer action/Undo checks `327 assertions in 34 test cases`; production dock event checks `71 assertions in 7 test cases`. Full suite: `100% tests passed out of 3043` (`203.87 sec`). Dedicated event harness: `100% tests passed out of 294` (`9.33 sec`). Logs: `/tmp/dock-shortcuts-build-verified.log`, `/tmp/dock-shortcuts-full-final.log`, `/tmp/dock-shortcuts-events-final.log`.
- Affected-source clang-tidy checks find no new/increased complexity violations. Existing main command-state/main scores stay 27 and status-bar score stays 28. A duplicate frontend compile entry crashed clang-tidy; the successful checks use one canonical entry per source copied from `build/compile_commands.json` into `/tmp/dock-shortcuts-tidy-db`, plus the dispatch-only test entry in `/tmp/dock-shortcuts-dispatch-tidy-db`. Logs: `/tmp/dock-shortcuts-tidy-clean.log`, `/tmp/dock-shortcuts-tidy-last.log`, `/tmp/dock-shortcuts-tidy-reveal-final.log`, `/tmp/dock-shortcuts-tidy-dispatch-tests.log`.
- Reviewed real-window isolated Xvfb captures in `/tmp/dock-shortcuts-visual/`: creation while Layers is closed, appended Layer/Layer 2 selected and visible, separate Undo operations restoring 203 original layers and clean title, Sounds hide/show, independent Go-to-sound picker, and Layers visible at 960 px. The fixture file SHA-256 is unchanged. No windows were opened on the user's desktop.
- Implementation and completion documentation are committed together; the preceding Timing Lock remap is committed as `04fafad`. Nothing pushed.
