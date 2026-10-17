# Measure lane: bar lines as editable objects

[Back to task cards](../tasks.md).

User request (2026-10-17): add a "measure lane" that treats measure lines as natural notes, with full copy/paste and displacement support. [Open question 19](../decisions/open-questions.md) (answered 2026-10-17): bar-line edits are presentational, so every other object keeps its song time; time propagation is an explicit toggle, off by default. The work is split into four slices that each land on their own.

## T-654 Select bar lines and delete them by merging measures

- **Status:** done (2026-10-17)
- **Outcome:** A bar line is a selection object (`editor_selection_object_kind_t::bar_line`, keyed by the ID of the measure it starts); Delete with bar lines selected merges each with the measure before it, together with any other selected objects, in one Undo step, and no object changes its row.
- **Read first:** `docs/architecture.md`, `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp` (`split_measure_at`, `resize_keeping_rows`), `src/core/interaction.cpp` (`delete_editor_selection`, `existing_ids`), `include/wormedit/core/timing_lane_catalog.h`.
- **Files owned:** `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp`, `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/core/interaction_internal.h`, `src/core/interaction_selection.cpp`, `src/core/interaction_hit_test.cpp`, `include/wormedit/core/timing_lane_catalog.h`, `tests/core/measure_editor_tests.cpp`, `tests/core/interaction_tests.cpp`, this card.
- **Steps:**
  1. Stale-plan check: `grep -rn "bar_line" include/wormedit/core/interaction.h`.
  2. Add a keep-rows merge to the measure editor: the measure before the bar line takes both lengths, the later measure is removed, and lifted objects return to their rows (the inverse of `split_measure_at`).
  3. Add the `bar_line` selection kind; the chart's first measure has no selectable bar line. Handle it in every exhaustive switch over the kind.
  4. Make Delete merge selected bar lines inside its existing transaction, after removing the other selected objects.
- **Invariants:** Every note, BGM, timing, marker and layout row is unchanged by a merge. Measure lengths stay exact rationals. Delete stays one Undo step.
- **Reject/failure behavior:** Merging away the first measure, a bar line that starts a lane-layout segment, a merge past the measure row limit, or one inside resolved BMS conditional branches rejects the whole Delete without changing the document or selection.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'` (merge keeps rows, mixed note+bar-line Delete, Undo, each rejection); `clang-tidy -p build src/core/measure_editor.cpp src/core/interaction.cpp`; `ctest --test-dir build -R '^documentation\.'`.
- **Out of scope:** moving bar lines (T-655), clipboard (T-656), the lane column and its hit testing (T-657).
- **Done:** `remove_bar_lines` in `src/core/measure_editor.cpp` validates every bar line first, then lifts each object's row, merges lengths and re-places. Delete stages it after its other removals (`stage_delete_bar_lines`). Bar lines are registered in the hit-test index so selection validates them; the new `editor_selection_scope_t::bar_lines` flag (off by default) keeps them out of hover, row and area selection until T-657 shows the lane. The 50,000-event hover scale test now counts the 52 bar lines its chart has. Evidence: 4,017/4,017 CTest pass; new functions score at most 7 cognitive complexity and `delete_editor_selection` rose from 17 to 18; `git diff --check` clean.

## T-655 Move and nudge selected bar lines, with an optional time-propagation toggle

- **Status:** done (2026-10-17)
- **Outcome:** Dragging or nudging a selection that holds bar lines moves them with the other objects; by default every other object keeps its song time, and only with the time-propagation toggle on does content after a moved bar line move with it.
- **Read first:** `docs/architecture.md`, `src/core/interaction_gesture_runner.h`, `include/wormedit/core/measure_editor.h` (`move_bar_line_action_t`), the note move gesture in `src/core/interaction_gesture_lifecycle.cpp`, `include/wormedit/core/command.h`.
- **Files owned:** `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp`, `src/core/interaction.cpp`, `src/core/interaction_gesture_lifecycle.cpp`, `include/wormedit/core/preferences.h`, `src/core/preferences.cpp`, `tests/core/measure_editor_tests.cpp`, `tests/core/interaction_tests.cpp`, this card.
- **Steps:**
  1. Generalize the bar-line move to several bar lines by row deltas, keeping rows; a bar line may not cross its neighbor.
  2. Plan bar lines inside the existing move and nudge gestures' proposal, so preview, stale checks and commit come from the runner.
  3. Add the time-propagation policy value (off by default) and its ripple variant of the move.
- **Invariants:** With the toggle off, no non-bar-line object changes song time. One Undo step per gesture.
- **Reject/failure behavior:** Crossing or zero-length measures, the first bar line, or rows past the measure limit reject the whole move.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'` (toggle off and on, mixed selection, nudge, Undo); `clang-tidy -p build <changed .cpp>`.
- **Out of scope:** clipboard (T-656), the lane column (T-657), the Alt-drag gestures (T-398).
- **Done:** Owner direction (2026-10-17): one row-move path, with drag kept distinct but using it. `time_shift` is that executor: `shift_selection_action_t` carries bar lines, `bar_line_time_t` and `include_timing` (timing rides along by default under propagation), and `stage_shift_selection` runs it on a working copy. Geometry is the measure editor's keep-rows `move_bar_lines`; a lane layout change rides with its bar line. Order is geometry first for later moves and objects first for earlier ones, and each object moves at most once. The step move (Transform shift) builds its request with `editor_shift_selection`; the drag stages its row delta for selected bar lines inside its own commit, naming the objects it already moved. Scope change: the toggle is session state (`editor_interaction_state_t::bar_line_time`, never persisted) instead of a preference, so propagation is always an explicit request; `preferences.{h,cpp}` were not touched. Limitation kept from `time_shift`: charts with retained BMS control flow or measure-length lines reject row moves. Evidence: 4,023/4,023 CTest; new functions at most cognitive complexity 10, and the move gesture's `apply` fell from 26 to 7 after extracting `commit_bgm_note_move`.

## T-656 Copy, cut and paste bar lines with the selection

- **Status:** ready
- **Outcome:** Copying a selection that holds bar lines stores them relative to the copy anchor; paste splits measures at the pasted rows and cut merges the originals, each in one Undo step, without moving other objects in time.
- **Read first:** `include/wormedit/core/chart_clipboard.h`, `src/core/chart_clipboard.cpp`, `include/wormedit/core/measure_editor.h`, `docs/architecture.md`.
- **Files owned:** `include/wormedit/core/chart_clipboard.h`, `src/core/chart_clipboard.cpp`, `tests/core/chart_clipboard_tests.cpp`, this card.
- **Steps:**
  1. Carry bar-line offsets in `chart_clipboard_t`.
  2. Paste replays them as keep-rows splits; a bar line landing on an existing one is a no-op.
  3. Cut merges the copied bar lines through T-654's merge.
- **Invariants:** Clipboard text from other editors and older payloads still paste unchanged.
- **Reject/failure behavior:** A paste whose split would exceed limits or split inside retained BMS branches rejects without partial splits.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'` (copy/paste round trip, cut, existing clipboard tests).
- **Out of scope:** BMS-format clipboard interop for bar lines (`bms_clipboard.h`), the lane column (T-657).

## T-657 Show an optional measure lane in Compose

- **Status:** ready
- **Outcome:** A Compose view option shows a measure lane where each bar line is a hit-testable object: click selects, area selection includes it, Draw places one (split), Delete removes it, and the selection gestures from T-655 and T-656 work on it.
- **Read first:** `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/core/interaction_hit_test.cpp`, `docs/frontend-event-harness.md`.
- **Files owned:** `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/core/interaction_hit_test.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Project the lane (hidden by default) from the existing grid-line projection.
  2. Index bar lines in the hit test so click and area selection produce `bar_line` objects.
  3. Route Draw in the lane to the existing split action.
- **Invariants:** The lane draws no bar line twice; the canvas grid stays the single bar-line projection.
- **Reject/failure behavior:** Hidden lane: no hit targets and no change to existing input.
- **Verify:** `ctest --test-dir build -L event-harness`; isolated Xvfb capture via `scripts/smoke-compose-virtual-x.sh`.
- **Out of scope:** skin artwork for the lane; the time-propagation toggle's control placement beyond a plain option.
