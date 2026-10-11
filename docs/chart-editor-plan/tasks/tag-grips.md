# Search tag drag grips

[Back to task cards](../tasks.md).

## T-539 Reorder metadata search tags with drag grips

- **Status:** done
- **Stale-plan audit (2026-10-11):** `render_metadata_tag_row` still uses Up/Down and text Remove buttons in `metadata_song_section.cpp`.
- **Outcome:** Search tags have Lucide drag grips with insertion feedback and icon removal, with changes staged until Apply and discarded by Revert.
- **Read first:** `src/ui/nuklear/metadata_song_section.cpp`, `src/ui/nuklear/metadata_panel.cpp`, `src/ui/nuklear/metadata_state.h`, `src/ui/nuklear/setup_panel.cpp`, `tests/ui/nuklear_event_harness.h`.
- **Files owned:** `src/ui/nuklear/metadata_panel.cpp`, `src/ui/nuklear/metadata_panel.h`, `src/ui/nuklear/metadata_state.cpp`, `src/ui/nuklear/metadata_state.h`, `src/ui/nuklear/metadata_song_section.cpp`, `src/ui/nuklear/frontend.cpp` (metadata drag cancellation hooks only), `tests/ui/nuklear_metadata_state_tests.cpp`, `tests/ui/tag_grips_event_harness_tests.cpp`, `docs/chart-editor-plan/tasks/tag-grips.md`.
- **Supporting scope:** Coordinator approved extracted song section and narrow frontend cancellation hooks. Coordinator registers the dedicated event test in `tests/CMakeLists.txt`.
- **Steps:**
  1. Add bounded draft drag state and lossless insertion reordering.
  2. Replace Up/Down with Lucide grips and Remove with a labeled icon tooltip; render insertion feedback and guarded drop handling.
  3. Add state and production event tests for drag, cancel, Apply, Revert and removal; run focused verification and clang-tidy.
- **Invariants:** Tag strings, duplicates, hidden rows and order remain lossless; drag/removal edit only the draft, never emit actions until Apply; grip input does not select or edit text.
- **Reject/failure behavior:** Outside release, Escape, focus loss, modal ownership, tab/context changes and stale source cancel without applying a reorder. Read-only metadata never starts drag or removes tags.
- **Verify:** Focused frontend state `[tag-grips]` and event harness `[tag-grips]` pass; `clang-tidy -p build` on affected C++ sources has no new functions above complexity25; `ctest --test-dir build --output-on-failure -R '^documentation.chart_editor_state$'` passes after coordinator integration; isolated native capture verifies icon and insertion marker.
- **Out of scope:** Core metadata commands, other metadata sections, generic drag infrastructure, search behavior and shared plan indices.

- **Verification (2026-10-11):** Scratch-linked focused production event harness passed 3 cases / 38 assertions (`/tmp/t539-events.log`); state tests passed 2 cases / 11 assertions (`/tmp/t539-state.log`). Affected C++ files pass compilation and clang-tidy with no new complexity violations; existing metadata rational/reset tests and existing frontend status-bar complexity remain unchanged. The documentation checker passes against this worktree. Coordinator native captures `/tmp/next-batch-tags-{initial,marker,draft,revert,applied,removed,remove-revert,escape}.png` verify insertion feedback, staged reorder/removal, Revert, Apply and Escape. Coordinator save probe `/tmp/next-batch-tags-native-check.log` confirms saved order `beta,gamma,alpha,delta` after Apply and Save. Full suite and integrated test registration are verified.

Coordinator final verification (2026-10-11): normal and release builds pass; 3,265/3,265 full CTest checks and 414 production event-harness cases / 5876 assertions pass. Affected clang-tidy has no new or increased complexity violations. Documentation checker passes. Evidence: /tmp/next-batch-policy-build.log, /tmp/next-batch-policy-release.log, /tmp/next-batch-full-ctest.log, /tmp/next-batch-all-events.log and /tmp/next-batch-docs-final.log. No commits.
