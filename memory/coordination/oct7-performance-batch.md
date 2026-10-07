# October 7 workspace and stem projection performance batch

User requested the next delegated batch after the hybrid batch at `d8792c6`.
Root owns main, integration, build manifests and status/index documents.
Two worktrees only; Sol 6.1 medium coordinators reuse one Luna medium executor
sequentially under the four-agent limit.

- `wormedit-workspace-cache-oct7`, `batch/workspace-cache-oct7`: T-444 Sounds
  dock list, timing markers and BMS branch outline caches. Coordinator
  `/root/budget_recovery_61`.
- `wormedit-stem-row-table-oct7`, `batch/stem-row-table-oct7`: T-440 shared
  forward row-to-time projection table and bounded pixel-row stem sampling.
  Coordinator `/root/bga_preview_next_61`.

T-444 owns its listed production paths and exact supporting test paths recorded
on its own card before edits. T-440 does likewise. Root permits separate changes
inside `src/sys/compose_view.h`: T-444 only adds its list/timing projection cache
fields and helpers; T-440 only adds the shared row table field/includes. No
cross-feature edits. Coordinators edit only their own T-444/T-440 section of
`stems-performance.md`; root serially reconciles the isolated header/card edits.
Any other overlap is escalated before edits. Root owns manifest changes unless
it explicitly hands over a narrow isolated edit. Both coordinators can record
necessary exact supporting ownership extensions within their outcome, respecting
out-of-scope limits and notifying root of shared paths.

Luna `/root/budget_recovery_61/budget_luna` executes narrow owned slices, first
for T-444 then for T-440 after explicit release. Coordinators implement the
complementary slices, own all build writes, review and commit the final work.
If Luna capacity fails repeatedly, the assigned Sol completes that slice.

Scratch is `/home/zardoru/devel/wormedit-scratch-builds/perf-oct7` (workspace,
stems, root), never ephemeral `/tmp`. Use separate normal dependency-aware
builds, immutable main dependency source overrides and no network fetches;
restore temporary libotoworm symlinks before status/commit. No `/fast`, no CTest
during relinks. New C++ functions and tests <=25 configured cognitive complexity;
existing violations cannot increase. Source charts are read-only. No desktop
windows, source chart edits, pushes or user app restarts.

T-438 explicitly allows the event-harness frame probe if Xvfb is unavailable;
the established sandbox restriction remains. Use production headless frame and
projection probes, with Release before/after phase measurements on actual chart
inputs and operation counts. Report unavailable GUI/perf shares separately and
do not claim those percentage gates passed without measuring them. No new Xvfb
retry or live desktop profiling is authorized by this batch.

Keep exact editing/slicing behavior unchanged. T-440 display sampling preserves
continuation, stops/warps and cut placement, ring reuse and all linked-stem
filtering from T-460. T-444 caches must account for full projection requests and
identities, with per-frame selection/viewport behavior preserved. Do stale-plan
checks first and follow card steps in order. T-469/T-470/T-471 and hybrid UI or
conversion writers are outside this batch.

Root reviews actual diffs and independent evidence, integrates serially, runs a
normal main application build and complete suite, updates durable status and
retires both clean workers and generated builds, retaining branches/logs.

## Verification and ownership refinements

Fresh optimized builds would otherwise freeze all source work. Root approves
same-binary Release reference/cached measurements with identical chart inputs,
retained uncached projection APIs and a narrow internal timing-cache disable
option for tests/probes. Stop active builds cleanly before source edits, then
resume normal dependency-aware builds. Do not describe these as untouched
historical Release builds. Root separately captured the original main Debug
production output and timings before either integration.

T-444 supporting ownership: workspace_projection.h (optional cache argument),
application_frame_projection.cpp (pass list cache), compare_view.h (branch cache),
sound_list_projection_tests.cpp, workspace_tests.cpp. Shared frame_timing_tests.cpp
is split: T-444 appends only dedicated list/timing cache cases and helpers;
T-440 owns existing waveform fixture changes and new row-table cases. Ordinary
retained BMS statement/sidebar rows remain visible even without choice blocks.

T-440 supporting ownership: aligned_waveform.cpp/.h (shared song clock),
waveform_view_projection.cpp (same row table), exact waveform tests recorded on
its card. Fresh stl_solst1 has no continuation points and is now entirely packed;
a detached dense benchmark may explicitly link its 181 retained channels as
stems to represent a migrated legacy saved chart, clearly labeled as constructed
intent. Fresh Kick remains the real visible-stem input. No original is changed.

## Reviewed integration and final evidence

T-444 worker `0301923` is integrated as `b5fb2b7`; T-440 worker `ef51e0f`
is integrated as `69abfa4`. Root retained both independently appended test
regions when resolving their shared-file conflict. Planning and supporting
ownership refinements are committed as `a849bdc` and `2d399ca`.

Normal combined main application build passes, and all **2,544/2,544 CTest
cases pass** in 51.64 seconds. Root production probes against the merged main
retain byte-identical Dragback snapshots at all three scroll windows and
unchanged old/reference Kick projection. The new Kick path passes **14,560**
bidirectional zero-amplitude-tolerance peak comparisons within one pixel over
nine zoom/scroll windows. All three original chart hashes remain unchanged.
Logs/probes/snapshots are retained under `wormedit-scratch-builds/perf-oct7/root`:
`main-build.log`, `main-ctest.log`, `workspace-main-parity.log`,
`stem-main-parity.log`, and `source-hash-check.log`.

Optimized same-binary production evidence (worker logs retained):

- Dragback observed combined projection p50 **1.006070 → 0.296084 ms**;
  p99 **1.762030 → 0.753129 ms**. The uncached Release reference already met
  the 3 ms target. Cached snapshots match both that reference and the
  original root Debug baseline; all four caches build once in 60 frames.
- Fresh Kick's nine stems at zoom 32: complete projection **137.77–192.60 →
  1.17–1.18 ms**. One-pixel scrolling projects nine ring rows and reuses
  5,409, building one shared clock row.
- Constructed legacy dense-stem intent over stl's 181 real retained channels:
  complete low-zoom projection **331.31–469.22 → 0.58–1.32 ms**. This is
  explicitly not fresh-import classification, which remains packed.
- Complete-pyramid benchmark quadruples source length with identical operation
  counts. Partial/base-only pyramids preserve available-bin aggregation without
  claiming the same bounded-bin cost. RMS now uses max matching-level bin RMS,
  with edge refinement; exact edit/slice math and PCM sample-point paths stay
  on the existing implementation. Peak placement and RMS policy have separate
  explicit tests.

The requested live main-thread shares (<1% for each workspace projection and
<10% for stem projection) remain unmeasured under the existing GUI restriction;
these headless phase measurements do not establish those percentages. Both cards
are done in code with the live capture gate pending. Next performance work is
T-470, then T-469 after measuring the integrated renderer, and T-471.

Both clean, explicitly released worker trees and their generated builds/tmp
directories are removed, reclaiming about **4.5 GB**. Only main remains
registered; both worker branches and all durable logs/probes are retained.
