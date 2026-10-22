# Bookmark rename harness gesture alignment

[Back to the task card index](../tasks.md).

## T-456 Exercise double-click bookmark rename in the event harness

- **Status:** done (2026-10-07)
- **Result:** the baseline failures followed T-396's intentional removal of the single-click rename strip. All eight bookmark-rename event-harness cases pass with double-click opening and a new single-click negative case; documentation check passes. Production behavior is unchanged.
- **Outcome:** bookmark rename workflow tests use the deliberate double-click gesture, and a single click does not open the editor.
- **Read first:** `tests/ui/bookmark_rename_event_harness_tests.cpp`, `src/ui/nuklear/compose/compose_bookmark_label.cpp`, `src/ui/nuklear/app/frontend.cpp`, `docs/chart-editor-plan/tasks/compose-cleanup.md` (T-396).
- **Files owned:** `tests/ui/bookmark_rename_event_harness_tests.cpp`, `cmake/check_chart_editor_docs.cmake` (register this card in the checker), this card; root owns task index and status coordination.
- **Steps:**
  1. Confirm the five baseline failures use the removed single-click rename gesture.
  2. Update rename-opening events to double-clicks and add an ordinary-click regression.
  3. Run the bookmark event tests and documentation check.
- **Invariants:** production interaction stays unchanged; capture, identity, cancellation and stale-revision assertions remain.
- **Reject/failure behavior:** ordinary clicks must not open a rename field.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -L bookmark-rename`; `ctest --test-dir build --output-on-failure -R documentation.chart_editor_state`.
- **Out of scope:** changing bookmark gestures or other batch implementation files.
