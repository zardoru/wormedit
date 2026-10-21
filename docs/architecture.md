# Edit pipeline

The one model to learn before reading wormedit's code (owner, 2026-10-15). With domain knowledge of
the chart formats, this page should be enough to find any edit and go down into its details.
Names marked *(T-6xx)* are what that card is putting in place; follow them in new code now.

## The model

```
                    ┌─► direct command ─────────────────────────────┐
input ─► dispatch ──┤                                               ├─► transaction ─► core edit
                    └─► gesture runner ─► gesture ─► proposal_t ────┘         │
                                                                              ▼
                                         outcome ◄── result ◄─────────── commit / reject
```

There are two paths in and one way to change the document.

- **Direct commands:** menu items, shortcuts and Compose actions. Each command or Compose enum has
  one route to its handler *(T-636)*. The handler builds the edit and runs it.
- **Gesture dispatch:** pointer drags, paint and keyboard holds. The gesture runner owns the
  lifecycle (begin → update → commit or cancel), the "another gesture is already active" and
  stale checks, and the result. A gesture holds only its planning and the edit it proposes
  *(T-634)*.
- **Proposals:** a gesture, drag or staged preview produces a `proposal_t<Payload, Stamp>`, the
  payload plus the document stamp it was planned against. `revise` updates it. `apply_proposal` applies it through the transaction and is
  the commit *(T-630, T-634)*.
- **Transaction (the executor):** `document_command_transaction_t` is the only way to change the
  document. It checks the stamp, copies the document lazily once, runs the edit, rejects
  without publishing, detects a no-op, and commits once with its Undo label *(T-607, T-637)*.
  `stage` resolves the working chart and passes the chart/document to an edit without
  publication. Nested stages share one copy; a rejected stage blocks the outer commit while
  retaining working storage until callbacks unwind. Typed `stage<Operation>` adds declared
  limits, event caps and postvalidation; `chart_edit_operation` in `core/command.h` lists what an
  operation must declare. `execute<Operation>` stages and then publishes once. Undo stores snapshots, so there is no separate command object. The proposal's payload, or the
  direct command's operation, *is* the edit.
- **Core edit:** a plain function or `apply(transaction&)` in `src/core` that changes chart data.
  This is where domain logic lives.
- **Failure views:** edit code returns diagnostics only. The public action and gesture boundaries
  attach a missing failure view through `editor_interaction_result_t::ensure_view`, after rollback
  and terminal gesture cleanup, using the caller’s history and state.
- **Results going up:** the transaction returns `document_command_result_t` (applied, revision
  and diagnostics); `command_result_with_t<Payload>` adds domain data when an edit needs it.
  The gesture path carries its result in `editor_interaction_result_t`, and direct commands in
  `compose_action_outcome_t`; the shared finishers deliver feedback and rebinding *(T-618)*.
  Expected failures use `result_t<T, E>`, optional values with diagnostics use `attempt_t<T>`,
  and reports that retain partial work on failure return that report directly. Shared
  `failure_code_t` names failures while domain owners retain their lifecycle phases *(T-633)*.

Accepted audition requests travel on those outcomes to the shared finisher, which submits them
to the project-owned `compose_auditions_t`. Its input scope delivers the last object request,
then the last draft request, after the entire input batch. Its catalog scope delivers the
previous frame's catalog request before workspace actions can submit another. These scopes
borrow the existing audio services only while delivering; resetting project state clears pending
requests. Submission precedes rebinding, preserving an accepted request if rebinding later fails
*(T-652)*.

Not everything reaches the transaction. View-only actions such as zoom, scroll, selection, tool
switches and filters change interaction or UI state and stop at dispatch. Waveform analysis,
visual decoding, resource-copy and inspection requests, and conversion previews run through
`async_job_t` and publish their results on the UI thread. Autosave, transactional import/export,
and queue/progress services retain their domain lifecycles and publication rules; see
[async task scheduling](async-task-scheduling.md).

Compose input batches retain the admitted geometry revision. Lane-key events additionally carry
a chart stamp advanced only by their own accepted synchronous edits, before feedback callbacks.
This permits chord releases in one frame without treating the first note's commit as an external
edit; pointer/viewport inputs still require the original geometry stamp. External changes and
failed rebinding stop continuation.

Selection commits share Undo/Redo chronology with document commands through the history owner's
monotonic event sequence. Selection snapshots live only in its bounded transient selection
history, not in song snapshots, codecs, saved revisions or document dirty state. The editor
dispatcher records accepted selection changes once; marquee previews and cancellations record
nothing. Both keyboard and command-menu history routes use `apply_editor_history`. A new accepted
selection or document edit discards both redo branches. Pointer presses reserve capture without
replacing selection; release commits a click or marquee, and Escape consumes an active capture
without also invoking Clear Selection.

## Glossary

| Term | Meaning and owner |
|---|---|
| Bar line / measure line | The selectable boundary that starts a measure. Core selection calls it `editor_selection_object_kind_t::bar_line`; projected grid geometry calls it `editor_grid_line_kind_t::measure`. `measure_id_t` is its identity. |
| Measure rail | The dedicated canvas strip from x=0 to `compose_measure_rail_width`. `compose_bar_line_bounds` in `src/ui/compose_timing_geometry.h` owns its handle geometry. It is not a timing-catalog lane. |
| BMS measure labels | Format-specific `#000`, `#001`, … presentation beside the grid. They do not identify or edit a bar line. |
| Split / merge | `split_measure_action_t` and `remove_bar_line_action_t` in `include/wormedit/core/measure_editor.h`; `add_bar_lines` and `remove_bar_lines` own the underlying structural changes. |
| Selection object | Stable `{kind, id}` identity carried by `editor_selection_object_t`. Scope flags decide which kinds participate. |
| Hit target | Geometry under a pointer at this frame. It selects an identity; it is not itself selection state. Shared bounds belong in geometry helpers used by drawing and input. |
| Area geometry | A snapshot of selectable rectangles used by area selection. `cache_area_object_geometry` projects visible scene objects; `editor_objects_in_row_range` supplies the row-range fallback when no snapshot exists. |
| Proposal | A stamped, revisable edit candidate, usually `proposal_t<Payload, Stamp>`. It owns validation and the eventual apply operation. |
| Report | Domain data retained from planning or completion: counts, mappings, warnings or partial-work facts. A report does not publish the document by itself. |
| Ghost preview | Presentation geometry derived from a proposal or report. Paste ghosts come from `paste_geometry`; the core proposal remains the authority for acceptance and commit. |
| Public entry | A command ID or frontend action/input type that starts a workflow. |
| Capability | The predicate and registry state that say whether the entry is currently available. |
| Workflow/session owner | Project-lifetime state for a multi-frame workflow, or the gesture/proposal owner for a bounded edit. |
| Completion | The typed transaction result plus any domain report, mapped once into shared command/interaction feedback. |

## Adding a chart edit

You write only what changes:

1. **The operation:** what it validates against the current chart, the data it changes, and its
   effect (Undo label, what it selects or reports).
2. **One entry:** a route from its command enum (direct command), or a gesture kind's
   `begin`/`update`/`apply` (gesture). Drags that preview before committing are a `proposal_t`
   payload.

The runners exist to make this the easiest way to write an edit, not to restrict what an edit can
do. They keep every safeguard: the stale-document check, the single lazy copy, rejection without
publishing, the no-op check, one commit with its Undo record, the "another gesture is already
active" rule, the stale message, result construction and feedback. Writing an edit through them
gets all of these without having to remember them; writing them by hand is more work and the
first thing to drift. If a new edit needs a safeguard the runner lacks, add it to the runner so
every edit gets it.

## Where to look

| To find | Search |
|---|---|
| A direct command's route | `application_command_route_t::from` in `src/sys/application_command_dispatch.cpp`; `compose_action_route_t::find` and the constexpr module route tables in `src/sys/compose_action_routes.h` and `src/sys/compose_actions.cpp` (with the area, resource, marker, bookmark, BGA and sound module tables). The typed route runners in `compose_action_routes.h` bind actions and use `run_compose_action` in `include/wormedit/core/proposal.h`; `finish_application_command` / `finish_compose_action` in `src/sys/compose_action_outcome.cpp` complete the outcome. |
| An editor action's capability and execution | `editor_action_handler_t<Action>` in `src/core/interaction.cpp`; `editor_action_dispatch_t` checks its declared virtual-cursor policy, prepares the view/cursor context once, and calls `perform`. Every variant alternative must satisfy the handler contract; there is no default. Gesture cancellation, stale capture, admission and terminal cleanup remain in `interaction_gesture_lifecycle.cpp`. |
| A gesture | `src/core/interaction_gesture_runner.h` (`editor_gesture`, `begin_gesture`/`revise_gesture`/`apply_gesture`/`cancel_gesture`, implementing begin/update/commit/cancel) and the gesture's own file |
| A proposal | `proposal_t<`, `apply_proposal` in `include/wormedit/core/proposal.h`, and the payload's type |
| The executor | `document_command_transaction_t` in `include/wormedit/core/command.h`: construct with history (and an optional captured stamp); `document()` creates the working copy once, `current_document()` reads without copying, `source_document()` / `source_chart(id)` read the committed version, and `outcome()` carries diagnostics. `is_ready()` reports a stale capture born rejected. |
| A committing note edit’s editor effects | `editor_edit_effect_t`, `prepare_editor_edit`, `run_editor_edit` and `commit_editor_edit` in `src/core/interaction_edit_commit.h`: stage a detached working document, commit once, then apply selection/cursor/lane, update surviving capture stamps and build the result. Internal delete/tap reuse shares action context preparation and staging instead of dispatching on scratch histories. Domain commands that already own a transaction use the same finisher. |
| A core edit | the editor in `src/core` named for the data (`bpm_editor`, `marker_editor`, …) |
| Area selection | `cache_area_object_geometry` in `src/sys/compose_view.cpp` projects rectangles when a scene snapshot exists. `editor_objects_in_row_range` in `src/core/interaction_selection.cpp` is the row-range fallback; `is_scope_accepted` and `is_editor_selection_scope_accepted` filter both by scope. |
| Paste | `edit.paste` enters through `begin_compose_paste`; `application_command_state_t::paste_session_t` in `src/sys/compose_view.h` retains the preview. `chart_paste_proposal_t` in `include/wormedit/core/chart_clipboard.h` proposes, revises and applies; `paste_geometry` in `src/sys/compose_view.cpp` derives ghosts from its report. |
| The measure lane | The measure rail made editable: `compose_bar_line_bounds` in `src/ui/compose_timing_geometry.h` is the one handle rectangle for drawing, picking and area selection, derived from grid lines that carry `measure_id`. Placing a bar line is `split_measure_action_t`; deleting merges through `remove_bar_lines`; pasting splits through `add_bar_lines`. |
| A row move | `stage_shift_selection` in `include/wormedit/core/time_shift.h` is the one executor for moving objects and bar lines by chart rows (step moves, shift from cursor, bar-line moves with or without time propagation). The drag gesture keeps its own lane and collision planning and runs its row component through it inside its commit; bar-line geometry itself is `move_bar_lines` in `measure_editor.h`. |
| Frontend frame data | `ui::workspace_view_t` in `src/ui/frontend/workspace.h` owns C++ presentation data, including `compose_view_t`. The application retains main/Compare `sys::frontend_workspace_view_t` projection owners and calls `refill` in `src/sys/application_frame_view.cpp`; their live query caches and geometry scratch stay in sys. Views retain list/text capacity, with no frontend arena, pointer rebinding or C ABI codec. |
| Background work | `async_job_t` in `include/wormedit/core/async_job.h` |

Rules for writing each piece: [code style](chart-editor-plan/decisions/code-style.md).

### Feature-to-owner index

| Feature | Public entry and capability | Workflow/session owner | Core operation | Completion and regression home |
|---|---|---|---|---|
| Paste | `edit.paste`; `paste_target_available` and `refresh_application_command_states` in `src/sys/application.cpp` | `application_command_state_t::paste_session_t`; `begin_compose_paste`, `revise_compose_paste`, `apply_compose_paste` in `src/sys/compose_view.cpp` | `chart_paste_proposal_t` and `paste_chart_clipboard` in `include/wormedit/core/chart_clipboard.h` | `document_command_result_t`; `tests/core/chart_clipboard_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp` |
| Quantize | `edit.apply_quantize` and `WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE`; `refresh_quantize_commands` and `can_apply_compose_quantize` | `compose_quantize_editor_for` and `toggle_compose_quantize_preview` in `src/sys/compose_quantize.cpp`; retained state remains in `application_command_state_t::project.quantize` | Typed `apply_compose_quantize_selection` in `src/sys/compose_quantize.cpp`; `quantization_plan_t::from` and `apply_quantize_command` own the core operation | `quantize_command_result_t` mapped once to `compose_action_outcome_t` by the typed workflow; `tests/core/quantize_tests.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp` |
| Note ↔ BGM | `edit.move_to_bgm`, `edit.move_next_to_bgm`, and `WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE`; `compose_move_to_bgm_available` | Current editor selection; BGM drop dispatch in `dispatch_note_to_bgm_input` and `move_selected_bgm_to_lane` | `stage_move_notes_to_bgm` and `stage_move_bgm_events_to_lane` in `include/wormedit/core/note_to_bgm.h` | `selected_note_to_bgm_result_t` or `editor_interaction_result_t`; `tests/core/note_to_bgm_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/compose_notification_tests.cpp` |
| Measure editing | `measures.split_at_cursor` and the measure-lane Compose actions; `compose_measure_edit_rejection` | `application_command_state_t::project.compose_view` owns lane visibility/policy; `measure_resize_t` in `src/ui/nuklear/compose_state.h` owns resize capture | `measure_edit_action_t`, `apply_measure_edit`, `add_bar_lines`, `remove_bar_lines`, `move_bar_lines` in `include/wormedit/core/measure_editor.h` | `document_command_result_t`; `tests/core/measure_editor_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/ui/compose_measure_resize_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp` |

### Fixed navigation exercises

These are the fixed prompts used before and after T-722. Reuse them unchanged for any later
comparison:

1. Find where keyboard quantize settings become a core request and where failure reaches feedback.
2. Find where paste is admitted, revised, rejected and committed, including the owner of its retained preview.
3. Find where bar-line toggling publishes document and cursor/selection changes.
4. Find the note-move gesture's domain implementation and its shared stale/cancel handling without confusing the two.

Keep model, effort, tools and starting context identical. Record source files inspected and wrong
turns first; wall time is secondary. A successful answer must identify mutation and
failure/completion boundaries. This is comparison evidence, not a performance gate.

## Adding a cross-layer surface

Use the measure lane as the worked example. These recipes list the touchpoints that must agree;
T-661 and compiler warnings from T-669 should enforce closed enums and table sizes where possible.

### Adding a selection object kind

1. Add the stable vocabulary value beside `editor_selection_object_kind_t::bar_line` in
   `include/wormedit/core/vocabulary.h`; carry it as `editor_selection_object_t`, never as a display
   number or container position.
2. Add scope participation beside `is_scope_accepted` in
   `src/core/interaction_selection.cpp`, plus row-range and projected-area collection where the
   object participates.
3. Project the same ID into the view and use it from hit testing and emitted actions. The measure
   path uses `ui::compose_grid_line_view_t::measure_id` and `append_area_bar_line_geometry`.
4. Cover direct selection, scoped area selection and unavailable/hidden cases in core, sys and
   event-harness tests.

### Adding a Compose action

1. Add the boundary action beside `WORM_UI_COMPOSE_ACTION_TOGGLE_MEASURE_LANE` in
   `src/ui/frontend/actions.h` when extending that legacy enum; new or converted surfaces use the
   C++ boundary types required by the frontend rule.
2. Give it one operation and route. The worked example is `toggle_compose_measure_lane_action_t`
   and its `compose_action_route_t` entry in `src/sys/compose_actions.cpp`.
3. Emit it once from the owning frontend control and verify both route and feedback. Closed action
   tables must fail at compile time when an entry is missing.

### Adding a toolbar button

1. Put rendering and emission in the owning toolbar group; `render_measure_lane_toggles` in
   `src/ui/nuklear/compose_toolbar.cpp` is the example.
2. Update the toolbar layout count in the same file so the control is reachable at supported
   widths. Derive enabled/checked state from the view or command registry instead of duplicating
   capability rules.
3. Add an event-harness assertion for the emitted typed action and any visibility-dependent
   companion control.

### Adding a Compose view field

1. Define a backend-neutral C++ view type with owned storage and queries, like
   `compose_measure_lane_view_t` in `src/ui/frontend/compose_measure_lane.h`; add it to the owning
   Compose view in `src/ui/frontend/compose.h`.
2. Fill it at the sys projection owner (`src/sys/compose_view.cpp` for the measure lane), carrying
   stable IDs for every selectable/editable object.
3. Read it in the frontend without reaching document/history state. Check frame ownership and avoid
   copying an enclosing view after adding owned containers.

### Adding a selectable canvas lane

1. Add the durable data and typed core edit first. The measure lane uses
   `split_measure_action_t`/`remove_bar_line_action_t` in
   `include/wormedit/core/measure_editor.h`.
2. Project identity and presentation, then put shared bounds at one geometry owner. The worked path
   uses `compose_bar_line_bounds`, `compose_bar_line_at` and `is_compose_measure_lane_press` in
   `src/ui/compose_timing_geometry.h`.
3. Use those bounds for drawing (`draw_compose_measure_lane`), ordered pointer claiming
   (`compose_pointer_start_hits_element` and `handle_compose_measure_lane_click`) and area geometry
   (`append_area_bar_line_geometry`). Do not add the lane to the timing catalog unless its domain
   semantics are timing events.
4. Test projection plus actual frontend emission, overlap priority, visibility/lock refusal and the
   committed core edit; inspect the real layout in isolated Xvfb when pixels change.

## Extension direction

New specialized lanes should add their domain data/projection, named drawing and targeting,
and edit/gesture payloads through the existing transaction. Common lane metadata and input
participation facts have their own owners; point/span/handle geometry, musical versus audio coordinates,
resource requirements, snapping and lock policies remain explicit.

For a new specialized lane, start with the contrasting families in the
[extension inventory](chart-editor-plan/tasks/extension-friction.md#lane-and-gesture-execution-inventory-2026-10-18)
and follow these extension sites:

1. Add its durable data, validation and typed edit at the core domain owner. Timing events use
   `timing_event_access_t` and the timing-lane controller; BGA resources, measure boundaries and
   audio sources retain their own editors and transactions.
2. Add the projected view at the matching sys owner: timing markers in `compose_marker_view.cpp`,
   SPEEDS spans in `compose_speed_spans.cpp`, BGA in `compose_scene_view.cpp`, measure/grid in
   `compose_view.cpp`, and audio overlays in `compose_waveform_overlays.cpp`. Retain the current
   scene-cache keys and frame-storage lifetimes.
3. Put each geometry fact at its existing shared owner and use it from drawing and targeting.
   `compose_timing_geometry.h` owns timing partitions and marker/bar-line bounds. Point heads,
   span bodies, handles, resource stacks and audio coordinates keep their distinct policies.
4. For a timing family, add the core catalog descriptor and its UI enum correspondence in
   `sys/timing_lane_ui.h`. The codec's descriptor query derives selection identity from
   `core/timing_lane_catalog.h`; staging, marker projection, marker selection, area geometry and
   paste ghosts use these owners. Labels come from the core catalog, widths from timing geometry,
   and icons/theme from rendering. Other specialized lanes do not enter this timing catalog.
5. Add named drawing and ordered pointer handlers, selection/area participation and one action
   route or gesture kind through the existing edit pipeline. Preserve visibility, locks, snapping,
   resource resolution and overlap priorities. Verify projection plus actual input emission;
   use isolated Xvfb for the resulting layout.

For a new UI gesture:

1. Keep its capture fields in the matching lifetime group in `compose_state.h`; use
   `compose_drag_capture_t` for the existing stamp, threshold and continuation decisions. Choose
   its domain release policy explicitly. Core edits use the core gesture/proposal runner.
2. Declare any hover/paint reservation in the participation table in `compose_input_queries_t`.
   Active and candidate flags may differ: a BMSON candidate reserves paint, while a hold candidate
   reserves hover. Captures handled entirely by an ordered consumer need no blanket reservation.
   Inline editors, popups and bookmark rename keep their separate editor/input gates.
3. Add the named consumer in the existing ordered routing path. Query participation again after
   earlier handlers can acquire input; never cache the query result across those handlers.
4. Register capture and candidate reset hooks with `cancel_compose_gestures` in
   `compose_input_panel.cpp`. The cancellation inventory owns cleanup, while the participation
   table owns consumer policy. Preserve capture evidence until hooks observe it; retain the
   separate stamp and deferred cancellation for audio-offset edits.
5. Add the actual production emitter to the lifecycle event fixture and its interruption/late
   release matrix. Check both its hover/paint policy and its distinct click/release/source-validity
   behavior. Navigation and view-only captures retain their own effects.

SV compilation uses `sv_recipe_prepared_t` in `core/sv_recipe.h`: pure built-ins keep exact
random-access values, while an already-evaluated emission batch holds its preceding value
between rows. The prepared owner stores validated data and inspection samples, with no live
evaluator. Composition uses that owner for overlap multiplication and authored baseline rows;
draft preview reuses its prepared candidate, and export baking reaches the same composition
path. These transient values belong to one input snapshot; captured work still needs the stamp
checks above. Lua execution and stored post-processing stages remain separate feature work.

The [extension checkpoints](chart-editor-plan/tasks/extension-friction.md) record the current
SV capability references, cross-cycle state direction, bounded lane/gesture findings,
the T-681–T-683 implementation scopes and their bounded verification evidence.

## File preparation and publication

Opening a project reads/parses/normalizes into a detached `project_open_result_t`
through `open_project_file` (`sys/project_open.cpp`). The application submits selected files through `project_open_runtime_t` over `async_job_t`.
The worker owns the path, encoding and byte progress; the UI polls its completion. `finish_project_open` owns encoding retry/error feedback and only
passes successful results to `publish_opened_project`. That UI-thread method owns
session replacement, imported session identity, recent-file updates and aggregated
warnings. The job generation, cancellation and captured document/session admission precede this
completion boundary, keeping the previous document until replacement. Reading checks cancellation
between bounded chunks; parser calls are non-interruptible, and cancelled parse results are discarded.

`output_group_export_plan_t::from` (`sys/output_group_export.h`) owns format request
policy shared by preview and publication, including StepMania filename/profile and
BMS sliced-layout rules. `output_group_export_preparation_t::from` shares asset discovery,
pending-copy planning, conversion and publication preflight between Project Preview and Export.
Preview lists the planned destinations and diagnostics without writing files.
`output_group_export_t::start` admits the request before
copying its document, owns task submission and captures all worker inputs.
`run_output_group_export` (`sys/output_group_export.cpp`) owns staging/conversion and
calls the existing atomic publisher. The runtime owns completion and notification.
Cancelling retains busy state until terminal polling, and a separate domain receipt
preserves publication evidence when scheduler cancellation replaces the task outcome.
Conversion and preview expose advisory artifact counters and check stop tokens between
writers/reimports. Atomic staging checks cancellation between existing files, generated
artifacts and immediately before exchange. Automatic music, keysound and image copies are pending mappings
until copied into private staging, so rejection/cancellation cannot leave a new output
folder or audio file. Completed exchange remains published even if cancellation races
with completion. Individual writers and filesystem calls are not preemptible.
Selected-area audio export belongs to `runtime::preview_audio_export_t`
(`sys/preview_audio_export.h`) over `async_job_t`. Its admitted request owns the
document snapshot, source paths, row interval and trim/fade/encoder settings. The worker
uses the ordinary timing schedule, shared music binding and hitsound resolver with
`render_offline_interval`, then encodes Vorbis and decode-verifies a same-directory
temporary owned by `prepared_atomic_file_t`. UI-thread polling checks the document stamp
and selected chart before explicit atomic publication. Cancelled, stale and abandoned
results destroy their temporary without replacing the destination. Replacement is explicit;
project asset destinations are rejected even when replacement was requested.

osu! note samples are semantic `osu_hitsample_t` values, persisted by the native codec.
`hitsound_values_t` owns mixed-selection presentation and toggle/cycle decisions;
`apply_hitsound_edit` batches selected-note changes through the transaction. Empty-selection
palette actions change only `editor_interaction_state_t::armed_hitsample`, captured by tap,
paint and hold placement. Preview resolves inherited sample context once per chart and
expands normal/addition voices through the existing bridge's multi-voice bindings.
Object audition uses the same hitsound binding builder, with object-filtered resolution from
the project audio-source owner; generated voices do not bypass normal stem-source validation.
The editor's persisted playback-audition preference gates typed hitsample voices in the live
transport without removing their compiled bindings. Changing it refreshes prefetched output at
the audible frame, preserving loop, rate and pitch mode. Click audition remains governed by its
independent existing preference, and offline export retains the authored mix rather than the
live-monitoring policy.

## Composing edits

Use `document_command_transaction_t::stage` for each component and publish at the outer
boundary. Chart callbacks receive `chart_document_t&` and `song_document_t&`; document-wide
callbacks receive `song_document_t&`. Return `attempt_t<Effect>` with domain validation,
mutation and effect. A missing value or error rejects the transaction, including all previous
stages. An unchanged effect leaves earlier successful stages available for the outer commit.
Callbacks must preserve the document/chart storage borrowed by enclosing callbacks.

`document_edit_effect_t` carries a command's changed flag and Undo label;
`finish_execution` applies the command's explicit no-op policy and commits once. Editor edits
retain `editor_edit_effect_t`; their existing finisher publishes selection/cursor effects only
after the outer document commit succeeds. `stage_move_notes_to_bgm`,
`stage_move_bgm_events_to_lane` and `stage_assign_note_lanes` compose through this executor.

Do not create a scratch history, invoke committing commands on it, then adopt its document
into another transaction to combine edits. The [global execution audit](audits/edit-execution.md)
records remaining legacy sites and distinguishes session creation and detached payloads.
`architecture.edit_execution` checks every first-party source/header against an exact per-file
footprint; new files have no allowances, and reductions require lowering the inventory.
