# Tooltip viewport bounds

## T-377 Keep hover tooltips readable inside the application viewport

- **Status:** ready (implementation reviewed; root full app and suite pending)
- **Outcome:** Sounds dock and shared tooltips wrap or move inside every edge of the logical application viewport.
- **Read first:** `src/ui/nuklear/chrome.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/notification_toasts.cpp`, `docs/frontend-event-harness.md`.
- **Files owned:** Executor: new `src/ui/nuklear/tooltip.h`, new `src/ui/nuklear/tooltip.cpp`, `src/ui/nuklear/chrome.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/metadata_difficulty_section.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/metadata_audio_section.cpp`, `src/ui/nuklear/notification_toasts.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_panel.cpp`. Coordinator: `tests/ui/nuklear_event_harness.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, new `tests/ui/nuklear_tooltip_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `docs/chart-editor-plan/tasks/tooltip-bounds.md`, `docs/chart-editor-plan/tasks.md`, new `memory/coordination/tooltip-bounds.md`.
- **Steps:**
  1. Reproduce the Sounds dock overflow with real Nuklear command geometry in the windowless frontend and inspect logical viewport dimensions.
  2. Add shared viewport-bounded tooltip rendering and migrate direct callers; audit timing and notification tooltip geometry for all edges.
  3. Add real draw-command regressions covering right, left, top, bottom, long text, and viewport resize.
  4. Build local targets, run focused event/geometry/documentation tests and cognitive-complexity checks, and record pending visual verification.
- **Invariants:** Preserve hover activation, style, input behavior, and logical coordinates/DPI scaling; do not modify vendor sources.
- **Reject/failure behavior:** Missing or too-small viewport safely suppresses unreadable tooltip drawing without swallowing input.
- **Verify:** `python3 /tmp/tooltip-bounds-review/build-frontend.py` (compile/link affected production objects with unchanged dependencies read-only); `env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir /tmp/tooltip-bounds-review/frontend -L event-harness --output-on-failure --no-tests=error` (140 isolated cases pass); `ctest --test-dir build --output-on-failure -R 'documentation.chart_editor_state'`; `clang-tidy -p build <changed-source-file>` (new function cognitive complexity <=25; existing complexity does not increase).
- **Out of scope:** Audio/playback changes, text/content redesign, external Nuklear edits, user-desktop windows, Xvfb retry after rejected sandbox approval, main integration or pushes.

- **Supporting test ownership approval (2026-10-04, before edits):** Coordinator may expose retained drawn-text rectangles through `tests/ui/nuklear_event_harness.h` to verify the actual Sounds clear-selection hover producer after production render clears commands. No production test-only rendering branch is introduced.

- **Ownership transfer (2026-10-04, before coordinator source edits):** Executor releases all listed tooltip source/header files to the coordinator after the initial implementation, namespace fixes, and direct notification viewport seam. The coordinator now owns all T-377 files for verification, necessary fixes, and review. Executor moves to the separate T-378 hidden-history worktree under root coordination; no concurrent tooltip source edits remain.

- **Review evidence:** `memory/coordination/tooltip-bounds.md` records native baseline overflow, seven actual command/hover cases, 140 isolated frontend cases, read-only dependency reuse, complexity review, and explicitly pending root full app/suite and visual gates.
