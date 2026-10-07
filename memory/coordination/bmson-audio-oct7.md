# October 7 BMSON and audio batch

Branch `batch/bmson-audio-oct7`, base `14f8c25`; coordinator Sol 6.1 medium,
implementation Luna medium; root owns final main integration.

Cards execute in order: T-446, T-452, T-441, T-442. Initial symbol review
confirms all four outcomes are still missing. Read-only external evidence is
Kick Your Ass and Rotterdam; generated probes and PCM belong in temporary
directories. Source chart folders must remain unchanged.

Executor owns each card's listed files and these four card sections only.
No resume, plan-index or main-worktree edits. Necessary supporting ownership
extensions must be recorded and coordinated before edits. This note is an
approved supporting path. The concurrent editing batch owns Compose input,
note-to-BGM actions/view and profile-switch files; avoid those paths.

Use isolated build output. Main build objects/libraries may only be copied
read-only after root confirms the baseline build has finished. Never write
shared dependencies or the user desktop. Existing isolated Xvfb socket block
is recorded in `memory/coordination/README.md`; do not retry escalation.

Verification requires meaningful profile/export round trips, asset resolver
ambiguity and case tests, stem timing/duration regressions, and a non-silent
SDL dummy-audio mid-seek PCM probe. New functions must satisfy the configured
cognitive complexity gate at 25; existing complexity must not increase.
Root will perform final whole-application integration and full tests.


## Post-recovery evidence

The formatter cleanup dropped unreleased edits; Sol restored only semantic token
changes from the backup, preserving existing formatting. Root approved the
native load-repair support paths and the BMSON writer's uint64 source-x bound
fix before edits. The visual resolver classifies supported video declarations
at project-open time, preserves resource names, and marks ambiguous/missing
resources missing. No parser extension was needed.

- Isolated full build: `/tmp/bmson-recovery-build-final.log` (404/404 targets).
- Latest affected-object compile/relink, using the isolated compilation database:
  `/tmp/bmson-recovery-custom-build.log`.
- Core/format/open/stem tags: `/tmp/bmson-recovery-core-tags-final.log`,
  132 cases and 4,357 assertions passed.
- Native colors/order repair: `/tmp/bmson-recovery-native-final.log`,
  3 cases and 79 assertions passed.
- Card-focused audio/stem/documentation CTest: `/tmp/bmson-recovery-focused-final.log`,
  189/189 passed (20.52 seconds).
- Production Rotterdam: `/tmp/bmson-rotterdam-recovery.log`, 173 visuals,
  0 unresolved; source chart and assets remained read-only.
- Production Kick geometry: `/tmp/wormedit-oct7-root/kick-registry.log`;
  every stored stream lacked geometry after import, all 12 Ogg sources registered
  at 44,100 Hz with exact decoder frame counts. Visibility evidence:
  `/tmp/wormedit-oct7-root/kick-window-probe.log`, all 12 retained at measures
  1, 20 and 40, including fx/hat/pc.
- Configured production complexity checks: `/tmp/bmson-recovery-tidy.log`
  and `/tmp/bmson-recovery-tidy-gate.log`. Existing unrelated violations remain;
  new production functions and affected bridge production functions are <=25.
  Final test-wrapper recheck is `/tmp/bmson-recovery-test-wrapper-tidy.log`.

The headless build excludes SDL application tests; the explicit SDL CTest query
found no tests and is not PCM evidence. Root owns the new SDL dummy-audio PCM
case, whole-application build/full suite and final card/status updates after
integration. All four cards remain ready until those final gates. The existing
Xvfb socket/approval restriction remains recorded; no escalation was retried.
