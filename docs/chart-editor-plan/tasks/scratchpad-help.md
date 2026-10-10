# Scratchpad: Select input and Help

[Back to task cards](../tasks.md).

User-authorized scratchpad batch, 2026-10-09. Shared frontend/menu ownership is serialized by the coordinator.

- **Ownership extension (coordinator):** Existing lane-key placement regressions in `tests/sys/frame_timing_tests.cpp` and `tests/sys/compose_notification_tests.cpp` must explicitly use Draw; the production callback fixture also verifies empty Select input in authored and virtual rows.

## T-510 Keep keyboard note creation out of Select mode

- **Status:** done (2026-10-09; implementation and verification committed together)
- **Stale-plan audit (2026-10-09):** Both frontend and application start a lane hold without a selected note; no Draw-only guard exists.
- **Outcome:** Empty-selection lane keys do nothing in Select mode; selected notes/BGM still reassign, and Draw retains keyboard tap/hold entry.
- **Read first:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose_input.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`.
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp`, `src/ui/workspace_menu.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `docs/chart-editor-plan/tasks/scratchpad-help.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through existing production input/menu/dialog paths. 2. Verify routing, preservation and dismissal with production tests. 3. Build, run the suite and complexity checks, inspect isolated native captures, then synchronize the Brain.
- **Invariants:** authored chart data and Undo are unchanged by Help/menu presentation; selected-object lane reassignment and Draw keyboard input remain available.
- **Reject/failure behavior:** empty Select input cannot create or retain a note gesture; modal, text, focus and stale-state guards remain intact.
- **Verify:** `cmake --build build -j8`; `ctest --test-dir build --output-on-failure`; combined frontend event binary; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; affected-source `clang-tidy -p build`; isolated Xvfb About, shortcuts and Playback captures.
- **Completion:** Frontend suppresses empty Select lane-key presses before capture; application independently suppresses creation for empty/non-note selections. Authored/virtual rows preserve chart/Undo and no gesture is retained; existing selected-note/BGM reassignment and Draw tap/hold/chord tests pass.
- **Verification evidence:** application build; **3,118/3,118 full tests** (222.34 s); **363/363 combined frontend cases** (5,268 assertions); documentation validation; affected-source clang-tidy with no new/increased complexity violations; inspected isolated native Playback/About/shortcuts captures at standard and minimum 800×560 sizes. Logs `/tmp/scratchpad-help-{build,all,events,tidy,documentation}.log`; captures `/tmp/scratchpad-help-{playback,about,shortcuts,about-minimum,shortcuts-minimum}.png`. macOS native runtime remains unverified on Linux.
- **Out of scope:** automatic commit, unfinished manual links, dock redesign and new shortcut bindings.

## T-511 Separate external player commands in Playback

- **Status:** done (2026-10-09; implementation and verification committed together)
- **Stale-plan audit (2026-10-09):** The shared model puts external commands directly after Metronome without a separator.
- **Outcome:** Playback draws a separator between internal transport and external player commands, shared with the native menu model.
- **Read first:** `src/ui/nuklear/menu_bar.cpp`, `src/ui/workspace_menu.h`, `src/sys/macos_menu.mm`.
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp`, `src/ui/workspace_menu.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `docs/chart-editor-plan/tasks/scratchpad-help.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through existing production input/menu/dialog paths. 2. Verify routing, preservation and dismissal with production tests. 3. Build, run the suite and complexity checks, inspect isolated native captures, then synchronize the Brain.
- **Invariants:** authored chart data and Undo are unchanged by Help/menu presentation; selected-object lane reassignment and Draw keyboard input remain available.
- **Reject/failure behavior:** empty Select input cannot create or retain a note gesture; modal, text, focus and stale-state guards remain intact.
- **Verify:** `cmake --build build -j8`; `ctest --test-dir build --output-on-failure`; combined frontend event binary; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; affected-source `clang-tidy -p build`; isolated Xvfb About, shortcuts and Playback captures.
- **Completion:** The shared menu model carries one action-none separator after Metronome. The Nuklear renderer sizes and draws the token separator, and the existing macOS adapter consumes the same entry without new dispatch behavior.
- **Verification evidence:** application build; **3,118/3,118 full tests** (222.34 s); **363/363 combined frontend cases** (5,268 assertions); documentation validation; affected-source clang-tidy with no new/increased complexity violations; inspected isolated native Playback/About/shortcuts captures at standard and minimum 800×560 sizes. Logs `/tmp/scratchpad-help-{build,all,events,tidy,documentation}.log`; captures `/tmp/scratchpad-help-{playback,about,shortcuts,about-minimum,shortcuts-minimum}.png`. macOS native runtime remains unverified on Linux.
- **Out of scope:** automatic commit, unfinished manual links, dock redesign and new shortcut bindings.

## T-512 Show project credits and disclosure in About

- **Status:** done (2026-10-09; implementation and verification committed together)
- **Stale-plan audit (2026-10-09):** About currently contains only version and build.
- **Outcome:** About shows the repository, author, license and concise AI disclosure alongside version/build.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `README.md`, `src/ui/nuklear/dialog.h`.
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp`, `src/ui/workspace_menu.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `docs/chart-editor-plan/tasks/scratchpad-help.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through existing production input/menu/dialog paths. 2. Verify routing, preservation and dismissal with production tests. 3. Build, run the suite and complexity checks, inspect isolated native captures, then synchronize the Brain.
- **Invariants:** authored chart data and Undo are unchanged by Help/menu presentation; selected-object lane reassignment and Draw keyboard input remain available.
- **Reject/failure behavior:** empty Select input cannot create or retain a note gesture; modal, text, focus and stale-state guards remain intact.
- **Verify:** `cmake --build build -j8`; `ctest --test-dir build --output-on-failure`; combined frontend event binary; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; affected-source `clang-tidy -p build`; isolated Xvfb About, shortcuts and Playback captures.
- **Completion:** About displays zardoru, a clickable GitHub repository, concise Claude/Codex disclosure and The Unlicense notice alongside version/build. The future manual link remains pending.
- **Verification evidence:** application build; **3,118/3,118 full tests** (222.34 s); **363/363 combined frontend cases** (5,268 assertions); documentation validation; affected-source clang-tidy with no new/increased complexity violations; inspected isolated native Playback/About/shortcuts captures at standard and minimum 800×560 sizes. Logs `/tmp/scratchpad-help-{build,all,events,tidy,documentation}.log`; captures `/tmp/scratchpad-help-{playback,about,shortcuts,about-minimum,shortcuts-minimum}.png`. macOS native runtime remains unverified on Linux.
- **Out of scope:** automatic commit, unfinished manual links, dock redesign and new shortcut bindings.

## T-513 Add a shortcut cheat sheet to Help

- **Status:** done (2026-10-09; implementation and verification committed together)
- **Stale-plan audit (2026-10-09):** Help currently has only About; the bounded menu projection already carries formatted current command chords.
- **Outcome:** Help opens a compact modal shortcut sheet for common editing, timing, navigation and dock actions, displaying current remapped platform chords and explicit unbound commands.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/workspace_menu.h`, `src/sys/menu_command_projection.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`.
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/menu_bar.cpp`, `src/ui/workspace_menu.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `docs/chart-editor-plan/tasks/scratchpad-help.md`, `docs/chart-editor-plan/tasks.md`, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Implement the outcome through existing production input/menu/dialog paths. 2. Verify routing, preservation and dismissal with production tests. 3. Build, run the suite and complexity checks, inspect isolated native captures, then synchronize the Brain.
- **Invariants:** authored chart data and Undo are unchanged by Help/menu presentation; selected-object lane reassignment and Draw keyboard input remain available.
- **Reject/failure behavior:** empty Select input cannot create or retain a note gesture; modal, text, focus and stale-state guards remain intact.
- **Verify:** `cmake --build build -j8`; `ctest --test-dir build --output-on-failure`; combined frontend event binary; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; affected-source `clang-tidy -p build`; isolated Xvfb About, shortcuts and Playback captures.
- **Completion:** Help opens a compact sheet using current registry-projected platform chords, explicit Unbound/Unavailable states, and navigation/lane-key guidance. Remapped Command chords, empty bindings, modal suppression and Escape/re-entry pass production event tests; both dialogs fit at 800×560.
- **Verification evidence:** application build; **3,118/3,118 full tests** (222.34 s); **363/363 combined frontend cases** (5,268 assertions); documentation validation; affected-source clang-tidy with no new/increased complexity violations; inspected isolated native Playback/About/shortcuts captures at standard and minimum 800×560 sizes. Logs `/tmp/scratchpad-help-{build,all,events,tidy,documentation}.log`; captures `/tmp/scratchpad-help-{playback,about,shortcuts,about-minimum,shortcuts-minimum}.png`. macOS native runtime remains unverified on Linux.
- **Out of scope:** automatic commit, unfinished manual links, dock redesign and new shortcut bindings.
