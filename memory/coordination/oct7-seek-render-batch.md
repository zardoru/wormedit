# Integrated seek, playhead and rendering batch

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

## Released worker ownership

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

## Integration and verification

Root reviewed and serially integrated renderer `946a8c8` as `03c3773`, then
audio `204d837` as `a9a0525`. Main's preexisting six profiling edits remained
byte-identical through integration. The user subsequently authorized committing
them; root reviewed them with the renderer coordinator and committed them
separately as `0108809`. Two narrow review corrections preserve the original
viewport timestamp sampling before autosave and make the new busy-CPU test
independent of scheduler preemption. Profiling adds wall/CPU p99, Linux context
switch counts, event subphases and optional spike-log evidence. Configured
baseline/current checks have unchanged legacy warnings; new functions stay at
or below 25, including the final root corrections.

Normal dependency-aware main application build passes. Full CTest passes
**2,568/2,568** in **51.77 seconds**, including the profiling edits. Audio worker
focused tests pass 235/235 and renderer field tests 47/47. No production test
expectations were removed to conceal failures; offline rendering explicitly
primes on its owning thread after the live start path became asynchronous.

Optimized software-SDL production canvas, 300 cached synthetic stems and 120
zoom frames plus first settlement: p50/p99/max **40.4045/45.4958/61.436 →
9.40164/10.9284/38.3756 ms**. Refills are bounded to eight per changing frame;
first settlement refreshes the remaining 292 stems once and costs 38.3756 ms,
with zero refills on the next unchanged frame. The constructed stress case is
not fresh Solstice import behavior; the card records the separate nine-key
Kick renderer fixture and its representative-source limitation. The requested
live under-3% main-thread sample share remains unmeasured.

Root repeated its own unchanged Debug production-canvas fixture against merged
main: p50/p99/max **190.225/206.077/207.904 → 38.3308/42.9179/170.078 ms**;
regenerated rows **4,679,700 → 162,460**, no memory fallbacks. The initial
same-zoom current-data refresh remains in this root fixture; its reported
maximum is separate from the optimized worker sequence.
Settled surface hash **5663771512333176944** is exactly identical before/after;
first settlement regenerates 37,960 rows and repeat settlement zero.

Root's independently linked ELF trace over merged main production objects,
actual read-only Kick `clap.ogg` at source frame 20,000, SDL dummy postmix and
30 warm seeks: call p50/p99 **0.02458/0.03559 ms**, first-audible p50/maximum
**22.671/23.1239 ms**. Decode trace: **185 requests, zero caller, 185 worker**.
The source's authored initial silence is excluded by the declared source point;
target block residency is checked before measurement. This is a representative
warm source gate, not a cold-cache or whole-chart live profiler claim. The first
root invocation had a conflicting SDL environment hint; its diagnostic log is
retained, and the corrected invocation passes all 70 assertions.

Durable root evidence: `wormedit-scratch-builds/seek-render-oct7/root/`:
`main-build.log`, `main-suite.log`, `canvas-baseline-pixels.log`,
`canvas-main-final.log`, `kick-decode-thread-main-final.log`, standalone probe
sources/link scripts and `profiling-review/` baseline/current complexity logs.
Worker evidence paths remain on the cards. Original Dragback, Kick and Solstice
chart hashes match the previous recorded hashes. No user app was restarted and
no desktop or original-chart files were written.

Both workers explicitly released clean source/build ownership. Root removed
the two worker trees and their generated build/tmp directories (about 1.3 GB),
retaining branches, logs, probes and review artifacts. Only main remains
registered. Next performance card is measurement-first **T-471**; T-277 playhead
extrapolation and its pacing scope were not included here.
