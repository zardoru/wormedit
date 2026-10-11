# BGA lane discoverability

[Back to task cards](../tasks.md).

## T-540 Verify and expose BGA Layer 2 and Poor lanes

- **Status:** ready (priority user feedback, 2026-10-11)
- **Outcome:** Users can find and edit Layer 2 and miss/Poor BGA lanes, including on charts where those lanes begin empty.
- **Read first:** AGENTS.md; docs/chart-editor-plan/tasks/bmson-bga-editing.md T-450/T-451; src/ui/nuklear/compose_bga_panel.cpp; src/ui/nuklear/compose_canvas.cpp; tests/ui BGA event cases.
- **Files owned:** src/ui/nuklear/compose_bga_panel.cpp; src/ui/nuklear/compose_canvas.cpp; relevant BGA production event tests; this card. Escalate supporting projection/runtime changes to coordinator.
- **Steps:** 1. Reproduce the scratchpad observation with populated and empty lanes. 2. Audit the existing four-kind picker, hidden-empty-lane policy and preview behavior against T-450/T-451. 3. Fix the concrete missing or undiscoverable path without conflating Layer 2, Layer or Poor. 4. Verify production input, save/reopen and isolated native evidence; reconcile the scratchpad.
- **Invariants:** Preserve authored event kind, resource, timing and provenance. Revealing an empty lane is view-only. Layer 2 stays distinct from Layer. Poor/miss behavior retains its specified preview policy.
- **Reject/failure behavior:** Stale chart/revision, unsupported formats and missing resources reject with clear feedback and no partial edits.
- **Verify:** Production event harness for revealing/placing/moving each supported kind, core round trips, affected clang-tidy, normal build, full CTest, documentation checker and isolated authenticated native captures.
- **Done so far:** Fresh scratchpad observation retained. Initial source audit finds all four labels (BGA, Layer, Layer 2, Poor) in compose_bga_panel.cpp; T-450/T-451 document prior implementation. This does not yet establish that the reported UI path works or is discoverable.
- **Remaining:** Reproduce and complete the steps above in the next batch.
- **Out of scope:** Reimplementing already preserved BMS event kinds or changing unrelated semantic note layers.
