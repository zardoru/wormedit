# Workflow navigation audit and proposed plan

Date: 2026-10-18. Source revision: `09f4b2a`. Scope: static tracing of thirteen
representative workflows through application commands, Compose input/actions,
editor effects, core edits and asynchronous file operations.

The main navigation cost is variation in how a workflow crosses owners. The shared
transaction and gesture machinery already provide a useful backbone. Several
workflows still hide orchestration in projection files, translate through frontend
payloads internally, or reconstruct execution outcomes from presentation feedback.
Improve those seams before reorganizing directories. File length is a useful place
to investigate, but does not establish that a file needs splitting.

The review sequence and the decision to run the user-facing walkthrough first were accepted by the
owner on 2026-10-18, as recorded below. Execution remains card-gated: the walkthrough blockers and
their integration follow-ups finish before T-658 establishes the navigation reference and baseline
measurement. Source references below use paths and symbols so searches survive later line movement.

## Scope and evidence

The sample deliberately contrasts direct commands, staged previews, pointer
gestures, resource review, view-only navigation and external publication. It is not
a complete call graph, a measured developer navigation benchmark, or a finding of
incorrect behavior. The observations establish concrete detours; their relative
cost is an engineering judgment to validate during the first small extraction.

The existing application build passed **4,168/4,168 CTest tests** at this source
revision during the preceding project assessment. That run used
`ctest --test-dir build -j 8 --output-on-failure`; it was not a fresh build or GATE.
Documentation validation for this addition is reported separately at delivery.

### Representative workflow inventory

Each row names the principal source landmarks, rather than every transitive call.
The last column distinguishes useful separation from avoidable searching.

| Workflow | Source landmarks in execution order | Navigation finding |
|---|---|---|
| Move selected notes to BGM | `ui/nuklear/compose_note_menu.cpp::emit_compose_menu_command` with `edit.move_to_bgm`; `sys/application_command_dispatch.cpp::note_to_bgm_command`; `core/note_to_bgm.cpp::move_editor_selection_to_bgm`, `move_notes_to_bgm`, `stage_move_notes_to_bgm` | Core mutation has a named owner. Availability is projected and registered in `sys/application.cpp`; editor selection completion and sys feedback require separate stops. |
| Move selected BGM to a playable lane | `sys/compose_note_to_bgm.cpp::move_selected_bgm_to_lane`; `core/interaction.cpp::assign_selected_objects_to_lane`; `core/note_to_bgm.cpp::move_selected_objects_to_lane`; stages including `core/interaction.cpp::stage_assign_note_lanes` | The sys adapter copies proposed selection, dispatches an editor action and adapts its result. Related conversion in the preceding row has a direct core editor wrapper. The selection policies differ; their orchestration styles also differ. |
| Delete mixed selection | `sys/application_command_dispatch.cpp::edit_commands`; `core/chart_clipboard.cpp::delete_selection_all`, `delete_selection_all_impl`; `core/interaction.cpp::stage_editor_selection_delete`; `core/interaction_edit_commit.h::commit_editor_edit` | Delete is owned publicly by a clipboard file. Mixed timing/span deletion still combines work through a temporary history before the final transaction. |
| Apply quantize from the keyboard | `sys/application_command_dispatch.cpp::compose_quantize_command`; `sys/compose_actions.cpp::invoke_compose_quantize_command`; `sys/compose_area_actions.cpp::apply_compose_quantize_selection`; `sys/compose_quantize_request.h::quantize_request_from_action`; core `apply_quantize_command` | The command constructs `WormUiWorkspaceAction` from typed settings, then reparses those settings. Capability, invocation, report storage and preview projection live in different files. |
| Begin and commit paste | `sys/application_command_dispatch.cpp::clipboard_commands`; `sys/compose_view.cpp::begin_compose_paste`, `revise_compose_paste`, `apply_compose_paste`; `sys/compose_input.cpp` paste input handler; `core/chart_clipboard.cpp::chart_paste_proposal_t`, `paste_chart_clipboard` | Session mutation, external clipboard decoding and commit orchestration live in a projection file. Ghost construction legitimately needs projection, but session ownership does not need that address. |
| Toggle a bar line at the cursor | `sys/application_command_dispatch.cpp::measure_command`; `sys/compose_measure_commands.cpp::apply_cursor_measure_split`; core `apply_measure_edit` with `split_measure_action_t` or `remove_bar_line_action_t` | The helper handles edit and cursor/selection effects but returns a string and output severity. Dispatch infers acceptance from severity rather than receiving the command outcome. |
| Drag notes | `sys/compose_input.cpp::begin_note_move_gesture_action_t` emission; `core/interaction.cpp::editor_action_handler_t`; `core/interaction_gesture_runner.h`; `core/interaction_gesture_lifecycle.cpp::pending_note_move_gesture_t`; `core/note_move.cpp::apply_note_move_plan` | The runner is predictable and reusable. Domain begin/revise/apply bodies remain in the file named for shared lifecycle, while planning and mutation live in the note-move file. |
| Drag selected timing markers | `sys/compose_timing_input.cpp::move_compose_timing_selection`; core `move_timing_lane_event_group` in `timing_lane_controller.cpp`; typed timing editors | Musical targeting and selection policy justify a specialized input path. Group execution retains temporary-history composition already recorded in the execution audit. |
| Confirm sound import | `ui/nuklear/sound_import_state.cpp` and `sound_import_dialog.cpp`; `sys/compose_sound_actions.cpp::confirm_compose_sound_import_action_t`; `core/sound_import.cpp::apply_bulk_sound_import` | Review state, payload admission and document import have legitimate separate lifetimes. The core batch still calls committing operations on a draft history and adopts its document. |
| Edit song metadata | `sys/metadata_actions.cpp::dispatch_song_edit`, `publish_metadata_edit`; `sys/metadata_commands.h::apply_metadata_edit`; core metadata editor | Ownership is reasonably searchable. Its local feedback/rebind boundary differs from Compose. Parsing-before-staleness and lock-before-staleness ordering are explicit contracts, not redundant checks to delete. |
| Navigate to a beat bookmark | `sys/application_command_dispatch.cpp::bookmark_commands`, `run_bookmark_command`; `sys/bookmark_commands.cpp::bookmark_navigation_t::navigate_beat_bookmark` | Named navigation owner is a useful example. Playback seeks and recent-seek tracking belong to this workflow; a document transaction would obscure it. |
| Open a project | `sys/application.cpp` start/poll/`finish_project_open`/`publish_opened_project`; `sys/project_open.cpp::project_open_runtime_t` and `open_project_file` | Detached preparation and UI-thread publication are explicit. The application publication boundary legitimately spans session replacement, recents and feedback. |
| Export an output group | `sys/output_group_export.h::output_group_export_plan_t`; `sys/resource_job_state.cpp::runtime::output_group_export_t`; `sys/output_group_export.cpp::run_output_group_export`; atomic conversion publisher | Plan, runtime and publication are distinct owners. The runtime's location is less discoverable than its type. Its publication receipt preserves completed exports across cancellation races and must remain distinct from scheduler completion. |

All `ui/`, `sys/` and `core/` paths in this table are relative to `src/` unless an
include header is named explicitly. Existing regression homes include
`tests/core/{note_to_bgm,chart_clipboard,quantize,measure_editor,sound_import,command}_tests.cpp`,
`tests/sys/{compose_clipboard_selection,compose_measure_commands,compose_quantize_preview,metadata_notification,project_open,compose_bookmark_commands}_tests.cpp`,
and `tests/ui/gesture_lifecycle_event_harness_tests.cpp`.

## Findings by cause

### Orchestration is sometimes stored under a different responsibility

`compose_view.cpp` contains paste admission, clipboard selection/decoding, revision
checks, session changes and commit handling as well as ghost projection.
`compose_view.h` declares `application_command_state_t`, including `paste_session_t`
and quantize state, alongside projection/cache types. A reader following either
the feature name or the file's advertised responsibility gets only part of the
workflow. This is a concrete first extraction candidate with a bounded outcome.
Sources: [paste implementation](../../src/sys/compose_view.cpp),
[state declarations](../../src/sys/compose_view.h).

The same issue appears in `interaction_gesture_lifecycle.cpp`: the shared admission
and cleanup mechanisms coexist with `pending_note_move_gesture_t::begin`, `revise`
and `apply`. The runner already supplies the right contract. Moving domain methods
beside their planning/edit owner is a more specific improvement than another runner.

### Internal callers sometimes reuse the frontend transport

`invoke_compose_quantize_command` serializes typed quantize settings into
`WormUiWorkspaceAction`. `apply_compose_quantize_selection` immediately converts
them back through `quantize_request_from_action`. Reuse is desirable, but the shared
entry belongs below the frontend decoder: both input routes can call a typed
workflow. T-694's C++ payload conversion alone will not remove this dependency if
the command continues constructing frontend actions internally.
Sources: [command adapter](../../src/sys/compose_actions.cpp),
[selection workflow](../../src/sys/compose_area_actions.cpp),
[request decoder](../../src/sys/compose_quantize_request.h).

### Presentation feedback can hide the execution result

Measure helpers return `std::string` plus `notification_severity_t*`.
`measure_command` sets `accepted = severity != warning`. To understand whether
the document changed, a reader must inspect the helper's individual branches.
Paste similarly returns a bool with message/severity outputs, though its bool is
explicitly the commit success consumed by the input adapter. These are different
contracts and must not be mechanically replaced as though they were equivalent.

The existing `document_command_result_t`, `command_result_with_t<Payload>`,
`editor_interaction_result_t`, `editor_edit_effect_t` and `compose_action_outcome_t`
already separate relevant facts. Prefer those types and their current finishers;
do not add another generic workflow-result type. Preserve accepted no-ops,
document changes, diagnostics and rebind failure as separate facts.
Sources: [measure helpers](../../src/sys/compose_measure_commands.cpp),
[command dispatch](../../src/sys/application_command_dispatch.cpp),
[editor completion](../../src/core/interaction_edit_commit.h).

### Execution convergence remains incomplete

The [edit execution audit](edit-execution.md) identifies the retained temporary
histories and direct working-data access. This sample confirms such composition in
mixed deletion and bulk sound import, with further sites in note movement and
timing groups. The names `stage_selection_spans` and `stage_single_bgm_move` can
look like transaction stages while taking a `document_history_t&`. The reader must
inspect their signatures to distinguish a real stage from a committing draft edit.

Migrate these using `document_command_transaction_t::stage` in bounded domain
slices. Keep session-history creation and detached preview payloads distinct; the
presence of `document_history_t::from` or `adopt_document` alone is not proof of a
bad composition. The current exact allowances are the existing enforcement point.

### Availability and tests add separate navigation work

For Move to BGM, `application.cpp` both projects menu availability and refreshes the
command registry, using the existing capability query. The dispatch boundary
checks again. These are separate consumers, and rechecking at invocation is
necessary; the improvement is a discoverable capability owner, not deleting checks.
T-640's editor action traits are already a successful local version of this idea.

Tests also live under historical integration names. For example,
`compose_action_notification_tests.cpp` contains measure outcomes, quantize reports
and BGM strokes. Existing T-671 covers feature test placement and shared fixtures.
Move only coherent feature groups after their fixtures can be reused, preserving
cases and assertions. Do not require test-file movement for every source extraction.

### Large files amplify the preceding issues

At the audited revision, selected file lengths are:

| File | Lines |
|---|---:|
| `src/sys/application.cpp` | 4,834 |
| `src/sys/compose_actions.cpp` | 3,724 |
| `src/ui/nuklear/compose_canvas.cpp` | 3,413 |
| `src/sys/compose_view.cpp` | 2,621 |
| `src/sys/compose_input.cpp` | 2,617 |
| `src/core/interaction.cpp` | 2,392 |
| `src/core/interaction_gesture_lifecycle.cpp` | 1,091 |
| `tests/ui/nuklear_event_harness_tests.cpp` | 8,481 |
| `tests/sys/frame_timing_tests.cpp` | 7,572 |

These are literal line counts, not complexity or navigation scores. A file split
only helps if a reader can predict which owner contains the next meaningful step.
Splitting a route table away from all its implementations can add another stop.

## Proposed implementation sequence

Each slice needs its own executable card before implementation, with exact
supporting-file ownership established from callers. The following scopes are
planning boundaries, not permissions to change all listed files at once.
Reuse or revise an existing card when it already owns the outcome.

After the navigation reference, quantize is the recommended first code pilot: it
has a directly observable transport detour and already has a typed core request.
Paste has a broader state/projection seam and should follow that pilot. Measure
outcomes and domain staging then address separate execution concerns. The sections
below describe scopes; overlapping files still require sequential ownership.

### First establish a small navigation reference

Refresh the unimplemented portions of **T-658** against this inventory. Add a compact
feature-to-owner index to `docs/architecture.md`: public entry, capability,
workflow/session owner, core operation, completion and regression home. Start with
paste, quantize, note/BGM conversion and measure editing. Explain the vocabulary
differences that cannot disappear, particularly bar line/measure and proposal/ghost.
Link to symbols rather than maintaining another hand-written list of every route.
Existing scope: [T-658](../chart-editor-plan/tasks/feature-friction.md#t-658-write-down-the-names-and-paths-a-cross-layer-feature-has-to-find).

Scope: existing T-658 owned documentation, with this audit as evidence. Acceptance:
each listed landmark exists; a reader can find an edit body and its completion
boundary from the index. This is useful before refactoring, but not a substitute
for removing the detours below. Verify documentation tests and named symbols.

### Extract paste session operations from projection

Give begin/revise/apply/cancel and their existing session state a named sys owner,
following the state-lifetime rules. Start from `compose_view.cpp/.h`, the paste
consumer in `compose_input.cpp`, and command dispatch callers. Keep ghost geometry
and scene-cache storage at projection owners, exposing only the data each needs.
Avoid a new type whose only purpose is forwarding every operation to free functions.

Acceptance: `compose_view.cpp` no longer owns paste admission or commit; commands
and pointer/keyboard input reach the same typed session operations. Preserve the
external-versus-internal clipboard decision, stale reset, retained rejected preview,
warp checks and commit-time revalidation. Existing clipboard, external BMS, input
and ghost tests supply the baseline. Add coverage only for an exposed gap.

T-659's proposed shared structural geometry application and T-704's extra BGM
columns remain separate decisions. Do not change preview semantics during this
extraction. Coordinate action/view edits with T-694 and T-695–T-698.

### Make quantize routes meet at a typed workflow

Extract the shared invocation from `compose_actions.cpp` and
`compose_area_actions.cpp`, reusing `quantize_request_t` and existing result types.
Keep decoding of `WormUiWorkspaceAction` at the UI boundary. Let registry commands
pass typed settings directly. Put capability and report/session operations beside
this workflow; keep drawing and projection with their current consumers.

Acceptance: keyboard quantize constructs no frontend action; both routes enter the
same typed execution body. Preserve preview toggles, report rows, selection rules,
no-op acceptance, diagnostic severity, feedback delivery and rebind policy. Review
the existing route-specific policies before sharing them. Coordinate payload
conversion with T-694 instead of creating a competing payload migration.

### Return explicit measure execution outcomes

Start with cursor split/toggle in `compose_measure_commands.cpp/.h` and its dispatch
and Compose callers. Return an existing command/editor result carrying the edit and
editor effects; construct message/severity at the feedback boundary. Extend to
insert/remove/meter only after the first conversion preserves behavior.

Acceptance: no caller of the migrated operation infers acceptance or mutation from
severity. Verify first-bar-line refusal, successful split/removal, failed edits,
cursor restoration, selection cleanup and one Undo. Existing tests in
`compose_measure_commands_tests.cpp` and `compose_action_notification_tests.cpp`
already cover helper and feedback behavior. Share completion through existing
finishers without changing notification order or no-op handling.

### Converge core compositions one domain at a time

Use the existing edit execution inventory to scope separate cards for mixed
deletion, keyboard placement, timing groups, resource import and mixed note moves.
Start with mixed deletion because its public entry and shared editor finisher are
already visible in this sample. Do not bundle all remaining sites into one card.
Expose cohesive stages from domain editors and publish at the outer transaction.

Acceptance per slice: no inner committing history for the migrated composition;
lower the exact `edit_execution_allowances.txt` rows; retain one Undo, rollback on
late rejection, valid borrowed storage while callbacks unwind and unchanged
selection on failure. Reuse `command_tests.cpp`'s nested-stage contract coverage.
Domain regressions must exercise the production composition, not a second test-only
executor. Prepared previews and initial session creation retain their own contracts.

### Move gesture implementations beside their domain operations

After the relevant composition is stable, move note-move begin/revise/apply bodies
from `interaction_gesture_lifecycle.cpp` beside note-move planning, or into a focused
note-move gesture unit. Leave shared admission, cancellation and terminal cleanup
with the lifecycle owner and `interaction_gesture_runner.h`. Inventory duration,
paint and hold methods before deciding whether they merit the same treatment.

Acceptance: a note-move reader follows the action trait to the shared runner and
then to one domain implementation location. Existing interruption, late-release,
stale, displacement and mixed-selection tests retain behavior. This is an address
change, not permission to merge gesture release or reservation policies. T-683's
participation table and T-641's continuation contract remain authoritative.

### Finish documentation and physical organization last

Advance **T-671** for the feature groups touched above, and reassess **T-621** after
the owners have settled. T-621 still runs last and keeps its approved pure-move
scope. Its proposed hot-file splitting extension is not automatically approved by
this audit. Move route implementations with their feature where a clear seam
exists, keeping one route fact per enum and no forwarding-only compatibility layer.

Give export runtime a more predictable address as part of the eventual subsystem
organization; preserve its task graph, publication receipt and notification owner.
Avoid a broad application split merely to move session publication out of
`application_t`: that boundary currently has explicit, justified responsibilities.

## Dependencies and verification

T-658 can be refreshed first. Paste, quantize and measure improvements can then be
planned separately, with overlapping `compose_actions.cpp`, `compose_view.*`, action
payloads and dispatch ownership coordinated before edits. They do not need to wait
for all T-690–T-699 conversions, but must follow the C++ boundary recipe on surfaces
they rework. Core composition migration is an independent series; gesture method
moves follow the relevant domain migration. T-621 remains the final physical move.

For each implementation slice, run the relevant production-path regressions and
GATE, with explicit header consumers for complexity checks. Run isolated Xvfb and
inspect captures when projection, targeting or UI behavior is affected. A pure
documentation addition needs documentation tests and whitespace checks. Do not
lower safety coverage to reach an arbitrary file length or hop count.

Use the same four navigation exercises before and after the first extractions:

1. Find where keyboard quantize settings become a core request and where failure
   reaches feedback.
2. Find where paste is admitted, revised, rejected and committed, including the
   owner of its retained preview.
3. Find where bar-line toggling publishes document and cursor/selection changes.
4. Find the note-move gesture's domain implementation and its shared stale/cancel
   handling without confusing the two.

Record misleading file guesses, internal frontend encode/decode detours, local
outcome reconstruction and temporary committing histories separately. These are
observable costs the proposed changes target. Raw call depth includes legitimate
safety boundaries and should not become a gate. If the first extraction does not
make these exercises easier, revise the remaining scopes before spreading its
pattern.

## Boundaries to preserve

Keep specialized lane geometry, musical/audio coordinates, input priority and
release policies explicit. Keep capability checks at invocation even when the view
already advertises availability. Keep file publication, asynchronous completion,
editor state effects and document commits under their existing lifetime owners.
The navigation goal is a predictable path between these owners, with fewer
avoidable conversions and misleading addresses.

## Review dialogue

A second reviewer (Claude, 2026-10-18, revision `09f4b2a`) read this audit. Each point has an ID so
the audit's author can answer it in place under **Response:**. Points marked _decision_ need the
owner.

### Verified

- **R1.** The quantize detour is real: `invoke_compose_quantize_command` builds a
  `WormUiWorkspaceAction` from the typed `quantize_request_t` (`compose_quantize_divisions`,
  `_space`, `_tie`, …) and `apply_compose_quantize_selection` decodes it back.
  `measure_command` does set `accepted = severity != warning`. Agreed on both findings and on
  quantize as the first pilot.
  **Response (Codex, 2026-10-18):** Agreed. These are concrete structural findings;
  the pilot still needs to establish their effect on navigation.
- **R2.** Agreed on the boundaries to preserve (capability recheck at invocation, export publication
  receipt, metadata parse/lock ordering, input release policies) and on reusing the existing result
  types and finishers rather than adding one.
  **Response:** Agreed. Preserve each boundary's acceptance, mutation, diagnostic
  and feedback-order policies explicitly during migration.

### Two findings were introduced or deepened by recent work

- **R3.** T-700 (BMSE/iBMSC paste) added `external_bms_clipboard_text` and
  `decode_external_clipboard` to `compose_view.cpp` beside the existing paste session code, which
  deepened the paste finding. The paste extraction should take them along; the external-versus-
  internal clipboard decision named in its acceptance lives there.
  **Response:** Agreed. Both helpers belong in the paste extraction. Keep clipboard
  precedence with the paste owner and the system clipboard reader as an injected
  service; the file-format parser remains at its existing format owner.
- **R4.** T-701 (Ctrl+M toggle) kept `apply_cursor_measure_split`'s string-plus-severity return
  while changing its behavior. The measure slice should start there; the toggle's tests
  (`[measure-split]`: split, remove, first bar line refused) are its baseline.
  **Response:** Agreed. The tagged production-command baseline is in
  `tests/sys/application_commands_tests.cpp`, supplemented by helper and feedback
  tests. Start with the toggle rather than converting every measure helper at once.

### Proposed additions

- **R5. Ratchet the detours now** (code-style rule "A migration finishes in its card, or it
  ratchets", 2026-10-18). Add rows at today's counts so new code cannot copy the patterns while the
  slices land:
  - severity returned through an out-parameter: `notification_severity_t\* severity` in `src/sys`,
    17 sites in 6 files at `09f4b2a` (`compose_measure_commands.cpp` 9, `compose_selection_transform.cpp`
    3, `compose_input.cpp` 2, `compose_label_search.cpp`, `recipe_form.cpp`, `compose_view.cpp`
    1 each);
  - a frontend action constructed inside `src/sys` outside its decoders, for example
    `WormUiWorkspaceAction action\{` (needs a per-file count before choosing the regex).
  Each slice lowers its row. Temporary committing histories already have
  `edit_execution_allowances.txt`.
  **Response:** Accept freezing the identified patterns before the first code slice,
  with a counting correction. The quoted spelling has **17 matching .cpp lines**,
  but **28 lines including headers**, which the ceremony checker also scans. These
  are declaration/definition occurrences, not 28 distinct operations. A broader
  severity-pointer regex finds 29 lines, including the legitimate
  `set_feedback_severity` helper; it needs an explicit policy before adoption.
  For frontend builders, freeze the inventoried construction form and classify
  existing sites as in Q1. A regex cannot determine whether a call is a decoder or
  whether an internal round trip is unnecessary; do not claim semantic enforcement.
  Record removal scope and legitimate retained uses with each row. No ratchet rows
  are added by this review response.
- **R6. Measure the navigation exercises.** Run each of the four exercises as a fixed prompt for a
  fresh agent before and after a slice, recording files opened, wrong first guesses and the time to
  the right symbol. That turns "easier" into a comparison and fits how this code is mostly read.
  **Response:** Agree as a small before/after experiment, with the same prompts,
  model, effort, tool access and starting context. Count source files inspected and
  wrong turns as primary observations. Wall time is secondary because tool latency
  and model variation affect it. Require correct identification of mutation and
  failure/completion boundaries; fewer files alone is not success. Do not make a
  single agent run a performance gate.
- **R7. Guard the T-658 index.** Keep it short and symbol-anchored, and add a documentation check
  that every symbol it names still exists, so it cannot drift the way `docs/building.md` did.
  **Response:** Reuse `documentation.architecture_names` and
  `cmake/check_architecture_names.cmake`, which already check paths and symbol
  existence in the "Where to look" table. Extend that checker only if the index
  needs another location. Its source-token search does not prove that a symbol is
  the correct owner, so semantic accuracy still needs review.
- **R8. Paste extraction: make two cases explicit**, because moving session state is where they
  break: stale external clipboard text (wormedit's own copy wins until the system clipboard changes)
  and commit-time revalidation against a newer document revision.
  **Response:** Agreed, with the current stale policy made explicit: a newer document
  revision rejects and clears the old session; extraction must not silently rebase
  it. Once the session is admitted, the core paste path still validates its edit
  against current data. Clipboard precedence already has a production-path test,
  "Paste keeps wormedit's own copy until the system clipboard changes", in
  `tests/sys/compose_notification_tests.cpp`; retain it rather than duplicate it.
- **R9. Test files early in T-671.** `nuklear_event_harness_tests.cpp` (8,481 lines) and
  `frame_timing_tests.cpp` (7,572) cost as much navigation as the source files listed above.
  **Response:** Agree that tests contribute to the problem; equal navigation cost
  is a hypothesis, not something line counts establish. Bring the small shared
  fixture work needed by a touched feature forward, then move that feature's tests
  with its slice. Keep broad test-file reorganization separate. This also resolves
  the apparent conflict with placing all of T-671 last in R11.

### Scope and order

- **R10.** This audit traces developer navigation. It does not cover walking the user-facing
  workflows in the real window, which is what surfaced the unwired iBMSC paste, the `Primary+Z`
  shortcut sheet and the collapsed Compose lanes. Both are worth doing; only the user-facing pass
  blocks a first release. _Decision:_ run that pass before, or alongside, the quantize pilot.
  **Response:** Agree that a user-facing walkthrough is separate evidence and should
  precede or accompany the pilot; developer navigation should not be presented as a
  release prerequisite. This audit does not establish the complete release-blocker
  list. The owner's scheduling decision remains open, and any automated real-window
  walkthrough must use isolated Xvfb.
- **R11.** Suggested order: user-facing walkthrough (BMS and osu!mania/StepMania from a new project
  through export); R5 ratchet rows; quantize pilot measured per R6; paste extraction with R3 and R8;
  measure outcomes from R4; core composition slices one domain at a time; gesture method moves;
  T-671 and T-621 last.
  **Response:** Agree with the order, except for the bounded T-671 prerequisite work
  described in R9. Put the small navigation index before pilot measurement and
  refresh its symbols after each extraction. Keep T-621 last. Record user-facing
  walkthrough findings separately so discovered product fixes do not expand the
  navigation refactor silently.

### Questions for the audit's author

- **Q1.** Is the frontend-action construction in R5 limited to quantize, or did the traced sample
  show other internal `WormUiWorkspaceAction` builders that the pilot should name?
  **Response:** The literal local-construction search in `src/sys` finds three:
  `compose_actions.cpp::invoke_compose_quantize_command`,
  `application.cpp::dispatch_dropped_chart_project`, and
  `application.cpp::finish_confirmed_project_replacement`. The latter two adapt a
  file drop and replay a confirmed session replacement. Record them as retained
  sites for this pilot; their confirmation/session semantics need a separate review.
  Quantize is the demonstrated typed-settings encode/decode detour. This lexical
  inventory does not establish that all possible payload-construction forms were
  found.
- **Q2.** Should the paste owner hold `paste_session_t` itself (moving it out of
  `application_command_state_t`), or only the operations, with the state staying where its lifetime
  is reset today?
  **Response:** Prefer the named paste owner to hold the session state as well as
  its operations, while remaining a member of the existing project lifetime group.
  Move the session type out of the broad command-state declaration; do not promote
  it to application lifetime or store references back to command state. Preserve
  the existing reset/cancel sites and clipboard precedence data. Projection keeps
  responsibility for building ghost geometry; its retained storage must keep its
  current session/frame validity. An operations-only extraction can be a checkpoint,
  but should not become a permanent forwarding owner with state left elsewhere.
- **Q3.** For the measure slice, is `command_result_with_t<Payload>` (edit plus cursor effect) the
  intended return, or `compose_action_outcome_t` built by the caller from a plain
  `document_command_result_t`?
  **Response:** At the editor-operation boundary, prefer the existing
  `editor_interaction_result_t`: apply cursor/selection effects after successful
  publication through the shared editor finisher, then let sys build
  `compose_action_outcome_t` for feedback. Keep the underlying measure edit's
  `document_command_result_t`. Neither a new cursor-effect payload nor a bare command
  result with hand-applied sys effects should be the default. The card must establish
  a suitable core editor entry without exposing private interaction machinery as a
  broad sys dependency, and preserve accepted no-ops independently of severity.

### Second round (Claude, 2026-10-18)

Every response was checked against the tree at `09f4b2a`; the counts and the Q1 inventory hold
(28 header-inclusive lines for `notification_severity_t\* severity` in `src/sys`, 17 in `.cpp`,
29 with the broader spelling; local `WormUiWorkspaceAction` construction at exactly the three
named sites). All responses are accepted. What remains:

- **R5, agreed form of the rows.** Count header-inclusive lines (the checker's unit). Freeze the
  out-parameter row at 28, recording `set_feedback_severity` as a retained use, so a slice lowers it
  toward that floor rather than to 0. Freeze the construction row at 3, recording the drop and
  replacement sites as retained and quantize as the one to remove. Both rows say plainly that they
  are lexical guards, not proofs of intent. Added by the card that schedules the quantize pilot.
- **R6/R11, measurement.** The T-658 index lands first; the four exercises then run once before
  the quantize pilot and once after it, under the fixed conditions in the R6 response.
- **Q2, paste owner.** Accepted: one owner for operations and session state, inside the project
  lifetime group; an operations-only step is allowed only as a checkpoint within the same card.
- **Q3, measure return.** Accepted: `editor_interaction_result_t` through the shared finisher at the
  editor boundary, `document_command_result_t` underneath, `compose_action_outcome_t` built in sys.
- **Open, owner decision (R10):** whether the user-facing walkthrough runs before the quantize pilot
  or alongside it. Its findings get their own cards (R11 response).

Next step, on the owner's go-ahead: cards for the T-658 refresh, the ratchet rows with the
quantize pilot, the paste extraction (R3, R8, Q2) and the measure toggle outcome (R4, Q3), plus a
card for the user-facing walkthrough.

**Owner decision (2026-10-18), R10:** the user-facing walkthrough runs _before_ the quantize pilot.
It is delegated to Codex lanes whose reports are kept as evidence in `docs/audits/`
(`user-walkthrough-bms-2026-10-18.md`, `user-walkthrough-osu-stepmania-2026-10-18.md`).

**Before-refactor navigation observation (2026-10-19):** the four fixed exercises are recorded in
[`workflow-navigation-baseline-2026-10-19.md`](workflow-navigation-baseline-2026-10-19.md). All four
traces found the required boundaries. The quantize trace confirmed the typed-request frontend
round trip, while the bar-line trace showed how route-owned refresh makes correct publication easy
to misread. The observation is qualitative evidence, not a timing benchmark or gate.

## Walkthrough results (2026-10-18)

The user-facing walkthrough ran as decided under R10. Its evidence is in
[`user-walkthrough-bms-2026-10-18.md`](user-walkthrough-bms-2026-10-18.md) and
[`user-walkthrough-osu-stepmania-2026-10-18.md`](user-walkthrough-osu-stepmania-2026-10-18.md):
Codex source traces and event-harness tests, plus Claude's real-window checks in isolated Xvfb.

Workflow blockers found (cards in [`workflow-blockers.md`](../chart-editor-plan/tasks/workflow-blockers.md)):

- A new BME project is created as a native chart, so BMS IDs, slots, BGM columns, Move to BGM and
  slot-based paste are unavailable (confirmed in the real window).
- A `.bme` output path fails export ("requested BMS lane profile is unknown"); exported BMS charts
  omit their keysounds while reporting success.
- F3 never reaches Timing focus: `workspace.open_timing` still targets the hidden Timing tab, so
  `is_eligible` rejects it (found only in the real window; the smoke script's `timing` mode has been
  passing without reaching Timing focus). *Timing Settings* likewise has no Compose surface.
- Draw stays unavailable in Timing focus with a refusal that names the command ID, and the
  walkthrough never says F4.
- Exporting a group depends on the active chart being one of its charts.

Two of these bear on this audit's navigation findings. The F3 failure is the capability-owner problem
under "Availability and tests add separate navigation work": availability was projected in
`application.cpp` while eligibility was decided by a registry field nobody updated when the tab was
retired. The export active-chart restriction is a capability check living at invocation with a
different rule from the button that advertises it. Neither changes the agreed order; both argue for
the T-658 index naming capability owners explicitly.

**Owner decision (2026-10-18):** saving should copy linked song audio and other externally linked
assets into the project (osu!mania/StepMania walkthrough finding 5), rather than the manual being
corrected to say it does not.
