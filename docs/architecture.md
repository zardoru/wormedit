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
  payload plus the document stamp it was planned against. `revise` updates it. Applying it is
  the commit *(T-630, T-634)*.
- **Transaction (the executor):** `document_command_transaction_t` is the only way to change the
  document. It checks the stamp, copies the document lazily once, runs the edit, rejects
  without publishing, detects a no-op, and commits once with its Undo label *(T-607, T-637)*.
  `execute<Operation>` resolves the target chart, checks the operation's declared event caps,
  and passes `chart_document_t&` to its edit overload. Undo stores snapshots, so there is no separate command object. The proposal's payload, or the
  direct command's operation, *is* the edit.
- **Core edit:** a plain function or `apply(transaction&)` in `src/core` that changes chart data.
  This is where domain logic lives.
- **Results going up:** the transaction returns the command result. The gesture path returns it as
  `editor_interaction_result_t`, and the direct-command path as the Compose outcome, which one
  finisher turns into feedback and rebinding *(T-618)*. One shared failure type instead of
  per-feature outcomes *(T-633)*.

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
| A direct command's route | `application_command_route_t::from` in `src/sys/application_command_dispatch.cpp`, `src/sys/compose_actions.cpp` (the `compose_action` concept, `run_compose_action` in `include/wormedit/core/proposal.h`); command completion is `finish_application_command` in `src/sys/compose_action_outcome.cpp` |
| A gesture | `src/core/interaction_gesture_runner.h` (`editor_gesture`, `begin_gesture`/`update_gesture`/`commit_gesture`/`cancel_gesture`) and the gesture's own file |
| A proposal | `proposal_t<` and the payload's type |
| The executor | `document_command_transaction_t` in `include/wormedit/core/command.h` |
| A core edit | the editor in `src/core` named for the data (`bpm_editor`, `marker_editor`, …) |
| Background work | `async_job_t` in `include/wormedit/core/async_job.h` |

Rules for writing each piece: [code style](chart-editor-plan/decisions/code-style.md).
