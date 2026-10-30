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

Compose timing staging is owned by `sys::runtime::timing_edit_t`: begin, draft updates,
confirmation, moves, deletion and cancellation all enter its dispatcher. The application routes
frontend actions to that owner; projections borrow its stage through `stage_view()`. This same
boundary supports event-harness workflows without a second dispatcher or direct test commits.

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

Document-only metadata edits may set publication options on `document_edit_effect_t`.
The shared finisher passes them to commit; project settings preserve existing imported-source
flags without enabling passthrough for sources that were already edited *(T-263)*.

Public BMS import, new-project creation and native load use immutable
`bms_program_document_t` snapshots in `song_document_t::prepared_bms_programs`.
Ordinary format difficulties remain in chart vectors; `read_charts` applies the shared
project chart order across both storage owners. It returns a borrowed pointer span from an
immutable cached index, invalidated by storage, chart identities, scoped owners and explicit
order. Copies validate their own storage before borrowing; concurrent first readers publish
one cache atomically. Effective resource indices live beside each playback/authoring projection,
so warmed selected catalog and chart-list queries allocate nothing. A scoped difficulty has no
flat mutable mirror.
`bms_program_edit_target_t` captures the document stamp, difficulty, scope and exact Chart;
scope selection resolves its first direct Chart. The active-preview API rejects inactive targets;
`for_authoring` explicitly resolves inactive or unreachable nodes without changing evaluation choices.
`document_history_t::read_chart` and transaction reads use that session target; document-only
`read_chart` continues to supply playback. The detached authoring view includes the selected
body's Charts and ancestor/common Charts, marking other owners read-only. Inherited object
updates require deliberately selecting their owning node. Imported hold endpoints without a
complete playback witness remain read-only markers with a warning, avoiding invented pairings.
Authoring-origin and read-only markers are derived metadata, never serialized or published.
`transaction.stage(bms_program_content_edit_batch_t)` changes authoritative typed content,
retaining recorded owners for updates/deletions. Cross-scope hold edits require a separate
explicit operation. The shared publication boundary checks bounded alternative evaluations,
new collisions and unresolved references before validation and the existing Undo publication.
Exhausting the evaluation budget rejects the entire edit. Each immutable owner retains at
most one projection result, keyed by exact evaluation choices and optional authoring Chart,
including its diagnostics. Reachability, publication, validation and read views reuse it.
Cache replacement is synchronized; readers retain immutable results and builders return
detached copies. New owners start cold, so content, settings, catalog and retained-source
changes cannot reuse stale evidence. Alternative evaluations still run all publication checks.
Projections remain detached values; the prepared difficulty cannot also have an independently
writable chart-vector counterpart.
Runtime event precedence follows program order; original source offsets remain provenance.
Prepared editor commands share the same typed-operation runner. It validates a detached
command proposal and extracts the explicitly named event or object IDs into a semantic
batch; it never publishes an edited projection or infers owners from source order.
`read_chart` supplies an immutable prepared view for planning; mutable `find_chart` still
resolves only authored legacy charts. A generic chart operation without a scoped adapter
rejects explicitly. Native additions retain exact measure fractions, symbolic WAV/BMP
references and nested identities; layer definitions/defaults remain difficulty settings.
Position-only timing edits retain their definition references. Imported note/hold annotations
remain on typed BMS content, and hold edits retain both endpoint identities and WAV values.
New BGM columns and semantic batches use the complete prepared/history identity ceiling.
A transaction cannot implicitly write multiple semantic Charts.

Flat BMS imports use one semantic Chart. Ordered opaque Text inside its content
retains blank lines, comments and unknown statements without creating another
edit target; leading Text can remain a separate node. Conditional imports retain
their ordered scope/Chart/Text boundaries. Both forms share the same Text byte
budgets, codec validation and writer, including source-equivalence verification.

The explicit BMS note-transfer command names source and destination Charts within one
scoped difficulty. It resolves selected notes through the source authoring projection,
collects both proven imported hold endpoints, then constructs one immutable graph with
both content lists changed. Moves retain identities; copies allocate above current,
Undo and Redo identities. Exact positions, attributes and symbolic WAV references remain;
the destination's ordered declarations supply their usual bindings. Source and playback
selection do not change. Mixed owners, retained sound-channel points and unproven or
cross-owner holds reject. Inactive imported endpoint markers still require a complete
playback witness; this command does not invent a pairing or change preview choices.
The shared publication runner validates all bounded outcomes, including hidden collisions
and re-pairing of surviving unchanged imported hold endpoints, and publishes one Undo step. Generic content batches retain their single-Chart restriction.

Prepared RANDOM range edits preserve all nodes and arm values, warn about unreachable
IF/ELSEIF arms, and clear an invalid explicit preview choice without choosing a replacement.
Shared evaluation reconciliation removes missing selectors and out-of-range RANDOM choices
before constructing the proposed immutable owner. The parameter and cleared choice publish
in one Undo step; invalid fixed settings or unprovable bounded outcomes reject the entire edit.

Prepared structural deletion previews the complete typed subtree and requires an explicit
confirmation tied to its original project, revision, difficulty and node. It removes only
that subtree, retains shared pools, and drops evaluation choices only for removed selectors.
Commit, Undo and Redo reconcile dangling prepared target IDs before publication observers:
walk the previous ancestry to the nearest surviving scope and choose its first Chart.
A valid session target remains selected through semantic Undo; history does not restore
an old selection. Missing difficulties, projects or previous ancestry clear the target instead.

Prepared unwrap previews ordered child promotion and its reachability/definition effects.
Affected IF continuations and selector-dependent branches become explicit bare scopes only
after the caller confirms unconditional conversion; ordinary promotion confirmation cannot
authorize that conversion. CASE conversion reports removal of selector-specific SKIP behavior.
All bodies and surviving node identities remain, unrelated nested conditions retain their
selectors, and the shared publication checks reject newly simultaneous collisions. Move,
delete and unwrap share proposal staging and detached preview execution. Native schema v15
adds bare scopes; BMS emits their ordered contents without conditional markers. Direct CASE
promotion and unproven fixed-setting bindings refuse rather than retaining hidden gating or
rebinding a selector.

Session target selection stays outside document history. Gesture and proposal stamps pin
that selection, including through terminal gesture cleanup and continuation after a
partial chord commit. Changing the session selection cannot retarget a captured insert;
captured existing-object updates resolve their recorded owner. The project-owned BMS scope
runtime owns node selection, drafts and destructive confirmations. Target changes clear object
and retained-range selection and transient selection history while preserving cursor/viewport;
valid nodes remain selected through document Undo. Scope tree and diagnostic views cache by
revision and selected node, separately from playback. Stale drafts require explicit reload and
rejected edits retain their text. Widgets emit typed, captured requests and never mutate the graph.
Delete, unwrap promotion and unconditional conversion use distinct frontend confirmation operations;
a confirmation cannot authorize a different pending operation.
Project/revision captures reject
another project even when its revision happens to match.

Notes/holds, resource placement/strokes/assignment, clipboard content, timing points and
Kiai spans, recipes, scoped metadata/offsets, selected moves/resize/delete/quantize/shift,
hitsounds, labels and layer edits route through these shared prepared boundaries.
Unconditional measure geometry, virtual-measure extension, complete lane mappings,
scoped catalog imports and full-program difficulty duplication/import have typed adapters.
Conditional geometry/timing links, retained-stem rewrites and unsupported multi-owner
operations retain atomic capability refusals with diagnostics. Shared resource and asset
metadata stays in canonical project pools; WAV/BMP declarations and references belong to
semantic Charts. Sound lists and pickers read effective aliases from the selected authoring
projection, without attaching scope aliases to the global resource pool.

Native schema v16 persists complete scoped programs, native difficulty settings, exact
fractions, retained source provenance, opaque Text bytes and shared catalog references.
Optional filesystem-resolution evidence preserves an authored token through automatic
extension/case lookup, but cannot authorize stale-source export after an explicit asset relink.
Public legacy native load migrates every BMS difficulty atomically. Migration reconciles
edited native values with the independent legacy codec witness, preserves annotations,
layers, native event families and inactive bodies, and allocates above current/Undo/Redo
identities. Unprovable or malformed legacy data rejects without publishing a partial graph.
Sparse ambiguous lane channels require an explicit profile through the asynchronous import
options dialog. The raw-statement editing API, source mutation helpers and legacy panel
routes are retired. The private read-only decoder/encoder remain codec witnesses and a
one-way migration boundary. Public BMS export writes the complete scoped program by
default; explicit flattening still uses playback choices and existing loss policy.

Timing links materialize through `reconcile_timing_links` in the shared publication boundary,
before document validation and journal/Undo publication. It compares each unchanged active
association with its previous effective category, detaches actual local writes, then resolves
sources in dependency order with measure layouts first. Exact mappings and stable local IDs
belong to the timing-link owner. A failed mapping, missing source or capacity failure rejects the
entire initiating edit and its propagation. Detached transaction previews use this same path;
Undo/Redo restore already resolved snapshots. Domain edits contain no link lifecycle plumbing.
Explicit link/detach/relink edits stage through the same transaction and require confirmation
before replacing local category data.

Keysound painting collects existing note identities through the gesture runner, then stages the
optional catalog assignment and armed osu! hitsample in one transaction. Ordinary resource
assignment shares `stage_resource_assignment`; painting never invokes committing commands on
scratch histories. Locked/hidden/conditional notes are skipped, and retained-source conflicts
reject the entire stroke.

Live history may attach `document_publication_observer_t` through the autosave loop.
The shared commit and Undo/Redo boundaries capture the accepted post-state with its prior
revision and command label without waiting for recovery workers. Detached previews constructed through the shared preview
runner have no observer. Domain edits contain no journal code.
`command_journal_runtime_t` diffs, serializes and appends through `async_job_t`; the single
writer retains the accepted snapshot and sequence metadata without decoding its own
output. Journal version 2 stores compact native seeds and id-keyed accepted-value deltas,
using the native codec's serializers and shared collection definitions. Immutable BMS
owners compare by pointer, then typed node/content identity before encoding changed values;
inactive bodies and shared catalog changes remain lossless. Deltas chain to the preceding sequence and checksum; the writer does not re-validate the whole document for a delta, only for a seed. Missing publications,
unsupported comparisons, size bounds and the 128-delta replay budget force a new seed.
Version 1 journals remain readable. Recovery parses each seed into an encoded tree,
patches deltas with cached collection positions, and runs full native validation once
on the final state of the segment; a reconstructed validation failure recovers the latest seed with
a diagnostic. Project saves are compact JSON; readers also accept indented files. A successful autosave
acknowledges its captured journal cursor before compaction, preserving later publications
and their required seed. Capacity or I/O failures request an early native checkpoint
through the existing autosave coordinator. Explicit Save and shutdown report durability
only after flushing the journal and any required checkpoint. Recovery preparation anchors
media paths for snapshots and journals; generator recipes retain their project-relative
reload origins and frozen source. Reader and writer budgets share
`command_journal_limits_t`; bounded queue overflow also requests a checkpoint.

Accepted audition requests travel on those outcomes to the shared finisher, which submits them
to the project-owned `compose_auditions_t`. Its input scope delivers the last object request,
then the last draft request, after the entire input batch. Its catalog scope delivers the
previous frame's catalog request before workspace actions can submit another. These scopes
borrow the existing audio services only while delivering; resetting project state clears pending
requests. Submission precedes rebinding, preserving an accepted request if rebinding later fails
*(T-652)*.

Not everything reaches the transaction. View-only actions such as zoom, scroll, selection, tool
switches and filters change interaction or UI state and stop at dispatch. Area-selection captures
permit cursor steps, page moves, position jumps and viewport scrolling while retaining their pointer
anchor; other gestures and document edits remain exclusive. Waveform analysis,
visual decoding, resource-copy and inspection requests, and conversion previews run through
`async_job_t` and publish their results on the UI thread. Autosave, transactional import/export,
and queue/progress services retain their domain lifecycles and publication rules; see
[async task scheduling](async-task-scheduling.md).

Compose's right dock uses `ui_layout_preferences_t` as its sole visibility/order/size/arrangement owner.
`src/ui/nuklear/dock.cpp` handles the strip, split and width captures; `compose_panel.cpp` wraps
existing panel renderers in independent scrolling groups. The typed record is stored under
`layout.compose_dock` in the existing atomic application preferences file. Dock record version2
adds the side-by-side preference; version1 records retain their stacked arrangement. Geometry
falls back to stacking when two minimum-width columns do not fit, without changing the preference.
The right strip owns panel activation and the arrangement toggle. Drag captures defer
publication until release; hiding Compose or losing input cancels capture without a late click.
Markers embeds the same bounded bookmark-list controls and projection as the popup chooser.
Position/name sorting is view state. Manual bookmark ordering is document-owned presentation
metadata, persisted natively and edited through the document transaction; it never changes
marker positions or scoped BMS content.

Display service ownership lives in `sys/app/application_frame_services`: the
event batch retains a second edge for the same key/button until the next frame,
leaving subsequent SDL events queued in order. Hidden/minimized/occluded frames
retire frontend commands without submitting them, while input, audio, jobs and
autosave continue. The application-owned display watchdog reads atomic progress,
reports a presentation stall once, and stops before SDL teardown.
SDL remains the cross-platform renderer abstraction. The audited private SDL
source override uses bounded acquire/recovery polling with presentation fences
on Linux, requires the maintenance extensions at startup, and preserves Metal
on Apple platforms. See [the override contract and checks](../cmake/sdl/README.md).

Cross-chart sound/BGA use activation and notification jumps build a detached
`workspace_navigation_t` in `src/sys/project/workspace_navigation.h`. It opens the chart and
moves the cursor through normal workspace and editor actions before publishing both view states.
Usage activation revalidates the projected revision and exact target identity first; BGA activation
also selects that event before publication. No document command is involved.

Compose input batches retain the admitted geometry revision. Lane-key events additionally carry
a chart stamp advanced only by their own accepted synchronous edits, before feedback callbacks.
Keyboard navigation in that batch uses the same stamp, so a chord release or snap move can follow
the note's commit in one frame. Pointer/viewport inputs still require the original geometry stamp.
External changes and failed rebinding stop continuation.

The semantic-layer session's `effective_solo_layer` in
`src/sys/app/application_command_state.h` supplies the current chart's selection policy.
Its implementation lives in `src/sys/app/application_command_state.cpp`, so changing
eligibility alone does not recompile every session-header consumer.
Pointer batches, Compose view actions and registered scoped-selection commands install it
before selection; same-sound queries read it directly. Projection reads the effective policy
into detached views and scene requests without repairing retained interaction state.
Application command-state refresh retains invalid-target and brush cleanup.

`src/sys/compose/input/compose_input_admission.h` admits paint, Draw-source, span-handle
and recipe-command selectors once at each input event boundary. Unsupported wire values
return ordinary feedback before handler dispatch. Admitted family payloads live in the
event-scoped input context; routes must pair each event with its corresponding payload
alternative. Updates/releases retain captured gesture intent where that gesture owns it.
The frontend action bag remains transport and can carry simultaneous frame events; stamp,
gesture and transaction validation stay with their existing runners.

Lane-key deletion publishes immediately while retaining the pressed key's original row as a
hold anchor. Moving away previews a new hold through the same gesture; release commits it.
Release at the anchor or cancellation leaves the deletion in place. The shared gesture runner
recaptures surviving keys after each publication, including independently released chord keys.

Selection commits share Undo/Redo chronology with document commands through the history owner's
monotonic event sequence. Selection snapshots live only in its bounded transient selection
history, not in song snapshots, codecs, saved revisions or document dirty state. The editor
dispatcher records accepted selection changes once; marquee previews and cancellations record
nothing. Both keyboard and command-menu history routes use `apply_editor_history`. A new accepted
selection change discards selection Redo but preserves pending document Redo after the new
selection event; a document edit discards both redo branches. Pointer presses reserve capture without
replacing selection; release commits a click or marquee, and Escape consumes an active capture
without also invoking Clear Selection.

Note-paint proposals retain explicit canvas rows for virtual cells, without inventing authored
measure IDs for their previews. Release materializes the required extent and paints the stroke
in the same transaction and Undo step. Cancellation and erase-only strokes do not create empty
measures.

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

Timing edits may retain musical positions or preserve sound-object audio times through
`stage_timing_reposition` in `core/timing_reposition.h`, composed before the shared transaction
publishes. Its report counts moved objects and positions requiring explicit quantization.
`document_command_transaction_t::preview` runs that same command boundary on an isolated
copy with no retained Undo, so preview reports and rejection use the real edit path without
publishing the live document. The timing controller owns only its candidate and report.

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
| A direct command's route | `application_command_route_t::from` in `src/sys/commands/application_command_dispatch.cpp`; `compose_action_route_t::find` and the constexpr module route tables in `src/sys/compose/actions/compose_action_routes.h` and `src/sys/compose/actions/compose_actions.cpp` (with the area, resource, marker, bookmark, BGA and sound module tables). The typed route runners in `compose_action_routes.h` bind actions and use `run_compose_action` in `include/wormedit/core/proposal.h`; `finish_application_command` / `finish_compose_action` in `src/sys/compose/actions/compose_action_outcome.cpp` complete the outcome. |
| An editor action's capability and execution | `editor_action_handler_t<Action>` in `src/core/interaction.cpp`; `editor_action_dispatch_t` checks its declared virtual-cursor policy, prepares the view/cursor context once, and calls `perform`. Every variant alternative must satisfy the handler contract; there is no default. Gesture cancellation, stale capture, admission and terminal cleanup remain in `interaction_gesture_lifecycle.cpp`. |
| A gesture | `src/core/interaction_gesture_runner.h` (`editor_gesture`, `begin_gesture`/`revise_gesture`/`apply_gesture`/`cancel_gesture`, implementing begin/update/commit/cancel) and the gesture's own file |
| A proposal | `proposal_t<`, `apply_proposal` in `include/wormedit/core/proposal.h`, and the payload's type |
| The executor | `document_command_transaction_t` in `include/wormedit/core/command.h`: construct with history (and an optional captured stamp); `document()` creates the working copy once, `current_document()` reads without copying, `source_document()` / `source_chart(id)` read the committed version, and `outcome()` carries diagnostics. `is_ready()` reports a stale capture born rejected. |
| Shared edit values | `include/wormedit/core/document_command_types.h` owns stamps, results and effects; `position_types.h` owns measure-local positions and spans. Include these contracts when document/history/execution definitions are unnecessary. |
| Structural chart collections | `src/core/chart_collections.h` owns flat object-ID collection membership and structural position roles. Allocation, identity traversal, labels, focus, diagnostics, redivision, quantize and shifts derive applicable traversal from it. References, nested objects, optional positions, endpoint rules, timing-link categories, validation and codec fields retain their domain owners. |
| A committing note edit’s editor effects | `editor_edit_effect_t`, `prepare_editor_edit`, `run_editor_edit` and `commit_editor_edit` in `src/core/interaction_edit_commit.h`: stage a detached working document, commit once, then apply selection/cursor/lane, update surviving capture stamps and build the result. Internal delete/tap reuse shares action context preparation and staging instead of dispatching on scratch histories. Domain commands that already own a transaction use the same finisher. |
| A core edit | the editor in `src/core` named for the data (`bpm_editor`, `marker_editor`, …) |
| Area selection | `cache_area_object_geometry` in `src/sys/compose/views/compose_view.cpp` projects rectangles when a scene snapshot exists. `editor_objects_in_row_range` in `src/core/interaction_selection.cpp` is the row-range fallback; `is_scope_accepted` and `is_editor_selection_scope_accepted` filter both by scope. |
| Compose selection policy | `effective_solo_layer` on the semantic-layer session in `src/sys/app/application_command_state.h`, implemented in `src/sys/app/application_command_state.cpp`; registered, view-action and pointer dispatch install it before selection. Projection only reads it; same-sound selection uses it in `src/sys/compose/compose_sound_selection.h`. |
| Compose selector admission | `admit_compose_input_selector` and bounded family payloads in `src/sys/compose/input/compose_input_admission.h`; `dispatch_compose_input_event` in `src/sys/compose/input/compose_input.cpp` admits each event before routing. |
| Paste | `edit.paste` enters through `begin_compose_paste`; `compose_clipboard_workflow_t` in `src/sys/compose/compose_clipboard_workflow.h` and `src/sys/compose/compose_clipboard_workflow.cpp` owns admission, revision, application and the retained `compose_paste_session_t`. Input and projection call the owner's `revise`/`apply` directly. `chart_paste_proposal_t` in `include/wormedit/core/chart_clipboard.h` owns the core proposal; `build_compose_paste_geometry` and ghost projection remain in `src/sys/compose/views/compose_view.cpp`. |
| Retained application workflows | `src/sys/app/application_command_state.h` declares application services and project-lifetime workflow groups. Clipboard and quantize use their dedicated state owners; scene caches remain in `compose_view.h`. Quantize settings, presets and typed selection application share `src/sys/compose/actions/compose_quantize.cpp` and its route module. |
| The measure lane | The measure rail made editable: `compose_bar_line_bounds` in `src/ui/compose_timing_geometry.h` is the one handle rectangle for drawing, picking and area selection, derived from grid lines that carry `measure_id`. Placing a bar line is `split_measure_action_t`; deleting merges through `remove_bar_lines`; pasting splits through `add_bar_lines`. |
| A row move | `stage_shift_selection` in `include/wormedit/core/time_shift.h` is the one executor for moving objects and bar lines by chart rows (step moves, shift from cursor, bar-line moves with or without time propagation). The drag gesture keeps its own lane and collision planning and runs its row component through it inside its commit; bar-line geometry itself is `move_bar_lines` in `measure_editor.h`. |
| Frontend frame data | `ui::workspace_view_t` in `src/ui/frontend/workspace.h` owns C++ presentation data, including `compose_view_t`. The application retains main/Compare `sys::frontend_workspace_view_t` projection owners and calls `refill` in `src/sys/app/application_frame_view.cpp`; their live query caches and geometry scratch stay in sys. Views retain list/text capacity, with no frontend arena, pointer rebinding or C ABI codec. |
| Frontend dependency seam | `cmake/check_ui_boundary.cmake` covers private/public core, audio and format headers and implementations, and frontend contracts. Frontend direct imports admit a reviewed list of shared value headers; model/history/state services and resolved private first-party paths are rejected. Guard regression cases live in `cmake/check_ui_boundary_tests.cmake`. Macro/alias and arbitrary transitive dependency proofs remain outside this source guard. |
| Background work | `async_job_t` in `include/wormedit/core/async_job.h` |

Rules for writing each piece: [code style](chart-editor-plan/decisions/code-style.md).

### Feature-to-owner index

| Feature | Public entry and capability | Workflow/session owner | Core operation | Completion and regression home |
|---|---|---|---|---|
| Paste | `edit.paste`; `paste_target_available` and `refresh_application_command_states` in `src/sys/app/application.cpp` | `application.project.clipboard` is a `compose_clipboard_workflow_t`; `begin`/`revise`/`apply` and `compose_paste_session_t` in `src/sys/compose/compose_clipboard_workflow.h` and `src/sys/compose/compose_clipboard_workflow.cpp`. `begin_compose_paste` in command dispatch additionally reads the clipboard service and clears paint modes after admission. | `chart_paste_proposal_t` and `paste_chart_clipboard` in `include/wormedit/core/chart_clipboard.h` | `document_command_result_t`; `tests/core/chart_clipboard_tests.cpp`, `tests/sys/compose_notification_tests.cpp`, `tests/ui/walkthrough_bms_event_harness_tests.cpp` |
| Quantize | `edit.apply_quantize` and `WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE`; `refresh_quantize_commands` and `can_apply_compose_quantize` | `compose_quantize_editor_for` and `toggle_compose_quantize_preview` in `src/sys/compose/actions/compose_quantize.cpp`; retained state remains in `application_command_state_t::project.quantize` | Typed `apply_compose_quantize_selection` in `src/sys/compose/actions/compose_quantize.cpp`; `quantization_plan_t::from` and `apply_quantize_command` own the core operation | `quantize_command_result_t` mapped once to `compose_action_outcome_t` by the typed workflow; `tests/core/quantize_tests.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp` |
| Note ↔ BGM | `edit.move_to_bgm`, `edit.move_next_to_bgm`, and `WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE`; `compose_move_to_bgm_available` | Current editor selection; BGM drop dispatch in `dispatch_note_to_bgm_input` and `move_selected_bgm_to_lane` | `stage_move_notes_to_bgm` and `stage_move_bgm_events_to_lane` in `include/wormedit/core/note_to_bgm.h` | `selected_note_to_bgm_result_t` or `editor_interaction_result_t`; `tests/core/note_to_bgm_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/compose_notification_tests.cpp` |
| Measure editing | `measures.split_at_cursor` and the measure-lane Compose actions; `compose_measure_edit_rejection` | `application_command_state_t::project.compose_view` owns lane visibility/policy; `measure_resize_t` in `src/ui/nuklear/compose/compose_state.h` owns resize capture | `measure_edit_action_t`, `apply_measure_edit`, `add_bar_lines`, `remove_bar_lines`, `move_bar_lines` in `include/wormedit/core/measure_editor.h` | `document_command_result_t`; `tests/core/measure_editor_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/ui/compose_measure_resize_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp` |

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
   and its `compose_action_route_t` entry in `src/sys/compose/actions/compose_actions.cpp`.
3. Emit it once from the owning frontend control and verify both route and feedback. Closed action
   tables must fail at compile time when an entry is missing.

### Adding a toolbar button

1. Put rendering and emission in the owning toolbar group; `render_measure_lane_toggles` in
   `src/ui/nuklear/compose/compose_toolbar.cpp` is the example.
2. Update the toolbar layout count in the same file so the control is reachable at supported
   widths. Derive enabled/checked state from the view or command registry instead of duplicating
   capability rules.
3. Add an event-harness assertion for the emitted typed action and any visibility-dependent
   companion control.

### Adding a Compose view field

1. Define a backend-neutral C++ view type with owned storage and queries, like
   `compose_measure_lane_view_t` in `src/ui/frontend/compose_measure_lane.h`; add it to the owning
   Compose view in `src/ui/frontend/compose.h`.
2. Fill it at the sys projection owner (`src/sys/compose/views/compose_view.cpp` for the measure lane), carrying
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
checks above. Ordered post-processing transforms the immutable batch before composition.

Lua recipes use the same prepared owner. Core declares the script session interface and owns
fixed/adaptive traversal; `scripting::lua_curve_backend_t` supplies bounded calls. An explicit
`sv_recipe_evaluation_scope_t` installs a backend only on a worker/build thread and restores
the previous scope on exit. Each independent preview/export starts fresh sessions from frozen
source and seed. Ordered sessions consume a fixed ascending-row schedule once, with cycle
counts partitioning that schedule, including the endpoint inspection query. Their immutable
output holds the preceding value between emissions. Pure sessions additionally verify retained
state and reverse/repeated-query consistency. Composition and post-processing never call Lua.
`sv_recipe_prepared_scope_t` supplies exact matching immutable results to frame-thread Apply;
the caller retains responsibility for captured project/chart/revision stamps. Missing or changed
Lua results reject rather than evaluating synchronously. The inspector owns evaluation through
`async_job_t`, cancellation and snapshot publication, and preserves its last good graph on errors.
Native schema v17 stores the API-v1 Lua descriptor and frozen source, never prepared samples.
Module paths are project-relative reload origins; Reload reads and hashes their bytes on a worker.
The output-group worker and headless converter explicitly scope fresh export backends; format
writers and core remain independent of Lua.

The [extension checkpoints](chart-editor-plan/tasks/extension-friction.md) record the current
SV capability references, cross-cycle state direction, bounded lane/gesture findings,
the T-681–T-683 implementation scopes and their bounded verification evidence.

## File preparation and publication

Opening a project reads/parses/normalizes into a detached `project_open_result_t`
through `open_project_file` (`sys/project/project_open.cpp`). The application submits selected files through `project_open_runtime_t` over `async_job_t`.
The worker owns the path, encoding and byte progress; the UI polls its completion. `finish_project_open` owns encoding retry/error feedback and only
passes successful results to `publish_opened_project`. That UI-thread method owns
session replacement, imported session identity, recent-file updates and aggregated
warnings. The job generation, cancellation and captured document/session admission precede this
completion boundary, keeping the previous document until replacement. Reading checks cancellation
between bounded chunks; parser calls are non-interruptible, and cancelled parse results are discarded.

`output_group_export_plan_t::from` (`sys/project/output_group_export.h`) owns format request
policy shared by preview and publication, including StepMania filename/profile and
BMS sliced-layout rules. `output_group_export_preparation_t::from` shares asset discovery,
pending-copy planning, conversion and publication preflight between Project Preview and Export.
Preview lists the planned destinations and diagnostics without writing files.
`output_group_export_t::start` admits the request before
copying its document, owns task submission and captures all worker inputs.
`run_output_group_export` (`sys/project/output_group_export.cpp`) owns staging/conversion and
calls the existing atomic publisher. The runtime owns completion and notification.
Cancelling retains busy state until terminal polling, and a separate domain receipt
preserves publication evidence when scheduler cancellation replaces the task outcome.
Conversion and preview expose advisory artifact counters and check stop tokens between
writers/reimports. Atomic staging checks cancellation between existing files, generated
artifacts and immediately before exchange. Automatic music, keysound and image copies are pending mappings
until copied into private staging, so rejection/cancellation cannot leave a new output
folder or audio file. Completed exchange remains published even if cancellation races
with completion. Individual writers and filesystem calls are not preemptible.
Song-wide export cuts are source-song millisecond metadata, edited by the marker transaction.
`export_cut_timeline_t` owns union normalization and exact source/collapsed time queries;
`export_cut_axis_t` adapts this to source rows, while `export_cut_frame_timeline_t` quantizes
absolute edges once for audio. Compose keeps authored source identities and borrows these maps
for projection, pointer inversion, navigation, waveform sampling and prepared playback schedules.
Timing focus uses the full source clock. The existing audio-span capture owns both preview and
cut-edge drags; release routes the cut proposal through the same transaction as typed edits.

`export_cut_transform_t` creates a detached export document and generated-audio plans. Common
conversion applies it before target writers; the decoder/publication owner renders the plans
inside existing staging, so rejection or cancellation cannot replace published files. Export
uses one 48 kHz generated clock, preserving exact integer-millisecond seams across charts and
music assets. The no-cut path retains its original conversion and audio behavior.

Selected-area audio export belongs to `runtime::preview_audio_export_t`
(`sys/audio/preview_audio_export.h`) over `async_job_t`. Its admitted request owns the
document snapshot, source paths, row interval and trim/fade/encoder settings. The worker
uses the ordinary timing schedule, shared music binding and hitsound resolver with
`render_offline_interval`, then encodes Vorbis and decode-verifies a same-directory
temporary owned by `prepared_atomic_file_t`. UI-thread polling checks the document stamp
and selected chart before explicit atomic publication. Cancelled, stale and abandoned
results destroy their temporary without replacing the destination. Replacement is explicit;
project asset destinations are rejected even when replacement was requested.

osu! note samples are semantic `osu_hitsample_t` values, persisted by the native codec.
Volume and sample-set timing controls are typed chart events, each unique per lane/row.
Their columns are independently optional and hidden by default, including in Timing focus.
They use the ordinary timing proposal, selection and clipboard paths. Import and schema27
migration lift retained timing-line sample fields into these owners; empty saved lanes stay
empty. The sample-context index combines these held controls with retained custom indices.
Export combines coincident sample changes with BPM/SV/effect lines and emits inherited
sample-only lines carrying the active SV. Addition sets remain hit-object overrides.
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

Latched keyboard range selection is a view-only gesture that permits playback cursor following.
Its captured anchor and stamp stay fixed while the ordinary playhead cursor supplies its tail.
Transport shortcuts preserve this gesture; Escape stops inline playback first and keeps the last
endpoint instead of restoring the transport origin. A stopped Escape or a click on the chart
canvas outside playable lanes cancels through the existing gesture runner. Shift-held ranges and
authoring gestures retain their existing playback exclusion and input-loss safeguards.

Chart playback publishes audible frame, observation time, effective rate, loop bounds
and the rendered frontier through the existing coherent audio snapshot. The frame
thread projects that evidence once to expected presentation time; following, transport
and BGA read the same cached chart frame. Prediction cannot pass rendered PCM and
holds after 100 ms without a report. Genuine seeks/rate changes reset correction;
ordinary loop wraps retain the unwrapped clock. Frame timing estimates presentation
latency from recent completed work, excluding explicit pacing sleep. This changes
display projection only, without advancing audio or changing paused positions.
The status text owner refreshes playback text at 12 Hz and invalidates immediately
on input or changed status/selection metadata. Overview/playhead geometry keeps the
display cadence; revision-stable inspector/BGM rows retain their existing owners.

## Composing edits

Use `document_command_transaction_t::stage` for each component and publish at the outer
boundary. Chart callbacks receive `chart_document_t&` and `song_document_t&`; document-wide
callbacks receive `song_document_t&`. Return `attempt_t<Effect>` with domain validation,
mutation and effect. A missing value or error rejects the transaction, including all previous
stages. An unchanged effect leaves earlier successful stages available for the outer commit.
Callbacks must preserve the document/chart storage borrowed by enclosing callbacks.

Typed `chart_edit_operation` declares `storage_support`: object edits with a native collection
adapter, or an intentional `legacy_only` refusal. Composite edits pass a `chart_edit_mutation_t`
scope to `stage`: existing-object updates or combined layer-settings/object edits.
The transaction selects the existing authoritative prepared adapter and retains its exact
owner catalog through `read_projection`. Combined settings/object callbacks may run twice and
must be deterministic; content publication uses only the explicitly named object identities.
Domains still supply validation, mutation and effect, with one lazy copy and outer publication.
Other domains continue using their named settings or geometry adapters; this composite API does
not add unused mutation modes.

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

Compare/Validate reads complete playback projections rather than the session's selected BMS
authoring Chart. Its pane cache adapts one detached document snapshot to the existing Compose
projection API, with no authoring target or semantic Undo records; it never runs chart edits.
The adapter is rebuilt only when the document/pair cache key changes. Both fields and their
difference rows therefore share playback choices while the user's authoring selection remains
untouched. The right-hand picker offers only difficulties other than the active left pane.

The optional `wormedit::scripting` target (`WORMEDIT_ENABLE_LUA=ON`) owns the Lua 5.4
sandbox behind an opaque, move-only session. The default memory cap is 8 MiB, with one
million VM instructions, 64 KiB source/result limits and an explicit SplitMix64 seed.
The allocator charges retained capacity and its aligned headers; shrinking cannot fail.
One count hook and a cooperative stop token cover the complete session, including repeated
calls and RNG draws. They are not a wall-clock interrupt for standard-library C functions.
Only base/string/table/math/utf8 libraries are installed. File/module/process/debug access,
host output, GC control and metatable access/installation are absent. Dynamic loading accepts
bounded text strings, never binary chunks or reader functions. `xpcall` runs its error handler
only after an inner protected call restores instruction hooks. User finalizers are excluded
because Lua disables hooks during GC finalization. Callbacks that may raise contain only
trivial automatic values; setup, parsing and execution stay inside `lua_pcall` boundaries.
Globals and closures survive successful calls in one session. Any host evaluation failure
closes the VM; only bounded scalar results and typed errors leave the wrapper. No document
binding or native VM serialization crosses the sandbox boundary. T-189 adds protected retained
function calls and recursively immutable context userdata; their backing tables never leave the
proxy. Pure calls inspect bounded reachable closure state and reject retained mutation or opaque
mutable userdata. The recipe adapter exposes only owned value snapshots and scalar results.

The API-v1 `lua_document_snapshot_t` builds owned, bounded `wormedit` snapshots before running
Lua. Measures, timing, lane layouts, selection and existing MIDI fields carry exact IDs and
rationals; no live document pointers enter a VM. The general snapshot exposes modeled MIDI,
while generator execution filters it to declared inputs at the scripting boundary. Extra MIDI
categories and file parsing remain separate work. Named snapshots use the sandbox's recursive
immutable proxies. Structured script results leave Lua only after bounded raw inspection with a reserved Lua
stack; Lua errors never unwind host-owned result objects. Host construction and extraction
charge the complete evaluation budget.

Note generators return keyed descriptors to `generator_preview_t`, a typed core proposal. The
core owner validates placement, hold spans, layer definitions, target ownership and historical
output witnesses, then uses the ordinary proposal runner and transaction. Worker snapshots rebuild
scoped BMS read owners from immutable prepared data so authoring caches stay private; chart
views are reacquired after proposal validation. Workers capture one
history identity floor instead of copying Undo/Redo; shared BMS native/layer adapters preserve
explicit history-safe insertion IDs while retaining their existing normalization/publication
checks. Generated chart objects remain ordinary editable data. The last materialization stored
with a recipe is a historical witness, not a writable chart mirror. Stable keys retain IDs;
changed or deleted authored outputs reject regeneration. Detach removes only recipe ownership.
`generator_runtime_t` owns explicit Run, hash-change acknowledgement, cancellation, stale checks
and immutable preview publication through `async_job_t`. Ordinary source/input preflight precedes Lua;
acknowledgement runs the captured bytes. Project entry points use `lua_project_source_t`
(T-186): explicit bounded discovery/replay resolves ordered includes and named asset hashes
inside the native project directory, then freezes them into the existing recipe source.
Discovery/replay and frozen evaluation/replay split the normal instruction budget. Canonical
paths reject symlink escapes; cycles and malformed module output reject without publication.
The host reader capability exists only during discovery and retains no raw Lua references;
frozen execution uses a Lua-owned source table, with no filesystem access. Named asset
declarations expose hashes and optional already-imported IDs; import/MIDI selection remain
explicit. Bounded discovery may execute before changed-source confirmation; acknowledgement
still uses only the captured bundle. No native schema change or implicit load/save execution
is introduced. Independent sandbox replay verifies typed output under
one cumulative budget. Frame-thread Apply consumes the prepared proposal and never runs Lua.
Schema v19 persists frozen generator metadata and witnesses; loading/saving never regenerates.
T-187 adds chart descriptors through a trusted sandbox result adapter, preserving user chunk
names and line numbers. `generator_chart_content_t` witnesses generated measures, notes/BGM,
layout/input and declared timing; `generator_chart_binding_t` maps stable declaration keys to
native chart/object IDs. `generator_preview_t` allocates history-safe IDs and stages all owned
difficulties through the same transaction. The shared chart ID visitor serves duplication and
generator remapping; historical declarations reserve ceilings without counting as live objects.
Schema23 stores these optional bindings. The same generator host accepts keyed `sv_points`
and `sv_spans`, decoded to canonical scroll events and built-in SV recipes. Witnesses protect
manual edits and retain identities; BMS materialization shares the native event adapters.
Points set the held authored baseline, while half-open spans compose through the existing
SV recipe owner. Generated difficulties acquire recipe-list ownership only when declaring
spans, preserving independent recipes for older scripts. Additive optional schema23 fields
store these witnesses; no VM state or sampled recipe cloud is persisted. The inspector summarizes difficulty output instead of
projecting it onto the launch chart. Independent authored positions block resolution changes;
full document validation rejects regeneration that removes their referenced measures.

SV recipe impulses are structural normalized-position areas, persisted by schema v18 alongside
their exact millisecond box width. Preparation converts them to right-owned, half-open boxes on
the exact chart grid; unrepresentable widths, warped spans, range overflow and non-instant
SPEEDS transitions reject. Ordinary built-ins keep their analytic continuous values. Processed
and explicit materialized batches keep held values. Post-processing accounts for impulse area;
composition multiplies continuous factors and adds impulse contributions, freezing other factors
at each impulse position without forming impulse cross-products. Coincident sources add before
weighting by nonparticipating continuous recipes.
Preview and export consume the same prepared boxes and expose realized area/error without
persisting sampled clouds.

SV recipe Copy/Paste retains a project-owned definition with a stable recipe key.
Each placement keeps its own ID, range and enabled flag; the definition owns all
other built-in and Lua configuration, including frozen source and its descriptor.
Existing chart consumers read effective placement snapshots. The shared publication
boundary reconciles configuration edits through the definition and refreshes all
uses after timing-link resolution, before validation and one Undo publication;
conflicting edits reject together.
Deleting a placement retains the definition, and Duplicate explicitly creates an
independent recipe. Native schema23 stores definitions once and placements as
references, rebuilding effective snapshots on load. The optional definition list
defaults empty for other additive schema23 files. Scoped BMS recipe clipboard
operations currently refuse explicitly until reference edits have a semantic adapter.
The single schema22-to-23 migration initializes both this definition pool and empty
generated-chart bindings; both codecs also accept their absent optional additions
on documents already tagged schema23.

The current native document schema is 28. Schema28 adds per-output-group audio encoding
settings; migration defaults missing settings to Vorbis with quality 0.4. Schema27 adds typed volume and sample-set control
lanes, migrating legacy osu! timing-line sample fields when the lanes are absent. Schema26 permits ordered opaque Text
inside Chart content for flat imports. The 25-to-26 migration preserves existing
authored nodes and identities; fresh flat imports no longer split at Text.
Schema25 adds the saved per-output-group
BMS flattening option; the additive 24-to-25 migration defaults missing choices to preserve.
Schema24 adds optional per-difficulty bookmark
list order independently of timeline positions and scoped BMS content. The 23-to-24
migration leaves data unchanged: absent orders keep the default timeline order,
and orders from the earlier unbumped schema23 build are retained. Writers use the current schema;
readers refuse newer schemas before publishing a document. Recovery snapshots and
command journals use the same native codec and schema limit.
