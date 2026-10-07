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

- **Status:** done (2026-10-07; findings below; profiled on the user's live Debug build with `perf record -e cpu-clock:u --call-graph dwarf`, not Xvfb, at the user's request)
- **Findings (Debug build, so absolute times are inflated about 3–5×; the call shapes match Release):**
  - **Dragback `_i.bms`, paused and playing:** workspace projection averages 12.6 ms (p99 19.3 ms): preparation 2.0, actions 1.2, flatten 5.0, frame view 1.5, compose controls 2.4. Main-thread shares:
    - Sounds dock list, about 30%: `project_keysound_list` natural-sorts every sound on every frame (`sort_keysound_sections` → `natural_name_less`, 17%), and `collect_keysound_resources` → `is_placeable_keysound` finds each audio asset linearly (10%).
    - Compose timing markers, about 14%: `append_compose_chart_timing_markers` (`compose_view.cpp:1979`) formats the label strings for every timing event before culling, and `absolute_row` searches measures linearly.
    - BMS branch outline, about 10%: `make_workspace_view` → `make_branch_outline` (`workspace.cpp:754`) rebuilds the tree every frame, even for a chart with no branch statements.
  - **BMSON playback while zooming and skipping:** the main thread accounts for 69% of samples, the SDL audio thread 25%.
    - Stem projection, 31% of the main thread: `populate_channel_ring` → `project_channel_waveform` → `channel_pixel_for_column` (`src/audio/channel_waveform.cpp:129`). It runs an exact-rational `musical_position_at` for every *audio waveform column*, not every pixel, so zooming out costs the most. Self time is `std::gcd` (10%), and the call repeats once per stem. Zooming invalidates every ring; skipping moves the window beyond the ring.
    - Field renderer, 13%: `renderer_t::stem_envelope` → `collect_envelope_rows` refills each stem's texture after every zoom step.
    - Seeking, 3.7%: `preview_session_t::seek` → `pump_preview_lookahead` → `decode_block` decodes synchronously on the main thread on every skip.
    - Audio thread: almost all of it is WSOLA `time_stretch_t::correlation` (Keep pitch at a reduced rate). In Debug, the bounds-checked `vector::operator[]` dominates.
  - The earlier per-frame stem-selection suspects are ruled out, because T-439 already fixed them.
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

- **Status:** done (2026-10-07; root integrated and verified)
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
- **Root integrated evidence:** normal main application build and all 2,495/2,495 tests pass (`next-oct7/root/final-main-ctest-verified.log`). The final merged production canvas probe renders 300 textured stems and retains all textures after reorder/selected-sound changes without regeneration or fallback (`final-canvas-stems-probe.log`). Both completed worker trees and generated builds were removed; branches and logs remain.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'stem|frame_timing'`; `clang-tidy -p build src/sys/bmson_waveform_projection.cpp`.
- **Out of scope:** the zoom path (T-440) and the reach fix (T-441).

- **Integrated evidence:** main `c03f1ac`, normal application build and 2,453/2,453 tests pass. The opt-in 181-stem benchmark passes 5,826 assertions: indexed 0.686818 ms versus reference 128.896 ms (max of five, 32 windows). Production Kick retains all 12 registered stems at measures 1/20/40 in identical order. Workspace root evidence: `wormedit-scratch-builds/root-oct7/stem-budget-backend-main-ctest.log`, `stem-main-timing.log`, `stem-real-main-probe.log`. This query benchmark does not replace T-438 frame profiling.

## T-440 Project stem waveforms per pixel row through one shared row-to-time table

- **Status:** done in code (2026-10-07; normal Release application build and automated gates passed; live playback capture/main-thread-share gate pending)
- **Outcome:** stem projection costs O(viewport rows × visible stems) with cheap lookups, independent of zoom level and audio length. During playback with zooming and skipping on `kick_your_ass2.bmson` and `stl_solst1`, stem projection takes under 10% of main-thread samples, down from 31% in T-438. Settled rows match today's projection, within one pixel of placement.
- **Read first:** T-438's findings, `src/audio/channel_waveform.cpp` (`channel_pixel_for_column`, `rasterize_channel_columns`), `src/sys/bmson_waveform_projection.cpp` (`populate_channel_ring`, `project_stem`), `include/wormedit/core/timing.h` (`musical_position_at`), `src/audio/aligned_waveform.cpp`.
- **Files owned:** `src/audio/channel_waveform.cpp`, `include/wormedit/audio/channel_waveform.h`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/bmson_waveform_projection.h`, `src/sys/compose_view.h` (scene cache field for the row table), `tests/core/audio_aligned_waveform_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `src/audio/aligned_waveform.cpp`, `include/wormedit/audio/aligned_waveform.h`, `src/sys/waveform_view_projection.cpp`, this T-440 section only. Supporting aligned-waveform and song-projection paths are root-approved to share the row table across stems, the selected channel and the song envelope; exact sample/editing paths remain unchanged.
- **Ownership:** Sol implemented and reviewed table/production wiring and owned all builds. Luna implemented only appended core regressions under a narrow test-only brief, then released the file and agent slot; Sol reviewed and corrected fixtures and ran all gates. Source ownership is released with the reviewed commit.
- **Steps:**
  1. Build one row-to-time table per frame, keyed by (timing revision, scroll, zoom, height). Each viewport row gets its chart-time span in seconds, as a double, computed with forward timing (row → time). It is shared by every stem, the selected channel and the song waveform.
  2. Project each stem by walking its slices against the table. For each row, map its time span into the slice's source frames and read the peak and RMS from the waveform pyramid at the matching level. No per-column inverse timing, and no exact rationals on the display path.
  3. Ring reuse: a scroll reuses the rows already projected, as today. A zoom rebuilds only the table, and each stem re-reads its rows from the pyramid.
  4. Keep the exact path for anything that edits audio (slice points, razor, paste); only the display changes.
  5. Test that, for fixtures with tempo changes, stops and continuation slices, every row's peaks match the reference within one row of placement, and that the work does not depend on zoom (opt-in benchmark as in T-145).
- **Invariants:** slice boundaries, restart and continue behavior and cut-line rows are unchanged.
- **Reject/failure behavior:** a timing index that cannot map a row leaves that row empty, as an out-of-range column is today.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'channel_waveform|bmson|stem'`; repeat the T-438 BMSON capture and record the new share.
- **Implementation:** one padded world-pixel table shares converted forward timing across song, selected channel and linked stems. Cache identity includes timing backing storage/fingerprint, project/chart/revision, profile, rounding metadata, scroll/zoom/height. Scroll reuses old rows; timing replacement retires rings and native slice derivation. Counters describe the last changed generation/operation and remain unchanged on identical cache hits. Exact sample-point/editing/reference overloads remain unchanged.
- **Sampling policy:** each pixel time interval intersects the exact derived source slice; a before-event DELAY belongs to the previous voice and an after-event STOP to the new voice. Both pixel endpoints use before-delay times, assigning a pause on a pixel boundary to the following pixel without duplication. Half-pixel pyramid levels supply max peaks and max bin RMS; partial edge bins descend to base level to avoid importing peaks outside a clip. RMS is the maximum of matching-level bin RMS, replacing the old maximum of weighted audio-column RMS; the numeric pyramid regression fixes this policy explicitly. Complete analyzed dyadic pyramids visit at most 136 bins per active slice/row (8 plus two per bounded depth); uncommon base-only/partial pyramids retain exact available-bin aggregation without the bounded-bin claim.
- **Validation:** fresh normal Ninja RelWithDebInfo application/test build passed. The card filter plus waveform, uppercase Stem and documentation cases passed 110/110; the opt-in performance case passed 8 assertions. Configured cognitive-complexity threshold 25 passed for every new helper/test; all 15 existing findings across the six changed translation units match the measured HEAD baseline without increase. Tests cover full timing identity, repeated prepare, one-row scroll reuse, tempo/native-rate clip/warp, restart/continuation, delay-before-event and stop-after-event impulses, invalid timing/slices/requests, silent EOF, shared song/channel preparation, timing replacement and peak/RMS mathematics. A fixture warp through distinct markers was corrected to a valid between-marker warp; the original rejected geometry remains an explicit invalid-slices test.
- **Release evidence:** same-binary production-loaded, same-waveform old/reference versus new projection, 600-pixel viewports at zoom 32/192/1536 and scroll 0/500/1200. Fresh Kick has 9 linked stems: low-zoom complete projection 137.77–192.60 ms before versus 1.17–1.18 ms after. Its production BMSON phase warm cost is 0.0037–0.0042 ms; scrolling one pixel projects 9 rows, reuses 5,409 ring rows, and builds one shared table row. The real dense `stl_solst1/001.bmson` fresh import is packed; the detached stress chart explicitly links its 181 retained channels to reproduce legacy schema-4 stem intent. That constructed scenario takes 331.31–469.22 ms before versus 0.58–1.32 ms after at zoom 32; BMSON phase cold 2.75–7.21 ms/warm 0.122–0.137 ms, with 181 new/108,781 reused ring rows on one-pixel scroll. These phase measurements are not whole-frame timings. Input native serialization remains unchanged in both probes.
- **Placement/scaling evidence:** serialized first-source rows pass bidirectional zero-amplitude-tolerance peak containment within one pixel (Kick 7,280 row comparisons, dense 1,078). The opt-in complete-pyramid benchmark quadruples source frames 262,144→1,048,576 with identical work: each 600-row case visits 600 slices; zoom 480/3,840 samples 1,030/653 bins respectively. Four reference cases total 13.270 ms versus 0.099 ms display time. Final refined measurements supersede the initial coarse-edge measurements. Logs and immutable snapshots reside under `/home/zardoru/devel/wormedit-scratch-builds/perf-oct7/stems`: `tests-final-reviewed.log`, `benchmark-final.log`, `kick-final.log/json`, `dense-final.log/json`, corresponding `*-final-placement.log`, `tidy-final-reviewed.log`, `tidy-gates-fixed.log`, and `tidy-cache-counter-fixed.log`.
- **Pending live gate:** no permitted desktop/window capture was available; headless production loading/projection validates code and placement, but does not establish the requested under-10% main-thread sample share during live zoom/skip playback. Repeat T-438's user-session capture to close that gate.
- **Out of scope:** GPU refills (T-469).

- **Root integrated evidence:** main `69abfa4` with T-444 `b5fb2b7`, normal application build and all **2,544/2,544** tests pass. Independent merged-main real Kick probe retains unchanged reference output and passes 14,560 bidirectional zero-tolerance peak comparisons within one pixel over nine windows. Original chart hashes are unchanged. Evidence: `wormedit-scratch-builds/perf-oct7/root`; both completed workers and about 4.5 GB of generated builds removed, branches/logs retained. Live sample-share gate remains pending.

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

- **Status:** done in code (2026-10-07; root integrated and automated verification passed; original-chart GUI capture pending)
- **Outcome:** the stem caps (192, 66 GPU slots, and 64 drawn as textures) are gone. Envelope textures are keyed by channel ID and evicted least recently used under a byte budget. Every stem in a fitting viewport draws as a texture; visible pressure is counted and falls back to lines. Scrolling retains textures for stems that stayed visible.
- **Read first:** `src/ui/field/field_renderer.h` (`envelopes_`), `src/ui/field/field_renderer.cpp`, `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_stem_piece`), `src/sys/waveform_analysis_runtime.h` (`stem_waveform_cache_stats`), `src/core/arena.cpp`.
- **Files owned:** `src/ui/field/field_renderer.h`, `src/ui/field/field_renderer.cpp`, `src/ui/nuklear/compose_canvas.cpp` (root-approved exclusive stem/waveform envelope drawing and full-frame key pinning; BGA retains lane/event/ghost regions), `src/ui/frontend.h` (root-approved exclusive edit of `WormUiComposeStemWaveformView` for `stem_key`; BGA retains other regions until transfer), `src/sys/bmson_waveform_projection.cpp`, `src/sys/stem_waveform_window.h`, `src/sys/waveform_analysis_runtime.h`, `src/sys/waveform_analysis_runtime.cpp`, `tests/ui/field_renderer_tests.cpp` (exact existing field renderer tests), `tests/sys/stem_window_tests.cpp` (root-approved uncapped 300-channel window selection), `tests/sys/frame_timing_tests.cpp` (root-approved production stable-key/300-stem projection), `tests/sys/audio_notification_tests.cpp` (root-approved production analysis memory admission/LRU retention), this card.
- **Steps:**
  1. Project a stable key (the channel ID) per stem piece.
  2. Replace `std::array<…, 66>` with a pool of channel ID → envelope cache. Texture bytes are budgeted (64 MiB by default) and the least recently drawn caches are evicted.
  3. Bound waveform analyses by memory instead of count, and evict the stems least recently in the viewport.
  4. Remove `maximum_visible_stems` and the `index < 64` fallback.
  5. Test that a slot keeps its texture when the visible set shifts, that eviction is LRU, and that a chart with 300 stems draws without line fallbacks.
- **Invariants:** the song waveform and the selected-stem slots keep their reserved slots.
- **Reject/failure behavior:** when the budget is exhausted, the oldest offscreen caches are evicted first. A visible stem falls back to lines when admission or transient replacement reservation cannot fit the byte budget, and that refusal is counted; existing reserved textures and queued draw batches remain protected.
- **Evidence (isolated worker, 2026-10-07):** normal dependency-tracked core/frame/field target builds and production canvas object compile pass. Focused `field|stem|audio-notification|documentation` CTest passes 102/102. Actual SDL software renderer passes seven pool cases (1,329 assertions), including 300 textured stems, retained texture after reorder, offscreen LRU, visible fallback accounting, transient replacement reservation, reserved priority and selected overlay pixels. Production projection/runtime checks pass six cases (1,867 assertions); the uncapped window check passes all 300 channels in indexed and compatibility paths. Configured clang-tidy adds no violations and preserves existing scores. Analysis ownership charges result/key/vector capacities and entry bookkeeping under a 64 MiB default; the separately bounded backing cache and external shared snapshots are reported separately. Original-chart isolated capture remains pending because Xvfb is blocked in this sandbox.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'field|stem'`; an isolated capture of `stl_solst1` at a measure with more than 64 sounding stems.
- **Out of scope:** CPU projection cost (T-439, T-440).

## T-444 Bring the dragback workspace projection under 3 ms

- **Status:** done in code (Release production frame probe passes; live GUI sample-share capture pending)
- **Outcome:** with `iberai_dragback/_i.bms` open, prep, flatten and compose controls together take under 3 ms at p50 when paused, in a Release build. A repeat of the T-438 capture shows the Sounds dock list, timing markers and branch outline each under 1% of main-thread samples.
- **Read first:** T-438's findings, `src/sys/sound_list_projection.h` (`keysound_list_request_t`), `src/sys/workspace_projection.cpp` (`populate_compose_keysounds`), `src/sys/compose_view.cpp` (`append_compose_chart_timing_markers`), `src/core/workspace.cpp` (`make_branch_outline`).
- **Files owned:** `src/sys/sound_list_projection.cpp`, `src/sys/sound_list_projection.h`, `src/sys/workspace_projection.cpp`, `src/sys/compose_view.cpp`, `src/sys/compose_view.h` (scene cache fields), `src/core/workspace.cpp`, `include/wormedit/core/workspace.h`, `src/sys/compare_view.cpp` (`build_workspace_frame_model`), `src/sys/workspace_projection.h` (optional list-cache argument), `src/sys/application_frame_projection.cpp` (pass the production list cache), `src/sys/compare_view.h` (branch cache), `tests/sys/sound_list_projection_tests.cpp`, `tests/core/workspace_tests.cpp`, `tests/sys/frame_timing_tests.cpp` (append-only dedicated list/timing-cache cases and helpers), this card.
- **Supporting ownership:** root approved the exact additional header, wiring and test paths above. T-444 changes only list/timing cache fields and helpers in shared `compose_view.h`; T-440 owns its row table fields/includes and existing waveform/projection test fixtures. Luna exclusively implements the two sound-list projection files and their existing test file after the Release baseline is captured; this coordinator owns other production paths, tests and all build writes. Root also approved a dedicated document/chart source-identity guard on scene-index rebuilding so reused IDs/revisions cannot leave stale measure mapping for timing markers. Verification uses optimized same-binary uncached reference versus cached production paths on the original CP932 dragback input, with a narrow internal timing-cache bypass; this is not an untouched historical Release baseline. Ordinary retained BMS statements remain visible; the empty-outline fast path applies only to a genuinely empty retained tree.
- **Steps:**
  1. Sounds dock: cache the sorted and filtered list per (document revision, chart, sort, filter request). Give `is_placeable_keysound` an asset-ID map built once per revision.
  2. Timing markers: cache (row, short label, detail) per (revision, chart). Each frame then only culls to the viewport and projects y, and selection flags stay per frame.
  3. Branch outline: cache per (revision, chart), and skip it entirely when the chart has no branch statements.
  4. For each cache, test that the cached output equals the uncached output on the dragback fixture shape (600 sounds, 70 BPM events), and that a revision bump invalidates it.
  5. Repeat the T-438 dragback capture in a Release build and record the times.
- **Invariants:** the projected view is byte-identical.
- **Reject/failure behavior:** a cache built for a stale revision is never shown.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'sound_list|compose|workspace'`; the capture on this card.
- **Worker verification:** normal isolated Release core/frame/converter and application-wiring objects build successfully. Required card filter passes 27/27 tests; focused cache gates pass 5 list cases, 26 assertions in 3 timing cases, and 3 branch cases with deep reference equality. Configured clang-tidy compares unchanged baseline warning sets; every new function/test remains at or below 25. Logs are under `wormedit-scratch-builds/perf-oct7/workspace`.
- **Release frame evidence:** the same optimized binary reads the original CP932 dragback chart and runs production prep, flatten and Compose projection for 60 frames (10 warm-up), with three scroll snapshots. Observed combined p50 is 1.006070 ms through uncached reference paths and 0.296084 ms with caches; p99 is 1.762030 versus 0.753129 ms. The uncached Release reference was already below 3 ms. All three cached snapshots exactly equal both the same-binary reference and the original Debug snapshots, including 616 keysounds and 96 retained sidebar statement rows. List, asset index, timing payload and branch outline each build once. This measures production projection phases, not GUI sample percentages; the live <1% share capture remains pending under the established Xvfb restriction.
- **Out of scope:** GPU work, BMSON stems (T-440).

- **Root integrated evidence:** main `b5fb2b7` with T-440 `69abfa4`, normal application build and all **2,544/2,544** tests pass. Independent merged-main production Dragback snapshots at three scroll windows are byte-identical to the original Debug baseline. Evidence: `wormedit-scratch-builds/perf-oct7/root`; both completed workers and about 4.5 GB of generated builds removed, branches/logs retained. Live sample-share gate remains pending.

## T-445 Let Options raise the keysound memory budget

- **Status:** done (2026-10-07; root integrated and verified)
- **Outcome:** Options › Audio has a "Keysound memory" field (256 MiB–8 GiB, default 512 MiB) persisted in `preferences.json`. Raising it takes effect for the open project without reopening it. The over-budget notification names the setting.
- **Read first:** `include/wormedit/audio/resident_samples.h`, `src/audio/resident_samples.cpp`, `include/wormedit/core/preferences.h` (`audio_preferences_t`), `src/ui/nuklear/options_window.cpp`, `src/sys/audio_source_registration.cpp`.
- **Files owned:** `tests/sys/audio_notification_tests.cpp` (root-approved final integration correction: existing exact fallback-message expectation now includes the new Options guidance), `include/wormedit/core/arena.h`, `src/core/arena.cpp` (root-approved grow-only hard ceiling), `src/sys/main.cpp` (root-approved loaded/live budget application), `src/sys/audio_source_registration.h` (root-approved retry boundary), `tests/core/arena_tests.cpp`, `tests/sys/audio_source_registration_tests.cpp` (root-approved active retry/snapshot retention), `tests/ui/nuklear_event_harness_tests.cpp` (root-approved Options Audio field/action), `tests/ui/nuklear_options_state_tests.cpp` (root-approved production Options apply/clamping regression using its existing runtime fixture), those five, plus `src/core/preferences.cpp`, `src/sys/options_runtime.cpp`, `src/ui/frontend.h` (option field), preference and resident-sample tests, this card.
- **Steps:**
  1. Add `keysound_budget_mib` to `audio_preferences_t`, with load clamping and defaulting.
  2. Make the budget mutable: raising it lets the arena accept more. Lowering it applies on the next load and never evicts samples that are playing.
  3. Retry the loads that were skipped as over budget after a raise.
  4. Add the Options field, and add "raise Keysound memory in Options" to the over-budget notification.
- **Invariants:** existing preference files load unchanged.
- **Reject/failure behavior:** values outside the range are clamped, and the field shows the clamped value.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'preferences|resident|budget|Options Audio'`; isolated actual-loader checks cover completed and active-pass raises, snapshot retention, latest-project retries and retry submission after scheduler capacity returns, plus the real Audio field/action. The isolated normal dependency build and 48 focused tests passed; configured clang-tidy has no new function over 25. Workspace evidence: `wormedit-scratch-builds/root-oct7/keysound-live-worker-tests-final.log` and `keysound-live-tests-tidy-final.log`.
- **Out of scope:** stem waveform memory (T-443).

- **T-445 integrated evidence:** main backend `9274bdc`, UI/runtime `853344b`, normal application build and all 2,461/2,461 CTest cases pass. Root corrected the existing exact notification-message expectation to include the required Options guidance. Workspace logs: `wormedit-scratch-builds/root-oct7/keysound-main-build-final.log` and `keysound-main-ctest-final.log`.

## T-469 Stop refilling every stem texture on each zoom step

- **Status:** done in code (2026-10-07); live main-thread share gate pending under the existing no-GUI restriction.
- **Outcome:** during a continuous zoom on `stl_solst1`, the field renderer's stem envelope work (`stem_envelope` → `collect_envelope_rows`) takes under 3% of main-thread samples, down from 13%. Stems keep drawing on every frame.
- **Read first:** T-438's findings, `src/ui/field/field_renderer.cpp` (`envelope_for_key`, `collect_envelope_rows`), `src/ui/field/field_renderer.h`, `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_envelope_texture`).
- **Files owned:** `src/ui/field/field_renderer.cpp`, `src/ui/field/field_renderer.h`, `src/ui/nuklear/compose_canvas.cpp`, `tests/ui/field_renderer_tests.cpp`, this T-469 section only. Sol owns measurement, renderer/canvas implementation, review and builds. Luna completed exclusive appended benchmark and regression cases, then released the test file and shared executor slot; Sol reviewed the actual diff and ran all gates.
- **Steps:**
  1. Measure after T-440. If the share is already under 3%, set this card to done with the numbers.
  2. Otherwise, while the zoom changes, draw the retained texture scaled in y, and refill at most a budgeted number of stems per frame, in viewport order. Refill all of them exactly once the zoom settles.
  3. Test the per-frame refill budget and that settled pixels match.
- **Invariants:** paused, settled pixels are identical to today.
- **Reject/failure behavior:** a stem without a texture draws its line fallback, which is counted, as in T-443.
- **Verify:** `ctest --test-dir build --output-on-failure -R field`; repeat the T-438 BMSON capture.
- **Out of scope:** CPU projection (T-440).

- **Stale-plan/measurement decision:** T-440 was integrated, but the production canvas still converted and fingerprinted all stem rows before every zoom refill. An isolated optimized, app-capable normal build measured the software-SDL production canvas after T-440: 300 cached synthetic stems, 120 zoom frames plus first settlement, total p50/p99/max **40.4045/45.4958/61.436 ms**; collection p50 13.6437 ms, submit p50 26.7514 ms, 4,679,700 regenerated rows. This justified the refill budget. This is a constructed dense cached-stem stress case, not fresh `stl_solst1` import behavior (that chart imports packed channels).
- **Implementation:** changing zoom refills at most eight stems per frame through a rotating window in left-to-right viewport order. Compatible retained textures use the current scroll and Y scale; the canvas queries retention before row conversion. Missing textures and uncovered zoom-out gaps draw counted peak-line fallbacks with the existing clamp. Reserved song/selected slots and memory-budget policy remain unchanged. First identical zoom fingerprints current rows and refreshes every remaining stem once; the next identical frame refills none. Revision, layout, style, width and X-subpixel guards prevent incompatible reuse. Same-revision asynchronously updated waveform rows may lag during changing zoom, but first settlement fingerprints the current data and restores exact pixels; the retained key does not claim a decoder-version identity.
- **Final optimized production evidence:** the same 300-stem zoom-plus-settlement sequence reports total p50/p99/max **9.40164/10.9284/38.3756 ms**, collection p50 7.50114 ms and submit p50 1.88557 ms. It regenerates 162,460 rows, performs 1,252 actual stem refills (initial current-data refresh plus eight per changing frame), defers 34,748 stem submissions, collects 160,256 rows, and has zero memory or line fallbacks. First settlement is **38.3756 ms**, refreshing the remaining 292 stems/37,960 rows; repeat settlement regenerates zero. Settlement is included in the reported samples and maximum, not hidden from the timing gate.
- **Actual-chart supplemental evidence:** a read-only production parse of `Kick_Your_Ass/kick_your_ass2.bmson` resolves nine linked keys. A renderer-only fixture replicates retained T-440 first-source projected rows across those real keys; it does not measure all nine actual waveforms or a complete application frame. Alternating zoom profiles report p50/p99/max 1.07227/1.32778/1.35593 ms, at most eight uploads per changing frame, 119 counted visible line fallbacks and zero memory fallbacks. First settlement refreshes one stem/130 rows, repeat refreshes zero, and all 16,384 software-surface pixels equal a fresh full refill.
- **Verification:** normal optimized dependency-aware field-renderer and production frontend harness targets pass. Required `ctest -R field` passes **47/47**, including rotating budget, exact settled pixels, same-revision waveform update at first settlement, canonical downscroll origin, scroll/zoom-out gaps and direction toggles, identity invalidation, invalid/empty requests and absent-texture fallback accounting. The opt-in unbudgeted reference benchmark passes six assertions and reports full collection/submit timing across 122 samples. Configured cognitive-complexity checks find zero violations, matching zero baseline violations; every new function/test is at most 25. `third_party/libotoworm` remains an empty directory.
- **Evidence paths:** `/home/zardoru/devel/wormedit-scratch-builds/seek-render-oct7/render/`: `canvas-baseline.log`, `canvas-final.log`, `canvas-kick-final.log`, `benchmark-reference.log`, `build-final.log`, `tests-field-final-reviewed.log`, `tidy-baseline.log`, and `tidy-final-reviewed.log`. Root independently reran the same Debug production canvas on merged main: settled surface hash 5663771512333176944 exactly matches the baseline, regenerated rows fall 4,679,700→162,460, and repeat settlement regenerates zero. The main application build and all 2,568 tests pass; integration evidence is in `memory/coordination/oct7-seek-render-batch.md`.
- **Pending live gate:** no live sample percentage or original-chart GUI capture was measured. The under-3% main-thread outcome remains pending a permitted live profiler capture; the measured headless renderer phase and operation gates above are complete. CPU projection remains T-440's scope.

## T-470 Seek without decoding audio on the frame thread

- **Status:** done in code (2026-10-07; integrated main application build and 2,568/2,568 tests pass)
- **Outcome:** a skip during playback (arrow, PageUp/PageDown, wheel or minimap click) returns from `chart_preview_t::seek` without decoding audio on the main thread. Audio resumes at the new position from blocks decoded off-thread, or already resident, and the main-thread share of `decode_block` during skipping drops from 3.7% to zero.
- **Read first:** `src/sys/chart_preview_state.cpp` (`chart_preview_t::seek`), `src/audio/preview_session.cpp` (`seek`, `prime_started`), `src/audio/preview_pump.cpp` (`pump_preview_lookahead`), `include/wormedit/audio/source_registry.h`.
- **Files owned:** `src/audio/preview_session.cpp`, `include/wormedit/audio/preview_session.h`, `src/audio/preview_pump.cpp`, `src/sys/chart_preview_state.cpp`, `tests/core/audio_preview_bridge_tests.cpp` (or a new `audio_preview_seek_tests.cpp`), `src/sys/sdl_preview_output.cpp`, `src/sys/sdl_preview_output.h`, `tests/sys/sdl_preview_output_tests.cpp`, `src/sys/audio_transport_frame.cpp`, `tests/sys/audio_transport_notification_tests.cpp`, `tests/core/audio_preview_session_tests.cpp`, `tests/core/audio_playback_rate_tests.cpp`, `src/audio/offline_mix.cpp`, this card.
- **Supporting paths (recorded before implementation):** `src/sys/sdl_preview_output.cpp`, `src/sys/sdl_preview_output.h`, `tests/sys/sdl_preview_output_tests.cpp`, `src/sys/audio_transport_frame.cpp`, `tests/sys/audio_transport_notification_tests.cpp` (deferred failure notification retains the new seek position), `tests/core/audio_preview_session_tests.cpp` (explicit headless pumping and concurrent seek regressions), `tests/core/audio_playback_rate_tests.cpp` (explicit initial pump preserves the existing latency fixture), `src/audio/offline_mix.cpp` (offline owner explicitly primes before its first rendered chunk; its output and failure tests remain unchanged).
- **Initial audit:** `prime_started` materializes the lookahead and synchronously invokes `pump_preview_lookahead`, including decoder seek/read on cache misses, before publishing the session. SDL output already has a refill worker; initial silence must hold the new source clock until its first prime completes.
- **Steps:**
  1. Record on this card what `prime_started` decodes before it returns, and why. It may be needed to avoid an initial gap.
  2. Move that priming to the existing preview decode worker. The output plays silence, or the resident keysounds, until the first block arrives, and seek returns at once.
  3. Bound the gap: measure the time from seek to first audible frame with SDL dummy audio, and keep it under 50 ms on a warm cache.
  4. Test rapid repeated seeks (10 within 100 ms): each seek supersedes the previous one, no stale block plays, and no decode runs on the calling thread.
- **Implementation:** start/seek only materialize bounded work and publish a pending generation. Existing SDL refill-worker pumping performs the initial decode; pending render emits silence without advancing the new clock. Old in-flight pumps cannot clear a newer generation’s pending state. Offline rendering explicitly pumps before its first chunk. Deferred failures use the existing notification and pause at the new audible position.
- **Measured evidence:** optimized normal build, SDL dummy postmix, 30 measured warm seeks after an unmeasured warmup with the target block verified cached. Read-only real Kick `clap.ogg`, source frame 20,000 (the stem has initial silence): seek-call p50 **0.01266 ms**, p99 **0.04628 ms**; first-audible p50 **22.6148 ms**, maximum **23.0499 ms**. Capture is armed after seek returns, so the gap is conservative; it measures the representative compressed source, not whole-chart GUI sample share or a cold-cache guarantee. A standalone ELF wrapper over the same production objects observes **184 decode_block requests, 0 on the calling thread, 184 on the refill worker**.
- **Verification:** **235/235** audio/SDL CTest cases pass, including 10 seeks within 100 ms while an old pump is blocked, latest-position PCM identity, pending Stop cancellation, initial failure retaining the new cursor, mid-slice/loop/rate/tail behavior and unchanged offline output/failure expectations. The bounded gate always releases and joins on failure. Configured tidy has no new violation and its 13 prior test violations are unchanged. Scratch logs and `link-decode-thread-probe.py` remain in `wormedit-scratch-builds/seek-render-oct7/audio`; no original source file was written.
- **Invariants:** mid-slice resume (T-442) and loop regions behave as before.
- **Reject/failure behavior:** if a decode fails after the seek, the error appears as today's notification, and the transport stays at the new position.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|^sdl-audio\.'`; the isolated SDL dummy-audio gap measurement on this card.
- **Out of scope:** WSOLA cost (T-471).

## T-471 Measure WSOLA time-stretch cost in Release and bound it

- **Status:** ready
- **Outcome:** this card records the audio-thread CPU share of Keep-pitch playback at 0.5× and 0.1× in a Release build. If it exceeds 10% of one core, the correlation search is reduced (decimated coarse search, then fine search around the best candidate) and stays bit-for-bit deterministic for a given input.
- **Read first:** `include/wormedit/audio/time_stretch.h`, `src/audio/time_stretch.cpp` (`best_candidate`, `correlation`), T-233 in [frontend shell](frontend-shell.md).
- **Files owned:** `src/audio/time_stretch.cpp`, `include/wormedit/audio/time_stretch.h`, time-stretch tests, this card.
- **Steps:**
  1. Build RelWithDebInfo, play Kick Your Ass at 0.5× and 0.1× with Keep pitch, and record `perf` audio-thread shares here.
  2. If either is under 10%, stop: the Debug build's `vector::operator[]` checks explain the T-438 numbers.
  3. Otherwise, implement a coarse-to-fine candidate search, and compare its output against the current one with the T-233 sine measurements.
- **Invariants:** the output is deterministic, and the T-233 isolated measurements still pass.
- **Reject/failure behavior:** none; the playback rate and Keep pitch choices are unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'time_stretch|playback_rate'`; the measurements on this card.
- **Out of scope:** pitch-changing playback (T-45).

