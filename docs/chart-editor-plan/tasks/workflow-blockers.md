# Workflow blockers found by the user walkthrough

[Back to task cards](../tasks.md).

The 2026-10-18 user-facing walkthrough ([BMS](../../audits/user-walkthrough-bms-2026-10-18.md),
[osu!mania/StepMania](../../audits/user-walkthrough-osu-stepmania-2026-10-18.md)) found steps a chart
author cannot complete. Owner (2026-10-18): "We should really deal with those workflow blockers";
"Saving… kinda should copy it." These cards come before the navigation refactors in
[the workflow navigation audit](../../audits/workflow-navigation-2026-10-18.md).

The original blocker cards prove their fixes from a **new project** through the production path,
with walkthrough coverage in `tests/ui/walkthrough_{bms,osu-stepmania}_event_harness_tests.cpp` and
user-visible confirmation in isolated Xvfb. The original blocker lanes are complete.
The integration follow-ups T-718 (copy naming), T-719 (portability notice), T-720 (format hint),
T-721 (Timing-focus policy) and the T-716 manual correction are complete. Their shared-file edits
were assigned explicitly during the parallel batch. T-658 is next.

## T-707 Create BMS-family projects as BMS charts

- **Status:** done
- **Evidence (2026-10-18):** Blank and audio-backed factories use the layout descriptor's shared output family; every catalog layout retains its lanes, bindings and output groups. Replaced the blanket native-family test assumption with per-layout family checks; no existing project fixtures changed. The production wizard covers BME and BMSON; the from-new-BME walkthrough covers the `#000` projection/virtual canvas, BMS metadata target, reviewed import with `WAV01`, projected Sounds slots, BGM column button and core command, Move to BGM capability/core command, and iBMSC paste of that sound. All 13 focused CTest cases pass; changed-C++ complexity passes (4 translation units). `scripts/gate.sh --jobs 4`: `gate: passed`, 4,189/4,189 tests, build/whitespace/complexity pass. No supporting-file extensions. Xvfb was not run, per the owner's windowless execution instruction.
- **Outcome:** A project created from a BMS, BME or PMS layout has charts whose `source_family` is `bms`, so imported sounds get `#WAV` IDs, the Sounds dock offers BMS slots, BGM columns are editable, Move to BGM works and slot-based paste accepts the imported sounds. BMSON layouts likewise start as BMSON. Layouts whose outputs mix families (osu!mania with StepMania) stay native.
- **Read first:** BMS walkthrough F1; `src/sys/application_defaults.cpp` (`make_initial_document`); `include/wormedit/core/lane_profiles.h` (`new_project_layout_descriptor_t`, `default_output_groups`).
- **Files owned:** `src/sys/application_defaults.cpp`, `include/wormedit/core/lane_profiles.h` and `src/core/lane_profiles.cpp` (only to expose the family fact if needed), `tests/sys/application_defaults_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Derive the chart family from the layout's output groups when they share one family; keep `native_project` otherwise.
  2. Check what BMS-family charts need beyond the family flag (virtual `#000` canvas, BMS metadata section, WAV identity allocation) and that each turns on.
  3. Walkthrough test: new BME 7K → import → IDs, slots, BGM column, Move to BGM, iBMSC paste of the imported sound.
- **Invariants:** Lanes, lane keys and output groups unchanged; existing projects unaffected.
- **Reject/failure behavior:** Not applicable (construction).
- **Verify:** GATE; Xvfb: new BME 7K shows `#000`, the BMS slots switch and an editable BGM column count.
- **Out of scope:** Converting an existing native project to BMS.

## T-708 Separate output file extension from the BMS lane profile

- **Status:** done
- **Evidence (2026-10-18):** New BME/PMS publication matrix covers explicit .bme/.bms/.pms filenames, layout-derived folder names and foreign-extension refusal (including the slicing route). Export-chain/documentation focused checks: 23/23 passed. `scripts/gate.sh --jobs 3`: `gate: passed`, 4183/4183 tests; complexity passed for 13 translation units. Window checks omitted per the owner’s no-display instruction.
- **Outcome:** A BMS output path such as `song/normal.bme` exports a `.bme` file using the chart's own lane profile; a folder path names files with the extension that matches each chart's layout (`.bme` for BME, `.pms` for PMS).
- **Read first:** BMS walkthrough F2; `src/sys/project/output_group_export.h` (request planning), `src/formats/conversion.cpp` (target profile and filename), `src/formats/bms_writer.cpp` (profile lookup).
- **Files owned:** `src/sys/project/output_group_export.h`, `src/sys/project/output_group_export.cpp`, `src/formats/conversion.cpp`, `include/wormedit/formats/conversion.h` (if the request type changes), tests in `tests/formats/` and the BMS walkthrough test, `tests/sys/resource_notification_tests.cpp`, this card.
- **Supporting-file extension:** `tests/sys/resource_notification_tests.cpp` updates the existing shared-plan assertion that encoded extension-as-profile behavior and covers foreign extensions on the slicing route; `src/sys/project/output_group_export.cpp` validates family extensions before that route can bypass ordinary naming.
- **Steps:**
  1. Stop deriving `target_profile` from the extension; carry the extension as an output-naming fact.
  2. Choose the folder filename extension from the chart's BMS layout.
  3. Tests: explicit `.bme`/`.bms`/`.pms` paths and folder output for BME and PMS charts.
- **Invariants:** BMSON, osu!mania and StepMania naming unchanged.
- **Reject/failure behavior:** An extension that contradicts the format family still refuses with a clear message.
- **Verify:** GATE; the walkthrough export to `song/normal.bme` succeeds.
- **Out of scope:** Asset packaging (T-709).

## T-709 Package referenced sounds and images with exported charts

- **Status:** done
- **Evidence (2026-10-18):** New BME import → production note edit → native save → explicit .bme publication resolves the copied WAV; BMS/BMSON sound/image copies and named missing-source refusal pass. Export-chain/documentation focused checks: 23/23 passed. `scripts/gate.sh --jobs 3`: `gate: passed`, 4183/4183 tests; complexity passed for 13 translation units. Window checks omitted per the owner’s no-display instruction.
- **Outcome:** Exporting a BMS or BMSON chart copies every referenced keysound and BGA file that is not already at its destination next to the chart (atomically with the chart, through the existing pending-asset publication path) and rewrites references to the copies; a source that cannot be found refuses the export naming it, instead of reporting success.
- **Read first:** BMS walkthrough F3; `src/sys/project/output_group_export.cpp` (`plan_output_music`, staging), `src/formats/conversion_publication.cpp` (asset discovery), `src/core/resource_file_copy.cpp`.
- **Files owned:** `src/sys/output_group_export.cpp/.h`, `src/formats/conversion_publication.cpp`, `include/wormedit/formats/conversion_publication.h`, tests, this card.
- **Supporting-file extension:** `include/wormedit/formats/conversion_publication.h` exposes pending-copy planning at the existing asset-discovery owner so sys does not duplicate its asset inventory.
- **Steps:**
  1. Plan copies for referenced sample and image assets the way osu!mania/StepMania music is planned.
  2. Refuse publication when a referenced source is missing, naming it.
  3. Test: new BME → import keysound → note → save → export to a fresh folder → the `#WAV` file exists beside the chart.
- **Invariants:** Publication stays atomic; existing destinations are not overwritten unless the user chose to replace.
- **Reject/failure behavior:** A missing source or copy failure publishes nothing.
- **Verify:** GATE; Xvfb export and file inspection.
- **Out of scope:** Re-encoding audio.

## T-710 Make F3 and Timing Settings work from Compose

- **Status:** done
- **Evidence (2026-10-18):** Added a hidden-Timing registry regression and two new-project production-path walkthrough cases for F3, visible palette/Compose settings and F4 recovery; focused tests passed 14/14. `scripts/gate.sh --jobs 3`: `gate: passed`, 4195/4195 tests, complexity 6/6. Smoke script shell syntax passed; its new `timing` pixel assertion and real-window checks remain unverified here for the coordinator to run.
- **Outcome:** F3 (*Open Timing*) enters Timing focus in Compose, and *Timing Settings* opens a visible panel from Compose and the command palette. The smoke script's `timing` mode asserts that Timing focus is on.
- **Read first:** osu!mania/StepMania walkthrough, real-window checks; `src/core/command_registry.cpp` (the F1–F7 loop, `is_eligible`), `src/sys/commands/application_command_dispatch.cpp` (`workspace_open_timing`, `timing_settings_visible`), `src/ui/nuklear/timing_settings_panel.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/application.cpp` (registration state only), the timing settings frontend files, `scripts/smoke-compose-virtual-x.sh`, tests (including `tests/CMakeLists.txt`), this card.
- **Supporting-file extension (2026-10-18):** `tests/CMakeLists.txt` links the production application command dispatcher and its Compose route dependencies into the walkthrough event harness.
- **Steps:**
  1. Register `workspace.open_timing` against Compose (it is an alias), so destination visibility passes.
  2. Give Timing Settings a Compose surface (or retarget the command to an existing one); add an event-harness test that it becomes visible.
  3. Make the smoke `timing` mode fail unless the Timing focus button is active.
- **Invariants:** The hidden Timing tab stays hidden; Storyboard stays disabled.
- **Reject/failure behavior:** Not applicable.
- **Verify:** GATE; Xvfb: F3 highlights the timer button; Timing Settings appears.
- **Out of scope:** Redesigning Timing Settings.

## T-711 Explain refusals in Timing focus

- **Status:** done
- **Evidence (2026-10-18):** Added three feedback unit cases (including all seven Timing-disabled commands) and a new-project F3 → refused Alt+2 → F4 → note-placement walkthrough; focused tests passed 14/14. `scripts/gate.sh --jobs 3`: `gate: passed`, 4195/4195 tests, complexity 6/6. Real-window feedback verification remains unverified here for the coordinator to run.
- **Outcome:** Commands refused because Timing focus is on say so and name the way out ("Timing focus is on: press F4 to edit notes."), instead of "Command is currently unavailable: edit.tool_draw". Unavailable-command feedback in general uses the command's display name, not its ID.
- **Read first:** osu!mania/StepMania walkthrough finding 1; `src/sys/application.cpp` (unavailable-command feedback), `src/sys/commands/application_command_dispatch.cpp` (Draw refusal).
- **Files owned:** `src/sys/application.cpp` (that feedback only), `src/sys/commands/application_command_dispatch.cpp`, `src/sys/commands/application_command_dispatch.h`, tests, this card.
- **Supporting-file extension (2026-10-18):** `src/sys/commands/application_command_dispatch.h` declares the shared unavailable-command feedback formatter for the application boundary and production-path tests.
- **Steps:**
  1. Unavailable-command feedback names the command's display name.
  2. Commands disabled by Timing focus carry the reason and the F4 remedy.
- **Invariants:** Which commands are enabled does not change.
- **Reject/failure behavior:** Not applicable.
- **Verify:** GATE; Xvfb: Timing focus → Alt+2 shows the new message.
- **Out of scope:** Other disabled-command reasons beyond the display-name change.

## T-712 Export a group whatever chart is active

- **Status:** done
- **Evidence (2026-10-18):** New-project Compose dispatch exports only the group’s included chart with an excluded difficulty active; empty-group refusal and Project button availability pass. Export-chain/documentation focused checks: 23/23 passed. `scripts/gate.sh --jobs 3`: `gate: passed`, 4183/4183 tests; complexity passed for 13 translation units. Window checks omitted per the owner’s no-display instruction.
- **Outcome:** Export (row button and *Export…*) publishes a group's own charts regardless of which chart is active; the button is enabled exactly when the group has charts.
- **Read first:** osu!mania/StepMania walkthrough finding 2; `src/sys/compose/actions/compose_resource_actions.cpp` (export validation), `src/ui/nuklear/setup_panel.cpp` (button enablement).
- **Files owned:** those two files and their tests, this card.
- **Steps:** 1. Validate the group's chart IDs instead of the active chart. 2. Test with an excluded difficulty active.
- **Invariants:** Stale and busy checks unchanged.
- **Reject/failure behavior:** An empty group still refuses.
- **Verify:** GATE.
- **Out of scope:** Multi-group export.

## T-713 Send the StepMania export policy to Preview and Export

- **Status:** done
- **Evidence (2026-10-18):** New-project Etterna/legacy policy requests reach serialization and publication; legacy output differs, incompatible combinations refuse, and async Preview receives the policy. Export-chain/documentation focused checks: 23/23 passed. `scripts/gate.sh --jobs 3`: `gate: passed`, 4183/4183 tests; complexity passed for 13 translation units. Window checks omitted per the owner’s no-display instruction.
- **Outcome:** The StepMania export policy chosen in the Project tab (target and timing syntax) is the policy Preview and Export use for SM/SSC groups.
- **Read first:** osu!mania/StepMania walkthrough finding 3; `src/sys/audio/transport_player_actions.cpp` (policy storage), `src/sys/project/output_group_export.h` (request builder), `src/sys/project/project_view.cpp` (preview).
- **Files owned:** those files, `src/sys/project/project_view.h`, `src/sys/application_edit_inputs.h`, `src/sys/application.cpp`, `src/sys/compose/actions/compose_resource_actions.cpp`, `src/ui/nuklear/setup_panel.cpp`, tests, this card.
- **Supporting-file extensions:** `src/sys/project/project_view.h` captures policy in preview requests; `src/sys/application_edit_inputs.h` passes the session policy to resource actions; `src/sys/application.cpp` supplies the existing stored policy to both paths; `src/sys/compose/actions/compose_resource_actions.cpp` forwards it to export planning; `tests/sys/application_dispatch_fixture.h` keeps aggregate setup named after the input extension. The existing view-owned session policy remains the sole storage owner. `src/ui/nuklear/setup_panel.cpp` explains that group paths own SM/SSC family.
- **Steps:** 1. Store the policy where both planners read it. 2. Tests: Etterna and legacy-timing choices change the written file.
- **Invariants:** Default policy output unchanged.
- **Reject/failure behavior:** An incompatible combination is refused before publication.
- **Verify:** GATE.
- **Out of scope:** New StepMania targets.

## T-714 Preview the same plan that Export publishes

- **Status:** done
- **Evidence (2026-10-18):** Audio-started projects preview and export all three default groups; Preview writes nothing, renders filenames/copies/warnings, and matches publication collision and missing-source refusals. Export-chain/documentation focused checks: 23/23 passed. `scripts/gate.sh --jobs 3`: `gate: passed`, 4183/4183 tests; complexity passed for 13 translation units. Window checks omitted per the owner’s no-display instruction.
- **Outcome:** Preview uses export's planning (asset staging included), so a project that exports also previews, and Preview lists the files it would write and its warnings rather than only counts.
- **Read first:** osu!mania/StepMania walkthrough findings 4 and 9, BMS walkthrough F5; `src/sys/project/project_view.cpp` (preview), `src/sys/project/output_group_export.cpp` (planning).
- **Files owned:** those files, `src/sys/project/output_group_export.h`, `src/sys/project/project_view.h`, `src/sys/application.cpp`, `src/ui/nuklear/setup_panel.cpp`, `tests/CMakeLists.txt`, `docs/architecture.md`, tests, this card.
- **Supporting-file extensions:** `src/sys/project/output_group_export.h` exposes shared read-only preparation; `src/sys/project/project_view.h` reuses the export request for async Preview; `src/sys/application.cpp` supplies the asset base; `tests/CMakeLists.txt` links shared preparation into core Project-view tests; `docs/architecture.md` records the shared preparation boundary. The existing unbounded preview text is retained and wrapped by the Project tab renderer.
- **Steps:** 1. Share the planning step without publishing. 2. Show planned file names, copies and warning texts. 3. Test: audio-started project previews and exports.
- **Invariants:** Preview never writes files.
- **Reject/failure behavior:** Preview reports the same refusals export would.
- **Verify:** GATE; Xvfb Project tab preview.
- **Out of scope:** Destination collision detection beyond what export already plans.

## T-715 Copy linked assets into the project when saving

- **Status:** done
- **Evidence (2026-10-18):** Explicit Save/Save As runs on the existing resource worker through `async_job_t`: atomic native snapshot, verified asset copies/reuse, atomic link persistence, then live/Undo/Redo path publication without reverting newer edits. External audio, images, MIDI and generator sources are covered, including files in parent folders and symlinks escaping the project. Copy failures retain the external link and name the file; status lists newly copied paths. The from-new-audio-project event walkthrough covers Save input, the production save runtime, copied bytes/native reopen and a repeat save without copying. All 22 focused CTest cases pass. `scripts/gate.sh --jobs 4`: `gate: passed`, 4,201/4,201 tests, build/whitespace/complexity pass (11 changed translation units, including the accepted T-707 work). Xvfb was not run, per the owner's windowless execution instruction; no originals are moved or deleted.
- **Outcome:** Saving a project copies song audio, keysounds and images that live outside the project directory into its assets folder and relinks them, so a saved project is self-contained (owner, 2026-10-18).
- **Read first:** osu!mania/StepMania walkthrough finding 5; `src/sys/application.cpp` (save path), `src/sys/project/project_save_paths.cpp`, `src/core/resource_file_copy.cpp` and `src/sys/resources/resource_job_state.cpp` (the existing project-copy job).
- **Files owned:** `src/sys/application.cpp` (save and its completion/close lifecycle only), `src/sys/project/project_save_paths.cpp`, `src/sys/project/project_save_paths.h`, `src/sys/resources/resource_job_state.cpp`, `src/sys/resources/resource_job_state.h`, `include/wormedit/core/resource_file_copy.h`, `src/core/resource_file_copy.cpp`, `tests/core/resource_file_copy_tests.cpp`, `tests/sys/project_save_paths_tests.cpp`, `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`, `tests/CMakeLists.txt`, this card.
- **Supporting-file extensions:** `src/sys/project/project_save_paths.h` exposes the save worker/report used by the application and tests. `include/wormedit/core/resource_file_copy.h` and `src/core/resource_file_copy.cpp` add opt-in reuse of any identical file in `assets/` at the existing byte-comparison owner. `tests/CMakeLists.txt` links the shared save worker into the three existing resource-runtime test targets.
- **Steps:**
  1. Owner decision (2026-10-18): every save copies every asset outside the project folder into `assets/` and relinks it, silently; identical files are reused, so a repeat save copies nothing. The status message lists what was copied.
  2. Reuse the existing resource copy job (atomic copy, relink, reuse of identical files) after the native file is written.
  3. Tests: audio-started project saved elsewhere ends with relative asset paths and copied files; a second save copies nothing new.
- **Invariants:** Save stays atomic for the project file; a failed copy never loses the link.
- **Reject/failure behavior:** A copy failure reports which file and leaves the external link in place.
- **Verify:** GATE; Xvfb save and inspect the project folder.
- **Out of scope:** Moving or deleting the originals.

## T-716 Correct the manual after the blockers land

- **Status:** done
- **Evidence (2026-10-19):** The two audited walkthroughs and five supporting chapters now use the
  integrated commands and exact current limits. Three isolated Xvfb captures start in the production
  wizard, select BME 7K (outside scratch), and visibly show BMS measure labels/BGM controls/Sounds,
  empty WAV slot 09 as the selected import target, and a four-mine iBMSC paste preview with the live
  paste status. Manual documentation tests pass 2/2, both production walkthrough suites pass 23/23,
  the manual target builds, the three scripts pass `bash -n`, and whitespace is clean. The stale-shot
  audit was run: all 12 existing shots remain listed because this uncommitted integration batch
  changes UI sources; the three regenerated images are stamped `b4d9b5a+dirty`, while the other nine
  remain T-432/T-433 capture work. This card does not broaden into those cards.
- **Outcome:** The two walkthrough chapters and the five supporting chapters below describe the
  integrated behavior observed from newly created projects. The manual no longer preserves any
  workaround from the pre-fix audit, and the BMS slot, BGM and paste screenshots no longer hide the
  new-project path behind an imported BME fixture.
- **Read first:** the "Manual corrections" and "Needs a real-window check" sections of both
  walkthrough reports; `docs/manual/index.adoc`; T-432 and T-433.
- **Files owned:** `docs/manual/chapters/walkthrough-bms.adoc`,
  `docs/manual/chapters/walkthrough-osu-stepmania.adoc`,
  `docs/manual/chapters/getting-started.adoc`, `docs/manual/chapters/timing.adoc`,
  `docs/manual/chapters/placing-notes.adoc`, `docs/manual/chapters/measures.adoc`,
  `docs/manual/chapters/sounds-and-layers.adoc`, `docs/manual/shots/bms-slots.sh`,
  `docs/manual/shots/compose-overview.sh`, `docs/manual/shots/paste-ibmsc.sh`,
  `docs/manual/images/bms-slots.png`, `docs/manual/images/compose-overview.png`,
  `docs/manual/images/paste-ibmsc.png`, `docs/manual/images/revisions.adoc`, this card.
- **Supporting-file extension (2026-10-19):** `docs/manual/chapters/navigating.adoc` owns the
  `compose-overview.png` caption and alt text; both now describe the fresh empty BME state shown by
  the replacement capture. The referenced `docs/manual/README.md` did not exist.
- **Coordination:** this card owns only corrections identified by the two 2026-10-18 walkthrough
  audits. It does not complete T-432's reader trial or missing tool/dock/import-review shots, or
  T-433's Troubleshooting, exercises and Project output-group shot.
- **Steps:**
  1. Re-run both walkthroughs from Ctrl+N through Save/Preview/Export and record any remaining
     mismatch on this card before changing prose.
  2. Apply only still-true audit corrections: BMS metadata Apply/Add, exact paste priority and
     missing-WAV feedback, authored-measure Ctrl+M, song-audio assignment, F3/F4 timing workflow,
     beat-rail wording, mouse tools, chart creation/duplication, output-group actions and policy,
     Preview/publication behavior, Save asset copying, Tap tempo, timing lock and unsupported note
     kinds.
  3. Change the three named capture scripts so their visible state is created from a new BME
     project during the capture run; regenerate only their PNGs and the revision table.
- **Invariants:** Generated tables stay generated.
- **Reject/failure behavior:** Not applicable.
- **Verify:** `ctest --test-dir build -j 8 -R '^documentation\.manual_' --output-on-failure`;
  `ctest --test-dir build -j 8 -R '^frontend-state\.(BMS|Osu StepMania) walkthrough' --output-on-failure`;
  `scripts/manual-captures.sh bms-slots compose-overview paste-ibmsc`;
  inspect `docs/manual/images/{bms-slots,compose-overview,paste-ibmsc}.png` and record that each
  shows the intended state reached from a newly created BME project;
  `scripts/manual-captures.sh --check-stale`; `cmake --build build --parallel 8 --target manual`.
- **Out of scope:** New chapters.

## T-717 Start sound imports at the selected BMS slot

- **Status:** done
- **Evidence (2026-10-18):** Six new walkthrough cases start from the production BMS session factory (the wizard family fix remains T-707), project the real catalog and cover picked 0A–0C assignments, armed-05 automatic imports, occupied-slot review, overflow refusal, selection resets and single-file slot drops. Extended drop-capture and overflow unit coverage; focused tests passed 22/22. `scripts/gate.sh --jobs 3`: `gate: passed`, 4201/4201 tests, complexity 13/13. Xvfb verification remains unverified here for the coordinator to run.
- **Outcome:** If the user has single-clicked a BMS slot in the Sounds dock's *BMS slots* view (the highlighted slot), any multi-file import (*Add sounds…*, a file-dialog import or a drop) fills consecutive slots starting at that slot. Occupied slots go through the existing replace review. With no slot clicked, imports use automatic IDs, as *Add sounds…* does today. Owner, 2026-10-18: "if the BMS slot is specifically selected … mass-imports should also probably start at that index".
- **Read first:** `src/ui/nuklear/compose_sounds_panel.cpp` (`keysound_selected_slot`: set by a click at the slot row, reset to 1 on mode or chart change, synced to the armed sound by `sync_selected_slot_to_armed_resource`), `src/sys/application.cpp` (`stage_sound_import`, `stage_dropped_sound_slots`, `dispatch_reviewed_sound_import`), `src/sys/application_frame_services.cpp` (`collect_dropped_resources`, `first_slot`), `src/sys/compose/actions/compose_sound_actions.cpp` (`prepare_sound_slot_batch`).
- **Files owned:** the files above, `src/ui/nuklear/compose_state.h` (the selection fact), `src/ui/nuklear/frontend.cpp` (the dock request), tests, this card; supporting files below.
- **Supporting-file extensions (2026-10-18):** `src/ui/frontend/compose.h` and `src/sys/resources/sound_drop_transaction.h` carry the explicit-choice fact across the existing frontend and captured-drop boundaries. `src/sys/resources/sound_import_staging.h`, `src/sys/resources/sound_import_staging.cpp`, `CMakeLists.txt` and `tests/CMakeLists.txt` share and link the production staging path for the application and walkthrough harness. `src/sys/compose/actions/compose_sound_actions.h` documents the revised overflow result. `docs/chart-editor-plan/tasks.md` advances the free-ID range as requested.
- **Steps:**
  1. Record whether the slot was chosen explicitly. A click selects it. The default slot 1 and the armed-sound sync do not, so arming a sound never redirects imports. Mode, chart or project changes clear the choice.
  2. Route *Add sounds…*, file-dialog imports and drops through the slot batch when a slot is explicitly chosen, and through automatic IDs otherwise. Today a slots-mode drop with no click starts at 01.
  3. If the batch runs past ZZ, refuse it before staging and name how many slots are left.
  4. Tests: click 0A then import three files, which land in 0A, 0B and 0C; arm a sound in slot 05 then import, which uses automatic IDs; an occupied slot inside the range opens the replace review.
- **Invariants:** Single-file drops on a slot row still target that row; Automatic IDs mode is unchanged.
- **Reject/failure behavior:** A batch that runs past the last slot is refused, and nothing is staged.
- **Verify:** GATE; Xvfb: click a slot, *Add sounds…*, and check the slot rows.
- **Out of scope:** Selecting several slots at once.

## T-718 Keep original asset names when they are unambiguous

- **Status:** done
- **Evidence (2026-10-18):** One read-only planner now selects and carries the destination through
  inspection, Save, Preview and atomic Export publication. Free and identical basenames retain the
  source name; on-disk and same-plan conflicts use the asset-ID fallback, and a conflict at both
  candidates refuses without overwrite. The BMS walkthrough verifies two external sounds with the
  same basename and exact Preview/Export path equality. The focused expression passes 62/62.
  `scripts/gate.sh --agents 1` passes 4,228/4,228 tests with build, whitespace and changed-C++
  complexity clean. In isolated Xvfb, production Save created `assets/keysound.ogg` and
  `assets/38-keysound.ogg`; Project Preview listed those paths and row Export published the BME and
  matching files. The new-project origin is covered by the production walkthrough; the real-window
  run used a disposable tutorial-project copy to inject the two absolute same-basename sources.
- **Outcome:** Save and BMS/BMSON export use the source basename when it is free or already contains
  identical bytes (`assets/keysound-c4.ogg`). If that basename is claimed by different bytes in the
  destination or earlier in the same plan, the copy uses the existing deterministic
  `<asset-id>-<basename>` fallback. Preview reports the same paths that Export publishes.
- **Read first:** `include/wormedit/core/resource_file_copy.h` (`project_asset_copy_request_t`);
  `src/core/resource_file_copy.cpp` (`inspect_project_asset_file`, `create_project_asset_copy`);
  `src/sys/project/output_group_export.cpp` (`prepare_output_assets`);
  `src/formats/conversion_publication.cpp` (`validate_pending_assets`).
- **Files owned:** `include/wormedit/core/resource_file_copy.h`,
  `src/core/resource_file_copy.cpp`, `src/sys/project/output_group_export.cpp`,
  `src/formats/conversion_publication.cpp`,
  `include/wormedit/formats/conversion_publication.h` if the prepared plan must carry the chosen
  path, `tests/core/resource_file_copy_tests.cpp`, `tests/sys/project_save_paths_tests.cpp`,
  `tests/sys/resource_notification_tests.cpp`,
  `tests/ui/walkthrough_bms_event_harness_tests.cpp`,
  `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`, this card.
- **Supporting-file extension (2026-10-18):**
  `tests/formats/conversion_publication_tests.cpp` owns the existing discovery, atomic-staging and
  collision expectations that encoded the old prefixed-name contract.
- **Steps:**
  1. Make destination selection a shared read-only planning result used by inspection, publication,
     Save, Preview and Export; do not recompute a filename after planning.
  2. Prefer the basename, reuse byte-identical destinations, and fall back to the asset-ID-prefixed
     name for different bytes or a same-plan collision.
  3. Cover free, identical, conflicting-on-disk and conflicting-in-one-plan basenames on both Save
     and export, including Preview/Export path equality and repeat Save.
- **Invariants:** Identical-file reuse and atomic publication stay as they are; existing projects with prefixed names still load.
- **Reject/failure behavior:** If both candidate names contain different bytes or cannot be safely
  inspected, refuse before publication and name the destination; never overwrite either file.
- **Verify:** `ctest --test-dir build -j 8 -R '^(core\.(project asset copy|export asset copy|export asset discovery|Automatic export music|project save|conversion publication)|frame-arena\.resource copy|frontend-state\.(BMS|Osu StepMania) walkthrough)' --output-on-failure`;
  GATE; isolated Xvfb Save and BMS export from a new project with two external sounds sharing a
  basename, followed by inspection of the saved/exported references and files.
- **Out of scope:** Renaming assets already copied into existing projects.

## T-719 Make the portability notice describe Save

- **Status:** done
- **Evidence (2026-10-18):** `project_portability_notice_t` owns the exact advice; the application
  only posts it. The three focused portability cases pass, including exact Save/Save As and
  failed-copy wording. The integrated GATE passes 4,228/4,228. Isolated Xvfb shows the revised
  warning on the disposable project with absolute asset links.
- **Outcome:** On opening a project with outside links, the warning says that Save or Save As copies
  those assets into `assets/` and relinks the project, and that a failed copy retains the original
  link. It does not direct the user to a project-copy control that no longer owns this workflow.
- **Read first:** `src/sys/project/project_save_paths.h` (`project_portability_notice_t`);
  `src/sys/project/project_save_paths.cpp` (`project_portability_notice_t::update`);
  `src/sys/application.cpp` (`publish_project_portability_notice`).
- **Files owned:** `src/sys/project/project_save_paths.h`, `src/sys/project/project_save_paths.cpp`,
  `src/sys/application.cpp` (portability notice publication only),
  `tests/sys/project_save_paths_tests.cpp`, this card.
- **Steps:** 1. Put the user-facing portability fact beside `project_portability_notice_t`; keep
  `application.cpp` responsible only for posting it. 2. Test the exact advice plus the existing
  once-per-session/revision behavior.
- **Invariants:** Detection of absolute, escaping and revision-carried paths is unchanged.
- **Reject/failure behavior:** Not applicable.
- **Verify:** `ctest --test-dir build -j 8 -R '^core\.Portability warning' --output-on-failure`;
  GATE.
- **Out of scope:** Changing when automatic asset copying runs.

## T-720 Name BGM movement for the active chart format

- **Status:** done
- **Evidence (2026-10-18):** The idle hint uses the existing BMS/BMSON family projections and falls
  back to generic BGM text. Two focused event-harness cases cover all three families and preserve
  move, conflict and paste priority. Focused tests pass 2/2; clang-tidy and affected complexity
  pass; the integrated GATE passes 4,228/4,228.
- **Outcome:** The idle Compose hint says `drag BMS BGM to move` for BMS, `drag BMSON BGM to move`
  for BMSON, and `drag BGM to move` for other chart families. It uses the existing projected family
  facts (`bms_measure_labels` and `bmson_tools_available`).
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`compose_editor_status_hint`);
  `src/ui/frontend/compose.h` (the two family facts); `src/sys/compose/views/compose_view.cpp` (their projection).
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`,
  this card.
- **Steps:** 1. Build the idle hint from the existing view facts. 2. Test BMS, BMSON and native views,
  including that higher-priority move/conflict/paste hints still win.
- **Invariants:** Input behavior and hint priority are unchanged.
- **Reject/failure behavior:** Not applicable.
- **Verify:** `ctest --test-dir build -j 8 -R '^frontend-state\.' --output-on-failure`; GATE.
- **Out of scope:** Rewording other status hints.

## T-721 Share one Timing-focus command policy

- **Status:** done
- **Evidence (2026-10-18):** One typed application-command policy now drives registry disablement
  and both refusal paths. The test iterates parsed production registry entries and retains the
  direct-boundary check for all seven members. The focused expression passes 12/12; affected
  complexity passes; the integrated GATE passes 4,228/4,228.
- **Outcome:** One typed policy identifies commands disabled specifically by Timing focus. Registry
  enablement and both unavailable-command feedback paths query it, so adding or removing a command
  cannot leave the F4 refusal list out of sync.
- **Read first:** `src/sys/commands/application_command_dispatch.cpp` (`timing_focus_command_refusal`);
  `src/sys/application.cpp` (`refresh_note_kind_commands`, `refresh_quantize_commands`,
  `refresh_compose_dock_commands`); `src/sys/commands/application_commands.h` (command kinds and parser).
- **Files owned:** `src/sys/commands/application_command_dispatch.h`,
  `src/sys/commands/application_command_dispatch.cpp`, `src/sys/application.cpp`,
  `tests/sys/application_commands_tests.cpp`,
  `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`, this card.
- **Steps:** 1. Define the Timing-focus capability/refusal policy once at the application-command
  owner. 2. Derive registry enablement and direct/registry refusal feedback from it. 3. Iterate the
  production registry entries accepted by `parse_application_command`, rather than maintaining a
  second command list, and prove their policy, enabled state and refusal agree in Compose and Timing
  focus. Keep the existing direct-boundary test for every command the policy disables.
- **Invariants:** The seven currently disabled commands and the F4 remedy are unchanged; unrelated
  disablement reasons keep their existing messages.
- **Reject/failure behavior:** Not applicable.
- **Verify:** `ctest --test-dir build -j 8 -R '^(frame-timing\.Timing focus|frontend-state\.Osu StepMania walkthrough)' --output-on-failure`;
  `scripts/check-complexity.py --build-dir build src/sys/application.cpp src/sys/commands/application_command_dispatch.cpp`;
  GATE.
- **Out of scope:** A general command-reason framework or changes to Timing-focus membership.
