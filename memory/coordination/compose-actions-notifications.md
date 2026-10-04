# Compose action notification implementation

Coordinator: GPT-6.1 Sol medium. Executor: GPT-6 Luna medium, with exclusive ownership of the entire `src/sys/compose_actions.cpp` until handoff; Sol reviewed and corrected the final source. Written task: `/tmp/wormedit-batch-implementation/compose-actions-luna.txt`. Isolated branch: `coord/compose-notifications`.

## Scope and dependencies

Sol owns `compose_actions.h`, `compose_area_actions.h/.cpp`, `bookmark_rename_commands.h/.cpp`, `compose_selection_transform.h/.cpp`, `compose_measure_commands.h/.cpp`, `compose_label_search.h/.cpp`, `recipe_form.h/.cpp`, new `tests/sys/compose_action_notification_tests.cpp`, narrow `tests/CMakeLists.txt` wiring, the T-222 supporting scope and this record. Parent confirmed no overlap before supporting edits.

Mixed helper strings now carry actual severity through optional trailing output pointers. Recipe previews and bookmark rename outcomes retain typed severity fields. Existing public strings, booleans, controller behavior and document/history operations remain unchanged. This separates validation, navigation, accepted no-op and core diagnostic outcomes without classifying message prose or observing revisions to infer severity.

Reviewed input dependencies are local cherry-picks `810fa01` and `cc8beb3`, equivalent to root `68d8b2f` and `4ea6383`; they are dependencies, not action handoff commits. The action BMSON staging adapter uses their actual typed producer seam.

Workspace API replaces trailing `std::string& status` with `notification_sink_t notifications`. View API keeps its string return and adds trailing `std::optional<notification_sink_t> notifications = {}`. Root/parent own the final main adapter. The isolated app build used a recorded, validation-only main change from the workspace trailing status argument to `context.notification_sink_ref`; it is restored and excluded from the action commit.

## Reviewed behavior

Action branches publish explicit severity/source across quantization presets, area operations, note menus, audio offsets, bookmark/measure navigation, lane profiles/layouts, view controls, catalog import/resource placement/copy/export, BMSON, markers, transforms, measure/meter editing, label search, recipe forms, semantic layers and BMS branches. Document mutations are success/edit; catalog import and background operations retain their explicit import/export producers. Audio offset and audio-time marker authoring are document edits; playback controls/seeks retain audio producer ownership.

Sound/BGA helpers receive the optional sink and retain their own publication. Bookmark/seek callbacks use `feedback_posted`/`transport_seek_posts_feedback` to avoid duplicates. Runtime start and consumed terminal messages remain runtime-owned. Preparation validation and task submission failures publish explicitly. Empty outcomes stay quiet; NONE stays quiet even without a chart, while NONE plus area-clear retains the original quiet controller clear.

Review corrected diagnostic severity for bookmark navigation, distinguished rejected rename from accepted no-op through producer metadata, retained canceled/staged/navigation hints, and restored original area-clear behavior. Failed rebind after a committed mutation preserves the existing compatibility return while retaining an ordered success event followed by the actual diagnostic/error with document source. Successful rebind publishes success once. A cohesive rebind helper and semantic-control message helper keep new functions within complexity 25; the existing view-group score remains 36.

The original string-literal sets across all seven changed producer `.cpp` files are identical: no added or removed literals. This supplements the actual source-order review and dispatcher assertions; it is not a substitute for branch review.

## Evidence

- Offline Debug app and `wormedit_frame_timing_tests` built with max `-j4`; final log `/tmp/wormedit-batch-implementation/compose-actions-final-build.log` ends with both executable links.
- Actual dispatcher tests: **195 assertions / 21 cases passed** (`compose-actions-dispatch-tests.log`). They cover mutation/one undo, no-op, stale and pending rejection, NONE/area clear, diagnostic severity, navigation, BGA duplicate suppression, callback-owned seek, real filesystem resource start plus consumed completion (two entries, count 1), workspace branch validation/core rejection, measure/transform/search/recipe seams and ordered failed-rebind mutation outcomes including rename.
- Existing helper regressions: **173 assertions / 23 cases passed** (`compose-actions-helper-tests.log`); existing area regressions: **97 assertions / 10 cases passed** (`compose-area-tests.log`). Core target rebuilt after helper API changes.
- Wider frame CTest: **241 / 242 passed**, 65.06 s (`compose-actions-frame-ctest.log`). The sole failure is existing projection test `Compose marker validation precedes arena exhaustion and preserves cap ordering`, which still requires retired `view.status_text` at `tests/sys/frame_timing_tests.cpp:3506`. Root explicitly owns its typed projection-feedback adaptation after action integration. Do not claim the entire wider selection passed.
- Configured clang-tidy ran on every changed producer/test `.cpp`; supporting helpers have no diagnostics. Final action/test correction log has only pre-existing `dispatch_compose_view_action_group` complexity 36 and unrelated `build_speed_graph` header complexity 29. A review-added layer-control score 28 was corrected through a cohesive message helper; no remaining new/increased violation. Logs: `compose-actions-final-tidy.log`, `compose-actions-helpers-tidy.log`, and final `compose-actions-corrections-tidy.log`.
- Direct chart documentation check and `git diff --check` pass. Literal audit: `/tmp/wormedit-batch-implementation/compose-actions-message-audit.log`.

All listed logs are under `/tmp/wormedit-batch-implementation/`. Root must apply the final main glue and rerun integrated verification. The manual Xvfb/T-207 gate remains pending: prior automatic sandbox approval review rejected escalation, and this batch neither retried nor claimed a visual pass. T-222 remains active until final integrated/manual gates are satisfied; no shared resume/index was edited.
