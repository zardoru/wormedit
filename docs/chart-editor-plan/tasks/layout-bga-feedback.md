# Task cards: compatible profiles and empty BGA lanes

[Back to the task-card index](../tasks.md).

Authorized 2026-10-11 from remaining priority scratchpad observations. Root coordinates shared view/action files.

## T-551 Offer compatible key profiles for created and custom layouts

- **Status:** done
- **Outcome:** Same-count all-key layouts expose existing osu!mania/StepMania profile choices regardless of their current profile ID or source format. The current layout is explicit until a deliberate selection. Segment choices also include these compatible profiles. Notes, lane IDs, bindings and stage placement remain unchanged; incompatible roles reject.
- **Read first:** AGENTS.md; include/wormedit/core/lane_profile_switch.h; src/core/lane_profile_switch.cpp; src/sys/compose/views/compose_view.cpp; src/sys/compose/actions/compose_actions.cpp; src/ui/nuklear/compose/compose_inspector.cpp; tests/core/lane_profile_switch_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/ui/bga_lane_event_harness_tests.cpp.
- **Files owned:** include/wormedit/core/lane_profile_switch.h; src/core/lane_profile_switch.cpp; src/sys/compose/views/compose_view.cpp; src/sys/compose/actions/compose_actions.cpp; src/ui/nuklear/compose/compose_inspector.cpp; tests/core/lane_profile_switch_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/ui/bga_lane_event_harness_tests.cpp; this card; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root approves supporting test changes within scope.
- **Steps:** 1. Reproduce current production behavior. 2. Implement shared eligibility and guarded live actions. 3. Verify round trips/Undo, input harness and native appearance.
- **Invariants:** no implicit profile conversion, dropped scratch/auxiliary lanes, changed lane identity or view-only document mutation.
- **Reject/failure behavior:** stale revisions and active gestures reject unchanged; incompatible roles/counts never silently convert.
- **Verify:** normal/Release builds, full CTest, production event harness, affected default clang-tidy, isolated authenticated TCP Xvfb, documentation and whitespace checks.
- **Out of scope:** assist-tick playback, loop gaps, timing dock, new layout families and manual link.

## T-552 Make the BGA visibility toggle reveal an empty lane

- **Status:** done
- **Outcome:** The toolbar button uses effective visibility and reveals the base lane on empty charts in one click. Hiding/showing preserves revealed kinds and authored events without editing the document.
- **Read first:** AGENTS.md; src/sys/compose/actions/compose_actions.cpp; src/ui/nuklear/compose/compose_toolbar.cpp; tests/sys/compose_action_notification_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/bga_lane_event_harness_tests.cpp.
- **Files owned:** src/sys/compose/actions/compose_actions.cpp; src/ui/nuklear/compose/compose_toolbar.cpp; tests/sys/compose_action_notification_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/bga_lane_event_harness_tests.cpp; this card; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root approves supporting test changes within scope.
- **Steps:** 1. Reproduce current production behavior. 2. Implement shared eligibility and guarded live actions. 3. Verify round trips/Undo, input harness and native appearance.
- **Invariants:** no implicit profile conversion, dropped scratch/auxiliary lanes, changed lane identity or view-only document mutation.
- **Reject/failure behavior:** stale revisions and active gestures reject unchanged; incompatible roles/counts never silently convert.
- **Verify:** normal/Release builds, full CTest, production event harness, affected default clang-tidy, isolated authenticated TCP Xvfb, documentation and whitespace checks.
- **Out of scope:** assist-tick playback, loop gaps, timing dock, new layout families and manual link.

Verification (2026-10-11): normal and Release builds pass; **3,341/3,341 full CTest checks** and **454 production event cases / 6,262 assertions** pass. Affected default clang-tidy has no new/increased complexity violations; unchanged legacy findings remain. Isolated authenticated TCP Xvfb verifies first-click empty BGA reveal/hide and deliberate Created 7K → osu!mania 7K selection. Native save comparison preserves notes, lane IDs, bindings and BGA events; one Undo restores the complete original document exactly. Documentation and whitespace checks pass. Evidence: `/tmp/layout-bga-final-{build,release,ctest,events,tidy,core-tidy}.log`, `/tmp/layout-bga-native-{before,shown,hidden,profile,choices,applied,undone}.png`, `/tmp/layout-bga-native-values.log`.
