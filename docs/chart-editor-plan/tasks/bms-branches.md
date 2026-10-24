# Task cards: BMS branches and channel maps

[Back to the task-card index](../tasks.md).

Historical Stage 3 branch and channel-map work from
[authoring/export](../stages/authoring-export.md) and the
[BMS control-flow reference](../../references/bms-control-flow.md).

**Scoped cutover (2026-10-24, T-744):** the source-backed editing entry points and
panel described in T-90–T-92 have been retired in favor of typed Control/Chart/Text
ownership, explicit edit targets and the scope editor from T-731–T-743. Their done
statuses remain historical evidence, not claims that raw-statement controls still exist.
T-93 remains individual-note transfer/copy between semantic Charts, separate from scoped
subtree movement; no implicit owner transfer is enabled. T-287 remains opt-out malformed-flow
repair work. Atomic refusal of an unprovable legacy migration does not implement repair.

## T-90 Expose inactive-statement edit, insert, and delete in the branch outline

- **Status:** done (2026-10-03)
- **Outcome:** selecting an inactive raw statement in the branch outline offers Edit text / Insert after / Delete, calling the existing core commands; errors show the typed BMS diagnostic.
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `src/ui/nuklear/project/bms_branch_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp` (choice command wiring), `docs/references/bms-control-flow.md`.
- **Files owned:** `src/ui/nuklear/bms_branch_panel.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `tests/core/bms_branch_editor_tests.cpp`; user-approved sequential-edit/error extensions (2026-10-03): `src/formats/bms.cpp`, `src/sys/compose/views/compose_view.h`.
- **Implementation notes (2026-10-03):** raw rows expose stable statement IDs and retained full text. The bounded editor calls the existing revision-checked core commands, preserves valid selection across document revisions, and disables all three controls for active statements. Typed feedback remains in the branch panel even when navigation replaces the status bar. Replacement updates original-encoding source bytes and all affected spans atomically while preserving line endings; sequential edit/native reload/insert/delete and CP932 rejection regressions cover the backing repair. Fresh isolated mixed-RANDOM fixture snapshots verify Edit→Insert→Delete, exact retained spans, unchanged active chart/resource fields and two existing notes, structural rejection without mutation, and the active-statement hint. No new function exceeds complexity 25; older warnings are unchanged.
- **Verification output (2026-10-03):** application build: `[100%] Built target wormedit`; core prefix: `100% tests passed out of 1158`; post-rebase frontend prefix: `100% tests passed out of 110`; complete post-rebase suite: `100% tests passed out of 1690` (`Total Test time (real) = 73.29 sec`); saved-state comparison: `Saved-state verification passed for 3 snapshot(s).` The required isolated Compose smoke was generated and visually inspected.
- **Steps:**
  1. Add statement selection (by stable branch ID) to the outline view.
  2. Add a bounded single-line text field and three buttons wired through the standard pattern.
  3. Disable all three for active statements with the hint "switch the branch choice first".
- **Invariants:** full-tree retention; active projection unchanged by inactive edits.
- **Reject/failure behavior:** stale, structural, or multi-chart-source edits reject with the core message.
- **Verify:** standard build/test; manual edit on a `#RANDOM` fixture in `tests/fixtures/bms`.
- **Out of scope:** creating new IF/RANDOM blocks (T-91).

## T-91 Create and remove branch blocks

- **Status:** done
- **Implementation/verification (2026-10-03):** source-verified, revision-checked create/remove commands serialize the current document before splicing and reparsing. Creation selects leaf 1 and leaves the other leaves empty; removal requires an exact empty RANDOM/IF block. Retained statement IDs and relative order survive while physical source spans/orders refresh. Normalized generated statements receive distinct IDs; existing branch selections, note/BGM branch links, lane/input identities, and resource/asset references are reconciled before one history commit. Parser byte/line limits bound leaf construction. Unsupported editor graphs, custom layouts, multiple charts, and selected statements rewritten by serialization reject atomically instead of discarding state. Eight new regressions cover writer output, undo/redo, unsaved edits, normalized root-note identity, catalog/custom-layout rejection, overlap and nonempty removal. Application build, configured clang-tidy (no new violations; existing test warnings remain), all 1,135 core tests, and the complete 1,667-test suite pass (`/tmp/wormedit-lanes-branches-core.log`, `/tmp/wormedit-lanes-branches-all.log`).
- **Outcome:** core commands wrap a selected range of root statements into a new `#RANDOM n`/`#IF k`…`#ENDIF`/`#ENDRANDOM` block, and remove an empty block, each atomically.
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `include/wormedit/core/document.h` (BMS control tree types), `docs/references/bms-control-flow.md`, `tests/core/bms_branch_editor_tests.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_branch_editor_tests.cpp`.
- **Steps:**
  1. `apply_bms_branch_block_create_command` with `n` leaves; the selected statements go into leaf 1, other leaves start empty.
  2. `apply_bms_branch_block_remove_command` for a block whose leaves are all empty.
  3. Tests: create/undo/redo; writer round-trip emits the new block; removing a nonempty block rejects.
- **Invariants:** source order and provenance of moved statements preserved.
- **Reject/failure behavior:** partial overlap with an existing block rejects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** SWITCH blocks, UI (T-92).

## T-92 Add block create/remove and leaf reorder to the outline

- **Status:** done
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Outcome:** outline buttons create a block from the current selection, remove an empty block, and move a leaf up/down.
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `src/ui/nuklear/project/bms_branch_panel.cpp`.
- **Files owned:** `src/ui/nuklear/project/bms_branch_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`, `src/core/bms_branch_editor.cpp` (leaf reorder command), `tests/core/bms_branch_editor_tests.cpp`. User-approved supporting scope (2026-10-03): `include/wormedit/core/bms_branch_editor.h`.
- **Implementation audit (2026-10-03):** create/remove commands now exist, but no public leaf-reorder command is declared. Its typed request and callable declaration require `include/wormedit/core/bms_branch_editor.h`, which this card omits. Header ownership was approved by the user on 2026-10-03; no implementation was started.
- **Result (2026-10-09):** `apply_bms_branch_leaf_move_command` (header, `src/core/bms_branch_editor.cpp`) swaps the bodies of two adjacent plain `#IF`/`#ELSEIF` leaves of one RANDOM block as one revision-checked undoable command, so the `#IF`/`#ELSEIF` values stay in order and the block's active choice follows the moved content; `#ELSE` leaves, nested control flow, edges and stale identities reject atomically, and the one-chart restrictions are unchanged. The outline has four buttons (New block, Remove block, Leaf up, Leaf down) wired through `WORM_UI_COMPOSE_ACTION_CREATE/REMOVE_BMS_BRANCH_BLOCK` and `MOVE_BMS_BRANCH_LEAF` to `dispatch_bms_branch_block_action`. Tests: 3 core cases, 1 event-harness case (all four buttons), 1 sys dispatch case in `tests/sys/compose_action_notification_tests.cpp` (coordinator-approved supporting path, create then undo and a rejected remove). Real-window Xvfb captures on `control-flow-mixed.bms`: `/home/zardoru/devel/wormedit-scratch-builds/t92/` (`leaf-selected.png`, `block-selected.png`). Full suite 3047/3047.
- **Steps:**
  0. Scope extension approved by the user (2026-10-03).
  1. Core leaf-reorder command with test.
  2. Wire three buttons through the standard pattern.
- **Invariants:** reordering leaves renumbers `#IF` values consistently.
- **Reject/failure behavior:** reordering a leaf that is the active selection keeps the same leaf selected.
- **Verify:** standard build/test.
- **Out of scope:** moving chart objects between leaves (T-93).

## T-93 Move or copy selected notes into another branch leaf

- **Status:** done (2026-10-24)
- **Prerequisites:** T-744 public scoped ownership cutover.
- **Decision (user, 2026-10-03):** support fresh authored notes and both hold endpoints; do not limit the command to imported taps. Notes from several semantic Charts at once reject.
- **Scoped reconciliation (coordinator, 2026-10-24):** T-744 retired the serialized-statement authority and rematerialization APIs named by the original card. Preserve its approved product outcome using recorded typed owners, immutable program construction and the existing transaction/publication runner. No serialized-source reconstruction or writable flat mirror is needed. Destination is an explicit semantic Chart within the same difficulty; source selection remains explicit and independent of playback choices. Authored WAV references retain their values and resolve in destination program order, as other scoped edits do.
- **Outcome:** selected notes belonging to one Chart can be moved or copied into another Chart as one command, including complete imported and fresh authored holds.
- **Read first:** `docs/architecture.md`, `include/wormedit/core/bms_program_edit.h`, `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `src/core/bms_program_edit.cpp`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `src/core/bms_program_edit.cpp`, `tests/core/bms_note_transfer_tests.cpp`, `tests/CMakeLists.txt`, `docs/chart-editor-plan/tasks/bms-branches.md`, `docs/architecture.md`, `memory/chart-editor-status/resume.md`.
- **Supporting ownership:** the dedicated test translation unit and its build entry isolate this command's domain coverage; architecture and status record the explicit multi-owner operation. Existing generic single-owner content edits remain unchanged.
- **Pairing-safety ownership extension (coordinator):** `src/core/bms_program_edit.cpp` keeps surviving, unchanged imported endpoint pairs intact across each bounded publication outcome. The new transfer regression demonstrated that interleaved endpoints could otherwise silently re-pair without a lane-overlap diagnostic. This is a shared publication safeguard, not a second evaluator in the transfer command.
- **Steps:**
  1. Resolve selected-note ownership through the source authoring projection and recorded typed contributions, including fresh notes and both imported hold endpoints; reject ambiguous, partial or multiple-Chart ownership atomically.
  2. Move complete authored contributions or copy them with history-wide fresh identities into the explicit destination Chart through immutable graph construction and one shared transaction. Keep unrelated items, exact musical positions, attributes and playback choices intact.
  3. Test root→leaf move, leaf→leaf copy, fresh/imported holds, inactive targets, hidden collisions, native reload/export, identity allocation and Undo/Redo. Verify with GATE and explicit affected header consumers.
- **Invariants:** unrelated notes and channel-line contributions retain positions and identity. Moves retain selected item IDs; copies allocate fresh item IDs above current/Undo/Redo identities. No implicit playback switch or resource-declaration edit occurs.
- **Reject/failure behavior:** mixed/unknown owners, incomplete or cross-owner holds, invalid targets and unprovable/colliding outcomes publish nothing. Shared runners own stale/no-op/transaction checks.
- **Verify:** GATE; focused `[bms-note-transfer]`; explicit header-consumer complexity checks.
- **Bounded test review:** reviewed all ten new `[bms-note-transfer]` bodies against `c5c967e`: root/inactive destination ownership and neighboring entries; imported tap/hold copies with Redo identity ceilings; fresh annotated native holds; hidden collisions; invalid/mixed selections; complete imported hold moves; cross-owner refusal; explicit inactive hold witnesses; interleaved endpoint rejection; and valid transfers beside existing destination holds. Each covers distinct transfer data or publication behavior. No existing bodies were removed or reviewed for suite-wide nonredundancy. The cross-owner refusal uses a deliberate prepared fixture with a chosen witness, following the existing authoring tests; public import rejects its unpaired default evaluation.
- **Verification:** GCC GATE passes **4,866/4,866**, including discovery, whitespace and complexity for **three changed TUs**. **Four explicit header consumers** pass separately; no complexity allowances increased. Focused transfer coverage passes **320 assertions / 10 cases**. Logs: `/tmp/t93-gate-2.log` (details `/tmp/wormedit-gate.f8wFWg`), `/tmp/t93-focused-final.log`, `/tmp/t93-headers-1.log`. The interleaving regression failed for both move and copy before the shared pairing guard and passes with it. No frontend workflow was added; no window-based acceptance is claimed.
- **Handoff:** T-287 malformed-flow repair remains ready and separate; no successor is active.
- **Out of scope:** scope picker or broader UI workflow; the original optional inspector button is omitted because selecting an arbitrary destination Chart requires a dedicated workflow. No cross-difficulty transfers, automatic resource declaration copying or hold repair.

## T-94 Decide multi-chart branch source identity and malformed-flow policy

- **Status:** done (user decision 2026-09-30: each chart keeps its own retained BMS source tree and branch edits target the selected chart; malformed control flow is preserved as-is by default, and the editor offers repair proposals the user can review and opt out of. Follow-up: T-287.)
- **Outcome:** decisions recorded.
- **Read first:** `docs/references/bms-control-flow.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** recorded; follow-up added.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-95 Add a custom BMS channel-map edit command

- **Status:** done
- **Implementation/verification (2026-10-03):** revision-bound sealed previews validate the final channel map, so simultaneous swaps work, case-insensitive duplicates name the conflicting lanes, and affected-note counts stay within the selected layout segment. Apply preserves note lane IDs and commits one undoable command. Used channels cannot be removed; branch-local notes retain their actual source channel identity. Four new regressions cover swaps, syntax/category ranges, conflict names, stale or altered previews, export/reimport, and branch-source protection. Application build, configured clang-tidy (no new violations), all 1,135 core tests, and the complete 1,667-test suite pass (`/tmp/wormedit-lanes-branches-core.log`, `/tmp/wormedit-lanes-branches-all.log`).
- **Outcome:** a core command changes one lane's `bms_channel_map_t` (visible/invisible/LN/mine channel strings), validates uniqueness and syntax, and reports how many notes would change channel on export.
- **Read first:** `include/wormedit/core/document.h` (`bms_channel_map_t`, `lane_definition_t`), `include/wormedit/core/lane_profiles.h`, `docs/references/bms-command-memo.md`.
- **Files owned:** new `include/wormedit/core/channel_map_editor.h`, new `src/core/channel_map_editor.cpp`, new `tests/core/channel_map_editor_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Preview function returns the affected-note count and conflicts without mutating.
  2. Apply command commits the whole map change atomically.
  3. Tests: swapping two lanes' visible channels; duplicate channel rejects; invalid base-36 spelling rejects.
- **Invariants:** note lane IDs never change; only export channels do.
- **Reject/failure behavior:** conflicts reject with the conflicting lane names.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (T-96).

## T-96 Edit channel maps in Metadata

- **Status:** done
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Outcome:** Metadata's chart section lists lanes with their four channel strings, lets the user edit them, shows the preview count, and applies once.
- **Read first:** `include/wormedit/core/channel_map_editor.h`, `src/ui/nuklear/metadata/metadata_panel.cpp`, `src/sys/metadata/metadata_actions.cpp`.
- **Files owned:** `src/sys/metadata/metadata_actions.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata/metadata_panel.cpp` (or the section unit from T-42). User-approved supporting scope (2026-10-03): `src/sys/metadata/metadata_actions.h`. Coordinator-approved extensions (2026-10-08): `src/sys/metadata/metadata_drop_state.h` and `src/sys/metadata/metadata_drop_state.cpp` (the sealed `channel_map_preview` field, discarded by `discard_if_stale` when the revision or chart changes; this is the holder that carries the proposal from dispatch to projection and Apply); `src/ui/frontend.h`, `src/ui/nuklear/metadata/metadata_state.h`, `src/ui/nuklear/metadata/metadata_difficulty_section.cpp`, `tests/sys/metadata_notification_tests.cpp` (application tests); `src/sys/workspace_projection.h` (one frame-span member `metadata.channel_map_lane` for the projected lane array, needed because the frame storage lives there). `src/sys/app/main.cpp` and `src/sys/application_frame_projection.cpp` did not need changes because `metadata_drop` was already passed through.
- **Implementation audit (2026-10-03):** no channel-map form exists. Owned projection/section files can provide a layout-segment selector and editable lane table, but the sealed preview must persist between dispatch and projection so conflicts disable Apply and confirmation uses exactly the reviewed proposal. The shared declaration belongs in unowned `src/sys/metadata/metadata_actions.h`; approval was granted on 2026-10-03.
- **Result (2026-10-08):** Preview stores the sealed proposal in the drop state; Apply applies exactly that proposal (a stale one rejects in core). Lane table with four channel fields per lane, Preview/Apply, conflicts disable Apply; projection uses the first layout segment. Tests: 3 new application tests in `tests/sys/metadata_notification_tests.cpp`; full suite 2812/2812 passed; `^core\.` 1658/1658; `metadata|channel_map` 55/55; event-harness 249/249; `^documentation\.` 1/1; clang-tidy cognitive complexity reports nothing for changed files. Smoke PNG of `metadata` not run.
- **Steps:**
  0. Scope extension approved by the user (2026-10-03).
  1. Project lanes.
  2. Editable table plus Preview/Apply through the standard pattern.
- **Invariants:** shown only for BMS-family charts.
- **Reject/failure behavior:** conflicts disable Apply.
- **Verify:** standard build/test; smoke PNG of `metadata`.
- **Out of scope:** profile switching (T-126).

## T-97 Stop importing BMS `00` slots as BGM events

- **Status:** done (headless tests; the real application was not run)
- **Implementation notes (2026-10-01):** `add_bgm_event` skips `00` slots (slot indices stay in `structural_slot`). The writer remembers each source line's slot count from the event's retained line text (`channel_line_t::minimum_slots`), so an edited export keeps each line's `00` padding instead of collapsing to the coarsest exact grid; unedited sources still export byte-identically. Tests in `bms_tests.cpp` and `bms_writer_tests.cpp` were updated to the non-`00` event lists.
- **Outcome:** importing `#00101:01000200` creates two BGM events (slots 0 and 2), not four; unedited export stays byte-identical.
- **Read first:** `docs/references/bms-command-memo.md` (Wormedit consequences), `src/formats/bms.cpp` (`add_bgm_event`, `materialize`), `src/formats/bms_writer.cpp` (BGM emission near `structural_slot`), `tests/formats/bms_writer_tests.cpp`.
- **Files owned:** `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/bms_writer_tests.cpp`, `tests/fixtures/README.md` (the `repeated-bgm-ratio-stop-collision.bms` row), `include/wormedit/core/document.h` (comment on `bgm_event_t::sound` only).
- **Steps:**
  1. Write the failing test first: the fixture `bms/repeated-bgm-ratio-stop-collision.bms` yields only its non-`00` BGM events, each keeping its original `structural_slot`.
  2. In `add_bgm_event`'s caller, skip slots whose object ID is `00`.
  3. Make the writer rebuild `00` padding from the slot indices of the events it has, instead of expecting one event per slot. Keep the unchanged-source path byte-identical.
  4. Update the tests that index `bgm_events[...]` by slot (`bms_writer_tests.cpp` around the `structural_slot == 1` checks) to the new event list.
  5. Update the fixture README row; do not change the `.bms` file.
- **Invariants:** row positions exact; source provenance keeps line and slot; unedited files re-export byte-identically.
- **Reject/failure behavior:** a BGM event whose slot index does not fit the line's subdivision on export falls back to the existing normalized-subdivision path with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms|round.?trip|Stage 0'`.
- **Out of scope:** column assignment (T-98); older native projects (T-99).

## T-98 Assign BMS BGM lines to per-measure columns

- **Status:** done (headless tests; opening the fixture in the real application to see five BGM columns is still to do)
- **Implementation notes (2026-10-01):** importer keeps a per-measure BGM line counter (`bms_channel_materializer_t::next_bgm_column`), columns are named `BGM 1`, `BGM 2`, … and shared across measures; more than 128 lines in a measure imports with a warning. The writer groups by (measure, column) in column order. The branch BGM export check no longer compares a lane to one source line because lanes are shared columns; the retained line is identified by its claimed slot. New fixture `bms/bgm-columns-per-measure.bms` (5 columns, 602 events, 200 measures) with import, byte-identical, normalized-export and round-trip tests in `bms_writer_tests.cpp`.
- **Outcome:** a chart whose busiest measure has 3 `#mmm01` lines shows exactly 3 BGM columns, however many measures it has; in each measure the k-th line (active source order) lands in column k.
- **Read first:** `docs/references/bms-command-memo.md` (per-measure columns), `src/formats/bms.cpp` (`materialize`, the `channel == "01"` block that pushes a new `bgm_source_lanes` entry per line), `src/formats/bms_writer.cpp` (grouping by `(measure, source_lane)`), `include/wormedit/core/document.h` (`bgm_source_lane_t`).
- **Files owned:** `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/bms_writer_tests.cpp`, new fixture `tests/fixtures/bms/bgm-columns-per-measure.bms`, `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`.
- **Steps:**
  1. Fixture: 200 measures, each with three `#mmm01` lines; one measure with five. Failing test: `bgm_source_lanes.size() == 5`, not 601.
  2. Import: keep a per-measure line counter (reset at each measure, like iBMSC's `mColumn(xMeasure)`); column k reuses the chart-wide lane k, creating it only when k exceeds the current count. Lines inside inactive branch leaves occupy no column.
  3. Rename new lanes `BGM 1`, `BGM 2`, …; the source line identity stays in each event's provenance, not in the lane.
  4. Writer: group by `(measure, column)`, emit columns in index order, and skip columns with no events in that measure.
  5. Round-trip test: import → export → import gives the same columns and events; the unedited export is byte-identical.
- **Invariants:** event IDs, rows, sounds unchanged; Compose BGM move/placement keep using `bgm_source_lanes` as columns (no frontend change needed).
- **Reject/failure behavior:** more than 128 lines in one measure (BMSE's limit) imports but reports a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms|round.?trip|Stage 0|^frontend-state\.'`; open the new fixture in the app and check the BGM pane shows 5 columns.
- **Out of scope:** BMSON sound-channel lanes (they keep one lane per channel); older native projects (T-99).

## T-99 Normalize BGM columns in older native projects

- **Status:** done (headless tests)
- **Implementation notes (2026-10-01):** instead of a schema bump (which would touch recovery/autosave files and every document literal), `normalize_legacy_bms_bgm` in `native_project_migrations.cpp` repairs old documents when they load: it drops sound-less events whose provenance names `01 slot N object 00`, then maps the k-th legacy `BGM source #…` lane of each measure onto column k, renames the kept lanes `BGM k`, and removes the other legacy lanes. It is content-detected and idempotent, and runs before validation. Tests in `native_project_tests.cpp`.
- **Outcome:** a native project saved before T-97/T-98 loads with no sound-less BMS `00` events and with per-measure BGM columns, through a schema migration.
- **Read first:** `src/formats/native_project_migrations.cpp` (from T-122), `src/formats/native_project.cpp` (`bgm_source_lanes`, `structural_slot`), `tests/formats/native_project_tests.cpp`.
- **Files owned:** `src/formats/native_project_migrations.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:**
  1. Migration: drop BGM events with no sound whose provenance names object `00`; then rebuild columns with the T-98 rule using each event's provenance source order.
  2. Test with a hand-written v1 JSON document in the test file.
- **Invariants:** BMSON-linked events (`sound_channel_event` set) and user-created events are untouched.
- **Reject/failure behavior:** events without BMS provenance keep their lane; migration never deletes a sound-bound event.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'native_project'`.
- **Out of scope:** changing the native schema beyond the version bump.

## T-311 Set up BGM columns from the Compose UI

- **Status:** done
- **Implementation notes (2026-10-02):** Core and toolbar are implemented, including Enter/focus-loss commits, atomic occupied-column rejection, undo/redo and explicit text ownership. In isolated Xvfb, the converted chart `/tmp/wormedit-t311/converted/fixture-three-bgm-lines-per-measure-1.wormedit` showed count 5→6 with +, back to 5 with −, accepted typed count 8, rejected occupied shrink to 2 while resetting the field to 8, and undo back to 5. Typing did not insert a lane note. The osu!mania toolbar was separately verified growing 5→6; the osu! writer already emits BGM events as `Sample` events. A core regression now covers count changes and undo on a BGM-only chart while preserving its empty playable layouts/bindings and existing event; the full 1,568/1,568-test checkpoint passed (`/tmp/wormedit-final-all-tests.log`). These GUI checks used the converted project; raw BMS file-picker import was not verified. This earlier missing-pane gate is resolved by the 2026-10-03 projection cut below.
- **Verification update (2026-10-03):** BGM-only charts now use a neutral read-only scene projection without creating a retained playable cursor or synthetic lane layouts. In isolated Xvfb, the native converted fixture visibly changed its BGM pane 5→6→5, accepted typed 8, rejected occupied shrink to 2, and undo restored 5. Saving retained all 602 BGM events and all measure contents, five source lanes, and zero playable layouts; only format metadata and revision changed. The raw `tests/fixtures/bms/bgm-columns-per-measure.bms` was also imported through the actual SDL/Xdnd file-drop path, and its visible pane changed 5→6→5 (`/tmp/wormedit-t311/raw-bms-plus.png`, `raw-bms-minus.png`). The native file picker remains unverified; raw import itself is verified. Required core/frontend-state/documentation tests passed 1,171/1,171 (`/tmp/wormedit-t311-final-tests2.log`, 28.68 s); the cursor-write guard caught and verified the correction to use the core cursor-pose API. Configured clang-tidy found only existing complexity warnings outside the new helper.
- **Outcome:** on a BMS or osu!mania chart, the BGM pane header shows "BGM columns: N" with − and + buttons and a number field; raising N appends empty columns named `BGM k`; lowering N removes trailing columns only while they are empty; each change is one undo step.
- **Request (user, 2026-10-01):** a "set up BGM columns" feature in the chart UI; "it's pretty important."
- **Current behavior (2026-10-01):** new BMS charts start with 15 columns (`make_bms_initial_chart`, `src/sys/app/application_defaults.cpp`); BMS import builds columns per measure (T-98); placing onto an occupied cell appends a column (`src/core/resource_assignment.cpp`). There is no way to add, remove, or count columns in the UI.
- **Stale-plan check:** grep `set_bgm_column_count` in `src/core/`.
- **Read first:** `src/core/resource_assignment.cpp` (BGM lane append, ID and `source_order` rules), `include/wormedit/core/document.h` (`bgm_source_lane_t`), `src/ui/nuklear/compose/compose_toolbar.cpp` (BGM column scroll controls), `src/sys/compose/views/compose_view.cpp` (BGM lane projection).
- **Files owned:** new `include/wormedit/core/bgm_columns.h`, new `src/core/bgm_columns.cpp`, new `tests/core/bgm_columns_tests.cpp`, `src/ui/frontend.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/nuklear/compose/compose_toolbar.cpp`, user-approved text-ownership scope extension (2026-10-02): `src/ui/nuklear/compose_state.h/.cpp`, `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/ui/nuklear/app/frontend.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`; `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Core `apply_bgm_column_count(history, expected_revision, chart, count)`: append columns with fresh IDs, the next `source_order`, and names `BGM k`; remove trailing columns that have no BGM events; reject atomically if any column to remove has events, naming the first such column and its event count. Allowed range 1–128 (BMSE's limit, as in T-98).
  2. Tests: grow, shrink empty, refuse a shrink over events, the range limits, undo/redo, unchanged event IDs and rows.
  3. Wire it through the standard pattern: the view gets `bgm_column_count` and `bgm_column_count_editable` (true for BMS and osu!mania charts), the action carries the new count.
  4. Widget beside the BGM column scroll buttons: label, −, number field (commits on Enter or focus loss), +. Enabled for BMS and osu!mania charts; BMSON sound-channel lanes remain untouched.
- **Invariants:** export unchanged for the same events (the writer already skips empty columns); BMSON sound-channel lanes are untouched.
- **Reject/failure behavior:** a refused shrink shows the core message and leaves the count and the field at the current value.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|bgm_columns|^frontend-state\.|^documentation\.'`; `clang-tidy -p build` on each changed `.cpp`; isolated Xvfb capture of a BMS chart after + and − with the BGM pane visible.
- **Out of scope:** renaming or reordering columns, deleting a column that has events (moving them elsewhere), BMSON lanes.

## T-287 Per-chart branch sources and opt-out repair proposals

- **Status:** ready
- **Outcome:** inactive-branch edits work in multi-chart projects by targeting the selected chart's own retained BMS source; malformed control flow (unterminated `#IF`/`#RANDOM`/`#SWITCH`, orphan `#ENDIF`, `#ELSE` without `#IF`) is preserved by default and listed in the branch outline as repair proposals (for example "close #IF at end of block") that the user can apply individually or dismiss; dismissed proposals stay dismissed for that file (user decision 2026-09-30).
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp` (the one-chart restriction), `include/wormedit/formats/bms.h` (control tree, diagnostics), `src/ui/nuklear/project/bms_branch_panel.cpp`, `tests/fixtures/bms/negative-orphan-control.bms`.
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_branch_editor_tests.cpp`, `include/wormedit/core/document.h` (per-chart source link if missing), `src/formats/native_project.cpp`, `src/ui/nuklear/project/bms_branch_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Steps:** 1. Link each chart to its own retained source tree; remove the one-chart restriction. 2. Repair proposal generator from parser diagnostics (pure, no mutation). 3. Apply-proposal command (atomic, undoable) and dismiss state stored in the project. 4. Tests on the negative fixtures.
- **Invariants:** without user action, export writes malformed blocks back unchanged.
- **Reject/failure behavior:** a proposal made stale by an edit is withdrawn.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bms'`.
- **Out of scope:** automatic repair.
