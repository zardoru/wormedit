# Task cards: Sounds tab (library, bulk import, zoomable waveform, slicer)

[Back to the task-card index](../tasks.md).

User request (Zar, 2026-10-02), redesigning the workspace tab `audio_slices`
("Sonidos", `src/ui/nuklear/audio/audio_panel.cpp`). Today it shows a grid of
"Asset N" buttons and one fixed 190 px overview. Target layout: a listbox of
sounds on the left, a vertical column with the waveform in the centre, and a
properties column on the right (with side margins). This is the workspace tab,
not the Compose dock panel of [T-229](panels-menus.md), which lists BMS WAV
slots; both read the same resource catalog. Apply the stale-plan check (rule 9)
first.

Decisions (2026-10-02): imported stems are copied into the project; the list
shows every audio asset; mp3 defaults to "sound", wav/ogg to "stem" when the
chart has no BGM. Sound models are separate: **stem** (full-length BMSON sound
channel), **BGM/keysound** (classic BMS `#WAV`), **sample** (osu!/storyboard
sample). Users can tag sounds. A Woslicer/bmhelper-style slicer lives here
(a long DAW export cut at BPM, beat division and offset), with the aim of
later covering more bmhelper functions.

## T-324 Add sound kind and free tags to the resource catalog

- **Status:** done
- **Implementation notes (2026-10-02):** `sound_kind_t` and `tags` live on `resource_entry_t`; `apply_resource_sound_metadata` normalizes tags (trim, ASCII-case de-duplication, 32 tags of 64 bytes) and a no-op returns not-applied with no diagnostics, like the relink command. `native_project.cpp` writes `sound_kind`/`tags` only when set, so no schema bump was needed; older files load with the defaults. Core, format and full headless suites pass.
- **Outcome:** a resource carries a `sound_kind` (`stem`, `keysound`, `sample`) and a bounded list of free-text tags; a revision-checked command sets both and undoes in one step; old native projects load with kind inferred from role and no tags.
- **Stale-plan check:** grep `sound_kind` and `tags` in `include/wormedit/core/document.h`.
- **Read first:** `include/wormedit/core/document.h` (resource struct), `include/wormedit/core/resource_assignment.h`, `src/formats/native_project_migrations.cpp` (the migration table is schema 1 only), `src/formats/native_project.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `src/formats/native_project.cpp`, `tests/core/resource_assignment_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:**
  1. Add the optional fields with defaults; serialize them only when non-default so existing files round-trip byte-identically. If that requires a schema bump, **STOP** and ask.
  2. Add `apply_resource_sound_metadata(history, expected_revision, resource, kind, tags)` with limits (32 tags, 64 bytes each, trimmed, case-folded duplicates removed).
  3. Tests: set/undo, limits rejected atomically, old-file load, exact round trip.
- **Invariants:** BMS identifiers, asset IDs and conversion output are unchanged; tags never reach exported BMS/BMSON/osu! files.
- **Reject/failure behavior:** invalid tags leave the document and revision unchanged.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^formats\.|^documentation\.'`; `clang-tidy -p build` on each changed `.cpp`.
- **Out of scope:** tag UI (T-330), export of tags.

## T-325 Import many sounds in one history step

- **Status:** done
- **Implementation notes (2026-10-02):** `apply_bulk_sound_import` (`include/wormedit/core/sound_import.h`) runs on a draft history and commits once. Stems in a BMSON chart get a sound channel, a restart event at the first row, their own BGM lane and a linked marker, plus a `sound_channel` target identity and `$.sound_channels[N]` provenance so the BMSON writer re-exports them (round trip tested). Stems in other chart families, or with no chart, become catalog sounds only. The caller must pass project-relative copies with consecutive reserved asset IDs (BMSON export rejects absolute paths). The Compose rendering of the new lane is untested here (SDL3).
- **Outcome:** `apply_bulk_sound_import` registers N probed audio files as assets and resources of a chosen kind in one command; stems also create a BMSON sound channel with a restart event at position 0 and its semantic BGM marker (via the `insert_bmson_bgm_marker` path), so they appear in Compose; undo removes all of it at once.
- **Read first:** `include/wormedit/core/resource_assignment.h` (`apply_resource_asset_import`, `next_resource_asset_id`), `include/wormedit/core/bmson_sound_channel_editor.h`, `include/wormedit/core/resource_file_copy.h`, `src/sys/audio/audio_project_import.cpp`.
- **Files owned:** new `include/wormedit/core/sound_import.h`, new `src/core/sound_import.cpp`, new `tests/core/sound_import_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Define the request (ordered list of path, name, kind, copied project path) and result with per-file diagnostics.
  2. Validate every entry (limits, duplicate names resolved deterministically, catalog capacity) before mutating anything.
  3. Apply stems, keysounds (allocating BMS identifiers when the chart is BMS-family) and samples.
  4. Tests: 1 and 50 files, mixed kinds, capacity failure, undo, a stem visible in the BGM projection.
- **Invariants:** list order is the catalog order; file copying stays outside the command (caller reserves IDs as `import_resource_asset_action_t` already does).
- **Reject/failure behavior:** one invalid entry rejects the whole batch with all diagnostics.
- **Verify:** as T-324 plus `-R 'sound_import'`.
- **Out of scope:** dialog and drag-drop (T-326, T-329), extracting a stem into slices (T-331).

## T-326 Model the "Import as…" dialog and drop handling

- **Status:** done
- **Implementation notes (2026-10-02):** `src/ui/nuklear/sound_import_state.h/.cpp` holds the pending list and phases (`choose_kind`, `review_list`, `ready`); the 8 cases in `tests/ui/sound_import_state_tests.cpp` are registered in the frontend-state target, which needs SDL3 and was not built here. They passed compiled standalone against Catch2 (50 assertions); run `ctest -R '^frontend-state\.'` on your build to confirm. Also done: flac accepted as a stem-friendly extension; mp3 and any single file when the chart has BGM go straight to `ready`.
- **Outcome:** dropped or picked files produce a pending import list (name, kind, remove, reorder) that the user confirms or cancels; a single wav/ogg with no BGM asks "stem or sound"; a single mp3 defaults to sound; several files always show the list.
- **Read first:** `src/sys/metadata/metadata_drop_state.cpp`, `src/ui/nuklear/compose_resources.cpp` (file dialog callback), `src/ui/nuklear/audio/audio_state.h`, `src/sys/audio/audio_project_import.h`.
- **Files owned:** new `src/ui/nuklear/sound_import_state.cpp/.h`, new `tests/ui/sound_import_state_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Pure state machine: add paths, classify by extension and chart-has-BGM, edit, confirm, cancel. 2. Tests for each rule above, bounds (1,000 files), duplicate paths, non-audio files rejected with a message.
- **Invariants:** no filesystem access in the state; plain C types at the `frontend.h` boundary.
- **Reject/failure behavior:** cancel leaves the pending list empty and the document untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.|^documentation\.'`.
- **Out of scope:** drawing the dialog (T-329), background copy and probe jobs.

## T-327 Share a zoomable waveform viewport

- **Status:** done
- **Implementation notes (2026-10-02):** `waveform_viewport_t` with `fit`, `clamp`, `zoom` (anchored, 1/16 to whole-source frames per pixel), `pan` and `project_waveform_viewport` is in `include/wormedit/audio/waveform_viewport.h`; the 6 cases in `tests/core/audio_waveform_viewport_tests.cpp` pass (audio.* 137/137), including an 80-minute source. clang-tidy could not be run on the new file (include path missing for the new unit in the compile database); the functions are small.
- **Outcome:** a viewport type (first visible source frame, source frames per pixel) with anchored zoom, drag pan, clamping and fit-to-window, plus a projection helper that picks the pyramid level through `waveform_level_for_zoom` and `project_waveform`; used by the Sounds tab now and available to Compose's stem-cut editor later.
- **Read first:** `include/wormedit/audio/waveform.h`, `src/audio/waveform.cpp`, `src/sys/waveform_view_projection.cpp`, `src/sys/audio/waveform_analysis_runtime.cpp`.
- **Files owned:** new `include/wormedit/audio/waveform_viewport.h`, new `src/audio/waveform_viewport.cpp`, new `tests/core/audio_waveform_viewport_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Pure arithmetic with 64-bit frames and exact clamping. 2. Zoom about a pixel keeps the frame under it fixed (tested at several levels). 3. Projection wrapper bounded to the visible pixel count. 4. Tests for zero-length, one-frame, 80-minute (about 2.3e8 frames at 48 kHz) sources and extreme zoom.
- **Invariants:** deterministic, no allocation beyond the output span, no decoder calls.
- **Reject/failure behavior:** invalid inputs return an empty projection and leave the viewport unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|^documentation\.'`.
- **Out of scope:** Compose razor integration (T-333), sample-level PCM rendering.

## T-328 Project the sound list and selected-sound properties

- **Status:** done
- **Implementation notes (2026-10-02):** `project_sound_list` and `project_sound_properties` (`src/sys/sound_list_projection.h`) are pure and tested headless (4 cases): audio resources only, case-insensitive search and exact tag filter, row windowing with clamped start, use counts from notes and BGM events, inferred kind (stem for music/BGM-only resources). The plain-C view structs in `frontend.h` and the `workspace_frame_view` wiring were left to T-329, which draws them and needs the SDL3 build to check; that avoids adding uncompiled glue now.
- **Outcome:** the audio-slices view exposes bounded sound rows (name, kind, tags, duration, sample rate, use count, missing flag) filtered by search text and tag, plus the selected sound's properties and its viewport; rows are built only for the visible range.
- **Read first:** `src/sys/project/workspace_frame_view.cpp`, `src/sys/waveform_view_projection.cpp`, `src/sys/compose/views/compose_resource_presenter.cpp`, `src/ui/frontend.h` (`WormUiAudioSlicesView`).
- **Files owned:** new `src/sys/sound_list_projection.cpp/.h`, new `tests/sys/sound_list_projection_tests.cpp`, `src/sys/project/workspace_frame_view.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Add plain-C row and property structs and the selection/scroll fields. 2. Projection with filtering and use counts. 3. Tests: 10,000 rows project only the window; filters; stable selection across revisions.
- **Invariants:** existing `WormUiAudioSlicesView` fields keep their meaning so the offset drag still works.
- **Reject/failure behavior:** an invalid view fails `worm_ui_nuklear_audio_slices_view_valid`, never undefined reads.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^sys\.|^frontend-state\.|^documentation\.'`.
- **Out of scope:** drawing (T-329).

## T-329 Redraw the Sounds tab: listbox, waveform column, properties column

- **Status:** done
- **Final verification:** normal/Release builds, 3,321/3,321 full CTest checks and 445 production event cases (6,173 assertions) pass; affected default clang-tidy, documentation and whitespace checks pass. Evidence: `/tmp/sounds-complete-{build,release,ctest,events}.log`.
- **Completion (2026-10-11):** The live Audio/Slices workspace has adaptive three-column layout with side margins, a virtual searchable list, Up/Down selection and Space/double-click audition. The shared waveform viewport supports pointer-anchored wheel zoom, Fit/+/- and left-drag pan; Shift-drag retains offset editing. Name/kind changes are staged with Apply/Revert and one revision-checked Undo, preserving tags. The clipped content area reuses existing import review. Modal/input guards and resize/context cancellation prevent click-through and stale gestures. Native authenticated TCP Xvfb captures verify 1280/1600 layouts, audition, zoom, pan and saved rename; narrow 800 px production tests cover gesture hints.
- **Done so far (2026-10-03):** `WormUiSoundRowView` rows in `frontend.h` (cap 512), filled from `project_sound_list` in `workspace_projection.cpp`, and a three-column layout (searchable list, waveform, read-only properties) with side margins in `audio_panel.cpp`. At the time it was compiled only as a syntax check against Nuklear master, plus the headless suite (1225/1225).
- **Native audit (2026-10-11):** `/tmp/sounds-native-before.png` confirms the live three-column tab renders, but its fixed-height columns leave excess empty space, the waveform only adjusts offset, the list lacks keyboard audition and properties remain read-only. The old smoke script has no audio mode; isolated authenticated TCP Xvfb capture used instead. Coordinator supporting scope: `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/waveform_view_projection.cpp/.h`, `src/sys/application_frame_projection.cpp`, `src/sys/app/main.cpp`, `src/sys/compose/actions/compose_actions.cpp`, core resource sound-metadata name support and focused production event/sys tests. Existing import review/drop machinery is audited and reused; tags/slicer remain excluded.
- **Outcome:** the tab shows a searchable listbox (click, Up/Down, double click or Space audition), a central zoomable waveform (wheel zoom about the pointer, drag pan, offset drag kept), and a right column with properties and side margins; dropping files opens the T-326 dialog.
- **Read first:** `src/ui/nuklear/audio/audio_panel.cpp`, `src/ui/nuklear/theme.cpp`, `src/ui/nuklear/metadata/metadata_panel.cpp` (margin pattern), `src/ui/nuklear/compose_resources.cpp`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp/.h`, `src/ui/nuklear/audio_state.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/app/main.cpp` (action wiring only), `tests/ui/nuklear_audio_state_tests.cpp`. Supporting ownership (2026-10-11): `src/ui/frontend.h`, `src/sys/waveform_view_projection.cpp/.h`, `src/sys/application_frame_projection.cpp`, `src/sys/compose_sound_actions.cpp/.h`, `src/sys/compose/actions/compose_actions.cpp`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `tests/core/resource_assignment_tests.cpp`, `tests/sys/sound_properties_actions_tests.cpp`, `tests/sys/audio_waveform_projection_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/ui/audio_sounds_workflow_event_harness_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `scripts/smoke-compose-virtual-x.sh`.
- **Steps:** 1. Three-column layout with theme tokens. 2. Listbox with keyboard model. 3. Waveform with the T-327 viewport. 4. Property fields and the import dialog. 5. Action wiring through the standard pattern.
- **Invariants:** The production Nuklear frontend is built and exercised by event tests; state logic remains separately tested.
- **Reject/failure behavior:** a missing file shows a row marker, not an empty crash.
- **Verify:** headless tests, then on the user's SDL3 build `scripts/smoke-compose-virtual-x.sh /tmp/out.png audio` and open the PNG.
- **Out of scope:** slicer controls (T-332), tag editing (T-330).

## T-330 Edit and filter sound tags

- **Status:** done (2026-10-20)
- **Verification:** Add/remove chips, case-insensitive duplicate handling, tag-only edits, bounded rejection and filtering are covered through core/metadata and production-input tests. GATE passes 4,318/4,318, whitespace and changed-C++ complexity (`/tmp/wormedit-gate.aCMmTI`); manual instructions are in `sounds-and-layers.adoc`.
- **Outcome:** the properties column edits tags as chips plus a text field, and the list filters by tag.
- **Read first:** `src/ui/nuklear/audio/audio_panel.cpp`, `include/wormedit/core/resource_assignment.h`.
- **Files owned:** `src/ui/nuklear/audio/audio_panel.cpp`, `src/ui/nuklear/audio_state.cpp/.h`, `src/sys/app/main.cpp` (action wiring only), `tests/ui/nuklear_audio_state_tests.cpp`.
- **Supporting ownership (2026-10-20):** `src/ui/frontend/actions.h`, `src/ui/frontend/metadata_audio.h`, `src/sys/project/workspace_view.cpp`, `src/sys/compose/actions/compose_sound_actions.cpp`, `tests/sys/sound_properties_actions_tests.cpp`: the current metadata route lives in Compose sound actions rather than main; carry structured tags through the view and an optional owned C++ tag payload through that route, with command integration coverage.
- **Supporting ownership (UI coverage):** `tests/ui/audio_sounds_workflow_event_harness_tests.cpp` exercises the production chip/add emitters and their separation from unapplied name drafts.
- **Steps:** 1. Commit through `apply_resource_sound_metadata`. 2. Tag filter state. 3. Tests for add, remove, duplicate and limit messages.
- **Invariants:** one history step per tag edit.
- **Reject/failure behavior:** a rejected edit shows the diagnostic and keeps the old tags.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`.
- **Out of scope:** autocomplete.

## T-331 Compute slice boundaries for a long audio export (Woslicer-style)

- **Status:** done
- **Implementation notes (2026-10-02):** `compute_audio_slices` (`include/wormedit/core/audio_slicer.h`) returns contiguous exact ranges using portable `wide_math` (no `__int128`), with floor/ceil/nearest-even rounding, lead-in option and a slice cap; 7 cases pass. No WAV writer exists in the repo, so the render job moved to T-334.
- **Outcome:** a pure function takes BPM (exact rational), beat division, start offset, source frame count and sample rate and returns exact, non-overlapping frame ranges.
- **Read first:** `include/wormedit/core/timing.h`, `include/wormedit/core/sound_slice.h`, `include/wormedit/audio/decoder.h`, `src/core/sound_slice.cpp`.
- **Files owned:** new `include/wormedit/core/audio_slicer.h`, new `src/core/audio_slicer.cpp`, new `tests/core/audio_slicer_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Boundary math with the same rounding as `sample_rounding_t`. 2. Tests: 120 BPM sixteenths over 10 s, negative offset, non-integer BPM, a tail shorter than one slice, slice-count limit (default 10,000).
- **Invariants:** no drift: boundary N is computed from N, never accumulated.
- **Reject/failure behavior:** zero or negative BPM, division or offset past the source returns diagnostics and no ranges.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|^documentation\.'`.
- **Out of scope:** MIDI-driven cutting and other bmhelper functions (later cards), UI (T-332).

## T-334 Render slice ranges to WAV files in a background job

- **Status:** done
- **Implementation notes (2026-10-02):** `encode_wav_pcm16` and `render_audio_slices` (`include/wormedit/audio/slice_render.h`) cut the decoder's 44.1 kHz stereo output at `audio_slice_range_t` ranges and publish `<base>_001.wav` files with `save_file_atomic`; they validate the ranges, honor a `std::stop_token`, report progress and delete every published file on failure or cancel (3 audio cases pass against the ogg fixture). Scheduling on the job system is left to T-332, which owns the UI flow; compute the ranges with `sample_rate` 44100.
- **Outcome:** a bounded job decodes the source once and writes each `audio_slice_range_t` as a 16-bit stereo WAV file into the project, reporting progress and honoring cancellation.
- **Read first:** `include/wormedit/core/audio_slicer.h`, `include/wormedit/audio/decoder.h`, `include/wormedit/core/resource_file_copy.h`, `src/audio/waveform_job.cpp`.
- **Files owned:** new `include/wormedit/audio/wav_writer.h`, new `src/audio/wav_writer.cpp`, new `include/wormedit/audio/slice_render_job.h`, new `src/audio/slice_render_job.cpp`, new `tests/core/audio_slice_render_tests.cpp`, `cmake/dependencies/miniaudio.cmake`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Write the WAV writer (RIFF header, little-endian PCM, atomic publish like `resource_file_copy`). 2. Stream-decode and cut at the exact frames. 3. Tests: round trip through the decoder, sample-exact boundaries, cancellation leaves no partial files.
- **Invariants:** the source file is never modified; output names are deterministic (`<name>_001.wav`).
- **Reject/failure behavior:** a failed or cancelled job removes every file it created.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|^core\.|^documentation\.'`.
- **Out of scope:** UI (T-332), MIDI-driven cuts.

## T-332 Add the slicer controls to the Sounds tab

- **Status:** ready
- **Outcome:** with a sound selected the user enters BPM, division and offset, sees the cut lines on the zoomable waveform, and "Slice" creates the resulting sounds in one bulk import (T-325) as keysounds.
- **Read first:** `src/ui/nuklear/audio/audio_panel.cpp`, `include/wormedit/core/audio_slicer.h`, `include/wormedit/core/sound_import.h`.
- **Files owned:** `src/ui/nuklear/audio/audio_panel.cpp`, `src/ui/nuklear/audio_state.cpp/.h`, `src/sys/app/main.cpp` (action wiring only), `tests/ui/nuklear_audio_state_tests.cpp`.
- **Steps:** 1. Fields and cut-line preview. 2. Background slicing job with progress. 3. Bulk import of the outputs. 4. Tests for field validation.
- **Invariants:** the source sound is never modified.
- **Reject/failure behavior:** a failed job imports nothing and reports the first error.
- **Verify:** headless tests plus the visual check from T-329 on the user's build.
- **Out of scope:** automatic placement of slices in Compose.

## T-333 Use the shared waveform viewport in the Compose stem-cut editor

- **Status:** deferred (was: blocked-on T-327)
- **Outcome:** Compose's BMSON razor editing zooms the stem waveform with the same viewport semantics.
- **Read first:** `src/sys/bmson_waveform_projection.cpp`, `include/wormedit/audio/waveform_viewport.h`.
- **Files owned:** to be listed when un-deferred; touches Compose files owned by other threads.
- **Steps:** to be planned when the user names this card.
- **Invariants:** the shared mobile-head viewport contract in Compose stays unchanged.
- **Reject/failure behavior:** not applicable until planned.
- **Verify:** to be planned.
- **Out of scope:** everything in T-324–T-332.

## T-335 Add the Compose keysound listbox with drop import and layer groups

- **Status:** done (2026-10-03)
- **Outcome:** Compose shows a Sounds column beside the inspector: a sticky toolbar (Paint, Play, Preview on click, Add, Remove, Select all, Clear selection, Layers) and search above a virtualized, Ctrl/Shift multi-selectable list of the catalog's samples; selecting a row arms it for the existing Place pointer mode, audio files dropped on the column are added as keysounds in one undo step, Remove deletes unused sounds, and the Layers strip adds or removes a semantic layer for the whole selection so painted notes join those layers.
- **Implementation notes (2026-10-03, Claude):** the user chose per-sound defaults stored in the document and an own column rather than the T-228 dock. Layer IDs are chart-local, so the mapping is `chart_document_t::sound_layer_defaults` (written to the native project only when non-empty, so no schema bump); layer and sound deletion clean it, chart cloning remaps it, and `validate_document` rejects dangling entries. Locked layers cannot be added, and a layer locked later is skipped at placement. Core: `apply_sound_layer_defaults`, `apply_sound_layer_membership`, `apply_resource_deletions` (all-or-nothing). Application: `compose_sound_actions.cpp` (bulk import with audio probing, skips unreadable or already-listed files), `process_sound_audition_request`, drop collection in `poll_frontend_events`. Frontend: `compose_sounds_panel.cpp` plus the pure `keysound_list_state.cpp`. Verified with 20 new unit cases, an isolated Xvfb run on a 40-sound fixture (rows, chips, selection, armed outline, layer strip, audition), and the full suite.
- **Read first:** `src/ui/nuklear/compose_sounds_panel.cpp`, `src/sys/compose/actions/compose_sound_actions.cpp`, `include/wormedit/core/semantic_layer_editor.h`, `src/sys/sound_list_projection.h`.
- **Files owned:** the files above plus `src/ui/nuklear/keysound_list_state.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/frontend.h`.
- **Steps:** none remaining.
- **Invariants:** sound IDs and BMS `#WAV` slots of other sounds never change; one undo step per command.
- **Reject/failure behavior:** a referenced sound, a locked layer, an unknown ID or a stale revision rejects the whole batch with the document unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure` (all pass); `scripts/smoke-compose-virtual-x.sh` style run against a project with keysounds.
- **Out of scope:** keyboard navigation of the list (Up/Down/Delete need a list-focus input owner), drag-stroke painting of sounds, dragging rows onto the chart, an OS-drop hover highlight, removing the old Samples tree from the inspector (T-229/T-232), physical drop from a file manager (only the pending-path handling is exercised).
