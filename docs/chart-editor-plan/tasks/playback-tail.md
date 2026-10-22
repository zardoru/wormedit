# Playback tail and Compose navigation

## T-376 Stop completed preview and preserve navigation through audio tails

- **Status:** done (reviewed implementation and integrated main build/full suite pass; real-window check pending)
- **Outcome:** Imported charts retain legitimate trailing audio, stop at natural EOF, and allow Compose navigation after the final chart row without unavailable-playhead warnings.
- **Read first:** `src/sys/audio/sdl_preview_output.cpp`, `src/audio/preview_session.cpp`, `src/sys/audio/chart_preview_state.cpp`, `src/sys/compose/compose_playhead_cursor.h`, `docs/frontend-event-harness.md`.
- **Files owned:** Executor: `src/sys/audio/sdl_preview_output.cpp`, `src/sys/audio/sdl_preview_output.h`, `src/sys/audio/chart_preview_state.cpp`, `src/sys/audio/chart_preview_state.h`, `src/sys/compose/compose_playhead_cursor.h`, `src/sys/compose_bookmark_commands.cpp`, `src/sys/audio/inline_preview_cursor.h`, `src/audio/preview_session.cpp`, `include/wormedit/audio/preview_session.h`, `tests/sys/sdl_preview_output_tests.cpp`, `tests/core/audio_preview_session_tests.cpp`. Coordinator: `src/sys/audio/audio_transport_frame.cpp`, `src/sys/audio/audio_transport_frame.h`, `tests/sys/audio_transport_notification_tests.cpp`, `docs/chart-editor-plan/tasks/playback-tail.md`, `docs/chart-editor-plan/tasks.md`, `memory/coordination/playback-tail.md`, `src/sys/app/main.cpp`, `src/sys/compose/actions/compose_area_actions.cpp`, `tests/sys/compose_playhead_cursor_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Search current end-of-playback and row-resolution behavior; reproduce the imported Hybris HYPER/osutest failure with read-only source charts and windowless runtime tests.
  2. Correct only reproduced preview EOF, row-resolution, and follow/navigation causes; request and record exact supporting ownership before editing additional files.
  3. Add real SDL event-routing and runtime regressions for tail playback, natural EOF, and user navigation.
  4. Run focused runtime/event/documentation tests and cognitive-complexity checks; record review evidence and pending real-window verification.
- **Invariants:** Source chart/audio files remain read-only; legitimate audio tails and sustained notes play to their natural completion; no main-worktree edits or user-desktop windows.
- **Reject/failure behavior:** Failed row resolution leaves a valid editable cursor and user navigation available; failed audio operations preserve existing failure reporting.
- **Verify:** `cmake --build build --target wormedit_sdl_audio_tests wormedit_audio_tests wormedit_core_tests wormedit_frontend_event_harness_tests`; `ctest --test-dir build --output-on-failure -R 'audio|preview|playhead|documentation.chart_editor_state' -E '_NOT_BUILT'`; `env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build -L event-harness --output-on-failure --no-tests=error`; `clang-tidy -p build <changed-source-file>` (new functions complexity <=25 and existing complexity does not increase).
- **Out of scope:** Chart content/import changes, audio-tail cutoff at the last chart event, unrelated notification redesign, Xvfb retry (previous sandbox approval rejected), main integration or pushes.

- **Supporting ownership approval (2026-10-04, before edits):** Coordinator owns `src/sys/audio/audio_transport_frame.cpp`, `src/sys/audio/audio_transport_frame.h`, and `tests/sys/audio_transport_notification_tests.cpp` to repair and test UI-only bounded tail row projection and inverse arithmetic overflow. Executor retains audio lifecycle owners exclusively.

- **Review evidence:** `memory/coordination/playback-tail.md` records actual eight-chart runtime reproduction, preserved 165.866 s music tail, backward-seek recovery, natural EOF, focused tests, and pending root integration/real-window verification. No preview lifecycle source change was warranted.

- **Final root checkpoint (2026-10-04):** main `d64c048` and evidence `6315a69` are integrated; full application build and 2,255/2,255 CTest cases pass with T-377/T-378, including 144 isolated event-harness cases. Eight real-chart main runtime probes preserve natural music EOF and allow backward tail seeks; see `memory/coordination/playback-tail.md`. Final suite: `/tmp/hidden-history-validation/main-suite.log`. Real-window verification remains pending under the recorded sandbox approval block.
