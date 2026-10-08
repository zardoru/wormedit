# Layer tooling and editing feedback batch

Authorized 2026-10-08 by the user. Base `d75af5c` (2,710 main tests pass).
Root owns main integration, manifests, status ledgers, scope arbitration and
final verification/cleanup. Two Sol 6.1 medium coordinators use one sequential
Luna medium executor slot; exact ownership is released before another writer.

## Worktrees and sequence

- `wormedit-layer-oct8`, branch `batch/layer-oct8`: T-413, then T-414.
- `wormedit-feedback-oct8`, branch `batch/feedback-oct8`: T-419, then T-420.
- Evidence/builds: `/home/zardoru/devel/wormedit-scratch-builds/oct8-layer-feedback`.

First perform the stale-code audit. T-412 and T-436 are complete; T-393 already
removed the inspector layer section. Preserve its removal rather than reviving
the old paint button. Update obsolete dependencies on owned cards accordingly.

## Exclusive phases

Initial Layer ownership: T-413 interaction header/source, exact existing core
interaction/layer test paths identified by coordinator, Compose input,
viewport input and Layers dock. Inspector is read-only: T-393 is already done.
Initial Feedback ownership: T-419 inspector, quantize preview source/header,
Compose projection, frontend contract and exact existing event-harness test
paths identified by coordinator. Feedback owns `editing-feedback.md`; Layer
owns `draw-tool.md`. Neither edits root status/manifests/coordination.

Shared `tests/ui/nuklear_event_harness_tests.cpp` initially belongs to Feedback;
Layer must select disjoint existing harness translation units or request a
sequential loan. Any supporting-file extension requires root approval before
edits. New source/test files and build manifests remain root-owned.

Layer completes T-413 and releases interaction paths before Feedback starts
T-420. Feedback completes T-419 and releases frontend/projection before Layer
starts T-414. Root integrates each reviewed checkpoint serially; coordinators
refresh the next phase onto that integrated checkpoint before editing overlaps.
T-414 then owns frontend/projection/canvas/panel while T-420 may work only in
released core interaction paths. Feedback waits for T-414 release/integration
before T-420 frontend/projection/canvas and shared tests. No simultaneous writes
or builds over a changing dependency source tree.

## Verification and invariants

One undo per layer stroke/move; locked/hidden/stale rejection is atomic. Add
and remove are idempotent over mixed memberships. Quantize payloads/custom
validation stay compatible, and algorithm/performance wins stay intact.
Collision ghosts track every proposed target; invalid release does not change
revision/undo. Preserve Draw/BMSON/BGA routing, source cancellation, tool registry,
canvas input outside chip bounds, viewport clipping and UTF-8 label ownership.

Normal application/event builds, card-required core/quantize/event suites,
header-inclusive configured clang-tidy with no new/increased >25 findings,
documentation gate after card edits and diff checks are required. Initial
parallel builds use at most two jobs per coordinator; root may grant more after
the other build releases. Use immutable existing dependency sources from main's
build, ordinary CMake targets, no symlink/gitlink or production-object swaps.
No user desktop windows or app restarts. Windowless event harness and isolated
SDL dummy/software captures are acceptable evidence; report native display and
cadence as unmeasured. Root performs final normal main full suite and captures,
commits planning updates, then removes released clean worktrees/generated builds
while retaining branches, configurations, runtime and evidence.
