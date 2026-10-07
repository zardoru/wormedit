# WSOLA Release measurement batch

Authorized on 2026-10-07 after the completed seek/rendering batch. Root integrates
main; one Sol 6.1 medium coordinator with narrow Luna medium briefs owns T-471
in one clean worktree. Measure before changing the algorithm.

- Worker: `/home/zardoru/devel/wormedit-wsola-oct7`, branch `batch/wsola-oct7`.
- Coordinator: `/root/budget_recovery_61`; existing Luna executor is available
  only under an explicit exclusive brief/release.
- Owned: `src/audio/time_stretch.cpp`, `include/wormedit/audio/time_stretch.h`,
  `tests/core/audio_time_stretch_tests.cpp`, T-471 section only in
  `docs/chart-editor-plan/tasks/stems-performance.md`.
- Root owns main, manifests, coordination/status and independent verification.
  Supporting paths require exact approval and recording before edits.

Use a fresh RelWithDebInfo normal dependency-aware audio/SDL build under
`wormedit-scratch-builds/wsola-oct7/audio/build`, immutable existing dependency
sources, no network fetch, no main object reuse as optimized measurements.
Scratch logs/probes live outside the repo under `wsola-oct7/{audio,root}`.
`perf` user-mode task-clock is available in this session. No desktop or user-app
interaction: reproduce actual read-only Kick playback through the production
SDL dummy-audio path; record the workload and isolation limitations precisely.
Measure audio-thread one-core utilization and WSOLA attribution at both 0.5x
and 0.1x, distinguishing samples within a busy thread from utilization of one
core over wall time. Follow T-471 step 2's explicit stop condition before any
optimization. If needed, coarse/fine search remains deterministic and preserves
T-233 isolated sine/duration/silence/mode-transition gates. No pitch-changing
playback or T-277 scheduling scope.

Run card checks, configured complexity <=25, docs gate, then commit and explicitly
release a clean worker. Root reviews and integrates serially, builds main, runs
the full suite and independent measurements, commits evidence/status and removes
the worker/build while retaining branch/logs. Original assets stay read-only.
