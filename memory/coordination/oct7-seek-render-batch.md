# Next seek, playhead and rendering batch

User added T-276 to the proposed T-470/T-469 batch on 2026-10-07.
This records the batch scope; implementation has not been dispatched.
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
