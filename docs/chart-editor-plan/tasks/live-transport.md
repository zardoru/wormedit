# Task cards: frame time during live transport (Ctrl+Space)

[Back to the task-card index](../tasks.md).

Opportunities beyond T-243 (workspace projection), T-213 (scene reuse), T-212
(decode refill), T-240 (waveform detail), the rendering group
([rendering](rendering.md)), and resident samples (T-267/T-271). Findings from
code reading on 2026-09-30:

- `project_application_frame` (`src/sys/application_frame_projection.cpp`)
  builds a new `flattened_workspace_t` every frame; the struct
  (`src/sys/workspace_projection.h`) has 29 `std::vector`/`std::string` members,
  so every frame reallocates them.
- `register_audition_sources` (`src/sys/audio_source_registration.cpp`) calls
  `decoded_source_registry_t::register_source`, which opens a real miniaudio
  decoder (`decoder.open`, header/length probe) for **every** audio asset,
  synchronously, on the frame thread, and keeps every decoder open. A BMS with
  ~1,000 `#WAV` files means ~1,000 file opens and probes before the first frame,
  and ~1,000 open decoders afterwards. The same full reconciliation runs again
  after Save As (`reset_project_runtime_state` and the save-as path in
  `src/sys/main.cpp`).
- Keysounds are decoded lazily on first audition or playback (T-267 fixes the
  repetition; this group makes it start at load).
- Per frame, the playhead is read through `source_position()` /
  `timeline_frame()` on both the preview and audition sessions, each taking the
  session lock that the audio side also uses.
- Everything in the frame is recomputed at display rate even when only the
  playhead and scroll changed.

Each card states its expected gain so the order can follow T-246/T-211
measurements. Apply the stale-plan check (rule 9) first.

## T-272 Arena-allocate the flattened workspace in fill order

- **Status:** blocked-on T-271
- **Outcome:** each frame's `flattened_workspace_t` lives in a `frame_arena_t` (T-271) that is reset at the start of the frame: every array and string the C view points to is carved from the arena in the order the projection fills it, so steady-state playback makes **zero** heap allocations in projection and the data the frontend walks is contiguous and in draw order.
- **Why this shape:** the frontend boundary (`src/ui/frontend.h`) already consumes plain C views (pointer + count pairs), every array has a fixed capacity (`WORM_UI_*_CAPACITY`), and the view is consumed within the same frame before the next projection. Today `project_application_frame` builds a fresh `flattened_workspace_t` per frame whose 29 `std::vector`/`std::string` members reallocate every frame, and `rebind_view_pointers` then patches pointers into them.
- **Read first:** `src/sys/workspace_projection.h/.cpp` (`flattened_workspace_t`, `flatten_workspace`, `rebind_view_pointers`), `src/sys/application_frame_projection.cpp` (fill order), `src/sys/compose_view.cpp`, `src/sys/timing_view.cpp`, `src/sys/waveform_view_projection.cpp`, `include/wormedit/core/arena.h` (T-271).
- **Files owned:** `src/sys/workspace_projection.cpp/.h`, `src/sys/application_frame_projection.cpp/.h`, `src/sys/compose_view.cpp/.h`, `src/sys/timing_view.cpp/.h`, `src/sys/waveform_view_projection.cpp/.h`, `src/sys/metadata_view_projection.cpp`, `src/sys/workspace_frame_view.cpp`, `src/sys/main.cpp` (arena ownership and reset only), `src/sys/frame_timing.cpp/.h`, new `tests/sys/frame_projection_arena_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Add a debug-build per-frame heap allocation counter (global `operator new` hook, off in release) to the frame-timing overlay; record the before number.
  2. Introduce `frame_span<T>` (pointer + count + capacity) and helpers `arena_array<T>(arena, capacity)` and `arena_format(arena, fmt, ...)` for C strings; the arena is owned by the application and reset at the top of each frame.
  3. Reorder `flattened_workspace_t` members to the fill order of `project_application_frame` (workspace header → frame view → metadata → timing → compose scene → waveform rows → overlays → BMSON → status strings) and convert members one projection unit at a time from `std::vector`/`std::string` to `frame_span`/arena strings, building after each unit.
  4. Arrays whose final count is only known after filling allocate their fixed capacity up front (they are bounded) or use a two-pass count where the capacity is large; never reallocate inside the arena.
  5. Delete `rebind_view_pointers` once nothing points into growable containers.
  6. Sizing: start with 1 MiB chunks; log once when a frame needs a new chunk so the default can be tuned; after warm-up a frame must not commit new chunks.
  7. Tests: two consecutive projections of the same state report zero heap allocations on the second (counter); arena used bytes stay stable across 1,000 simulated playback frames; every view pointer lies inside the arena or static storage.
- **Invariants:** the frontend sees identical view contents; no view pointer survives past the next reset (the frontend must not keep pointers across frames — assert in debug).
- **Reject/failure behavior:** if a frame exceeds the arena's byte limit, the projection degrades that unit (truncated flag, as capacities do today) instead of failing the frame.
- **Verify:** standard build/test; overlay shows 0 allocations per frame during steady Ctrl+Space; T-211 table before/after.
- **Out of scope:** revision-cached data (T-275 keeps it in a second arena).
- **Expected gain:** removes allocator churn and page faults from every frame and improves cache locality of the data Nuklear and the field renderer walk.

## T-273 Register audio sources lazily and off the frame thread

- **Status:** ready
- **Outcome:** opening a project registers audio assets without opening any decoder on the frame thread; geometry probes run on the task scheduler in parallel (bounded), and a decoder is opened only while a source is being decoded, then closed; Save As only rebases paths in existing bindings instead of re-registering everything.
- **Read first:** `src/sys/audio_source_registration.cpp` (`register_audition_sources`, `reconcile_audition_sources`), `src/audio/source_registry.cpp` (`register_source`, `entry_t`), `include/wormedit/audio/decoder.h`, `src/sys/main.cpp` (`reset_project_runtime_state`, the save-as path that calls `reconcile_audition_sources`).
- **Files owned:** `include/wormedit/audio/source_registry.h`, `src/audio/source_registry.cpp`, `src/sys/audio_source_registration.cpp/.h`, `src/sys/main.cpp` (call sites only), `tests/core/audio_source_registry_tests.cpp`, `tests/sys/audio_source_registration_tests.cpp`.
- **Steps:**
  1. Split registration into a cheap synchronous step (asset → resolved path, state `pending`) and an asynchronous probe (open, read geometry, close) that publishes `ready` or `failed` bindings by generation.
  2. Decoders become short-lived: the decode path (block reads, T-267 preload) opens a decoder from a small bounded pool (for example 8) and closes it when done; no per-asset open decoder.
  3. Probe order: the song first, then keysounds in order of first use from the current cursor.
  4. Save As: rebase source paths in place (same files, same identities) and keep decoded data; only changed files are re-probed.
  5. Tests: opening a 1,000-asset fixture (generated in the test with tiny WAVs) returns before any probe completes; at most N decoders are open at any time; Save As does not re-probe unchanged files.
- **Invariants:** audition/preview of a not-yet-probed source waits for it (status "Loading audio…") instead of failing; source identities stay stable.
- **Reject/failure behavior:** probe failures are reported once per asset.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|audio_source'`; manual: open a large BMS; the window appears immediately.
- **Out of scope:** the resident arena itself (T-267).
- **Expected gain:** removes the load-time stall that scales with keysound count, and frees ~1,000 open decoders' memory and handles.

## T-274 Start keysound preloading as soon as the chart is parsed

- **Status:** blocked-on T-273, T-267
- **Outcome:** right after a BMS (or any chart) finishes parsing, before the first frame of the new project is drawn, the background preload starts: the song's first seconds and waveform analysis, then keysounds into the resident arena in first-use order from the cursor; playback started early uses whatever is resident and streams the rest.
- **Read first:** `src/sys/project_open.cpp`, `src/sys/main.cpp` (`reset_project_runtime_state`), `src/sys/waveform_analysis_runtime.cpp` (`waveform_job_.request`), T-267 and T-273.
- **Files owned:** `src/sys/project_open.cpp/.h`, `src/sys/audio_source_registration.cpp`, `src/sys/main.cpp` (ordering only), `src/audio/resident_samples.cpp`, `tests/sys/project_open_tests.cpp`.
- **Steps:** 1. Emit a "document ready" hook from the open path that starts probe + preload jobs immediately. 2. Priority order: song head (first 10 s) → waveform → keysounds by first use → the rest. 3. Re-prioritize when the cursor jumps far (preload near the new position first). 4. Progress in the status line ("Loading keysounds 312/1,024").
- **Invariants:** preload never blocks the UI; cancelled on project close or replacement.
- **Reject/failure behavior:** missing files are skipped and reported once.
- **Verify:** standard build/test; manual: open a large BMS and press Ctrl+Space at once; no audio dropouts after the first second.
- **Out of scope:** disk cache of decoded audio between sessions.
- **Expected gain:** moves decode cost out of playback entirely for typical charts.

## T-275 Split projection into revision-cached and per-frame parts

- **Status:** blocked-on T-243, T-272
- **Outcome:** while only the transport frame and scroll change (playback following), the frame skips everything that depends only on the document revision, selection, or settings (metadata, timing marker lists, BMSON projection, resource presenter, recipe views, status texts) and updates only playhead-dependent and scroll-dependent data.
- **Read first:** `src/sys/application_frame_projection.cpp`, `src/sys/compose_view.cpp` (`compose_scene_cache_t`, `make_editor_field_scene_reusing_window`), `src/sys/compose_waveform_overlays.cpp`, `src/sys/bmson_waveform_projection.cpp`.
- **Files owned:** `src/sys/application_frame_projection.cpp/.h`, `src/sys/compose_view.cpp/.h`, `src/sys/compose_waveform_overlays.cpp`, `src/sys/bmson_waveform_projection.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. Define a frame key (revision, chart, selection version, settings version, window size, zoom) and a scroll/playhead key. 2. Each sub-projection declares which key it depends on; revision-keyed outputs live in a second, longer-lived `frame_arena_t` reset only when its key changes (generational arenas: the per-frame arena from T-272 plus this revision arena), and the per-frame view points into it. 3. Test: with only the playhead advancing, the revision-keyed sub-projections run zero times (counters).
- **Invariants:** any edit, selection, zoom, or resize refreshes everything on the next frame.
- **Reject/failure behavior:** a missed dependency shows stale UI; tests guard each sub-projection's key.
- **Verify:** standard build/test; T-211/T-243 before/after tables.
- **Out of scope:** GPU caching (T-250).
- **Expected gain:** most of the remaining workspace phase after T-243.

## T-276 Read the playhead without locks

- **Status:** ready
- **Outcome:** the preview and audition sessions publish their current timeline frame, source position, and generation through a lock-free snapshot (seqlock or atomic struct) updated by the audio/control side; the frame thread reads it without taking the session mutex.
- **Read first:** `src/audio/preview_session.cpp` (`timeline_frame`, `source_position`), `src/audio/audition_session.cpp`, `src/sys/application_frame_projection.cpp` (`populate_source_playhead`).
- **Files owned:** `include/wormedit/audio/preview_session.h`, `src/audio/preview_session.cpp`, `include/wormedit/audio/audition_session.h`, `src/audio/audition_session.cpp`, `tests/core/audio_preview_session_tests.cpp`, `tests/core/audio_audition_session_tests.cpp`.
- **Steps:** 1. Fixed-size snapshot (frame, source frame, source id hash, generation) with a sequence counter. 2. Writers publish after each render/refill step; readers retry on odd/changed sequence. 3. Stress test with concurrent writer and reader for consistency.
- **Invariants:** stale generations are never reported as current; Stop still clears the playhead.
- **Reject/failure behavior:** a torn read retries a bounded number of times, then reports the previous value.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`.
- **Out of scope:** changing the audio callback structure (T-265 handles lock order).
- **Expected gain:** removes frame-thread/audio contention; fewer audio glitches and steadier frame times.

## T-277 Throttle non-critical updates and extrapolate the playhead to present time

- **Status:** blocked-on T-275
- **Outcome:** during playback, low-value per-frame work (status line formatting, song overview, BGM pane counters, inspector values) refreshes at 10–15 Hz instead of every frame, while the playhead and scroll use the audio clock extrapolated to the expected presentation time (frame start + measured present latency), giving smooth motion at 144 Hz.
- **Read first:** `src/sys/compose_view.cpp` (playhead following and smoothing from commit `f20c4ff`), `src/sys/application_frame_projection.cpp`, `src/sys/frame_timing.cpp` (present timing from T-246).
- **Files owned:** `src/sys/application_frame_projection.cpp`, `src/sys/compose_view.cpp`, `src/sys/workspace_frame_view.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Steps:** 1. A small scheduler of "refresh every N ms while playing" for listed sub-projections. 2. Playhead extrapolation: clock frame at last audio report + elapsed wall time × rate, clamped, corrected smoothly when the audio clock reports. 3. Test the extrapolation with a simulated jittery audio clock: on-screen position never moves backwards and error stays under one frame.
- **Invariants:** user actions (click, key) still update immediately; paused/stopped behavior unchanged.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; manual: Ctrl+Space on a 144 Hz display looks smooth.
- **Out of scope:** VSync policy changes.
- **Expected gain:** fewer CPU cycles per frame and visibly smoother scrolling.

## T-278 Set thread priorities so background work cannot starve the frame

- **Status:** ready
- **Outcome:** the preview refill thread runs at high priority, the frame thread at normal priority, and task-scheduler workers (decode, preload, analysis) at low priority with a worker count of at most `hardware_concurrency − 2`; heavy preload (T-274) no longer causes frame or audio hitches.
- **Read first:** `src/core/task_graph.cpp` (worker creation), `src/sys/sdl_preview_output.cpp` (refill thread), `docs/async-task-scheduling.md`.
- **Files owned:** `src/core/task_graph.cpp`, `include/wormedit/core/task_graph.h`, `src/sys/sdl_preview_output.cpp`, `src/sys/main.cpp` (scheduler construction), `tests/core/task_graph_tests.cpp`.
- **Steps:** 1. Optional per-scheduler priority hint applied with `SDL_SetCurrentThreadPriority` (or platform calls) at worker start. 2. Worker count clamp. 3. Test that the clamp and hint are applied (no timing assertions).
- **Invariants:** deterministic results regardless of priorities.
- **Reject/failure behavior:** if the OS rejects a priority change, continue at default priority (one warning).
- **Verify:** standard build/test; manual: start preload of a large BMS while playing; no dropouts.
- **Out of scope:** realtime scheduling classes.
- **Expected gain:** removes contention spikes during loading and analysis.
