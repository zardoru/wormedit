# Next seek, playhead and rendering batch

User added T-276 to the proposed T-470/T-469 batch on 2026-10-07.
Implementation authorized and dispatched after the user said “Hit it.”
Base commit: `9d0e112`.
Root retains final integration authority over main.

- Audio worktree: **T-276 + T-470**, one Sol 6.1 medium coordinator with
  narrow Luna medium task briefs. They share preview_session.cpp/.h, so
  one coordinator serializes those edits. Audit and complete the existing
  playhead snapshot first, then implement asynchronous seek priming.
- Rendering worktree: **T-469**, a second Sol 6.1 medium coordinator.
  Measure after integrated T-440 before choosing whether budgeted texture
  refills are necessary. Luna can be shared sequentially between worktrees
  under the four-agent limit.

T-276 already has playhead_snapshot.h and snapshot-backed active/timeline
reads in preview and audition sessions. Do a stale-plan audit instead of
replacing that foundation blindly. Check coherent source-position/generation
coverage, Stop clearing, stale-generation handling and bounded retry fallback
against the card; run concurrent writer/reader consistency tests. Record exact
supporting ownership before edits. T-470 must retain these guarantees while
keeping decoding off the calling thread, superseding rapid seeks, preserving
mid-slice resume/loops and measuring warm-cache audible resumption under 50 ms.

Canonical cards: [T-276](../../docs/chart-editor-plan/tasks/live-transport.md)
and [T-470/T-469](../../docs/chart-editor-plan/tasks/stems-performance.md).
T-471 remains a later measurement-first task.

Use only two clean worktrees, separate durable scratch builds outside /tmp,
exclusive executor file ownership, normal dependency-aware builds and the
configured complexity limit of 25. Root reviews committed diffs, integrates
serially, runs the full application build and suite, records actual evidence,
and removes completed workers/builds while retaining branches/logs. Existing
main worktree modifications from other work are outside this planning change.
Existing GUI restrictions persist; headless timing/operation evidence must
not be described as live profiler percentages.

## Active ownership

- `/home/zardoru/devel/wormedit-seek-playhead-oct7`, branch
  `batch/seek-playhead-oct7`: audio coordinator `/root/budget_recovery_61`.
- `/home/zardoru/devel/wormedit-stem-refills-oct7`, branch
  `batch/stem-refills-oct7`: renderer coordinator `/root/bga_preview_next_61`.
- Existing Luna `/root/budget_recovery_61/budget_luna` is initially audio-owned;
  explicit release and handoff are required before renderer tasks.

Workers own only their card paths and recorded supporting extensions. Root
owns build manifests, main integration and status documents. Main's existing
modifications in `src/sys/frame_timing.cpp/.h`, `src/sys/main.cpp`,
`src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp` and
`tests/sys/frame_timing_tests.cpp` belong to other work and are excluded.
A binary patch of that starting state is retained under durable scratch
`wormedit-scratch-builds/seek-render-oct7/root/preexisting-main.patch`.
Scratch/builds/logs are split into audio/render/root under that directory.
Root will preserve those external modifications through integration and verify
actual seek/zoom p50/p99 evidence without claiming unavailable live percentages.
