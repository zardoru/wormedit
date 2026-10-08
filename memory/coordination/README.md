# Worktree coordination pilot

Completed 2026-10-07: [dense-chart interaction and frame budget](oct7-dense-batch.md),
T-475/T-476 plus the T-278 scheduling audit. Main application build and
**2,582/2,582 tests pass**. Warm inspector/paint projection improves; fourteen
merged renderer hashes and 3,360 per-frame samples match. Software rendering
still misses 5 ms; real GPU/display and older-device validation remain pending.
Only main remains after removing clean workers and about 3.1 GB of builds;
branches/logs/probes retained.

Completed in code 2026-10-07: [WSOLA and frame pacing batch](oct7-wsola-batch.md),
T-471/T-472, reviewed with Sol 6.1 medium coordinators and Luna contributions.
Main application build and **2,574/2,574 tests pass**. Actual optimized reduced-rate
WSOLA CPU falls about 90%; 1× Keep pitch bypasses stretching, and routine UI
service avoids SDL's queue lock. Pacing spends only the remaining 5 ms frame
budget. Live 200 Hz cadence, dense whole-frame p99 and older-device certification
remain pending. Both worker trees and about 1.2 GB of builds removed; only main
remains, branches/logs retained.

Completed in code 2026-10-07: [seek, playhead and rendering](oct7-seek-render-batch.md),
T-276/T-470 and T-469 reviewed and integrated with Sol 6.1 medium coordinators
and sequential Luna medium contributions. Normal main build and **2,568/2,568**
tests pass. Optimized zoom p99 45.50→10.93 ms; first settlement costs 38.38 ms.
Merged actual Ogg warm seeks resume within 23.13 ms with zero caller decoding;
settled canvas pixels match exactly. User-authorized profiling edits committed
separately as `0108809`. Live sample-share gates remain pending. Both workers
and about 1.3 GB of generated builds removed; only main remains, branches/logs
retained. Subsequent T-471/T-472 evidence is recorded above.

Completed in code 2026-10-07: [workspace and stem performance batch](oct7-performance-batch.md),
T-444/T-440 reviewed and integrated with Sol 6.1 medium coordinators and Luna
medium contributions. Normal application build and **2,544/2,544** tests pass;
merged real-chart output/peak placement checks pass. Release Dragback projection
p50 1.006→0.296 ms; Kick low-zoom stems 138–193→1.18 ms. Live sample-share
gates remain pending. Both workers and about 4.5 GB of generated builds removed;
only main remains, branches/logs retained.

Completed 2026-10-07: [hybrid BMSON and slice-span batch](oct7-hybrid-batch.md),
T-457/T-458/T-460 and T-465 integrated with Sol 6.1 medium coordinators and
Luna medium contributions. Root normal application build and **2,527/2,527**
tests pass; actual Kick/dragback import, migration and slice planning checks
pass. Planning edits committed separately as `279c1d2`. Both worker trees and
about 4 GB of generated builds removed; only main remains, branches/logs retained.

Completed in code 2026-10-07: [rendering batch](oct7-render-batch.md),
T-443 and T-451 integrated with Sol 6.1 medium coordinators and Luna medium
contributions. Normal application build and **2,495/2,495** tests pass.
Only main remains; worker branches/logs retained, about 13 GB of generated
builds reclaimed. Original-chart GUI captures remain pending.

Completed 2026-10-07: [stems and layers batch](oct7-next-batch.md),
T-439/T-445 and T-447/T-450 reviewed and integrated on main. Application build
and all **2,461/2,461** tests pass. Sol 6.1 medium completed the final memory
budget slice after Luna capacity failures. Root removed 20 completed worktrees
and pruned one missing registration, retaining every branch; only main remains.

Completed 2026-10-07: [six quick fixes in two worktrees](oct7-batch.md), with
GPT-6.1 Sol medium coordinators and GPT-6 Luna medium executors. Root reviewed
and integrated both batches. Application build and **2,434/2,434** tests pass,
including SDL mid-slice seek PCM. Real sabun, Kick and Rotterdam probes pass;
original charts remain unchanged. Visual GUI checks remain pending under the
recorded Xvfb restriction.

Follow-up T-448 correction: [profile picker boundary](profile-click-oct7.md),
reviewed and integrated as `2749c6d`. Root application build and 2,437/2,437
tests pass; actual dragback first-profile selection now creates the 10K layout
and all 10 bindings with one undo and unchanged BGM data.

Follow-up T-448 [10K color ordering](10k-color-oct7.md): reviewed and
integrated as `a3adcfa`. Root build and all 2,439 tests pass; actual rendered
lane positions have `wbwbg | ybwbw`, preserving IDs, stage order and bindings.

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
| `/home/zardoru/devel/wormedit-notification-ux` | `coord/notification-ux` | T-374 badge ownership hardening, T-375 transient feedback, integrated | [Notification UX](notification-ux.md); original click reproduction pending |
| `/home/zardoru/devel/wormedit-playback-tail` | `coord/playback-tail` | T-376 legitimate audio-tail navigation, integrated | [Playback tail](playback-tail.md) |
| `/home/zardoru/devel/wormedit-tooltip-bounds` | `coord/tooltip-bounds` | T-377 viewport tooltip placement and wrapping, integrated | [Tooltip bounds](tooltip-bounds.md) |
| `/home/zardoru/devel/wormedit-hidden-history` | `fix/hidden-history` | T-378 hidden history input capture, integrated | [Hidden history](hidden-history.md); Luna under root review |

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

The worktree table above records historical assignments. On 2026-10-07 root
removed 20 clean completed trees and pruned one missing registration at the user’s
request, retaining every branch. Only main remains registered; scratch artifacts
use workspace storage.

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

The user's later T-374/T-375 batch is integrated as `3feaced`. Routine transport,
audition and navigation hints use bounded four-second status-line feedback outside
history; warnings, errors and meaningful edits remain durable. The badge checks
its current visible widget bounds while preserving backend activation semantics.
Root Debug build and all **2,242/2,242** CTest cases pass, including 133 isolated
event-harness cases (`root-ux-main-build.log`, `root-ux-main-suite.log`). The
original middle-canvas click is unreproduced; T-374 is hardening with manual
reproduction pending, not a proved root-cause fix. Required manual visual gates
remain pending under the established Xvfb block.

## Playback tail and tooltip follow-up

Playback tail `d64c048` retains the final editable row after chart timing ends,
allowing backward seeks while decoded music continues naturally. Original Hybris
audio lasts 165.866485 seconds; seven charts end at 157.215 seconds and beginner
at 157.597 seconds. Core inverse timing bounds remain unchanged. Root main build
and three focused consumer/music/event cases pass. Final full suite will include
the parallel tooltip batch. The Sol coordinator reuses the existing Luna for the
separate tooltip worktree after releasing playback source ownership because
additional agent launches reached the session thread limit.

## Final follow-up checkpoint

Root accepted tooltips `34fdf42` and Luna hidden-history `f17d4f4` after review.
The user-provided gdb diagnosis is confirmed: Nuklear X hides history without
setting closed, while the old model and hit bounds continued to claim input.
The producer now publishes close for hidden windows, both retained contexts
close, and hidden/closed/absent windows cannot claim new input. Real X down/up,
close/reopen, visible capture and stale-hidden motion/wheel cases pass. This
resolves the confirmed invisible-rectangle input problem; the earlier isolated
OPEN_HISTORY middle-click observation remains distinct from that diagnosis.
Main full application build and **2,255/2,255** CTest cases pass, including
**144** isolated event-harness cases (`/tmp/hidden-history-validation/main-suite.log`,
169.16 seconds). Independent configured main complexity checks pass. Tooltip
geometry and actual Sounds hover pass on main; eight real charts retain natural
audio tails and backward navigation. Required real-window gates remain pending
under the established automatic sandbox-approval rejection. Worktrees are
retained; root did not push or restart the user app.
