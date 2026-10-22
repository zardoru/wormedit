# Project creation and export-target UX

[Back to the task-card index](../tasks.md).

These cards turn the reviewed 2026-10-19 mockups and
[output-groups audit](../../audits/output-groups-ux-2026-10-19.md) into executable work. The export
cards start with presentation over the existing model, then consolidate behavior, then migrate
persisted policy. The [new-project layout picker](../mockups/new-project-layout.svg) is independent.

## T-723 Present output groups as export-target cards

- **Status:** done
- **Completed:** The Project tab now renders bounded export-target cards with format badges,
  destinations, display-only chart chips, one-line status, direct Export and a stamped overflow
  menu. Create, edit, duplicate and destructive deletion use the shared modal lifecycle; Escape,
  stale revisions and reopened sessions publish nothing. The existing output-group action and core
  command remain the only mutation boundary. Focused project-view and frontend event-harness tests,
  GATE, affected-file complexity and clang-tidy pass; isolated Xvfb captures at 1600×900 and
  800×700 cover populated, empty, edit and deletion-confirmation states.
- **Outcome:** The Project tab calls the section **Exports** and presents each existing output group
  as the card in [output-groups.svg](../mockups/output-groups.svg): format badge, name, destination,
  chart chips and one status line; **Edit**, **Preview**, **Duplicate** and **Delete…** live in its
  overflow menu, and create/edit/delete use the shared modal-dialog rules instead of the trailing
  inline form and deletion checkbox.
- **Read first:** `docs/audits/output-groups-ux-2026-10-19.md`; `src/ui/nuklear/setup_panel.cpp`
  (`render_output_groups`); `src/ui/nuklear/dialog.h`; `src/ui/frontend/project.h`;
  `src/sys/project_view.cpp` (`build_output_group_view`).
- **Files owned:** `src/ui/nuklear/setup_panel.cpp`, `src/ui/frontend/project.h`,
  `src/sys/project_view.cpp`, `tests/sys/project_view_tests.cpp`,
  `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp`,
  `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`,
  `docs/manual/chapters/walkthrough-bms.adoc`,
  `docs/manual/chapters/walkthrough-osu-stepmania.adoc`, this card and
  `memory/chart-editor-status/resume.md`.
- **Owned-file extension:** The two walkthrough event-harness files are included because they
  exercised the retired inline Edit/Preview controls and the former multi-line row preview; they
  now verify the card overflow route and the intentional one-line status.
- **Steps:**
  1. Stale-plan check: search for `Output groups`, `render_output_group_row` and
     `Confirm group deletion`; if cards and a modal already replace all three, record the owning
     implementation and stop.
  2. Preserve `WormUiProjectOutputGroupAction` and `apply_set_output_group_command` as the mutation
     boundary. Project the presentation facts a card needs; do not inspect the document from UI.
  3. Replace the seven-column row with the mockup hierarchy. A chart chip is display-only on this
     card; clicking it does not change membership until T-727.
  4. Put secondary actions in one per-card overflow menu. **Duplicate** opens a create draft with a
     distinct default name and the source target's family, path and membership.
  5. Use a shared modal for create/edit and the standard destructive confirmation for deletion.
     Escape cancels, stale revision/session closes without publishing, and Save emits one existing
     create/update action.
  6. Update the two walkthroughs only for names and controls changed by this card.
- **Invariants:** The stored `output_group_t`, format/path validation, Preview and Export behavior,
  chart membership, document revision, one Undo step and published files are unchanged. The UI
  retains the existing 256-row bound and reports truncation.
- **Reject/failure behavior:** Invalid or stale drafts retain the existing diagnostic and do not
  mutate the document. Closing or cancelling a dialog emits no action. Delete still keeps files
  already published.
- **Verify:** focused `project_view` and output-group event-harness tests; `GATE`; isolated Xvfb at
  1600×900 and 800×700 covering populated, empty, edit and delete-confirmation states; inspect the
  PNGs against the mockup.
- **Out of scope:** Export overwrite behavior (T-724), persisted StepMania policy (T-725), changing
  destination semantics or preview contents (T-726), membership editing (T-727), and persisted
  last-export timestamps.

## T-724 Give each export target one export action and existing-file policy

- **Status:** done
- **Completed:** Export targets persist Ask/Replace/Keep, expose saved and one-run actions, and
  share one background preflight/confirmation path from cards and File → Export. Export all
  prepares every target before the first publication and qualifies batch results by target. Native
  schema 7 migrates older targets to Ask. Focused core/sys/UI/native tests, GATE, affected-file
  complexity and clang-tidy pass; isolated Xvfb captures cover Ask, Replace and Keep tooltips plus
  Export all.
- **Outcome:** Every export-target card has one split **Export** action: its main button follows a
  saved **Ask / Replace / Keep** existing-files policy, while the menu can choose Replace or Keep
  for one run; the Project header has **Export all**, and the duplicate header/dialog export path is
  removed. File → Export reaches the same target choice and policy.
- **Read first:** `src/ui/nuklear/setup_panel.cpp`; `src/ui/nuklear/compose_resources.cpp`;
  `src/sys/compose_resource_actions.cpp` (`export_compose_output_group_action_t`);
  `src/sys/output_group_export.h`; `include/wormedit/formats/conversion.h`.
- **Files owned:** `include/wormedit/core/document.h`, `include/wormedit/formats/conversion.h`,
  `src/core/chart_editor.cpp`, `src/ui/frontend/project.h`, `src/ui/frontend/actions.h`,
  `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/compose_resources.cpp`,
  `src/sys/project_view.h`, `src/sys/project_view.cpp`, `src/sys/compose_resource_actions.cpp`,
  `src/sys/output_group_export.h`, `src/sys/output_group_export.cpp`,
  `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`,
  `tests/core/chart_editor_tests.cpp`, `tests/formats/native_project_tests.cpp`,
  `tests/sys/project_view_tests.cpp`, `tests/sys/resource_notification_tests.cpp`,
  `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card
  and `memory/chart-editor-status/resume.md`.
- **Supporting-file extensions (2026-10-19):** `src/sys/resources/resource_job_state.h` and
  `src/sys/resources/resource_job_state.cpp` own the background export lifecycle and therefore retain the
  prepared Ask-confirmation request; `src/sys/application.cpp` projects that lifecycle state and
  supplies it to dispatch; `src/ui/frontend/compose.h`, `src/ui/nuklear/compose_state.h`,
  `src/sys/workspace_view.cpp`, `src/ui/nuklear/compose_input_queries.h`, and
  `src/ui/nuklear/frontend.cpp` carry the existing global File → Export chooser/confirmation
  surface. These extensions are required to keep conversion and filesystem preflight off the UI
  thread while using the same workflow from every project tab. `src/formats/native_project_storage.cpp`
  and `tests/formats/native_project_storage_tests.cpp` advance the recovery envelope's supported
  native schema and its persistence expectations;
  `tests/ui/dock_shortcut_event_harness_tests.cpp` and
  `tests/ui/walkthrough_bms_event_harness_tests.cpp` exercise the same changed chooser/card labels.
  `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp` covers the changed Ask-conflict
  result in the existing production export walkthrough; `tests/sys/compose_action_notification_tests.cpp`
  verifies the production background-export messages and conversion-loss publication result;
  `docs/manual/chapters/walkthrough-bms.adoc` and
  `docs/manual/chapters/walkthrough-osu-stepmania.adoc` describe the consolidated workflow.
- **Steps:**
  1. Stale-plan check: trace every producer of `WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP` and
     `document.export_output_group`; record which ones ask for or force overwrite.
  2. Add one typed existing-files policy to `output_group_t` and its native serialization. Older
     projects migrate to **Ask**, preserving today's explicit choice before overwrite.
  3. Carry the saved or one-run policy to `output_group_export_plan_t::from`; **Keep** maps to the
     current exclusive publication, **Replace** to overwrite, and **Ask** preflights conflicts
     before opening one confirmation.
  4. Render the per-target split action and **Export all**. Export all prepares every target before
     starting publication and reports target-qualified progress/results.
  5. Route File → Export through the same chooser and remove the second Project-header chooser.
  6. Cover Ask/no-conflict, Ask/conflict/cancel, one-run override, Replace, Keep and all-target
     preflight with production-path tests.
- **Invariants:** Conversion requests, loss policy, atomic per-target publication, cancellation,
  target membership and asset-copy naming remain unchanged. A one-run choice never changes the
  saved policy.
- **Reject/failure behavior:** Ask never writes before confirmation. Keep refuses an existing
  destination without changing it. Export all starts no publication if any target cannot be
  prepared or a required Ask confirmation is cancelled.
- **Verify:** focused output-group core/sys/UI/native tests; `GATE`; affected clang-tidy and
  complexity checks; isolated Xvfb for each policy and Export all.
- **Out of scope:** Per-target StepMania options (T-725), destination derivation and expanded file
  preview (T-726), cross-target atomic rollback, and remembering last-export results.

## T-725 Store StepMania export policy on each export target

- **Status:** done
- **Completed:** StepMania SM/SSC, timing-syntax and implementation choices now belong to each
  export target and travel through the same preview/export plan. The global setup policy and
  application-lifetime state are removed. Native schema 8 migrates old targets, compatible new
  projects create one explicit SSC target, and target edits remain one undoable transaction. GATE,
  affected clang-tidy and complexity pass; isolated Xvfb shows two targets with independent editor
  values.
- **Outcome:** A StepMania export target stores its SM/SSC file profile, timing syntax and target
  implementation; its edit dialog shows those fields, the separate project-wide StepMania policy
  section is removed, and new compatible projects create one StepMania target instead of parallel
  SM and SSC groups.
- **Read first:** `include/wormedit/formats/conversion.h` (`stepmania_export_policy_t`);
  `include/wormedit/core/document.h` (`output_group_t`); `src/sys/output_group_export.h`;
  `src/core/lane_profiles.cpp` (`mania_step_outputs`); `src/ui/nuklear/setup_panel.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `include/wormedit/core/lane_profiles.h`,
  `include/wormedit/formats/conversion.h`, `src/core/lane_profiles.cpp`,
  `src/ui/frontend/project.h`, `src/ui/nuklear/setup_panel.cpp`, `src/sys/project_view.cpp`,
  `src/sys/output_group_export.h`, `src/sys/workspace_view.cpp`,
  `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`,
  `tests/core/lane_profile_tests.cpp`, `tests/formats/native_project_tests.cpp`,
  `tests/sys/application_defaults_tests.cpp`, `tests/sys/application_commands_tests.cpp`,
  `tests/sys/project_view_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`,
  `docs/manual/chapters/walkthrough-bms.adoc`,
  `docs/manual/chapters/walkthrough-osu-stepmania.adoc`, this card and
  `memory/chart-editor-status/resume.md`.
- **Supporting-file extensions (2026-10-19):** `src/formats/native_project_storage.cpp` and
  `tests/formats/native_project_storage_tests.cpp` advance recovery support with native schema 8;
  `src/sys/application.cpp`, `src/sys/application_edit_inputs.h`,
  `src/sys/application_frame_view.{h,cpp}`, `src/sys/compose_resource_actions.cpp`,
  `src/sys/project_view.h`, and `src/sys/transport_player_actions.{h,cpp}` remove the obsolete
  application-lifetime export policy and pass target-owned policy through the existing routes.
  `src/ui/frontend/actions.h`, `src/ui/nuklear/setup_state.{h,cpp}` and
  `tests/ui/nuklear_setup_state_tests.cpp` move validation from the retired global form to target
  rows. `tests/sys/resource_notification_tests.cpp`, `tests/sys/frame_projection_arena_tests.cpp`,
  `tests/sys/application_dispatch_fixture.h`, `tests/ui/menu_bar_event_harness_tests.cpp`,
  `tests/ui/popup_menu_event_harness_tests.cpp`, and the two walkthrough event-harness files follow
  those changed projection/dispatch contracts and labels.
- **Migration fallback:** Schema-7 targets whose path is exactly `sm` or has a `.sm` extension
  become SM; every other StepMania path becomes SSC. Timing defaults to native WARPS and the
  implementation remains syntax-only/unspecified, matching the former application-session
  defaults. A nondefault runtime-only session choice was never persisted and therefore cannot be
  recovered from an older project.
- **Steps:**
  1. Stale-plan check: locate every read/write of the application-lifetime
     `stepmania_export_policy`; classify export, preview and unrelated preference uses.
  2. Add an optional StepMania policy owned by `output_group_t`, validate it only for StepMania
     targets and serialize it with the next additive native schema migration.
  3. Migrate older StepMania targets by deriving SM/SSC as today and copying the old project/session
     default for the remaining fields; record the exact fallback when no saved policy exists.
  4. Make preview and export read the target's policy. Remove the Project tab's global policy form
     and any export-only application state it leaves unused.
  5. Change compatible new-project descriptors to one StepMania target with an explicit default
     policy, without changing osu!mania or other targets.
  6. Test old-project migration, new-project defaults, independent policies on two targets,
     Preview/Export equality and one Undo of an edit.
- **Invariants:** StepMania conversion output for an unchanged migrated project is byte-equivalent;
  non-StepMania groups ignore the optional policy; target edits remain one transaction and Undo.
- **Reject/failure behavior:** A missing or invalid StepMania policy rejects before conversion or
  publication and names the target. Unsupported policy values fail native loading validation.
- **Verify:** native migration and StepMania conversion/publication tests; focused project and
  wizard event-harness tests; `GATE`; isolated Xvfb editing two StepMania targets.
- **Out of scope:** Program-wide StepMania defaults, changing the writer's supported syntax,
  destination derivation (T-726), and timing sharing (T-730).

## T-726 Choose typed destinations and show the complete export plan

- **Status:** done
- **Outcome:** An export target chooses a folder for one-file-per-chart formats and a song file for
  StepMania; WormEdit derives and displays every chart, generated and copied-asset destination, and
  Preview expands the card into per-file **new / replace / reuse / copy** rows with byte sizes plus
  every conversion diagnostic before publication.
- **Read first:** `src/sys/output_group_export.h` (`output_group_export_preparation_t`);
  `src/formats/conversion_publication.cpp`; `src/sys/project_view.cpp` (`output_preview`);
  `src/ui/nuklear/setup_panel.cpp`; `src/sys/project_save_paths.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/chart_editor.cpp`,
  `src/formats/conversion_publication.cpp`, `src/ui/frontend/project.h`,
  `src/ui/frontend/actions.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/setup_panel.cpp`,
  `src/sys/application.cpp`, `src/sys/output_group_export.h`,
  `src/sys/output_group_export.cpp`, `src/sys/project_view.h`, `src/sys/project_view.cpp`,
  `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`,
  `tests/core/chart_editor_tests.cpp`, `tests/formats/conversion_publication_tests.cpp`,
  `tests/formats/native_project_tests.cpp`, `tests/sys/project_view_tests.cpp`,
  `tests/sys/resource_notification_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card
  and `memory/chart-editor-status/resume.md`.
- **Supporting-file extensions (2026-10-19):**
  `include/wormedit/core/resource_file_copy.h`,
  `include/wormedit/formats/conversion_publication.h`, `src/core/lane_profiles.cpp`,
  `src/core/resource_file_copy.cpp`, `src/formats/conversion_publication_storage.cpp`,
  `src/formats/native_project_storage.cpp`, `src/sys/metadata/metadata_view.cpp`,
  `src/sys/workspace_view.cpp`, `src/tools/wormedit_convert.cpp`,
  `src/ui/frontend/runtime.h`, and `src/ui/nuklear/setup_panel.h` carry the typed destination
  through existing asset, storage, projection, CLI and picker boundaries. The corresponding
  storage/default tests plus `tests/sys/compose_action_notification_tests.cpp`,
  `tests/sys/frame_projection_arena_tests.cpp`, and both walkthrough event-harness files update
  fixtures that previously inferred file versus folder from an extension. The reviewed
  `cmake/complexity_baseline.txt` changes lower three existing test-function scores; no allowance
  increases.
- **Migration fallback:** Schema-8 directory-format paths retain the same folder. StepMania and
  native paths become file destinations; an old extensionless folder target appends exactly the
  filename the old publication plan derived, so the resolved output path is unchanged. Explicit
  `.sm`, `.ssc`, and `.wormedit` paths remain unchanged.
- **Steps:**
  1. Stale-plan check: inventory how each format interprets `relative_output_path` and how
     `conversion_publication_plan_payload_t` already classifies destinations.
  2. Define one typed destination shape on the target: directory for BMS/BMSON/osu!mania and file
     for StepMania/native. Migrate existing safe paths without changing their resolved outputs.
  3. Add **Save to** and **Browse…** to the target dialog with format-specific help and a live
     resolved-file summary. New targets default to `exports/<format>/` or an explicit song file.
  4. Project the preparation's planned files, byte sizes, assets, conflicts and complete conversion
     diagnostics as owned view data; do not parse the existing preview-status string in the
     frontend.
  5. Expand/collapse the plan below its target card and label every row by action. Preview and
     Export must consume the same preparation.
  6. Test every format's path shape, migration, collision state, two same-basename assets,
     cancellation and Preview/Export path equality.
- **Invariants:** Paths remain project-relative and traversal-safe; conversion bytes and atomic
  publication are unchanged; planning performs no writes; bounded view limits report truncation.
- **Reject/failure behavior:** Invalid or incompatible paths reject at edit time. Uninspectable or
  conflicting destinations appear in Preview and prevent publication without overwriting files.
- **Verify:** focused conversion-publication, output-group and project-view tests; `GATE`; affected
  clang-tidy/complexity; isolated Xvfb with BMS and StepMania targets and inspected plan rows.
- **Out of scope:** Renaming converted charts, cross-target deduplication, persisted export history,
  and changing asset-copy conflict policy.

## T-727 Edit compatible target membership and improve the empty state

- **Status:** done
- **Completed:** One lane-profile compatibility query now owns per-family membership eligibility
  and plain refusal reasons, including stable-layout and BMS segment checks. Target editors, card
  chips and chart-row *Exports* checklists publish the same complete revision-checked group update;
  incompatible choices remain visible and disabled, while core validation rejects a newly
  incompatible whole update atomically. Empty Exports offers the selected layout's normal new-
  project targets as uncommitted drafts. The BMS and osu!/StepMania manual workflows describe both
  editing directions. GATE passes 4,251/4,251 (`/tmp/wormedit-gate.pjtTdL`); affected clang-tidy
  and complexity pass. Isolated Xvfb captures cover the suggested empty state and a mixed-
  compatibility draft.
- **Outcome:** Target edit dialogs disable incompatible charts with a plain reason, target cards
  add/remove compatible charts through their chips, the chart table's Exports cell opens the same
  membership checklist, and an empty Exports section offers suggested targets from the active
  layout instead of a blank generic form.
- **Read first:** `src/ui/nuklear/setup_panel.cpp`; `src/sys/project_view.cpp`;
  `src/core/chart_editor.cpp` (`apply_set_output_group_command`);
  `src/core/lane_profiles.cpp` (`default_output_groups`); `src/formats/conversion.cpp`.
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`,
  `src/core/chart_editor.cpp`, `src/ui/frontend/project.h`, `src/ui/nuklear/setup_panel.cpp`,
  `src/sys/project_view.cpp`, `tests/core/chart_editor_tests.cpp`,
  `tests/core/lane_profile_tests.cpp`, `tests/sys/project_view_tests.cpp`,
  `tests/ui/nuklear_event_harness_tests.cpp`, `docs/manual/chapters/walkthrough-bms.adoc`,
  `docs/manual/chapters/walkthrough-osu-stepmania.adoc`, this card and
  `memory/chart-editor-status/resume.md`.
- **Supporting-file extensions (2026-10-19):** `src/sys/project_view.h` owns the projected
  compatibility strings and suggestion storage; `src/formats/conversion.cpp` consumes the public
  BMS layout query instead of retaining a second compatibility fact. `src/sys/application.cpp`
  supplies the selected chart before building suggestions. The BMS walkthrough harness fixture
  declares its projected compatibility after the view contract gained the mask and reasons.
- **Steps:**
  1. Stale-plan check: trace current format/layout compatibility checks and every Project-tab
     membership display/edit path; choose one typed compatibility query owner.
  2. Project each chart's compatibility and reason for the draft target family. Disable rather than
     hide incompatible charts.
  3. Make chip removal, **+** addition and the chart-table Exports checklist build the same complete
     `output_group_t` update and publish through `apply_set_output_group_command`.
  4. Build empty-state suggestions from `new_project_layout_descriptor_t::default_output_groups`;
     creating one opens a normal target draft and does not commit until Save.
  5. Update the manual for the final target and membership workflow.
- **Invariants:** Membership changes are one revision-checked transaction and Undo; target order,
  names, paths and policies are retained; incompatible existing membership remains visible until
  the user edits it.
- **Reject/failure behavior:** A stale, deleted or newly incompatible chart rejects the whole
  membership update with its reason. No partial membership change is published.
- **Verify:** core compatibility and command tests; project-view and event-harness tests from both
  editing sides; `GATE`; isolated Xvfb for mixed-compatible charts and the empty state.
- **Out of scope:** Automatic removal of existing incompatible membership, automatic creation on
  project load, destination rules (T-726), and layout-picker presentation (T-728).

## T-728 Replace the new-project layout combo with a grouped picker

- **Status:** done
- **Completed:** The Create project dialog now chooses from a short game row, lane-shape cards,
  group-local variants and a role-colored lane preview. Explicit descriptor metadata drives the
  groups without parsing labels, while stable catalog IDs remain the only construction key and
  preserve the manual/audio defaults. Selection is retained by ID across group changes and
  resizing; bounded keyboard navigation and disappearing-ID rejection are covered. Focused core,
  projection, defaults, walkthrough and event-harness tests pass; GATE passes 4,255/4,255
  (`/tmp/wormedit-gate.mAvtIq`). Isolated Xvfb captures cover all five game families at 1600×900
  and the picker at 800×700.
- **Outcome:** The Create project dialog matches
  [new-project-layout.svg](../mockups/new-project-layout.svg): a short game list, layout cards grouped
  by lane shape, variant toggles and a lane preview replace the roughly 60-entry combo, while the
  chosen stable catalog ID still drives project construction.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_new_project_wizard`);
  `src/ui/frontend/workspace.h`; `src/sys/workspace_view.cpp` (`new_project_layout_view`);
  `include/wormedit/core/lane_profiles.h`; `src/core/lane_profiles.cpp` (`new_project_layouts`).
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`,
  `src/ui/frontend/workspace.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/resources/catalog_view.h`,
  `src/sys/workspace_view.cpp`, `tests/core/lane_profile_tests.cpp`,
  `tests/sys/application_defaults_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card
  and `memory/chart-editor-status/resume.md`.
- **Supporting-file extensions (2026-10-19):** `tests/sys/frame_projection_arena_tests.cpp`
  verifies the typed catalog projection. `tests/ui/walkthrough_bms_event_harness_tests.cpp` and
  `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp` supply the new typed picker view and
  exercise its replacement controls. `cmake/complexity_baseline.txt` records the installed
  clang-tidy 23.1.3 full 595-translation-unit remeasurement and removes one stale allowance; no
  allowance increased.
- **Steps:**
  1. Stale-plan check: count catalog entries and inventory stable IDs, labels, lane roles, variants,
     step types, bindings and default outputs; do not infer grouping by parsing display labels.
  2. Extend the catalog descriptor and backend-neutral view with explicit game, lane-shape group,
     variant label and preview lane roles. Keep `id` as the only construction key.
  3. Render the game list, grouped layout cards, applicable variants and lane preview. Selection
     must survive resizing and switching between groups; keyboard navigation and focus are bounded.
  4. Preserve the current manual/audio-start defaults by stable ID and emit the selected catalog
     ID through the existing workspace action.
  5. Test every descriptor's grouping metadata, one selection per game family, variant switching,
     keyboard navigation and project defaults after creation.
- **Invariants:** `make_initial_document`, `make_audio_project_document`, lane IDs/roles/bindings,
  source family, default export targets and project-path behavior are unchanged for a chosen ID.
- **Reject/failure behavior:** Missing or inconsistent grouping metadata fails catalog validation;
  the Create button stays disabled if the selected ID disappears. Cancel/Escape creates nothing.
- **Verify:** application-default and wizard event-harness tests; `GATE`; isolated Xvfb at 1600×900
  and 800×700 covering osu!mania, BMS, PMS and StepMania families.
- **Out of scope:** Adding layouts, changing profile semantics/default bindings, editing export
  targets in the wizard, and per-target policy migration (T-725).
