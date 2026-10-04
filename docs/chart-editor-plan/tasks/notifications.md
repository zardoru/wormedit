# Task cards: global notifications

[Back to the task-card index](../tasks.md).

Today every message is one string. `src/sys/main.cpp` threads a single
`std::string& status` through dozens of handlers (last writer wins), Compose and
Timing keep their own `status` strings (`src/sys/compose_view.h`,
`src/sys/timing_view.h`), and the Nuklear frontend has a separate
`g_project_dialog_error`. Nothing carries a severity, so a routine hint such as
"Compose navigation canceled." can hide a command's result or an error in the
same frame (seen in T-207), and worker failures (autosave, export, decode)
compete for the same line.

These cards add a small notification center: a headless, bounded queue of typed
messages; a status-bar line for transient info; toasts for warnings and errors;
and a history panel. Existing strings migrate in batches without behavior loss.

## Design (shared by every card)

- **Severity:** `hint` (status line only, never overrides a newer non-hint in the same frame), `info`, `success`, `warning`, `error`.
- **Entry:** stable id, severity, source (`document`, `edit`, `audio`, `export`, `import`, `recovery`, `format`, `input`, `system`), short message (≤ 200 bytes), optional detail text (≤ 4 KiB), optional target (chart id + exact position, or object id) for "Go to", monotonic timestamp, repeat count, read flag.
- **Dedupe:** an entry identical in severity, source, and message to the newest one within 2 s increments its count instead of adding a row.
- **Bounds:** ring of 256 entries; oldest `hint`/`info` evicted first, errors last.
- **Threads:** workers post through a mutex-protected inbox; the frame thread drains it once per frame. Posting never blocks on the UI.
- **Presentation:** the status bar shows the newest non-hint entry for 4 s (hint otherwise), plus a badge with unread warning/error counts. Warnings and errors also appear as toasts (bottom-right, max 3, warnings fade after 8 s, errors stay until dismissed). A history panel lists everything with a severity filter, Copy details, and Go to.
- **Headless first:** the queue and its policy live in core and are tested there; Nuklear only draws a bounded scalar view.

## T-218 Add the headless notification center

- **Status:** done (2026-09-30; integrated build and 824 core/documentation tests passed)
- **Outcome:** `notification_center_t` implements the design's entry type, severity rules, dedupe, bounded eviction, thread-safe inbox, read/dismiss, and a bounded snapshot for views.
- **Read first:** this file's Design section, `include/wormedit/core/document.h` (`diagnostic_severity_t`, `diagnostic_list_t`), `include/wormedit/core/async_job.h`.
- **Files owned:** new `include/wormedit/core/notifications.h`, new `src/core/notifications.cpp`, new `tests/core/notifications_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Types and `post`, `drain_inbox(now)`, `mark_read`, `dismiss`, `dismiss_all`, `snapshot(max)`.
  2. Helper `post_diagnostics(source, diagnostic_list_t)` mapping `diagnostic_severity_t` (info/warning/error) to entries.
  3. Tests: dedupe window, eviction order under 300 posts, 4 threads × 1,000 posts lose nothing that fits, hint never replaces a same-frame error as the "current line".
- **Invariants:** no allocation beyond the ring and message strings; deterministic with an injected clock.
- **Reject/failure behavior:** over-long messages are truncated at a UTF-8 boundary with "…".
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI.
- **Verification evidence (2026-09-30):** full application build passed; `ctest --test-dir build --output-on-failure -R '^(core|documentation)\.'` reported `100% tests passed out of 824` (14.74 s). Configured clang-tidy on implementation/tests passed without diagnostics. Review added cross-drain sliding-window dedupe regressions and allocation-free `borrowed_snapshot(max)` for frame views; owning `snapshot(max)` remains available. After four seconds, current line selects the newest hint or becomes empty when none exists, following the Design section.

## T-219 Show notifications in the status bar and as toasts

- **Status:** done
- **Outcome:** the application owns one `notification_center_t`; the status bar shows the current line and an unread badge; warnings/errors show as toasts with a dismiss button; Escape dismisses the newest toast when no edit gesture or text field has focus.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_status_bar`), `src/ui/frontend.h` (`WormUiWorkspaceView::status_text`), `src/sys/workspace_projection.cpp` (`flatten_workspace`).
- **Files owned:** `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, new `src/ui/nuklear/notification_toasts.cpp/.h`, `src/ui/nuklear/sdl_backend.cpp/.h` (isolated overlay context submission), `src/sys/workspace_projection.cpp/.h`, `src/sys/main.cpp` (ownership, drain, and view only), `CMakeLists.txt`, `tests/CMakeLists.txt` (user-approved test wiring extension), `tests/ui/nuklear_compose_state_tests.cpp` or a new `tests/ui/nuklear_notification_state_tests.cpp`.
- **Steps:**
  1. Scalar view: current line (text, severity), badge counts, up to 3 toasts (id, severity, message, count).
  2. Color by severity from the theme; toasts drawn in a `nk_begin` window anchored bottom-right, above the status bar.
  3. Actions: dismiss toast (by id), open history (T-220).
  4. Keep `status_text` working during migration: if no notification is current, show it as today.
- **Invariants:** toasts never take keyboard focus or intercept chart input outside their rectangle.
- **Reject/failure behavior:** none.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `clang-tidy -p build <changed .cpp>`; isolated Xvfb/SDL-dummy smoke PNG with `WORMEDIT_NOTIFICATION_SMOKE=1` (one warning and one error), and `WORMEDIT_NOTIFICATION_SMOKE_LONG=1` for the fixed 200-byte detail fixture.
- **Evidence:** integrated build passed; full suite 1096/1096 passed in 51.13 seconds (`/tmp/wormedit-t219-final-full-ctest.log`); focused notification/central-combo guard checks passed 38/38. The real pinned-Nuklear renderer tests cover primary active-window/ROM and queued text preservation, allocation-failure input passthrough, editor-owned popup lifetime, and wrapped text/scissor bounds in normal and 400×200 viewports. Actual isolated captures show startup warning/error and unread counts, eight-second warning expiry with persistent error, inline typing 177→hover→8 retaining 1778, and editor-first Escape leaving the error toast visible. The fixed 200-byte message is visibly contained in its bounded hover detail (`/tmp/wormedit-t219/verified-hover-editor-typed.png`, `/tmp/wormedit-t219/verified-editor-escape-first.png`). Configured clang-tidy reports no new owned-source complexity violations. Six affected staged C++ sources also pass syntax checks against HEAD plus the selective cut, without the uncommitted preference/rendering changes (`/tmp/wormedit-t219/staged-syntax.log`). The final actual Xvfb lifecycle gate exited 0: body-to-close release leaves the error intact, while a genuine close dismisses it persistently (`/tmp/wormedit-t219/gui-lifecycle.log`). History remains out of scope: the readable unread counts retain an explicitly unavailable history action until T-220.
- **Out of scope:** history panel.

## T-220 Add the notification history panel

- **Status:** done (bounded modeless history, exact navigation, complete UTF-8 detail/OS clipboard, isolated focus and geometry; full 1162/1162 and actual Xvfb gates passed)
- **Outcome:** View → Notifications (and command `view.notifications`) opens a modeless panel listing entries newest-first with severity filter, repeat counts, relative time, expandable detail, Copy details (to the OS clipboard), Go to (for targets), Dismiss, and Clear all; opening it marks entries read.
- **Read first:** `include/wormedit/core/notifications.h`, `src/core/command_registry.cpp`, `src/ui/nuklear/frontend.cpp`.
- **Files owned:** new `src/ui/nuklear/notification_panel.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/core/command_registry.cpp`, `src/sys/main.cpp` (history ownership, projection, and dispatch), `src/sys/application_commands.h/.cpp`, `src/sys/application_command_dispatch.cpp/.h`, new `src/sys/notification_history_runtime.cpp/.h`, new `tests/sys/notification_history_runtime_tests.cpp`, new `tests/ui/nuklear_notification_panel_tests.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Bounded view of up to 256 rows. 2. Go to reuses the existing cursor-jump path (chart select + exact position). 3. Register the command with a default chord that does not conflict (check `diagnose_shortcut_conflicts`).
- **Invariants:** opening the panel does not change tool, selection, or cursor.
- **Reject/failure behavior:** a Go to target whose chart or object no longer exists shows "Target no longer exists".
- **Verify:** standard build/test; smoke PNG with the panel open.
- **Out of scope:** persisting history across sessions.
- **Approved decisions (2026-10-01):** the user approved the integration/test scope above and navigation policy: valid targets without an exact chart position have disabled Go to with “Sin posición en el chart”; positional targets navigate exactly; a deleted previously valid target rejects with “Target no longer exists”.

## T-221 Route the global status string through notifications

- **Status:** ready (T-219 is done; 2026-10-04 readiness audit)
- **Outcome:** every `status = "…"` / `status.assign` in `src/sys/main.cpp` posts a notification with an explicit severity and source instead; the `std::string& status` parameters are replaced by a `notification_sink_t&` (a thin handle that posts to the center).
- **Read first:** `src/sys/main.cpp` (grep `std::string& status` and `status =`), `include/wormedit/core/notifications.h`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/timing_edit_runtime.cpp/.h`, `src/sys/metadata_actions.cpp/.h`, `src/sys/application_command_dispatch.cpp/.h`.
- **Steps:**
  1. Classify each message: failures and rejections → `warning` (user can fix) or `error` (something broke); confirmations → `success`; guidance ("Drag over the Scroll / SV lane…") → `hint`; everything else → `info`. List any unclear message in the report instead of guessing.
  2. Convert one function at a time; build after each file.
  3. Keep message text identical.
- **Invariants:** no message disappears; ordering within a frame is preserved.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; `grep -n "std::string& status" src/sys/main.cpp` returns nothing.
- **Out of scope:** Compose/Timing view strings (T-222), workers (T-223).

## T-222 Route Compose, Timing, and dialog messages through notifications

- **Status:** blocked-on T-221
- **Outcome:** `compose_input_family_result_t` messages, `flattened.compose.status`, the Timing view `status`, and `g_project_dialog_error` post notifications; routine gesture messages ("Compose navigation canceled.", "… move") become `hint`, so they can no longer hide a command result (fixes the precedence half of T-207).
- **Read first:** `src/sys/compose_input.cpp` (`continue_batch` messages), `src/sys/compose_actions.cpp` (`compose_result_status`), `src/sys/compose_view.cpp` (`view.status_text` assignments), `src/sys/timing_view.cpp`, `src/ui/nuklear/frontend.cpp` (`g_project_dialog_error`).
- **Files owned:** those files and their headers, `src/ui/frontend.h`.
- **Steps:** as T-221, file by file; add a regression test that a command success followed by a navigation-cancel hint in the same frame keeps the success as the current line.
- **Invariants:** as T-221.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; manual repro of T-207's add-layer flow shows "Semantic layer added."
- **Out of scope:** new messages.

## T-223 Post worker and background failures

- **Status:** ready (T-219 is done; 2026-10-04 readiness audit)
- **Outcome:** autosave/backup failures, export and publication results, resource copy/import results, decode/source-unavailable and preview underruns, recovery scan findings, and SDL device failures post through the thread-safe inbox with the right source; repeated underruns collapse by dedupe.
- **Read first:** `src/core/autosave_loop.cpp`, `src/sys/audio_transport_frame.cpp` (underrun and source-unavailable messages), `src/sys/compose_actions.cpp` (export worker), `src/sys/resource_job_state.cpp`, `src/sys/platform_recovery.cpp`.
- **Files owned:** those files and headers.
- **Steps:** 1. Pass a `notification_sink_t` (thread-safe) to each runtime at construction. 2. Replace their status outputs with posts. 3. Test in `tests/core/autosave_loop_tests.cpp` that a failed publication posts one `error` with source `recovery`.
- **Invariants:** workers never touch UI state directly.
- **Reject/failure behavior:** posting failures are ignored (never crash a worker).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'autosave|recovery|^audio\.|^core\.'`.
- **Out of scope:** stderr logging.

## T-224 Attach targets to validation and import diagnostics

- **Status:** blocked-on T-220
- **Outcome:** diagnostics that carry a chart position or object (import warnings, document validation, conversion losses) post with a target so Go to jumps to them; a burst of more than 20 from one operation collapses into one summary entry whose detail lists them all.
- **Read first:** `include/wormedit/core/document.h` (diagnostic types), `include/wormedit/formats/conversion.h`, `src/sys/project_open.cpp`.
- **Files owned:** `src/core/notifications.cpp`, `include/wormedit/core/notifications.h`, `src/sys/project_open.cpp`, `src/sys/compose_actions.cpp` (export results only), `tests/core/notifications_tests.cpp`.
- **Steps:** 1. `post_diagnostics` gains an optional target resolver. 2. Summary collapse with a test.
- **Invariants:** detail text stays bounded (4 KiB, truncated with a count of omitted lines).
- **Reject/failure behavior:** unresolvable targets post without Go to.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** the Compare/Validate list (T-47) — it may later read the same entries.
