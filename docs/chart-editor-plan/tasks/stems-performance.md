# BMSON stems, projection cost and audio budgets (2026-10-07 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-07, with the causes found while planning:

- **Zooming and BMSON stem projection during playback are expensive.** Per-frame suspects in `src/sys/bmson_waveform_projection.cpp` and `src/sys/stem_waveform_window.h`:
  - `visible_stem_channels` runs twice per frame, once for analysis in `waveform_analysis_runtime.cpp:361` and once for projection at `:309`. Each run rebuilds a measure hash map, and for every channel it scans the BPM list, searches `audio_assets` linearly, and checks every event.
  - When no lane matches by source order, `stem_source_lane` falls back to scanning every measure × BGM event × channel event, for each drawn stem on every frame.
  - `split_stem_by_lane` walks every slice of every drawn stem whenever the scroll changes, which is every frame during playback.
  - A zoom change fails `ring.zoom != context.zoom`, so every stem's ring is cleared and the full viewport is projected again for each stem, on every zoom step.
  - Each revision change calls `derive_sound_channel_slices` for every drawn stem, and each call rebuilds the chart's measure index and timing index.
- **The dragback workspace projection is slow:** prep 2 ms, flatten 5 ms, compose controls 2.5 ms on `/mnt/pukeipukei/G/bms/bms-b/BOFXVII/iberai_dragback/_i.bms`. It is a BMS chart, not BMSON, with 616 sound files, 359 `#WAV`, 122 `#BMP` and 536 BGM channel lines. [T-275](live-transport.md) (revision-cached projection) is the planned structural fix. Its blocker, T-243, is done.
- **Disappearing stem in `/home/zardoru/Documents/charts/Kick_Your_Ass/kick_your_ass2.bmson`:** the chart has only 12 stems, so the cap is not the cause. The likely cause is that `fx`, `hat` and `pc` each have one event, at measure 1, and their audio runs the whole song. `stem_reach_measures` falls back to 30 seconds when the stored asset has no `frame_count`. At 100 BPM that is about 13 measures, so after that the stems leave the window.
- **Stem playback starts only at slice points.** `preview_transport_bridge_t::begin_session` (`src/audio/preview_bridge.cpp:317`) skips every BGM event before the seek frame. Only the single music track gets a source offset (`:328`). A stem slice that is already sounding at the playhead stays silent until its next point.
- **What limits the stem count today?** The CPU-side vectors are not the problem, so a dynamic arena alone would not help. The real limits are:
  - The GPU envelope caches are a fixed `std::array<…, 66>` in `src/ui/field/field_renderer.h:141`, indexed by draw position (`2 + index` in `compose_canvas.cpp`). Pieces past index 64 fall back to plain lines. When the visible set shifts, the slots are reassigned and refill their textures.
  - `maximum_visible_stems = 192` and `maximum_stem_waveforms = 192` bound how many decodes and waveform analyses run at once.
  - The frontend contract already passes a pointer and a count, so it imposes no limit.
- **The keysound memory budget is fixed:** `resident_sample_options_t::budget_bytes` defaults to 512 MiB in `include/wormedit/audio/resident_samples.h:27`, and no option exposes it.

## T-438 Attribute frame cost on BMSON playback, zooming and the dragback chart

- **Status:** ready
- **Outcome:** this card records a table of per-phase frame times and the top 15 self-time symbols for each of the three scenarios below, with the commands that produced them. Each suspect listed above is marked as confirmed or ruled out.
- **Read first:** `src/sys/frame_timing.cpp` (`WORMEDIT_FRAME_TIMING_REPORT`, phases), `src/sys/bmson_waveform_projection.cpp`, `src/sys/stem_waveform_window.h`, [T-275](live-transport.md).
- **Files owned:** this card only. Any temporary probe lives in the scratchpad and is not committed.
- **Steps:**
  1. Build Release with symbols (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`) in a separate build directory.
  2. Scenario A: `kick_your_ass2.bmson` and a dense stem chart such as `stl_solst1`, playing at default zoom for 20 seconds under isolated Xvfb with `WORMEDIT_FRAME_TIMING_REPORT`. Attach `perf record -g` for the same window.
  3. Scenario B: the same charts paused while zoom steps continuously, scripted with xdotool Ctrl+wheel.
  4. Scenario C: `iberai_dragback/_i.bms`, both paused and playing.
  5. Record the per-phase p50 and p99 times and the top symbols on this card, and classify each suspect.
  6. **STOP** and report. The follow-up cards below are already ordered by the expected payoff, so the user may re-rank them.
- **Invariants:** no source changes.
- **Reject/failure behavior:** if a scenario cannot run in Xvfb, record why and use the event harness frame probe instead. Do not run it on the user's desktop.
- **Verify:** the table is on the card, with log paths under the scratchpad.
- **Out of scope:** fixing anything.

## T-439 Cache the stem window per revision and compute it once per frame

- **Status:** ready (runs without waiting for T-438; T-438 measures the gain)
- **Outcome:** with 181 stems, the per-frame stem selection costs O(visible stems) instead of O(channels × events). Analysis and projection share one result per frame. `stem_source_lane` and `stem_reach_measures` come from a per-revision index.
- **Read first:** `src/sys/stem_waveform_window.h`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/waveform_analysis_runtime.cpp`, `tests/sys/frame_timing_tests.cpp`.
- **Files owned:** `src/sys/application_frame_projection.cpp` (root-approved 2026-10-07 narrow frame-boundary ordering: defer stem analysis until compose projection has finalized cursor and viewport, then share that result with BMSON projection), `src/sys/stem_waveform_window.h` (and a new `.cpp` if it grows), `src/sys/bmson_waveform_projection.cpp`, `src/sys/bmson_waveform_projection.h`, `src/sys/waveform_analysis_runtime.cpp`, `src/sys/waveform_analysis_runtime.h`, `src/sys/compose_view.h` (`compose_scene_cache_t`), a new `tests/sys/stem_window_tests.cpp`, `tests/sys/frame_timing_tests.cpp` (root-approved 2026-10-07 prepared-span consumer regression using the real BMSON projection path), `tests/CMakeLists.txt`, this card.
- **Steps:**
  1. Add a `stem_index_t` built once per (document revision, chart). For each channel it holds the sorted event measure indices, the reach in measures and the source lane.
  2. `visible_stem_channels` binary-searches each channel's events, and later uses an interval list, instead of scanning them.
  3. Compute the frame's stem list once and pass it to both `wanted_stem_assets` and `drawn_stems`.
  4. Replace the `erase_if`/`none_of` reconciliation with a set lookup.
  5. Test that the results match the old function on the fixtures, including a chart with 181 stems, and add a timing assertion that is opt-in, like T-145.
- **Invariants:** the same stems are drawn, in the same order, as before.
- **Reject/failure behavior:** an index built for a stale revision is never used.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'stem|frame_timing'`; `clang-tidy -p build src/sys/bmson_waveform_projection.cpp`.
- **Out of scope:** the zoom path (T-440) and the reach fix (T-441).

## T-440 Keep stem waveforms responsive while zooming

- **Status:** blocked-on T-438
- **Outcome:** during a zoom gesture, stems still draw, and frame p99 stays within the T-438 paused baseline plus 2 ms. Stems reproject only after the zoom settles (no zoom change for one frame) or within a per-frame row budget.
- **Read first:** `src/sys/bmson_waveform_projection.cpp` (`populate_channel_ring`, `project_stem`), `src/audio/channel_waveform.cpp` (`project_channel_waveform`), T-438's findings.
- **Files owned:** `src/sys/bmson_waveform_projection.cpp`, `src/sys/bmson_waveform_projection.h`, `tests/sys/` projection tests, this card.
- **Steps:**
  1. Give each frame a shared projection budget of rows across all stems. The selected stem goes first, then stems in viewport order. A stem left over the budget keeps its previous rows, rescaled in y by `new_zoom / old_zoom` around the scroll anchor.
  2. After the zoom settles, every stem reprojects exactly.
  3. Test that the settled rows match exact projection, and that a zoom step never projects more than the budget.
- **Invariants:** paused, settled rows are pixel-identical to today.
- **Reject/failure behavior:** a stem with no previous rows shows the pending placeholder, not a blank lane.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bmson|stem'`; repeat T-438 scenario B and record the new p99.
- **Out of scope:** the GPU slot pool (T-443).

## T-441 Keep long single-event stems in the window for their whole length

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** in `kick_your_ass2.bmson`, the `fx`, `hat` and `pc` waveforms stay visible from measure 1 to the end of their audio, at any cursor position.
- **Read first:** `src/sys/stem_waveform_window.h` (`stem_reach_measures`), `src/sys/audio_source_registration.cpp`, `include/wormedit/audio/source_registry.h` (`decoded_source_binding_t`).
- **Files owned:** `src/sys/stem_waveform_window.h`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/waveform_analysis_runtime.cpp`, `tests/sys/stem_window_tests.cpp`, `tests/CMakeLists.txt` (approved extension: register the new stem window regression source), `src/sys/waveform_analysis_runtime.h` (approved extension: pass the already cached timing index to the shared visibility helper, avoiding a full timing rebuild each frame).
- **Steps:**
  1. Confirm the cause: log `asset.stream.frame_count` for the 12 stems after load. If they are set, find the real cause before going on, and record it here. **Observed:** all 12 Kick Your Ass assets had `sample_rate=0` and no stored `frame_count` after open; their paths resolved to Ogg files. The 30-second unknown-length fallback therefore underestimates these stems. Root's read-only loader probe: `/tmp/wormedit-oct7-root/kick-registry.log` confirms all 12 sources registered with decoded frame counts; ffprobe found 6,879,876 frames at 44,100 Hz for `fx.ogg`, and 6,880,151 frames at 44,100 Hz for `hat.ogg`/`pc.ogg`.
  2. Take the reach from the registered binding's `source_frame_count` when the stored stream lacks it. While neither is known, treat the stem as reaching the end of the chart, not 30 seconds.
  3. Compute the reach from the chart's timing, so that the time from the stem's last event to the end of its audio maps to rows, instead of using the fastest BPM.
  4. Add a regression fixture: a 12-channel BMSON with single-event, full-length stems, with the cursor at measures 1, 20 and 40. **Observed:** the read-only production registry probe registered all 12 original Kick assets with exact decoded frame counts (6.88M–6.90M) and the production visibility helper retained all 12 at measures 1, 20 and 40; see `/tmp/wormedit-oct7-root/kick-window-probe.log`.
- **Invariants:** stems whose audio really ends still leave the window.
- **Reject/failure behavior:** an asset that fails to decode keeps its placeholder band and does not vanish.
- **Verify:** `ctest --test-dir build --output-on-failure -R stem`; an isolated Xvfb capture of Kick Your Ass at measure 30 shows the `fx` lane waveform.
- **Out of scope:** the stem cap.

- **Integrated evidence:** main application build and all 2,434 tests pass; `/tmp/wormedit-oct7-root/audio-integrated-tests.log`. The integrated production registry probe retains all 12 original Kick stems at measures 1, 20 and 40 (`kick-window-integrated.log`). The isolated GUI capture remains pending under the recorded Xvfb restriction.

## T-442 Start stem slices mid-sample at the playhead

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** starting or seeking playback inside a sounding BMSON slice plays that slice from the matching source frame at once. A probe shows non-silent PCM from the stem within 10 ms of the start.
- **Read first:** `src/audio/preview_bridge.cpp` (`begin_session`, `seek_preview_queue`), `src/core/timing.cpp` (`build_preview_schedule`), `include/wormedit/core/sound_slice.h`, `src/audio/audition_resolver.cpp`.
- **Files owned:** `src/audio/preview_bridge.cpp`, `include/wormedit/audio/preview_bridge.h`, `tests/core/audio_preview_bridge_tests.cpp`, `tests/sys/sdl_preview_output_tests.cpp` (approved extension: verify non-silent dummy SDL PCM for a seek inside a sounding slice), `tests/core/audio_audition_resolver_tests.cpp` (approved extension: verify a nonzero native clip offset and continuation boundaries normalize correctly when source rate differs from the fixed preview output rate), `tests/core/audio_preview_session_tests.cpp` (approved extension: assert resumed seek voice stop counts and post-stop silence), this card.
- **Steps:**
  1. Reuse each resolved preview voice's normalized source start and frame count as the authoritative slice bounds; do not duplicate source-range metadata in the timeline schedule because its rate may differ from the decoder's source-frame rate.
  2. In `begin_session`, for BGM events before `seek_frame` whose resolved slice is still sounding at `seek_frame`, start them with `source_start_frame += seek_frame - sample_frame`. Restart and continuation points follow the derived slice voices from the resolver.
  3. Plain BMS keysounds follow the same rule, gated on the sample still sounding, so a long BMS BGM sample resumes too.
  4. Test with one-slice and three-slice stems and a nonzero clip begin: seek at a slice start, in the middle, exactly on an event, and just past its end. Verify source-rate conversion remains correct when the engine resamples.
- **Invariants:** a seek exactly on an event behaves as today. Loop-region starts use the same path.
- **Reject/failure behavior:** a slice whose audio is not resident plays from its next point, as it does today, and logs no error.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'audio_preview|^audio\.'`; an isolated SDL dummy-audio PCM check at a mid-slice seek.
- **Out of scope:** audition clicks on single notes.

- **Integrated evidence:** main application build and all 2,434 tests pass; `/tmp/wormedit-oct7-root/audio-integrated-tests.log`. The new SDL dummy mid-slice seek case passes with non-silent PCM in the first 10 ms; configured SDL test complexity has no new violations (`sdl-pcm-tidy.log`).

## T-443 Replace fixed stem slots with a budgeted pool keyed by channel

- **Status:** blocked-on T-439
- **Outcome:** the stem caps (192, 66 GPU slots, and 64 drawn as textures) are gone. Envelope textures are keyed by channel ID and evicted least recently used under a byte budget. Every stem in the viewport draws as a texture, and scrolling never refills the texture of a stem that stayed visible.
- **Read first:** `src/ui/field/field_renderer.h` (`envelopes_`), `src/ui/field/field_renderer.cpp`, `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_stem_piece`), `src/sys/waveform_analysis_runtime.h` (`maximum_stem_waveforms`), `src/core/arena.cpp`.
- **Files owned:** `src/ui/field/field_renderer.h`, `src/ui/field/field_renderer.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/frontend.h` (a stable `stem_key` on `WormUiComposeStemWaveformView`), `src/sys/bmson_waveform_projection.cpp`, `src/sys/stem_waveform_window.h`, `src/sys/waveform_analysis_runtime.h`, `src/sys/waveform_analysis_runtime.cpp`, field renderer tests, this card.
- **Steps:**
  1. Project a stable key (the channel ID) per stem piece.
  2. Replace `std::array<…, 66>` with a pool of channel ID → envelope cache. Texture bytes are budgeted (64 MiB by default) and the least recently drawn caches are evicted.
  3. Bound waveform analyses by memory instead of count, and evict the stems least recently in the viewport.
  4. Remove `maximum_visible_stems` and the `index < 64` fallback.
  5. Test that a slot keeps its texture when the visible set shifts, that eviction is LRU, and that a chart with 300 stems draws without line fallbacks.
- **Invariants:** the song waveform and the selected-stem slots keep their reserved slots.
- **Reject/failure behavior:** when the budget is exhausted, the oldest offscreen caches are evicted first. A visible stem falls back to lines only if the visible set alone exceeds the budget, and that event is counted.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'field|stem'`; an isolated capture of `stl_solst1` at a measure with more than 64 sounding stems.
- **Out of scope:** CPU projection cost (T-439, T-440).

## T-444 Bring the dragback workspace projection under 3 ms

- **Status:** blocked-on T-438
- **Outcome:** with `iberai_dragback/_i.bms` open, prep, flatten and compose controls together take under 3 ms at p50 when paused, measured as in T-438.
- **Read first:** T-438's findings, [T-275](live-transport.md), `src/sys/application_frame_projection.cpp`, `src/sys/compose_view.cpp`.
- **Files owned:** decided after T-438. The coordinator records them here before any edit. If T-438 points at the revision split, this card becomes "do T-275".
- **Steps:**
  1. From T-438's table, pick the top two costs.
  2. Move each one to revision-keyed caching or to viewport-bounded iteration.
  3. Measure again and record the result.
- **Invariants:** the projected view is byte-identical, checked by an existing projection equality test or a new one.
- **Reject/failure behavior:** a cache built for a stale revision is never shown.
- **Verify:** T-438 scenario C before and after; `ctest --test-dir build --output-on-failure`.
- **Out of scope:** GPU work.

## T-445 Let Options raise the keysound memory budget

- **Status:** done
- **Outcome:** Options › Audio has a "Keysound memory" field (256 MiB–8 GiB, default 512 MiB) persisted in `preferences.json`. Raising it takes effect for the open project without reopening it. The over-budget notification names the setting.
- **Read first:** `include/wormedit/audio/resident_samples.h`, `src/audio/resident_samples.cpp`, `include/wormedit/core/preferences.h` (`audio_preferences_t`), `src/ui/nuklear/options_window.cpp`, `src/sys/audio_source_registration.cpp`.
- **Files owned:** `include/wormedit/core/arena.h`, `src/core/arena.cpp` (root-approved grow-only hard ceiling), `src/sys/main.cpp` (root-approved loaded/live budget application), `src/sys/audio_source_registration.h` (root-approved retry boundary), `tests/core/arena_tests.cpp`, `tests/sys/audio_source_registration_tests.cpp` (root-approved active retry/snapshot retention), `tests/ui/nuklear_event_harness_tests.cpp` (root-approved Options Audio field/action), `tests/ui/nuklear_options_state_tests.cpp` (root-approved production Options apply/clamping regression using its existing runtime fixture), those five, plus `src/core/preferences.cpp`, `src/sys/options_runtime.cpp`, `src/ui/frontend.h` (option field), preference and resident-sample tests, this card.
- **Steps:**
  1. Add `keysound_budget_mib` to `audio_preferences_t`, with load clamping and defaulting.
  2. Make the budget mutable: raising it lets the arena accept more. Lowering it applies on the next load and never evicts samples that are playing.
  3. Retry the loads that were skipped as over budget after a raise.
  4. Add the Options field, and add "raise Keysound memory in Options" to the over-budget notification.
- **Invariants:** existing preference files load unchanged.
- **Reject/failure behavior:** values outside the range are clamped, and the field shows the clamped value.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'preferences|resident|budget|Options Audio'`; isolated actual-loader checks cover completed and active-pass raises, snapshot retention, latest-project retries and retry submission after scheduler capacity returns, plus the real Audio field/action. The isolated normal dependency build and 48 focused tests passed; configured clang-tidy has no new function over 25. Workspace evidence: `wormedit-scratch-builds/root-oct7/keysound-live-worker-tests-final.log` and `keysound-live-tests-tidy-final.log`.
- **Out of scope:** stem waveform memory (T-443).
