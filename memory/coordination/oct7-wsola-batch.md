# WSOLA Release measurement batch

Authorized on 2026-10-07 after the completed seek/rendering batch. Root integrates
main; one Sol 6.1 medium coordinator with narrow Luna medium briefs owns T-471
in one clean worktree. Measure before changing the algorithm. Dispatch base: `b456c62`.

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

## Target hardware steering

The user clarified that this Ryzen 7 5700X is too powerful to be the target gate;
the intended floor is roughly Ryzen 2000 or a mobile Intel CPU from 2015.
The original local-under-10% stop condition is therefore superseded by that
steering. Root authorizes coarse/fine search after the actual baseline if it
materially improves headroom while preserving the quality gates. A local 2%
one-core WSOLA budget is a conservative sensitivity proxy: a fivefold cost
would use 10%. This does not claim the specified CPUs are five times slower,
and no target-device budget is certified without measurement on that device.

Root's independent production-stretcher probe compiles the existing source at
-O2 and -O0, tests all nine supported rates on sine and asymmetric stereo with
transients, and verifies duration, pitch, bounded finite output, repeatability
and byte-identical output across 512/127-frame blocks. Optimized CPU time per
output second is about 2.65%; Debug is about 26.9%, with identical output hashes
in all 18 cases. This isolated constructed-input evidence is supplementary;
actual chart/SDL/perf measurements remain the algorithm decision baseline.

## High-refresh pacing extension

The user then set a 144 Hz goal and raised the working local target to **200 Hz,
5 ms**. Root's read-only renderer-coordinator audit found that audio uses SDL's
callback and a separate decode/refill worker; its utilization is separate from
the UI frame budget. Visible-note queries already have interval/window caches.
A fixed post-present `SDL_Delay(1)` is concrete avoidable pacing debt.

Root authorized the separate [T-472 pacing card](../../docs/chart-editor-plan/tasks/frame-pacing.md)
in `/home/zardoru/devel/wormedit-frame-pacing-oct7`, branch `batch/frame-pacing-oct7`,
coordinator `/root/bga_preview_next_61`. It owns only the card's pacing paths;
T-471 owns stretcher paths. Root owns manifests/status and serial integration.
The shared Luna stays audio-owned until explicit release. Both builds must be
quiet for actual audio timing windows. A second worker does not change monitor
settings or access the desktop. Dense inspector/hover lookups and measure-index
rebuilds remain audit findings, not unmeasured justifications for a scene rewrite.
