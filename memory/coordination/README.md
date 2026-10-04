# Worktree coordination pilot

Requested 2026-10-04: root integrates main; each worktree has a `gpt-6.1-sol`
coordinator with medium reasoning and `gpt-6-luna` executors with medium
reasoning. Follow [the agent workflow](../agent-workflow.md).

The first round audited readiness and produced reviewed implementation briefs.
Implementation batches are now authorized (2026-10-04), starting at `59962da`.

Pilot review complete: root integrated both coordinator commits after reviewing
their reports, source evidence, and diffs. T-221/T-223/T-239 have completed
prerequisites; T-222 waits for T-221 implementation. Timing's runnable order is
T-239 then T-51 are integrated as `f2f838b`; their automated checks pass in the
worktree and root: integrated Debug build and full suite 2,108/2,108 pass,
including 121 event-harness cases (`/tmp/wormedit-batch-implementation/root-timing-suite.log`).
Required manual visual gates remain pending.
The user chose Ctrl+Alt for T-81 on 2026-10-04, conditional on no collisions;
timing now audits that gesture and implements the next batch. Main integration
remains serialized.

| Worktree | Branch | Batch | Reviewed task list |
| --- | --- | --- | --- |
| `/home/zardoru/devel/wormedit-notifications` | `coord/notifications` | T-221, T-222, T-223 | [Notifications](notifications.md) |
| `/home/zardoru/devel/wormedit-tap-apply` | `coord/tap-apply` | T-239, T-81, T-51 | [Timing](tap-apply.md) |
| `/home/zardoru/devel/wormedit-audio-notifications` | `coord/compose-notifications` | Audio then Compose widget producers, integrated | [Actions](compose-actions-notifications.md) |
| `/home/zardoru/devel/wormedit-osutest-load` | `coord/osutest-playback` | Real osu!mania import and partial-measure playback, integrated | [Playback](osutest-playback.md) |
| `/home/zardoru/devel/wormedit-osutest-cursor` | `coord/osutest-cursor` | Exact fractional cursor frame conversion, integrated | [Cursor](osutest-cursor.md) |
| `/home/zardoru/devel/wormedit-notification-ux` | `coord/notification-ux` | T-374 blank-area click, T-375 transient feedback, active | Sol 6.1 medium with Luna medium; dedicated brief in that worktree |

The completed audio worktree is now reused on `coord/compose-notifications`
at `b57b47f` for the remaining Compose action producers; its original
`coord/audio-notifications` branch is retained. Root approves the exact action
brief in `/tmp/wormedit-batch-implementation/compose-actions-brief.txt`:
`compose_actions.{h,cpp}`, new action-notification tests, narrow test manifest,
task-card scope and a dedicated coordination note. Any needed area-action or
bookmark-rename support must be recorded before edits. The Sol coordinator is
reviewing the Luna implementation and supporting typed producer seams. The Luna
has released source ownership after implementation. The import worktree is now
on `coord/osutest-playback`; its Sol coordinator independently reviews root's fix
for the user's Space playback regression.

## Ownership and scheduling

- Root alone changes main, the resume checklist, and this index. Coordinators
  commit reviewed changes on their own branches and report evidence to root.
- Each Sol coordinator assigns a written Luna brief with exclusive paths,
  positive and negative cases, checks, and out-of-scope behavior. Sol reviews
  the actual diff and evidence before reporting completion.
- Four active agent slots permit root, two Sol coordinators, and one Luna.
  Root grants the Luna slot to notifications first, then timing. Slot availability
  does not authorize simultaneous edits to shared files.
- Feature implementation requires exact local ownership assignments. Different
  worktrees may edit the same logical paths independently; root serializes their
  integration into main and reviews conflicting application/frontend/build
  changes. Inside each worktree, Sol and Luna never edit the same file concurrently.
- Coordinators request exact supporting paths before executors edit them.
  Root records approved extensions on the owning card. Unanswered design
  decisions and explicit STOP steps remain gates.

## Integration gate

Root reads each committed diff, checks source claims and dependencies, and
cherry-picks only accepted results. Run `git diff --check` and
`ctest --test-dir build --output-on-failure -R '^documentation\.'` for this
planning round. Production changes additionally require the owning
card's build, test, complexity, and isolated visual/workflow checks. Workers
must not merge or push main.

## Accepted implementation increments

- Timing T-239/T-51: `f2f838b`, integrated Debug build and full suite
  2,108/2,108, including 121 event-harness cases. T-81 continues under
  the user's Ctrl+Alt choice after a collision audit.
- Notifications sink/full-detail foundation: `11e577d`/`c0ea10f`.
- Autosave/recovery failure producer: `67494af`, independent 30/30 tests pass.
- Dispatch/global producer increment: `36fb59b`, integrated Debug build,
  225/225 focused tests and 632 assertions in 53 dispatch cases pass.
  Root retained atomic tap Apply, Timing Settings and count-in while resolving
  the shared-code conflicts; Tap Apply now publishes typed notifications.
  T-221–T-223 remain in progress, with metadata/import/drop, Compose/dialog and
  worker completion slices still required. No incomplete card is marked done.
- Required isolated visual gates remain unperformed: Xvfb cannot open sockets
  in this sandbox, and automatic approval review rejected escalation because
  sandbox approval is disabled. Windowless event checks are recorded separately.

Logs are under `/tmp/wormedit-batch-implementation/`.

N7 scheduling extension (2026-10-04): notifications Sol approved independent
`audio_transport_frame.{h,cpp}`, `audio_source_registration.{h,cpp}`,
`waveform_analysis_runtime.{h,cpp}`, isolated `main.cpp` glue, new
`tests/sys/audio_notification_tests.cpp` and narrow test manifest wiring.
These files are untouched in the notifications worktree. The timing coordinator
moves to this third worktree after committing T-81; each worktree keeps its own
written Sol/Luna task list. Root alone integrates shared main changes. This
parallel producer cut depends on the integrated sink, with no Compose migration
dependency; T-223 remains incomplete until all of its slices and gates pass.

New user priority: all eight charts in `/home/zardoru/Documents/charts/osutest/`
fail the real loader's timing geometry bounds. A fourth isolated worktree and
Sol 6.1 medium coordinator now audit/fix those limits with a Luna executor once
the slot becomes available. The user accepts import quantization and considers
960 divisions per standard measure already very high. Preserve read-only input
files, use bounded normal resolution, and measure timing error/hold behavior.
Onset snapping is integrated as `c50e61f`; its integrated build, 25 focused
system/detector tests and all 122 CTest-isolated event cases pass. A broader
single-process aggregate event run exposed seven fixture/state-order failures;
it is not recorded as passing. Standard isolated CTest cases remain passing.
Metadata/resource and selection producer increment `92ccc16` has an integrated
Debug build and 52 relevant metadata/transport tests passing.

Root integration follow-up: `656031b`/`bb7f1bc` pass the full Debug build and
352 relevant audio/import/notification tests. Two stale object files needed a
forced incremental rebuild after the API signature change; no source correction
was needed. Logs: `root-audio-import-final-build.log` and
`root-audio-import-tests.log` in the artifact directory.

The aggregate event issue reproduces with all seven new timing/onset cases
excluded: 115 existing cases yield the same seven failures and 27 failed
assertions (`root-existing-aggregate-events.log`). This points to a
single-process harness ordering issue, distinct from the passing 122 standard
CTest-isolated cases; no aggregate pass is claimed.

Event-drop/import-warning increment `169486a` passes the integrated Debug build
and 14 relevant drop/import-feedback cases (`root-drop-build.log`,
`root-drop-tests.log`). Startup recovery error deduplication and the remaining
seek/bookmark/rebind messages are under coordinator review. The import Luna
executor has handed off its draft; notifications receives the next Luna slot
for N6b worker completions while the import Sol completes arithmetic and
roundtrip review. All eight real charts pass draft native reopen and osu export
reimport; these results are preliminary until reviewed integration.

T-221 startup/seek/rebind follow-up `c93a65d` passes the integrated Debug build
and four focused regressions (`root-final-global-build.log`,
`root-final-global-tests.log`). Startup context no longer repeats the platform
recovery producer's error, seek callbacks own their typed publication, and
authoritative workspace retries suppress unchanged failure posts. N4
input/projection/dialog and N6b completions remain active. The final main status
reference audit is a full-batch gate after those consumers and history actions.
The completed clean audio worktree is available for a separately owned Compose
actions branch after the next reviewed notification increment and the import
coordinator's release; its Sol 6.1 medium coordinator will have a Luna medium
task list and wait for the current input executor's slot.

Resource/export and projection/dialog increment `b57b47f` is reviewed and
integrated; root build and focused verification are underway. Worker completion
consumers post once, projection diagnostics track identity and repair edges,
and the frontend bridge uses a scoped noexcept callback with UI-thread
consumption. Coordinator verification includes actual failed/too-long picker
callbacks, cancellation/stale callbacks/shutdown, filesystem copy and export
completion. N4 input/actions and recovery/final glue remain active.

Root verification of `b57b47f` passes the full Debug build and eight relevant
picker/worker/projection cases (`root-projection-dialog-resource-build.log`,
`root-projection-dialog-resource-tests.log`, `root-projection-edge-tests.log`).
Import compatibility is integrated as `4d2a8e9`: root Debug build and 1,374
core/converter/documentation tests pass (`root-osutest-build.log`,
`root-osutest-tests.log`). The independent main pipeline loads all eight real
charts, saves/reopens exact native documents, exports/reimports osu, preserves
note/lane/hold inventory, and measures actual core endpoint timing. Maximum
error is 5.169 ms on native import and 5.168 ms after reimport. Original inputs
remain unchanged; artifacts are in `osutest-load/root-final/` below the log
directory. Normal resolution is bounded at 960 divisions per standard measure,
with quantization warnings and explicit source-time origin.

Corrected N4 input is integrated as `68d8b2f`/`4ea6383`: ordinary gesture
cancellation and fit previews are hints, fit/edit commits preserve typed results,
paste retains diagnostic/rebind metadata, and original batch-stop boundaries
are restored. Root Debug build, configured production complexity checks and
352 assertions in 16 focused cases pass. Recovery/history/audition producers
are integrated as `73b3194`; root build, 53 recovery/settings assertions and
30 audition assertions pass. The combined 136 focused checks pass.
The action coordinator reviews the separately implemented widget producers,
and notifications prepares the final main/projection adapter retirement.

User's Space playback regression is integrated as `b88e4b7`, with a dedicated
[playback brief](osutest-playback.md). The negative-frame warning hid fatal
metronome divisibility errors in seven real charts. Unmetered partial measures
now use normal quarter-note pulse spacing including their final partial onset;
preview failure feedback prefers an actual error over earlier warnings.
All eight real schedules compile and runtime start/advance/pause/resume checks
with real music registered pass under SDL dummy audio. Worktree application
build, 1,370 core cases, 76 focused preview cases and three timing-settings cases
pass; root integrated build passes. The import Sol independently audits this fix.

## Current integrated verification

Actions `6473d8c`, final main/projection retirement `50dd2cc`, and cursor
conversion `3cdc1a6` are accepted. Main Debug build and all **2,228/2,228**
CTest cases pass (`root-notification-cursor-suite.log`), including the current
127 isolated event-harness cases. Root's actual action/marker check passes
7,165 assertions in 29 cases, preserving validation and published-cap order.
Configured production complexity checks report only unchanged existing scores.
The final main global status-reference/string grep returns no matches.
Root independently checks all 27,468 actual chart cursor queries against integer
frame arithmetic: all pass, 951 previously valid rejected positions are fixed,
and timing fractions are unchanged. A bounded wide-arithmetic fallback avoids
intermediate overflow while true quotient/rounding overflow still rejects.

The user's later requests start a separate T-374/T-375 batch: blank Compose
clicks must not open history, and routine trace feedback must stay outside
durable history. Sol and Luna implement this in `wormedit-notification-ux`.
Required manual visual gates remain pending under the established Xvfb block.
