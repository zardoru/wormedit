# Task cards: editing and Sounds UX follow-ups

[Back to the task-card index](../tasks.md).

User requests (2026-10-04). Sounds tab means the dedicated Sounds screen (`audio_panel.cpp`); Sounds/BGA dock means the Compose resource listing. Apply the stale-plan check before implementing each card. These cards record planned behavior, not completed work.

## T-361 Advance sounds in the active automatic or slots order

- **Status:** done (already implemented: src/ui/nuklear/compose_sounds_panel.cpp:314; src/sys/workspace_projection.cpp:747)
- **Outcome:** advance-after-place in the Sounds dock follows automatic mode's displayed resource order or slots mode's ascending BMS WAV ID order.
- **Read first:** `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/keysound_list_state.h`, `src/sys/sound_list_projection.cpp`.
- **Files owned:** `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/compose_state.h`, `src/sys/sound_list_projection.cpp/.h`, `src/sys/workspace_projection.cpp`, `tests/ui/keysound_list_state_tests.cpp`, `tests/sys/sound_list_projection_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Steps:**
  1. Audit `keysound_advance_rows` and T-346/T-358 first; use the active mode's displayed ordering for the next eligible sound, preserving current search/filter semantics.
  2. Switching mode rebuilds the advance order and reveals the resulting selection; skip empty, missing and nonplaceable slots, and preserve the existing end-of-list policy.
  3. Test names whose natural order differs from their WAV IDs, mode changes, filtered lists, empty slots and advancing beyond the visible window after actual placement.
- **Invariants:** Advancement changes only the armed resource and selection, never IDs or chart objects beyond the requested placement.
- **Reject/failure behavior:** A stale/deleted resource is not armed; no eligible successor leaves the last valid selection intact.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** automatic allocation or renumbering; BGA advancement (T-367).

## T-362 Add visual separators to the Sounds tab

- **Status:** done (2026-10-04; shared section and column separators)
- **Outcome:** the Sounds tab separates related toolbar actions and the list, waveform/slicer and properties sections using the shared chrome separators.
- **Read first:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/chrome.h`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/chrome.h`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Steps:**
  1. Identify the current toolbar and section boundaries in `audio_panel.cpp`; group existing controls by purpose without changing their actions.
  2. Apply shared toolbar/layout separators with consistent spacing; preserve the list and waveform space at narrow window sizes.
  3. Inspect isolated Xvfb captures at normal and narrow widths and exercise controls adjacent to separators through the event harness.
- **Invariants:** Layout changes do not alter document data, selection or tool state.
- **Reject/failure behavior:** Narrow layouts keep controls reachable and avoid overlapping hit regions.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** new Sounds actions; sound-group headers (T-357).

- **Verification (2026-10-04):** `cmake --build build` passed; event harness: `100% tests passed out of 65`; required core/frontend/frame/documentation filter: `100% tests passed out of 1613`; full suite: `100% tests passed out of 1914`. Affected sources checked with `clang-tidy -p build`, with no new cognitive-complexity violations. Isolated Xvfb captures at 1,280 and 800 pixels were inspected; row selection and search pass the input harness. Existing list, waveform and properties actions stay reachable.

## T-363 Copy sound sources into the project from the Sounds tab

- **Status:** done (2026-10-04; Sounds source copy through the existing project-copy path)
- **Outcome:** the Sounds tab offers Copy source into project for selected sounds, using the existing project-copy flow and relinking their sources only after successful copies.
- **Read first:** `src/ui/nuklear/audio_panel.cpp`, `include/wormedit/core/resource_file_copy.h`, `src/sys/audio/audio_project_import.cpp`, `src/ui/nuklear/metadata_audio_section.cpp`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/audio_state.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/app/main.cpp`, `src/sys/audio_project_import.cpp/.h`, `src/core/resource_file_copy.cpp`, `include/wormedit/core/resource_file_copy.h`, `tests/core/resource_file_copy_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, new `tests/ui/sound_source_copy_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `src/sys/compose/actions/compose_actions.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** New `tests/ui/sound_source_copy_event_harness_tests.cpp` and `tests/CMakeLists.txt` isolate the Sounds copy workflow tests. `src/sys/compose/actions/compose_actions.cpp` lets Audio/Slices use the existing revision-checked asset-copy path; its shared application edits belong to the coordinator.
- **Steps:**
  1. Reuse the existing project-copy policy for destination paths, filename collisions, shared assets and unsaved projects; expose the action for the current Sounds selection.
  2. Wire a revision-checked request through the application, preserving sample identity and references while copying and relinking source assets.
  3. Test shared sources, existing destination files, missing/unreadable files, cancellation and unsaved projects; inspect actual copied files and saved project paths in isolated Xvfb.
- **Invariants:** Resource IDs, note links and audio content remain unchanged; existing files are never silently overwritten.
- **Reject/failure behavior:** A failed/cancelled copy does not relink the affected source or report success; errors identify the source and destination.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** export-copy destinations; moving/deleting original files.

- **Verification (2026-10-04):** `cmake --build build` passed. Core/frame gates: 1,455/1,455; frontend/documentation gates: 223/223; event harness: 86/86; complete application suite: 1,978/1,978. Affected-source clang-tidy found no new cognitive-complexity violations. The event harness checks selected-asset/revision payloads and missing-source suppression; the shared copy tests cover reuse, collisions and failure without relinking. In isolated Xvfb the Sounds action copied the selected external WAV into `assets/18-lane.wav`; the saved relative path persisted and all resource IDs/chart links stayed unchanged. The 44,144-byte copy matches its original byte-for-byte. Captures and saved fixture: `/tmp/wormedit-slots-bga-batch/`; full log: `/tmp/wormedit-batch-full-passed.log`.

## T-364 Find sound uses from the Sounds tab

- **Status:** done (2026-10-04)
- **Outcome:** Find uses in the Sounds tab lists references to the selected sound with chart and position, and activates/navigates to a chosen reference.
- **Read first:** `src/ui/nuklear/audio_panel.cpp`, `src/sys/sound_list_projection.cpp`, `include/wormedit/core/document.h`, `src/sys/compose_bookmark_commands.cpp`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/audio_state.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/app/main.cpp`, `src/sys/workspace_projection.cpp/.h`, `src/sys/application_frame_projection.cpp`, `src/sys/compose/actions/compose_actions.cpp`, new `src/sys/sound_usage_projection.cpp/.h`, new `src/sys/sound_usage_actions.cpp/.h`, new `tests/sys/sound_usage_projection_tests.cpp`, new `tests/sys/sound_usage_actions_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, new `tests/ui/sound_usage_event_harness_tests.cpp`, `tests/sys/frame_projection_arena_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Supporting scope (coordinator approved, 2026-10-04):** `src/sys/application_frame_projection.cpp` passes retained usage requests into the independent projection; `tests/sys/frame_projection_arena_tests.cpp` covers arena-owned result labels. Existing Sounds separator harness checks now inspect filtered list rows separately from the retained selected sound in Properties and apply emitted selection to the fixture view. Dedicated `tests/ui/sound_usage_event_harness_tests.cpp` verifies precise resource selection and usage activation without overlapping existing layer/group harness edits. New `src/sys/sound_usage_actions.cpp/.h` isolates the navigation boundary for `tests/sys/sound_usage_actions_tests.cpp` to prove stale/deleted rejection and source-only navigation without a cursor move. Audio contracts carry full stable identity and revision; positioned uses navigate to Compose, while source-only uses select their chart/channel/clip in Sounds without inventing a cursor position.
- **Steps:**
  1. Add a deterministic usage projection for note and BGM resource references across charts, including supported sound-channel/asset references and shared-asset distinctions.
  2. Expose Find uses for a selected sound with a bounded result list showing chart, position and reference kind; route activation through normal chart selection and Compose navigation.
  3. Test unused sounds, multiple charts, multiple references at one position, missing resources and stale results; verify an actual result jump in the event harness.
- **Invariants:** Searching and jumping never mutate resources or chart events; results distinguish resource references from shared-asset uses.
- **Reject/failure behavior:** Deleted/stale results refresh or report unavailable instead of jumping to an unrelated object.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Verification evidence:** Full application suite 2,025/2,025; event harness 97/97; required core/frontend/frame/documentation gates 1,723/1,723. Projection and dispatch regressions cover exact resource/shared-asset identity, bounded ordering, stale/deleted results, unchanged document addresses and source-only cursor preservation. Isolated final results, source selection, list scrolling and exact row-600 jump captures are at `/tmp/wormedit-bgm-hover-groups-batch/usage-final-*.png`. Affected new functions pass cognitive-complexity checks.
- **Out of scope:** replacing usages; BGA usage search.

## T-365 Remove the inspector sample list

- **Status:** done (2026-10-04; sample selection through the Sounds dock)
- **Outcome:** the Compose inspector no longer duplicates the sample picker; the Sounds dock remains the sample selection and placement entry point.
- **Read first:** `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`.
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_resources.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, new `tests/ui/inspector_resource_workflow_tests.cpp`, `tests/CMakeLists.txt`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `tests/ui/nuklear_event_harness.cpp/.h`.
- **Supporting scope (coordinator approved, 2026-10-04):** New `tests/ui/inspector_resource_workflow_tests.cpp` adds isolated ownership of actual input-harness workflows; `tests/CMakeLists.txt` registers it. `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp` and `tests/ui/nuklear_event_harness.cpp/.h` expose rendered text bounds to the no-window harness so resource controls can be clicked at their actual positions.
- **Steps:**
  1. Audit the inspector's resource section and separate sample-list controls from other resource/placement functions before removing the sample list.
  2. Remove duplicate sample rows and sample-only controls; preserve any still-needed BGM/BGA controls and make the Sounds dock readily reachable.
  3. Verify sound arming, digit placement, advance-after-place and non-sample workflows through the event harness and an isolated inspector capture.
- **Invariants:** Removing the list does not clear resource assignments or disable existing placement commands.
- **Reject/failure behavior:** Missing or closed dock state cannot strand the user without an accessible sample picker.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** removing the resource catalog; redesigning the full inspector.

- **Verification (2026-10-04):** `cmake --build build` passed; `ctest --test-dir build -L event-harness --output-on-failure`: `100% tests passed out of 74`; required core/frontend/frame/documentation filter: `100% tests passed out of 1635`; full suite: `100% tests passed out of 1935`. Affected sources checked with `clang-tidy -p build`, with no new cognitive-complexity violations. The event harness verifies sample arming/painting, generic Arm/Disarm provenance, preserved BGM/BGA controls, compact access and toolbar close/reopen. Isolated normal and 700-pixel Compose captures confirm the Sounds dock remains reachable without duplicate inspector sample rows. Captures are under `/tmp/wormedit-import-bga-batch/`; the final real-window smoke command passed and its PNG was inspected.

## T-366 Add standard BMS 5K without an auxiliary lane

- **Status:** done (2026-10-04; standard six-lane BMS 5K profile)
- **Outcome:** the BMS profile picker and import/export support five keys plus scratch without a Free Zone or Foot Pedal lane.
- **Read first:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `src/core/bms_profile_detection.cpp`, `src/formats/bms.cpp`, `tests/fixtures/bms/profiles/README.md`.
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `src/core/bms_profile_detection.cpp`, `src/core/lane_profile_switch.cpp`, `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `src/ui/nuklear/setup_panel.cpp`, `tests/core/lane_profile_tests.cpp`, `tests/core/bms_profile_detection_tests.cpp`, `tests/core/lane_profile_switch_tests.cpp`, `tests/formats/bms_writer_tests.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/fixture_validation_tests.cpp`, new `tests/fixtures/bms/profiles/bms-5k-standard.bms`, `tests/fixtures/bms/profiles/README.md`.
- **Supporting scope (coordinator approved, 2026-10-04):** `tests/formats/bms_tests.cpp` updates the exhaustive catalog fixture inventory and automatic 5K import expectations for the new standard profile.
- **Steps:**
  1. Add a distinct standard 5K profile with five key channels and scratch, no auxiliary lane, and the supported scratch placements; retain explicit free-zone and pedal variants.
  2. Expose it in profile selection and conservative BMS detection/export; preserve or diagnose auxiliary-channel content when switching rather than dropping notes.
  3. Add fixture round-trip, profile-switch and keyboard-mapping tests; verify a real six-lane field without auxiliary lanes.
- **Invariants:** Existing profile IDs and free-zone/pedal layouts stay compatible; imported unsupported channels are never silently discarded.
- **Reject/failure behavior:** An unsafe profile switch reports affected content and leaves the document unchanged.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** scratchless 5K; changing DP5/10K profiles.

- **Verification (2026-10-04):** `cmake --build build` passed; event harness: `100% tests passed out of 65`; required core/frontend/frame/documentation filter: `100% tests passed out of 1613`; full suite: `100% tests passed out of 1914`. Affected sources checked with `clang-tidy -p build`, with no new cognitive-complexity violations. The real isolated Compose field shows five keys plus scratch. Fixture import/export and unsafe auxiliary-lane switching pass; the saved native chart retains all six notes and lane bindings.

## T-367 Advance after placement in the BGA listing

- **Status:** done (2026-10-04; independent BGA advance after successful placement)
- **Outcome:** the BGA dock offers advance-after-place that arms the next eligible image/video resource after successful event placement.
- **Read first:** `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/sys/bga_list_projection.cpp`, `src/sys/compose/input/compose_input.cpp`.
- **Files owned:** `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/sys/bga_list_projection.cpp/.h`, `src/sys/bga_dock_runtime.cpp/.h`, `tests/sys/bga_list_projection_tests.cpp`, `tests/ui/keysound_list_state_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, new `tests/ui/bga_advance_workflow_tests.cpp`, `tests/CMakeLists.txt`.
- **Supporting scope (coordinator approved, 2026-10-04):** `src/ui/nuklear/compose_state.cpp` and `tests/ui/nuklear_compose_state_tests.cpp` are owned for the shared successful-placement advance callback and its separate BGA state regression coverage. `src/ui/nuklear/compose_resources.cpp` and `src/ui/nuklear/compose_sounds_panel.cpp` clear BGA advance provenance when another resource picker arms or disarms; their current T-365/T-355 owners incorporate these supporting changes to avoid concurrent edits. New `tests/ui/bga_advance_workflow_tests.cpp` adds actual input-harness coverage; `tests/CMakeLists.txt` registers it.
- **Steps:**
  1. Reuse the successful-placement advance mechanism from the Sounds dock, keeping separate BGA toggle and selection state.
  2. Advance in the BGA listing's current sort/filter order, skip unusable resources and reveal the successor beyond the visible window.
  3. Test successful placement, rejected/cancelled placement, end of list, missing resources, search and name/ID order through the event harness.
- **Invariants:** Only successful new placement advances; the BGA resource catalog and existing BMP IDs remain unchanged.
- **Reject/failure behavior:** No eligible successor retains the last valid armed resource; stale rows are not armed.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** bulk ID allocation; BGA video decoding; sound advancement (T-361).

- **Verification (2026-10-04):** `cmake --build build` passed; `ctest --test-dir build -L event-harness --output-on-failure`: `100% tests passed out of 74`; required core/frontend/frame/documentation filter: `100% tests passed out of 1635`; full suite: `100% tests passed out of 1935`. Affected sources checked with `clang-tidy -p build`, with no new cognitive-complexity violations. The event harness verifies sort/search requests, actual pointer placement, no advancement before successful notification, off-window successors and end-of-order stop; projection/state tests cover missing/nonvisual assets, stale callbacks and natural/BMP order. Actual isolated saved events used resources 25, 21 and 23 in natural-name order, retaining the final selection and turning placement/advance off. Original BMP identities and playable measures stayed unchanged. Captures are under `/tmp/wormedit-import-bga-batch/`; the final real-window smoke command passed and its PNG was inspected.

## T-368 Center the minimap span when zoomed in

- **Status:** done (2026-10-04; centered minimap with stable drag mapping)
- **Outcome:** a zoomed minimap centers its displayed range on the current field's visible span, clamping at chart boundaries.
- **Read first:** `src/sys/compose/input/compose_minimap_navigation.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/sys/compose/views/compose_view.cpp`.
- **Files owned:** `src/sys/compose_minimap_navigation.cpp/.h`, `src/sys/compose_view.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/ui/frontend.h`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/sys/waveform_view_projection.cpp/.h`, `src/sys/compose_bgm_bar.cpp/.h`, new `tests/ui/minimap_center_event_harness_tests.cpp`, new `tests/sys/minimap_center_tests.cpp`, `tests/CMakeLists.txt`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_viewport_input.cpp/.h`, `src/ui/nuklear/compose_panel_internal.h`, `src/sys/application_frame_projection.cpp`, `tests/sys/compose_bgm_bar_tests.cpp`, `src/sys/app/main.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** Waveform projection supplies the current visible source-frame span; shared BGM bar mapping handles zoomed-range clamping. Dedicated tests avoid shared test-file overlap with BGA relocation; existing BGM bar dispatch tests follow the centered zoom contract. The unconditional frontend-frame handoff in `main.cpp` retains/clears a context-stamped capture so waveform rows and marks use the same fixed range through release/cancel. Application-frame waveform reconciliation retains and clears explicit pan overrides when the visible span or project/chart/asset context changes. Input dispatch rebases zoom to the latest projected range; the retained-state capture helpers and viewport input freeze the displayed range through a drag to prevent recentering feedback. Coordinator owns build registration and shared contract coordination. Center the zoomed range as the visible field span changes and on zoom/reset; drawing and navigation consume the same range, with boundary clamping. Explicit pan may override centering until the field span changes again; active navigation captures retain their initial mapping. Zoom and pan do not themselves move the field.
- **Steps:**
  1. Audit minimap range and viewport-span projection; calculate the zoomed range from the visible span midpoint with boundary clamping.
  2. Share the range mapping between drawing and click/drag navigation; preserve the viewed chart position when changing minimap zoom.
  3. Test middle/start/end positions, upscroll/downscroll, playback and zoom transitions; inspect isolated captures of a long chart.
- **Invariants:** Minimap zoom and centering never move events or introduce an unintended field scroll.
- **Reject/failure behavior:** Empty/short charts and degenerate spans use a finite clamped range.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** waveform zoom redesign; unrelated field zoom changes.

- **Verification evidence (2026-10-04):** Application build and full suite passed 2,045/2,045, including all 104 input-harness cases and the five minimap projection regressions. Tests cover both scroll directions, start/end clamping, playback/zoom, pan invalidation and stale drag stamps. The real isolated two-minute chart keeps the field position during paused zoom, follows playback, freezes the scale through an active drag, recenters on release, and preserves explicit pan until field scrolling. New helpers pass the complexity check; the updated old input test decreases complexity. Artifacts: `/tmp/wormedit-filters-bga-minimap-batch/`; suite log: `/tmp/wormedit-filters-full-suite-passed.log`.

## T-369 Wire Ctrl+S to the existing Save action

- **Status:** done (2026-10-04; platform Primary+S routes through existing Save)
- **Outcome:** Ctrl+S invokes the same project save action as File → Save, including the existing destination picker for an unsaved project.
- **Read first:** `src/core/command_registry.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/app/main.cpp`, `src/sys/project/project_save_paths.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/sys/app/main.cpp`, `src/sys/commands/platform_shortcut_profile.cpp`, `src/sys/application_commands.cpp/.h`, `src/sys/workspace_projection.cpp`, `tests/core/command_registry_tests.cpp`, `tests/sys/project_save_paths_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** `src/sys/application_commands.cpp/.h` are owned to route the registered Save command through the existing application action; `src/sys/workspace_projection.cpp` publishes the originating project ID on every tab.
- **Steps:**
  1. Audit shortcut registration and application routing for Save; bind Ctrl+S through the existing command/shortcut system, with platform Primary compatibility.
  2. Route it to the exact File → Save behavior and deduplicate held-key repeats or an already-open save dialog; honor modal/input ownership.
  3. Test named and unsaved projects, repeat, dialog cancellation and failed saves; verify a real saved revision after Ctrl+S in isolated Xvfb.
- **Invariants:** Saving retains the existing format/destination and dirty-state policy; no separate persistence path.
- **Reject/failure behavior:** Cancellation or write failure does not clear dirty state or report success.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** new Save As shortcut; autosave policy changes.

- **Verification (2026-10-04):** `cmake --build build` passed; event harness: `100% tests passed out of 65`; required core/frontend/frame/documentation filter: `100% tests passed out of 1613`; full suite: `100% tests passed out of 1914`. Affected sources checked with `clang-tidy -p build`, with no new cognitive-complexity violations. Real isolated Ctrl+S saved revision 2 from Compose and saved from Sounds. Unsaved projects open the existing picker and retain state on repeated cancellation. A failed write retained the dirty marker and left the destination byte-identical.

## T-370 Open Go To Bookmark with Ctrl+Shift+G

- **Status:** done (2026-10-04; bounded bookmark chooser and exact playback-preserving navigation)
- **Outcome:** Ctrl+Shift+G opens a bookmark chooser showing position and name; selecting an entry navigates to that bookmark.
- **Read first:** `src/sys/compose_bookmark_commands.cpp`, `src/core/command_registry.cpp`, `include/wormedit/core/marker_navigation.h`, `src/ui/nuklear/frontend.cpp`.
- **Files owned:** `src/sys/compose_bookmark_commands.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/workspace_projection.cpp/.h`, `src/core/command_registry.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.h`, `tests/core/command_registry_tests.cpp`, `tests/sys/compose_bookmark_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/sys/application_commands.cpp/.h`, new `src/sys/bookmark_chooser_projection.cpp/.h`, new `src/ui/nuklear/compose_bookmark_chooser.cpp/.h`, new `tests/sys/bookmark_chooser_projection_tests.cpp`, new `tests/ui/bookmark_chooser_event_harness_tests.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_panel_internal.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/sys/application_frame_projection.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/app/main.cpp`, `tests/sys/frame_projection_arena_tests.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** Dedicated bounded chooser projection and UI units keep bookmark IDs, formatted positions and navigation policy separate from frontend routing. Application command mapping and retained-state integration complete the registered shortcut. Coordinator owns shared frontend/state/action/projection shell edits and build registration. The application-frame request getter, application command callback and main wiring open the same retained chooser through the registered command; arena tests verify row text lifetime, and application command tests verify callback stamps and modal/unavailable rejection.
- **Steps:**
  1. Register the gesture and project a stable ordered list of bookmarks for the active chart with formatted position and name.
  2. Draw a bounded chooser with keyboard/mouse selection and Escape cancellation; reuse existing bookmark navigation for the chosen stable ID.
  3. Test unnamed/duplicate-name bookmarks, empty lists, stale entries, text/modal ownership and playback-preserving navigation.
- **Invariants:** Opening or cancelling the chooser never edits markers or moves the cursor; navigation preserves the existing playback policy.
- **Reject/failure behavior:** A missing bookmark refreshes the chooser or reports unavailable without jumping elsewhere.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** adding bookmarks; editing names (T-371); changing Ctrl+G behavior.

- **Verification evidence (2026-10-04):** Shared final gates for T-370/T-371/T-372: `cmake --build build -j4` ended with `[100%] Built target wormedit`; event harness ended with `100% tests passed out of 115` and `Total Test time (real) = 3.17 sec`; required core/frontend/frame/documentation partition ended with `100% tests passed out of 1769` and `Total Test time (real) = 107.47 sec`; full suite ended with `100% tests passed out of 2076` and `Total Test time (real) = 141.24 sec`. Logs: `/tmp/wormedit-bookmark-build12.log`, `/tmp/wormedit-bookmark-event-final.log`, `/tmp/wormedit-bookmark-partition-final.log`, `/tmp/wormedit-bookmark-full-final.log`. Affected-source `clang-tidy -p build` checks passed with no new complexity violations; increased export-popup complexity was removed by extracting lifecycle, choices and control helpers. Existing unrelated violations remain unchanged. Root final frontend/panel checks: `/tmp/wormedit-bookmark-frontend-final-tidy.log`, `/tmp/wormedit-bookmark-panel-final-tidy.log`; agent final menu checks: `/tmp/t372-tidy-final-*.log`.
- **Isolated visual evidence:** Actual Ctrl+Shift+G opened the chooser with duplicate-name and unnamed entries, positions and stable IDs. Selecting the audio marker sought exactly 1500 ms while playback remained active. Bounded pagination and stale/modal/empty-list behavior are covered by projection, command and event-harness tests. Artifacts: `/tmp/wormedit-bookmark-menu-batch/`.


## T-371 Rename a bookmark inline from its timeline line

- **Status:** done (2026-10-04; inline bookmark rename preserving exact anchors and one undo)
- **Outcome:** clicking a small target around a bookmark line/label opens an inline name editor, with Enter committing and Escape cancelling.
- **Read first:** `src/ui/nuklear/compose_bookmark_label.cpp`, `src/ui/nuklear/compose_panel.cpp`, `include/wormedit/core/marker_editor.h`, `src/sys/compose_bookmark_commands.cpp`.
- **Files owned:** `src/ui/nuklear/compose_bookmark_label.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/sys/compose_bookmark_commands.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/core/marker_editor.cpp`, `tests/core/marker_editor_tests.cpp`, `tests/ui/compose_bookmark_label_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `src/ui/nuklear/compose_canvas.cpp/.h`, `src/ui/nuklear/compose_input_panel.cpp`, new `src/ui/nuklear/compose_bookmark_rename.cpp/.h`, new `src/sys/bookmark_rename_commands.cpp/.h`, new `tests/sys/bookmark_rename_commands_tests.cpp`, new `tests/ui/bookmark_rename_event_harness_tests.cpp`, `src/ui/nuklear/compose_panel_internal.h`, `src/ui/nuklear/compose_state.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/ui/nuklear/sdl_backend.cpp`, `tests/ui/nuklear_sdl_backend_tests.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** The actual bookmark drawing and pointer priority live in the shared canvas/input units. Dedicated retained editor and revision-checked rename command units preserve exact beat/audio anchors and avoid overlap with chooser commands. Coordinator owns shared frontend/state/action dispatch and build registration. Actual text-input coverage exposed the SDL backend's one-glyph assumption; coordinator support decodes valid multi-codepoint SDL3 text events without aborting, with a UTF-8 backend regression.
- **Steps:**
  1. Add a bounded bookmark rename hit target around the rendered line/label, including unnamed markers; resolve closely spaced markers deterministically.
  2. Open a retained editor for the stable bookmark ID and expected revision; reuse marker editing to commit one rename on Enter, and cancel on Escape or blur.
  3. Test UTF-8 names, empty names according to existing marker rules, overlapping targets, scroll direction, stale/deleted markers and shortcut suppression while typing.
- **Invariants:** Rename preserves bookmark ID and exact position; unrelated chart data is unchanged; one accepted edit is one undo step.
- **Reject/failure behavior:** Cancelled, invalid or stale edits leave the original name intact and do not trigger a chart placement gesture.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** dragging bookmark positions; full marker manager.

- **Verification evidence (2026-10-04):** Shared final gates for T-370/T-371/T-372: `cmake --build build -j4` ended with `[100%] Built target wormedit`; event harness ended with `100% tests passed out of 115` and `Total Test time (real) = 3.17 sec`; required core/frontend/frame/documentation partition ended with `100% tests passed out of 1769` and `Total Test time (real) = 107.47 sec`; full suite ended with `100% tests passed out of 2076` and `Total Test time (real) = 141.24 sec`. Logs: `/tmp/wormedit-bookmark-build12.log`, `/tmp/wormedit-bookmark-event-final.log`, `/tmp/wormedit-bookmark-partition-final.log`, `/tmp/wormedit-bookmark-full-final.log`. Affected-source `clang-tidy -p build` checks passed with no new complexity violations; increased export-popup complexity was removed by extracting lifecycle, choices and control helpers. Existing unrelated violations remain unchanged. Root final frontend/panel checks: `/tmp/wormedit-bookmark-frontend-final-tidy.log`, `/tmp/wormedit-bookmark-panel-final-tidy.log`; agent final menu checks: `/tmp/t372-tidy-final-*.log`.
- **Isolated visual evidence:** Actual beat and audio renames saved documents identical to the original except the selected name and revision; one undo restored each name. The inline editor is beside the timeline line. UTF-8 typing, exact beat/audio anchor retention, cancellation and stale/deleted marker behavior pass command/backend/harness tests. Artifacts: `/tmp/wormedit-bookmark-menu-batch/`.


## T-372 Normalize popup menu behavior with the global menus

- **Status:** done (2026-10-04; shared menu dismissal and release ownership)
- **Outcome:** context and dropdown menus follow the same interaction conventions as the global File/Edit menus, including opening, item activation, outside-click dismissal and Escape.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/chrome.h`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `src/ui/nuklear/compose_recipe_menu.cpp`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/chrome.h`, `src/ui/nuklear/compose_note_menu.cpp/.h`, `src/ui/nuklear/compose_area_menu.cpp/.h`, `src/ui/nuklear/compose_recipe_menu.cpp/.h`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, new `src/ui/nuklear/popup_lifecycle.h`, new `tests/ui/popup_lifecycle_tests.cpp`, new `tests/ui/popup_menu_event_harness_tests.cpp`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, `tests/CMakeLists.txt`.
- **Supporting scope (coordinator approved, 2026-10-04):** A shared lifecycle helper supplies open/context/dismissal/release ownership to existing menus and dropdowns. Dedicated tests isolate this work from bookmark tests. Coordinator serializes frontend/state/panel/pointer shell integration with the bookmark cards; menu action policy stays local.
- **Supporting integration:** Coordinator includes `compose_object_input.cpp` to arm the dense BGA popup release guard at its opening transition; T-371 incorporates the same transition in its owned input panel. Existing Layers, Note and dense BGA fixtures must wait for the shared quiet-frame release guard before a fresh gesture or initial popup inspection. The T-372 agent owns these existing harness files during the regression-fix phase; coordinator retains the frontend shell.
- **Steps:**
  1. Inventory popup menus and record the global menu behavior for pointer activation, keyboard navigation, submenu switching, disabled items and dismissal as the reference.
  2. Extract shared menu helpers and migrate context/dropdown menus, retaining bounded scrolling, focus ownership and release guards against click-through.
  3. Add event-harness workflows for open/activate/dismiss/switch, nested or inline submenu fallback, disabled items, Escape and parent-window movement; inspect representative menus in isolated Xvfb.
- **Invariants:** Opening/dismissing a menu never edits a document; each chosen action fires once; background chart gestures remain blocked while the menu owns input.
- **Reject/failure behavior:** Dismissal releases retained input locks without replaying the click/key to the field.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness --output-on-failure`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^frame-timing\.|^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb check of the card's workflow.
- **Out of scope:** confirmation dialogs and inline editors; changing the underlying menu commands.

- **Verification evidence (2026-10-04):** Shared final gates for T-370/T-371/T-372: `cmake --build build -j4` ended with `[100%] Built target wormedit`; event harness ended with `100% tests passed out of 115` and `Total Test time (real) = 3.17 sec`; required core/frontend/frame/documentation partition ended with `100% tests passed out of 1769` and `Total Test time (real) = 107.47 sec`; full suite ended with `100% tests passed out of 2076` and `Total Test time (real) = 141.24 sec`. Logs: `/tmp/wormedit-bookmark-build12.log`, `/tmp/wormedit-bookmark-event-final.log`, `/tmp/wormedit-bookmark-partition-final.log`, `/tmp/wormedit-bookmark-full-final.log`. Affected-source `clang-tidy -p build` checks passed with no new complexity violations; increased export-popup complexity was removed by extracting lifecycle, choices and control helpers. Existing unrelated violations remain unchanged. Root final frontend/panel checks: `/tmp/wormedit-bookmark-frontend-final-tidy.log`, `/tmp/wormedit-bookmark-panel-final-tidy.log`; agent final menu checks: `/tmp/t372-tidy-final-*.log`.
- **Isolated visual evidence:** Isolated real-window File-to-Edit switching, Escape and quick outside right-click dismissal passed. Sounds filter opening and Escape dismissal retain the normal opening release. Harness coverage verifies disabled items, inline Layer submenu, stale contexts, dense BGA picker paging/dragging, quiet-frame release and coalesced outside clicks without background actions. Artifacts: `/tmp/wormedit-bookmark-menu-batch/`.


## T-373 Move the BGA timeline lane between timing and playable notes

- **Status:** done (2026-10-04; BGA between timing and playable notes)
- **Request (user, 2026-10-04):** The BGA lane at the far right is difficult to work with beside the docks; put it between the timing lanes and the note lanes. This follows [T-360](compose-followups.md#t-360-add-a-bga-event-editing-lane-to-the-compose-timeline).
- **Outcome:** Compose displays timing lanes, then the BGA timeline lane, then playable note lanes, keeping BGA events accessible with the Sounds, BGA, Layers and inspector docks open.
- **Read first:** `src/sys/compose/views/compose_view.cpp` (`compose_horizontal_layout` and scene request), `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`.
- **Files owned:** `src/core/editor_scene.cpp`, `tests/core/editor_scene_tests.cpp`, `src/sys/compose_view.cpp/.h`, `src/sys/compose_input.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_panel.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/sys/bga_lane_workflow_tests.cpp`, new `tests/sys/bga_lane_projection_tests.cpp`, `tests/CMakeLists.txt`, `tests/ui/bga_lane_event_harness_tests.cpp`.
- **Supporting scope (coordinator approved, 2026-10-04):** `tests/CMakeLists.txt` registers the new `tests/sys/bga_lane_projection_tests.cpp` BGA scene cases in the existing frame-timing target, which links the real Compose projection; the core-only target cannot resolve that application projection. The coordinator owns this registration and new projection test file; existing headless core workflow coverage stays in its original target. `src/ui/nuklear/compose_input_panel.cpp` must use the timing rail boundary before BGA and accept BGA pointer targets before the playable field; the old timing/placement guards otherwise route BGA clicks to timing insertion. `src/core/editor_scene.cpp` and `tests/core/editor_scene_tests.cpp` address duplicate BGA records in full cached-window projection exposed by the new scene check, preserving one record per event and metadata/scalar parity.
- **Steps:**
  1. Make the shared horizontal layout expose the BGA lane directly after the visible timing rail, moving the playable field's left edge after it while preserving the BGM pane after the playable field.
  2. Use the same layout in scene projection, lane headers, placement and hit-testing so BGA selection, dense overlap picking, dragging and previews agree with the displayed lane.
  3. Add regressions for timing-lane toggles, BGA visibility, narrow widths, both scroll directions and open docks; verify actual BGA placement, selection, movement and undo with docks open in the event harness and isolated Xvfb.
- **Invariants:** Event IDs, positions, BMS BMP identities, BGM column order and playable lane order do not change; only horizontal lane layout changes.
- **Reject/failure behavior:** Docks and resize cannot send a BGA gesture to a playable note lane or leave an invisible active drag; stale geometry retains the existing cancellation behavior.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'BGA|bga|horizontal layout'`; `ctest --test-dir build --output-on-failure -L event-harness`; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; affected-source `clang-tidy -p build <source-file>` with no new complexity violations; isolated Xvfb PNG with the docks open showing the timing/BGA/note lane order and an actual saved BGA edit.
- **Out of scope:** BGA resource dock redesign; new BGA event kinds; changing vertical timing or event positions.

- **Verification evidence (2026-10-04):** Application build and full suite passed 2,045/2,045, including all 104 input-harness cases. Layout tests cover full/narrow widths and lane visibility; BGA harness workflows cover placement, selection, movement and dense overlap picking with docks. Cached/fresh core projection has one record per BGA event in both directions. Actual isolated editing saved event 302 at row 480 with the original resource/kind/provenance; one undo restored row 240, with playable notes, BGM events and resources unchanged. Wide and 1,000px window captures retain timing → BGA → notes. Affected-source tidy found no new complexity violations. Artifacts: `/tmp/wormedit-filters-bga-minimap-batch/`; suite log: `/tmp/wormedit-filters-full-suite-passed.log`.
