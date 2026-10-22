# Task cards: resources and audio

[Back to the task-card index](../tasks.md).

Stage 3 resource and stage 4 audio/BMSON leftovers from
[authoring/export](../stages/authoring-export.md). Placement, assignment,
replacement, auto-increment, asset relink, BMSON restart/continue, razor cuts,
and source-registry reconciliation already exist; do not rebuild them. Apply the
stale-plan check (rule 9) first.

## T-100 Add a protected resource-delete command

- **Status:** done
- **Implementation/verification (2026-10-03):** `apply_resource_deletion` checks revision and catalog identity before one reference pass, rejects with the exact stored-event count, and commits one undoable catalog-only deletion. Direct note/BGM/BGA IDs count alongside each retained sound-channel event whose source asset matches; aliases sharing that asset are conservatively protected, and linked semantic/retained objects count separately. Nine new regression cases cover cross-chart counts, orphan channel protection, aliases, stale/missing/zero IDs, undo, assets, and unchanged BMS identifiers before undo. Final core output: `100% tests passed out of 1109` (`/tmp/wormedit-t100-final-core.log`). The application builds, new functions satisfy configured clang-tidy; existing earlier test-wrapper warnings are unchanged.
- **Outcome:** a core command deletes one catalog resource only when no note, BGM, BGA, or sound-channel event references it; otherwise it rejects with the reference count.
- **Read first:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `include/wormedit/core/document.h` (resource catalog), `tests/core/resource_assignment_tests.cpp`.
- **Files owned:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `tests/core/resource_assignment_tests.cpp`.
- **Steps:**
  1. `apply_resource_deletion(history, revision, resource_id)`; count references in one pass.
  2. Tests: unused deletes and undo restores; used rejects with the exact count; stale revision rejects.
- **Invariants:** the underlying asset file and asset record are untouched; BMS identifiers of other resources are not renumbered.
- **Reject/failure behavior:** any reference rejects with no history entry.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** cascading delete, UI (T-101).

## T-101 Add Delete to the resource catalog

- **Status:** done
- **Final verification:** normal/Release builds, 3,321/3,321 full CTest checks and 445 production event cases (6,173 assertions) pass; affected default clang-tidy, documentation and whitespace checks pass. Evidence: `/tmp/sounds-complete-{build,release,ctest,events}.log`.
- **Completion (2026-10-11):** Protected per-row Delete is live in Sounds/Images. A shared 64-bit cross-chart reference map counts direct note/BGM/BGA references and retained sound-channel asset aliases; the same map drives deletion, counts and filters. Used rows reject deletion, successful deletion prunes selection/arming, hidden rows retain identity. Core, projection, state and production event regressions cover the contract.
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Stale-plan audit (2026-10-03):** No catalog Delete/count UI exists. The actual catalog-to-C-view copy is `append_compose_resource` in unowned `src/sys/workspace_projection.cpp`; the owned presenter and frontend contract cannot project a new count without that owner. Use counts must match T-100 across charts and retained sound-channel asset aliases. Armed clearing can remain in owned `compose_resources.cpp` after a successful catalog refresh.
- **Coordinator scope/audit (2026-10-11):** protected batch deletion already exists in live docks/core; finish per-row disabled states and exact usage tooltip rather than duplicate deletion. Supporting ownership: `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `tests/core/resource_assignment_tests.cpp`, `src/sys/sound_list_projection.cpp/.h`, `tests/sys/sound_list_projection_tests.cpp`, live Sounds/Images docks and their state/tests. Share exact cross-chart/retained-alias usage counts with deletion and T-103/T-206.
- **Outcome:** each Compose catalog row has a Delete button, enabled only for unreferenced resources; referenced rows show their use count in the tooltip.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose/views/compose_resource_presenter.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose_resource_presenter.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`, `tests/core/compose_resource_presenter_tests.cpp`. User-approved supporting scope (2026-10-03): `src/sys/workspace_projection.cpp`.
- **Steps:** 0. Scope extension approved by the user (2026-10-03). 1. Project a per-row reference count. 2. Wire Delete through the standard pattern.
- **Invariants:** an armed resource that is deleted is disarmed.
- **Reject/failure behavior:** core rejection shown in the status line.
- **Verify:** standard build/test.
- **Out of scope:** usage navigation (T-102).

## T-102 Navigate to a resource's uses

- **Status:** partial (sound-resource navigation completed by T-364; visual/BGA-resource navigation remains)
- **Done so far:** T-364 added bounded cross-chart sound-use projection and activation from the
  Sounds tab, covering note/BGM references, shared assets, source-only channel/clip references,
  stale/deleted results and exact positioned jumps. Production regressions live in
  `tests/sys/sound_usage_projection_tests.cpp`, `tests/sys/sound_usage_actions_tests.cpp` and
  `tests/ui/sound_usage_event_harness_tests.cpp`.
- **Remaining:** Add the corresponding use list and activation for image/video resources and BGA
  events from the BGA dock. Reuse the T-364 result identity/navigation contract rather than reviving
  the retired inspector catalog path. Recheck whether explicit Next/Previous controls add value once
  the bounded list is visible; T-364 activates a chosen row directly.
- **Outcome:** Sounds and BGA resources each offer a bounded **Find uses** list across project charts;
  activating a positioned note/BGM/BGA result opens its chart and moves the cursor to it, while a
  source-only sound result selects its source without inventing a chart position.
- **Read first:** `src/sys/resources/sound_usage_view.cpp`; `src/sys/resources/sound_usage_actions.cpp`;
  `src/sys/resources/bga_list_view.cpp`; `src/ui/nuklear/compose_bga_panel.cpp`;
  `src/sys/compose/input/compose_input.cpp` (BGA event selection/navigation).
- **Files owned:** new `src/sys/bga_usage_view.h`, `src/sys/bga_usage_view.cpp`, new
  `src/sys/bga_usage_actions.h`, `src/sys/bga_usage_actions.cpp`,
  `src/ui/frontend/compose.h`, `src/ui/nuklear/compose_bga_panel.cpp`,
  `src/ui/nuklear/compose_state.h`, `src/sys/app/application_frame_view.cpp`,
  `src/sys/compose/input/compose_input.cpp`, new `tests/sys/bga_usage_view_tests.cpp`, new
  `tests/sys/bga_usage_actions_tests.cpp`, new `tests/ui/bga_usage_event_harness_tests.cpp`,
  `CMakeLists.txt`, `tests/CMakeLists.txt`, this card and `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Stale-plan check: reuse the T-364 result/view/action shapes where their sound terminology does
     not leak; inventory base/Layer/Layer 2/Poor BGA event identity and navigation.
  2. Project BGA uses in chart/position/kind order with the same bound and stable revision identity
     as sound uses.
  3. Add **Find uses** to the selected BGA resource and activate a chosen result through normal
     chart selection and BGA-event navigation.
  4. Test unused resources, every BGA kind, multiple charts/positions and stale/deleted results.
- **Invariants:** Finding and activating uses never changes the document, resource selection or
  authored BGA data except for the explicit view-only selection/navigation result.
- **Reject/failure behavior:** Zero uses shows an empty list; stale or deleted results refresh or
  report unavailable without jumping to another event.
- **Verify:** focused BGA usage projection/action and event-harness tests; `GATE`; isolated Xvfb
  activation from the BGA dock.
- **Out of scope:** Replacing usages, asset-level reverse links unrelated to chart events, and
  restoring the retired inspector resource list.

## T-103 Filter the catalog by use state and name

- **Status:** done
- **Final verification:** normal/Release builds, 3,321/3,321 full CTest checks and 445 production event cases (6,173 assertions) pass; affected default clang-tidy, documentation and whitespace checks pass. Evidence: `/tmp/sounds-complete-{build,release,ctest,events}.log`.
- **Completion (2026-10-11):** Live Sounds/Images headers provide All/Used/Unused/Missing and case-insensitive name/full-path/BMS-ID matching before virtual windowing. Existing group/layer filters, complete slot identities and hidden selected/armed resources are preserved.
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Stale-plan audit (2026-10-03):** Search state and name/source-filename matching already exist, but catalog rows expose no usage signal/count and only the source basename. The requested use-state and path filters need unowned `src/ui/frontend.h`, `src/sys/compose/views/compose_resource_presenter.cpp`, and the actual catalog-to-C-view copy in `src/sys/workspace_projection.cpp`.
- **Coordinator scope (2026-10-11):** extend live Sounds/Images docks (`compose_sounds_panel.cpp`, `compose_bga_panel.cpp`, `keysound_list_state.cpp/.h`, `compose_state.h`) and their state/event tests; `sound_list_projection.cpp/.h` and tests filter before virtual windows. `frontend.cpp` exports filter requests. Existing inspector resource list is retired; preserve group/layer filters and armed resource identity.
- **Outcome:** the catalog has a text filter (case-insensitive substring on name/path/BMS ID) and a Used/Unused/Missing/All selector in addition to the existing role tabs.
- **Read first:** `src/ui/nuklear/compose_resources.cpp` (`filter_role`, `filter_name`), `src/sys/compose/views/compose_resource_presenter.cpp`, `docs/chart-editor-plan/document-model.md` (resource list paragraph).
- **Files owned:** `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `tests/ui/nuklear_compose_state_tests.cpp`. User-approved supporting scope (2026-10-03): `src/ui/frontend.h`, `src/sys/compose/views/compose_resource_presenter.cpp`, `src/sys/workspace_projection.cpp`. Supporting ownership (2026-10-11): `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/sound_list_projection.cpp/.h`, `src/sys/bga_list_projection.cpp/.h`, `src/sys/resources/bga_dock_runtime.cpp`, associated projection/state/event tests.
- **Steps:** 0. Scope extension approved by the user (2026-10-03). 1. Add filter text and use-state to frontend state. 2. Apply both when iterating rows. 3. State tests for the filter predicate.
- **Invariants:** armed resource stays armed even when filtered out.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`.
- **Out of scope:** sorting.

## T-104 Show BMS audio fallback candidates for missing files

- **Status:** done (2026-10-10)
- **Outcome:** a missing `#WAV` row shows which fallback file was chosen, or lists ambiguous candidates and lets the user pick one through the existing relink command.
- **Read first:** `docs/references/bms-audio-fallback.md`, `src/sys/audio/audio_source_registration.cpp`, `include/wormedit/core/resource_assignment.h` (`apply_resource_asset_relink`), `src/ui/nuklear/compose_resources.cpp`.
- **Files owned:** `src/sys/audio_source_registration.cpp/.h`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/frontend.h`, `tests/sys/audio_source_registration_tests.cpp`.
- **Steps:** 1. Expose the resolution outcome (exact, fallback path, ambiguous list, corrupt) per resource. 2. Show it in the row tooltip; ambiguous rows get a picker that calls relink.
- **Invariants:** the authored `#WAV` token never changes unless the user picks.
- **Reject/failure behavior:** corrupt exact files are shown as corrupt, not replaced.
- **Verify:** standard build/test.
- **Out of scope:** image fallback.
- **Result (2026-10-10):** the retired inspector list is replaced by the Sounds dock. `resolve_audio_fallback` (`src/sys/audio/audio_fallback.h`, header-only) follows `docs/references/bms-audio-fallback.md`: exact path, else same-directory same-stem `.wav/.ogg/.flac` files matched case-insensitively (one = fallback, several = ambiguous, none = missing; absolute and `..` paths are only checked exactly; directory listings cached per pass). `project_keysound_fallbacks` (`sound_list_projection`) lists audio resources whose authored file is absent and has candidates, cached per document revision and asset base (it does not rely on `resource.missing`, which was false for files absent at open). `populate_compose_keysound_fallbacks` projects them into `WormUiComposeView`; the row tooltip says "Found nearby: …" or "Several files match, right-click to choose: …", and the row's context menu gets "Use <file>" entries that send `WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK`, handled by `relink_sound_fallback` through `apply_resource_asset_relink` (one undo step; the authored #WAV token is untouched). Core change: an audio relink now clears `missing` on that asset's resources. Corrupt exact files are left to the decoder diagnostic. Tests: four resolver cases (exact, single, ambiguous, none/unsafe), the projection test, and the relink action test. Xvfb captures in `sweep/t104-*.png` (menu with "Use amb.ogg / Use amb.flac", and "Sound relinked to amb.ogg."). Full suite 3113/3113 after a clean rebuild of the frontend harness target (stale objects had caused spurious BMSON lane-mode failures).


## T-105 Audit BMSON slice collision and final-slice rules

- **Status:** done
- **Implementation notes (2026-10-01):** all steps done: `tests/core/sound_slice_rules_tests.cpp` has eight `sound_slice rule N` cases (restart wins at a shared pulse; continue keeps the source position; one slice and polyphony one per pulse; open-ended final slice and its bounds by decoded length or clip end; BGM note discarded when a playable note shares the pulse, on import and export; unequal fused hold lengths rejected on export; equal lengths accepted; duplicate lane/pulse notes in one channel rejected). Every rule passes against current production code, so no gap is recorded and no follow-up card is needed. Run them with `ctest --test-dir build --output-on-failure -R sound_slice`.
- **Outcome:** a test file proves each rule in the BMSON slicing reference (same-pulse restart/continue conflict, collision layering, open-ended final slice) or the report lists the rule that fails.
- **Read first:** `docs/references/bmsone-slicing.md`, `include/wormedit/core/sound_slice.h`, `tests/core/bmson_sound_channel_editor_tests.cpp`, `tests/fixtures/bmson/`.
- **Files owned:** new `tests/core/sound_slice_rules_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. One `TEST_CASE` per numbered rule in the reference. 2. Do not change production code; if a rule fails, STOP and report it with the failing expectation.
- **Invariants:** tests only.
- **Reject/failure behavior:** a failing rule becomes a new card, not a fix here.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'sound_slice'`.
- **Out of scope:** fixing failures.

## T-106 Render a selected interval to PCM offline

- **Status:** done
- **Implementation/verification (2026-10-03):** `render_offline_interval` owns a synchronous production preview session/bridge/mixer, renders from timeline zero, and retains exactly `[begin_frame, end_frame)` so prior keysound/BGM tails survive. Metronome clicks are excluded, silent tails remain, the optional linear fade ends at digital zero, and timeline/frame/byte caps bound work and output. Missing bindings, decoder failures, incomplete/capacity-limited scheduling and cancellation return no PCM. Nine generated-PCM regressions cover exact stereo sums and source-tail endings, chunk boundaries with small caller lookahead, playable keysounds, exclusive end, deterministic repeats, fade/silence, early/late source failures, capacity, cancellation and frame/byte bounds. Audio output: `100% tests passed out of 164` (`/tmp/wormedit-t106-final-audio.log`); complete output: `100% tests passed out of 1641` (`/tmp/wormedit-t100-t106-all-tests.log`). Application build and configured clang-tidy of both new source/test files pass. Encoding/UI remains T-107.
- **Outcome:** a core function mixes the exact selected chart interval (music + BGM + keysounds) into an in-memory float32 stereo buffer using the same preview schedule and mixer as live playback, deterministically.
- **Read first:** `include/wormedit/audio/preview_session.h`, `include/wormedit/audio/preview_bridge.h`, `docs/timing-preview-scheduling.md`, `tests/core/audio_preview_session_tests.cpp`.
- **Files owned:** new `include/wormedit/audio/offline_mix.h`, new `src/audio/offline_mix.cpp`, new `tests/core/audio_offline_mix_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Drive a preview session with a synthetic callback loop from `begin_frame` to `end_frame`; decode synchronously (no realtime thread).
  2. Apply a linear fade-out over the last N ms (parameter).
  3. Tests: identical output on two runs; length equals the frame span; fade reaches zero.
- **Invariants:** a byte/frame cap rejects oversized intervals; supports a stop token for cancellation.
- **Reject/failure behavior:** missing sources are reported, not silently skipped.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`.
- **Out of scope:** encoding (T-107).

## T-107 Encode and publish preview.ogg

- **Status:** done (2026-10-20)
- **Verification:** Deterministic stereo encoder, full decode verification, private atomic preparation, explicit replacement, protected assets, cancellation and stale publication tests pass. GATE passes 4,318/4,318, whitespace and changed-C++ complexity (`/tmp/wormedit-gate.aCMmTI`). Isolated real-UI export resolves 0–20 seconds and publishes `/tmp/wormedit-priority-visual/preview.ogg`; independent ffprobe reports Vorbis, stereo, 44,100 Hz, 20.000000 seconds. Dialog and completion captures `/tmp/wormedit-preview-dialog.png` and `/tmp/wormedit-preview-exported.png` inspected.
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Outcome:** the area-selection menu offers "Export preview audio…": the selected interval is mixed with T-106, encoded to Ogg Vorbis with pinned libogg + libvorbis built through FetchContent (user decision 2026-09-30), written to a temporary file, verified by decoding it back, and atomically published as `preview.ogg` (or a chosen name) next to the chart.
- **Read first:** `cmake/dependencies/` (pattern for pinned dependencies), `include/wormedit/audio/offline_mix.h` (T-106), `docs/chart-editor-plan/stages/authoring-export.md` (stage 4 preview bullet), `include/wormedit/formats/conversion_publication.h` (atomic publication helpers).
- **Files owned:** new `cmake/dependencies/vorbis.cmake`, `CMakeLists.txt`, new `include/wormedit/audio/ogg_encoder.h`, new `src/audio/ogg_encoder.cpp`, new `tests/core/audio_ogg_encoder_tests.cpp`, `tests/CMakeLists.txt`, `src/ui/nuklear/compose_area_menu.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`; user-approved supporting scope (2026-10-03): `src/sys/resource_job_state.h/.cpp` for preview-export job data, progress and cancellation; existing application frame-service wiring if a dedicated runtime is needed.
- **Steps:** 0. Scope extension approved by the user (2026-10-03). 1. Pin libogg and libvorbis (record versions and license in the dependency file). 2. Encoder wrapper (quality setting, stereo, 44.1 kHz) with a stop token. 3. Encode on a worker with progress; verify by decoding with the existing miniaudio decoder. 4. Menu entry and atomic publish.
- **Invariants:** the chart and its assets are never modified; cancellation leaves no partial file.
- **Reject/failure behavior:** encode or verify failure reports an error and publishes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual export of a 20-second preview.
- **Out of scope:** other audio formats.
- **Supporting ownership (2026-10-20):** `include/wormedit/core/atomic_file_storage.h`, `src/core/atomic_file_storage.cpp`, `tests/core/atomic_file_storage_tests.cpp` split existing atomic writing into RAII preparation and explicit publication, allowing worker verification and UI-thread stamp admission without duplicating temporary-file plumbing. `src/sys/preview_audio_export.h/.cpp`, `src/sys/app/application.cpp`, `src/sys/app/application_frame_services.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend/preview_audio.h`, `src/ui/frontend/compose.h`, `src/ui/frontend/actions.h`, `src/ui/nuklear/preview_audio_dialog.h/.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/frontend.cpp` wire the selected interval, duration/trim/fade, explicit replacement, progress and cancellation. `include/wormedit/audio/preview_bridge.h`, `include/wormedit/audio/audition_resolver.h`, `src/audio/audition_resolver.cpp`, `src/sys/audio/chart_preview_state.cpp` share the live-preview music binding rather than copying its scheduling logic. `tests/support/temporary_directory.h` owns isolated test directories shared by new file/audio tests.
- **Supporting ownership (verification/documentation):** `src/sys/app/application_frame_services.h`, `tests/core/audio_preview_export_tests.cpp`, `tests/ui/preview_audio_export_event_harness_tests.cpp`, `docs/architecture.md`, `docs/manual/chapters/sounds-and-layers.adoc`. Preview export has its own runtime owner, sharing the resource-worker scheduler rather than extending resource-copy state.
- **Supporting ownership (dialog lifecycle):** `src/ui/nuklear/compose_resources.cpp` renders the preview form with the existing top-level Compose dialogs, after the workspace window has ended.
- **Supporting ownership (modal cleanup):** `src/ui/nuklear/compose_input_panel.cpp` stamps visible-chart cleanup even when a modal occludes Compose, avoiding false stale-action warnings on preview-dialog open/close; event-harness coverage pins the identity and viewport.

## T-267 Keep keysounds decoded in memory while editing

- **Status:** done (2026-10-01; `resident_sample_store_t` in `include/wormedit/audio/resident_samples.h`, `src/audio/resident_samples.cpp`; headless `audio.` suite 126/126, includes 100 repeated block requests with one decode, contiguous load order, relink/rebind, compaction at the one-third threshold, budget overflow and concurrent readers. The real application was not built or run in this session (no SDL3), so the manual verify step is still pending)
- **Implementation notes:** the arena is `append_arena_t` used directly (no extra `sample_arena_t` wrapper). The index is keyed by runtime source identity and shares decoded content by a content key (path, size, mtime), so a reconcile that only changes binding generations rebinds instead of decoding again. The mixer prefers the snapshot, then the block cache; `transport_t::set_resident_snapshot` swaps it under the transport lock and releases the old one off that lock. The budget is the `resident_sample_options_t` default (512 MiB) until the Editor options page (T-253) exists. Load order follows the first chart's first use, not the cursor (T-274 adds re-prioritization).
- **Outcome:** each short sample referenced by the open project (BMS `#WAV`, BMSON/osu!/SM keysounds; default: sources up to 30 s) is decoded once into an immutable in-memory buffer shared by audition and chart preview; replaying, re-auditioning, seeking, and restarting playback never decode it again. Long sources (the song) keep streaming through the block cache.
- **Cause (code reading, 2026-09-30):** decoded audio lives in three separate block caches: audition (`decoded_block_cache_t audition_cache` in `src/sys/app/main.cpp`, default limits in `include/wormedit/audio/block_cache.h`: **2 MiB total, 512 KiB per asset**, so a 2 s stereo float32 keysound ≈ 705 KB never fits and is decoded on every audition), preview (`preview_block_cache_limits()` in `include/wormedit/audio/preview_pump.h`, ~134 MB shared with the streaming music, whose blocks evict keysounds), and the waveform runtime's own cache. Each session start also clears the decode session's ready list.
- **Storage design (user decision, 2026-09-30):** keysound PCM lives in a dynamic **append-only arena**, not in per-sample allocations or an LRU. Editing usually only adds keysounds, so the arena grows and samples are laid out contiguously in load order (good cache locality during playback). Metadata (asset id, source revision, offset, frame count, channel count, sample rate, stale flag) lives in a separate index that the arena does not own. Replacing or relinking a sample marks its old span **stale** without freeing it; when stale bytes reach **one third** of the arena's used bytes (configurable, ⅓ by default, ½ allowed), the store compacts into a fresh arena off the realtime path.
- **Read first:** `include/wormedit/audio/block_cache.h`, `include/wormedit/audio/source_registry.h`, `include/wormedit/audio/audition_resolver.h`, `include/wormedit/audio/preview_pump.h`, `src/sys/audio/audio_source_registration.cpp` (`reconcile_audition_sources`).
- **Files owned:** new `include/wormedit/audio/resident_samples.h`, new `src/audio/resident_samples.cpp` (arena + index), new `tests/core/audio_resident_samples_tests.cpp`, `src/audio/audition_pump.cpp`, `src/audio/preview_pump.cpp`, `src/audio/source_registry.cpp`, `src/sys/audio_source_registration.cpp/.h`, `src/sys/app/main.cpp` (construction only), `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. `sample_arena_t`: an `append_arena_t` from T-271 configured with large chunks (default 64 MiB); `append(frames, channels) -> span`; uses the generic stale accounting and compaction. No per-sample free.
  2. `resident_sample_index_t`: immutable snapshot mapping (asset, source revision) → {chunk, offset, frames, channels, sample rate}; holds `std::shared_ptr`s to the chunks it references. Lookups read the current snapshot through an atomically published `std::shared_ptr<const snapshot>`; writers build a new snapshot and publish it (copy-on-write), so readers never lock.
  3. Loading: after a project opens or its sources are reconciled, decode eligible samples (default ≤ 30 s) on the task scheduler (cancelable, newest generation wins) directly into the arena, ordered by first use in the selected chart so playback walks memory forward; publish the index in batches; report progress in the status line.
  4. Invalidation: on relink or file-identity change (size, mtime, or content hash) decode the new data into the arena, publish an index pointing to it, and add the old span's bytes to the stale count. On project close drop the whole arena.
  5. Compaction: when stale ≥ threshold × used, a background task copies live spans into a new arena in the current playback order, publishes a new snapshot, and the old chunks are freed automatically when the last reader releases the old snapshot (no realtime-thread frees: release happens on the control thread when sessions refresh their snapshot).
  6. Budget: soft cap from options (default 512 MiB); beyond it, new samples stay on the streaming path with one warning instead of evicting resident ones.
  7. The audition and preview pumps resolve clips through the snapshot first and fall back to the block cache; music and long sources never become resident.
  8. Tests: decode counter (once across 100 auditions and 10 preview restarts); spans are contiguous in load order; relink marks stale and serves the new data; compaction triggers exactly at the threshold, preserves every live sample bit-for-bit, and frees old chunks only after the last snapshot holder releases; budget overflow keeps streaming; concurrent readers during publish/compaction (thread-sanitizer friendly).
- **Invariants:** `render()` never allocates, frees, or locks for resident samples (sessions hold a snapshot taken on the control thread); Stop semantics unchanged; arena memory never exceeds the budget plus one chunk.
- **Reject/failure behavior:** a sample that fails to decode is reported once and stays on the streaming path.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`; manual: open a BMS with many keysounds, wait for preload, audition and play repeatedly; the T-211 overlay's audio phase drops.
- **Out of scope:** the waveform runtime's cache (may later read from the store), compressed in-memory formats.

## T-268 Add a "play sound on click" toggle

- **Status:** done (armed BGM/BGA placement audition is a follow-up, see below)
- **Done so far:** `audio.toggle_click_audition` registered in `src/core/command_registry.cpp` (no shortcut) with its checked state refreshed in `src/sys/app/main.cpp`; `dispatch_registered_application_command` in `src/sys/app/main.cpp` routes it through `toggle_audition_on_click` (`src/sys/preferences/options_runtime.cpp`, same `apply_program_preferences` save path as Options); placements that create a note (click, virtual/measure click, lane-key release, alt pointer hold, sample placement) audition it when the preference is on (`src/sys/compose/input/compose_input.cpp`, `audition_created_note`); Shift lane assignment unchanged; test "Placement auditions the placed note only while click audition is on" in `tests/sys/application_commands_tests.cpp` (frame-timing exe, passes).
- **Follow-up (not blocking):** armed BGM/BGA resource placement does not audition, because those results carry no created object id.
- **Remaining:** none.
  - Note: `dispatch_registered_application_command` matches the command by id before parsing; `src/sys/commands/application_commands.cpp` has no kind for it (not owned).
- **Implementation audit (2026-10-07):** the `audition_on_click` editor preference already exists with an Options toggle (`src/sys/preferences/options_runtime.cpp:128`) and gates click audition in `src/sys/compose/input/compose_input.cpp:2316`. What remains is the toolbar toggle and the `audio.toggle_click_audition` command, driving that same preference. The user calls the toolbar toggle necessary for BMS work.
- **Outcome:** a toolbar toggle (speaker icon, command `audio.toggle_click_audition`, default on, as in iBMSC/BMSE) controls whether clicking, selecting, or placing a note or BGM event plays its sound; when off, clicks only select or place. Lane-key Shift assignment keeps its explicit audition.
- **Current behavior (2026-09-30):** clicking a BGM event always passes `request_audition = true` (`select_compose_bgm_event` in `src/sys/compose/input/compose_input.cpp`), note clicks request an audition too, and there is no way to turn it off.
- **Read first:** `src/sys/compose/input/compose_input.cpp` (`select_compose_bgm_event`, the batch loop that stores `result.audition` into `scene_cache.audition_request`), `src/core/interaction.cpp` (where `.audition` is set), `src/sys/compose/actions/compose_audition_requests.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/core/command_registry.cpp`.
- **Files owned:** `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/actions/compose_audition_requests.cpp`, `src/sys/app/main.cpp` (state and dispatch only), `src/core/command_registry.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/sys/application_commands_tests.cpp`.
- **Steps:**
  1. Session flag in application state, gated where `audition_request` is stored (one place), so the core keeps proposing auditions and the application decides.
  2. Also audition after a successful placement (click, lane key, armed-resource placement) when the toggle is on, matching iBMSC.
  3. Toolbar toggle and registry command; persist as a program-scope option once T-253 exists (Editor page).
  4. Tests: toggle off → no audition request reaches the output; toggle on → one request per click/placement.
- **Invariants:** chart preview playback unaffected; Shift lane assignment still auditions.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; manual: click notes with the toggle on and off.
- **Out of scope:** audition volume (T-256).
- **Result (2026-10-08):** done; `-L event-harness` 249/249; regexes `application_commands|command_registry|preferences|options` 24/24 passed, `^documentation\.` 1/1; armed BGM/BGA audition is a follow-up.

## T-271 Generalize the chunked arena into a core library

- **Status:** done (2026-09-30; CMake integrated; application build and 801 arena/storyboard/core tests passed)
- **Outcome:** one core arena library provides a shared chunked, aligned bump allocator with byte/object limits and statistics, plus two lifetime policies on top of it: a **frame** arena (reset keeps chunks; today's storyboard behavior) and an **append** arena (append-only, stale-byte accounting, compaction into a fresh arena, chunks kept alive by reference-counted snapshots; T-267's behavior). The storyboard preview moves onto the frame policy with no behavior change.
- **Why:** the storyboard already has `storyboard_frame_arena_t` (`include/wormedit/core/storyboard.h`, implementation in `src/core/storyboard.cpp`, used by `src/core/storyboard_preview.cpp` and `src/sys/audio/player_preview_presenter.cpp`), T-267 needs the same chunk layer with a different lifetime, and planned per-frame scratch (the field renderer batches in T-248, flattened view vectors in T-243) would otherwise grow a third copy.
- **Read first:** `include/wormedit/core/storyboard.h` (`storyboard_frame_arena_t`, `storyboard_arena_options_t`, `storyboard_arena_error_t`), `src/core/storyboard.cpp` (arena implementation), `tests/core/storyboard_tests.cpp` (arena tests), T-267 above.
- **Files owned:** new `include/wormedit/core/arena.h`, new `src/core/arena.cpp`, new `tests/core/arena_tests.cpp`, `include/wormedit/core/storyboard.h`, `src/core/storyboard.cpp`, `src/core/storyboard_preview.cpp`, `src/sys/audio/player_preview_presenter.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. `chunk_arena_core_t`: chunk list, aligned `allocate_bytes(size, alignment)`, limits (`maximum_bytes`, `maximum_objects`), statistics (committed, used, objects), typed errors (`invalid_alignment`, `byte_limit`, `object_limit`, `out_of_memory`). Chunks are separately allocated and never move.
  2. `frame_arena_t`: core + `reset()` that rewinds and keeps committed chunks. Make `storyboard_frame_arena_t` and its option/error types aliases of the generic ones (keep the old names compiling), then move the implementation out of `storyboard.cpp` (pure move; storyboard tests must pass unchanged).
  3. `append_arena_t`: core whose chunks are `std::shared_ptr`-owned blocks; `append` returns a span plus a chunk handle; `mark_stale(bytes)`; `stale_ratio()`; `compact(live_spans, order) -> append_arena_t` that copies live spans into a new arena and returns the remap. Snapshots hold chunk handles, so old chunks die with the last snapshot.
  4. Tests for both policies: alignment, limits, reset reuse without new allocations (allocation-counting allocator), append stability of returned spans, stale accounting, compaction remap correctness, chunk lifetime with outstanding snapshots.
  5. Keep the existing `frame_arena_t` guidance in T-248 and T-272, which now owns the per-frame work formerly tracked by T-243.
- **Invariants:** storyboard preview output bit-identical; no allocation on the realtime audio path from either policy after warm-up.
- **Reject/failure behavior:** allocation failures return typed errors, never throw.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'arena|storyboard|^core\.'`.
- **Out of scope:** resident samples themselves (T-267), GPU texture pools.

- **T-271 verification evidence (2026-09-30):** integrated application build passed; `ctest --test-dir build --output-on-failure -R 'arena|storyboard|^core\.'` reported `100% tests passed out of 801` (14.22 s). Configured clang-tidy passed for the arena implementation and tests. The old and new preview implementations matched bitwise across 98 frame results (easing, loops, arena limits); canonical output SHA-256 `d8d2a58bbc3c7532f90dbb3cedbfc325e0f798a859882bed23bafa75119d611e`. Forced allocation failures return typed errors; append stale accounting excludes alignment padding. Shared chunk ownership adds one control-block allocation when a chunk is created.

## T-749 Author preview points and spans with format-aware duration

- **Status:** deferred (owner requested a later card, 2026-10-20)
- **Outcome:** set a song/chart preview point and edit its playback region as a span, preserving formats that store a point only versus a point and duration, including osu!mania and StepMania.
- **Read first:** `docs/architecture.md`, `include/wormedit/core/document.h`, `src/formats/osu_mania.cpp`, `src/formats/stepmania.cpp`, `src/sys/audio/preview_audio_export.cpp`, `src/ui/nuklear/compose_area_menu.cpp`.
- **Files owned:** this card, `docs/chart-editor-plan/editing/metadata-interop.md`, `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/native_project.cpp`, `src/formats/osu_mania.cpp`, `src/formats/stepmania.cpp`, `src/formats/conversion.cpp`, `src/sys/audio/preview_audio_export.cpp`, `src/ui/nuklear/compose_area_menu.cpp`, `docs/manual/chapters/sounds-and-layers.adoc`; the first step must name exact shared command, projection, gesture and regression-test consumers before implementation.
- **Steps:**
  1. Audit existing preview metadata/import/export and playback-region owners; record the actual osu!mania and SM/SSC point/duration capabilities and units, distinguishing authored duration from an editor audition default.
  2. Specify one native preview owner and command contract, format conversion/loss policy and span-handle behavior; resolve uncovered ownership questions before changing data or codecs.
  3. Implement setting the preview point and dragging/moving/resizing its span through shared transactions and gesture cancellation, with one Undo step and explicit duration when the target supports it.
  4. Round-trip native, osu!mania and SM/SSC fixtures; test point-only imports/exports, explicit duration, offsets, cancellation, invalid/empty spans and compatibility diagnostics. Document the workflow.
- **Invariants:** point-only formats do not gain a fabricated authored duration; supported durations and exact time offsets survive round trips; audition defaults do not silently modify authored metadata.
- **Reject/failure behavior:** invalid ranges, stale gestures and unsupported/lossy target mappings reject or report through existing diagnostics without partial edits or publication.
- **Verify:** GATE; isolated Xvfb set/move/resize/cancel workflow and native/osu!mania/StepMania round-trip tests named by the audit.
- **Out of scope:** current mouse-selection cancellation/history fix; automatic preview selection or waveform analysis; implementation is not started by this planning request.

## T-514 Mark missing audio on open, and relink sounds without needing lanes

- **Status:** done (2026-10-11)
- **Found by:** T-104 (2026-10-10).
- **Outcome:** a project opened with an audio file absent on disk marks that resource `missing` immediately, and relinking a sound (including a T-104 fallback pick) works on any chart, including one without editable lanes.
- **Audit (2026-10-11 stale-plan recheck):** in the real app, `resource_entry_t::missing` stayed false for files that were already absent when the project opened, so T-104's projection checks the disk itself. `WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK` is dropped on lane-less charts with "no editable lanes", although relinking a resource does not depend on lanes.
- **Read first:** `src/sys/project/project_open.cpp` (resource missing flags after open), `src/sys/audio/audio_source_registration.cpp`, `src/sys/compose/actions/compose_actions.cpp` (`relink_sound_fallback` and the lane gate before it), `src/sys/sound_list_projection.cpp`.
- **Files owned:** `src/sys/project_open.cpp/.h`, `src/sys/audio/audio_source_registration.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/sys/sound_list_projection.cpp`, `tests/sys/project_open_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/sound_list_projection_tests.cpp`, this card. Coordinator-approved supporting ownership: `project_open.h` exposes the shared availability initializer; coordinator owns `main.cpp` calls at CLI startup and session replacement/recovery. Coordinator-approved lane-less UI support: `src/ui/nuklear/compose_sounds_panel.cpp` and the fallback section of `tests/ui/nuklear_event_harness_tests.cpp` (actual menu payload).
- **Implementation policy (coordinator approved, 2026-10-11):** Availability is initialized on open, independent of asynchronous decoder probes. Existing BMS/BMSON/StepMania resolver outcomes supply missing state; native/osu sources use one exact availability check per asset, shared across aliases. A present unsupported/corrupt file stays nonmissing. Native assets are external paths; no embedded/package bytes need separate treatment. No decoder API changes are needed. The same exact availability initializer is exposed for native CLI startup and session replacement/recovery, which bypass the file-picker open path. Lane-less relinking may use a temporary chart-only interaction after current project/chart/revision validation, without storing fake lane geometry.
- **Steps:**
  1. Write a failing test: open a project whose audio file is absent and assert `missing` is set.
  2. Set `missing` from existing source-resolution results where available and initialize exact native/osu availability before projection; let T-104 projection use the flag again. Decoder failure alone must not set missing.
  3. Write a failing test: relink a fallback on a lane-less chart. Then move the relink action ahead of the lane gate, or exempt it.
- **Done so far:** Missing-on-open regression first failed (one missing-state assertion, six others passed; `/tmp/t514-missing-open-failing.log`). Lane-less fallback regression first failed with four assertions (`/tmp/t514-laneless-failing.log`). Open-time initialization, saved native flag/alias refresh, fallback projection flag/cache use and temporary lane-less action context implemented. Final open/Sounds tests, including actual native startup and empty-base relative/absolute availability, pass 87 cases / 1381 assertions (`/tmp/t514-startup-final-tests.log`); all Compose action notification tests pass 50 cases / 600 assertions (`/tmp/t514-all-action-tests.log`). New and changed functions satisfy complexity <=25; only unchanged existing/third-party warnings remain. Documentation checker passes (`/tmp/t514-docs.log`). Alias updates use one resource pass after per-asset resolution, avoiding quadratic audio-open scans. Fallback cache checks the bounded resource missing-ID vector each projection so same-revision availability refresh invalidates old entries; there are no additional per-frame disk checks.
- **Coordinator verification (2026-10-11):** Shared initializer runs before CLI startup history creation and session replacement/recovery. The real Sounds fallback menu now includes the captured project ID; a lane-less production event test verifies all five action identifiers (1 case/9 assertions, `/tmp/t514-fallback-events.log`). Authenticated isolated native startup refreshes a deliberately stale saved flag, offers Use declared.ogg without editable lanes, persists the chosen path and clears missing, and one native Undo restores declared.wav and missing (`/tmp/t514-native-complete.log`, `/tmp/t514-native-{menu,relinked,undo}.png`). Normal/release builds and the final full-suite results are recorded in the batch checkpoint.
- **Invariants:** a present file stays `missing == false`. Relinking stays one undo step, and stale revisions still reject.
- **Reject/failure behavior:** unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'project_open|sound_list|^core\.'`; full suite.
- **Out of scope:** BMS audio fallback rules (T-104).

