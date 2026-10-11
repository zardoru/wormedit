# Menu pointer capture

[Back to task cards](../tasks.md).

## T-525 Capture workspace menu gestures through the closing release

- **Status:** done (2026-10-11; verified; uncommitted follow-up)
- **Stale-plan audit (2026-10-11):** Menu selection closes Nuklear's popup before underlying widgets render; the same pointer edge can also select a workspace tab or Project control. Existing menu dispatch tests checked command IDs but did not reject simultaneous workspace actions.
- **Outcome:** Selecting or dismissing a workspace menu consumes the entire pointer gesture; only the menu action is emitted and a fresh subsequent click works normally.
- **Read first:** `src/ui/nuklear/menu_bar.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/popup_lifecycle.h`, `tests/ui/menu_bar_event_harness_tests.cpp`, `docs/frontend-event-harness.md`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `docs/chart-editor-plan/tasks/menu-input-capture.md`.
- **Steps:** 1. Retain menu pointer ownership after selection and dismissal. 2. Mask mouse edges and held travel for later widgets with scoped restoration of original input. 3. Exercise menu commands, Project and landing controls, held input and release through the production event harness. 4. Run integrated build, regression suite and affected-source complexity checks.
- **Invariants:** Menu switching remains available; each menu command dispatches once; native file dialogs and document mutation stay in existing application paths; pointer state is restored before the next input cycle.
- **Reject/failure behavior:** An outside dismissal cannot activate the covered widget; closing press, held frames and release remain captured; the first fresh click after a quiet frame is accepted.
- **Verify:** `cmake --build build -j8`; `build/tests/wormedit_frontend_event_harness_tests '[menu-bar]'`; full combined frontend event binary; `ctest --test-dir build --output-on-failure`; `clang-tidy -p build src/ui/nuklear/menu_bar.cpp src/ui/nuklear/frontend.cpp`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`.
- **Done so far:** Scoped capture masks all mouse button edges/held states, movement and scroll after the title bar, including auxiliary dialogs, and restores original input at the end of the frame. Selected menu items immediately arm the release latch. Existing menu dispatch regression now rejects simultaneous covered workspace/chart/Compose actions.
- **Verification evidence:** Normal and release application builds pass. Full CTest run passed 3,174/3,175 initially; one SDL audition test timed out under parallel load and passed alone on retry (0.11 s), so all 3,175 checks are verified. Production frontend event harness passes 379 cases / 5,506 assertions. Scratch-presentation core checks pass 5 cases / 62 assertions; theme checks pass 12 cases / 2,253 assertions. Native Xvfb verifies DP5 skin-default both-left versus Chart layout outside placement, plus SP7 Right preview, Close restoration, Apply persistence and Revert. Theme importer reproduces all eight schemes. No new/increased production complexity violations. Logs: `/tmp/scratch-presentation-{ready-build,release-ready,all-tests,test-retry,events-final,core-final,theme-final}.log`; captures: `/tmp/scratch-presentation-dp5-{default,authored}.png`, `/tmp/t526-options-{right-preview,close-restores,applied,revert-restores}.png`. Menu-only regression passes 9 cases / 320 assertions. Scoped production/test tidy adds no new complexity violations.
- **Remaining:** None.
- **Out of scope:** Native macOS menu implementation, chart editing semantics, automatic commit and unrelated popup redesign.
