# WSOLA and high-refresh pacing batch

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

## Reviewed implementation and integration

T-472 is integrated as `9ed59b8` (worker `f27b524`): whole-frame pacing
uses the remaining 5 ms active budget and a 50 ms invisible-window budget.
Already-paced or over-budget frames receive no extra delay. All 257 injected
policy checks pass; actual display cadence remains unmeasured.

T-471 uses a deterministic stride-four candidate/sample coarse search, followed
by full-precision refinement and a predicted-position fallback. Sparse energy
and bound arithmetic receive explicit regression coverage. Actual optimized
Kick callback measurements reduce WSOLA one-core utilization from about 2.62%
to 0.255–0.259% at 0.1×/0.5×, roughly 90% less work. Callback wall p99 falls
from 0.8645/0.8612 ms to 0.0980/0.1143 ms with no measured silenced voices or
zero postmix packets. These dummy-audio measurements precede the separate
unity/service follow-up; the correlation kernel is unchanged by that follow-up.
The older-device sensitivity proxy remains a proxy, not device certification.

User steering added two precisely recorded SDL supporting paths before edits.
Keep pitch now passes raw PCM through at 1× while preserving its setting and
existing mode fades, source clocks and tails. Routine active frame service no
longer repeats the callback-owned latency update through SDL's stream queue
lock. Conditional exact loop/drain queries remain. Sol's independent read-only
review approved the transitions and latency boundary; 52 focused audio tests
pass, including exact stereo PCM and completion with the stream lock held.
The before trace places every WSOLA/correlation call on the callback thread,
none on main or during stopped service: the UI could wait on the callback lock,
but stopped WSOLA was already absent, so no stopped/idle CPU reduction is claimed.
The final exact correlation-count trace records zero process/correlation calls
at 1× with Keep pitch enabled; reduced-rate calls remain callback-only. The
instrumented trace establishes call attribution, not performance timings.

Root independent optimized probes cover all nine supported rates and a wider
110 Hz–15 kHz comparison. Duration, deterministic/chunk-invariant bounded PCM
and isolated 440 Hz quality gates pass. Correlation choices change the old PCM;
near 15 kHz at 0.1× the measured zero-crossing estimate differs by about 0.11%.
This is not a listening test or bit-identical replacement claim. Logs remain
under `wormedit-scratch-builds/wsola-oct7/{audio,pacing,root}`. Original Dragback,
Kick and Solstice chart hashes remain unchanged (3/3).

Root integrates audio as `80e757a`/`7ffa395` (worker `5ce61a9`/`a9347bb`).
Normal main application build and all **2,574/2,574 tests pass** in 59.88 s,
including 196 event-harness cases. Merged optimized probes pass 18 all-rate
and 24 wider-tone cases and match the reviewed final outputs. Merged actual
production SDL functional traces confirm unity zero calls and reduced-rate
callback-only calls; these main Debug/instrumented traces supply no timing claim.
Both clean worker trees and about 1.2 GB of generated builds are removed; only
main remains registered, with branches/logs/probe binaries retained.
Live 200 Hz cadence, dense-chart whole-frame p99 and older-device performance
remain pending; existing dense inspector/hover and index audit findings are
measurement candidates for a later batch.
