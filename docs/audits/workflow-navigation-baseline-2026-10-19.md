# Workflow navigation baseline — 2026-10-19

This is the before-refactor observation requested by the
[workflow-navigation audit](workflow-navigation-2026-10-18.md). It is a small qualitative check,
not a performance benchmark or merge gate.

## Conditions

Four fresh agents each received one exercise. Every agent used `gpt-6-luna` at medium effort,
read-only repository tools, and the same starting instruction: work in this repository, begin from
source and tests, and do not read the architecture page, the workflow-navigation audit, status
memory or another agent's report. Each agent had to report the correct trace, files inspected in
order, wrong guesses and elapsed wall time when measurable.

The documentation exclusion keeps the source-navigation comparison stable across the later code
slice. It means this run does not measure whether the T-658 index helps a reader who chooses to use
it.

Wall time is not comparable: two agents could not measure it, one estimated 2–3 minutes, one
estimated 3 minutes, and one reported roughly one second of tool execution. The comparison uses
correctness, inspected files and wrong turns instead.

## Results

| Exercise | Correct boundary found | Unique files inspected | Wrong turns or misleading paths |
|---|---|---:|---|
| Keyboard quantize settings to core request and feedback | Yes | 9 | One nonexistent file; broad search surfaced conversion and preview paths unrelated to the registered keyboard command |
| Paste admission, revision, rejection, commit and retained preview | Yes | 7 | Three guessed source/test paths did not exist |
| Bar-line toggle publication of document and cursor/selection changes | Yes | 11 | One guessed UI path did not exist; the route-owned refresh flag made completion ownership easy to misread |
| Note-move domain implementation versus shared stale/cancel handling | Yes | 7 | None; broad search results were discarded without becoming a false trace |

All four traces identified the mutation and failure/completion boundaries. File counts are unique
files whose contents the agent inspected; nonexistent guesses and repeated reads are excluded.

### 1. Keyboard quantize

Prompt: “Find where keyboard quantize settings become a core request and where failure reaches
feedback.”

The settings action in `src/sys/compose_actions.cpp` parses the frontend action through
`quantize_request_from_action` and stores the request in the project quantize editor. The registered
`edit.apply_quantize` command runs through `src/sys/application_command_dispatch.cpp`, reads that
stored request, serializes it into `WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE`, and reaches
`apply_compose_quantize_selection` in `src/sys/compose_area_actions.cpp`. That path attaches the
selection and calls `apply_quantize_command` in `src/core/command.cpp`.

A rejected `quantization_plan_t` returns diagnostics without a transaction commit. The area action
maps the first diagnostic into `compose_action_outcome_t`; `src/sys/compose_action_outcome.cpp`
posts it as edit feedback. Success commits the chart, records the quantize report, rebinds the
workspace and posts success. Tests in `tests/sys/compose_action_notification_tests.cpp` cover
rejection without revision/Undo changes, successful mutation/reporting, and rebind failure after
mutation.

Inspection order:

1. `src/sys/compose_area_actions.cpp`
2. `src/sys/compose_quantize_request.h`
3. `src/sys/application_command_dispatch.cpp`
4. `src/core/command.cpp`
5. `src/sys/compose_actions.cpp`
6. `src/sys/application_commands.cpp`
7. `tests/sys/compose_action_notification_tests.cpp`
8. `src/sys/compose_action_outcome.cpp`
9. `src/sys/compose_action_routes.h`

The agent first guessed nonexistent `src/sys/compose_action_context.cpp`. Its initial broad search
also surfaced format conversion and preview/core quantize tests, which did not lead to the keyboard
command path. The typed request's round trip through a frontend action is the main navigation
detour for the planned slice.

### 2. Paste

Prompt: “Find where paste is admitted, revised, rejected and committed, including the owner of its
retained preview.”

`begin_compose_paste` in `src/sys/compose_view.cpp` admits Wormedit clipboard content or decodable
external BMS editor text and rejects a pending gesture. It builds the preview and stores it in
`application_command_state_t::paste_session_t`, declared in `src/sys/compose_view.h`. That session
owns the clipboard snapshot, anchor, proposal, preview geometry, scene index, ghosts and anchor
error across frames.

Pointer updates reach `revise_compose_paste` through `src/sys/compose_input.cpp`. A missing chart or
stale captured revision clears the session. A changed valid anchor rebuilds it; invalid placement
can retain ghosts and diagnostics while remaining inapplicable. `apply_compose_paste` revises once
more, rejects an unavailable anchor or valueless proposal, and calls the core paste path in
`src/core/chart_clipboard.cpp`. The core rebuilds and validates against the current document before
one transaction mutates it. Success clears the session; an ordinary failed commit preserves it for
revision or retry.

Inspection order:

1. `src/sys/compose_view.h`
2. `src/sys/compose_view.cpp`
3. `src/sys/compose_input.cpp`
4. `src/core/chart_clipboard.cpp`
5. `tests/sys/frame_timing_tests.cpp`
6. `tests/core/chart_clipboard_tests.cpp`
7. `include/wormedit/core/chart_clipboard.h`

The agent guessed nonexistent `src/core/chart_clipboard.h`, `tests/sys/compose_input_tests.cpp` and
`tests/sys/compose_view_tests.cpp` before finding the declaration and regression homes.

### 3. Bar-line toggle

Prompt: “Find where bar-line toggling publishes document and cursor/selection changes.”

The direct `measures.split_at_cursor` command routes through `measure_command` in
`src/sys/application_command_dispatch.cpp` to `apply_cursor_measure_split` in
`src/sys/compose_measure_commands.cpp`. The helper resolves the cursor's absolute row, applies a
split away from row zero or removes an existing noninitial bar line at row zero, then re-finds the
same absolute row. On success it moves the cursor there and clears the retained range, canvas range
and selected canvas measure. Rejection leaves those values unchanged.

Completion is split across two owners: `measure_command` reconstructs an outcome from a message and
severity, while its application-command route sets `refresh_on_accepted`. The dispatcher turns that
flag into `is_workspace_refresh_required`, so the shared finisher rebinds and publishes the changed
document even though the local outcome does not set `mutated`. This is the ambiguity identified by
the audit, not a demonstrated publication defect.

The separate measure-lane placement path in `src/sys/compose_input.cpp` selects the inserted bar
line and returns `document_changed`; its input-result adapter marks the outcome mutated before the
same shared finisher. Application-command and lane-input regressions cover the toggle, same-row
cursor placement, first-line refusal, inserted selection and existing-line no-op.

Inspection order:

1. `src/sys/compose_measure_commands.cpp`
2. `src/sys/compose_measure_actions.cpp`
3. `src/sys/compose_measure_commands.h`
4. `src/sys/application_command_dispatch.cpp`
5. `src/core/measure_editor.cpp`
6. `src/sys/compose_action_outcome.cpp`
7. `tests/sys/compose_measure_commands_tests.cpp`
8. `tests/core/measure_editor_tests.cpp`
9. `src/sys/document_edit_feedback.cpp`
10. `tests/sys/application_commands_tests.cpp`
11. `src/sys/compose_input.cpp`

The agent guessed `src/ui/compose_input.cpp`; the implementation is under `src/sys`.

### 4. Note move

Prompt: “Find the note-move gesture's domain implementation and its shared stale/cancel handling
without confusing the two.”

`src/core/note_move.cpp` owns domain planning and mutation: `plan_move_gesture` builds the plan,
`has_note_move_changes` identifies a real edit, and `apply_note_move_plan` stages note relocation,
linked sound-event synchronization and optional bar-line shifts before the shared commit. BGM
conversion and displacement validate on a scratch document before adoption.

`pending_note_move_gesture_t` in `src/core/interaction_gesture_lifecycle.cpp` owns gesture-specific
begin, revise and apply behavior. Shared capture validation and cancellation live in
`src/core/interaction_gesture_runner.h` and the lifecycle dispatcher. Revision mismatch clears the
capture and returns the shared stale diagnostic. Cancellation clears it without a document command.
Apply revises at the release position and clears capture before domain application, so rejection or
commit failure does not leave a live gesture.

Inspection order:

1. `src/core/interaction.cpp`
2. `src/core/interaction_gesture_lifecycle.cpp`
3. `src/core/interaction_gesture_runner.h`
4. `src/core/interaction_internal.h`
5. `tests/core/interaction_tests.cpp`
6. `tests/core/active_gesture_tests.cpp`
7. `src/core/note_move.cpp`

The agent reported no wrong first guess. Broad stale/cancel search results were recognized as
unrelated and did not become a false trace.

## Before/after comparison

Repeat these exact prompts with fresh `gpt-6-luna` medium agents after the first quantize ownership
slice. Preserve the source-and-tests-only exclusion and reporting fields above. A useful result must
still locate mutation and feedback/completion correctly. Compare unique files inspected and wrong
turns, with special attention to whether quantize still takes the frontend encode/decode detour.
Do not treat elapsed time or a smaller file count alone as success.
