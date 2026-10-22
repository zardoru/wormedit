# Priority user feedback: navigation and waveform controls

[Back to task cards](../tasks.md).

User feedback from the Brain scratchpad, authorized 2026-10-11. Worktree `feedback/navigation-oct11`; every item is priority user feedback. Coordinator owns integration and shared plan/status files.

## T-519 Switch the active mode family between upscroll and downscroll

- **Status:** done (2026-10-11; verified; committed with this batch)
- **Outcome:** A Compose toolbar direction icon toggles upscroll/downscroll for the active mode family and persists that preference without editing chart data.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/sys/app/main.cpp`.
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp` (navigation group/layout only), `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h` (action/options fields only), `src/sys/app/main.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp` and `tests/ui/inspector_resource_workflow_tests.cpp` (toolbar geometry fixtures), this card.
- **Steps:** 1. Audit existing mode-family direction preferences. 2. Add toolbar toggle through the revision-checked action boundary and persist through session preferences. 3. Verify direction/cursor/history and stale input; inspect isolated Xvfb toolbar.
- **Invariants:** Notes, lane identity, cursor row, zoom, document revision and Undo remain unchanged; other mode families retain their preferences.
- **Reject/failure behavior:** Reject stale actions and pending chart gestures; preference save failures use existing diagnostics.
- **Verify:** `cmake --build build -j2`; `ctest --test-dir build --output-on-failure -R 'compose_action_notifications|documentation.chart_editor_state'`; affected-source `clang-tidy -p build`; final integrated full suite and isolated Xvfb direction check.
- **Out of scope:** Changing authored lane layouts or binding a new keyboard shortcut.

## T-520 Offer independent field mouse-wheel inversion

- **Status:** done (2026-10-11; verified; committed with this batch)
- **Outcome:** Options Editor offers a persistent wheel inversion setting that reverses vertical field scrolling and playback seeking independently of upscroll/downscroll.
- **Read first:** `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/sys/preferences/options_runtime.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/preferences/options_window.cpp`.
- **Files owned:** `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/sys/preferences/options_runtime.cpp`, `src/sys/compose/input/compose_input.cpp` (wheel routing only), `src/ui/nuklear/preferences/options_window.cpp`, `src/ui/frontend.h` (options fields only), `tests/core/preferences_tests.cpp`, `tests/ui/nuklear_options_state_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, this card.
- **Steps:** 1. Audit normalized wheel and preference paths. 2. Add preference, draft UI, persistence and vertical wheel routing. 3. Verify both directions and inversion combinations, Options lifecycle, and unchanged zoom behavior.
- **Invariants:** Default wheel behavior, modifier zoom, horizontal BGM scroll, autoscroll and keyboard navigation remain unchanged.
- **Reject/failure behavior:** Invalid preference types retain safe defaults; stale Options actions retain current preferences.
- **Verify:** `cmake --build build -j2`; `ctest --test-dir build --output-on-failure -R 'preferences|options|compose_action_notifications'`; affected-source `clang-tidy -p build`; final integrated full suite.
- **Out of scope:** OS-level natural scrolling or changes outside Compose.

## T-521 Control field waveform opacity

- **Status:** done (2026-10-11; verified; committed with this batch)
- **Outcome:** Appearance offers a live-preview persistent opacity slider for vertical song, channel and stem waveforms, with a mild 75-percent default.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/render/theme.cpp`, `src/sys/preferences/options_runtime.cpp`, `src/sys/app/main.cpp`, `include/wormedit/core/preferences.h`.
- **Files owned:** `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/sys/preferences/options_runtime.cpp`, `src/sys/app/main.cpp`, `src/ui/frontend.h` (options/setter only), `src/ui/nuklear/frontend.cpp` (setter only), `src/ui/nuklear/preferences/options_window.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/render/theme.h`, `src/ui/nuklear/render/theme.cpp` (opacity helper only), `tests/core/preferences_tests.cpp`, `tests/ui/nuklear_options_state_tests.cpp`, `tests/ui/theme_tests.cpp` (appended opacity test only), this card.
- **Steps:** 1. Audit PCM, envelope textures, retained gaps and direct fallback drawing. 2. Add one alpha scaler and settings lifecycle with cache invalidation. 3. Verify endpoints, invalid values, preview/revert/defaults and isolated native waveform captures.
- **Invariants:** Waveform timing, source data, RGB colors, note visibility and overview navigation remain unchanged.
- **Reject/failure behavior:** Invalid opacity retains defaults; closing/reverting draft restores saved opacity; failed saves use existing diagnostics.
- **Verify:** `cmake --build build -j2`; `ctest --test-dir build --output-on-failure -R 'preferences|options|theme'`; affected-source `clang-tidy -p build`; final integrated full suite and isolated Xvfb opacity captures.
- **Out of scope:** Sound-editor waveform controls, overview-bar opacity and note skin colors (T-517).

Coordinator-approved ownership extension: the added direction icon shifts later toolbar cells by 27 px. Update existing production-frontend click/hover fixtures, including the compact Sounds toggle, and run the entire event harness to detect real layout regressions.

Final coordinator verification (2026-10-11): normal application build passed; full CTest **3,162/3,162** passed; combined production frontend event harness **375/375 cases, 5,420 assertions** passed. All initial integration failures were repaired (five toolbar coordinate fixtures and the old fully opaque lane assertion). Native Xvfb captures and affected-file complexity checks passed with no new/increased violations. Evidence: `/tmp/feedback-muted-build.log`, `/tmp/feedback-all-tests-passing.log`, `/tmp/feedback-all-events-final.log`. Implementation, tests and completion documentation are committed together.

Focused evidence: 8 navigation/playhead cases (307 assertions), 2 Options feedback cases (20 assertions), 24 preference cases (216 assertions), and the integrated opacity/theme tests pass. Native `/tmp/feedback-navigation-{default,down}.png` verifies direction and persistence; `/tmp/feedback-waveform-{default,zero,full}.png` verifies 75/0/100 percent. `/tmp/feedback-waveform-full-{preview,restored}.png` verifies immediate near-zero draft preview and Close restoration to 100 percent while overview audio remains visible. Logs `/tmp/feedback-{navigation-tests,options-tests,preferences-tests,navigation-final-tidy,toolbar-fixture-tidy}.log`.
