# October 7 next implementation batch

User authorized another delegated implementation batch after the 10K color fix.
Base: `db7aec3`; root owns main, final review, serial integration and status updates.

- `/home/zardoru/devel/wormedit-stems-budget-oct7`, branch `batch/stems-budget-oct7`: T-439 stem-window revision cache followed by T-445 adjustable keysound memory budget. Own only those cards' listed source/test paths and their sections of `stems-performance.md`.
- `/home/zardoru/devel/wormedit-layers-oct7`, branch `batch/layers-oct7`: T-447 BMSON import channel layers followed by T-450 BMS Layer 2 model/round trips. Own only those cards' listed source/test paths and their sections of `bmson-bga-editing.md`.

Each worktree has a GPT-6.1 Sol medium coordinator and GPT-6 Luna medium executor. Coordinator records exact supporting ownership before edits and reviews implementation and evidence. Root resolves shared build-manifest/test registration changes during integration. No shared build-directory writes: each worktree uses an isolated build. No user desktop, chart-original edits, pushes or app restarts. Existing Xvfb restriction remains; use documented windowless harness/probes and report any outstanding visual gates honestly.

Do stale-plan checks first. Keep all new C++ functions within cognitive complexity 25 and do not increase existing violations. Preserve the already fixed long-stem visibility and mid-slice seek behavior. T-438, T-440, T-443, T-444, T-451 and T-453/T-455 are outside this batch. Coordinators do not edit root status/index files.

The new stems coordinator launched `/root/stems_budget_oct7_61/stems_luna`, then became unavailable for reactivation: collaboration returned `agent thread limit reached`. Root therefore reuses the existing `/root/editing_oct7_61` Sol coordinator for independent reviews and subsequent narrow briefs in both isolated worktrees; both Luna executors remain active. Worktree/source/build isolation is unchanged.

T-447 reviewed and integrated as `4ba5087`: application build and all 45 integrated `bmson_layer|semantic` cases pass. Root read-only production Kick probe now reports 12 channels/12 layers and checks all 515 materialized memberships, native serialization/reparse and identical BMSON export bytes with/without layers (`/tmp/wormedit-oct7-root/layer-import-integrated.log`). Original chart SHA-256 remains unchanged. The layers executor proceeds to T-450; T-439 remains under review for one authoritative frame selection after final cursor/viewport projection, then T-445 follows.

## Scratch storage and worktree cleanup (2026-10-07)

The user wiped `/tmp`; source commits and the active workspace builds survived.
Future root scratch probes and logs live in
`/home/zardoru/devel/wormedit-scratch-builds/root-oct7`, and worker build/log
artifacts also use workspace storage. Earlier `/tmp` evidence paths are historical
records of outputs read before the wipe; do not assume those files still exist.

At the user's request root removed 18 clean completed worktrees and pruned one
missing worktree registration. All branch refs remain, including the four older
branches whose original commits differ from their documented integration commits.
Only main and the current layers/stems-budget worktrees remain registered.

T-450 is integrated as `4c446cd`. The main application build and all
**2,446/2,446** CTest cases pass. Regenerated logs: workspace root scratch
`layer2-integrated-build.log` and `layer2-integrated-ctest.log`.
The independent root Layer 2 probe also passes distinct 04/06/07/0A kinds, exact
retained bytes, normalized export, schema-3 recovery and duplicate refusal
(`layer2-probe.log` in that same workspace directory).

T-439 final review found the two new Catch cases exceeded complexity 25 despite
passing behavior. Root assigned `tests/sys/frame_timing_tests.cpp` exclusively
to the existing Sol coordinator after the Luna ownership-release acknowledgement;
Luna retains `tests/sys/stem_window_tests.cpp`. Each splits its own test into
cohesive helpers and runs configured tidy. Root serially rebuilds both targets
and reruns focused and opt-in tests after both edits; no concurrent build writes.

After Layer 2's completed integration and verification, root also removed its
clean worktree, retaining `batch/layers-oct7`. Cleanup total is now 19 removed
trees plus one missing registration pruned; main and stems/budget remain.

To use the released coordinator slot while Luna finishes the independent T-439
benchmark helpers, root assigns the T-445 backend slice to Sol: exclusively
`include/wormedit/core/arena.h`, `src/core/arena.cpp`,
`include/wormedit/audio/resident_samples.h`, `src/audio/resident_samples.cpp`,
`tests/core/arena_tests.cpp` and the existing resident-sample tests. These paths
were already approved on T-445. Sol provides a locked by-value options snapshot
and grow-only hard-ceiling/live soft-budget setter with snapshot-retention tests.
Luna receives the remaining T-445 preference/UI/loader/application slice after
T-439's narrow commit. Root reviews the Sol backend, and all build writes remain
serialized; no backend edits may be included in the T-439 commit.
