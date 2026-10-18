# Feedback from writing the manual

[Back to task cards](../tasks.md).

Writing the user manual (T-430–T-433) on 2026-10-18 surfaced gaps the owner wanted fixed or planned.
T-700 (iBMSC/BMSE paste) is in [editing-commands.md](editing-commands.md).

## T-701 Toggle the measure line at the cursor with Ctrl+M

- **Status:** done (2026-10-18; owner: "ctrl+m should toggle a measure line … we need a shortcut to remove a measure-line")
- **Outcome:** Ctrl+M adds a bar line at the cursor when the cursor is inside a measure, and removes the bar line under the cursor (merging that measure into the one before) when it is on one; no object changes its row, each press is one Undo step, and the chart's first bar line is refused with a hint.
- **Read first:** `src/sys/compose_measure_commands.cpp` (`apply_cursor_measure_split`), `include/wormedit/core/measure_editor.h` (`remove_bar_lines`).
- **Files owned:** `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp` (`remove_bar_line_action_t`), `src/sys/compose_measure_commands.cpp`, `src/core/command_registry.cpp` (display name), `tests/sys/application_commands_tests.cpp`, `docs/manual/chapters/measures.adoc`, this card.
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
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `src/sys/application_defaults.cpp`, `src/tools/wormedit_manual_reference.cpp`, `tests/core/lane_profile_tests.cpp`, `tests/sys/application_defaults_tests.cpp`, `docs/manual/generated/layouts.adoc`, this card.
- **Steps:**
  1. Add `lane_keys` and `profile()` to the layout descriptor; creation and the manual generator use `profile()`.
  2. Bind the owner's sets; layouts without one keep D7 (no keys above ten lanes): DP free-zone and foot-pedal variants, 11K, 13K–15K, 17K, PMS 18, Octave, BMSON.
- **Invariants:** Keys follow lane display order; no duplicates; opened charts keep their own profiles.
- **Reject/failure behavior:** Not applicable (static tables, checked by tests).
- **Verify:** `wormedit_core_tests "[catalog]"`; `documentation.manual_reference`.
- **Out of scope:** Changing keys after creation (T-703).

## T-703 Let users bind lane keys per layout

- **Status:** ready
- **Value:** High for wide and unusual layouts: today a chart whose layout has no default keys ("No lane keys are bound for this chart") can only be keyed with the mouse, and nobody can change a default they dislike.
- **Outcome:** A *Lane keys* dialog (Options, and the command menu) shows the chart's lanes by stage with their keys and a physical keyboard map; clicking a lane and pressing a key binds it, Backspace unbinds, presets (Wormedit default, iBMSC interleaved for DP, Custom) fill all lanes at once, and *Apply* saves to this chart or to every chart with the layout. See the [mockup](https://claude.ai/artifact/12NUndRq3jDPrJCzcSfhXN) (owner, 2026-10-18).
- **Read first:** the mockup, `include/wormedit/core/document.h` (`input_profile_t`, `lane_binding_t`, `physical_key_hid_usage`), `validate_input_profile` in `include/wormedit/core/lane_profile_switch.h`, `src/sys/shortcut_rows.cpp` (`capture_shortcut_batch`, the existing key capture), `src/ui/nuklear/options_window.cpp`.
- **Files owned:** a new `src/ui/nuklear/lane_keys_dialog.cpp/.h`, its C++ view and action (per the frontend recipe in [frontend-cxx-views.md](frontend-cxx-views.md)), a core command that replaces a chart's `input_profile_t` through the transaction, the program preferences for per-layout keys, `src/core/command_registry.cpp` (a *Lane Keys…* command), tests, `docs/manual` (layout reference note), this card.
- **Steps:**
  1. **STOP:** confirm with the owner where per-layout keys live: the chart (exported with the project) or program preferences (every project), or both as the mockup's *Apply to* suggests.
  2. Core: a command that sets a chart's input profile, validated by `validate_input_profile`, one Undo step.
  3. Capture: reuse the shortcut capture so only letters and digits bind (Space, Tab, arrows and editing keys are refused with a reason); duplicates inside a stage are flagged before Apply.
  4. The dialog per the mockup; presets come from `new_project_layout_descriptor_t::lane_keys` and the factory profiles.
  5. Event-harness tests: bind, unbind, duplicate refusal, preset, Apply scope.
- **Invariants:** Bindings are physical keys; changing them never touches chart data or lanes.
- **Reject/failure behavior:** Cancel or Escape leaves bindings unchanged; an invalid profile is never applied.
- **Verify:** GATE; event harness; isolated Xvfb capture compared with the mockup.
- **Out of scope:** MIDI or second-keyboard input; per-stage focus switching for 48K.

## T-704 Add BGM columns when pasting BMS editor data needs them

- **Status:** ready
- **Value:** Medium: pasting iBMSC or BMSE notes with BGM into a chart with fewer BGM columns is refused today ("BMS BGM column has no matching target source lane"), so users must count columns and add them first.
- **Outcome:** When external BMS editor text (T-700) refers to more BGM columns than the chart has, the paste preview shows the extra columns and committing adds them and the notes in one Undo step.
- **Read first:** `src/formats/bms_clipboard.cpp` (BGM column mapping), `src/sys/compose_view.cpp` (`begin_compose_paste`, `decode_external_clipboard`), `src/core/bgm_columns.cpp` (`Set BGM column count`).
- **Files owned:** `src/formats/bms_clipboard.cpp`, `include/wormedit/formats/bms_clipboard.h`, `src/sys/compose_view.cpp`, `src/core/chart_clipboard.cpp` (applying with column growth), tests, `docs/manual/chapters/walkthrough-bms.adoc` (drop the "BGM needs columns" note), this card.
- **Steps:**
  1. Let the decoder report the highest BGM column the text uses instead of rejecting.
  2. Stage the column growth in the paste proposal so preview and commit agree; one transaction adds columns and pastes.
  3. Tests: paste into zero, fewer and enough columns; Undo restores the column count.
- **Invariants:** Existing BGM events and their columns are untouched; wormedit's own clipboard paste is unchanged.
- **Reject/failure behavior:** Beyond the format's BGM column limit the paste is refused with the limit named.
- **Verify:** GATE; Xvfb paste of iBMSC text with BGM into the empty tutorial project.
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
