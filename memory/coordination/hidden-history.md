# T-378 hidden notification history

The isolated `fix/hidden-history` worktree starts from `main` at `6315a69`.
Luna owns only the files listed in
[`hidden-notification-history.md`](../../docs/chart-editor-plan/tasks/hidden-notification-history.md).
The observed failure is confirmed: clicking X sets Nuklear's `NK_WINDOW_HIDDEN`
without setting `NK_WINDOW_CLOSED`; the panel producer checks only closed state,
and frontend hit-testing/capture uses the old `Notifications` bounds while the
model still reports history visible. A wheel or motion over that old rectangle
is then consumed by the hidden panel.

Root owns integration and review. Preserve real event routing in the regression;
directly injecting a close action does not prove pointer release. Do not use
Xvfb or the user's desktop. The user app is untouched and the debugging session
is already detached; do not restart or terminate it.

The producer now treats both `NK_WINDOW_CLOSED` and `NK_WINDOW_HIDDEN` as a
close action, and the frontend closes both retained contexts after close/Escape.
History capture requires an actual existing visible Nuklear window; hidden,
closed, or absent windows have empty hit bounds. The dedicated regression uses
real balanced title-bar X clicks, stale-model raw-hidden motion and wheel cases,
visible positive capture, absent-window fallthrough, and reopen. The local
production event-harness relink passes 23 assertions in 3 cases; docs check and
clang-tidy pass. Main integration remains with root.
