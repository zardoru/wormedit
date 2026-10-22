# BMSON layers and profiles, all BGA layers, one verb set for BGM and play lanes (2026-10-07 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-07, with the causes found while planning:

- **BMSON lane colors do not follow the profile.** `color_stage` in `src/core/lane_profiles.cpp` colors only the `bms-`, `bme-`, `pms-`, osu! and `bmson-generic-` profiles, so `bmson-beat-*` and `bmson-popn-*` lanes get no key colors. `bmson-beat-7k` should look like BMS SP: seven keys plus a scratch.
- **The sabun chart cannot take a lane profile.** `/mnt/pukeipukei/G/bms/bms-b/BOFXVII/iberai_dragback/sabun/_untitled.bms` has no playable channels, only `01`, `03`, `04` and `07`. It opens through `populate_bgm_only_compose_projection`, and `dispatch_compose_lane_profile` (`src/sys/compose/actions/compose_actions.cpp:300`) rejects with "No lane layout is active at the cursor." The "empty BMS takes its first layout" path from `e98244d` is reachable only through layout edits.
- **Only two BGA kinds show.** The document has `background`, `poor`, `layer`, `video` and `storyboard`, with no layer 2: the BMS parser folds `0A` into `layer` (`src/formats/bms.cpp:968`), and the writer splits collisions back into `07`/`0A`. The Compose BGA lane and the selected-image preview handle only `background` and `video` (`src/sys/compose_view.cpp:1137`).
- **The Rotterdam BGA preview is unreliable.** `rotterdam_7HYPER.bme` declares 163 `#BMP` files as `.png`, but the folder ships `.bmp` files. The name resolver tries alternate extensions only for audio (`src/sys/project/project_open.cpp:226`). Its 92 layer (`07`) events are also never shown in the editor.
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

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** opening a BMSON chart with no semantic layers creates one layer per sound channel, named after its file stem, with colors from a fixed 16-color cycle. Every note and BGM marker belongs to the layer keyed by its sound-channel event. Authored layers remain untouched, and a failed initialization leaves the chart open without partial layer state.
- **Read first:** `include/wormedit/core/document.h` (`semantic_layer_t`), `src/core/semantic_layer_editor.cpp`, `src/formats/bmson.cpp`, `src/sys/project/project_open.cpp`.
- **Files owned:** a new `src/core/bmson_layer_defaults.cpp` and `include/wormedit/core/bmson_layer_defaults.h`, `src/sys/project/project_open.cpp` (call site), `CMakeLists.txt`, a new `tests/core/bmson_layer_defaults_tests.cpp`, `tests/CMakeLists.txt`, `tests/sys/project_open_tests.cpp` (root-approved 2026-10-07 production BMSON import and native no-reseed boundary coverage), `memory/coordination/layers-oct7.md` (coordinator scope and review evidence), this card.
- **Steps:**
  1. Define the 16-color cycle as a constant table: evenly spaced hues, readable on the dark field.
  2. Add a pure function (chart → layers + membership) that does nothing when the chart already has layers.
  3. Apply it when a `.bmson` source file is opened, not when a native project is reopened, and not as an undo entry.
  4. Test with 12 channels and with 20 (the cycle wraps), test that existing layers prevent it, and test that native round trips keep the layers.
- **Invariants:** BMSON export is byte-identical with or without the layers.
- **Reject/failure behavior:** if the function fails, the chart opens without layers and a warning is shown.
- **Verify:** isolated `wormedit_core_tests` build passed. `ctest --test-dir /tmp/layers-oct7-headless-build --output-on-failure -R 'bmson_layer|semantic'` passed 33/33; focused BMSON default/import/native-boundary cases passed 4/4; `documentation.chart_editor_state` passed 1/1. Configured `clang-tidy` passed on all four changed source/test files with no changed-code function-complexity warnings (third-party nlohmann header warnings remain). Root integrated `4ba5087`: application build and 2,443/2,443 CTest cases passed; the subsequent Layer 2 main suite passes 2,446/2,446. Regenerated production Kick probe verifies 12 channels/layers, all 515 memberships, native round trip and identical BMSON export (`/home/zardoru/devel/wormedit-scratch-builds/root-oct7/layer-import-probe.log`).
- **Out of scope:** an Options toggle. Ask the user if one is wanted.

## T-448 Let a BGM-only BMS take its first lane profile

- **Status:** done (2026-10-07; root integrated and verified)
- **Result:** the initial profile's real picker action was being dropped at the workspace boundary when a BGM-only projection had no retained interaction state. The boundary now supplies a temporary state only for first-profile and initial-layout actions, after validating the active Compose chart and exact document revision; charts with an existing layout still require retained state. `SET_BGM_COLUMN_COUNT` keeps its established no-state route. Production-boundary regression coverage verifies BMS 10K selection, payload validation, unchanged BGM data and one-step undo. Root will rerun the integrated suite. Earlier batch evidence: `/tmp/wormedit-oct7-root/editing-integrated-tests.log`, `/tmp/wormedit-oct7-root/sabun-profile-integrated.log`. Real-window visual check remains unavailable under the existing Xvfb restriction.
- **Color-order follow-up (2026-10-07):** `a3adcfa` fixes BMS scratchless 10K canonical osu!mania colors to follow rendered left-to-right stage layout; same-profile and BMS-to-osu retargeting preserve lane identity, stage/order topology, and bindings. Actual scene geometry confirms `wbwbg | ybwbw`; root application build and all 2,439 tests pass. Focused checks passed 2,379 assertions in 37 profile cases; see `memory/coordination/10k-color-oct7.md` for commands and tidy evidence. Reapply/update the profile to refresh previously saved nondefault colors.
- **Outcome:** a BGM-only chart can choose any of the 41 built-in profiles (including osu!mania 12K and 16K SpecialStyle). On the sabun fixture, "BMS 7K" creates its layout and key bindings at measure 1, even with the cursor later in the chart. The playable lanes appear, note placement/undo works, and a second undo removes the layout. A native save/reopen preserves all 1,146 BGM events and 16 BGM columns (the source has 559 raw `#mmm01` lines).
- **Read first:** `src/sys/compose/actions/compose_actions.cpp` (`dispatch_compose_lane_profile`, `initial_input_profile`), `src/sys/compose_view.cpp` (`populate_bgm_only_compose_projection`), `src/core/lane_profile_switch.cpp`, `git show e98244d`.
- **Files owned:** `src/core/lane_profiles.cpp` and `tests/core/lane_profile_tests.cpp` (root-approved 2026-10-07 scratchless 10K color-order correction: assign canonical osu!mania colors in stage-then-display order, preserving lane IDs/orders/bindings), `memory/coordination/10k-color-oct7.md` (root-approved follow-up additionally corrects retarget rank ordering in already-owned `src/core/lane_profile_switch.cpp` and `tests/core/lane_profile_switch_tests.cpp` for BMS 10K to osu!mania 10K and same-profile reapply, preserving immutable IDs and stage topology), `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/core/lane_profile_switch.cpp`, `include/wormedit/core/lane_profile_switch.h`, `src/ui/frontend.h` (coordinator-approved: picker capacity 4 cannot hold all built-ins), `src/ui/nuklear/compose_inspector.cpp` (coordinator-approved: first profile needs a visible unselected placeholder), `tests/core/lane_profile_switch_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp` (coordinator-approved production dispatcher mutation and gesture-warning coverage), `tests/sys/frame_timing_tests.cpp` (root-approved production BGM-only profile projection and distinct SpecialStyle labels), `tests/ui/` event harness, this card.
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

- **Status:** done (2026-10-09; shared selector, three-channel regressions, normal application build and full 2,949-test suite pass)
- **Result:** all three gestures key linked semantic notes/BGM markers to the retained channel; projected cut lines provide the same stable source. The three-channel fixture keeps channels sharing an asset distinct, checks visible/whole scope and rejects unlinked objects. Sounds rows reuse the existing channel-resource binding resolver; ambiguous reuse selects nothing. Selection remains transient with no Undo. See [batch evidence](../../../memory/coordination/oct9-sound-selection-batch.md).
- **Supporting-file ownership (2026-10-09):** coordinator approved the shared input-panel source/header and event harness for picking stem cut-line sources and verifying BGM/cut routing through the T-426 inputs.
- **Outcome:** on a BMSON chart, the three [T-426](bms-editing.md) gestures use the sound channel (the stem) as the key. They select every tap and BGM marker of that channel: visible ones for Ctrl+Shift+Click, the whole chart for Ctrl+Alt+Click and Ctrl+Shift+A. Selecting a stem's cut line counts as a source.
- **Read first:** the T-426 card and its selector, `include/wormedit/core/document.h` (`sound_channel_t`, `sound_channel_event`), `src/sys/bmson_waveform_projection.cpp` (`rebuild_point_refs`).
- **Files owned:** the T-426 selector source and tests, `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_input_panel.h`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Extend the selector key with `sound_channel` for BMSON.
  2. Map notes and BGM events through `sound_channel_event` to their channel.
  3. Test a 3-channel fixture with mixed taps and markers, both visible-only and whole-chart.
- **Invariants:** selection only, with no undo entry.
- **Reject/failure behavior:** an object with no channel selects nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R select`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** BMS behavior (T-426).

## T-450 Model BMS layer 2 as its own BGA kind

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** BMS `0A` events load as `visual_event_kind_t::layer2` and save back to `0A`. Native projects persist the kind with a schema migration. A round trip of a fixture with `04`, `06`, `07` and `0A` keeps each channel.
- **Read first:** `include/wormedit/core/document.h` (`visual_event_kind_t`), `src/formats/bms.cpp:960-975`, `src/formats/bms_writer.cpp:2350-2410`, `src/formats/native_project_migrations.cpp`.
- **Files owned:** `src/core/timing.cpp` (coordinator-approved root-identified appended kind draw-order handling), `src/core/bga_preview.cpp` (coordinator-approved root-identified enum bound and explicit non-compositing Layer2 handling so preview/audio accepts the chart), `tests/core/bga_preview_tests.cpp` (coordinator-approved preview schedule acceptance without Layer2-to-Layer reinterpretation), those four, plus `src/formats/native_project_migrations.cpp` (schema 3→4 event provenance recovery), `src/formats/native_project_storage.cpp` (root-approved schema-4 recovery payload acceptance), `src/formats/native_project.cpp`, `src/formats/bmson.cpp` (BMSON `layer_events` stay `layer`; it has no layer 2), `tests/formats/bms_tests.cpp`, `tests/formats/bms_writer_tests.cpp` (root-approved expected-kind correction for its BMS round-trip), `tests/formats/native_project_tests.cpp`, `tests/formats/native_project_storage_tests.cpp` (root-approved schema-4 recovery and future-version package assertions), this card.
- **Steps:**
  1. Add the `layer2` enumerator, and update every `switch` the compiler flags.
  2. Parse `0A` to `layer2`, and write `layer2` to `0A`. Keep the collision split for two `layer` events at one position.
  3. Native migration: older files stay valid, and `0A` provenance is used to recover `layer2` where it is present.
  4. Test the round trips.
- **Invariants:** BMS export of existing fixtures is byte-identical.
- **Reject/failure behavior:** a native file from a newer schema is rejected as today.
- **Verify:** isolated `wormedit_core_tests` build passed. `ctest --test-dir /tmp/layers-oct7-headless-build --output-on-failure -R '^core\.|bms|native'` passed 1,496/1,496; `documentation.chart_editor_state` passed 1/1. Configured clang-tidy ran on all changed implementation and test sources; new functions are below complexity 25, with only existing complexity findings in previously large test cases and third-party nlohmann code. Root integrated `4c446cd`; application build and all 2,446/2,446 CTest cases pass. Independent synthetic probe passes distinct 04/06/07/0A kinds, exact retained bytes, normalized export, schema-3 provenance recovery and duplicate Layer 2 refusal. Regenerated evidence is under `/home/zardoru/devel/wormedit-scratch-builds/root-oct7/` (`layer2-integrated-build.log`, `layer2-integrated-ctest.log`, `layer2-probe.log`).
- **Out of scope:** drawing (T-451).

## T-451 Show every BGA layer as its own lane and composite them in the preview

- **Status:** done (2026-10-11; original-chart native visual gate verified)
- **Outcome:** the BGA area shows four lanes, labeled BGA, Layer, Layer 2 and Poor, and a lane is hidden when its kind has no events and the user has not added any. The BGA preview composites base, then layer, then layer 2, with black as the transparent key for layers, as in BMS players. Placing from the BGA dock targets the lane under the pointer.
- **Read first:** `src/sys/compose_view.cpp:1120-1200`, `src/ui/nuklear/compose_canvas.cpp:2200`, `src/sys/compose/actions/compose_bga_actions.cpp`, `include/wormedit/core/bga_preview.h` (`bga_layer_state_t`), `src/sys/audio/player_preview_presenter.cpp:440`.
- **Files owned:** `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose/actions/compose_bga_actions.cpp`, `include/wormedit/core/bga_preview.h`, `src/sys/audio/player_preview_presenter.cpp`, `src/core/bga_preview.cpp`, `src/sys/resources/bga_dock_runtime.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_bga_panel.cpp`, `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `tests/core/editor_scene_tests.cpp`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `tests/core/resource_assignment_tests.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.h`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/player_preview.cpp`, `src/ui/nuklear/player_preview_texture.cpp`, `src/ui/nuklear/player_preview_texture.h`, `tests/ui/nuklear_player_preview_texture_tests.cpp`, `tests/sys/player_preview_presenter_tests.cpp`, `tests/core/bga_preview_tests.cpp`, `tests/sys/bga_lane_projection_tests.cpp`, `tests/sys/bga_lane_workflow_tests.cpp`, `tests/ui/{bga_lane,nuklear}_event_harness_tests.cpp`, this card section.
- **Supporting ownership approved (2026-10-07):** the explicit scene, resource-assignment, pointer-input/state, player-renderer and existing test paths above support per-kind geometry, atomic kind-aware placement/movement, pointer payloads and black-key rendering. Build manifests and other card sections remain root-owned. BGA frontend regions and BGA canvas regions stay exclusive until root integration; root separately permits stem-only struct/envelope edits in the stem worktree.
- **Additional Files owned (root-approved 2026-10-07):** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp` for a view-only four-kind revealed-lane mask and its interaction-view projection. Reveal changes neither document revision nor undo, follows the existing chart/project interaction lifecycle, validates the four allowed bits, and cannot hide authored event kinds. Video uses the base lane; storyboard stays excluded.
- **Additional Files owned (root-approved 2026-10-07):** `tests/sys/compose_action_notification_tests.cpp` for production workspace input/drop/drag/reveal boundary regressions paired with real frontend payload tests. Stale/wrong-chart/unsupported-kind actions preserve revision, event geometry/resource and undo.
- **Additional Files owned (root-approved 2026-10-07):** `src/core/player_preview.cpp`, `tests/core/player_preview_tests.cpp` for Layer 2 scene validation parity (kind, identities, frame bounds), including valid/invalid/stale cases.
- **Additional Files owned (root-approved 2026-10-07):** `src/ui/nuklear/compose_input_internal.h` for one pointer-to-BGA-kind hit-test declaration shared by placement and drag handlers.
- **Compatibility policy (2026-10-07):** movement retains existing stack-capable overlap semantics, including kind changes onto occupied rows. Placement retains its collision rejection within the destination lane; different kinds may share a row. Storyboard resource replacement retains its existing behavior.
- **Steps:**
  1. Project one BGA lane per kind, with its events.
  2. Make `bga_layer_state_t` carry `layer2`, and composite in order with the black key.
  3. Dock placement and drags between BGA lanes change the kind, in one undo step.
  4. Add tests: projection per kind, compositing order, and a drag from Layer to Layer 2.
- **Invariants:** charts with only `04` events look as they do today, apart from the empty lanes, which stay hidden.
- **Reject/failure behavior:** dropping a video onto a layer lane is rejected with a reason.
- **Verify:** `ctest --test-dir build --output-on-failure -R bga`; an isolated capture of `rotterdam_7HYPER.bme` mid-song showing a character layer over the background.
- **Implementation evidence (2026-10-07):** fresh isolated normal application/event-harness build passed using immutable main dependency source directories (no gitlink symlink). Feature and production renderer/input checks pass 69/69 (`tests-dropdown-storyboard.log`), including an actual Add BGA lane dropdown click/reveal payload. Wider core/frame checks pass 1,775/1,775 (`tests-core-frame-final.log`); the full event-harness label passes 195/195 (`tests-final-event-harness.log`), followed by the additional dropdown case in the feature run. Required lowercase `bga` plus documentation checks pass (`tests-required-card-final.log`). Actual software-rendered pixels cover video/base→Layer→Layer 2 ordering, same-resource opaque/keyed cache separation, exact RGB-black transparency, preserved nonblack alpha and unchanged storyboard draw-order slots. Production input checks cover one-undo same-row kind changes, lane-targeted dock placement, view-only reveal, and unchanged event geometry/resource/revision/history on stale, wrong-chart, unsupported-kind and video-overlay rejection; the production rejection test checks the explanatory reason (`tests-video-reason.log`). Stacked-row moves remain permitted. Configured clang-tidy ran all affected implementation/test files; new functions/tests remain at or below 25 and existing findings are unchanged. Logs/build are under `/home/zardoru/devel/wormedit-scratch-builds/next-oct7/bga/`.
- **Root integrated evidence:** normal main application build and all 2,495/2,495 tests pass (`next-oct7/root/final-main-ctest-verified.log`). Final review corrected the new dropdown to use the central chrome guard and made the decoded image fixture use its actual resource and 2×1 geometry; architecture, actual dropdown and presenter regressions pass. Configured correction tidy has only unchanged legacy findings. Both worker trees and generated builds were removed; branches and logs remain.
- **Visual gate completed (2026-10-11):** Isolated authenticated TCP Xvfb, using actual `rotterdam_7HYPER.bme` imported by production `open_project_file` into a temporary native fixture, verifies a character layer over the background at 20 seconds. All 173 visual assets resolve without importer warnings; 2,319 authored BGA events remain. Player preview reports background/layer RGBA ready. Evidence: `/tmp/t540-rotterdam-preview-middle.png`, `/tmp/t540-rotterdam-fixture.cpp`, `/tmp/t540-rotterdam-preview-app.log`. No user desktop or original chart was modified; the former Xvfb limitation is resolved.
- **Out of scope:** the storyboard kind.

## T-452 Resolve BMS image names across extensions

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** `rotterdam_7HYPER.bme` resolves all 173 `#BMP` entries: names declared as `.png` find the `.bmp` files. The BGA preview shows the expected images at three sampled measures.
- **Read first:** `src/sys/project/project_open.cpp:220-260` (the audio resolver), `tests/sys/project_open_tests.cpp`.
- **Files owned:** `src/sys/project/project_open.cpp`, `tests/sys/project_open_tests.cpp`, this card.
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

- **Status:** done (2026-10-08; table approved by the user with one amendment)
- **User decision (2026-10-08):** the table is approved except the Select row: "BGM notes should in fact be placeable! I need to be able to place them manually, with a note tool!" BGM placement with Draw is [T-482](draw-tool.md#t-482-draw-places-bgm-events-on-bgm-lanes). The Paste row's stale BGM hold tail is [T-483](#t-483-paste-bgm-holds-with-their-tail-moved).
- **Outcome:** a table on this card lists every editing verb, with its BGM path, its play-lane path, the behavior difference, and a proposed single semantic. The verbs: select, move by drag, Y-lock, lane key, Shift+lane key, move to BGM, delete, copy, paste, quantize, layer assign, select same sound, and audition.
- **Read first:** `src/sys/compose_input.cpp`, `src/sys/compose/actions/compose_note_to_bgm.cpp`, `src/core/note_to_bgm.cpp`, `src/core/interaction.cpp`, `src/ui/nuklear/compose_object_input.cpp`.
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
- **Audit (2026-10-08):** paths are from opened files (`sys/` = `src/sys/`, `core/` = `src/core/`, `ui/` = `src/ui/nuklear/`). "none" means the grep found no such path.

| Verb | BGM path | Play-lane path | Difference | Intended? | Proposed semantic |
|---|---|---|---|---|---|
| Select (click) | sys/compose_input.cpp:2380 select_compose_bgm_event, then core/interaction.cpp:4387 select_editor_objects | sys/compose_input.cpp:2893 select_compose_pointer_target, same core action | Note click can place a note in place mode (2919); BGM never places | Intended (pointer tool) | One select_editor_objects for any object kind; placement stays a separate tool |
| Move by drag | sys/compose_input.cpp:804 begin_bmson_bgm_move, 913 update; ui/compose_object_input.cpp:386-421 (needs exactly one selected BGM); include/wormedit/core/resource_assignment.h:267 apply_bgm_event_move | sys/compose_input.cpp:1801 note move begin; core/interaction.cpp:2526 begin_note_move_gesture, 2570, 2606 | BGM: one object, separate gesture kinds; notes: multi-select, one gesture | Stem markers intended; single-BGM limit unclear, likely accidental | One move gesture over all selected objects; stem rules inside the command |
| Y-lock | sys/compose_input.cpp:950-952 preview target = origin (skipped for virtual canvas rows) | core/interaction.cpp:5075-5076 plan_note_move(keep_rows = state.y_lock), param at 1683, used at 1353 | Different mechanisms: BGM clamps preview, notes pass keep_rows to planner | Partly: linked stem markers stay in lane (intended) | One keep_rows flag applied to the whole mixed plan |
| Lane key | sys/compose_input.cpp:3631 BGM branch; handled only if BGM ids exist (sys/compose/actions/compose_note_to_bgm.cpp:110-112) | sys/compose_input.cpp:708-722 dispatch_compose_lane_key_press, then core/interaction.cpp:2332 assign_selected_notes_to_lane | Routing picks path by BGM presence; mixed selection goes to BGM path (core/note_to_bgm.cpp:554) | Accidental (routing) | Every lane key calls move_selected_objects_to_lane |
| Shift+lane key | sys/compose/actions/compose_note_to_bgm.cpp:113-126, first object via 53-74 over notes and BGM; deselect 135-140 | sys/compose_input.cpp:714-715 successive flag; core/interaction.cpp:2339-2390; deselect 2387-2390 | Same "first in document order, deselect" rule, two implementations | Intended (T-454 rule); split implementation accidental | Core: first object of mixed selection in document order, move one, deselect |
| Move to BGM | none (grep NOTE_MOVE_TO_BGM hits only compose_input.cpp:3535, 3649 and compose_object_input.cpp:778) | sys/compose_input.cpp:3649-3652 to sys/compose/actions/compose_note_to_bgm.cpp:78, core/note_to_bgm.cpp:401 move_notes_to_bgm | No BGM side; BGM events in a mixed selection are ignored | Intended (note-only verb) | Keep note-only; mixed selection moves notes and leaves BGM alone |
| Delete | sys/compose_input.cpp:684-687 to core/interaction.cpp:2127 delete_editor_selection; BGM rules 2073-2105 (linked BMSON marker must be alone) | sys/compose_input.cpp:3628-3629 stem taps move to BGM instead; else same core delete (2061) | Stem Delete becomes move-to-BGM; linked marker blocks a mixed delete | Intended (BMSON stem rules); branch-owned BMS error unverified | One delete command; stem and linked rules as named guards inside it |
| Copy | src/sys/commands/application_command_dispatch.cpp:282 to core/chart_clipboard.cpp:166 copy_chart_selection | same path | None (collect at 157-159 handles both kinds) | n/a | Keep single |
| Paste | sys/compose_input.cpp:3736-3739 to sys/compose_view.cpp:165-178 to core/chart_clipboard.cpp:295 place_object | same path | BGM paste keeps stored end (chart_clipboard.cpp:310-315, no rebase); hold copy unverified | Accidental (unverified) | Single paste; rebase or reject BGM holds explicitly |
| Quantize | none: sys/compose/actions/compose_area_actions.cpp:82-83 rejects BGM-only selection | sys/compose/actions/compose_area_actions.cpp:99-102 objects = selected_notes to core/command.cpp:115 | BGM never quantized, even in a mixed selection | Accidental (unverified) | One quantize over notes and BGM, or document notes-only |
| Layer assign | sys/compose/compose_semantic_layer_commands.h:38 selected_compose_bgm_events, edit with notes and BGM | sys/compose/actions/compose_actions.cpp:2087 apply_compose_layer_assignment (same call) | None: one command covers both | Intended | Keep one semantic-layer edit over notes and BGM |
| Select same sound | none (grep "same sound", "select_same" found nothing). Closest: sys/compose/actions/compose_actions.cpp:1186 select_resource_uses | same function: sys/compose_resource_presenter.cpp:102-113 (notes and BGM) | Keyed by resource, not sound identity; uncertain | Uncertain | Select all objects using one resource, both kinds, one replace-select |
| Audition | sys/compose_input.cpp:2395 (audition_on_click) to core/interaction.cpp:4381-4440 | sys/compose_input.cpp:2927 to same core; consumed at 3986 | None (BMSON draft audition at 1198 is separate) | n/a | Keep core rule: one note or BGM event |

Uncertain: "select same sound" (no sound-identity path found); Y-lock planner body (only the parameter read); BGM hold paste (no rebase found, not run); branch-owned BMS delete error (not traced to a fix).

## T-454 Shift+lane key on BGM events moves one and deselects it

- **Status:** done (2026-10-07; root integrated and verified)
- **Result:** Shift moves and deselects one selected BGM/note in deterministic document order; plain mixed assignment commits atomically with one undo. Collision preserves selection. Core, real event harness and production dispatcher coverage pass in the integrated 2,417-test suite; `/tmp/wormedit-oct7-root/editing-integrated-tests.log`.
- **Outcome:** with three BGM events selected, Shift+lane key moves the first one in document order into the lane and deselects it, leaving the other two selected, as for play notes. A mixed selection of notes and BGM events follows the same order. Each press is one undo step.
- **Read first:** `src/sys/compose_input.cpp` (`WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE` / `LANE_KEY_PRESS` branch), `src/sys/compose/actions/compose_note_to_bgm.cpp` (`move_selected_bgm_to_lane`), `src/core/interaction.cpp` (`assign_selected_notes_to_lane`), [T-422](bms-editing.md).
- **Files owned:** `src/sys/compose_input.cpp`, `src/sys/compose/actions/compose_note_to_bgm.cpp`, `src/sys/compose/actions/compose_note_to_bgm.h`, `include/wormedit/core/note_to_bgm.h` and `src/core/note_to_bgm.cpp` (coordinator-approved targeted atomic mixed-selection lane command; no general verb refactor), `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/core/note_to_bgm_tests.cpp` (coordinator-approved helper semantics coverage), this card.
- **Steps:**
  1. Pass `input.successive` (the Shift state) to `move_selected_bgm_to_lane`.
  2. When it is set, take the first selected object in document order, whether note or BGM, move only that one, and remove it from the selection. Leave the moved object deselected.
  3. Test the real helper for mixed-selection document order, one-command presses, undo, collision preservation, and the plain whole-selection path; add an event-harness case asserting the production Shift+lane key emits `successive`.
- **Invariants:** a plain lane key still moves the whole selection.
- **Reject/failure behavior:** a collision leaves the event selected and shows the reason.
- **Verify:** `ctest --test-dir build -L event-harness`.
- **Out of scope:** the general refactor (T-455).

## T-455 One core entry point per editing verb for mixed selections

- **Status:** done (2026-10-09; all 13 approved T-453 verbs audited)
- **Done so far (2026-10-09):** lane assignment uses one core action for notes, BGM events and mixed selections. Core owns document order, successive deselection, audition, active lane and conversion selection; the UI only distinguishes assignment from empty-selection lane placement. Large note selections remain supported. Normal application build and full CTest pass: `100% tests passed out of 2957`, `Total Test time (real) = 57.91 sec` (`/tmp/wormedit-lane-verified-suite.log`). Configured clang-tidy has no new complexity violation; existing test-macro warnings remain.
- **Done so far (drag, 2026-10-09):** T-55 mixed dragging/Y-lock is complete: either selected object starts one atomic note/BGM proposal, preserving tails and stem links; full suite 2968/2968 passes.
- **Done so far (single-BGM move, 2026-10-09):** ordinary and linked BGM drags now use the same core begin/update/release/cancel gesture as notes and mixed selections. Core owns source-lane choice, collision lane allocation, stem links, Y-lock and virtual BMS materialization. Shared scene ghosts show actual destinations and translated tails without changing document/history; release commits one Undo. The application-owned marker preview and direct commit route are removed. Renderer validation now accepts BGM-only move gestures instead of blanking the scene. Mixed moves retain their authored-row and source-lane rules.
- **Verification:** normal application build passes. Full CTest output: `100% tests passed out of 2991`, `Total Test time (real) = 189.55 sec`; includes 276 event-harness cases (`/tmp/wormedit-bgm-unify-suite.log`). Focused core: `All tests passed (111 assertions in 6 test cases)`; dispatcher and real-pointer checks cover horizontal/virtual destinations, Y-lock, cancellation and stale releases. Production scene composition checks virtual ghosts with tails before materialization. Configured clang-tidy ran every affected C++ file with no new complexity violations (`/tmp/wormedit-bgm-unify-final-tidy.log`, `/tmp/wormedit-bgm-unify-renderer-tidy.log`).
- **Done so far (Delete, 2026-10-09):** Delete has one core entry for notes, BGM and mixed selections. Linked playable stem points convert to BGM inside the same staged command as ordinary removal; missing selected objects, linked-marker mixed guards and branch-owned guards reject without partial conversion or selection changes. Cursor stem deletion preserves its source too. Core Delete: 244 assertions/22 cases; production mixed stem dispatcher: 10 assertions/1 case; all 276 event cases pass. Normal application build and configured complexity checks pass (`/tmp/t455-delete-*.log`).
- **Done so far (Quantize, 2026-10-09):** preview, report and Apply use one typed note/BGM request, including BGM-only selections and BGM hold endpoints. Core owns family filtering, missing-object/collision/branch guards and retained BMSON point synchronization; Apply stages one validated command and checks changed stem audio slices. BGM reports identify their family and shared ghosts project into the BGM pane. Normal application build and all 277 event-harness cases pass; core quantize 101 assertions/12 cases, preview 219 assertions/37 cases, mixed regressions 41 assertions/5 cases, production projection/Apply 16 assertions/1 case. Configured complexity checks have no new violations (`/tmp/t455-quantize-*.log`).
- **Done so far (menu Delete, 2026-10-09):** the Edit-menu dispatcher now calls the same core Delete directly, with no stem conversion prepass. Production menu and pointer tests pass together (28 assertions/2 cases), including one Undo on success and byte-identical document/selection on linked-marker rejection. Normal application build passes (`/tmp/t455-menu-delete-*.log`).
- **Done so far (Move to BGM, 2026-10-09):** `move_editor_selection_to_bgm` owns canonical selection, document ordering, successive deselection and converted selection. Existing selected BGM events remain unchanged, as T-453 approved. Missing selected notes, stale requests and active gestures reject without partial edits. The application-owned selector/order/filter implementation and obsolete Delete option are removed. Core conversion tests pass 284 assertions/29 cases; application command checks pass 12 assertions/1 case; normal application build and configured complexity checks pass (`/tmp/t455-move-*.log`).
- **Closure verification (2026-10-09):** all 13 approved verbs use the shared boundaries documented below, with retained differences owned by core. Normal application build passes. Final full CTest: `100% tests passed out of 3004`, `Total Test time (real) = 179.31 sec`, including all 277 event-harness cases (`/tmp/t455-closure-suite.log`). Configured clang-tidy ran every affected C++ file with no new or increased complexity violations; existing legacy/test-macro warnings remain. Quantize follow-ups cover horizontally clipped/offscreen BGM ghosts (22 production assertions) and branch ownership identified only by retained BMS source provenance (core quantize now 106 assertions/13 cases). Quantize commits: `8968043`, `1cf17a5`, `1487d59`; Edit-menu Delete: `bf467c8`. Move-to-BGM cleanup and this closure audit land together.
- **Outcome:** each verb approved in T-453 has one core command that takes notes and BGM events together. `compose_input.cpp` no longer branches on "BGM selected" before choosing a verb, and every approved difference is a documented rule inside the command.
- **Read first:** T-453's approved table, and the files it cites.
- **Scope extension (2026-10-09):** the user approved the remaining single-BGM horizontal/virtual move slice. Core gesture targets carry the virtual row and source lane; the renderer projects the shared preview, replacing the application-owned marker preview. Supporting owners: `src/core/editor_scene.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.{cpp,h}`, `tests/sys/compose_notification_tests.cpp`, and `tests/ui/bga_lane_event_harness_tests.cpp`.
- **Scope extension (completion, 2026-10-09):** finish the approved verb set. Supporting owners include `src/sys/compose/actions/compose_area_actions.cpp`, `src/core/quantize.cpp`, `include/wormedit/core/document.h`, `src/sys/compose_quantize_preview.{cpp,h}`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/quantize_tests.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/compose_measure_commands_tests.cpp`, `src/core/command.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, and `tests/ui/quantize_report_event_harness_tests.cpp` for typed mixed quantize, retained stem policy, and production dispatch evidence. The application-menu audit also owns `src/sys/commands/application_command_dispatch.cpp` and `tests/sys/application_commands_tests.cpp`; quantize notification coverage also owns `tests/sys/compose_action_notification_tests.cpp`. Each completed verb lands separately before the final capability audit.
- **Files owned:** lane assignment: `include/wormedit/core/{interaction,note_to_bgm}.h`, `src/core/{interaction,note_to_bgm}.cpp`, `src/sys/compose_{input,note_to_bgm}.{cpp,h}`, `tests/core/{interaction,note_to_bgm}_tests.cpp`, `tests/ui/{bga_lane,nuklear}_event_harness_tests.cpp`, this card and `memory/chart-editor-status/resume.md`. Move follow-up owners: `src/core/editor_scene.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_view.{cpp,h}`, `tests/sys/compose_notification_tests.cpp`, and `tests/ui/bga_lane_event_harness_tests.cpp`; the T-55 mixed dragging/Y-lock supporting owners remain recorded on that card.
- **Steps:**
  1. Do one verb per commit, in the order T-453 approves, starting with lane assignment (it absorbs T-454).
  2. Each commit removes the duplicate path and moves its tests to the shared command.
  3. After each verb, the full event harness passes.
- **Invariants:** BMSON stem rules (continue/restart points, slice-point moves) behave as before.
- **Reject/failure behavior:** a mixed command rejects atomically.
- **Verify:** `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on the touched files.
- **Out of scope:** new verbs.

### T-455 final approved-verb audit (2026-10-09)

All entries were traced from the production dispatcher to the named core boundary. BGM selection no longer chooses a different editing implementation in `compose_input.cpp`. Tool-specific placement, source-channel drafting, and UI availability checks remain separate from selection verbs.

| Approved verb | Shared core boundary | Retained rule / regression evidence |
|---|---|---|
| Select | `select_editor_objects` | Typed note/BGM selection, replace/add/remove; core interaction and pointer event cases. Draw BGM placement is the separate T-482 tool. |
| Move by drag | `begin/update/release_note_move_gesture` | Mixed and single-BGM proposals share one staged move, cancel and stale-release handling; core and production move cases. |
| Y-lock | Same move plan with `state.y_lock` | Keeps rows for every selected object; single and mixed BGM tail/Y-lock cases. Linked stem markers retain their source lane. |
| Lane key | `assign_selected_objects_to_lane` → `move_selected_objects_to_lane` | Whole mixed selection; conversion, lane activity and collision guards are core-owned; mixed lane tests. |
| Shift+lane key | Same action with `successive` | First object in document order, move and deselect once, one audition; successive mixed lane tests. |
| Move to BGM | `move_editor_selection_to_bgm` → `move_notes_to_bgm` | Intentionally note-only: selected BGM stays untouched. Core owns ordinary/group allocation, linked stem reassignment, ordering and selection; selection-to-BGM regressions. |
| Delete | `delete_editor_selection` | Pointer and Edit-menu dispatch directly. Stem taps return to BGM; selected linked markers must be alone; branch-owned BGM requires source-aware editing. Mixed guards reject before any conversion; core and menu mixed-delete cases. |
| Copy | `copy_chart_selection` | One typed clipboard includes notes and BGM, including retained restart/continue points; clipboard cases. |
| Paste | `paste_chart_clipboard` / `place_object` | One staged paste translates heads and tails; linked pulse collision/restart rules remain in core. T-483 tail, overflow and Undo cases. |
| Quantize | `apply_quantize_command` → `quantize_chart_exact` | One family-scoped note/BGM request for preview, report and Apply; BGM holds follow the same endpoint setting. Core owns collisions, source points and branch guards; mixed quantize, preview, production and inspector cases. |
| Layer assign | `apply_semantic_layer_edit` | One edit over notes/BGM, retaining the BGM single-layer rule and atomic locked-layer rejection; mixed semantic-layer cases. |
| Select same sound | `select_same_sound` → `select_editor_objects` | Resource/asset or retained-channel identity across both families, with visible/layer filters; sound-selection core and application cases. No history. |
| Audition | `select_editor_objects` emits `editor_object_audition_request_t` | Exactly one note or BGM target, one request, no history. Staged BMSON region audition is intentionally separate; interaction/audition and application cases. |

Mixed drag keeps its existing authored-row/source-lane rules; single ordinary BGM supports virtual-row materialization and collision lane allocation. These are documented inside the shared core move path. No new verbs were added.

## T-483 Paste BGM holds with their tail moved

- **Status:** done
- **Result (2026-10-08):** BGM holds record their tail length as the clipboard duration (same as notes) and rebuild `end` on paste from the new head row, rejecting the whole paste when the tail is unrepresentable. Plain BGM stays duration 0 with no end. Clipboard tests: 16 cases, 113 assertions pass; `clipboard|^core\.|^documentation\.` 1664/1664 pass.
- **Outcome:** copying and pasting a BGM event that has a native hold tail (`bgm_event_t::end`, added by T-423) pastes it with the tail moved by the same offset as the head, so its length is unchanged.
- **Found by:** the T-453 audit (2026-10-08). `append_bgm` in `src/core/chart_clipboard.cpp` records a zero duration and keeps the payload's absolute `end`. `place_object` moves `position` but not `end`, so the pasted tail points at the original location.
- **Read first:** `src/core/chart_clipboard.cpp` (`append_bgm`, `place_object`, the note duration path in `place_note`), `include/wormedit/core/document.h` (`bgm_event_t`).
- **Files owned:** `src/core/chart_clipboard.cpp`, `tests/core/chart_clipboard_tests.cpp`, this card.
- **Steps:**
  1. Record a BGM hold's length as the clipboard duration, the same way notes record theirs.
  2. On paste, rebuild `end` from the new head row plus that duration, using the note path's overflow and measure checks.
  3. Tests: copy and paste a BGM hold one measure later and check that the tail moves with it; paste across a measure-length change; a paste that would overflow rejects; plain BGM events are unchanged.
- **Invariants:** non-hold BGM paste is byte-identical. Undo restores exactly.
- **Reject/failure behavior:** an unrepresentable tail rejects the whole paste atomically.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'clipboard|^core\.'`.
- **Out of scope:** BMSON linked holds, which follow the T-423 linked rules. Coordinate with the active T-423 batch before editing if it still owns linked hold code.

