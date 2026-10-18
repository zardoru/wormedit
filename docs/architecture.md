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
  `execute<Operation>` resolves the target chart, checks the operation's declared event caps,
  and passes `chart_document_t&` to its edit overload. Undo stores snapshots, so there is no separate command object. The proposal's payload, or the
  direct command's operation, *is* the edit.
- **Core edit:** a plain function or `apply(transaction&)` in `src/core` that changes chart data.
  This is where domain logic lives.
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
| A core edit | the editor in `src/core` named for the data (`bpm_editor`, `marker_editor`, …) |
| The measure lane | The measure rail made editable: `compose_bar_line_bounds` in `src/ui/compose_timing_geometry.h` is the one handle rectangle for drawing, picking and area selection, derived from grid lines that carry `measure_id`. Placing a bar line is `split_measure_action_t`; deleting merges through `remove_bar_lines`; pasting splits through `add_bar_lines`. |
| A row move | `stage_shift_selection` in `include/wormedit/core/time_shift.h` is the one executor for moving objects and bar lines by chart rows (step moves, shift from cursor, bar-line moves with or without time propagation). The drag gesture keeps its own lane and collision planning and runs its row component through it inside its commit; bar-line geometry itself is `move_bar_lines` in `measure_editor.h`. |
| Background work | `async_job_t` in `include/wormedit/core/async_job.h` |

Rules for writing each piece: [code style](chart-editor-plan/decisions/code-style.md).

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
