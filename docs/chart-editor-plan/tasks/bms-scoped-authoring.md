# Task cards for BMS scope node authoring

[Back to cards](../tasks.md). Contract: [BMS scoped program authoring](../editing/bms-scoped-program.md).
Prerequisites are in [model and migration](bms-scoped-model.md). Questions 21–24 are
explicit gates, not permission to invent a destructive default. T-744 owns public activation.

## T-738 Add Control Chart and Text nodes in selected scopes

- **Status:** blocked-on-user
- **Prerequisites:** T-737; [question 22](../decisions/open-questions.md) for non-body control directives.
- **Outcome:** typed commands insert valid control scopes, Chart children and opaque Text at explicit ordered positions, including an empty/new Root.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/bms_program.h`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`.
- **Files owned:** `include/wormedit/core/bms_program.h`, `src/core/bms_program.cpp`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, new `tests/core/bms_program_authoring_tests.cpp`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. STOP until question 22 is answered and prerequisites pass; use its explicit distinction between editable scopes and grammar markers.
  2. Add typed insertion requests with stable parent/anchor IDs and grammar-aware factories; every new scope receives its first empty Chart automatically.
  3. Add bounded Text insertion/editing through the existing parser classification; refuse attempts to hide modeled/control commands in opaque Text without a semantic owner.
  4. Test empty Root, nested RANDOM/IF and supported switch/fixed forms, several Chart siblings, ordered comments, invalid parent/anchor, limits and one-step Undo/Redo.
- **Invariants:** no selected raw line is required; existing content and siblings are untouched; every surviving scope has a Chart child.
- **Reject/failure behavior:** stale targets, invalid syntax/placement or encoding failure reject atomically with a named reason.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** RANDOM range changes (T-739), movement/deletion/unwrap (T-740–T-742), widgets (T-743).

## T-739 Edit RANDOM outcome ranges without implicit arm edits

- **Status:** blocked-on-user
- **Prerequisites:** T-738; [question 21](../decisions/open-questions.md).
- **Outcome:** a revision-checked command edits RANDOM's positive outcome range independently of child count and preview selection.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/bms_program.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, this card.
- **Steps:**
  1. STOP until question 21 defines shrinking past existing arms and the current preview value.
  2. Add the scalar parameter edit with stable node identity, positive supported bounds and the approved reachability/selection diagnostics.
  3. Test grow, unchanged value, invalid value, shrink, sparse IF values, nested RANDOMs, export/reimport and Undo/Redo.
- **Invariants:** changing `n` does not add/delete/renumber IF arms, Text or Chart children; parameter and preview outcome are not conflated.
- **Reject/failure behavior:** invalid/stale/refused edits preserve parameter, descendants, preview state and history.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** automatic probability balancing or outcome enumeration changes (not requested); direct controls in the panel (T-743).

## T-740 Move whole program subtrees in ordered scopes

- **Status:** blocked-on T-738
- **Outcome:** one transaction moves a Control, Chart or Text subtree to an explicit valid parent/position while retaining its identities and ordered content.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/bms_program.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, `docs/references/bms-control-flow.md`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `src/core/bms_program.cpp`, `tests/core/bms_program_authoring_tests.cpp`, this card.
- **Steps:**
  1. Add an ordered move proposal identifying source subtree, destination scope and insertion anchor; describe affected definitions/projections before applying it.
  2. Reject cycles, invalid grammar, a surviving scope losing its last Chart, unresolved resources and invalid affected projections; do not silently rebind selectors or rebase rows.
  3. Test sibling reorder and legal reparenting with comments, nested controls, holds and scoped definitions, plus rejection atomicity and Undo/Redo.
- **Invariants:** Text moves with its block, not with an unrelated source-line index; unchanged nodes keep IDs, values and musical positions.
- **Reject/failure behavior:** a move with unprovable/invalid effects publishes neither a partial reparent nor cache/selection changes.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** copying/moving individual selected notes (T-93), drag/drop gestures (panel commands in T-743 provide the first UI).

## T-741 Delete scoped subtrees through explicit confirmation

- **Status:** blocked-on T-740
- **Outcome:** confirmed deletion removes exactly the named subtree and its Chart/Text contents, while Cancel and stale confirmation leave the program intact.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `src/sys/compose/actions/compose_measure_commands.cpp`, `tests/core/bms_program_authoring_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, this card.
- **Steps:**
  1. Prepare a typed deletion summary with stable subtree identity, revision and counts of descendant scopes, Charts, Text and semantic content.
  2. Apply only the confirmed proposal through the shared transaction; remove descendants, validate affected projections and resolve a deleted current target to the surviving parent's first Chart.
  3. Refuse Root removal and removal of a surviving scope's final Chart; retain assets used elsewhere and perform no implicit asset garbage collection.
  4. Test nonempty nested deletion, unrelated siblings, cancellation, stale confirmation, target repair and full one-step Undo/Redo.
- **Invariants:** deleting a control does not unwrap/promote children; no dangling owner/selection/resource references remain.
- **Reject/failure behavior:** failed/canceled confirmation publishes nothing; a structural failure rolls back the whole subtree change.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** confirmation widgets (T-743), unwrapping (T-742), project difficulty deletion (existing chart-management workflow).

## T-742 Unwrap controls as an explicit structural operation

- **Status:** blocked-on-user
- **Prerequisites:** T-741; [question 23](../decisions/open-questions.md).
- **Outcome:** a confirmed valid unwrap replaces one wrapper with its ordered children without deleting content or implicitly changing selector bindings.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_program_authoring_tests.cpp`, this card.
- **Steps:**
  1. STOP until question 23 defines the supported selector-unwrapping behavior; never infer whole-program flattening from the word unwrap.
  2. Propose the literal ordered child promotion and its reachability, definition and collision effects; require confirmation before an apply.
  3. Validate grammar, surviving Chart invariants and every affected bounded projection; retain child IDs and positions.
  4. Test valid body-scope unwrap, selector cases under the approved policy, Text order, canceled/stale confirmation, collisions and Undo/Redo.
- **Invariants:** unwrap is not delete, implicit renumbering, sibling coalescing, or a hidden active-projection export.
- **Reject/failure behavior:** invalid/orphaned/rebound conditional structure or unresolved content rejects without changing the wrapper or any child.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program-authoring]'`.
- **Out of scope:** lossy branch flattening (existing explicit export policy), widgets (T-743).

## T-743 Present the branch panel as a scope authoring tree

- **Status:** blocked-on-user
- **Prerequisites:** T-738–T-742; [question 24](../decisions/open-questions.md).
- **Outcome:** the panel exposes Root, control scopes, Chart and Text nodes, indicates the exact edit target, and offers validated node commands with distinct Delete/Unwrap confirmations.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `src/ui/nuklear/project/bms_branch_panel.cpp`, `src/sys/project/workspace_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** new `src/ui/frontend/bms_program.h`, `src/ui/frontend/compose.h`, `src/ui/frontend/actions.h`, `include/wormedit/core/workspace.h`, `src/core/workspace.cpp`, `src/sys/project/workspace_view.h`, `src/sys/project/workspace_view.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/actions/compose_actions.cpp`, new `src/sys/bms_program_runtime.h`, new `src/sys/bms_program_runtime.cpp`, `src/ui/nuklear/project/bms_branch_panel.h`, `src/ui/nuklear/project/bms_branch_panel.cpp`, new `tests/ui/bms_program_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. STOP until question 24 resolves scope selection versus preview choices; migrate the touched branch view/action boundary to C++ values with one runtime owner for session target/drafts.
  2. Draw the ordered scope tree and exact target path; selecting controls targets first Chart, selecting another Chart overrides it, and meaningful commands do not appear as raw-statement scope rows.
  3. Expose Add, move, parameter edit, Text editing, Delete and Unwrap through typed core capabilities; RANDOM count and preview value have separate labels/actions, and destructive actions show their prepared summaries.
  4. Cover new/empty Root without retained source, nested scope/alternate-Chart targeting, keyboard capture isolation, resizing, Cancel, stale targets and Undo/Redo in the event harness; inspect isolated Xvfb at normal and 800×700 sizes.
- **Invariants:** frontend widgets do not own program semantics or mutations; missing targets never write to a hidden leaf or silently fall back to Root.
- **Reject/failure behavior:** show typed diagnostics, keep rejected drafts recoverable and release capture; no data change before confirmation.
- **Verify:** GATE; `build/tests/wormedit_frontend_event_harness_tests '[bms-scope]'`; isolated `scripts/smoke-compose-virtual-x.sh /tmp/bms-scope.png compose` with the nested native fixture prepared by the card, then inspect the PNG.
- **Out of scope:** final public-owner activation/old adapter deletion (T-744), optional inactive ghost overlays or drag/drop trees (not required for this first UI).

## T-744 Activate scoped ownership and retire raw statement authority

- **Status:** blocked-on T-743
- **Prerequisites:** T-731–T-743, including the native migration and all product answers.
- **Outcome:** public BMS import/new-project/load/edit/save/export use one scoped program owner, with no independently writable retained-statement semantic graph or legacy panel adapter.
- **Read first:** `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/audits/bms-scoped-ownership.md`, `src/core/bms_branch_editor.cpp`, `src/formats/native_project_migrations.cpp`, `src/ui/nuklear/project/bms_branch_panel.cpp`.
- **Files owned:** `docs/chart-editor-plan/tasks/bms-scoped-model.md`, `docs/chart-editor-plan/tasks/bms-scoped-authoring.md`, `docs/chart-editor-plan/editing/bms-scoped-program.md`, `docs/chart-editor-plan/tasks/bms-branches.md`, `docs/chart-editor-plan/tasks/compose-docks.md`, `memory/chart-editor-status/resume.md`, `src/core/bms_program.cpp`, `src/core/bms_branch_editor.cpp`, `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/sys/app/application_defaults.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/sys/project/workspace_view.cpp`, `src/ui/nuklear/project/bms_branch_panel.cpp`, `tests/core/bms_program_edit_tests.cpp`, `tests/formats/bms_program_migration_tests.cpp`, `tests/formats/bms_program_writer_tests.cpp`, `tests/ui/bms_program_event_harness_tests.cpp`, `include/wormedit/core/bms_branch_editor.h`, `include/wormedit/core/bms_branch_view.h`, `include/wormedit/formats/bms.h`, `include/wormedit/formats/bms_branch_selection.h`, `src/core/bms_branch_view.cpp`, `src/formats/bms_branch_selection.cpp`, `src/core/command.cpp`, `src/core/chart_editor.cpp`, `src/core/id_ceiling.h`, `src/core/chart_object_ids.h`, `src/core/interaction.cpp`, `src/core/interaction_internal.h`, `src/core/resource_assignment.cpp`, `src/core/quantize.cpp`, `src/core/channel_map_editor.cpp`, `src/core/lane_migration.cpp`, `src/core/measure_editor.cpp`, `src/core/bpm_editor.cpp`, `src/core/pause_warp_editor.cpp`, `src/core/time_shift.cpp`, `src/core/timing_reposition.cpp`, `src/core/timing_links.cpp`, `src/core/workspace.cpp`, `src/formats/conversion.cpp`, `src/formats/osu_mania.cpp`, `src/sys/compose/actions/compose_action_outcome.cpp`, `src/ui/frontend/actions.h`, `src/ui/frontend/compose.h`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/project/bms_branch_panel.h`, `tests/core/bms_branch_editor_tests.cpp`, `tests/core/chart_editor_tests.cpp`, `tests/core/timing_links_tests.cpp`, `tests/sys/application_defaults_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/bms_writer_tests.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/native_project_tests.cpp`, `tests/core/command_journal_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp`, `src/core/chart_import.cpp`, `src/core/sound_import.cpp`, `tests/core/chart_import_tests.cpp`, `tests/core/sound_import_tests.cpp`.
- **Cutover inventory (T-731, 2026-10-23):** retire the exact functions and authority listed in the [one-way transition audit](../audits/bms-scoped-ownership.md#one-way-transition-and-public-cutover), including source-order guards and non-BMS conversion loss checks. Preserve refusals until replacement capabilities are verified. The supporting files above cover new/add/duplicate difficulty, all inactive IDs, legacy statement/selection APIs, panel feedback, migration/recovery and export; no implicit widening of supported dialects or conditional timing links.
- **Steps:**
  1. Activate the validated public import/native schema/new-project paths and scoped edit routing together; reconcile old command callers through the audited migration, never two writable authorities.
  2. Remove every temporary T-744 adapter/guard and the retained raw-statement panel as a semantic editing entry; retain read-only provenance and opaque Text where still required.
  3. Run the full contract fixture matrix across import, alternate outcomes, edits, node operations, native reload and default BMS export; verify untouched non-BMS documents and existing project difficulties.
  4. Reconcile historical T-90–T-93/T-287/T-399 statuses only against demonstrated outcomes; update the resume line with real gate/visual evidence and no premature completion claims.
- **Invariants:** program data has one authoritative owner; source caches and projected charts are derived; stable identities and every inactive outcome survive migration.
- **Reject/failure behavior:** failed migration, unsupported scoped content or an incomplete command-family adapter blocks cutover instead of discarding data or enabling partial production authoring.
- **Verify:** GATE; `build/tests/wormedit_core_tests '[bms-program],[bms-program-import],[bms-program-projection],[bms-program-writer],[bms-program-edit],[bms-program-migration],[bms-program-authoring]'`; `build/tests/wormedit_frontend_event_harness_tests '[bms-scope]'`; isolated Xvfb acceptance captures and native save/reload comparison.
- **Out of scope:** individual-note move/copy between scopes (T-93), opt-out malformed-flow repair proposals (T-287), new BMS dialects and an inactive ghost overlay (separate future work).
