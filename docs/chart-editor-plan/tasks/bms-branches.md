# Task cards: BMS branches and channel maps

[Back to the task-card index](../tasks.md).

Stage 3 branch and channel-map leftovers from
[authoring/export](../stages/authoring-export.md) and the
[BMS control-flow reference](../../references/bms-control-flow.md). Core already
has statement edit/insert/delete commands and `apply_bms_branch_choice_command`
(wired in `src/sys/compose_actions.cpp`); the outline panel is read-only apart
from choice selection. Apply the stale-plan check (rule 9) first.

## T-90 Expose inactive-statement edit, insert, and delete in the branch outline

- **Status:** ready
- **Outcome:** selecting an inactive raw statement in the branch outline offers Edit text / Insert after / Delete, calling the existing core commands; errors show the typed BMS diagnostic.
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `src/ui/nuklear/bms_branch_panel.cpp`, `src/sys/compose_actions.cpp` (choice command wiring), `docs/references/bms-control-flow.md`.
- **Files owned:** `src/ui/nuklear/bms_branch_panel.cpp/.h`, `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `tests/core/bms_branch_editor_tests.cpp`.
- **Steps:**
  1. Add statement selection (by stable branch ID) to the outline view.
  2. Add a bounded single-line text field and three buttons wired through the standard pattern.
  3. Disable all three for active statements with the hint "switch the branch choice first".
- **Invariants:** full-tree retention; active projection unchanged by inactive edits.
- **Reject/failure behavior:** stale, structural, or multi-chart-source edits reject with the core message.
- **Verify:** standard build/test; manual edit on a `#RANDOM` fixture in `tests/fixtures/bms`.
- **Out of scope:** creating new IF/RANDOM blocks (T-91).

## T-91 Create and remove branch blocks

- **Status:** ready
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

- **Status:** blocked-on T-91
- **Outcome:** outline buttons create a block from the current selection, remove an empty block, and move a leaf up/down.
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `src/ui/nuklear/bms_branch_panel.cpp`.
- **Files owned:** `src/ui/nuklear/bms_branch_panel.cpp`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `src/core/bms_branch_editor.cpp` (leaf reorder command), `tests/core/bms_branch_editor_tests.cpp`.
- **Steps:** 1. Core leaf-reorder command with test. 2. Wire three buttons through the standard pattern.
- **Invariants:** reordering leaves renumbers `#IF` values consistently.
- **Reject/failure behavior:** reordering a leaf that is the active selection keeps the same leaf selected.
- **Verify:** standard build/test.
- **Out of scope:** moving chart objects between leaves (T-93).

## T-93 Move or copy selected notes into another branch leaf

- **Status:** blocked-on T-91
- **Outcome:** selected notes that belong to one leaf (or root) can be moved or copied into another leaf as one command.
- **Read first:** `include/wormedit/core/bms_branch_editor.h`, `include/wormedit/formats/bms_branch_selection.h` (`rematerialize_bms_branch_selection`), `docs/chart-editor-plan/editing/metadata-interop.md` (branch paragraphs).
- **Files owned:** `include/wormedit/core/bms_branch_editor.h`, `src/core/bms_branch_editor.cpp`, `tests/core/bms_branch_editor_tests.cpp`.
- **Steps:**
  1. STOP if a selected note has no single owning statement; report the case.
  2. Rewrite the owning channel statements in source and target leaves; rematerialize.
  3. Tests: move root→leaf, leaf→leaf copy, undo.
- **Invariants:** other notes on the same channel line keep their positions.
- **Reject/failure behavior:** notes from several leaves at once reject.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI beyond one inspector button (add it in this card only if trivial).

## T-94 Decide multi-chart branch source identity and malformed-flow policy

- **Status:** blocked-on-user
- **Outcome:** recorded answers for (a) how a multi-chart project identifies which BMS source a branch edit targets and (b) which malformed control-flow constructs each target profile repairs, preserves, or rejects (see `memory/chart-editor-status/open-decisions.md`).
- **Read first:** `memory/chart-editor-status/open-decisions.md`, `docs/references/bms-control-flow.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask both questions. 2. Record answers and add follow-up cards.
- **Invariants:** none.
- **Reject/failure behavior:** until answered, inactive edits stay single-chart only.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-95 Add a custom BMS channel-map edit command

- **Status:** ready
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

- **Status:** blocked-on T-95
- **Outcome:** Metadata's chart section lists lanes with their four channel strings, lets the user edit them, shows the preview count, and applies once.
- **Read first:** `include/wormedit/core/channel_map_editor.h`, `src/ui/nuklear/metadata_panel.cpp`, `src/sys/metadata_actions.cpp`.
- **Files owned:** `src/sys/metadata_actions.cpp`, `src/sys/metadata_view_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/metadata_panel.cpp` (or the section unit from T-42).
- **Steps:** 1. Project lanes. 2. Editable table plus Preview/Apply through the standard pattern.
- **Invariants:** shown only for BMS-family charts.
- **Reject/failure behavior:** conflicts disable Apply.
- **Verify:** standard build/test; smoke PNG of `metadata`.
- **Out of scope:** profile switching (T-126).

## T-97 Stop importing BMS `00` slots as BGM events

- **Status:** ready
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

- **Status:** blocked-on T-97
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

- **Status:** blocked-on T-98, T-122
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
