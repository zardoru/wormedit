# Dense-chart performance batch

User authorized the next performance batch on 2026-10-07. Root owns main and
integration. T-475 measures then fixes dense interaction/projection hotspots;
T-476 measures the integrated production frame path in scratch artifacts.
T-278 receives a stale-plan audit: existing `bf0d817` already added priority
hooks, worker clamps and production hints. No duplicate implementation.

- Dense coordinator: `/root/bga_preview_next_61`, tree `wormedit-dense-oct7`,
  branch `batch/dense-oct7`; owns T-475 paths and card section only.
- Scheduling coordinator: `/root/budget_recovery_61`, tree
  `wormedit-scheduling-oct7`, branch `batch/scheduling-oct7`; read-only source
  audit, T-278 section and `docs/async-task-scheduling.md` documentation only.
- Existing Luna medium executor is available for one exclusive brief from the
  dense coordinator; source/test ownership transfers must be explicit.
- Root owns manifests, T-476, status, clean serial integration and final gates.

Scratch artifacts: `wormedit-scratch-builds/dense-oct7/{dense,scheduling,root}`.
Normal dependency-aware optimized builds use existing immutable dependency
sources without network. No timings while builds/tests run concurrently;
coordinators request exclusive measurement windows. No user desktop, app
restart or original-chart writes. Whole-frame useful work, omitted stages,
presentation/pacing wait and real display cadence must be distinguished.

Root preserves the two preexisting commits `b1450d2` and `279090b` and their
T-473/T-474 planned work; these are outside this batch. Clean released workers
and generated builds are removed after final gates; branches/logs retained.
