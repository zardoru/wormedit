# Beat handles and navigation polish

[Back to task cards](../tasks.md).

## T-504 Restore group-beat fitting handles

- **Status:** done
- **Stale-plan audit (2026-10-09):** Group lines are excluded from both rail drawing and hit testing.
- **Outcome:** All integral beat/group/measure rail ticks offer matching fit handles, with existing lock and row-zero guards.
- **Read first:** `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, [T-300](compose-ux.md#t-300-add-tab-modified-measure-navigation-and-double-speed-wheel-scrolling).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_playhead_skip.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose/input/compose_navigation_travel.h`, `src/sys/main.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through the existing production paths. 2. Cover boundary and cancellation behavior. 3. Build, verify and update the Brain scratchpad.
- **Invariants:** chart objects, Undo history, snap preference, text/modal ownership and stopped navigation retain their existing semantics.
- **Reject/failure behavior:** stale chart/revision, locked timing and lost input ownership cannot retain gestures or navigation debt; interrupted travel cannot resume later.
- **Verify:** normal build, full CTest, production event harness, documentation validation, affected-source complexity checks and isolated Xvfb visual verification.
- **Completion (2026-10-09):** A shared predicate now draws and hits integral beat, group and measure rail handles, restoring the third beat. Snap-only lines, row zero and locked timing retain their guards.
- **Verification evidence:** normal build, **3,080/3,080 full tests**, **304/304 dedicated event-harness cases**, **13/13 focused checks**, and affected-source clang-tidy with no new/increased complexity findings pass. Isolated native Xvfb captures show every beat handle and intermediate/settled Tab travel; fixture SHA-256 remains `70965706e3c5aa93db6a40d8716e8b0d0ba27f99eca24c778856714cd77d381d`. Logs: `/tmp/navigation-{build-final,all-tests,events,focused,tidy,final-runtime-tidy}.log`; captures: `/tmp/navigation-visual/`. Implementation, tests and completion documentation are committed together.
- **Out of scope:** automatic commit, new shortcut bindings, playback transport redesign.

## T-505 Skip playback by beats independently of editing snap

- **Status:** done
- **Stale-plan audit (2026-10-09):** Keyboard and wheel playback skips both request the active editing snap.
- **Outcome:** Ordinary playback navigation skips quarter-note beats; measure navigation retains measure steps and stopped editing retains its snap.
- **Read first:** `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, [T-300](compose-ux.md#t-300-add-tab-modified-measure-navigation-and-double-speed-wheel-scrolling).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_playhead_skip.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose/input/compose_navigation_travel.h`, `src/sys/main.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through the existing production paths. 2. Cover boundary and cancellation behavior. 3. Build, verify and update the Brain scratchpad.
- **Invariants:** chart objects, Undo history, snap preference, text/modal ownership and stopped navigation retain their existing semantics.
- **Reject/failure behavior:** stale chart/revision, locked timing and lost input ownership cannot retain gestures or navigation debt; interrupted travel cannot resume later.
- **Verify:** normal build, full CTest, production event harness, documentation validation, affected-source complexity checks and isolated Xvfb visual verification.
- **Completion (2026-10-09):** Ordinary playing arrows and wheel steps seek quarter-note boundaries independently of editing snap, including unsupported snap denominators. Chart bounds, fractional resolutions and both scroll directions are covered; measure navigation and stopped editing keep their existing units.
- **Verification evidence:** normal build, **3,080/3,080 full tests**, **304/304 dedicated event-harness cases**, **13/13 focused checks**, and affected-source clang-tidy with no new/increased complexity findings pass. Isolated native Xvfb captures show every beat handle and intermediate/settled Tab travel; fixture SHA-256 remains `70965706e3c5aa93db6a40d8716e8b0d0ba27f99eca24c778856714cd77d381d`. Logs: `/tmp/navigation-{build-final,all-tests,events,focused,tidy,final-runtime-tidy}.log`; captures: `/tmp/navigation-visual/`. Implementation, tests and completion documentation are committed together.
- **Out of scope:** automatic commit, new shortcut bindings, playback transport redesign.

## T-506 Give Tab measure navigation visible travel

- **Status:** done
- **Stale-plan audit (2026-10-09):** T-300 repeats immediately every 200 ms and viewport changes jump directly; StepMania uses a trailing display beat ([upstream ScreenEdit.cpp](https://github.com/stepmania/stepmania/blob/5_1-new/src/ScreenEdit.cpp#L1775-L1781)).
- **Outcome:** Tab moves the logical cursor immediately, scrolls toward its target over a short bounded transition and pauses before held repeats.
- **Read first:** `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, [T-300](compose-ux.md#t-300-add-tab-modified-measure-navigation-and-double-speed-wheel-scrolling).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_playhead_skip.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose/input/compose_navigation_travel.h`, `src/sys/main.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through the existing production paths. 2. Cover boundary and cancellation behavior. 3. Build, verify and update the Brain scratchpad.
- **Invariants:** chart objects, Undo history, snap preference, text/modal ownership and stopped navigation retain their existing semantics.
- **Reject/failure behavior:** stale chart/revision, locked timing and lost input ownership cannot retain gestures or navigation debt; interrupted travel cannot resume later.
- **Verify:** normal build, full CTest, production event harness, documentation validation, affected-source complexity checks and isolated Xvfb visual verification.
- **Completion (2026-10-09):** The logical cursor/range moves immediately while the viewport travels at ten standard measures per second (about 100 ms per standard measure). The first held repeat waits 350 ms; subsequent repeats remain 200 ms. OS repeats and delayed-frame debt cannot produce bursts. Input/focus interruption settles the owned destination after pointer cursor restoration, including edits advancing history; external cursor, viewport, zoom, chart, revision and playback changes retire obsolete travel. Fresh keyboard commands settle before editing, while OS repeat events leave travel alone.
- **Verification evidence:** normal build, **3,080/3,080 full tests**, **304/304 dedicated event-harness cases**, **13/13 focused checks**, and affected-source clang-tidy with no new/increased complexity findings pass. Isolated native Xvfb captures show every beat handle and intermediate/settled Tab travel; fixture SHA-256 remains `70965706e3c5aa93db6a40d8716e8b0d0ba27f99eca24c778856714cd77d381d`. Logs: `/tmp/navigation-{build-final,all-tests,events,focused,tidy,final-runtime-tidy}.log`; captures: `/tmp/navigation-visual/`. Implementation, tests and completion documentation are committed together.
- **T-587 follow-up (2026-10-12, NV7):** Playback starting during owned travel now finishes its destination before retiring it. External cursor, viewport, zoom, chart or revision changes still retire obsolete travel without overwriting the replacement state. T-509's explicit key-release freeze remains unchanged.
- **Out of scope:** automatic commit, new shortcut bindings, playback transport redesign.

## T-507 Shorten held Tab navigation repeats

- **Status:** done
- **Stale-plan audit (2026-10-09):** T-506 uses 200 ms repeats and Page keys share one-measure actions with Tab arrows. User requests faster repeats and a two-beat Page-key minimum and doubled distance with Tab.
- **Outcome:** Held Tab navigation repeats every 100 ms after the existing 350 ms initial pause.
- **Read first:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_state.h`, `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction_cursor.cpp`, `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_playhead_skip.cpp`, `tests/core/interaction_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `include/wormedit/core/editor_navigation_input.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose/input/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Refine cadence or typed Page navigation. 2. Verify held/released routing, both directions, playback and chart bounds. 3. Update status and Brain.
- **Invariants:** initial pause, motion speed, Shift ranges, editing snap and Undo semantics remain intact; ordinary arrows retain snap/beat units.
- **Reject/failure behavior:** stale input and lost ownership cannot retain repeat debt; page movement clamps at chart ends.
- **Verify:** normal build; focused navigation tests; full CTest; production event harness; documentation validation; affected-source cognitive complexity.
- **Completion (2026-10-09):** Held Tab repeats use 100 ms after the existing 350 ms pause. Page keys use one measure with a two-beat minimum; Tab+Page uses two measures with a four-beat minimum. Both stopped cursor and playing preview cross tiny measures, clamp at canvas/chart ends and preserve chart/Undo state. Tab arrows retain one-measure steps.
- **Verification evidence:** normal build, **3,084/3,084 full tests**, **12/12 focused navigation checks**, **305/305 dedicated event-harness cases**, documentation validation, and affected-source clang-tidy with no new/increased complexity violations pass. Logs: `/tmp/navigation-tuning-{build-final,all-tests,focused,tidy,tidy-core-final}.log`. Implementation, tests and completion documentation are committed together.
- **Out of scope:** automatic commit, new shortcuts or changed travel speed.

## T-508 Keep Page navigation moving through short measures

- **Status:** done
- **Stale-plan audit (2026-10-09):** T-506 uses 200 ms repeats and Page keys share one-measure actions with Tab arrows. User requests faster repeats and a two-beat Page-key minimum and doubled distance with Tab.
- **Outcome:** Page Up/Down moves or seeks one measure with a two-beat minimum; Tab doubles that distance and minimum. Tab arrows retain one-measure steps.
- **Read first:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_state.h`, `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction_cursor.cpp`, `src/sys/compose/input/compose_playhead_skip.h`, `src/sys/compose/input/compose_playhead_skip.cpp`, `tests/core/interaction_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `include/wormedit/core/editor_navigation_input.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose/input/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Refine cadence or typed Page navigation. 2. Verify held/released routing, both directions, playback and chart bounds. 3. Update status and Brain.
- **Invariants:** initial pause, motion speed, Shift ranges, editing snap and Undo semantics remain intact; ordinary arrows retain snap/beat units.
- **Reject/failure behavior:** stale input and lost ownership cannot retain repeat debt; page movement clamps at chart ends.
- **Verify:** normal build; focused navigation tests; full CTest; production event harness; documentation validation; affected-source cognitive complexity.
- **Completion (2026-10-09):** Held Tab repeats use 100 ms after the existing 350 ms pause. Page keys use one measure with a two-beat minimum; Tab+Page uses two measures with a four-beat minimum. Both stopped cursor and playing preview cross tiny measures, clamp at canvas/chart ends and preserve chart/Undo state. Tab arrows retain one-measure steps.
- **Verification evidence:** normal build, **3,084/3,084 full tests**, **12/12 focused navigation checks**, **305/305 dedicated event-harness cases**, documentation validation, and affected-source clang-tidy with no new/increased complexity violations pass. Logs: `/tmp/navigation-tuning-{build-final,all-tests,focused,tidy,tidy-core-final}.log`. Implementation, tests and completion documentation are committed together.
- **Out of scope:** automatic commit, new shortcuts or changed travel speed.


## T-509 Share measure repeat cadence and stop travel on release

- **Status:** done
- **Stale-plan audit (2026-10-09):** User reports plain Page repeat stalling and trailing motion after release. Plain Page keys still use the ordinary navigation controller; release intentionally leaves travel active.
- **Outcome:** Plain Page keys use the Tab repeat path with the existing two-beat minimum. Releasing Tab or the direction key drops remaining viewport travel; destinations finish within 75 ms, before the next 100 ms repeat without accumulating displacement debt.
- **Read first:** `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose/input/compose_navigation_travel.h`, `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose/input/compose_navigation_travel.h`, `src/sys/compose/input/compose_input.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Share cadence and release routing. 2. Verify repeat, modifier transitions, release and bounded travel through production paths. 3. Update status and Brain.
- **Invariants:** Page distances, snap/beat arrows, chart content, Undo and range selection remain intact.
- **Reject/failure behavior:** release, ownership loss and delayed frames cannot replay obsolete repeat or travel debt.
- **Verify:** normal build; focused navigation tests; full CTest; production event harness; documentation validation; affected-source cognitive complexity.
- **Completion (2026-10-09):** Plain Page Up/Down uses the shared Tab cadence: immediate step, 350 ms initial pause and 100 ms held repeats, discarding OS repeats and delayed-frame debt. Tab changes Page distance while held; key release drops remaining viewport travel at its current position. Every latest destination completes within 75 ms, so large pages cannot accumulate a motion backlog. Cursor/range changes remain immediate; chart and Undo state remain unchanged.
- **Verification evidence:** normal build, **3,088/3,088 full tests**, **17/17 focused checks**, **306/306 dedicated event-harness cases**, documentation validation, and affected-source clang-tidy with no new/increased complexity violations pass. Logs: `/tmp/page-repeat-{build-final,all-tests-final,focused-final,tidy,tidy-final}.log`. Implementation, tests and completion documentation are committed together.
- **Out of scope:** automatic commit, new shortcuts or changed Page distance.
