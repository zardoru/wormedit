# Task cards: Sounds tab (library, bulk import, zoomable waveform, slicer)

[Back to the task-card index](../tasks.md).

User request (Zar, 2026-10-02), redesigning the workspace tab `audio_slices`
("Sonidos", `src/ui/nuklear/audio_panel.cpp`). Today it shows a grid of
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

## T-313 Add sound kind and free tags to the resource catalog

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
- **Out of scope:** tag UI (T-319), export of tags.

## T-314 Import many sounds in one history step

- **Status:** ready
- **Outcome:** `apply_bulk_sound_import` registers N probed audio files as assets and resources of a chosen kind in one command; stems also create a BMSON sound channel with a restart event at position 0 and its semantic BGM marker (via the `insert_bmson_bgm_marker` path), so they appear in Compose; undo removes all of it at once.
- **Read first:** `include/wormedit/core/resource_assignment.h` (`apply_resource_asset_import`, `next_resource_asset_id`), `include/wormedit/core/bmson_sound_channel_editor.h`, `include/wormedit/core/resource_file_copy.h`, `src/sys/audio_project_import.cpp`.
- **Files owned:** new `include/wormedit/core/sound_import.h`, new `src/core/sound_import.cpp`, new `tests/core/sound_import_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Define the request (ordered list of path, name, kind, copied project path) and result with per-file diagnostics.
  2. Validate every entry (limits, duplicate names resolved deterministically, catalog capacity) before mutating anything.
  3. Apply stems, keysounds (allocating BMS identifiers when the chart is BMS-family) and samples.
  4. Tests: 1 and 50 files, mixed kinds, capacity failure, undo, a stem visible in the BGM projection.
- **Invariants:** list order is the catalog order; file copying stays outside the command (caller reserves IDs as `import_resource_asset_action_t` already does).
- **Reject/failure behavior:** one invalid entry rejects the whole batch with all diagnostics.
- **Verify:** as T-313 plus `-R 'sound_import'`.
- **Out of scope:** dialog and drag-drop (T-315, T-318), extracting a stem into slices (T-320).

## T-315 Model the "Import as…" dialog and drop handling

- **Status:** blocked-on T-314
- **Outcome:** dropped or picked files produce a pending import list (name, kind, remove, reorder) that the user confirms or cancels; a single wav/ogg with no BGM asks "stem or sound"; a single mp3 defaults to sound; several files always show the list.
- **Read first:** `src/sys/metadata_drop_state.cpp`, `src/ui/nuklear/compose_resources.cpp` (file dialog callback), `src/ui/nuklear/audio_state.h`, `src/sys/audio_project_import.h`.
- **Files owned:** new `src/ui/nuklear/sound_import_state.cpp/.h`, new `tests/ui/sound_import_state_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Pure state machine: add paths, classify by extension and chart-has-BGM, edit, confirm, cancel. 2. Tests for each rule above, bounds (1,000 files), duplicate paths, non-audio files rejected with a message.
- **Invariants:** no filesystem access in the state; plain C types at the `frontend.h` boundary.
- **Reject/failure behavior:** cancel leaves the pending list empty and the document untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.|^documentation\.'`.
- **Out of scope:** drawing the dialog (T-318), background copy and probe jobs.

## T-316 Share a zoomable waveform viewport

- **Status:** done
- **Implementation notes (2026-10-02):** `waveform_viewport_t` with `fit`, `clamp`, `zoom` (anchored, 1/16 to whole-source frames per pixel), `pan` and `project_waveform_viewport` is in `include/wormedit/audio/waveform_viewport.h`; the 6 cases in `tests/core/audio_waveform_viewport_tests.cpp` pass (audio.* 137/137), including an 80-minute source. clang-tidy could not be run on the new file (include path missing for the new unit in the compile database); the functions are small.
- **Outcome:** a viewport type (first visible source frame, source frames per pixel) with anchored zoom, drag pan, clamping and fit-to-window, plus a projection helper that picks the pyramid level through `waveform_level_for_zoom` and `project_waveform`; used by the Sounds tab now and available to Compose's stem-cut editor later.
- **Read first:** `include/wormedit/audio/waveform.h`, `src/audio/waveform.cpp`, `src/sys/waveform_view_projection.cpp`, `src/sys/waveform_analysis_runtime.cpp`.
- **Files owned:** new `include/wormedit/audio/waveform_viewport.h`, new `src/audio/waveform_viewport.cpp`, new `tests/core/audio_waveform_viewport_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Pure arithmetic with 64-bit frames and exact clamping. 2. Zoom about a pixel keeps the frame under it fixed (tested at several levels). 3. Projection wrapper bounded to the visible pixel count. 4. Tests for zero-length, one-frame, 80-minute (about 2.3e8 frames at 48 kHz) sources and extreme zoom.
- **Invariants:** deterministic, no allocation beyond the output span, no decoder calls.
- **Reject/failure behavior:** invalid inputs return an empty projection and leave the viewport unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|^documentation\.'`.
- **Out of scope:** Compose razor integration (T-322), sample-level PCM rendering.

## T-317 Project the sound list and selected-sound properties

- **Status:** blocked-on T-313
- **Outcome:** the audio-slices view exposes bounded sound rows (name, kind, tags, duration, sample rate, use count, missing flag) filtered by search text and tag, plus the selected sound's properties and its viewport; rows are built only for the visible range.
- **Read first:** `src/sys/workspace_frame_view.cpp`, `src/sys/waveform_view_projection.cpp`, `src/sys/compose_resource_presenter.cpp`, `src/ui/frontend.h` (`WormUiAudioSlicesView`).
- **Files owned:** new `src/sys/sound_list_projection.cpp/.h`, new `tests/sys/sound_list_projection_tests.cpp`, `src/sys/workspace_frame_view.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Add plain-C row and property structs and the selection/scroll fields. 2. Projection with filtering and use counts. 3. Tests: 10,000 rows project only the window; filters; stable selection across revisions.
- **Invariants:** existing `WormUiAudioSlicesView` fields keep their meaning so the offset drag still works.
- **Reject/failure behavior:** an invalid view fails `worm_ui_nuklear_audio_slices_view_valid`, never undefined reads.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^sys\.|^frontend-state\.|^documentation\.'`.
- **Out of scope:** drawing (T-318).

## T-318 Redraw the Sounds tab: listbox, waveform column, properties column

- **Status:** blocked-on T-317
- **Outcome:** the tab shows a searchable listbox (click, Up/Down, double click or Space audition), a central zoomable waveform (wheel zoom about the pointer, drag pan, offset drag kept), and a right column with properties and side margins; dropping files opens the T-315 dialog.
- **Read first:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/theme.cpp`, `src/ui/nuklear/metadata_panel.cpp` (margin pattern), `src/ui/nuklear/compose_resources.cpp`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp/.h`, `src/ui/nuklear/audio_state.cpp/.h`, `src/ui/nuklear/frontend.cpp`, `src/sys/main.cpp` (action wiring only), `tests/ui/nuklear_audio_state_tests.cpp`.
- **Steps:** 1. Three-column layout with theme tokens. 2. Listbox with keyboard model. 3. Waveform with the T-316 viewport. 4. Property fields and the import dialog. 5. Action wiring through the standard pattern.
- **Invariants:** Nuklear code is not compiled in the cloud; state logic stays in the tested state files.
- **Reject/failure behavior:** a missing file shows a row marker, not an empty crash.
- **Verify:** headless tests, then on the user's SDL3 build `scripts/smoke-compose-virtual-x.sh /tmp/out.png audio` and open the PNG.
- **Out of scope:** slicer controls (T-321), tag editing (T-319).

## T-319 Edit and filter sound tags

- **Status:** blocked-on T-318
- **Outcome:** the properties column edits tags as chips plus a text field, and the list filters by tag.
- **Read first:** `src/ui/nuklear/audio_panel.cpp`, `include/wormedit/core/resource_assignment.h`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/audio_state.cpp/.h`, `src/sys/main.cpp` (action wiring only), `tests/ui/nuklear_audio_state_tests.cpp`.
- **Steps:** 1. Commit through `apply_resource_sound_metadata`. 2. Tag filter state. 3. Tests for add, remove, duplicate and limit messages.
- **Invariants:** one history step per tag edit.
- **Reject/failure behavior:** a rejected edit shows the diagnostic and keeps the old tags.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`.
- **Out of scope:** autocomplete.

## T-320 Compute slice boundaries for a long audio export (Woslicer-style)

- **Status:** done
- **Implementation notes (2026-10-02):** `compute_audio_slices` (`include/wormedit/core/audio_slicer.h`) returns contiguous exact ranges using portable `wide_math` (no `__int128`), with floor/ceil/nearest-even rounding, lead-in option and a slice cap; 7 cases pass. No WAV writer exists in the repo, so the render job moved to T-323.
- **Outcome:** a pure function takes BPM (exact rational), beat division, start offset, source frame count and sample rate and returns exact, non-overlapping frame ranges.
- **Read first:** `include/wormedit/core/timing.h`, `include/wormedit/core/sound_slice.h`, `include/wormedit/audio/decoder.h`, `src/core/sound_slice.cpp`.
- **Files owned:** new `include/wormedit/core/audio_slicer.h`, new `src/core/audio_slicer.cpp`, new `tests/core/audio_slicer_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Boundary math with the same rounding as `sample_rounding_t`. 2. Tests: 120 BPM sixteenths over 10 s, negative offset, non-integer BPM, a tail shorter than one slice, slice-count limit (default 10,000).
- **Invariants:** no drift: boundary N is computed from N, never accumulated.
- **Reject/failure behavior:** zero or negative BPM, division or offset past the source returns diagnostics and no ranges.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.|^documentation\.'`.
- **Out of scope:** MIDI-driven cutting and other bmhelper functions (later cards), UI (T-321).

## T-323 Render slice ranges to WAV files in a background job

- **Status:** blocked-on T-320
- **Outcome:** a bounded job decodes the source once and writes each `audio_slice_range_t` as a 16-bit stereo WAV file into the project, reporting progress and honoring cancellation.
- **Read first:** `include/wormedit/core/audio_slicer.h`, `include/wormedit/audio/decoder.h`, `include/wormedit/core/resource_file_copy.h`, `src/audio/waveform_job.cpp`.
- **Files owned:** new `include/wormedit/audio/wav_writer.h`, new `src/audio/wav_writer.cpp`, new `include/wormedit/audio/slice_render_job.h`, new `src/audio/slice_render_job.cpp`, new `tests/core/audio_slice_render_tests.cpp`, `cmake/dependencies/miniaudio.cmake`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Write the WAV writer (RIFF header, little-endian PCM, atomic publish like `resource_file_copy`). 2. Stream-decode and cut at the exact frames. 3. Tests: round trip through the decoder, sample-exact boundaries, cancellation leaves no partial files.
- **Invariants:** the source file is never modified; output names are deterministic (`<name>_001.wav`).
- **Reject/failure behavior:** a failed or cancelled job removes every file it created.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.|^core\.|^documentation\.'`.
- **Out of scope:** UI (T-321), MIDI-driven cuts.

## T-321 Add the slicer controls to the Sounds tab

- **Status:** blocked-on T-318, T-323
- **Outcome:** with a sound selected the user enters BPM, division and offset, sees the cut lines on the zoomable waveform, and "Slice" creates the resulting sounds in one bulk import (T-314) as keysounds.
- **Read first:** `src/ui/nuklear/audio_panel.cpp`, `include/wormedit/core/audio_slicer.h`, `include/wormedit/core/sound_import.h`.
- **Files owned:** `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/audio_state.cpp/.h`, `src/sys/main.cpp` (action wiring only), `tests/ui/nuklear_audio_state_tests.cpp`.
- **Steps:** 1. Fields and cut-line preview. 2. Background slicing job with progress. 3. Bulk import of the outputs. 4. Tests for field validation.
- **Invariants:** the source sound is never modified.
- **Reject/failure behavior:** a failed job imports nothing and reports the first error.
- **Verify:** headless tests plus the visual check from T-318 on the user's build.
- **Out of scope:** automatic placement of slices in Compose.

## T-322 Use the shared waveform viewport in the Compose stem-cut editor

- **Status:** deferred (was: blocked-on T-316)
- **Outcome:** Compose's BMSON razor editing zooms the stem waveform with the same viewport semantics.
- **Read first:** `src/sys/bmson_waveform_projection.cpp`, `include/wormedit/audio/waveform_viewport.h`.
- **Files owned:** to be listed when un-deferred; touches Compose files owned by other threads.
- **Steps:** to be planned when the user names this card.
- **Invariants:** the shared mobile-head viewport contract in Compose stays unchanged.
- **Reject/failure behavior:** not applicable until planned.
- **Verify:** to be planned.
- **Out of scope:** everything in T-313–T-321.
