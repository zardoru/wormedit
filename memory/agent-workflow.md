# Agent workflow

Use this workflow when collaboration agents are available.

## Roles

- **Coordinator:** `gpt-5.6-sol`, high reasoning. Owns scope decomposition, requirement traceability, file ownership, source selection, acceptance criteria, and review. It does not hand an ambiguous design problem to the executor.
- **Executor:** `gpt-5.6-luna`, high reasoning. Receives a narrow written brief, edits the assigned files, runs the specified checks, and reports exact changes, evidence, and unresolved issues.
- **Root integrator:** owns the user conversation, resolves cross-task decisions, prevents conflicting edits, checks the coordinator's review, and updates the owning slice linked from [`chart-editor-status.md`](chart-editor-status.md).

## Cycle

1. Root gives Sol the current user requirements plus links to the canonical plan, status, relevant specifications, code, and fixtures.
2. Sol reads those sources, checks the request against settled decisions, and creates one reviewable Luna task. Sol assigns exclusive file ownership for the task.
3. Luna implements the brief with `apply_patch`, performs the requested positive and negative validation, and returns a concise change/evidence report to Sol.
4. Sol reviews the actual diff/files and test output against every acceptance criterion. It sends precise correction tasks to Luna until the slice is complete.
5. Sol reports the reviewed result and any decision that needs root integration. Root performs a final consistency pass and updates durable memory.

Do not let Sol and Luna edit the same file concurrently. Parallel Luna tasks are allowed only when their file ownership and generated artifacts cannot overlap.

## Required Luna brief

Every executor brief includes:

- **Outcome:** one concrete behavior or planning result.
- **Files owned:** exact paths Luna may edit.
- **Context:** relevant decisions and source links, including any format/version profile.
- **Invariants:** data that must remain lossless, transactional, deterministic, or cross-platform.
- **Required behavior:** explicit positive cases.
- **Rejection behavior:** explicit negative cases and the required unchanged state after failure.
- **Checks:** exact tests, searches, builds, or document consistency checks to run.
- **Out of scope:** adjacent work that must remain for a later task.
- **Report:** changed files, validation evidence, remaining risks, and suggested status-memory update.

## Planning-task brief example

```text
Outcome: Add the parametric SV curve contract to the Lua DSL draft.
Files owned:
  - docs/osbx-draft-spec.md
Context:
  - docs/chart-editor-plan.md
  - memory/chart-editor-status.md
  - User requires p in [0,1], SV/SCROLLS/SPEEDS output, linear/exponential,
    finite bump, zero-width impulse, and post-process normalization.
Invariants:
  - Dirac is a structural impulse with finite area, not a sampled infinity.
  - Bump Len(B) = TotalSegment - Len(A).
  - Unit-baseline balance is X*a + Y*b = 1 for normalized lengths.
  - Preview/apply is deterministic and transactional.
Positive cases:
  - Solve X, solve Y, scale both, and retain an impulse through normalization.
Negative cases:
  - Len(A) outside (0,L), zero denominator, NaN/overflow, point cap exceeded,
    unsupported target value, and inexact output position.
Checks:
  - Search for contradictory normalization language.
  - Confirm the example syntax matches the described post-process order.
Out of scope:
  - Implementing the Lua runtime or chart model.
```

If spawning is unavailable, root follows the same brief/review structure locally and leaves enough state here for the requested agents to resume later.
