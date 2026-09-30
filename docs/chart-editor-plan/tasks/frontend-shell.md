# Task cards: frontend shell

[Back to the task-card index](../tasks.md).

Stage 2 leftovers from [foundation](../stages/foundation.md): input plumbing,
Nuklear unit size, transport controls, and the empty Compare/Validate tab. Apply
the stale-plan check (rule 9) first; parts of stage 2 already landed.

## T-40 Make every Nuklear text field select all on double-click

- **Status:** ready
- **Outcome:** double-clicking any single-line text field in Metadata, Timing, or the Compose inspector selects its whole value, and typing replaces it.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (text-entry paragraph), `src/ui/nuklear/chrome.h`, `src/ui/nuklear/panel_input.h`, `src/ui/nuklear/metadata_panel.cpp`.
- **Files owned:** `src/ui/nuklear/chrome.h`, new `src/ui/nuklear/text_field.cpp`, new `src/ui/nuklear/text_field.h`, every `src/ui/nuklear/*.cpp` that calls `nk_edit_string` or `nk_edit_buffer`, `CMakeLists.txt`, `tests/ui/nuklear_metadata_state_tests.cpp`.
- **Steps:**
  1. `grep -n "nk_edit_string\|nk_edit_buffer" src/ui/nuklear/*.cpp` and list every call site in the report.
  2. Create `text_field.h/.cpp` with one wrapper `worm_ui_nuklear_text_field(...)` that forwards to `nk_edit_string` and, on a double-click inside the widget bounds, sets the edit's selection to the whole buffer (`nk_text_edit` `select_start = 0`, `select_end = length`).
  3. Replace each call site from step 1 with the wrapper. Keep flags and buffer sizes identical.
  4. Add a state test for the pure selection helper (no Nuklear context needed if you split the "compute selection range" part into a free function).
- **Invariants:** buffer capacity, filters, and commit-on-Enter behavior of each field are unchanged.
- **Reject/failure behavior:** if Nuklear's API cannot set the selection from outside, STOP and report the exact API gap.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^(frontend-state|architecture)\.'`; smoke PNG of `metadata` shows no layout change.
- **Out of scope:** multi-line fields, IME behavior, new fields.

## T-41 Add a simulated frontend event-queue test harness

- **Status:** ready
- **Outcome:** a test can feed a scripted list of SDL-like events (key, mouse, wheel, text) into the Nuklear frontend without a window and assert the resulting `WormUiWorkspaceAction` values.
- **Read first:** `src/ui/nuklear/frontend.cpp` (event queue functions near `queue_compose_wheel_event`), `src/ui/frontend.h`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/CMakeLists.txt`.
- **Files owned:** new `tests/ui/nuklear_event_harness.h`, new `tests/ui/nuklear_event_harness.cpp`, new `tests/ui/nuklear_event_harness_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Identify the queue entry points the SDL loop calls (`worm_ui_nuklear_queue_*`). List them in the report.
  2. Write a harness that owns a headless `nk_context` with a fixed font stub, calls those entry points from a scripted vector, runs one frame per script step, and collects emitted actions.
  3. Add three tests: wheel over Compose scrolls, Delete key emits the delete action, Tab-switch click emits the tab action.
- **Invariants:** no production code changes; the harness links only the frontend-state test target.
- **Reject/failure behavior:** if a frame cannot run without an SDL renderer, STOP and report which call requires it.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.'` includes the three new tests and passes.
- **Out of scope:** pixel screenshots, IME.

## T-42 Split `metadata_panel.cpp` by section (pure move)

- **Status:** ready
- **Outcome:** `metadata_panel.cpp` (about 1,900 lines) becomes an entry file plus section units; the app renders identically.
- **Read first:** `docs/chart-editor-plan/tasks/compose-split.md` (shared rules), `src/ui/nuklear/metadata_panel.cpp`, `src/ui/nuklear/metadata_panel.h`, `src/ui/nuklear/metadata_state.h`.
- **Files owned:** `src/ui/nuklear/metadata_panel.cpp`, new `src/ui/nuklear/metadata_panel_internal.h`, new `metadata_song_section.cpp/.h`, new `metadata_difficulty_section.cpp/.h`, new `metadata_format_fields.cpp/.h`, new `metadata_audio_section.cpp/.h`, `CMakeLists.txt`.
- **Steps:**
  1. List the collapsible sections the file renders (grep `nk_tree_push` / section titles). Map each `static` function to one section; put shared helpers in the internal header.
  2. Move one section per build, following the compose-split shared rules (pure move, no renames).
  3. Build after each move; fix only include/link errors.
- **Invariants:** identical widget order, IDs, and actions.
- **Reject/failure behavior:** a function used by two sections stays in `metadata_panel.cpp` and is declared in the internal header; do not duplicate it.
- **Verify:** standard build/test; `clang-tidy -p build` on each new file shows no new complexity warning; smoke PNG of `metadata`.
- **Out of scope:** any behavior change; `timing_panel.cpp` (T-43).

## T-43 Split `timing_panel.cpp` and `compose_input_panel.cpp` (pure move)

- **Status:** blocked-on T-42
- **Outcome:** neither file exceeds about 1,200 lines; behavior unchanged.
- **Read first:** `docs/chart-editor-plan/tasks/compose-split.md`, `src/ui/nuklear/timing_panel.cpp`, `src/ui/nuklear/compose_input_panel.cpp`.
- **Files owned:** those two files, new sibling units named after the responsibility moved (for example `timing_recipe_panel.cpp`, `compose_pointer_input.cpp`), their headers, `CMakeLists.txt`.
- **Steps:**
  1. For `timing_panel.cpp`, move the SV-recipe form and recipe-range drag code into `timing_recipe_panel.cpp`.
  2. For `compose_input_panel.cpp`, move pointer-gesture handling apart from keyboard handling.
  3. Build after each move.
- **Invariants:** identical behavior.
- **Reject/failure behavior:** as T-42.
- **Verify:** as T-42 with smoke PNGs of `timing` and `compose`.
- **Out of scope:** `main.cpp` reduction (separate decision).

## T-44 Add a loop region to the preview transport

- **Status:** ready
- **Outcome:** with a committed area/Space selection, **Loop selection** plays that exact interval repeatedly until Stop; the playhead jumps back at the interval end.
- **Read first:** `include/wormedit/audio/preview_session.h`, `include/wormedit/audio/transport.h`, `src/sys/transport_player_actions.cpp`, `docs/chart-editor-plan/playback/player-preview.md`.
- **Files owned:** `include/wormedit/audio/preview_session.h`, `src/audio/preview_session.cpp`, `src/sys/transport_player_actions.cpp`, `src/sys/transport_player_actions.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/core/audio_preview_session_tests.cpp`.
- **Steps:**
  1. Add an optional `[begin_frame, end_frame)` loop to the preview session; at `end_frame` it performs the existing seek to `begin_frame` on the control thread (never in `render()`).
  2. Test: a 2-second loop over a fixture renders the first sample of `begin_frame` again after `end_frame`, and Stop still yields silence on the next render.
  3. Wire a toolbar toggle through the standard pattern; convert the selection rows to frames with the existing timing index.
- **Invariants:** `render()` stays allocation-free; Stop remains a hard boundary.
- **Reject/failure behavior:** empty or reversed selection disables the toggle; nothing plays.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual run with a looped selection.
- **Out of scope:** playback rate (T-45), player-preview video.

## T-45 Play the preview at a reduced rate with pitch change

- **Status:** done
- **Outcome:** chart preview plays at one of the rates 0.1, 0.2, 0.25, 0.33, 0.5, 0.75, 0.8, 0.9, 1.0 with pitch following speed (tape-style); the playhead, player preview, metronome, and loop (T-44) stay on the unchanged source sample-frame clock.
- **Decision (user, 2026-09-30):** both modes are wanted, with and without pitch change; this card is the pitch-change mode, T-233 the pitch-preserving mode. Rates are stored as exact rationals: 1/10, 1/5, 1/4, **1/3** (labelled 0.33×), 1/2, 3/4, 4/5, 9/10, 1. No rates above 1 are offered.
- **Read first:** `src/sys/sdl_preview_output.cpp` (`stream_callback`, `render_requested`), `include/wormedit/audio/preview_session.h` (`render`, `pump`), `src/sys/audio_transport_frame.cpp` (playhead/source-frame reporting), `docs/timing-preview-scheduling.md`.
- **Files owned:** new `include/wormedit/audio/playback_rate.h`, new `src/audio/playback_rate.cpp`, new `tests/core/audio_playback_rate_tests.cpp`, `src/sys/sdl_preview_output.cpp/.h`, `src/sys/audio_transport_frame.cpp/.h`, `include/wormedit/audio/preview_session.h`, `src/audio/preview_session.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. `playback_rate.h`: the fixed rate table above as `rational_t`, lookup by index, label text, and `output_frames_for(source_frames, rate)` / `source_frames_for(output_frames, rate)` with exact rounding rules. Tests for every entry, including 1/3.
  2. The mixer keeps rendering **source-rate** frames exactly as today. Apply the rate at the SDL stream with `SDL_SetAudioStreamFrequencyRatio(stream, rate)` (SDL3 resamples and asks the callback for proportionally fewer frames). If that call is unavailable or rejects a value, fall back to `ma_linear_resampler` from the already vendored miniaudio (`src/audio/miniaudio_backend.c`) between `render()` and `SDL_PutAudioStreamData`; say in the report which path was used.
  3. Playhead: report latency in source frames. SDL_GetAudioStreamQueued reports queued input bytes on the source clock, so use those frames directly (converting again would double-scale them); expose the audible clock while scheduling retains its internal unadjusted source clock. Clamp latency at the seek/start frame.
  4. Rate changes during playback: change the ratio at the next callback; do not restart the session or reschedule events. Stop remains a hard boundary (existing Stop test must still pass).
  5. Tests (headless, no SDL): at rate 1/2 a 1-second chart advances exactly `sample_rate / 2` source frames per second of output; at 1/3 there is no drift over 10 minutes of simulated callbacks (rounding carries a remainder).
- **Invariants:** event scheduling, loop bounds, and the player preview use source frames only; `render()` stays allocation-free.
- **Reject/failure behavior:** a rate not in the table is rejected, never clamped; if neither resampling path works, playback stays at 1.0 with a warning status.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^audio\.|sdl_preview'` (92/92 passed); parent full suite 849/849 passed. Clang-tidy on affected C++ sources: no complexity warnings. Native SDL 3.4.16 accepted all nine ratios, with exact 44100-Hz source frequencies including 14700 Hz for 1/3; the SDL ratio path was used. Isolated dummy-device harness reached source frame 11025 at 1.012 s at 0.25x and source frame 22016 after 2 s, preserved the session on a callback-boundary rate change, and verified Stop silence. **Verification adjustment (user, 2026-09-30):** isolated SDL measurements replace physical listening for this backend card; physical listening remains for after T-234. Logs, harness and selective patch: `/tmp/wormedit-t45/`.
- **Out of scope:** pitch preservation (T-233), UI (T-234), rates above 1.0.

## T-233 Preserve pitch at reduced playback rates

- **Status:** ready
- **Outcome:** a "Keep pitch" option plays the same rates without pitch change, using an in-house WSOLA time-stretcher on the mixed stereo output; the clock and playhead behave exactly as in T-45.
- **Read first:** `include/wormedit/audio/playback_rate.h`, `src/sys/sdl_preview_output.cpp`, `include/wormedit/audio/preview_session.h`.
- **Files owned:** new `include/wormedit/audio/time_stretch.h`, new `src/audio/time_stretch.cpp`, new `tests/core/audio_time_stretch_tests.cpp`, `src/sys/sdl_preview_output.cpp/.h`, `src/sys/audio_transport_frame.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. WSOLA with fixed parameters (window 20 ms, overlap 50 %, search ±8 ms by normalized cross-correlation on the mid channel), preallocated buffers sized at construction for the lowest rate, `process(input, output)` allocation-free.
  2. In keep-pitch mode the SDL stream ratio stays 1.0; the output stage asks the session for `ceil(output × rate) + lookahead` source frames and stretches them.
  3. Report the stretcher's fixed latency so the playhead subtracts it (in source frames).
  4. Tests: a 440 Hz sine at rate 1/2 keeps its dominant frequency within ±2 Hz and doubles in length within one window; silence stays silent; output is deterministic; switching modes mid-play flushes without clicks larger than one window fade.
- **Invariants:** same as T-45; the stretcher never runs in the scheduling path.
- **Reject/failure behavior:** if a callback cannot get enough source frames, output silence for that block and report an underrun, as today.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual listening at 0.5 and 0.1.
- **Out of scope:** formant preservation, third-party libraries (Rubber Band/SoundTouch) unless the user asks.

## T-234 Add rate and pitch controls to the transport UI

- **Status:** done
- **Outcome:** the status-bar transport shows a rate dropdown with the nine rates (label "1×", "0.9×" … "0.1×") and a "Keep pitch" toggle (enabled once T-233 lands); commands `transport.rate_slower` / `transport.rate_faster` step through the table; the chosen rate and pitch mode persist per session.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_status_bar`), `src/core/command_registry.cpp`, `include/wormedit/audio/playback_rate.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/core/command_registry.cpp`, `src/sys/main.cpp` (dispatch and view only), `src/sys/transport_player_actions.cpp/.h`, `tests/core/command_registry_tests.cpp`, `src/sys/compose_view.h`, `src/sys/application_commands.h/.cpp`, `src/sys/application_command_dispatch.cpp`, `tests/sys/application_commands_tests.cpp` (scope extension approved by user, 2026-09-30).
- **Steps:** 1. Scalar view: rate index, keep-pitch flag, keep-pitch availability. 2. Dropdown + toggle using the themed chrome helpers. 3. Registry commands with default chords that do not conflict (check `diagnose_shortcut_conflicts`); implemented Primary+F11 (slower) and Primary+F12 (faster), using supported keys and checking registry conflicts.
- **Invariants:** changing rate never changes the document or cursor.
- **Reject/failure behavior:** stepping past 0.1× or 1× does nothing.
- **Verify:** `cmake --build build` passed; 821/821 filtered core/audio/frontend/documentation tests passed; isolated Xvfb smoke `/tmp/wormedit-t234/status.png` inspected with rate dropdown and disabled Keep pitch. Clang-tidy run on changed sources; final integrated verification follows parent handoff.
- **Out of scope:** per-chart saved rates.

## T-46 Render synchronized side-by-side difficulties in Compare/Validate

- **Status:** ready
- **Outcome:** the Compare/Validate tab draws two read-only Compose fields (active and comparison difficulty) that scroll together, with yellow rows where `compare_chart_difficulties` reports differences.
- **Read first:** `include/wormedit/core/chart_compare.h`, `include/wormedit/core/workspace.h` (comparison-pane selection), `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.h`, `src/ui/nuklear/frontend.cpp` (tab dispatch near `WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE`).
- **Files owned:** new `src/ui/nuklear/compare_panel.cpp/.h`, new `src/sys/compare_view.cpp/.h`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/main.cpp` (view hookup only), `CMakeLists.txt`, `tests/core/chart_compare_tests.cpp`.
- **Steps:**
  1. Build a `compare_view` that produces two field scenes for the same viewport rows, feeding the comparison's yellow-row primitives into both.
  2. Render them with `draw_compose_scene_canvas` in two columns; share one scroll offset.
  3. Add a difficulty picker for the comparison pane using the existing workspace selection.
- **Invariants:** read-only; no edit commands are emitted from this tab.
- **Reject/failure behavior:** with only one difficulty, show a one-line hint instead of the second pane.
- **Verify:** standard build/test; smoke PNG with a two-difficulty fixture (for example a multi-chart `.ssc` in `tests/fixtures/stepmania`).
- **Out of scope:** validation report list (T-47), editing from the compare tab.

## T-47 List validation diagnostics in Compare/Validate

- **Status:** blocked-on T-46
- **Outcome:** the tab shows a bounded list of document-validation diagnostics for the active chart; clicking one moves the Compose cursor to its row.
- **Read first:** `include/wormedit/core/document.h` (graph validation diagnostics), `src/sys/compare_view.cpp`, `src/ui/nuklear/compare_panel.cpp`.
- **Files owned:** `src/sys/compare_view.cpp/.h`, `src/ui/nuklear/compare_panel.cpp`, `src/ui/frontend.h`.
- **Steps:**
  1. Project up to 256 diagnostics (code, message, optional row) into the scalar view.
  2. Render them as a selectable list; a click emits a cursor-jump action reusing the marker-jump path.
- **Invariants:** diagnostics computed once per revision.
- **Reject/failure behavior:** diagnostics without a row are listed but not clickable.
- **Verify:** standard build/test; manual check with a fixture that produces diagnostics.
- **Out of scope:** export loss reports (T-110).

## T-48 Decide how trackpad momentum reaches Compose

- **Status:** blocked-on-user
- **Outcome:** a recorded decision on whether wormedit synthesizes momentum or relies on OS-delivered momentum wheel events.
- **Read first:** `include/wormedit/core/viewport_input.h` (`begin_momentum`), `src/ui/nuklear/frontend.cpp` (`queue_compose_wheel_event`), `docs/chart-editor-plan/editing/interactions.md` (wheel/trackpad paragraphs).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`.
- **Steps:**
  1. STOP: explain to the user that SDL3 exposes no trackpad phase events; macOS already sends momentum as wheel events. Ask whether synthesized momentum is wanted on Linux/Windows.
  2. Record the answer. If "yes", add a follow-up card that calls `begin_momentum` when smooth wheel input stops for 50 ms.
- **Invariants:** none (decision only).
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-49 Show BGA image thumbnails in the resource catalog

- **Status:** ready
- **Outcome:** each image row in the Compose BGA catalog shows a small thumbnail once decoded; undecoded rows show a placeholder.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/player_preview_texture.h`, `include/wormedit/visual/image_job.h`, `src/sys/compose_resource_presenter.cpp`.
- **Files owned:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/frontend.h`, `tests/core/compose_resource_presenter_tests.cpp`.
- **Steps:**
  1. Reuse the player-preview image job and texture LRU; request decode only for visible rows.
  2. Expose a per-row image handle through the scalar view; draw it at 32×32 logical pixels.
- **Invariants:** the texture LRU bound is unchanged; decoding stays off the frame thread.
- **Reject/failure behavior:** failed decodes show the missing-resource placeholder.
- **Verify:** standard build/test; smoke PNG with a BMS fixture that has `#BMP`.
- **Out of scope:** video thumbnails.
