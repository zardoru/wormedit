# Task cards: quantize intent and command access

[Back to the task-card index](../tasks.md).

Authorized 2026-10-11 from priority scratchpad feedback. Existing quantize core and modal surfaces are reused.

## T-545 Make quantize previews explicit

- **Status:** done
- **Outcome:** A chart-scoped session toggle defaults off and controls preview ghosts/reports independently of inspector visibility/scroll; Apply still uses existing validation and one Undo.
- **Read first:** src/ui/frontend.h; src/sys/compose_view.h/.cpp; src/sys/compose_actions.h/.cpp; src/ui/nuklear/compose/compose_inspector.cpp; src/ui/nuklear/compose/compose_canvas.cpp; tests/sys/frame_timing_tests.cpp; tests/sys/compose_quantize_preview_tests.cpp; tests/ui/quantize_report_event_harness_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/ui/frontend.h; src/sys/compose_view.h/.cpp; src/sys/compose_actions.h/.cpp; src/ui/nuklear/compose/compose_inspector.cpp; src/ui/nuklear/compose/compose_canvas.cpp; tests/sys/frame_timing_tests.cpp; tests/sys/compose_quantize_preview_tests.cpp; tests/ui/quantize_report_event_harness_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; this card; `memory/chart-editor-status/resume.md`. Shared ownership is coordinated by root.
- **Steps:** 1. Audit existing behavior. 2. Implement the shared live path. 3. Verify state, command/input routing and native presentation.
- **Invariants:** no document mutation for preview; existing atomic quantize/Undo and modal/stale guards remain.
- **Reject/failure behavior:** invalid Apply reports existing diagnostics without mutation; stale or occluded requests cannot bypass guards.
- **Verification (2026-10-11):** normal and Release builds pass; 3,329/3,329 full CTest checks and 448 production event cases / 6,197 assertions pass. Default clang-tidy has no new/increased project complexity violations; unchanged legacy/third-party findings remain. Native authenticated TCP Xvfb confirms palette preview on, Ctrl/Cmd+Alt+Q off, Apply row 130→240, and one Undo restoring row 130 and source-encoding flags. Evidence: `/tmp/quantize-intent-verified-{build,release,ctest}.log`, `/tmp/quantize-intent-all-events.log`, `/tmp/quantize-intent-{tidy,integration-tidy,final-tests-tidy}.log`, `/tmp/quantize-intent-native-{palette,on,shortcut-off,applied,undone}.png`, `/tmp/quantize-intent-native-values.log`.
- **Verify:** normal/Release builds, full CTest and production event harness, affected default clang-tidy, isolated native Xvfb, documentation and whitespace checks.
- **Out of scope:** Export modal/overflow, backdrop coverage, assist ticks, compatible layouts, BGA visibility, loop gaps and timing dock.

## T-546 Expose quantize preview and Apply in command search

- **Status:** done
- **Outcome:** Registry commands toggle preview (Ctrl/Cmd+Alt+Q by default) and Apply current quantize settings through the shared production path, preserving focus/context guards. Apply has no new default shortcut and remains remappable.
- **Read first:** src/core/command_registry.cpp; src/sys/application_commands.h/.cpp; src/sys/commands/application_command_dispatch.cpp; src/sys/app/main.cpp; tests/sys/application_commands_tests.cpp; tests/core/command_registry_tests.cpp.
- **Files owned:** src/core/command_registry.cpp; src/sys/application_commands.h/.cpp; src/sys/commands/application_command_dispatch.cpp; src/sys/app/main.cpp; tests/sys/application_commands_tests.cpp; tests/core/command_registry_tests.cpp; this card; `memory/chart-editor-status/resume.md`. Shared ownership is coordinated by root.
- **Steps:** 1. Audit existing behavior. 2. Implement the shared live path. 3. Verify state, command/input routing and native presentation.
- **Invariants:** no document mutation for preview; existing atomic quantize/Undo and modal/stale guards remain.
- **Reject/failure behavior:** invalid Apply reports existing diagnostics without mutation; stale or occluded requests cannot bypass guards.
- **Verification (2026-10-11):** normal and Release builds pass; 3,329/3,329 full CTest checks and 448 production event cases / 6,197 assertions pass. Default clang-tidy has no new/increased project complexity violations; unchanged legacy/third-party findings remain. Native authenticated TCP Xvfb confirms palette preview on, Ctrl/Cmd+Alt+Q off, Apply row 130→240, and one Undo restoring row 130 and source-encoding flags. Evidence: `/tmp/quantize-intent-verified-{build,release,ctest}.log`, `/tmp/quantize-intent-all-events.log`, `/tmp/quantize-intent-{tidy,integration-tidy,final-tests-tidy}.log`, `/tmp/quantize-intent-native-{palette,on,shortcut-off,applied,undone}.png`, `/tmp/quantize-intent-native-values.log`.
- **Verify:** normal/Release builds, full CTest and production event harness, affected default clang-tidy, isolated native Xvfb, documentation and whitespace checks.
- **Out of scope:** Export modal/overflow, backdrop coverage, assist ticks, compatible layouts, BGA visibility, loop gaps and timing dock.

## T-547 Clarify command palette title and key hints

- **Status:** done
- **Outcome:** Palette has a prominent title using the existing 28 px display face and same-row keycaps for navigation/activation/cancel; narrow headers use the regular face. Bold is not currently available in the atlas. Shared dialog defaults remain unchanged.
- **Read first:** src/ui/nuklear/command_menu.cpp; src/ui/nuklear/dialog.h/.cpp; tests/ui/command_menu_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/command_menu.cpp; src/ui/nuklear/dialog.h/.cpp; tests/ui/command_menu_event_harness_tests.cpp; this card; `memory/chart-editor-status/resume.md`. Shared ownership is coordinated by root.
- **Steps:** 1. Audit existing behavior. 2. Implement the shared live path. 3. Verify state, command/input routing and native presentation.
- **Invariants:** no document mutation for preview; existing atomic quantize/Undo and modal/stale guards remain.
- **Reject/failure behavior:** invalid Apply reports existing diagnostics without mutation; stale or occluded requests cannot bypass guards.
- **Verification (2026-10-11):** normal and Release builds pass; 3,329/3,329 full CTest checks and 448 production event cases / 6,197 assertions pass. Default clang-tidy has no new/increased project complexity violations; unchanged legacy/third-party findings remain. Native authenticated TCP Xvfb confirms palette preview on, Ctrl/Cmd+Alt+Q off, Apply row 130→240, and one Undo restoring row 130 and source-encoding flags. Evidence: `/tmp/quantize-intent-verified-{build,release,ctest}.log`, `/tmp/quantize-intent-all-events.log`, `/tmp/quantize-intent-{tidy,integration-tidy,final-tests-tidy}.log`, `/tmp/quantize-intent-native-{palette,on,shortcut-off,applied,undone}.png`, `/tmp/quantize-intent-native-values.log`.
- **Verify:** normal/Release builds, full CTest and production event harness, affected default clang-tidy, isolated native Xvfb, documentation and whitespace checks.
- **Out of scope:** Export modal/overflow, backdrop coverage, assist ticks, compatible layouts, BGA visibility, loop gaps and timing dock.
