# Edit publication: one commit tail, one failure shape

[Back to task cards](../tasks.md).

A review on 2026-10-18 (owner request: "several gestures and actions have a publishing step… does that need to live so close to the data manipulation?") found that the note-editing gestures and editor actions bypass the proposal runner (`apply_proposal_edit` in `include/wormedit/core/proposal.h`) and write their own post-commit tail: map a commit failure, apply selection, cursor and lane, sometimes re-stamp a surviving gesture, and build `editor_interaction_result_t`. The tails have drifted. Commit failures come back both with a view and without one, and the hold apply sets `accepted` and `document_changed` by hand. Because inner steps return results that may or may not carry a view, each layer above re-checks and rewraps them (`note_placement.cpp` checks one result three times). Five internal calls run the public dispatcher on a scratch copy to reuse an edit; each one commits into the scratch history and then rebuilds the view against the real one.

These cards follow the code-style rule "A migration finishes in its card, or it ratchets" (owner, 2026-10-18): every site converts here, and ratchet rows stop the old forms coming back. Execute in order T-686 → T-687 → T-688; T-689 is independent. Counts below were taken at `3fdb938`.

## T-686 Attach the failure view once, at the public boundary

- **Status:** ready
- **Value:** High. It removes about 110 view-building calls and every rewrap, and T-687 and T-688 rely on a failure being only diagnostics.
- **Outcome:** Edit code returns a failed `editor_interaction_result_t` with diagnostics only. `dispatch_editor_action`, the public gesture runner functions and every other public entry point found in step 3 attach one view, built from the real history and state, to a failed result that has none. `diagnostic_result` and `cursor_diagnostic_result` no longer exist (ratchet `interaction_failure_view` at 0).
- **Read first:** `docs/architecture.md`, `src/core/interaction_gesture_runner.h`, `src/core/interaction_gesture_lifecycle.cpp` (`dispatch_editor_action`), `src/core/interaction.cpp` (`diagnostic_result`, `editor_action_dispatch_t`), `cmake/check_ceremony_ratchet.cmake`.
- **Files owned:** `include/wormedit/core/interaction_state.h` (the result type only), `src/core/interaction*.cpp`, `src/core/interaction*.h`, `src/core/note_placement.cpp`, `src/core/note_move.cpp`, `src/core/chart_clipboard.cpp`, `src/core/note_to_bgm.cpp`, `cmake/ceremony_ratchet.txt`, the core tests that assert failure views, this card.
- **Steps:**
  1. Stale-plan check: `grep -rnE "(cursor_)?diagnostic_result\(" src/core | wc -l` (expect 111, 22 of them `cursor_diagnostic_result`).
  2. *(Done 2026-10-18 by the coordinator, ahead of the lane.)* The ratchet's directories column takes paths (`[a-z/,]+`), and `cmake/ceremony_ratchet.txt` holds this lane's rows at their frozen counts: `interaction_failure_view` 111, `core_committed_result` 12, `scratch_dispatch` 4, `manual_gesture_restamp` 1 (all `src/core`), `sys_workspace_rebind` 4 (`src/sys`). Confirm with `ctest --test-dir build -R '^documentation\.ceremony' -V`.
  3. Audit and record here: (a) every failure return whose view uses a state or history other than the one the boundary will see (a local `proposed`/`staged` copy, a scratch history); (b) every public function declared in `include/wormedit/core` that returns `editor_interaction_result_t` and does not pass through `dispatch_editor_action` or the gesture runner. **STOP** and report to the owner if any (a) entry is a failure view that deliberately shows a state the caller does not end up with.
  4. Add `editor_interaction_result_t::ensure_view(history, state)` (fills `view` on a failed result that has none) and call it in `dispatch_editor_action`, `begin_gesture`, `revise_gesture`, `apply_gesture` and the entry points from step 3(b).
  5. Mechanically replace each `diagnostic_result(h, s, d)` and `cursor_diagnostic_result(h, s, d)` with `editor_interaction_result_t{.diagnostics = d}`, using a scripted edit, then review the diff. Delete both helpers and the rewraps: the `!result.view` branch in `finish_gesture`, the rewrap in `dispatch_editor_action` (keep its `state.clear()`), `note_placement.cpp` around lines 305, 366 and 437. `editor_gesture_state_transaction_t::finish` keeps its rollback and clears the stale view so the boundary rebuilds it.
  6. Lower `interaction_failure_view` to 0.
- **Target shape:** `if (!placed) return {.diagnostics = std::move(failure)};` in edit code; at the boundary, `auto result = dispatch_editor_action_impl(history, state, action); result.ensure_view(history, state); return result;`. `grep -rn "diagnostic_result(" src` prints nothing.
- **Invariants:** Every failed result a caller gets from a public entry point carries the same diagnostics in the same order as today, and a view whenever one can be built. Diagnostic text is unchanged. Successful results are untouched.
- **Reject/failure behavior:** When no view can be built, `view` stays empty as today. Rolled-back gesture state is what the attached view shows.
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.'` shows `interaction_failure_view: 0`; `ctest --test-dir build -L event-harness --output-on-failure`.
- **Out of scope:** Commit tails (T-687); scratch-copy dispatch (T-688); successful results of non-committing actions (cursor, selection, viewport), which build their view once already; sys outcome types (T-689).

## T-687 Commit note edits through one finisher

- **Status:** blocked-on T-686
- **Value:** High. It is the drift the review found: twelve hand-written tails, each a little different.
- **Outcome:** Each committing note gesture and editor action stages its document and returns an `editor_edit_effect_t` (Undo label, selection, cursor, active lane, created note, audition). One `commit_editor_edit(context, transaction, effect)` commits, applies the effect only on success, re-stamps any gesture still captured after the commit, and builds the result. Ratchets: `core_committed_result` ≤ 1, `manual_gesture_restamp` 0.
- **Read first:** this file's introduction, `docs/architecture.md` ("Adding a chart edit"), `src/core/interaction_gesture_runner.h`, `src/core/note_placement.cpp` (`publish_row_placement`), `include/wormedit/core/command.h` (`document_command_transaction_t`).
- **Files owned:** `src/core/interaction_edit_commit.h` (new), `src/core/interaction_gesture_runner.h`, `src/core/interaction_gesture_lifecycle.cpp`, `src/core/interaction.cpp`, `src/core/note_move.cpp`, `src/core/note_placement.cpp`, `src/core/chart_clipboard.cpp`, `cmake/ceremony_ratchet.txt`, `docs/architecture.md` ("Where to look" row), `tests/core/interaction_edit_commit_tests.cpp` (new) and its `tests/CMakeLists.txt` entry, this card.
- **Steps:**
  1. Stale-plan check: the `core_committed_result` ratchet reports 12. The sites are `commit_bgm_note_move`, paint apply, `commit_stem_hold` and `pending_hold_t::apply` (`interaction_gesture_lifecycle.cpp`); resize and move (`note_move.cpp`); `complete_editor_delete`, lane assignment and lane-key entry (`interaction.cpp`); `publish_row_placement` and place-or-remove (`note_placement.cpp`); cut/delete (`chart_clipboard.cpp`).
  2. Add `editor_edit_effect_t` and `commit_editor_edit` in `interaction_edit_commit.h`. A no-op transaction skips the commit, still applies the effect and reports `document_changed = false` (resize and move rely on this). Test once, at the finisher: commit failure leaves history and state untouched; the effect lands on success; a surviving captured gesture gets the new stamp.
  3. Migrate one file per checkpoint, lowering `core_committed_result` each time. The virtual placement's consumption of the primary hold becomes an effect field, so `publish_row_placement` and its manual `refresh_gesture_stamp` go away. Delete `complete_editor_delete`.
  4. Remove `result.accepted = true` lines that follow `editor_interaction_result_t::from`, which already sets it when it builds a view (`interaction.cpp`, `from`).
  5. Add the finisher to the "Where to look" table in `docs/architecture.md`.
- **Target shape:** `auto transaction = document_command_transaction_t(context.history); transaction.adopt_document(std::move(prepared.document)); return commit_editor_edit(context, transaction, {.label = "Paint notes", .selection = std::move(prepared.painted_notes), .cursor = pending.last_position, .lane = pending.last_lane});`
- **Invariants:** Same Undo labels, one Undo step per edit, same resulting selection, cursor, lane, `created_note`, audition and `document_changed` (false for no-op resize and move), same diagnostics.
- **Reject/failure behavior:** A failed commit leaves history, selection, cursor, active lane and the captured gesture as they were before the call.
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.'` shows `core_committed_result` ≤ 1 and `manual_gesture_restamp: 0`; `ctest --test-dir build -L event-harness --output-on-failure`.
- **Out of scope:** Proposal-path edits (timing lane, SV, tap timing, audio offset), which already commit through `apply_proposal_edit`; `document_command_result_t` commands with no editor-state tail (resource, BMSON channel, measure, metadata editors); scratch-copy dispatch (T-688).

## T-688 Reuse delete and tap placement through staging functions

- **Status:** blocked-on T-687
- **Value:** Medium. It removes the last nested results and a double commit into throwaway histories.
- **Outcome:** Deleting a selection and placing a tap each have a staging function that changes a working document and returns its effect; their action handlers and the five internal callers use it. No core code runs `dispatch_editor_action` on a scratch history (ratchet `scratch_dispatch` 0).
- **Read first:** this file's introduction, `src/core/interaction.cpp` (delete handler, `stage_keyboard_occupant`, `place_keyboard_entry`), `src/core/note_placement.cpp` (place-or-remove, `place_virtual_bms_tap`), `src/core/interaction_gesture_lifecycle.cpp` (`prepare_stem_note_paint`), `src/core/chart_clipboard.cpp`.
- **Files owned:** `src/core/interaction.cpp`, `src/core/note_placement.cpp`, `src/core/interaction_gesture_lifecycle.cpp`, `src/core/chart_clipboard.cpp`, `src/core/interaction_internal.h`, `cmake/ceremony_ratchet.txt`, focused tests in `tests/core/`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "dispatch_editor_action(" src/core`. Scratch callers: delete at `interaction_gesture_lifecycle.cpp` (~474), `chart_clipboard.cpp` (~293), `interaction.cpp` (~1354); place tap at `interaction.cpp` (~1370), `note_placement.cpp` (~434, call split over two lines, so the ratchet counts 4).
  2. Extract the delete handler's body into a staging function over the working document and selection; the handler calls it and then `commit_editor_edit`.
  3. Do the same for tap placement.
  4. Move the five callers onto the staging functions and lower `scratch_dispatch` to 0.
- **Invariants:** Same documents, IDs, Undo labels, created notes and diagnostics. Each caller still makes exactly one real Undo step.
- **Reject/failure behavior:** A staging failure leaves the caller's working document unadopted and the real history unchanged, as today.
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.'` shows `scratch_dispatch: 0`.
- **Out of scope:** Dispatches that forward on the real history (`cancel_pointer_gesture`, the two pointer forwards near `interaction.cpp:1850`, select-scope near `:2196`). They pass the caller's own history and return the result unchanged, so they nest nothing.

## T-689 Rebind the workspace after an edit in one place

- **Status:** done (2026-10-18, Codex lane drake; reviewed and gated by Claude)
- **Done:** `rebind_document_edit` and `document_edit_feedback_t` (`src/sys/document_edit_feedback.{h,cpp}`) own rebind plus feedback mapping; `finish_compose_outcome`, `publish_metadata_edit`, `complete_project_resource_copy` and `link_imported_metadata_audio` pass only their messages, severity policy and sources. Every site's message, severity and source mapping was compared and kept, including Compose's action-only source policy and the resource copy's fixed failure text. Supporting edit: `tests/CMakeLists.txt` (new test, and the new source in the test binaries that link `compose_action_outcome.cpp`). Ratchet `sys_workspace_rebind` 4 → 1. Evidence: `scripts/gate.sh --agents 2` passed, 4,120/4,120 tests, complexity and whitespace pass.
- **Value:** Modest. Three copies today; the risk is new commands adding a fourth with different failure messages.
- **Outcome:** `publish_metadata_edit` (`src/sys/metadata_actions.cpp`), resource-copy relink completion (`src/sys/resource_job_state.cpp`) and audio import (`src/sys/metadata_asset_runtime.cpp`) rebind through the same function that `finish_compose_outcome` uses, passing only their own success and failure messages. Ratchet `sys_workspace_rebind` 1.
- **Read first:** `src/sys/compose_action_outcome.cpp` (`finish_compose_outcome`), `src/sys/metadata_actions.cpp`, `src/sys/resource_job_state.cpp`, `src/sys/metadata_asset_runtime.cpp`, `src/core/workspace.cpp` (`rebind_workspace`).
- **Files owned:** `src/sys/document_edit_feedback.h`/`.cpp` (new), the four sys files above, `cmake/ceremony_ratchet.txt`, `CMakeLists.txt` (source list), a focused test under `tests/sys/`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rnE "= (wormedit::)?rebind_workspace\(" src/sys` (expect 4). If T-686 has not landed, add the `sys_workspace_rebind` row from its step 2 first.
  2. Extract the rebind-and-map step (rebind, require no diagnostics, report the first diagnostic or a caller-given fallback) from `finish_compose_outcome` into `document_edit_feedback`.
  3. Move the three other sites onto it; lower the ratchet to 1.
- **Invariants:** Same messages, severities, notification sources and ordering relative to auditions and other feedback.
- **Reject/failure behavior:** A rebind failure still reports the same message and leaves the document change committed, as today.
- **Verify:** GATE; `ctest --test-dir build -R '^documentation\.'` shows `sys_workspace_rebind: 1`.
- **Out of scope:** Audition admission and feedback ordering in `finish_compose_outcome`, which stay compose-specific; frame-level `publish_*` functions in `src/sys/application.cpp` and scene publication in `src/sys/compose_view.cpp`, which publish UI output, not edits.
