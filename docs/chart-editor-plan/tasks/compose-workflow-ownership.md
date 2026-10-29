# Compose workflow ownership

[Back to task cards](../tasks.md).

## T-780 Give clipboard and quantize workflows explicit application owners

- **Status:** ready
- **Outcome:** Paste lifecycle and quantize settings/preset operations have workflow owners outside scene projection; their implementation dependencies exclude unrelated audio/recipe/frame caches.
- **Read first:** `docs/architecture.md`, `docs/chart-editor-plan/decisions/code-style.md`, `src/sys/compose/views/compose_view.h`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/actions/compose_quantize.cpp`.
- **Files owned:** `CMakeLists.txt` (only app clipboard source registration), `tests/CMakeLists.txt` (only frame-timing/event-harness clipboard source registrations), `src/sys/app/application_command_state.h`, `src/sys/compose/compose_clipboard_workflow.h`, `src/sys/compose/compose_clipboard_workflow.cpp`, `src/sys/compose/actions/compose_quantize_state.h`, `src/sys/compose/views/compose_view.h`, `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/views/compose_view_internal.h`, `src/sys/compose/actions/compose_quantize.h`, `src/sys/compose/actions/compose_quantize.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/actions/compose_action_routes.h`, `src/sys/compose/input/compose_input.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `tests/sys/compose_notification_tests.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `docs/chart-editor-plan/tasks/compose-workflow-ownership.md`, `docs/audits/compose-workflow-ownership-2026-10-29.md`.
- **Scope extension (coordinator-approved, 2026-10-29):** Apply and commit lane A's `/tmp/lane-c-clipboard-registration.patch`: one `src/sys/compose/compose_clipboard_workflow.cpp` entry beside `compose_view.cpp` in the app and corresponding `../src/` entries in frame-timing and event-harness. This makes the extracted compiled workflow independently linkable. Lane A confirmed these three hunks do not overlap its target refactor; all other CMake structure remains A-owned.
- **Steps:**
  1. Confirm paste begin/revise/apply remain projection-owned; record fresh luna-HIGH navigation baseline and dependency reach.
  2. Extract application state from scene declarations with explicit clipboard and quantize state owners; move paste lifecycle to a compiled workflow module while keeping preview geometry construction at projection owner.
  3. Move quantize settings/preset operations and routes into the existing quantize module; preserve exact outcome policy.
  4. Review lifetime/reset semantics and failure/completion ownership; run fixed identical navigation exercises and dependency measurements.
  5. Run verification, document limitations and create a draft PR if viable.
- **Invariants:** Same application/project/chart/gesture lifetimes, clipboard snapshots, stale clearing, ordinary failure retention, preview geometry, action payloads, document transactions, feedback and Undo labels.
- **Reject/failure behavior:** Invalid external clipboard, pending gesture, stale revision and quantize rejection preserve existing document/history behavior; shared runner remains the only publisher.
- **Verify:** `scripts/gate.sh --jobs 2`; `ctest --test-dir build -R '^documentation\\.' --output-on-failure`; `ctest --test-dir build -L event-harness -j 2 --output-on-failure`; `scripts/check-complexity.py --build-dir build src/sys/compose/compose_clipboard_workflow.cpp src/sys/compose/actions/compose_quantize.cpp src/sys/compose/views/compose_view.cpp src/sys/compose/input/compose_input.cpp src/sys/commands/application_command_dispatch.cpp`; `clang-tidy -p build` for affected consuming sources; `git diff --check`.
- **Target shape:** Named clipboard workflow state owns admission/revision/commit and quantize state lives beside quantize operations; `rg 'begin_compose_paste|apply_compose_paste' src/sys/compose/views/compose_view.cpp` finds no lifecycle definitions. The three compiled source registrations use lane A's approved patch; other build structure remains A-owned.
- **Out of scope:** T-659 geometry changes, core APIs/transactions (lane B), collection algorithms (lane D), build targets/registrations beyond the three approved clipboard entries (lane A), frontend schemas/UI, central architecture/task index/resume (coordinator). Existing T-722 is evidence only.

