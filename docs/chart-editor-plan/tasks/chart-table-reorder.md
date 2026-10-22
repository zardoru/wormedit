# Chart table header and grip reordering

[Back to task cards](../tasks.md).

## T-530 Chart table header and grip reordering

- **Status:** done (2026-10-11; verified; uncommitted user-feedback batch)
- **Outcome:** Match chart-table.svg header/row treatment and add visible Lucide grips with drag/drop insertion preview and one atomic undoable chart reorder.
- **Read first:** `src/ui/nuklear/project/setup_panel.cpp`, `src/ui/nuklear/project/setup_state.cpp`, `src/sys/project/project_view.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `docs/chart-editor-plan/mockups/chart-table.svg`.
- **Files owned:** `src/ui/nuklear/setup_panel.cpp/.h`, `src/ui/nuklear/setup_state.*`, `src/sys/project_view.*`, `src/ui/frontend.h` (project chart reorder payload only), existing core chart reorder API only if required, relevant project/frontend tests, `assets/icons/lucide/grip-vertical.svg`, `src/ui/nuklear/render/lucide_icons.h`, existing Lucide generator and regenerated pixel header (append the grip icon without changing existing IDs). Coordinator adds the focus-loss cancellation hook in `src/ui/nuklear/frontend.cpp`. This card; coordinator owns shared index/checker/status and integration. Coordinate separate frontend payload regions; append tests through incremental patches.
- **Steps:** 1. Audit implementation and mockup. 2. Implement the requested behavior through existing runtime boundaries. 3. Add meaningful interaction/persistence regressions. 4. Verify integrated build, full suite, native captures and complexity. 5. Reconcile Brain and local status.
- **Invariants:** Dragging starts only on the grip. Chart identity stays stable; reorder uses revision/session guards and one atomic Undo. Existing selection, Open and context actions remain available.
- **Reject/failure behavior:** Stale or missing targets reject safely; interrupted gestures cancel. Missing recent files report diagnostics through the existing open path. Dropdown closure cannot click through.
- **Verify:** Normal/release builds, relevant focused tests, complete production event harness and CTest suite, affected-file clang-tidy, isolated Xvfb native inspection and documentation validation.
- **Done so far:** Empty imported chart names now display as “Untitled chart” and remain selectable, preventing Nuklear from asserting on a valid StepMania import. Added rounded full-width header background, selected-row fill and pinned-source Lucide grip icons. Grip dragging previews before/after insertion and dispatches a revision/session-guarded reorder through the existing atomic Undo command. Escape, focus loss, hidden/stale context, outside release and clipped targets cancel safely.
- **Verification evidence:** Normal and release builds pass. Full CTest: 3,195/3,195 passed without retries. Production frontend event harness: 391 cases / 5,640 assertions. No new or increased affected-source complexity violations; existing unrelated warnings remain. Native verification uses isolated Xvfb. Evidence: `/tmp/feedback-batch-{ready-build,ready-release,all-tests,events,skins,frontend-tidy}.log`. State checks pass 24 assertions / 2 cases; command checks 19 assertions / 2 cases. Production drag/cancellation events pass. Native captures show the insertion line, first-to-last reorder and single Undo restoration: `/tmp/chart-table-{before,drag,after,undo}.png`.
- **Remaining:** None.
- **Out of scope:** Automatic commit, unrelated editor feature work, changes to authored chart content beyond explicit chart reordering.
