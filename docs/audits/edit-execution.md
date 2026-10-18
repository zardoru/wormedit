# Edit execution audit

Audit date: 2026-10-18. Base: `5248683`. Scope: first-party `src/` and `include/`,
including core, sys and audio consumers. This follows the owner's request to remove
manual rollups from deletion and stem conversion and audit the same pattern globally.

## Shared execution

`document_command_transaction_t::stage` owns working-document acquisition, chart
resolution, readiness and rejection. Native typed execution delegates to it; staging
and publication are separate phases. Nested stages share the same lazy working copy.
Failure blocks subsequent stages and publication without destroying the chart while
enclosing callbacks still hold references. A no-op stage does not discard earlier work.

The migrated chain covers ordinary/mixed editor deletion, linked BMSON marker deletion,
BMSON sound assignment, note-to-BGM conversion, BGM-to-lane conversion, note-only lane
assignment and mixed note/BGM lane assignment. These operations return their domain effect;
outer command/editor finishers own publication. No scratch histories remain in
`note_to_bgm.cpp`, and deletion obtains its working chart from the shared runner.

## Global inventory and remaining debt

Searches covered `document_history_t::from`, direct working-document/chart access,
`adopt_document`, direct document copies/assignments and `merge_recent_undo_records`.
The three runner implementation files own the permitted transaction machinery.
The exact per-file source footprint is in
[`cmake/edit_execution_allowances.txt`](../../cmake/edit_execution_allowances.txt).
Counts measure matching source lines, not functions or proof of semantic equivalence.

| Pattern outside the shared runners | At base | After this change |
|---|---:|---:|
| History construction | 31 | 27 |
| Direct working-document/chart access | 63 | 58 |
| Document adoption | 29 | 26 |
| Direct detached document copies | 4 | 4 |

The following existing compositions still need migration. They are explicitly retained
and frozen; this audit does not claim that every legacy path already uses staging.

| Owner | Remaining composition |
|---|---|
| `bmson_sound_channel_editor.cpp` | Insert a playable stem point by inserting then assigning on a scratch history. |
| `chart_clipboard.cpp` | Combine Kiai/recipe/timing deletion with ordinary deletion. |
| `chart_import.cpp` | Reuse the committing duplicate-chart command for fresh identities on a scratch history. |
| `interaction.cpp` | Keyboard entry combines occupant deletion/displacement and placement. |
| `interaction_gesture_lifecycle.cpp` | BMSON paint combines removals and multiple linked insertions. |
| `note_move.cpp` | BGM move previews, mixed linked-event moves, and displacement conversion. |
| `note_placement.cpp` | Virtual tap/hold materialization followed by staged placement. |
| `lane_profile_switch.cpp` | Materialize the initial target and apply its layout edit. |
| `timing_lane_controller.cpp` | Group move/delete calls committing per-event commands before adopting the result. |
| `resource_assignment.cpp` | Virtual resource/BGM/BGA edits; primary music import; resource creation; WAV slot and batch assignment. |
| `sound_import.cpp` | Batch import through a draft history. |
| `compose_bga_actions.cpp` | Batch image import through a candidate history. |
| `compose_input.cpp`, `audio/audition_resolver.cpp` | Detached BMSON draft validation/audition invokes a committing command on temporary history. |

Other findings have distinct ownership:

- `application.cpp` (startup and replacement) and `recovery_notification_feedback.cpp`
  construct a new session history. They are legitimate session boundaries, not edit rollups.
- Retained BMS reparsing, time-shift/tap-timing proposals and prepared gesture payloads can
  legitimately carry a detached document into the transaction. Adoption alone does not
  establish an inner-commit rollup. Their existing access sites are still recorded.
- Direct working-data access remains in metadata, resource, chart/lane/measure and
  specialized editors. Those bodies have not all been migrated to chart callbacks.
- `merge_recent_undo_records` has a declaration and definition but no callers; no current
  edit composes Undo records through it.

## Enforcement and verification

`architecture.edit_execution` scans all first-party C++ sources and headers. Every new
file starts at zero for the four detected patterns. Per-file allowances are exact:
increases fail, and decreases require lowering/removing the reviewed row. The checker
is a lexical growth barrier; aliases or a new equivalent spelling require code review.
The code-style rule remains validation, mutation and effect through the shared runner.

Behavioral regressions cover native stages sharing one working document, no publication
before the outer commit, nested rejection retaining storage while blocking publication,
nested no-ops retaining earlier edits, failed conversion retaining the live document,
restoring multiple events with only one remaining usable revision, and forwarding successful
stage warnings without treating warning-bearing no-ops as diagnostic-free acceptance.
