# Task cards: frontend shell

[Back to the task-card index](../tasks.md).

Stage 2 leftovers from [foundation](../stages/foundation.md): input plumbing,
Nuklear unit size, transport controls, and the empty Compare/Validate tab. Apply
the stale-plan check (rule 9) first; parts of stage 2 already landed.

## T-40 Make every Nuklear text field select all on double-click

- **Status:** done
- **Implementation notes (2026-10-02):** all 42 edit calls across 12 frontend units use the shared chrome adapter, including autofocus. The selection helper is independent of Nuklear; the adapter is inline so popup-test callers need no new link dependency. Full application build and 1,561/1,561 tests pass; clang-tidy shows no new complexity violations. Isolated Metadata captures verify whole-value selection and typing replacement with unchanged layout.
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
- **Implementation notes (2026-10-02):** feasibility audit stopped at the required headless-frame gate: `worm_ui_frontend_init` requires an SDL window and renderer before initializing its private context; `worm_ui_frontend_compose` requires that private context and window. The state-test target does not link the frontend. A no-window harness needs a production injection seam, outside this card’s owned files and no-production-change invariant. No harness was implemented.
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

- **Status:** done
- **Implementation notes (2026-10-02):** Song, Difficulty, Audio sync, Audio/export, and Format-specific fields moved in that order. The app build passed after each move (`/tmp/wormedit-t42-song-build.log`, `-difficulty-build.log`, `-audio-sync-build.log`, `-audio-export-build.log`, `-format-build.log`) and after restoring the six original Nuklear tree identities (`-preserved-tree-ids-build.log`). Clang-tidy passed on `metadata_panel.cpp` and all four section units with no project cognitive-complexity warnings (only vendored nlohmann warnings in the format unit). The isolated Metadata before/after capture comparison was pixel-identical (AE 0; `/tmp/wormedit-metadata-split/after.png`). The final coordinated build passed; full tests passed 1,568/1,568 (`/tmp/wormedit-final-all-tests2.log`, 53.50 seconds).
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

## T-43 Split Compose input responsibilities (pure move)

- **Status:** done
- **Implementation notes (2026-10-03):** stale-plan audit found `src/ui/nuklear/timing_panel.cpp` is no longer tracked; T-321 retired the Timing panel and its state after extracting the shared stage editor and recipe form. T-42 is done, so the old Timing-panel step and blocker are satisfied/obsolete. The remaining work is to split the still-large `compose_input_panel.cpp` by responsibility; no Timing panel is to be recreated.
- **Verification (2026-10-03):** application builds passed after each extraction; coordinator/keyboard/viewport/timing/object units are 906/490/530/361/555 lines. Configured clang-tidy reported no cognitive-complexity findings in the five changed/new units. Frontend-state tests passed 102/102 (`/tmp/wormedit-t43-frontend-tests.log`); the actual isolated Compose window displayed the raw BMS fixture and handled its column controls after extraction (`/tmp/wormedit-t311/raw-bms-plus.png`, `raw-bms-minus.png`). Documentation validation passed.
- **Outcome:** Compose keyboard, viewport, timing-gesture, and object/resource pointer input live in separate units, with shared queue/state/cancel coordination retained in the coordinator; behavior and action ordering remain unchanged.
- **Read first:** `docs/chart-editor-plan/tasks/compose-split.md`, `docs/chart-editor-plan/tasks/compose-timing.md` (T-321), `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_input_panel.h`.
- **Files owned:** `src/ui/nuklear/compose_input_panel.cpp/.h`, new `src/ui/nuklear/compose_input_internal.h`, new `src/ui/nuklear/compose_keyboard_input.cpp/.h`, new `src/ui/nuklear/compose_viewport_input.cpp/.h`, new `src/ui/nuklear/compose_timing_input.cpp/.h`, new `src/ui/nuklear/compose_object_input.cpp/.h`, `CMakeLists.txt`.
- **Steps:**
  1. Move keyboard event queuing and dispatch to `compose_keyboard_input.cpp/.h`.
  2. Move pointer focus, viewport wheel/pinch, beat-fit, and overview handling to `compose_viewport_input.cpp/.h`.
  3. Move timing stage/marker, Kiai, and recipe/SV gestures to `compose_timing_input.cpp/.h`.
  4. Move note, resource, BMSON, semantic-layer, and BGM pointer handling to `compose_object_input.cpp/.h`.
  5. Build after each move.
- **Invariants:** identical widget/action behavior and event ordering; shared helpers stay in the coordinator/internal header rather than being duplicated.
- **Reject/failure behavior:** stop if a moved responsibility requires behavior changes or an unowned production file.
- **Verify:** build after each move; run the frontend-state tests and `clang-tidy -p build` on changed/new `.cpp` files after extraction. Visually verify Compose in isolated Xvfb; Timing-screen smoke is obsolete because T-321 retired that screen.
- **Out of scope:** `main.cpp` reduction (separate decision), recreating the retired Timing panel.

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

- **Status:** done
- **Outcome:** a "Keep pitch" option plays the same rates without pitch change, using an in-house WSOLA time-stretcher on the mixed stereo output; the clock and playhead behave exactly as in T-45.
- **Read first:** `include/wormedit/audio/playback_rate.h`, `src/sys/sdl_preview_output.cpp`, `include/wormedit/audio/preview_session.h`.
- **Files owned:** new `include/wormedit/audio/time_stretch.h`, new `src/audio/time_stretch.cpp`, new `tests/core/audio_time_stretch_tests.cpp`, `src/sys/sdl_preview_output.cpp/.h`, `src/sys/audio_transport_frame.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`; user-approved integration extensions (2026-09-30): `src/sys/compose_view.h`, `src/sys/main.cpp`, `src/sys/transport_player_actions.cpp`, `tests/sys/application_commands_tests.cpp`, `tests/sys/sdl_preview_output_tests.cpp`, `src/sys/chart_preview_state.cpp/.h`, `src/sys/application_frame_projection.cpp` (EOF playing/frame and source-playhead projection).
- **Steps:**
  1. WSOLA with fixed parameters (window 20 ms, overlap 50 %, search ±8 ms by normalized cross-correlation on the mid channel), preallocated buffers sized at construction for the lowest rate, `process(input, output)` allocation-free.
  2. In keep-pitch mode the SDL stream ratio stays 1.0; the output stage asks the session for `ceil(output × rate) + lookahead` source frames and stretches them.
  3. Report the stretcher's fixed latency so the playhead subtracts it (in source frames).
  4. Tests: a 440 Hz sine at rate 1/2 keeps its dominant frequency within ±2 Hz and doubles in length within one window; silence stays silent; output is deterministic; switching modes mid-play flushes without clicks larger than one window fade.
- **Algorithm credit:** Werner Verhelst and Marc Roelands, “An overlap-add technique based on waveform similarity (WSOLA) for high quality time-scale modification of speech,” ICASSP 1993, volume 2, pp. 554–557, [DOI](https://doi.org/10.1109/ICASSP.1993.319366).
- **Invariants:** same as T-45; the stretcher never runs in the scheduling path.
- **Reject/failure behavior:** if a callback cannot get enough source frames, output silence for that block and report an underrun, as today.
- **Verify:** coordinated application build passed; full suite 893/893 passed. Core sine checks preserve 440 Hz within ±2 Hz and exact rational duration at 1×, 0.5×, and 0.1×; actual SDL dummy capture covers 0.5×/0.1×, rejected/successful EOF-drain seeks, mode/rate transitions, and mid-play Stop. Maximum measured transition sample step fell from 0.950985 to 0.0626853 after fading the old stage to zero and recomputing the new SDL callback budget. Isolated Xvfb controls inspected at `/tmp/wormedit-t233/keep-pitch-on.png` and `/tmp/wormedit-t233/keep-pitch-off.png`. Changed functions pass clang-tidy complexity checks. User approved isolated measurements instead of physical audio listening (2026-09-30).
- **Out of scope:** formant preservation, third-party libraries (Rubber Band/SoundTouch) unless the user asks.

## T-234 Add rate and pitch controls to the transport UI

- **Status:** done
- **Outcome:** the status-bar transport shows a rate dropdown with the nine rates (label "1×", "0.9×" … "0.1×") and a "Keep pitch" toggle (enabled once T-233 lands); commands `transport.rate_slower` / `transport.rate_faster` step through the table; the chosen rate and pitch mode persist per session.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_status_bar`), `src/core/command_registry.cpp`, `include/wormedit/audio/playback_rate.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`, `src/core/command_registry.cpp`, `src/sys/main.cpp` (dispatch and view only), `src/sys/transport_player_actions.cpp/.h`, `tests/core/command_registry_tests.cpp`, `src/sys/compose_view.h`, `src/sys/application_commands.h/.cpp`, `src/sys/application_command_dispatch.cpp`, `tests/sys/application_commands_tests.cpp` (scope extension approved by user, 2026-09-30).
- **Steps:** 1. Scalar view: rate index, keep-pitch flag, keep-pitch availability. 2. Dropdown + toggle using the themed chrome helpers. 3. Registry commands with default chords that do not conflict (check `diagnose_shortcut_conflicts`); implemented Primary+F11 (slower) and Primary+F12 (faster), using supported keys and checking registry conflicts.
- **Invariants:** changing rate never changes the document or cursor.
- **Reject/failure behavior:** stepping past 0.1× or 1× does nothing.
- **Verify:** `cmake --build build` passed; 821/821 filtered core/audio/frontend/documentation tests passed; isolated Xvfb smoke `/tmp/wormedit-t234/status.png` inspected with rate dropdown and disabled Keep pitch. Final parent build and full suite 863/863 passed; changed project functions pass clang-tidy complexity checks.
- **Out of scope:** per-chart saved rates.

## T-307 Skip the playhead with the navigation keys and wheel while playing

- **Status:** done (2026-10-02; `src/sys/compose_playhead_skip.cpp/.h`, `tests/sys/compose_playhead_skip_tests.cpp`, wired in `compose_input.cpp`; see the note below)
- **Outcome:** while chart preview plays in Compose, Up/Down move the playhead one active snap, PageUp/PageDown one measure, and the mouse wheel by the rows it would scroll, all in screen direction (downscroll flips them); playback continues from the new point and the following view jumps with it. Paused or stopped, these inputs keep moving the edit cursor and view as today.
- **Request (user, 2026-10-01):** "skip the playhead ahead and back while I am playing"; "obey pageup/dn, mwheel and arrows".
- **Current behavior (2026-10-01):** during playback Up/Down/PageUp/PageDown dispatch `move_edit_cursor_action_t` / `page_edit_cursor_action_t` (`dispatch_compose_navigation_actions`, `src/sys/compose_input.cpp`) and the wheel scrolls the view (`…_VIEWPORT_INPUT_COARSE_WHEEL` / `…_SMOOTH_WHEEL`), but the playhead-following scroll overrides both at once, so they appear to do nothing.
- **Stale-plan check:** grep `seek_compose_playhead` in `src/sys/`.
- **Read first:** `src/sys/compose_input.cpp` (`dispatch_compose_navigation_actions`, wheel viewport inputs), `src/sys/compose_minimap_navigation.cpp` (`navigate_compose_minimap_transport`, T-241 clamp and seek), `src/sys/compose_view.h` (`preview_playhead_absolute_row`, `transport_seek`, `transport_preview_playing`), `include/wormedit/core/timing.h` (`timing_at`).
- **Files owned:** `src/sys/compose_input.cpp`, new `src/sys/compose_playhead_skip.cpp/.h`, new `tests/sys/compose_playhead_skip_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. `compose_playhead_skip`: given the playhead absolute row, the chart's measure index, the active snap, and a signed step (snaps, measures, or wheel rows), return the target row. One snap goes to the next/previous active-snap row strictly past the playhead, so the first press lands on the grid. One measure goes to the same offset in the next/previous measure, clamped to that measure's length.
  2. Convert the target row to an output frame with `timing_at` (warp side = entry) and clamp it with T-241 step 7: before the first audible frame becomes that frame (≥ 0); past the compiled end becomes the end.
  3. In `compose_input.cpp`, when `transport_preview_playing()` is true, send navigation actions and non-zoom wheel inputs to the skip instead of the cursor or view. Sum every such input of one frame (key repeats, wheel and trackpad deltas, with sub-row remainders kept until the next frame) and call `transport_seek` at most once per frame. Primary+wheel keeps zooming.
  4. Wheel rows: the pixel delta the view would have scrolled, divided by the current pixels per row.
  5. Tests: snap step from between two rows lands on the next row; measure step across measures of different lengths; downscroll flips direction; both clamps; many inputs in one frame produce one seek; while paused or stopped no seek happens and the cursor moves as today.
- **Implementation note (2026-10-02, Claude):** `compose_playhead_skip` is wired into `dispatch_compose_navigation_actions` (arrows and PageUp/PageDown, including key repeat) and the Compose wheel path. Simplifications versus the steps above: each wheel event skips one active snap (no pixel-to-row conversion, no sub-row remainder) and each input seeks immediately instead of being summed into one seek per frame. Unit tests cover the target-row math only; paused behavior is unchanged by construction (the skip runs only when `transport_preview_playing()`). Compiled headless; the SDL-side wiring is syntax-checked only.
- **Invariants:** skipping never changes the document or playback rate; the seek uses the same frame clock as T-241.
- **Reject/failure behavior:** if the seek fails, keep playing from the prior position and show the existing seek message (T-241).
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^documentation\.|compose_playhead_skip'`; `clang-tidy -p build` on each changed `.cpp`; isolated Xvfb/SDL-dummy check that Down, PageDown, and one wheel notch during playback each advance the reported playhead by the expected rows and playback continues.
- **Out of scope:** seeking while paused, a fixed-seconds skip, audible scrubbing, marker jumps (T-308).

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

- **Status:** done (user decision 2026-09-30: no synthetic momentum; rely on OS-delivered wheel events (macOS momentum), elsewhere scrolling stops with the fingers.)
- **Outcome:** decision recorded.
- **Read first:** `include/wormedit/core/viewport_input.h`.
- **Files owned:** this file.
- **Steps:** recorded.
- **Invariants:** none.
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

## T-264 Fix the crash from combo boxes inside popups

- **Status:** done
- **Outcome:** opening the new-project wizard's layout combo and the BMS text-encoding combo no longer aborts; no combo, menu, or contextual menu is ever opened inside another popup, and a guard makes any future violation fail safely instead of crashing.
- **Cause (from a user crash report, macOS, 2026-09-30):** the backtrace ends in `nk_combo` → `nk_combo_begin_label` → `nk_combo_begin` → `nk_nonblock_begin` → `abort`. `nk_nonblock_begin` asserts `!(panel->type & NK_PANEL_SET_POPUP)`: Nuklear forbids opening a combo (itself a popup) inside a popup. In `src/ui/nuklear/frontend.cpp`, `render_new_project_wizard` opens `nk_popup_begin(..., "Create project", ...)` and calls `nk_combo` for the layout, and `render_bms_encoding_dialog` opens `nk_popup_begin(..., "BMS text encoding", ...)` and calls `nk_combo` for the encoding. Other combos (snap, playback rate, Setup policies, Metadata, markers, export group) are in ordinary windows. The user confirmed the trigger: after importing a song, choosing a layout in the new-project wizard (`render_new_project_wizard`); the BMS encoding dialog has the same defect and is fixed in the same card.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_new_project_wizard`, `render_bms_encoding_dialog`), `src/ui/nuklear/chrome.h`, `build/_deps/nuklear-src/nuklear.h` (`nk_nonblock_begin`).
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/chrome.h`, new `tests/ui/nuklear_popup_combo_tests.cpp`, `tests/CMakeLists.txt`; user-approved central-guard callers: `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_markers.cpp`, `src/ui/nuklear/metadata_panel.cpp`.
- **Steps:**
  1. Reproduce: open New project (and the BMS encoding prompt with an ambiguous-encoding fixture from `tests/fixtures/bms/encoding/`) in a debug build and open each combo; confirm the abort.
  2. Turn both dialogs into ordinary Nuklear windows (`nk_begin` with `NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR`, centered, focused with `nk_window_set_focus`, other input blocked the same way the popup did) so their combos are legal. Keep their actions, labels, and keyboard behavior identical.
  3. Add `chrome::safe_combo(...)`: if the current panel is a popup (`ctx->current->layout->type & NK_PANEL_SET_POPUP`), render the choices as an inline selectable list instead of opening a combo; otherwise call `nk_combo`. Use it for every `nk_combo` call site, and add `chrome::safe_combo_begin_label` for the `nk_combo_begin_label` sites.
  4. Test with the headless Nuklear context (or T-41's harness): build a popup, call `safe_combo`, click it, and assert no abort and that a selection is returned.
- **Invariants:** dialog results (layout index, encoding choice, cancel) unchanged; dialogs stay modal from the user's point of view.
- **Reject/failure behavior:** none.
- **Verify:** before-fix debug Xvfb reproduced both actual `nk_nonblock_begin` assertions (wizard layout and CP932 encoding prompt), recorded in `/tmp/wormedit-t264/wizard-abort.log` and `/tmp/wormedit-t264/encoding-abort.log`. Central guard standalone and integrated headless tests passed 4/4 (22 assertions), including real inline selection and architecture enforcement. Corrected real Xvfb/SDL-dummy windows passed wizard layout selection, Cancel/reopen and 4K project creation; encoding CP932 selection, Cancel/reopen in the same context and successful import. PNG evidence: `/tmp/wormedit-t264/wizard-final-created.png`, `/tmp/wormedit-t264/encoding-final-opened.png`. An intermediate Cancel assertion from closing the active Nuklear window was fixed by ending dialog rendering through its existing state; final lifecycle checks passed. Debug integrated build passed; full suite 936/936 passed in 33.39s (`/tmp/wormedit-t237-t264-final-ctest.log`), frontend-state prefix 38/38 passed (`/tmp/wormedit-t264-frontend-prefix.log`), and affected new code passed clang-tidy complexity checks. User approved isolated Linux substitution; macOS runtime check remains unavailable and is not claimed. Linux isolated checks replace the requested local macOS run by explicit user authorization; macOS runtime verification remains pending.
- **Out of scope:** theming (T-225).

## T-265 Fix the audio lock-order deadlock (freeze on seek/start/stop)

- **Status:** done
- **Outcome:** seeking, starting, stopping, and auditioning can never freeze the application: no SDL audio-device function is called while this code holds the SDL audio-stream lock.
- **Cause (gdb on a frozen process, 2026-09-30; full dump saved as `/tmp/wormedit-deadlock-bt.txt`):**
  - Main thread: `dispatch_transport_player_action` → `chart_preview_t::seek` (frame 2502878) → `sdl_preview_output_t::seek` (holds `SDL_LockAudioStream(stream_)`, `src/sys/sdl_preview_output.cpp:186`) → `activate_device` (line 130) → `SDL_PauseAudioStreamDevice` → `ObtainLogicalAudioDevice`, blocked on the **device** mutex.
  - SDL audio thread (`SDLAudioP31`, PipeWire): `SDL_PlaybackAudioThreadIterate` (holds the **device** lock) → `SDL_GetAudioStreamDataAdjustGain`, blocked on the **stream** mutex.
  - Classic AB/BA inversion; intermittent because it needs the audio callback to run at the same moment as the seek. All other threads were idle.
- **All sites with the same pattern (device call inside the stream lock):** `sdl_preview_output_t::start` (lock line 146 → `activate_device` line 165, failure path 169), `sdl_preview_output_t::seek` (186 → 203, 207), `sdl_preview_output_t::stop` (276 → 285, and the failure path 277), `src/sys/sdl_audition_output.cpp` start (59 → 70–72, 79, 84) and stop (126 → 127, 133).
- **Read first:** `src/sys/sdl_preview_output.cpp` (`start`, `seek`, `stop`, `activate_device`, `apply_output_mode`), `src/sys/sdl_audition_output.cpp`, SDL3 `SDL_audio.h` notes on `SDL_LockAudioStream`.
- **Files owned:** `src/sys/sdl_preview_output.cpp/.h`, `src/sys/sdl_audition_output.cpp/.h`, `tests/sys/sdl_preview_output_tests.cpp`, `tests/sys/sdl_audition_output_tests.cpp`, new `src/sys/sdl_audio_stream_lock.h`, and `tests/CMakeLists.txt` (shared RAII guard and enforced timeout scope approved by the user).
- **Steps:**
  1. Fix order everywhere: (a) `SDL_PauseAudioStreamDevice(stream_)` **without** holding the stream lock (the device is then not calling back); (b) `SDL_LockAudioStream`, reset session/output state, `SDL_ClearAudioStream`, `SDL_UnlockAudioStream`; (c) `SDL_ResumeAudioStreamDevice` without the lock. Failure paths follow the same order.
  2. Split `activate_device` into `pause_device()` and `resume_device()` that assert (in debug builds) that this thread does not hold the stream lock (track it with a thread-local flag set by a small RAII lock guard used for every `SDL_LockAudioStream` in these files).
  3. Check `apply_output_mode` (`SDL_SetAudioStreamFrequencyRatio`, `SDL_ClearAudioStream`) is only called outside device calls or under the same ordering.
  4. Stress test with the dummy audio driver: 2,000 rapid seek/start/stop/rate changes from one thread while the device thread runs; the test must finish within a timeout (a hang fails the test), and the existing "Stop is silent on the next render" test must still pass. Synchronize refill-worker stop requests with the condition-variable predicate mutex, notify and join after unlocking, and cover 64 pairs of idle and stopped-worker close/reopen transitions.
- **Invariants:** Stop is still a hard boundary (silence on the next callback); no audio from before a seek is heard after it.
- **Reject/failure behavior:** device pause/resume failures keep the existing status/message reporting; non-destructive audition rejection resumes the prior active session, and rejected seeks during WSOLA EOF drain retain and resume the valid queued tail. Failure cleanup pauses before locking and resumes only after unlocking.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^sdl-audio\.|^audio\.'` (the former filename-based filter did not select the actual SDL test namespace). The integrated application/audio build and corrected clean-cut full 945/945 suite passed in 48.37 seconds. Real dummy callbacks completed 2,000 preview transitions in 1.15 seconds and 2,000 audition transitions in 0.35 seconds; active-session rejection recovery passed in 0.10 seconds. Both stress cases have an enforced 30-second CTest timeout, and existing hard-Stop silence, source-failure, WSOLA, and EOF-drain checks pass. A subsequent literal audio-prefix run exposed the old fixed six-second keep-pitch test deadline: at 0.1× the completed source still had a valid WSOLA tail. Its finite budget now derives from two rate-scaled source runs, bounded WSOLA lookahead/callback work, and a device scheduling allowance. A separate static review eliminated a refill-worker shutdown lost-wake risk by changing its stop predicate under the wait mutex; it is covered by bounded repeated closes. The clean-index pitch check passed 2,929 assertions; the 128-close regression passed in 8.90 seconds. The literal corrected audio-prefix run passed 114/114 in 26.89 seconds, and the clean application preview-object compile/relink passed. New functions pass the cognitive-complexity check; unchanged historical test macros still warn at 44/60. The user approved isolated SDL dummy stress instead of physical/PipeWire playback; real PipeWire runtime verification remains pending. Logs: `/tmp/wormedit-t265-t248-build.log`, `/tmp/wormedit-t265-clean-final-full-ctest.log`, `/tmp/wormedit-t265-clean-final-focused-ctest.log`, `/tmp/wormedit-t265/clean-application-build.log`, `/tmp/wormedit-t265/tidy-final.log`, `/tmp/wormedit-t265/tidy-shutdown.log`, `/tmp/wormedit-t265/tidy-production-shutdown.log`.
- **Out of scope:** moving decode refill off the frame thread (T-212).
