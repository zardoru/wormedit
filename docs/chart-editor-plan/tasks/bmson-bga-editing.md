# BMSON layers and profiles, all BGA layers, one verb set for BGM and play lanes (2026-10-07 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-07, with the causes found while planning:

- **BMSON lane colors do not follow the profile.** `color_stage` in `src/core/lane_profiles.cpp` colors only the `bms-`, `bme-`, `pms-`, osu! and `bmson-generic-` profiles, so `bmson-beat-*` and `bmson-popn-*` lanes get no key colors. `bmson-beat-7k` should look like BMS SP: seven keys plus a scratch.
- **The sabun chart cannot take a lane profile.** `/mnt/pukeipukei/G/bms/bms-b/BOFXVII/iberai_dragback/sabun/_untitled.bms` has no playable channels, only `01`, `03`, `04` and `07`. It opens through `populate_bgm_only_compose_projection`, and `dispatch_compose_lane_profile` (`src/sys/compose_actions.cpp:300`) rejects with "No lane layout is active at the cursor." The "empty BMS takes its first layout" path from `e98244d` is reachable only through layout edits.
- **Only two BGA kinds show.** The document has `background`, `poor`, `layer`, `video` and `storyboard`, with no layer 2: the BMS parser folds `0A` into `layer` (`src/formats/bms.cpp:968`), and the writer splits collisions back into `07`/`0A`. The Compose BGA lane and the selected-image preview handle only `background` and `video` (`src/sys/compose_view.cpp:1137`).
- **The Rotterdam BGA preview is unreliable.** `rotterdam_7HYPER.bme` declares 163 `#BMP` files as `.png`, but the folder ships `.bmp` files. The name resolver tries alternate extensions only for audio (`src/sys/project_open.cpp:226`). Its 92 layer (`07`) events are also never shown in the editor.
- **Shift+lane key on a BGM selection moves everything.** `compose_input.cpp` routes every `LANE_KEY_PRESS` with BGM selected to `move_selected_bgm_to_lane` and ignores `successive`, while play notes go through `assign_selected_notes_to_lane` (`src/core/interaction.cpp:2087`), which honors it. The two paths duplicate verbs and each has its own exceptions. A refactor is worth it.

## T-446 Color and order BMSON beat and popn lanes like their BMS equivalents

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** `bmson-beat-5k`/`7k` draw scratch, then alternating white and black keys, exactly like `bms-7k` (scratch on the left), and `beat-10k`/`14k` draw like BMS DP. `bmson-popn-9k` uses the PMS button colors. A core test compares the key colors and display order with the BMS profile that has the same lane count.
- **Read first:** `src/core/lane_profiles.cpp` (`color_stage`, `assign_lane_key_colors`), `src/formats/bmson.cpp` (`select_bmson_beat_lanes`), `src/sys/compose_view.cpp:458`.
- **Files owned:** `src/core/lane_profiles.cpp`, `src/formats/bmson.cpp`, `tests/core/lane_profile_tests.cpp`, `tests/formats/bmson_tests.cpp`, `tests/sys/project_open_tests.cpp` (approved extension: verify BMSON source x lanes survive export and native save/reopen; listed tests lack those round trips), `src/formats/native_project.cpp` and `tests/formats/native_project_tests.cpp` (approved extension: repair old saved BMSON layouts whose generic colors were already serialized; retain authored nondefault colors), `src/formats/bmson_writer.cpp` and `tests/formats/bmson_writer_tests.cpp` (root-approved supporting extension: fix the uint32 upper-bound overflow that otherwise exports reordered source x incorrectly), this card.
- **Steps:**
  1. Treat `bmson-beat-` like `bms-` in `color_stage`, and `bmson-popn-` like `pms-`.
  2. Check the display order of the BMSON scratch (`x = 8`/`16`) against `bms-7k`/`bms-14k`, and fix the order if they differ.
  3. Run the existing load-time repair so charts already saved get the colors.
  4. Add a test per mode hint.
- **Invariants:** BMSON `x` values and export are unchanged. Only colors and display order change.
- **Reject/failure behavior:** an unknown mode hint keeps the `generic-nkeys` piano coloring.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'lane_profile|bmson'`; an isolated capture of `kick_your_ass2.bmson`. Recovery evidence: 132 tagged core/format/open/stem cases (4,357 assertions) and 3 native-color cases (79 assertions) pass in the isolated build. Source-x export also fixes an overflowing uint32 upper bound. The established Xvfb socket restriction leaves the isolated capture pending.
- **Out of scope:** user color schemes ([T-428](color-schemes.md)).

- **Integrated evidence:** main application build and all 2,434 tests pass; `/tmp/wormedit-oct7-root/audio-integrated-tests.log`. Mode colors, scratch order, authored native layouts and source-x round trips pass. The isolated GUI capture remains pending under the recorded Xvfb restriction.

## T-447 Give each BMSON sound channel its own semantic layer on import

- **Status:** ready
- **Outcome:** opening a BMSON chart with no semantic layers creates one layer per sound channel, named after its file stem, with colors from a fixed 16-color cycle. Every note and BGM marker of that channel belongs to the layer. A core test checks names, colors and membership.
- **Read first:** `include/wormedit/core/document.h` (`semantic_layer_t`), `src/core/semantic_layer_editor.cpp`, `src/formats/bmson.cpp`, `src/sys/project_open.cpp`.
- **Files owned:** a new `src/core/bmson_layer_defaults.cpp` and `include/wormedit/core/bmson_layer_defaults.h`, `src/sys/project_open.cpp` (call site), `CMakeLists.txt`, a new `tests/core/bmson_layer_defaults_tests.cpp`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. Define the 16-color cycle as a constant table: evenly spaced hues, readable on the dark field.
  2. Add a pure function (chart → layers + membership) that does nothing when the chart already has layers.
  3. Apply it when a `.bmson` source file is opened, not when a native project is reopened, and not as an undo entry.
  4. Test with 12 channels and with 20 (the cycle wraps), test that existing layers prevent it, and test that native round trips keep the layers.
- **Invariants:** BMSON export is byte-identical with or without the layers.
- **Reject/failure behavior:** if the function fails, the chart opens without layers and a warning is shown.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bmson_layer|semantic'`.
- **Out of scope:** an Options toggle. Ask the user if one is wanted.

## T-448 Let a BGM-only BMS take its first lane profile

- **Status:** done (2026-10-07; root integrated and verified)
- **Result:** the initial profile's real picker action was being dropped at the workspace boundary when a BGM-only projection had no retained interaction state. The boundary now supplies a temporary state only for first-profile and initial-layout actions, after validating the active Compose chart and exact document revision; charts with an existing layout still require retained state. `SET_BGM_COLUMN_COUNT` keeps its established no-state route. Production-boundary regression coverage verifies BMS 10K selection, payload validation, unchanged BGM data and one-step undo. Root will rerun the integrated suite. Earlier batch evidence: `/tmp/wormedit-oct7-root/editing-integrated-tests.log`, `/tmp/wormedit-oct7-root/sabun-profile-integrated.log`. Real-window visual check remains unavailable under the existing Xvfb restriction.
- **Color-order follow-up (2026-10-07):** BMS scratchless 10K canonical osu!mania colors now follow rendered left-to-right stage layout; same-profile and BMS-to-osu retargeting preserve lane identity, stage/order topology, and bindings. The actual scene geometry regression and default-color repair tests passed in the isolated core target (2,379 assertions, 37 profile cases); see `memory/coordination/10k-color-oct7.md` for focused commands and tidy evidence. Root handles the integrated build and full suite.
- **Outcome:** a BGM-only chart can choose any of the 41 built-in profiles (including osu!mania 12K and 16K SpecialStyle). On the sabun fixture, "BMS 7K" creates its layout and key bindings at measure 1, even with the cursor later in the chart. The playable lanes appear, note placement/undo works, and a second undo removes the layout. A native save/reopen preserves all 1,146 BGM events and 16 BGM columns (the source has 559 raw `#mmm01` lines).
- **Read first:** `src/sys/compose_actions.cpp` (`dispatch_compose_lane_profile`, `initial_input_profile`), `src/sys/compose_view.cpp` (`populate_bgm_only_compose_projection`), `src/core/lane_profile_switch.cpp`, `git show e98244d`.
- **Files owned:** `src/core/lane_profiles.cpp` and `tests/core/lane_profile_tests.cpp` (root-approved 2026-10-07 scratchless 10K color-order correction: assign canonical osu!mania colors in stage-then-display order, preserving lane IDs/orders/bindings), `memory/coordination/10k-color-oct7.md` (root-approved follow-up additionally corrects retarget rank ordering in already-owned `src/core/lane_profile_switch.cpp` and `tests/core/lane_profile_switch_tests.cpp` for BMS 10K to osu!mania 10K and same-profile reapply, preserving immutable IDs and stage topology), `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/core/lane_profile_switch.cpp`, `include/wormedit/core/lane_profile_switch.h`, `src/ui/frontend.h` (coordinator-approved: picker capacity 4 cannot hold all built-ins), `src/ui/nuklear/compose_inspector.cpp` (coordinator-approved: first profile needs a visible unselected placeholder), `tests/core/lane_profile_switch_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp` (coordinator-approved production dispatcher mutation and gesture-warning coverage), `tests/sys/frame_timing_tests.cpp` (root-approved production BGM-only profile projection and distinct SpecialStyle labels), `tests/ui/` event harness, this card.
- **Steps:**
  1. Reproduce headlessly with the sabun file copied into the scratchpad. Never write to `/mnt/pukeipukei`.
  2. When no segment is active, `SET_LANE_PROFILE` offers every built-in profile and creates the first layout through the `e98244d` layout and input path, with a materialized measure if one is needed.
  3. The BGM-only projection shows the picker.
  4. Add a core test and an event-harness case for choosing a profile, placing a note, then undoing.
- **Boundary regression correction (2026-10-07):** the actual dropdown action must pass through `dispatch_compose_workspace_action` when the BGM-only projection has no retained interaction state. The initial profile and initial layout insertion paths may create temporary state only for the active Compose chart with matching chart/revision payloads and no existing lane layouts. Test BMS 10K selection, initial layout insertion, stale/wrong-chart and existing-layout refusal, BGM preservation and one-step undo; pair the production boundary tests with dropdown press/release output from `tests/ui/bga_lane_event_harness_tests.cpp`.
- **Invariants:** all 1,146 parsed BGM events and all 16 BGM columns survive native project serialization and reopen. The raw `#mmm01` line count is 559, not the parsed event count.
- **Reject/failure behavior:** an active gesture blocks the change with the existing warning.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'lane_profile'`; `ctest --test-dir build -L event-harness`. Isolated evidence before final SpecialStyle addition: core lane-profile tests (61 assertions/12 cases), production dispatcher tests (20 assertions/4 cases), picker event-harness test (8 assertions), clang-tidy on touched C++ sources/tests. Scratch evidence: `/tmp/wormedit-oct7-root/sabun-profile-probe.log`; scratch copy `/tmp/wormedit-oct7-root/sabun-source/_untitled.bms` (CP932); native project `/tmp/wormedit-oct7-root/sabun-profile.wormedit`. Original file SHA-256 remains `be254a5c261be9bc4c99a264798f12081bad160de352479bd5e1fe4b17d07f43`.
- **Out of scope:** profile detection heuristics.

- **Boundary correction verified (2026-10-07):** `2749c6d` fixes first-profile and initial-layout actions silently dropped when retained interaction state is absent. Sol review and root application build/full 2,437-test suite pass. The actual dragback CP932 workspace-boundary probe now produces 1 layout, 10 bindings and 1 undo, preserving every serialized BGM event and column. [Detailed evidence](../../../memory/coordination/profile-click-oct7.md).

## T-449 Select same sound on BMSON selects the whole stem

- **Status:** blocked-on T-426
- **Outcome:** on a BMSON chart, the three [T-426](bms-editing.md) gestures use the sound channel (the stem) as the key. They select every tap and BGM marker of that channel: visible ones for Ctrl+Shift+Click, the whole chart for Ctrl+Alt+Click and Ctrl+Shift+A. Selecting a stem's cut line counts as a source.
- **Read first:** the T-426 card and its selector, `include/wormedit/core/document.h` (`sound_channel_t`, `sound_channel_event`), `src/sys/bmson_waveform_projection.cpp` (`rebuild_point_refs`).
- **Files owned:** the T-426 selector source and tests, `src/sys/compose_input.cpp`, this card.
- **Steps:**
  1. Extend the selector key with `sound_channel` for BMSON.
  2. Map notes and BGM events through `sound_channel_event` to their channel.
  3. Test a 3-channel fixture with mixed taps and markers, both visible-only and whole-chart.
- **Invariants:** selection only, with no undo entry.
- **Reject/failure behavior:** an object with no channel selects nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R select`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** BMS behavior (T-426).

## T-450 Model BMS layer 2 as its own BGA kind

- **Status:** ready
- **Outcome:** BMS `0A` events load as `visual_event_kind_t::layer2` and save back to `0A`. Native projects persist the kind with a schema migration. A round trip of a fixture with `04`, `06`, `07` and `0A` keeps each channel.
- **Read first:** `include/wormedit/core/document.h` (`visual_event_kind_t`), `src/formats/bms.cpp:960-975`, `src/formats/bms_writer.cpp:2350-2410`, `src/formats/native_project_migrations.cpp`.
- **Files owned:** those four, plus `src/formats/native_project.cpp`, `src/formats/bmson.cpp` (BMSON `layer_events` stay `layer`; it has no layer 2), `tests/formats/bms_tests.cpp`, `tests/formats/native_project_tests.cpp`, this card.
- **Steps:**
  1. Add the `layer2` enumerator, and update every `switch` the compiler flags.
  2. Parse `0A` to `layer2`, and write `layer2` to `0A`. Keep the collision split for two `layer` events at one position.
  3. Native migration: older files stay valid, and `0A` provenance is used to recover `layer2` where it is present.
  4. Test the round trips.
- **Invariants:** BMS export of existing fixtures is byte-identical.
- **Reject/failure behavior:** a native file from a newer schema is rejected as today.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|bms|native'`.
- **Out of scope:** drawing (T-451).

## T-451 Show every BGA layer as its own lane and composite them in the preview

- **Status:** blocked-on T-450
- **Outcome:** the BGA area shows four lanes, labeled BGA, Layer, Layer 2 and Poor, and a lane is hidden when its kind has no events and the user has not added any. The BGA preview composites base, then layer, then layer 2, with black as the transparent key for layers, as in BMS players. Placing from the BGA dock targets the lane under the pointer.
- **Read first:** `src/sys/compose_view.cpp:1120-1200`, `src/ui/nuklear/compose_canvas.cpp:2200`, `src/sys/compose_bga_actions.cpp`, `include/wormedit/core/bga_preview.h` (`bga_layer_state_t`), `src/sys/player_preview_presenter.cpp:440`.
- **Files owned:** those five, plus `src/core/bga_preview.cpp`, `src/sys/bga_dock_runtime.cpp`, `src/ui/frontend.h` (BGA lane kind), `src/ui/nuklear/compose_bga_panel.cpp`, BGA tests and the event harness, this card.
- **Steps:**
  1. Project one BGA lane per kind, with its events.
  2. Make `bga_layer_state_t` carry `layer2`, and composite in order with the black key.
  3. Dock placement and drags between BGA lanes change the kind, in one undo step.
  4. Add tests: projection per kind, compositing order, and a drag from Layer to Layer 2.
- **Invariants:** charts with only `04` events look as they do today, apart from the empty lanes, which stay hidden.
- **Reject/failure behavior:** dropping a video onto a layer lane is rejected with a reason.
- **Verify:** `ctest --test-dir build --output-on-failure -R bga`; an isolated capture of `rotterdam_7HYPER.bme` mid-song showing a character layer over the background.
- **Out of scope:** the storyboard kind.

## T-452 Resolve BMS image names across extensions

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** `rotterdam_7HYPER.bme` resolves all 173 `#BMP` entries: names declared as `.png` find the `.bmp` files. The BGA preview shows the expected images at three sampled measures.
- **Read first:** `src/sys/project_open.cpp:220-260` (the audio resolver), `tests/sys/project_open_tests.cpp`.
- **Files owned:** `src/sys/project_open.cpp`, `tests/sys/project_open_tests.cpp`, this card.
- **Steps:**
  1. Generalize the resolver to take an extension set per asset kind. Images try `.bmp`, `.png`, `.jpg` and `.jpeg`, and video tries `.mpg`, `.mp4`, `.avi`, `.wmv` and `.webm`, with the same "exactly one match" rule.
  2. Test a declared `.png` with only a `.bmp` on disk, an ambiguous pair, and case variations.
  3. Open the real chart read-only, count the unresolved images, and record the result here.
- **Invariants:** the declared name is kept for export. Only the resolved path changes.
- **Reject/failure behavior:** an ambiguous match stays unresolved with a warning naming both files.
- **Verify:** `ctest --test-dir build --output-on-failure -R project_open`. Recovery evidence: the read-only production probe opened Rotterdam with CP932, resolved all 173 visual assets, and reported 0 unresolved (`/tmp/bmson-rotterdam-recovery.log`). Image/video exact, alternate, ambiguous, case and path-safety cases pass in the tagged core test run; authored export declarations remain intact. Visual preview sampling remains pending under the established Xvfb restriction.
- **Out of scope:** layer display (T-451).

- **Integrated evidence:** main application build and all 2,434 tests pass; `/tmp/wormedit-oct7-root/audio-integrated-tests.log`. The integrated read-only Rotterdam probe reports 173 visuals and 0 unresolved (`rotterdam-integrated.log`). GUI preview sampling remains pending under the recorded Xvfb restriction.

## T-453 Audit the verbs on BGM objects and play notes

- **Status:** ready
- **Outcome:** a table on this card lists every editing verb, with its BGM path, its play-lane path, the behavior difference, and a proposed single semantic. The verbs: select, move by drag, Y-lock, lane key, Shift+lane key, move to BGM, delete, copy, paste, quantize, layer assign, select same sound, and audition.
- **Read first:** `src/sys/compose_input.cpp`, `src/sys/compose_note_to_bgm.cpp`, `src/core/note_to_bgm.cpp`, `src/core/interaction.cpp`, `src/ui/nuklear/compose_object_input.cpp`.
- **Files owned:** this card only.
- **Steps:**
  1. For each verb, follow both paths from the input kind to the core command, and record file:line.
  2. Mark each difference as intended (the BMSON stem rules, for example) or accidental.
  3. Propose one core entry point per verb that takes a mixed selection (`editor_selection_object_t` of notes and BGM events).
  4. **STOP** and ask the user to approve the semantics table before T-455.
- **Invariants:** no source changes.
- **Reject/failure behavior:** none.
- **Verify:** the table is complete for all listed verbs.
- **Out of scope:** implementation.

## T-454 Shift+lane key on BGM events moves one and deselects it

- **Status:** done (2026-10-07; root integrated and verified)
- **Result:** Shift moves and deselects one selected BGM/note in deterministic document order; plain mixed assignment commits atomically with one undo. Collision preserves selection. Core, real event harness and production dispatcher coverage pass in the integrated 2,417-test suite; `/tmp/wormedit-oct7-root/editing-integrated-tests.log`.
- **Outcome:** with three BGM events selected, Shift+lane key moves the first one in document order into the lane and deselects it, leaving the other two selected, as for play notes. A mixed selection of notes and BGM events follows the same order. Each press is one undo step.
- **Read first:** `src/sys/compose_input.cpp` (`WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE` / `LANE_KEY_PRESS` branch), `src/sys/compose_note_to_bgm.cpp` (`move_selected_bgm_to_lane`), `src/core/interaction.cpp` (`assign_selected_notes_to_lane`), [T-422](bms-editing.md).
- **Files owned:** `src/sys/compose_input.cpp`, `src/sys/compose_note_to_bgm.cpp`, `src/sys/compose_note_to_bgm.h`, `include/wormedit/core/note_to_bgm.h` and `src/core/note_to_bgm.cpp` (coordinator-approved targeted atomic mixed-selection lane command; no general verb refactor), `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/core/note_to_bgm_tests.cpp` (coordinator-approved helper semantics coverage), this card.
- **Steps:**
  1. Pass `input.successive` (the Shift state) to `move_selected_bgm_to_lane`.
  2. When it is set, take the first selected object in document order, whether note or BGM, move only that one, and remove it from the selection. Leave the moved object deselected.
  3. Test the real helper for mixed-selection document order, one-command presses, undo, collision preservation, and the plain whole-selection path; add an event-harness case asserting the production Shift+lane key emits `successive`.
- **Invariants:** a plain lane key still moves the whole selection.
- **Reject/failure behavior:** a collision leaves the event selected and shows the reason.
- **Verify:** `ctest --test-dir build -L event-harness`.
- **Out of scope:** the general refactor (T-455).

## T-455 One core entry point per editing verb for mixed selections

- **Status:** blocked-on T-453
- **Outcome:** each verb approved in T-453 has one core command that takes notes and BGM events together. `compose_input.cpp` no longer branches on "BGM selected" before choosing a verb, and every approved difference is a documented rule inside the command.
- **Read first:** T-453's approved table, and the files it cites.
- **Files owned:** recorded from T-453 before any edit.
- **Steps:**
  1. Do one verb per commit, in the order T-453 approves, starting with lane assignment (it absorbs T-454).
  2. Each commit removes the duplicate path and moves its tests to the shared command.
  3. After each verb, the full event harness passes.
- **Invariants:** BMSON stem rules (continue/restart points, slice-point moves) behave as before.
- **Reject/failure behavior:** a mixed command rejects atomically.
- **Verify:** `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on the touched files.
- **Out of scope:** new verbs.
