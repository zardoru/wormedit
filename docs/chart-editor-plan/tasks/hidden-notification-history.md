# Task card: hidden notification history releases pointer capture

[Back to the task-card index](../tasks.md).

## T-378 Release input capture after hiding notification history

- **Status:** done (2026-10-04; isolated production event-harness regression passes)
- **Outcome:** pressing the Notifications window's real close control hides history and releases its stale window rectangle from frontend pointer capture; reopening shows it normally and restores capture only while visible.
- **Read first:** `AGENTS.md`, `docs/chart-editor-plan/tasks.md`, this card, `src/ui/nuklear/notification_panel.cpp`, `src/ui/nuklear/frontend.cpp`, `tests/ui/nuklear_event_harness.h`, `tests/CMakeLists.txt`.
- **Files owned:** `src/ui/nuklear/notification_panel.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, new `tests/ui/hidden_notification_history_event_harness_tests.cpp`, `tests/CMakeLists.txt`, this card, `docs/chart-editor-plan/tasks.md`, new `memory/coordination/hidden-history.md`.
- **Steps:**
  1. Use actual event-harness input to close history with its X control; verify wheel and motion at the old bounds route back to Compose.
  2. Ensure both producer and frontend treat Nuklear's hidden window as closed for model state, hit testing and capture; reopening clears hidden state.
  3. Test visible positive capture, stale visible-model defense when Nuklear already marks the window hidden, and close/reopen behavior.
- **Invariants:** only visible history can own pointer events; don't alter notification content or unrelated window behavior.
- **Verify:** focused `[history]` event-harness cases, `documentation.chart_editor_state`, `clang-tidy` on changed C++ sources, and real close-button down/up sequence. No Xvfb or user desktop.
- **Out of scope:** notification layout, toast behavior, unrelated pointer routing.
- **Scope extension (2026-10-04, coordinator approved):** `src/ui/frontend.h` exposes a headless-only test seam implemented in the already-owned `frontend.cpp` to hide the real notification window while leaving the model stale-visible. This is needed to verify pure `NK_WINDOW_HIDDEN` defense separately from the real X-close path, whose frontend lifecycle normalizes the window to closed. The seam returns only success/failure and does not expose Nuklear context pointers.
- **Verification evidence:** manually compiled and relinked the production event harness using read-only main build objects into `/tmp/hidden-history-validation/hidden-history-tests`; `[history]` passed all 23 assertions in 3 cases. `documentation.chart_editor_state` script and clang-tidy passed. Root will run integrated full build and CTest after cherry-pick.
