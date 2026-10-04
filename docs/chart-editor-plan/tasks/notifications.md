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
- **Approved implementation scope (2026-10-04):** the user authorized the full T-221/T-222/T-223 batch; root approved the exact N1–N7 supporting paths in `memory/coordination/notifications.md`. T-221 additionally owns `include/wormedit/core/notifications.h`, `src/core/notifications.cpp`, `tests/core/notifications_tests.cpp`, `tests/sys/application_command_notification_tests.cpp`, `tests/CMakeLists.txt`, `src/sys/options_runtime.h`, and `src/sys/options_runtime.cpp`, for the missing sink API, typed outcomes, and focused regressions. Executors receive exclusive subsets before editing; coordinator records further producer extensions here before use.
- **Staged acceptance:** T-221 removes global application message-producing status-reference seams. Compose/view/worker seams and history/close-dialog fallback state remain explicitly excluded until T-222/T-223. The zero `std::string& status` main grep is the final full-batch gate, not permission to migrate excluded producers early. Sink posts are noexcept, suppress empty messages, preserve explicit producer classification and original text/order, and contain posting failures. The center must be declared before every posting scheduler/runtime, with joins completed before destruction on all exits.
- **Coordinator supporting extension (2026-10-04):** `tests/ui/nuklear_options_state_tests.cpp` is added to Files owned for existing Options producer tests: explicit typed success/info/warning/error results and no repeat on NONE/projection, preserving acceptance/failure state. Sol owns Options/main integration while N1 Luna owns only notification core files.
- **Coordinator producer extension (2026-10-04):** `src/sys/project_view.h`, `src/sys/project_view.cpp`, and `tests/sys/project_view_tests.cpp` are added to Files owned for typed project-chart outcomes; this resolves classification at the result producer before main consumes it. Sol owns these paths during the N1 core brief.
- **Root text-preservation follow-up (2026-10-04):** `tests/sys/notification_history_runtime_tests.cpp` is added to Files owned for long UTF-8 sink/diagnostic history and clipboard-payload checks. When no explicit detail is supplied, a message longer than 200 bytes is retained in detail within the existing 4 KiB UTF-8 bound; explicit detail remains authoritative. Sol owns this follow-up after N1 Luna finalized.
- **N2 focused executor ownership (2026-10-04):** `tests/sys/application_commands_tests.cpp` is added to Files owned to adapt existing dispatcher regressions to sink outcomes. N2 Luna exclusively owns this file and `src/sys/application_command_dispatch.cpp/.h`; Sol owns main, transport/external producer glue, and all other sources. Callback results stay producer-owned: dispatch must not infer severity from returned prose or repost a callback's typed notification.
- **Transport producer extension (2026-10-04):** `src/sys/chart_preview_state.h`, `src/sys/chart_preview_state.cpp`, and `tests/sys/sdl_preview_output_tests.cpp` are added to Files owned. Sol attaches explicit severity/acceptance metadata at preview result creation so existing string callbacks can post once at their main-owned producer bridge; seek success no longer depends on matching returned prose. This does not change playback policy.

- **N6a producer scheduling extension (2026-10-04):** T-221 adds `src/sys/metadata_asset_runtime.h/.cpp` and `tests/sys/resource_notification_tests.cpp` to resolve import/copy start classification at its producer. T-223 already owns these paths plus `tests/CMakeLists.txt`; N6a Luna exclusively owns them, with Sol owning main/metadata action/frame-service consumers. N6a can run before N4 because sink and N5 are reviewed and runtime completion has no Compose projection dependency; shared Compose/resource worker files remain serialized until later N6b.

- **Metadata edit producer extension (2026-10-04):** Sol owns `src/sys/metadata_actions.h/.cpp` and new `tests/sys/metadata_notification_tests.cpp` for explicit edit diagnostic/no-op/success metadata and single producer publication. N6a keeps exclusive runtime/test wiring; Sol defers CMake wiring until that ownership is released. Main consumer changes follow the clean rebase boundary.

- **Transport action producer extension (2026-10-04):** Sol owns `src/sys/transport_player_actions.h/.cpp` plus existing `tests/sys/application_commands_tests.cpp` for typed branch publication of rate/metronome/loop/pitch/preview/export-policy UI actions; callback-returned playback and seek remain explicitly producer-owned, avoiding guessed severity or duplicate posts.

- **Sound/BGA producer extension (2026-10-04):** Sol owns `src/sys/compose_sound_actions.h/.cpp`, `src/sys/compose_bga_actions.h/.cpp`, existing `tests/sys/application_commands_tests.cpp` and `tests/sys/bga_actions_tests.cpp` for explicit import/edit/no-op/diagnostic outcomes. Optional sink parameters preserve string-return consumers until T-222 updates Compose; main review/drop bridges pass the sink and retain only scoped dialog feedback. No import behavior or new messages are authorized by this extension.

- **Collector producer extension (2026-10-04):** Sol owns `src/sys/application_frame_services.h/.cpp`, `src/sys/metadata_drop_state.h/.cpp`, new `tests/sys/global_drop_notification_tests.cpp` and narrow `tests/CMakeLists.txt` event-harness wiring for event-specific import validation and guidance; real frontend code supplies drop hit testing. Resource/export poll consumers stay explicitly T-223 until N6b typed completion.

- **User import-fix text override (2026-10-04):** Root explicitly approves changing main's expanded import-warning prefix to `Chart imported with warning: …` and retaining all `opened.warnings` in explicit detail within the existing 4 KiB UTF-8 bound. Actual first diagnostic text/count stay identical. This user-authorized import-fix scope overrides historical migration-only prefix preservation; severity/source remain explicit warning/import. Import lead owns loader/CLI propagation, so Sol does not edit `src/sys/project_open.cpp` or `src/tools/wormedit_convert.cpp`.
- **Import warning formatter support (2026-10-04):** Sol additionally owns new `src/sys/project_import_feedback.h` and formatter/sink checks in `tests/sys/global_drop_notification_tests.cpp`. The helper retains all warning bytes and keeps the main session opener below the complexity limit; empty warnings remain quiet.
- **Final global producer support (2026-10-04):** Sol adds `src/sys/compose_view.h`, `src/sys/compose_minimap_navigation.h/.cpp`, `src/sys/compose_bookmark_commands.h/.cpp`, `tests/sys/compose_bookmark_commands_tests.cpp`, and existing `tests/sys/frame_timing_tests.cpp` for explicit seek producer publication and bookmark duplicate suppression. `src/sys/application_frame_services.h/.cpp` and main own revision/identity-based authoritative rebind failure suppression; failed rebinds still retry each frame. This does not migrate Compose projection messages before T-222.
- **Startup producer correction (2026-10-04):** Sol owns new `src/sys/application_startup_feedback.h` and regressions in `tests/sys/global_drop_notification_tests.cpp`. Startup context does not repeat the N5 platform error; loaded/failed primary outcomes are document success/error, no-project discovery context is recovery info.
- **Rebind edge support (2026-10-04):** Sol owns new `src/sys/workspace_edit_feedback.h` and actual core-rebind regressions in `tests/sys/global_drop_notification_tests.cpp`. Identity/revision/history stamp suppresses persistent frame failures, while each frame still retries and successful rebinding resets the edge.

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
- **Approved implementation scope (2026-10-04):** exact N4 paths in `memory/coordination/notifications.md`, including supporting `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`, `tests/core/notifications_tests.cpp`, `tests/ui/nuklear_notification_state_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, and `tests/CMakeLists.txt`, are approved for integration/regressions. Use sequential exclusive input/actions, projection, and dialog briefs after T-221 review.
- **N4 review correction support (2026-10-04):** Sol owns `src/sys/compose_view.h/.cpp` narrow paste commit typed-severity out parameter, and existing `tests/sys/compose_beat_fit_tests.cpp` for actual fit producer checks; input must preserve original stop flags, classify ordinary preview/cancel as hint, actual commits success/edit, accepted no-op info/edit and actual diagnostics without prose inference.
- **N4 input automated evidence (2026-10-04):** reviewed input/navigation/viewport producers and main sink bridge preserve revision guards and callback ownership; six focused actual-dispatch tests pass, including committed success followed by navigation cancel and both wheel/keyboard seek ownership. App/frame builds pass. Actions and final main/recovery seams remain pending; manual success/hint visual gate is unverified.
- **Dependency review (2026-10-04):** Reviewed T-221 implementation is integrated as root `c93a65d`; its automated global regressions pass. Root authorizes T-222 implementation while the established sandbox Xvfb block leaves T-221's manual visual gate pending. This does not mark T-221 done.
- **N4 projection/dialog supporting scope (2026-10-04):** Sol owns new `src/sys/projection_notification_feedback.h`, `src/sys/workspace_projection.h/.cpp`, existing `tests/sys/frame_timing_tests.cpp`, and `tests/sys/global_drop_notification_tests.cpp` for explicit projection records and identity/revision/producer edge suppression, plus frontend callback publication regressions. Luna exclusively retains `src/sys/compose_actions.cpp` and resource runtime/tests until N6b completes; Sol does not edit them concurrently. Diagnostic severities remain typed, missing-chart guidance is hint, invalid geometry/state is error or its actual diagnostic severity, stale chart is warning; no text inference or new messages.
- **Frontend sink bridge support (2026-10-04):** Sol owns new `src/sys/frontend_notification_sink.h`; its scoped UI-thread callback adapter unregisters on destruction and frontend shutdown. Picker allocation/SDL failures are explicit error/system; selected-path bounds validation is warning/input. Dialog retains scoped feedback, but no longer overrides the global notification line.
- **Picker handoff verification support (2026-10-04):** Existing frontend testing APIs are extended narrowly to begin the real dialog state, invoke its production SDL callback, and consume its result without displaying a native picker. Tests exercise null/long/canceled/stale callback results and shutdown detachment through that actual handoff.

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
- **Approved implementation scope (2026-10-04):** exact N5–N7 paths in `memory/coordination/notifications.md`, including `include/wormedit/core/autosave_loop.h`, `tests/core/autosave_loop_tests.cpp`, `src/sys/application_frame_services.cpp`, `src/sys/application_frame_services.h`, `src/sys/main.cpp`, `src/sys/metadata_asset_runtime.cpp`, `src/sys/metadata_asset_runtime.h`, `src/sys/audio_source_registration.cpp`, `src/sys/audio_source_registration.h`, `src/sys/waveform_analysis_runtime.cpp`, `src/sys/waveform_analysis_runtime.h`, `tests/sys/resource_notification_tests.cpp`, `tests/sys/audio_notification_tests.cpp`, and `tests/CMakeLists.txt`, are approved for typed completion metadata, one-time terminal posting and focused tests. Serialize shared paths after T-222 and establish join-safe sink lifetime first.

- **N7 exclusive implementation scheduling (2026-10-04):** Isolated `coord/audio-notifications` owns `src/sys/audio_transport_frame.h/.cpp`, `src/sys/audio_source_registration.h/.cpp`, `src/sys/waveform_analysis_runtime.h/.cpp`, `src/sys/main.cpp` audio producer glue, new `tests/sys/audio_notification_tests.cpp`, new `tests/sys/audio_transport_notification_tests.cpp`, `tests/sys/audio_source_registration_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/CMakeLists.txt`, and new `memory/coordination/audio-notifications.md`. GPT-6 Luna medium exclusively owns source registration, waveform runtime, their existing registration tests and the new worker notification tests; GPT-6.1 Sol medium owns transport, main, frame-timing adaptation, manifests and review. Root serializes main integration. Sink lifetime precedes schedulers; preserve message order, terminal consumption, progress dedupe and cancellation/stale-result silence.

- **N7 reviewed evidence (2026-10-04):** Offline isolated Debug app and selected audio/runtime targets built successfully; worker notifications passed 46 assertions/9 cases, registration passed 86 assertions/9 cases, transport notification producers passed 33 assertions/4 cases; relevant audio/SDL and adapted waveform CTest regression passed 193/193 (36.18s). Matching/cancelled/idle results stay quiet, consumed failures post once, and same-ID reset/late probe recovery is covered. Logs and exact owners are in `memory/coordination/audio-notifications.md`; no new complexity violations, direct docs and whitespace checks passed. This completes the bounded N7 producer batch; keep this card ready until remaining batches and integrated Verify are reviewed.
- **N6b reviewed evidence (2026-10-04):** Optional sinks, typed export worker metadata, start/terminal one-time resource/export publication and main/frame consumption are reviewed. Actual filesystem copy success, failed/stale/canceled results and publication success/failure checks pass with unchanged rejection state. App/relevant targets build; the combined producer/projection/dialog focused selection passes 9/9. Cancellation during staging/discovery uses stop metadata; deterministic private-worker staging cancellation was not exposed as a new test hook. Recovery UI/scan glue and full Verify remain pending, so this card is not done.
- **Final recovery/audition automated evidence (2026-10-04):** actual controller load/inspect/stale/validation/scan-generation tests and SDL dummy audition start/failed start/consume-once tests pass; main history, workspace-error edges, startup audio failures and registration counts use the sink. App/core/SDL build and configured tidy pass with only prior violations. Existing controller diagnostic/path bounds remain. Final actions adapter/projection fallback retirement and integrated Verify remain pending.
- **Final audition support (2026-10-04):** Sol owns `src/sys/compose_audition_requests.h/.cpp`, new `tests/sys/audition_notification_tests.cpp`, and narrow SDL audio test wiring for the remaining catalog/object/draft request producers. They consume once, retain original object-before-draft order and all dynamic diagnostics, and post explicit audition metadata instead of global status references. Validation/unavailable source warning/audio, actual core diagnostic severity/audio, successful start hint/audio, failed SDL start error/audio; unavailable decoder warning/audio.
- **Final recovery/main support (2026-10-04):** Sol owns new `src/sys/recovery_notification_feedback.h/.cpp`, new `tests/sys/recovery_notification_tests.cpp`, narrow `CMakeLists.txt`/`tests/CMakeLists.txt` wiring, plus main history/close/workspace-error and startup audio bridges. Recovery request validation is warning/recovery; controller/storage diagnostics retain actual severity and original message/path formatting; successful recovery/publication/discard/quarantine is success/recovery, read-only inspect info/recovery. Scan/controller diagnostic publication is keyed by scan generation. Audio registration counts are warning/audio and SDL open failures error/audio; these final main producers must not remain hidden in the retired global status string.

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
