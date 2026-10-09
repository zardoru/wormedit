# Beat handles and navigation polish

[Back to task cards](../tasks.md).

## T-504 Restore group-beat fitting handles

- **Status:** done
- **Stale-plan audit (2026-10-09):** Group lines are excluded from both rail drawing and hit testing.
- **Outcome:** All integral beat/group/measure rail ticks offer matching fit handles, with existing lock and row-zero guards.
- **Read first:** `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, [T-300](compose-ux.md#t-300-add-tab-modified-measure-navigation-and-double-speed-wheel-scrolling).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose_playhead_skip.h`, `src/sys/compose_playhead_skip.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose_navigation_travel.h`, `src/sys/main.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
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
- **Read first:** `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, [T-300](compose-ux.md#t-300-add-tab-modified-measure-navigation-and-double-speed-wheel-scrolling).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose_playhead_skip.h`, `src/sys/compose_playhead_skip.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose_navigation_travel.h`, `src/sys/main.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
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
- **Read first:** `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, [T-300](compose-ux.md#t-300-add-tab-modified-measure-navigation-and-double-speed-wheel-scrolling).
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/sys/compose_playhead_skip.h`, `src/sys/compose_playhead_skip.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.h`, `src/sys/compose_navigation_travel.h`, `src/sys/main.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_beat_fit_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/sys/compose_playhead_skip_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `docs/chart-editor-plan/tasks/navigation-polish.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through the existing production paths. 2. Cover boundary and cancellation behavior. 3. Build, verify and update the Brain scratchpad.
- **Invariants:** chart objects, Undo history, snap preference, text/modal ownership and stopped navigation retain their existing semantics.
- **Reject/failure behavior:** stale chart/revision, locked timing and lost input ownership cannot retain gestures or navigation debt; interrupted travel cannot resume later.
- **Verify:** normal build, full CTest, production event harness, documentation validation, affected-source complexity checks and isolated Xvfb visual verification.
- **Completion (2026-10-09):** The logical cursor/range moves immediately while the viewport travels at ten standard measures per second (about 100 ms per standard measure). The first held repeat waits 350 ms; subsequent repeats remain 200 ms. OS repeats and delayed-frame debt cannot produce bursts. Input/focus interruption settles the owned destination after pointer cursor restoration, including edits advancing history; external cursor, viewport, zoom, chart, revision and playback changes retire obsolete travel. Fresh keyboard commands settle before editing, while OS repeat events leave travel alone.
- **Verification evidence:** normal build, **3,080/3,080 full tests**, **304/304 dedicated event-harness cases**, **13/13 focused checks**, and affected-source clang-tidy with no new/increased complexity findings pass. Isolated native Xvfb captures show every beat handle and intermediate/settled Tab travel; fixture SHA-256 remains `70965706e3c5aa93db6a40d8716e8b0d0ba27f99eca24c778856714cd77d381d`. Logs: `/tmp/navigation-{build-final,all-tests,events,focused,tidy,final-runtime-tidy}.log`; captures: `/tmp/navigation-visual/`. Implementation, tests and completion documentation are committed together.
- **Out of scope:** automatic commit, new shortcut bindings, playback transport redesign.
