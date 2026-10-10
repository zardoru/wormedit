# Task cards: Options screen and project chart management

[Back to the task-card index](../tasks.md).

Findings (2026-09-30):

- Preferences are scattered: external players have their own file and panel
  (`src/sys/external_player.cpp`, `src/ui/nuklear/external_player_panel.cpp`),
  shortcuts have a profile file (`src/core/shortcut_profile_storage.cpp`,
  `src/sys/platform_shortcut_profile.cpp`), minimap preferences are consumed in
  `src/sys/compose_input.cpp` (`consume_minimap_preferences`), the theme is chosen
  by the `WORMEDIT_THEME_DIR` environment variable, and playback rate and
  metronome state are session-only. Several planned cards (T-228 dock layout,
  T-241 minimap click mode, T-234 rate) need somewhere to persist.
- Charts in a project: the song has ordered charts and `output_group_t` entries
  (name, family, chart list, relative path) in `include/wormedit/core/document.h`,
  but there are no duplicate/delete/reorder commands, "New Chart" is still
  disabled (T-52), and output groups have no management UI. The Setup tab holds
  recovery rows and the StepMania export policy only.

**Scope rule (user decision, 2026-09-30):** settings of different scopes are
different instances, stored and edited separately.

- **Program scope** — per user, independent of any project: stored in
  `preferences.json` (T-253), edited only in the **Options** window (T-254–T-258).
  Examples: theme, UI scale, audio device, shortcuts, external players,
  autosave interval, defaults used when creating new charts.
- **Project scope** — belongs to the open song and travels with it: stored in the
  native project document (T-263), edited only in the **Project** tab
  (T-260–T-263). Examples: charts, output groups, export policies (the StepMania
  policy now in Setup), project asset directories, per-project preview player
  choice.
- A setting lives in exactly one scope. When a project needs to differ from a
  program default, the project stores an explicit override shown as
  "Use program default" plus a value; the program store never holds
  project-specific values, and the project never stores per-user values
  (device, theme, shortcuts).
- The two UIs never share a window or a store. Chart data (notes, per-chart
  input profiles) is neither: it is document content edited in Compose/Metadata.

Apply the stale-plan check (rule 9) first.

## T-253 Add a unified preferences store

- **Status:** done (768/768 core and external-player tests; 16 preference cases; isolated two-start SDL dummy GUI persistence verified)
- **Outcome:** one versioned `preferences.json` in the SDL preference directory holds all per-user settings in sections (general, editor, audio, display, players, minimap, layout); it loads once at startup, saves atomically on change, and migrates the existing external-player file on first run. Shortcut profiles stay in their own file, referenced by path.
- **Read first:** `src/sys/external_player.cpp` (`load_external_player_preferences`, `valid_preferences`), `src/core/shortcut_profile_storage.cpp` (atomic save pattern), `src/sys/platform_shortcut_profile.cpp` (`SDL_GetPrefPath`), `src/sys/compose_input.cpp` (`consume_minimap_preferences`).
- **Files owned:** new `include/wormedit/core/preferences.h`, new `src/core/preferences.cpp`, new `tests/core/preferences_tests.cpp`, new `src/sys/preferences_runtime.cpp/.h`, `src/sys/external_player.cpp/.h`, `src/sys/main.cpp` (load/save wiring only), `CMakeLists.txt`, `tests/CMakeLists.txt`; user-approved atomic-helper extensions: new `include/wormedit/core/atomic_file_storage.h`, new `src/core/atomic_file_storage.cpp`, `include/wormedit/core/shortcut_profile_storage.h`, `src/core/shortcut_profile_storage.cpp`.
- **Steps:**
  1. Typed struct per section with defaults and ranges; bounded JSON read (size, depth), unknown keys ignored with one warning, invalid values replaced by defaults with a warning; version field and migration hook.
  2. Atomic save (same-directory temporary, flush, rename) reusing the shortcut-profile helper.
  3. Move external-player and minimap settings into it; on first run read the old external-player file and write the new one.
  4. Tests: defaults, round trip, corrupt file, out-of-range values, migration from the old file.
- **Invariants:** the application never fails to start because of preferences.
- **Reject/failure behavior:** a failed save keeps in-memory values and reports through notifications (T-219) or the status line.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|external_player'`.
- **Out of scope:** the Options UI (T-254).

## T-254 Build the Options window shell

- **Status:** done
- **Outcome:** program scope only. File → Options… and `workspace.options` (default Ctrl+, / ⌘,) open a modeless Options window with a category list on the left and the selected page on the right; each page edits a draft copy with Apply, Revert, and Restore defaults; Apply validates and saves through T-253.
- **Read first:** `include/wormedit/core/preferences.h`, `src/ui/nuklear/external_player_panel.cpp` (existing modeless panel), `src/core/command_registry.cpp`, `src/ui/nuklear/chrome.h`.
- **Files owned:** new `src/ui/nuklear/options_window.cpp/.h`, new `src/sys/options_runtime.cpp/.h`, `src/ui/frontend.h`, `src/core/command_registry.cpp`, `src/sys/main.cpp` (dispatch/view only), `CMakeLists.txt`, new `tests/ui/nuklear_options_state_tests.cpp`, `tests/CMakeLists.txt`; user-approved frontend integration extension: `src/ui/nuklear/frontend.cpp` (File menu entry and modeless renderer wiring); user-approved typed-command extension: `src/sys/application_commands.h/.cpp`, `tests/sys/application_commands_tests.cpp` (Options enum, parser mapping, and direct parse assertion); user-approved input-occlusion extension: `src/ui/nuklear/compose_panel.cpp` and `src/ui/nuklear/timing_panel.cpp` (draw charts while the Options window owns input, preserving focus, selection, gestures, and staged timing events).
- **Steps:** 1. Scalar view with the draft values and validation messages per field. 2. Category list: General, Editor, Playback & audio, Display, Input & shortcuts, External players. 3. Pages are registered functions so later cards add content without touching the shell.
- **Invariants:** opening Options does not change editor focus, selection, or tool; nothing is applied until Apply.
- **Reject/failure behavior:** invalid fields disable Apply and show their message inline.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; isolated Xvfb smoke PNG with the window open.
- **Out of scope:** page contents (T-255–T-258).
- **Verification evidence (2026-09-30):** full application build passed (`/tmp/wormedit-t254-timing-final-build.log`); full suite 1074/1074 passed in 47.70 s (`/tmp/wormedit-t254-final-all-tests.log`); Options focused tests 9/9 passed; configured clang-tidy passed with no new cognitive-complexity violations. Isolated Xvfb GUI proofs passed on the final coherent binary: landing Ctrl+, and File → Options; all six categories and modeless background interaction; Defaults/Revert without publication, Apply retaining unrelated audio preferences; selection and held-navigation release; Options double-clicks without staging a Timing SV. Evidence: `/tmp/wormedit-t254/options-landing.log`, `/tmp/wormedit-t254/options-compose-gui.log`, `/tmp/wormedit-t254/options-timing-final.log`; inspected PNGs include `options-landing-key.png`, `options-landing-menu.png`, and `options-timing-closed.png` in that directory.

## T-255 Fill the General and Editor pages

- **Status:** done (full 1133/1133; actual General/Editor edits, one-minute autosave, New snap, and all close paths verified)
- **Outcome:** General: autosave interval (1–30 min, default 5), recent-files count, confirm on close with unsaved changes. Editor: default snap for new charts, default scroll direction per mode family (override of the profile default), playhead anchor while following (default 20 %), note-kind palette visibility, lane-key legend on/off, area-band opacity.
- **Read first:** `include/wormedit/core/autosave_loop.h`, `src/sys/compose_view.cpp` (playhead anchor constant, scroll direction), `include/wormedit/core/preferences.h`.
- **Files owned:** `src/ui/nuklear/options_window.cpp`, `src/sys/options_runtime.cpp`, `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/sys/compose_view.cpp` (read the preference instead of the constant), `src/core/autosave_loop.cpp` (configurable interval), `tests/core/preferences_tests.cpp`, `tests/core/autosave_loop_tests.cpp`; user-approved complete-settings and confirmation extension: `src/ui/frontend.h`, `src/sys/main.cpp`, `src/sys/compose_view.h`, `src/sys/compose_scroll_geometry.h`, `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `include/wormedit/core/autosave_loop.h`, `include/wormedit/formats/native_project_storage.h`, `src/formats/native_project_storage.cpp`, `src/sys/application_frame_services.h/.cpp`, `src/ui/nuklear/frontend.cpp`, `tests/ui/nuklear_options_state_tests.cpp`, `tests/sys/frame_timing_tests.cpp`; separately approved follow-anchor extension: `src/sys/compose_playhead_cursor.h`; user-approved coherent follow-bound extension: `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `tests/core/interaction_tests.cpp`. The close-confirmation setting activates its planned default of true; recent-files count configures the limit without adding a recent-files subsystem.
- **Steps:** one field at a time: add to the struct, read where the constant lives today, add the widget, test the default.
- **Invariants:** defaults reproduce today's behavior exactly.
- **Reject/failure behavior:** out-of-range values are rejected by validation.
- **Verify:** standard build/test.
- **Verification evidence (2026-10-01):** the coherent application build passed; full suite 1133/1133 passed in 52.51 s (`/tmp/wormedit-t220/joint-hover-history-full-tests.log`); seven syntax checks on the isolated HEAD-plus-T-255 cut passed, excluding T-250/T-220 implementation and the preexisting status wrappers; configured clang-tidy passed with no new owned cognitive-complexity violations. Isolated Xvfb with dummy audio verified General/Editor numeric, checkbox, mode-combo and opacity edits; Defaults/Revert without publication and category Apply preserving unrelated audio preferences; restored palette and numeric lane legend; Dance downscroll; New with requested snap 5 saved a note at row 192 of resolution 960; startup one-minute autosave published a checksum-valid revision-2 backup. Native close, File Quit and Project Close Cancel/Discard passed; Save published the current three-chart revision before quit; a directory destination failed atomically and retained the modal and editor. A pathless project's actual isolated GTK Save chooser was canceled, leaving the close prompt, then Cancel restored the editor. A private debugger projection-failure injection verified recovery Quit, retained publication failure and Cancel recovery controls. Core regressions cover mode identity across imports, custom-snap preservation, live autosave interval changes, and follow-anchor boundary/paused-view validity. Proof matrix and captures: `/tmp/wormedit-t255/acceptance-matrix.md`, `/tmp/wormedit-t255/gui/`; terminal logs include `fields-final.log`, `save-autosave-final.log`, `pathless-final.log`, and `recovery-final.log` in `/tmp/wormedit-t255/`.
- **Out of scope:** per-chart settings (they belong to the Project tab).


## T-256 Fill the Playback & audio page

- **Status:** ready
- **Outcome:** output device choice (from `SDL_GetAudioPlaybackDevices`, "System default" first), buffer size, metronome volume and accent volume, count-in measures, default playback rate and keep-pitch (T-45/T-233/T-234), preview latency compensation in ms, audition volume.
- **Read first:** `src/sys/sdl_preview_output.cpp`, `src/sys/sdl_audition_output.cpp`, `src/sys/audio_source_registration.cpp` (metronome sources), `include/wormedit/audio/playback_rate.h`.
- **Files owned:** `src/ui/nuklear/options_window.cpp`, `src/sys/options_runtime.cpp`, `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `src/sys/sdl_preview_output.cpp/.h`, `src/sys/sdl_audition_output.cpp/.h`, `tests/core/preferences_tests.cpp`.
- **Steps:** 1. Device list refresh button. 2. Changing device reopens outputs only when stopped (otherwise "applies after Stop"). 3. Latency compensation shifts the reported playhead, not the schedule.
- **Invariants:** Stop remains a hard boundary; defaults match today.
- **Reject/failure behavior:** a device that fails to open falls back to the system default with a warning.
- **Verify:** standard build/test; manual device switch.
- **Out of scope:** per-project audio settings.

## T-257 Fill the Display page

- **Status:** ready (T-225 completed 2026-10-09; audit current implementation before starting)
- **Outcome:** theme choice from the bundled themes directory (replacing the `WORMEDIT_THEME_DIR` environment variable, which stays as an override), UI scale (auto, 100–200 %), frame-timing overlay toggle, reduced motion (no playhead smoothing), and waveform colors from theme roles.
- **Read first:** `src/ui/nuklear/theme.cpp` (`load`), `assets/themes/`, `src/ui/nuklear/frontend.cpp` (font and scale setup), `src/sys/frame_timing.cpp`.
- **Files owned:** `src/ui/nuklear/options_window.cpp`, `src/ui/nuklear/theme.cpp/.h`, `src/ui/nuklear/frontend.cpp` (scale and font rebuild only), `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `tests/core/preferences_tests.cpp`.
- **Steps:** 1. Theme reload at runtime (re-run `theme::load` and `apply_theme`). 2. UI scale rebuilds the font atlas once.
- **Invariants:** resize/high-DPI alignment unchanged.
- **Reject/failure behavior:** an invalid theme file keeps the previous theme with a warning.
- **Verify:** standard build/test; smoke PNGs at 100 % and 150 %.
- **Out of scope:** a light theme.

## T-258 Host shortcuts, input, and external players in Options

- **Status:** ready
- **Outcome:** the Input & shortcuts page embeds the shortcut remapping list (T-59) and default lane-key bindings used for new or imported charts (the T-242 number-row default and named alternatives such as DFJK for 4K); the External players page hosts the existing T-214 panel; the old standalone panels open the corresponding Options page instead.
- **Read first:** `src/ui/nuklear/external_player_panel.cpp`, `docs/chart-editor-plan/tasks/editing-commands.md` (T-59), `docs/chart-editor-plan/tasks/compose-ux.md` (T-242), `include/wormedit/core/lane_profiles.h`.
- **Files owned:** `src/ui/nuklear/options_window.cpp`, `src/ui/nuklear/external_player_panel.cpp/.h`, `src/sys/external_player.cpp`, `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `tests/core/lane_profile_tests.cpp`.
- **Steps:** 1. Move the external player UI into a page (pure move). 2. Default-binding preference per lane count consumed by T-242's `default_input_profile_for`. 3. Embed T-59's list when it exists; until then show a link-style note.
- **Invariants:** per-chart input profiles already saved in projects are never overwritten by preferences.
- **Reject/failure behavior:** conflicting default bindings are rejected with the conflicting keys named.
- **Verify:** standard build/test.
- **Out of scope:** per-chart binding editor (T-262 column).

## T-259 Add chart management commands

- **Status:** done
- **Outcome:** revision-checked, undoable core commands to duplicate a chart (new stable IDs for the chart and all its objects, same shared resources, name suffixed " (copy)"), delete a chart (removing it from output groups; deleting the last chart is rejected), reorder charts, and move a chart between output groups.
- **Read first:** `include/wormedit/core/document.h` (`chart_document_t`, `output_group_t`, song aggregate), `include/wormedit/core/command.h`, T-52 in `docs/chart-editor-plan/tasks/editing-commands.md` (new chart command, same new file).
- **Files owned:** `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp` (created by T-52 or here if T-52 has not landed), `tests/core/chart_editor_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Duplicate: deep copy with an ID remap table for measures, notes, BGM, timing events, layers, bookmarks, recipes, branch statements; validate the whole document.
  2. Delete: reject when referenced by something that cannot be updated automatically; otherwise remove from groups.
  3. Reorder and group move.
  4. Tests: duplicate validates and shares resources; undo of each; stale revision rejects; delete-last rejects.
- **Invariants:** resources and assets are never copied or deleted by these commands.
- **Reject/failure behavior:** any validation failure rejects without a history entry.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'` passed 746/746 in 15.86 seconds (`/tmp/wormedit-t259/core-prefix.log`). Clean-index production/test compilation and private linking passed; focused chart-management cases passed 7/7 with 74 assertions (`/tmp/wormedit-t259/focused-final.log`). Tests cover full graph remapping, shared resources, retained BMS branches/projections, undo/redo, history-retained ID freshness, and exact serialized-document preservation on rejection. Both affected C++ files passed clang-tidy cognitive-complexity checks (`/tmp/wormedit-t259/tidy-final.log`).
- **Out of scope:** UI (T-260), importing charts (T-262).

## T-260 Turn Setup into a Project tab with a chart manager

- **Status:** done (Project actions verified; New preserves the active profile, resolution, and exact offset; full suite 1016/1016, core 780/780, and isolated saved-document GUI proof passed)
- **Outcome:** project scope only. The Setup tab is renamed **Project** and shows a table of the song's charts: name, author, rating, mode/key count, output group, note count, length; row actions New (T-52), Duplicate, Delete (with confirmation), Up/Down, and Open in Compose; multi-select enables batch metadata (T-128). The recovery rows and StepMania export policy stay below the table.
- **Read first:** `src/ui/nuklear/setup_panel.cpp/.h`, `src/ui/nuklear/setup_state.h`, `src/core/workspace.cpp` (tab list and selected difficulty), `include/wormedit/core/chart_editor.h`.
- **Files owned:** `src/ui/nuklear/setup_panel.cpp/.h`, `src/ui/nuklear/setup_state.cpp/.h`, new `src/sys/project_view.cpp/.h`, `src/sys/main.cpp` (dispatch/view only), `src/core/workspace.cpp` (tab label only), `src/ui/frontend.h`, `CMakeLists.txt`, `tests/ui/nuklear_setup_state_tests.cpp`; user-approved prerequisite/integration extensions: `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`, `tests/core/chart_editor_tests.cpp` (New chart), `include/wormedit/core/metadata_editor.h`, `src/core/metadata_editor.cpp`, `tests/core/metadata_editor_tests.cpp` (atomic batch metadata), `src/sys/workspace_projection.cpp`, `src/ui/nuklear/frontend.cpp` (Project labels and recovery placement), `tests/CMakeLists.txt`, new `tests/sys/project_view_tests.cpp`.
- **Steps:** 1. Scalar table view (bounded to 256 charts). 2. Actions through the standard pattern. 3. Delete confirmation lists what will be removed (notes count, groups).
- **Invariants:** the command id `workspace.open_setup` keeps working (alias to the Project tab).
- **Reject/failure behavior:** core rejections shown per row.
- **Verify:** full build passed; full suite 1016/1016 in 44.64 seconds (`/tmp/wormedit-t249-t279-t294-final-ctest.log`); core 780/780 in 12.05 seconds (`/tmp/wormedit-t260/template-core-prefix.log`). Configured clang-tidy found no cognitive-complexity violations in the five affected follow-up translation units (`/tmp/wormedit-t260/template-tidy.log`). Isolated Xvfb/SDL dummy multi-difficulty SSC-derived GUI actions passed; Project New and Ctrl+Shift+N opened four-lane empty Compose charts, and the saved native document confirmed both new charts preserve `stepmania:dance-single`, resolution 3840, exact offset -7/20, one measure, and no notes (`/tmp/wormedit-t260/template-exact-values.log`; `project-new-compose.png`, `project-new-shortcut.png`, and `project-final.png` in the same directory).
- **Out of scope:** output-group editing (T-261).

## T-261 Manage output groups in the Project tab

- **Status:** ready
- **Outcome:** a second table lists output groups (name, family, output path, charts); the user can create, rename, delete a group, change its family and relative path, and assign charts with checkboxes; each group shows validation from the conversion service (for example "SM/SSC: several charts allowed", "BMS: one chart per file") and a Preview export button (T-110).
- **Read first:** `include/wormedit/core/document.h` (`output_group_t`), `include/wormedit/formats/conversion.h`, `include/wormedit/formats/conversion_publication.h`, `src/sys/compose_actions.cpp` (export action).
- **Files owned:** `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`, `tests/core/chart_editor_tests.cpp`, `src/ui/nuklear/setup_panel.cpp`, `src/sys/project_view.cpp`, `src/ui/frontend.h`.
- **Steps:** 1. Core group commands (create/rename/delete/family/path/membership) with validation of paths (relative, no traversal). 2. Table UI. 3. Per-group validation text from a dry-run conversion (off the frame thread).
- **Invariants:** existing published output manifests stay valid; renaming a group does not rename files on disk.
- **Reject/failure behavior:** invalid paths or unsupported family/chart combinations reject with the reason.
- **Verify:** standard build/test.
- **Out of scope:** the export flow itself (T-110/T-111).

## T-262 Import a chart from another file into the current project

- **Status:** ready
- **Outcome:** Project tab → "Import chart…" reads a chart file (BMS family, SM/SSC, osu!mania, BMSON, `.qua` when T-174 lands), lets the user pick which of its difficulties to add, previews resource matching (reuse identical files, add new ones, report missing), and adds the charts as one undoable command; timing differences from the song are shown before import.
- **Read first:** `src/sys/project_open.cpp`, `include/wormedit/core/resource_assignment.h` (`apply_resource_asset_import`), `include/wormedit/core/document.h`.
- **Files owned:** new `include/wormedit/core/chart_import.h`, new `src/core/chart_import.cpp`, new `tests/core/chart_import_tests.cpp`, `src/sys/project_open.cpp/.h`, `src/ui/nuklear/setup_panel.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Parse into a temporary song. 2. Resource merge plan (content hash or normalized path match). 3. ID remap and commit. 4. Tests: import an SM difficulty into a BMS-based song; duplicate resource reuse; missing file report.
- **Invariants:** the current song's charts and resources are untouched except for added references.
- **Reject/failure behavior:** parse errors or validation failures add nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|project_open'`.
- **Out of scope:** merging two charts into one.

## T-263 Add a project settings store and page

- **Status:** ready
- **Outcome:** the native project document carries a typed `project_settings_t` (export policies including the current StepMania policy, project asset directories, preferred external player id, and explicit overrides of program defaults such as default snap for new charts in this project); the Project tab shows them in a "Project settings" section with "Use program default" toggles; editing them is undoable and marks the project dirty.
- **Read first:** the scope rule above, `include/wormedit/core/document.h` (song aggregate), `src/sys/main.cpp` (where `stepmania_export_policy` lives today), `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `include/wormedit/core/preferences.h`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, new `include/wormedit/core/project_settings.h`, new `src/core/project_settings.cpp`, new `tests/core/project_settings_tests.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`, `src/ui/nuklear/setup_panel.cpp`, `src/sys/project_view.cpp`, `src/sys/main.cpp` (move the StepMania policy state into the document), `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Type with optional overrides; `effective(project_settings, preferences)` resolves a value and says which scope it came from.
  2. Revision-checked edit command; native schema migration (older projects get empty settings).
  3. Move the StepMania export policy from application state into the document (default = current program behavior).
  4. Project tab section; each overridable field shows the program default greyed when not overridden.
  5. Tests: resolution order, migration, undo, dirty flag.
- **Invariants:** no per-user values (device, theme, shortcuts, window layout) in the project; no project values in `preferences.json`.
- **Reject/failure behavior:** invalid values reject the edit.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|native_project'`.
- **Out of scope:** program options UI (T-254).
