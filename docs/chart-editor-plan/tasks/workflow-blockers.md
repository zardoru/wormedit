# Workflow blockers found by the user walkthrough

[Back to task cards](../tasks.md).

The 2026-10-18 user-facing walkthrough ([BMS](../../audits/user-walkthrough-bms-2026-10-18.md),
[osu!mania/StepMania](../../audits/user-walkthrough-osu-stepmania-2026-10-18.md)) found steps a chart
author cannot complete. Owner (2026-10-18): "We should really deal with those workflow blockers";
"Saving… kinda should copy it." These cards come before the navigation refactors in
[the workflow navigation audit](../../audits/workflow-navigation-2026-10-18.md).

Every card proves its fix from a **new project**, through the production path, extending the
walkthrough tests in `tests/ui/walkthrough_{bms,osu-stepmania}_event_harness_tests.cpp`; the
coordinator confirms user-visible results in isolated Xvfb. Lanes: T-707 and T-715 (project and
save); T-708, T-709, T-712, T-713, T-714 (export, in that order); T-710 and T-711 (commands and
feedback). T-716 updates the manual after the others land.

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

- **Status:** ready
- **Outcome:** A BMS output path such as `song/normal.bme` exports a `.bme` file using the chart's own lane profile; a folder path names files with the extension that matches each chart's layout (`.bme` for BME, `.pms` for PMS).
- **Read first:** BMS walkthrough F2; `src/sys/output_group_export.h` (request planning), `src/formats/conversion.cpp` (target profile and filename), `src/formats/bms_writer.cpp` (profile lookup).
- **Files owned:** `src/sys/output_group_export.h`, `src/formats/conversion.cpp`, `include/wormedit/formats/conversion.h` (if the request type changes), tests in `tests/formats/` and the BMS walkthrough test, this card.
- **Steps:**
  1. Stop deriving `target_profile` from the extension; carry the extension as an output-naming fact.
  2. Choose the folder filename extension from the chart's BMS layout.
  3. Tests: explicit `.bme`/`.bms`/`.pms` paths and folder output for BME and PMS charts.
- **Invariants:** BMSON, osu!mania and StepMania naming unchanged.
- **Reject/failure behavior:** An extension that contradicts the format family still refuses with a clear message.
- **Verify:** GATE; the walkthrough export to `song/normal.bme` succeeds.
- **Out of scope:** Asset packaging (T-709).

## T-709 Package referenced sounds and images with exported charts

- **Status:** blocked-on T-708
- **Outcome:** Exporting a BMS or BMSON chart copies every referenced keysound and BGA file that is not already at its destination next to the chart (atomically with the chart, through the existing pending-asset publication path) and rewrites references to the copies; a source that cannot be found refuses the export naming it, instead of reporting success.
- **Read first:** BMS walkthrough F3; `src/sys/output_group_export.cpp` (`plan_output_music`, staging), `src/formats/conversion_publication.cpp` (asset discovery), `src/core/resource_file_copy.cpp`.
- **Files owned:** `src/sys/output_group_export.cpp/.h`, `src/formats/conversion_publication.cpp`, tests, this card.
- **Steps:**
  1. Plan copies for referenced sample and image assets the way osu!mania/StepMania music is planned.
  2. Refuse publication when a referenced source is missing, naming it.
  3. Test: new BME → import keysound → note → save → export to a fresh folder → the `#WAV` file exists beside the chart.
- **Invariants:** Publication stays atomic; existing destinations are not overwritten unless the user chose to replace.
- **Reject/failure behavior:** A missing source or copy failure publishes nothing.
- **Verify:** GATE; Xvfb export and file inspection.
- **Out of scope:** Re-encoding audio.

## T-710 Make F3 and Timing Settings work from Compose

- **Status:** ready
- **Outcome:** F3 (*Open Timing*) enters Timing focus in Compose, and *Timing Settings* opens a visible panel from Compose and the command palette. The smoke script's `timing` mode asserts that Timing focus is on.
- **Read first:** osu!mania/StepMania walkthrough, real-window checks; `src/core/command_registry.cpp` (the F1–F7 loop, `is_eligible`), `src/sys/application_command_dispatch.cpp` (`workspace_open_timing`, `timing_settings_visible`), `src/ui/nuklear/timing_settings_panel.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/application_command_dispatch.cpp`, `src/sys/application.cpp` (registration state only), the timing settings frontend files, `scripts/smoke-compose-virtual-x.sh`, tests, this card.
- **Steps:**
  1. Register `workspace.open_timing` against Compose (it is an alias), so destination visibility passes.
  2. Give Timing Settings a Compose surface (or retarget the command to an existing one); add an event-harness test that it becomes visible.
  3. Make the smoke `timing` mode fail unless the Timing focus button is active.
- **Invariants:** The hidden Timing tab stays hidden; Storyboard stays disabled.
- **Reject/failure behavior:** Not applicable.
- **Verify:** GATE; Xvfb: F3 highlights the timer button; Timing Settings appears.
- **Out of scope:** Redesigning Timing Settings.

## T-711 Explain refusals in Timing focus

- **Status:** ready
- **Outcome:** Commands refused because Timing focus is on say so and name the way out ("Timing focus is on: press F4 to edit notes."), instead of "Command is currently unavailable: edit.tool_draw". Unavailable-command feedback in general uses the command's display name, not its ID.
- **Read first:** osu!mania/StepMania walkthrough finding 1; `src/sys/application.cpp` (unavailable-command feedback), `src/sys/application_command_dispatch.cpp` (Draw refusal).
- **Files owned:** `src/sys/application.cpp` (that feedback only), `src/sys/application_command_dispatch.cpp`, tests, this card.
- **Steps:**
  1. Unavailable-command feedback names the command's display name.
  2. Commands disabled by Timing focus carry the reason and the F4 remedy.
- **Invariants:** Which commands are enabled does not change.
- **Reject/failure behavior:** Not applicable.
- **Verify:** GATE; Xvfb: Timing focus → Alt+2 shows the new message.
- **Out of scope:** Other disabled-command reasons beyond the display-name change.

## T-712 Export a group whatever chart is active

- **Status:** blocked-on T-709
- **Outcome:** Export (row button and *Export…*) publishes a group's own charts regardless of which chart is active; the button is enabled exactly when the group has charts.
- **Read first:** osu!mania/StepMania walkthrough finding 2; `src/sys/compose_resource_actions.cpp` (export validation), `src/ui/nuklear/setup_panel.cpp` (button enablement).
- **Files owned:** those two files and their tests, this card.
- **Steps:** 1. Validate the group's chart IDs instead of the active chart. 2. Test with an excluded difficulty active.
- **Invariants:** Stale and busy checks unchanged.
- **Reject/failure behavior:** An empty group still refuses.
- **Verify:** GATE.
- **Out of scope:** Multi-group export.

## T-713 Send the StepMania export policy to Preview and Export

- **Status:** blocked-on T-712
- **Outcome:** The StepMania export policy chosen in the Project tab (target and timing syntax) is the policy Preview and Export use for SM/SSC groups.
- **Read first:** osu!mania/StepMania walkthrough finding 3; `src/sys/transport_player_actions.cpp` (policy storage), `src/sys/output_group_export.h` (request builder), `src/sys/project_view.cpp` (preview).
- **Files owned:** those files, the policy's owner if it moves, tests, this card.
- **Steps:** 1. Store the policy where both planners read it. 2. Tests: Etterna and legacy-timing choices change the written file.
- **Invariants:** Default policy output unchanged.
- **Reject/failure behavior:** An incompatible combination is refused before publication.
- **Verify:** GATE.
- **Out of scope:** New StepMania targets.

## T-714 Preview the same plan that Export publishes

- **Status:** blocked-on T-713
- **Outcome:** Preview uses export's planning (asset staging included), so a project that exports also previews, and Preview lists the files it would write and its warnings rather than only counts.
- **Read first:** osu!mania/StepMania walkthrough findings 4 and 9, BMS walkthrough F5; `src/sys/project_view.cpp` (preview), `src/sys/output_group_export.cpp` (planning).
- **Files owned:** those files, the Project tab preview view, tests, this card.
- **Steps:** 1. Share the planning step without publishing. 2. Show planned file names, copies and warning texts. 3. Test: audio-started project previews and exports.
- **Invariants:** Preview never writes files.
- **Reject/failure behavior:** Preview reports the same refusals export would.
- **Verify:** GATE; Xvfb Project tab preview.
- **Out of scope:** Destination collision detection beyond what export already plans.

## T-715 Copy linked assets into the project when saving

- **Status:** done
- **Evidence (2026-10-18):** Explicit Save/Save As runs on the existing resource worker through `async_job_t`: atomic native snapshot, verified asset copies/reuse, atomic link persistence, then live/Undo/Redo path publication without reverting newer edits. External audio, images, MIDI and generator sources are covered, including files in parent folders and symlinks escaping the project. Copy failures retain the external link and name the file; status lists newly copied paths. The from-new-audio-project event walkthrough covers Save input, the production save runtime, copied bytes/native reopen and a repeat save without copying. All 22 focused CTest cases pass. `scripts/gate.sh --jobs 4`: `gate: passed`, 4,201/4,201 tests, build/whitespace/complexity pass (11 changed translation units, including the accepted T-707 work). Xvfb was not run, per the owner's windowless execution instruction; no originals are moved or deleted.
- **Outcome:** Saving a project copies song audio, keysounds and images that live outside the project directory into its assets folder and relinks them, so a saved project is self-contained (owner, 2026-10-18).
- **Read first:** osu!mania/StepMania walkthrough finding 5; `src/sys/application.cpp` (save path), `src/sys/project_save_paths.cpp`, `src/core/resource_file_copy.cpp` and `src/sys/resource_job_state.cpp` (the existing project-copy job).
- **Files owned:** `src/sys/application.cpp` (save and its completion/close lifecycle only), `src/sys/project_save_paths.cpp`, `src/sys/project_save_paths.h`, `src/sys/resource_job_state.cpp`, `src/sys/resource_job_state.h`, `include/wormedit/core/resource_file_copy.h`, `src/core/resource_file_copy.cpp`, `tests/core/resource_file_copy_tests.cpp`, `tests/sys/project_save_paths_tests.cpp`, `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`, `tests/CMakeLists.txt`, this card.
- **Supporting-file extensions:** `src/sys/project_save_paths.h` exposes the save worker/report used by the application and tests. `include/wormedit/core/resource_file_copy.h` and `src/core/resource_file_copy.cpp` add opt-in reuse of any identical file in `assets/` at the existing byte-comparison owner. `tests/CMakeLists.txt` links the shared save worker into the three existing resource-runtime test targets.
- **Steps:**
  1. Every save copies every asset outside the project folder into `assets/` and relinks it, silently; identical files are reused so a repeat save copies nothing; the status message lists what was copied.
  2. Reuse the existing resource copy job (atomic copy, relink, reuse of identical files) after the native file is written.
  3. Tests: audio-started project saved elsewhere ends with relative asset paths and copied files; a second save copies nothing new.
- **Invariants:** Save stays atomic for the project file; a failed copy never loses the link.
- **Reject/failure behavior:** A copy failure reports which file and leaves the external link in place.
- **Verify:** GATE; Xvfb save and inspect the project folder.
- **Out of scope:** Moving or deleting the originals.

## T-716 Correct the manual after the blockers land

- **Status:** blocked-on T-707, T-708, T-709, T-710, T-711, T-715
- **Outcome:** The manual chapters match the fixed behavior; the corrections listed in both walkthrough reports that remain true after the fixes are applied, and the walkthrough chapters are rerun from a new project.
- **Read first:** both walkthrough reports ("Manual corrections").
- **Files owned:** `docs/manual/chapters/*.adoc`, `docs/manual/shots/`, `docs/manual/images/`, this card.
- **Steps:** 1. Apply the corrections that still hold. 2. Recapture affected shots from a new project, not the imported fixture.
- **Invariants:** Generated tables stay generated.
- **Reject/failure behavior:** Not applicable.
- **Verify:** `documentation.manual_*`; `scripts/manual-captures.sh --check-stale`; Asciidoctor `--failure-level WARN`.
- **Out of scope:** New chapters.
