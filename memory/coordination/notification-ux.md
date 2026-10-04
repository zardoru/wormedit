# Notification UX implementation batch

2026-10-04; coord/notification-ux, base50dd2cc.
Coordinator GPT-6.1 Sol medium; executor GPT-6 Luna medium.
User requests actual blank-canvas hit regression and transient routine feedback.
T-374/T-375 in notifications cards record exact ownership before edits.

## Tasks and ownership

1. Luna: actual frontend/event reproduction and bounded fix; exclusive frontend.cpp
   and dedicated hit-region harness tests. Sol owns manifest wiring/configuration.
2. Sol: explicit latest transient center/sink API + core bounds/priority/expiry/tests.
3. Sol: transport command/callback and toolbar producer classification, common
   typed Compose input/actions hint helpers, audition start helper. No text inference.
4. Sol reviews actual Luna source/logs, reruns focused checks; complete own producer
   checks, full independent Debug build/tests/docs/tidy. Root reviews/integrates.

Blank coordinates from screenshot are approximately x805..1173/y210..1003 at
1913x1040; actual harness layout/hit actions must establish causality. Badge and
View positives must use actual SDL events, not direct action injection. Existing
capture/focus/drag/clipping behavior remains unchanged.

Transient API is explicit; latest-only inbox/display storage remains bounded and
thread-safe, outside history/unread/dedupe/eviction. Normal four-second line expiry
and active non-hint priority preserve success/warning/error feedback. Center and
sink lifetimes remain join-safe/noexcept; durable warnings/errors/details/targets
remain in history. Existing all-history prose is overridden by current user scope.
Visual gate remains unverified under established Xvfb/sandbox block; use actual
headless event harness without bypassing policy or opening user desktop windows.

Exact supporting test correction before edits: existing transport command regressions
are in tests/sys/application_commands_tests.cpp (the initially named new command
notification test path was not present); Sol owns that existing test file.

Sol additional exact supporting scope: src/sys/compose_area_actions.cpp typed
producer feedback helper. Area-clear hint publishes there directly, so route its
explicit hint outcome transient while preserving durable changes/rejections.

## Reviewed implementation and evidence

The screenshot middle click remains unreproduced. Actual SDL event baseline
sweeps across the blank right canvas did not emit OPEN_HISTORY; badge and View
positives did. The final projection fixture is loaded Compose, ten key lanes,
four measures, selected note/inspector, reserved unavailable BGM row, six unread
warnings, 1913x1040. This is frontend projection data, not a backed real chart.
The minimal badge hardening captures its next widget bounds and clipped hover
before consuming the button, then requires Nuklear activation and the pointer
inside that current visible widget. The real backend activates on mouse-down;
a release-only draft failed the positive event test and was removed before final
review, preserving the existing activation semantics. View menu remains unchanged. This
is defensive ownership hardening, not a proven screenshot root-cause fix.
Original middle-click/manual real-window reproduction remains pending.

Transient storage has one mutex-protected pending entry and one frame-thread
entry; no history statistics/unread/eviction changes. UTF-8 message <=200 bytes,
no detail/target/toast retained for routine feedback, four-second expiry. Durable
post APIs keep explicit legacy hint retention and all diagnostic/edit semantics.
Active durable non-hints take precedence. Shared monotonic delivery sequence
resolves equal timestamps, including stable-ID deduplication updates. Transport
start/pause/resume accepts become hints from enum/accepted metadata only; warning
and error metadata retain durability. Space, command Stop, toolbar Stop, audition,
common Compose input/actions/area navigation/cancel/staging hints use explicit
transient API. Meaningful settings/edits/no-ops and failures remain durable.
Main typed guidance/project cancellation branches also use transient delivery.
Existing messages, call order, callback ownership, input stop boundaries and
center-before-producers lifetime are preserved.

Independent offline Debug builds of app, core, frame and frontend event targets
passed after all entry-layout dependencies rebuilt. Logs under
/tmp/wormedit-batch-implementation/:
- notification-ux-final-build.log and notification-ux-final-incremental.log.
- notification-ux-core-tests.log: 46 cases /206 assertions pass.
- notification-ux-frame-tests.log: 77 cases /896 assertions pass, including actual
  Space/command/toolbar stop and retained audio failure, success/cancel precedence.
- notification-ux-core-all-tests.log: 1381 cases /215278 assertions pass (existing
  host performance warning only).
- notification-ux-frame-all-tests.log: 244 cases /216086 assertions pass.
- notification-ux-app-final.log: final press-compatible badge guard app build passes.
Configured clang-tidy affected production/new tests completed with no errors;
core/new helpers/new tests/frontend guard are <=25. Prior unchanged violations
remain build_speed_graph29, compose view-action group36, main refresh/main27,
and old large application command tests. An introduced nested workspace report
classification was extracted before final checks (no remaining new violation).
Docs checker and git diff --check pass. Parent owns final integrated full CTest;
manual visual gate remains unavailable under prior isolated-Xvfb approval block.

Final actual event evidence: notification-ux-focused-final.log passes 6/6 isolated
CTest cases after correcting held badge/canvas press sequences to motion+down.
The 120-point blank grid, intended badge/View positives, stale popup/new badge,
open history/long feedback and disappearing-under-press cases pass. One legitimate
badge press opens once; disappearing badge release does not produce a second open.
Sol reviewed exact child source and logs; no proved original screenshot cause.

## Root integration

Accepted as `3feaced` on main. Independent root Debug build and all 2,242/2,242
CTest cases pass, including 133 isolated event-harness cases. Logs:
`/tmp/wormedit-batch-implementation/root-ux-main-build.log` and
`root-ux-main-suite.log` (161.34 seconds). T-375 is complete; T-374 is defensive
hardening with original middle-click/manual reproduction pending. The existing
automatic approval rejection for Xvfb escalation remains in force; no bypass
or real-window success is claimed.
