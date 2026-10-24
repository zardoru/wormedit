# Backlog reconciliation — 2026-10-24

Reconciled the non-deferred backlog against the current source and existing tests after
T-158/T-159/T-160/T-162. Only plan/status documentation changed. Deferred ambitions retain
their owner decisions; the existing supersession of T-272 is now represented in its status.

## Inventory

There are 681 task cards. Counts below describe card statuses, not estimated remaining effort.
The earlier completed count includes T-743's `complete` spelling, now normalized to `done`.

| Status | Before | After |
|---|---:|---:|
| Completed | 622 | 622 |
| Ready | 4 | 2 |
| Partial | 6 | 11 |
| Blocked, including user input | 7 | 4 |
| Deferred | 41 | 40 |
| Superseded | 1 | 2 |

Seventeen cards remain non-deferred and unfinished. Five changed classification because
implementation already exists but does not satisfy the complete outcome. No unfinished feature
was marked complete merely because its panel, cache or source model exists.

## Corrected cards

| Card | Reconciliation | Remaining work / evidence |
|---|---|---|
| T-231 | Blocked → partial; T-228 dependency cleared | Measures dock already virtualizes rows, supports Ctrl/Shift selection and navigation, and applies a meter to the selection. Exact batch length-ratio editing remains. See `compose_measures_panel.cpp`, `populate_measure_rows` in `compose_selection_view.cpp`, and the Measures dock event test. |
| T-232 | Blocked → partial; T-228/T-207 dependencies cleared | Layers and Markers are registered docks. Markers currently exposes bookmark navigation; `render_compose_marker_section` in `compose_inspector.cpp` still owns bookmark/section editing controls. Move those existing controls, then remove the Inspector duplication. |
| T-250 | Ready → partial | `field_tile_cache.cpp/.h`, retained static sources and cached/direct rendering are implemented. Existing card checkpoints establish bounded storage, invalidation and correctness coverage. Density-independent CPU/GPU work and the original performance target remain unproven; fresh current-tree measurements are required. |
| T-275 | Blocked → partial | Persistent C++ views and per-owner caches are implemented. T-243 and T-690–T-699 are complete; T-272 is superseded. Whole-workspace transport-only rebuild avoidance remains to be measured and completed. Replaced the obsolete arena steps with existing `keyed_cache_t`/owned-view integration. |
| T-287 | Ready → partial | Per-difficulty scoped program owners and exact authoring targets replace the old one-chart restriction. `prepare_bms_program` retains malformed source read-only; individual repair candidates, explicit apply/reparse and persistent dismissals remain. |
| T-272 | Deferred → superseded | The existing owner-approved replacement is T-690–T-699. Historical arena steps and measurements remain archived; they are not executable follow-up work. |
| T-743 | Complete → done | Canonical status spelling only. Its scoped authoring-tree implementation and recorded completion evidence are unchanged. |
| T-58 | Ready retained | Profile bindings support stage-qualified lanes, but the Compose stage selector and input focus integration remain. Replaced the deleted `src/ui/frontend.h` ownership with current view/action owners. |
| T-293 | Ready retained | Frame pacing exists, but rendering/presentation precedes its delay, VSync is requested, and no application presentation watchdog or explicit Wayland-first driver decision was found. Updated loop/backend ownership paths. Disabling VSync alone is not acceptance evidence for non-blocking acquisition. |
| T-277 | Blocked retained | Depends on T-275's remaining measured projection work. Removed the implicit dependency on the retired arena approach; current frame pacing is not proof of owner refresh throttling or presentation-time extrapolation. |

Canonical details, updated ownership and executable remaining steps live in the task cards:
[panels](../chart-editor-plan/tasks/panels-menus.md),
[transport](../chart-editor-plan/tasks/live-transport.md),
[rendering](../chart-editor-plan/tasks/rendering.md),
[BMS](../chart-editor-plan/tasks/bms-branches.md),
[editing](../chart-editor-plan/tasks/editing-commands.md), and
[scoped authoring](../chart-editor-plan/tasks/bms-scoped-authoring.md).

## Other open work retained

- **T-127:** Custom profile mapping still needs a per-lane editor; existing Metadata code
  explicitly disables Custom. The card's remaining verification requirements stay open.
- **T-224:** conversion diagnostics still carry chart/path without an object or chart position
  (`conversion_diagnostic_t`); import/export target production remains necessary.
- **T-432/T-433:** fresh-reader review, remaining walkthrough screenshots, troubleshooting and
  walkthrough exercises remain. The comparison chapter's existing exercise and the new Lua
  documentation do not complete both original walkthrough outcomes.
- **T-644/T-671:** keep bounded test-review/helper checkpoints partial; this reconciliation does
  not establish project-wide semantic test review or fixture consolidation.
- **T-31:** actual external Windows clipboard captures still require user-supplied evidence.
- **T-434/T-435:** tutorial timing/outline decisions and workflow-chapter prerequisites remain.

Ready cards are now **T-58 and T-293**. Partial cards are **T-127, T-224, T-231, T-232,
T-250, T-275, T-287, T-432, T-433, T-644 and T-671**. Blocked cards are **T-31, T-277,
T-434 and T-435**. Remaining deferred cards were not selected for implementation or un-deferred.

## Verification

Existing built tests were executed to check the implementation facts used above:

```text
build/tests/wormedit_frontend_event_harness_tests '[measures],[dock-layout]'
All tests passed (50 assertions in 8 test cases)

build/tests/wormedit_field_renderer_tests '[tile]'
All tests passed (202 assertions in 9 test cases)

ctest --test-dir build -j 4 --output-on-failure -R '^documentation\.'
100% tests passed out of 5
```

The focused tests verify existing dock behavior and tile logic. Current-tree CPU/GPU acceptance,
niri presentation-stall recovery, new-reader testing and external clipboard captures remain open.
No new implementation, performance result or full GATE run is claimed by this reconciliation.
