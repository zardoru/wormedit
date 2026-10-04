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
worktree, with root integration checks underway and manual visual gates pending.
The user chose Ctrl+Alt for T-81 on 2026-10-04, conditional on no collisions;
timing now audits that gesture and implements the next batch. Main integration
remains serialized.

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
