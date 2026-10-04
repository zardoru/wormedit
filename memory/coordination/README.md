# Worktree coordination pilot

Requested 2026-10-04: root integrates main; each worktree has a `gpt-6.1-sol`
coordinator with medium reasoning and `gpt-6-luna` executors with medium
reasoning. Follow [the agent workflow](../agent-workflow.md).

This first round audits readiness and produces reviewed implementation briefs.
It does not implement or complete the feature cards. Starting point: `f2b35bd`.

Pilot review complete: root integrated both coordinator commits after reviewing
their reports, source evidence, and diffs. T-221/T-223/T-239 have completed
prerequisites; T-222 waits for T-221 implementation. Timing's runnable order is
T-239 then T-51; T-81 waits for the snap-gesture decision. This gate does not
block independent ready cards. Shared source edits remain serialized.

| Worktree | Branch | Batch | Reviewed task list |
| --- | --- | --- | --- |
| `/home/zardoru/devel/wormedit-notifications` | `coord/notifications` | T-221, T-222, T-223 | [Notifications](notifications.md) |
| `/home/zardoru/devel/wormedit-tap-apply` | `coord/tap-apply` | T-239, T-81, T-51 | [Timing](tap-apply.md) |

## Ownership and scheduling

- Root alone changes main, the resume checklist, and this index. Coordinators
  commit reviewed changes on their own branches and report evidence to root.
- Each Sol coordinator assigns a written Luna brief with exclusive paths,
  positive and negative cases, checks, and out-of-scope behavior. Sol reviews
  the actual diff and evidence before reporting completion.
- Four active agent slots permit root, two Sol coordinators, and one Luna.
  Root grants the Luna slot to notifications first, then timing. Slot availability
  does not authorize simultaneous edits to shared files.
- Feature implementation requires fresh ownership assignments. Root serializes
  `src/sys/main.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, shared
  Compose input/projection/overlay files, and build registrations across batches.
  Narrow independent units may proceed concurrently after ownership is recorded.
- Coordinators request exact supporting paths before executors edit them.
  Root records approved extensions on the owning card. Unanswered design
  decisions and explicit STOP steps remain gates.

## Integration gate

Root reads each committed diff, checks source claims and dependencies, and
cherry-picks only accepted results. Run `git diff --check` and
`ctest --test-dir build --output-on-failure -R '^documentation\.'` for this
planning round. Future production changes additionally require the owning
card's build, test, complexity, and isolated visual/workflow checks. Workers
must not merge or push main.
