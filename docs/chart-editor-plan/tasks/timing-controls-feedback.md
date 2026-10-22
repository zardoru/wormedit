# Task cards: timing controls feedback

[Back to the task-card index](../tasks.md).

## T-553 Group timing controls in the inspector and declutter the toolbar

- **Status:** done
- **Outcome:** The inspector has a Timing section with initial BPM, audio-offset lock/value/nudges, BPM snap and timing settings. Offset and BPM-snap toolbar controls appear only in Timing focus. Both surfaces share controls and session state; inline offset editing has one owner.
- **Read first:** AGENTS.md; src/ui/nuklear/compose/compose_toolbar.cpp; src/ui/nuklear/compose/compose_inspector.cpp; src/ui/nuklear/compose/compose_state.h; src/ui/nuklear/frontend.cpp; src/ui/nuklear/compose/compose_panel.cpp; tests/ui/bga_lane_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/compose/compose_toolbar.cpp; src/ui/nuklear/compose/compose_toolbar.h; src/ui/nuklear/compose/compose_inspector.cpp; src/ui/nuklear/compose/compose_state.h; src/ui/nuklear/frontend.cpp; src/ui/nuklear/compose/compose_panel.cpp; tests/ui/bga_lane_event_harness_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; tests/ui/timing_settings_tests.cpp; docs/chart-editor-plan/tasks/timing-controls-feedback.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root coordinates shared UI changes and approves supporting test updates within scope.
- **Steps:** 1. Audit current controls and routing. 2. Reuse renderers in an inspector Timing section, condition toolbar layout/rendering on Timing focus, and guard inline ownership. 3. Verify production input, exact actions, cancellation and native layout.
- **Invariants:** exact offset edits and one Undo use existing actions; snap remains session-only; locks and stale/modal/occluded guards remain; duplicate surfaces never submit two edits.
- **Reject/failure behavior:** hidden/collapsed editor owners cancel; locked/unavailable offsets do not nudge or edit; invalid input retains existing validation.
- **Verify:** normal/Release builds, full CTest, production event harness, affected default clang-tidy, isolated authenticated TCP Xvfb and documentation/whitespace checks.
- **Out of scope:** separate timing dock, assist ticks, loop audio changes and manual link.

- **Result (2026-10-11):** Shared offset and beat-fit snap renderers now appear in a collapsible inspector Timing section with initial BPM/counts/settings. Their toolbar columns and widgets appear only in Timing focus, which retains its existing canvas-only layout. Inline editor ownership cancels on collapse, panel/mode loss, narrow-window hiding and existing stale/modal/focus-loss guards. Normal/Release builds, **3,348/3,348 CTest checks**, **461 production event cases / 6,353 assertions**, affected default clang-tidy and documentation/whitespace checks pass. No new/increased complexity findings; unchanged legacy Catch macro findings remain. Native isolated authenticated TCP Xvfb verifies exact -1/8 offset save and one Undo restoring the complete original document, shared 0.5 BPM snap in Timing focus, and normal/focus toolbar layout at 1600×900 and 880×700. Evidence: `/tmp/timing-controls-{build,focused,final-ctest,final-events,final-release,tidy,final-tidy,final-tests-tidy}.log`, `/tmp/timing-controls-native-{inspector,editor,applied,undone,snap,focus,narrow-focus,narrow-normal}.png`, `/tmp/timing-controls-native-values.log`.
