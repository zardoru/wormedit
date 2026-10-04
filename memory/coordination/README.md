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
| `/home/zardoru/devel/wormedit-audio-notifications` | `coord/audio-notifications` | T-223 N7 audio/decode/waveform | [Audio notifications](audio-notifications.md) |
| `/home/zardoru/devel/wormedit-osutest-load` | `coord/osutest-load` | Real osu!mania import compatibility | Dedicated brief and Sol 6.1 medium coordinator in that worktree |

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
