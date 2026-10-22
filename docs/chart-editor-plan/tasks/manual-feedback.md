# Feedback from writing the manual

[Back to task cards](../tasks.md).

Writing the user manual (T-430–T-433) on 2026-10-18 surfaced gaps the owner wanted fixed or planned.
T-700 (iBMSC/BMSE paste) is in [editing-commands.md](editing-commands.md).

## T-701 Toggle the measure line at the cursor with Ctrl+M

- **Status:** done (2026-10-18; owner: "ctrl+m should toggle a measure line … we need a shortcut to remove a measure-line")
- **Outcome:** Ctrl+M adds a bar line at the cursor when the cursor is inside a measure, and removes the bar line under the cursor (merging that measure into the one before) when it is on one; no object changes its row, each press is one Undo step, and the chart's first bar line is refused with a hint.
- **Read first:** `src/sys/compose/actions/compose_measure_commands.cpp` (`apply_cursor_measure_split`), `include/wormedit/core/measure_editor.h` (`remove_bar_lines`).
- **Files owned:** `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp` (`remove_bar_line_action_t`), `src/sys/compose/actions/compose_measure_commands.cpp`, `src/core/command_registry.cpp` (display name), `tests/sys/application_commands_tests.cpp`, `docs/manual/chapters/measures.adoc`, this card.
- **Steps:**
  1. Add `remove_bar_line_action_t` to the measure edit command over the existing `remove_bar_lines`.
  2. Make the cursor command remove the bar line at a measure head and split elsewhere; rename it *Toggle Measure Line at Cursor*. The command ID `measures.split_at_cursor` stays so saved shortcut profiles keep working.
- **Invariants:** Notes keep their song time; one Undo step per press.
- **Reject/failure behavior:** On the first measure head, a hint and no change; a rejected merge leaves the chart unchanged.
- **Verify:** GATE; `wormedit_frame_timing_tests "[measure-split]"` (split, remove, first-measure refusal).
- **Out of scope:** Alt-key variants (owner withdrew them, 2026-10-18).

## T-702 Give wide new-project layouts default lane keys

- **Status:** done (2026-10-18; owner's key sets)
- **Outcome:** New projects for BMS DP5/DP7, osu!mania 12K/16K/18K and 12K/16K SpecialStyle start with lane keys, left to right: DP5 `1Q2W3E` + `U8I9O0` (right scratch outside) or `YU8I9O` (right scratch inside); DP7 and 16K `1Q2W3E4R` + `Y7U8I9O0`; 12K `1Q2W3E` + `Y7U8I9`; 18K `1234QWERF` + `7890YUIOH`; SpecialStyle like the DP layout of the same width with both scratches outside.
- **Read first:** `include/wormedit/core/lane_profiles.h` (`new_project_layout_descriptor_t`), `src/core/lane_profiles.cpp` (`project_layouts`).
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `src/sys/app/application_defaults.cpp`, `src/tools/wormedit_manual_reference.cpp`, `tests/core/lane_profile_tests.cpp`, `tests/sys/application_defaults_tests.cpp`, `docs/manual/generated/layouts.adoc`, this card.
- **Steps:**
  1. Add `lane_keys` and `profile()` to the layout descriptor; creation and the manual generator use `profile()`.
  2. Bind the owner's sets; layouts without one keep D7 (no keys above ten lanes): DP free-zone and foot-pedal variants, 11K, 13K–15K, 17K, PMS 18, Octave, BMSON.
- **Invariants:** Keys follow lane display order; no duplicates; opened charts keep their own profiles.
- **Reject/failure behavior:** Not applicable (static tables, checked by tests).
- **Verify:** `wormedit_core_tests "[catalog]"`; `documentation.manual_reference`.
- **Out of scope:** Changing keys after creation (T-703).

## T-703 Let users bind lane keys per layout

- **Status:** done (2026-10-20)
- **Stale-plan audit / wiring (2026-10-20):** Preferences contain no lane-key overrides;
  Compose projects `chart.input_profile.bindings` directly. Shortcut capture is owned by
  `src/sys/commands/shortcut_rows.cpp`; Options uses `options_runtime.*` and `options_window.cpp`.
  This card adds a core preference value, a sys draft/dialog owner, and a C++ frontend surface.
- **Scope extension:** Coordinator approves `src/core/lane_keys.cpp`, `src/sys/lane_keys_runtime.*`,
  `src/ui/frontend/lane_keys.h`, `src/ui/frontend/workspace.h`, `src/ui/frontend/actions.h`,
  `src/sys/app/application.cpp`, `src/sys/application_commands.*`, `src/sys/commands/application_command_dispatch.cpp`,
  `src/sys/compose/views/compose_view.h`, `src/ui/nuklear/app/frontend.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
  and `memory/chart-editor-status/resume.md` for persistence, dialog lifecycle, command routing,
  input isolation, cached binding projection, build registration and completion evidence.
  `include/wormedit/core/lane_keys.h` owns the resolved-profile cache. `include/wormedit/core/document.h`,
  `include/wormedit/core/vocabulary.h` and `src/core/document.cpp` expose lane color vocabulary and
  the display-order query; `src/core/lane_migration.cpp` reuses that query instead of retaining its
  private copy. `src/ui/nuklear/preferences/options_window.h` threads the new typed Options entry action.
  The final DP7 check found stage-local display columns and intentionally unbound new-project
  factories. Coordinator also approves `src/core/lane_profiles.cpp` and
  `tests/core/lane_profile_tests.cpp`: defaults and saved keys now traverse P1 then P2, and the
  iBMSC preset uses raw BMS factories and built-in BMS compatibility metadata, preserving
  migration's existing column-rank query. The D7 unbinding policy now belongs to `profile()`;
  raw factories retain legacy bindings for every scratch placement. `include/wormedit/core/lane_profiles.h`
  documents this distinction, while new-project unbound-wide behavior stays unchanged.
  `src/tools/wormedit_manual_reference.cpp` also uses stage order so the generated layout reference
  agrees with the actual bindings rather than repeating the former ordering mistake.
- **Value:** High for wide and unusual layouts: today a chart whose layout has no default keys ("No lane keys are bound for this chart") can only be keyed with the mouse, and nobody can change a default they dislike.
- **Outcome:** A *Lane keys* dialog (Options, and the command menu) shows the current layout's lanes by stage with their keys and a physical keyboard map; clicking a lane and pressing a key binds it, Backspace unbinds, presets (Wormedit default, iBMSC interleaved for DP, Custom) fill all lanes at once, and *Apply* saves the layout's keys to the user's preferences, where every chart with that layout uses them. See the [mockup](https://claude.ai/artifact/12NUndRq3jDPrJCzcSfhXN) (owner, 2026-10-18: "quite beautiful").
- **Decision (owner, 2026-10-18):** lane keys per layout are the user's personal configuration, global: program preferences keyed by layout profile ID, not chart or project data. A chart's stored `input_profile_t` is only the fallback for a layout the user never customized.
- **Read first:** the mockup, `include/wormedit/core/document.h` (`input_profile_t`, `lane_binding_t`, `physical_key_hid_usage`), `validate_input_profile` in `include/wormedit/core/lane_profile_switch.h`, `src/sys/commands/shortcut_rows.cpp` (`capture_shortcut_batch`, the existing key capture), `src/ui/nuklear/preferences/options_window.cpp`.
- **Files owned:** `src/ui/nuklear/preferences/lane_keys_dialog.cpp`, `src/ui/nuklear/preferences/lane_keys_dialog.h`,
  `src/ui/frontend/lane_keys.h`, `src/ui/frontend/workspace.h`, `src/ui/frontend/actions.h`,
  `include/wormedit/core/preferences.h`, `include/wormedit/core/lane_keys.h`, `include/wormedit/core/lane_profiles.h`,
  `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/core/preferences.cpp`,
  `src/core/lane_keys.cpp`, `src/core/document.cpp`, `src/core/lane_migration.cpp`, `src/core/lane_profiles.cpp`,
  `src/core/command_registry.cpp`, `src/sys/preferences/lane_keys_runtime.cpp`, `src/sys/preferences/lane_keys_runtime.h`,
  `src/tools/wormedit_manual_reference.cpp`,
  `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/app/application.cpp`,
  `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`,
  `src/ui/nuklear/app/frontend.cpp`, `src/ui/nuklear/preferences/options_window.cpp`, `src/ui/nuklear/preferences/options_window.h`,
  `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/core/lane_keys_tests.cpp`, `tests/core/lane_profile_tests.cpp`,
  `tests/ui/lane_keys_event_harness_tests.cpp`, `docs/manual/generated/shortcuts.adoc`,
  `docs/manual/generated/layouts.adoc`,
  `docs/manual/chapters/keyboard-reference.adoc`, `docs/manual/chapters/getting-started.adoc`,
  `memory/chart-editor-status/resume.md`, this card.
- **Steps:**
  1. Preferences: a per-layout map from profile ID to ordered physical keys, stored with the program preferences (not Undo history, not the project), validated with `validate_input_profile` against the layout before use.
  2. Resolution: Compose uses the preference keys for the chart's layout when present, otherwise the chart's own `input_profile_t`. Opened charts and new projects follow the same rule.
  3. Capture: reuse the shortcut capture so only letters and digits bind (Space, Tab, arrows and editing keys are refused with a reason); duplicates inside a stage are flagged before Apply.
  4. The dialog per the mockup; presets come from `new_project_layout_descriptor_t::lane_keys` and the factory profiles. *Reset to preset* drops the preference so the layout's defaults apply again.
  5. Event-harness tests: bind, unbind, duplicate refusal, preset, a second chart with the same layout picking up the keys.
- **Invariants:** Bindings are physical keys; changing them never touches chart data, lanes or the project's dirty state.
- **Reject/failure behavior:** Cancel or Escape leaves bindings unchanged; an invalid profile is never applied.
- **Verify:** GATE; event harness; isolated Xvfb capture compared with the mockup.
- **Verification (2026-10-20):** Focused core/preferences checks pass 337 assertions in 38
  cases; lane-key event-harness checks pass 337 assertions in nine cases, including a second
  chart, capture isolation, duplicates, Reset/Cancel and the 800×700 footer. Affected default
  clang-tidy has no new complexity findings; no baseline allowances changed. Isolated Xvfb
  captures match the owner's supplied screenshot's stage/keyboard structure:
  `/tmp/t703-default-final.png` and `/tmp/t703-dialog-wide-fixed.png`. A disposable tutorial
  project retained its source hash while an A binding persisted to preferences and survived
  restart (`/tmp/t703-persisted-final.png`); A then placed the intended lane's note and Undo
  removed it (`/tmp/t703-placement.png`, `/tmp/t703-placement-undone.png`). The Options entry
  was also exercised (`/tmp/t703-options-input.png`, `/tmp/t703-options-entry.png`).
  Real GATE output tail:
  ```text
  gate: 8 job(s), min(8, 16 cores / 1 agent(s)); logs in /tmp/wormedit-gate.5ts4LX
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 4280
  gate: passed
  ```
- **Out of scope:** MIDI or second-keyboard input; per-stage focus switching for 48K.

## T-704 Add BGM columns when pasting BMS editor data needs them

- **Status:** done (2026-10-20)
- **Stale-plan audit / wiring:** The decoder rejected absent source lanes in `finish_record`;
  `chart_paste_proposal_t` already owned placement and atomic publication. The decoder now records
  the highest required column, the proposal allocates columns through the shared BGM allocator,
  and `compose_view.cpp` projects the proposed lanes and ghosts through existing frontend drawing.
  Format, core clipboard and sys paste regressions cover both external grammars, existing events,
  zero/fewer/enough columns, rejection, native-copy behavior and one-step Undo.
- **Scope extension (2026-10-20):** `include/wormedit/core/chart_clipboard.h` carries the external
  column requirement and proposed lanes; `include/wormedit/core/bgm_columns.h` and
  `src/core/bgm_columns.cpp` expose the existing column allocator for proposal reuse. Coordinator
  approved within the card's outcome; no separate allocator or scratch-history command.
  `include/wormedit/core/document.h` gives BGM source lanes default value equality, needed by the
  paste report's existing equality contract now that it retains proposed columns.
- **Value:** Medium: pasting iBMSC or BMSE notes with BGM into a chart with fewer BGM columns is refused today ("BMS BGM column has no matching target source lane"), so users must count columns and add them first.
- **Outcome:** When external BMS editor text (T-700) refers to more BGM columns than the chart has, the paste preview shows the extra columns and committing adds them and the notes in one Undo step.
- **Read first:** `src/formats/bms_clipboard.cpp` (BGM column mapping), `src/sys/compose/views/compose_view.cpp` (`begin_compose_paste`, `decode_external_clipboard`), `src/core/bgm_columns.cpp` (`Set BGM column count`).
- **Files owned:** `src/formats/bms_clipboard.cpp`, `include/wormedit/formats/bms_clipboard.h`, `src/sys/compose/views/compose_view.cpp`, `src/core/chart_clipboard.cpp` (applying with column growth), `include/wormedit/core/chart_clipboard.h`, `include/wormedit/core/bgm_columns.h`, `src/core/bgm_columns.cpp`, `include/wormedit/core/document.h`, tests, `docs/manual/chapters/walkthrough-bms.adoc` (drop the "BGM needs columns" note), `memory/chart-editor-status/resume.md` (completion record), this card.
- **Steps:**
  1. Let the decoder report the highest BGM column the text uses instead of rejecting.
  2. Stage the column growth in the paste proposal so preview and commit agree; one transaction adds columns and pastes.
  3. Tests: paste into zero, fewer and enough columns; Undo restores the column count.
- **Invariants:** Existing BGM events and their columns are untouched; wormedit's own clipboard paste is unchanged.
- **Reject/failure behavior:** Beyond the format's BGM column limit the paste is refused with the limit named.
- **Verify:** GATE; Xvfb paste of iBMSC text with BGM into the empty tutorial project.
- **Verification (2026-10-20):** Focused core/decoder tests pass 379 assertions in 46 cases;
  application paste tests pass 132 assertions in six cases. Affected default clang-tidy reports
  only existing complexity warnings; no allowances changed. Isolated Xvfb on
  `docs/manual/fixtures/tutorial/tutorial-empty.wormedit` shows three staged columns and a ghost,
  the committed event and column count 3, then one Undo restoring count 0 and no event:
  `/tmp/t704-preview.png`, `/tmp/t704-committed.png`, `/tmp/t704-undone.png`.
  Real GATE output tail:
  ```text
  gate: 8 job(s), min(8, 16 cores / 1 agent(s)); logs in /tmp/wormedit-gate.baznWP
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 4265
  gate: passed
  ```
- **Out of scope:** Pasting into BMS virtual measures past the chart's end (separate limitation, documented in the manual).

## T-705 Ship the manual in packages, opt-out

- **Status:** done (2026-10-18; owner: "Manual should be an opt-out part of package scripts")
- **Outcome:** The default build renders `docs/manual` to HTML (target `manual`) and `cmake --install` places it in `manual/` next to the program, so `scripts/windows-cross-package.sh` zips include it; `-DWORMEDIT_INSTALL_MANUAL=OFF` opts out. The packaged copy uses no web or icon fonts.
- **Read first:** `cmake/wormedit_manual.cmake`.
- **Files owned:** `cmake/wormedit_manual.cmake`, `CMakeLists.txt` (include), `docs/manual/chapters/maintaining.adoc`, this card.
- **Steps:**
  1. Find Asciidoctor (also in user gem directories), build with `--failure-level WARN`, copy the images.
  2. Without Asciidoctor, warn at configure time and fail `cmake --install` so packages cannot silently omit the manual.
- **Invariants:** Opting out removes the target and install rules entirely.
- **Reject/failure behavior:** A manual with Asciidoctor warnings fails the build.
- **Verify:** `cmake --build build --target manual`; `cmake --install build --prefix <tmp>` produces `bin/manual/index.html` and its images.
- **Out of scope:** A `--no-manual` flag on the Windows script (the script already passes `CMAKE_ARGS`; add the flag once the in-progress Windows packaging work lands).

## T-706 Show each command's shortcut in the command palette

- **Status:** done (2026-10-18; owner: "Yes, do add the shortcuts")
- **Outcome:** Every row of the command palette shows the command's current keys right-aligned in platform form (`Ctrl+Shift+Z`, `⌘` on macOS), nothing for unbound commands, following rebinds.
- **Read first:** `src/sys/commands/shortcut_rows.h`, `src/ui/nuklear/app/command_menu.cpp`, `src/ui/nuklear/chrome/chrome.h` (`menu_item`).
- **Files owned:** `src/sys/shortcut_rows.h/.cpp` (`command_shortcut_text_t`), `src/sys/app/application.cpp` (`build_command_menu_view`), `src/ui/frontend/workspace.h` (`WormUiCommandMenuRow::shortcut`), `src/ui/nuklear/chrome/chrome.h` (`draw_row_shortcut`, shared with menus), `src/ui/nuklear/app/command_menu.cpp`, `tests/sys/shortcut_rows_tests.cpp`, `tests/ui/command_menu_event_harness_tests.cpp`, `docs/manual/chapters/command-palette.adoc`, this card.
- **Steps:**
  1. Cache the chord text per command, reformatted only when a binding or the platform modifier changes, because the palette view is rebuilt every frame.
  2. Draw it with the same right-aligned helper the menu bar uses.
- **Invariants:** No steady-state per-frame allocation; palette search still matches names only.
- **Reject/failure behavior:** Not applicable.
- **Verify:** GATE; `wormedit_core_tests "[shortcut-rows]"`; isolated Xvfb capture of the palette.
- **Out of scope:** Searching by key.
