# Draw workflow and BMSON conversion batch

Authorized 2026-10-08 after T-464. Base `f47c78a`.
Root owns main, manifests, scope approvals, final review, integration and cleanup.
Two Sol 6.1 medium coordinators share one sequential Luna medium executor;
all briefs give exact exclusive file ownership and release before review/build.
No additional worktrees or executors without root coordination.

- `/root/bga_preview_next_61`: `wormedit-draw-oct8`, branch `batch/draw-oct8`.
  T-412 first, then T-463 after its dependency passes. Draw source and explicit
  right-drag erase, dock source selection, shortcuts; then empty/active stem
  points, dock stem drag and packed keysound placement. Preserve T-464 resizing,
  razor/move gestures, one undo per stroke and source-deletion cancellation.
  T-413/T-414/T-389 and T-461/T-453 are outside this batch.
- `/root/budget_recovery_61`: `wormedit-conversion-oct8`, branch
  `batch/conversion-oct8`. T-466 then T-467 share conversion implementation/tests
  and therefore remain sequential in this tree. Reuse T-465 normalized distinct
  slice spans, deterministic WAV names and transactional publication. Preserve
  unrelated conversions and round trips; over-capacity/decoder failure writes
  nothing. Verify actual reimport/preview PCM and BMS profile capacity.

Luna `/root/budget_recovery_61/budget_luna` starts loaned to Draw after its stale
audit and contract design. Conversion queues narrow briefs through root; no
concurrent Luna handoffs or same-file edits. Coordinators may implement design/
glue while Luna owns disjoint tests or a concrete implementation slice.

Read AGENTS, workflow and canonical cards; stale audit before changes. Follow
steps in order, stop on unanswered explicit decisions, and request exact root
approval for necessary supporting paths before edits. Root owns CMake source
manifests and application integration paths; provide narrow patches if needed.
Root alone edits resume/implemented/coordination status.

Scratch `wormedit-scratch-builds/oct8-draw-conversion/{draw,conversion,root}`.
Fresh normal offline optimized builds use immutable existing dependency source
overrides, explicit WORMEDIT_OTOWORM_SOURCE_DIR and disconnected FetchContent.
No temporary submodule symlinks, gitlink mutations or substituted optimized
production objects. Each worker uses at most two compile jobs while concurrent;
root uses four after releases. Keep original charts/audio read-only and all
outputs in scratch. Preserve logs/configuration/probes outside removed trees.

No user-desktop windows, app restarts or Xvfb under the existing restriction.
Use the full production frontend event harness and separate SDL dummy/software
captures; state native display/GPU limitations. Run every card gate and docs
after planning edits. Check configured cognitive complexity, including changed
headers and baseline scores: new functions <=25, no NOLINT or increased old
violations. Coordinators review actual diffs/results, commit clean source and
explicitly release all writes/jobs. Root integrates serially, normally builds
and tests all main, commits planning results and removes clean worker builds.
