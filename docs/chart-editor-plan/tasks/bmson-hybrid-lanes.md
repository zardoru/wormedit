# BMSON hybrid lanes: stem lanes beside packed keysound columns (2026-10-07 decisions)

[Back to the task card index](../tasks.md).

BMSON charts made of samples, including those converted from BMS, can exceed
the classic BMS sample limit. Today the BMSON importer (`src/formats/bmson.cpp`,
"Every stem gets its BGM column") gives every sound channel its own BGM lane, so
such charts open with hundreds of lanes, and a BMS→BMSON conversion with over a
thousand. The BMSON writer builds `sound_channels` from each event's sound and
its `sound_channel_event` link, not from BGM lanes, so the lane layout is purely
a display and editing model.

User decisions (2026-10-07):

- **Hybrid, BMSON only.** A BMSON chart shows stem lanes and packed keysound columns side by side. Each stem lane belongs to one sound channel. Packed columns behave like BMS BGM columns.
- **Classifying channels on open.** A file carries no record of the author's intent, so the file itself decides. A channel with any `c = true` note is a stem. Every other channel is packed.
- **The user states the intent at import.** The Sounds dock has an Add per section. Adding a stem creates its channel and lane at once, with no events. Adding a keysound only makes the sound available for drops on packed columns.
- **Stems are placed by hand.** A stem's first point is drawn by the user. Nothing is placed at measure 1 automatically.
- **Clicking a stem lane with Draw:**
  - outside the stem's audio span: restart point, with or without Shift;
  - inside a span: continue point;
  - Shift+click inside a span: restart point.

  Continue is the common case, so it gets the plain click.
- **Sharing one audio file** between a stem resource and a keysound resource is allowed, but nothing is built for it.
- **BMS charts:** "stem" is only an import label, with no stem lanes, because BMS cannot express continuation.
- **Stem lanes can be resized horizontally.** A wide stem lane shows its file name instead of `BGM n`.

Sequencing: the Draw tool ([T-412](draw-tool.md)) lands before T-463, and the verb audit ([T-453](bmson-bga-editing.md)) before T-461, so that the new verbs use the shared path.

## T-457 Model stem lanes and packed columns side by side

- **Status:** done
- **Outcome:** a BMSON chart can hold stem lanes, each linked explicitly to one sound channel, next to packed columns that hold any channel's events. A stem channel with no events is valid. Core tests cover the native round trip, a migration from schema N−1, and BMSON export of an empty stem channel.
- **Read first:** `include/wormedit/core/document.h` (`bgm_source_lane_t`, `sound_channel_t`), `src/formats/bmson.cpp` (channel and lane construction), `src/formats/bmson_writer.cpp` (`channel_builder_t`), `src/sys/bmson_waveform_projection.cpp` (`stem_source_lane`), `src/formats/native_project_migrations.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/bmson.cpp` (existing per-channel explicit links only; classification remains T-458), `src/formats/native_project_storage.cpp` (schema-5 recovery envelope parity), `tests/formats/native_project_storage_tests.cpp` (recovery schema regression), `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/formats/bmson_writer.cpp`, `src/core/sound_slice.cpp`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/stem_waveform_window.h`, `tests/formats/native_project_tests.cpp`, `tests/formats/bmson_writer_tests.cpp`, `tests/core/sound_slice_rules_tests.cpp` (exact empty-channel derivation regression), `tests/sys/stem_window_tests.cpp` (existing indexed-lane fixture now records its explicit channel link), `tests/sys/application_commands_tests.cpp` (root-approved fixture-only BMSON-to-BMS setup clears stem links before command validation), this card.
- **Steps:**
  1. Add `std::optional<object_id_t> stem_channel` to `bgm_source_lane_t`. A lane with it set is a stem lane, and a lane without it is a packed column.
  2. Migration: older BMSON-family charts link each lane to the channel whose index equals its `source_order`. That reproduces today's layout exactly.
  3. Replace the `source_order` lookup in `stem_source_lane` with the explicit link.
  4. Make an empty stem channel valid everywhere: slice derivation returns no slices, the waveform shows no rows, and the writer emits `"notes": []` with a validation warning "stem <name> has no points".
  5. Write tests: native round trip, migration equality, empty-channel export, and an event in a packed column that keeps its channel on export.
- **Invariants:** BMSON export of every existing fixture is byte-identical. BMS charts are unchanged.
- **Reject/failure behavior:** a stem lane that names a missing channel fails validation on load with the chart and lane named, and nothing is repaired silently.
- **Evidence (2026-10-07 isolated worker):** normal dependency-tracked core/converter builds pass; requested core/BMSON/native plus documentation CTest passes 1517/1517. Model/export/slice focused group passes 57 assertions in 13 cases. Schema 4 → 5 preserves old BMSON lane IDs, names, ordering and event links while adding explicit channel IDs; BMS lanes stay unlinked. Missing/ambiguous links reject with chart/lane names. Empty derivation already worked and needed no algorithm rewrite. Configured clang-tidy adds no violations and preserves baseline scores, including immutable JSON headers and existing Catch functions.
- **Post-integration fixture check:** root full-suite validation identified two command fixtures switching an imported BMSON chart to BMS while retaining explicit stem links. The approved fixture-only conversion helper now clears those links, preserving channels/events and all assertions. Normal frame-timing rebuild and the two exact command tests pass (6 assertions); configured clang-tidy matches baseline scores across both compile contexts with no new violation.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|bmson|native'`; `clang-tidy -p build` on the changed sources.
- **Out of scope:** classification (T-458) and UI.

## T-458 Classify BMSON channels on open: continuation points make a stem, the rest pack

- **Status:** done
- **Outcome:** opening a `.bmson` gives a stem lane to each channel with any `c = true` note and puts all other channels' BGM events into packed columns using the [T-423 allocation rule](bms-editing.md). Each resource's `sound_kind` is stored as stem or keysound. `kick_your_ass2.bmson` opens with 9 stem lanes; `fx`, `hat` and `pc` go into packed columns. A BMSON converted from `iberai_dragback/_i.bms` opens with at most as many columns as the BMS had.
- **Read first:** `src/formats/bmson.cpp` (`append_sound_note`, lane construction), [T-423](bms-editing.md) (`bgm_conversion.h` allocator, if landed), `src/sys/sound_list_projection.cpp` (`effective_sound_kind`), `include/wormedit/core/document.h`.
- **Files owned:** `src/formats/bmson.cpp`, `include/wormedit/core/bgm_packing.h` (root-approved pure header-only group-aware allocator; T-423 is not landed), `tests/formats/bmson_tests.cpp`, `tests/formats/native_project_tests.cpp` (root-approved fixture-only legacy-lane and unspecified-kind setup), `tests/fixtures/bmson/hybrid-three-stems-200-keysounds.bmson`, this card.
- **Execution ownership (2026-10-07):** Sol owns importer classification, allocator wiring, resource kinds and fixture generation; Luna exclusively owns the pure `bgm_packing.h` allocator and its unit cases in `tests/formats/bmson_tests.cpp`. Sol owns all build writes and review.
- **Steps:**
  1. Classify each channel during import: stem if any note has `c = true`, otherwise packed.
  2. Stem channels keep today's lane. Packed channels' BGM events go through the allocator. The column count is the allocator's result, and columns are named `BGM 1…n`.
  3. Store `sound_kind` on each channel's resource: stem or keysound.
  4. Add a fixture with 3 stem channels and 200 one-shot channels. Assert the lane counts, the kinds, and an export byte-identical to the input apart from key order.
- **Invariants:** playable notes and every event's channel are unchanged. Only the BGM display lanes differ.
- **Reject/failure behavior:** if the allocator rejects, the chart falls back to one lane per channel (today's behavior), with a warning.
- **Evidence (2026-10-07 isolated worker):** normal core/converter builds pass. BMSON/packing group passes 1146 assertions in 80 cases, including the canonical 3-stem + 200-keysound fixture with exact export bytes. The broad core/documentation gate passed 1521/1523 before revealing two stale native fixtures; both corrected fixtures then pass 14 assertions in 2 cases. Legacy migration setup now constructs old per-channel lanes independently of current import, and omission checks explicitly use unspecified kinds. Configured clang-tidy reports no new or increased owned-code complexity violations; existing Catch scores and immutable JSON header warnings are recorded. Read-only saved real-chart converter probes produce Kick 9 stem + 3 packed lanes with byte-identical 45956-byte export, and converted dragback 0 stem + 6 packed columns against 16 original BMS columns.
- **Verify:** `ctest --test-dir build --output-on-failure -R bmson`.
- **Out of scope:** long one-shot waveforms (T-459) and keeping packed channels out of the stem pipeline (T-460).

## T-459 Draw long one-shot waveforms in their packed column

- **Status:** blocked-on T-458
- **Outcome:** a packed BGM event whose sample lasts longer than one measure at the current tempo draws its waveform down its own column, from the event to the end of the sample or the next event in that column. In Kick Your Ass, `fx`, `hat` and `pc` show their waveforms over the whole song.
- **Read first:** `src/sys/bmson_waveform_projection.cpp`, `src/sys/stem_waveform_window.h`, `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_stem_piece`), `src/sys/waveform_analysis_runtime.cpp`.
- **Files owned:** `src/sys/bmson_waveform_projection.cpp`, `src/sys/stem_waveform_window.h`, `src/sys/waveform_analysis_runtime.cpp`, `src/ui/frontend.h` (piece key per event), `src/ui/nuklear/compose_canvas.cpp`, projection tests, this card.
- **Steps:**
  1. Project a waveform piece per long packed event, keyed by event ID in the T-443 cache.
  2. Clip each piece at the next event in the same column.
  3. Short one-shots draw no waveform.
  4. Test the length threshold, clipping at the next event, and the cache key staying stable while scrolling.
- **Invariants:** stem lanes draw exactly as they do today.
- **Reject/failure behavior:** a sample that has not been decoded yet shows the pending band.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'stem|bmson'`; an isolated capture of Kick Your Ass at measure 30.
- **Out of scope:** stem slicing UI.

## T-460 Keep packed channels out of the stem pipeline

- **Status:** done
- **Outcome:** only channels with a stem lane enter `visible_stem_channels`, stem waveform analysis and slice derivation. On the converted dragback chart, the per-frame stem work is the same as for a chart with no stems.
- **Read first:** `src/sys/stem_waveform_window.h`, `src/sys/waveform_analysis_runtime.cpp` (`wanted_stem_assets`), `src/sys/bmson_waveform_projection.cpp` (`drawn_stems`).
- **Files owned:** those three, plus `tests/sys/stem_window_tests.cpp`, `tests/sys/frame_timing_tests.cpp` (production filtered projection and explicit-link fixtures), `tests/sys/audio_notification_tests.cpp` (actual filtered runtime asset requests and explicit-link fixtures), this card.
- **Execution ownership (2026-10-07):** Sol owns the three production files, stem-window and frame-timing tests, and all build writes. Luna exclusively owns runtime request regressions and explicit-link fixtures in `tests/sys/audio_notification_tests.cpp`.
- **Steps:**
  1. Filter channels by whether they have a stem lane, in the per-revision stem index from T-439.
  2. Test a chart with 3 stems and 1000 packed channels: the index holds 3 channels, and no analysis is requested for a packed asset.
- **Invariants:** stem-only charts behave exactly as they do today.
- **Reject/failure behavior:** none beyond the existing behavior.
- **Evidence (2026-10-07 isolated worker):** normal core, frame-timing and audio target builds pass. Stem-window group passes 7693 assertions in 10 cases, including 300 visible stems, filtered 3-stem + 1000-packed index positions, conservative error fallbacks, and the 181-stem benchmark. Production waveform projection passes 162074 assertions in 20 cases; mixed projection accepts both the filtered index and an intentionally unfiltered caller list, keeps three stable stem keys, and rejects selected packed slice derivation even with a ready packed source analysis. Actual worker requests pass 95 assertions in 14 cases: three real stems finish, all 1000 packed assets stay outside the cache, selected packed markers start no channel job, and notifications stay empty. Configured clang-tidy adds no new or increased violations; the exact baseline warning-score multiset is unchanged. Generic transport/selected asset waveform code is unchanged and its existing positive worker/projection checks pass.
- **Verify:** `ctest --test-dir build --output-on-failure -R stem`.
- **Out of scope:** T-459's per-event waveforms, which use their own path.

## T-461 Make stem and Pack: convert a channel between its stem lane and packed columns

- **Status:** blocked-on T-458, T-453
- **Outcome:** the BGM lane header menu offers "Pack into columns" on a stem lane. A packed event's context menu offers "Make stem", which gathers its channel's events into a new stem lane. Changing a sound's kind in Sounds properties runs the same command. Each is one undo step.
- **Read first:** T-453's approved verb table, the T-423 allocator, `src/sys/compose_note_to_bgm.cpp`, `src/ui/nuklear/compose_note_menu.cpp`.
- **Files owned:** a new `src/core/bmson_lane_mode.cpp` and `include/wormedit/core/bmson_lane_mode.h`, `src/sys/compose_input.cpp` (or the T-455 shared dispatcher), `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp` (kind change), `src/ui/frontend.h`, `CMakeLists.txt`, core and event-harness tests, this card.
- **Steps:**
  1. Core command `set_bmson_channel_lane_mode(channel, stem | packed)`. Make stem creates the lane and moves the channel's BGM events into it. Pack runs the allocator and removes the lane. Each sets the resource's `sound_kind`.
  2. Pack is refused for a channel that has continuation points, with the reason "continuation points need a stem lane"; offer "Convert continue points to restarts and pack" as a separate confirmed step.
  3. Add the menu items and the kind change in Sounds properties.
  4. Test round trips: Make stem then Pack is equal to the input, and refusal is atomic.
- **Invariants:** the exported channels are byte-identical after each command.
- **Reject/failure behavior:** an allocator rejection changes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'lane_mode|bmson'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** BMS charts.

## T-462 Add stems and keysounds from their own Sounds dock sections

- **Status:** blocked-on T-457
- **Outcome:** on a BMSON chart, the Sounds dock's Stems section has "Add stem…" and its Keysounds section has "Add keysound…". Adding a stem creates the resource, its empty channel and its stem lane, and places no event. Adding a keysound creates only the resource. The review dialog's kind is fixed to the section the user added from. On BMS charts, the stem kind is only a label.
- **Read first:** `src/core/sound_import.cpp` (`append_stem`, `import_entry`), `src/sys/compose_sound_actions.cpp` (the two `apply_bulk_sound_import` calls), `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/sound_import_dialog.cpp`, `src/ui/nuklear/sound_import_state.cpp`.
- **Files owned:** those five, plus `include/wormedit/core/sound_import.h`, `src/ui/frontend.h`, `tests/core/sound_import_tests.cpp`, `tests/ui/` import state tests and the event harness, this card.
- **Steps:**
  1. `append_stem` creates the channel and its stem lane with no event and no marker, and does so only for BMSON charts.
  2. Each section passes its kind into the import plan. The dialog shows the kind read-only.
  3. The quick add path (`compose_sound_actions.cpp`, which passes `.chart = {}`) is used only for keysounds. A stem add always passes the chart.
  4. Test: a stem added to a BMSON chart has a lane and zero events; a stem added to a BMS chart has neither; adding a keysound creates no lane.
- **Invariants:** BMS bulk import and slot allocation are unchanged.
- **Reject/failure behavior:** a failure in one entry rejects the whole import atomically, as it does today.
- **Verify:** `ctest --test-dir build --output-on-failure -R sound_import`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** placing points (T-463).

## T-463 Place stem points and packed one-shots with the Draw tool

- **Status:** blocked-on T-412, T-457
- **Outcome:** with Draw active:
  - **On a stem lane:** a click outside the stem's audio span adds a restart point, with or without Shift. A click inside a span adds a continue point, and Shift+click inside a span adds a restart point. An empty stem lane accepts its first point.
  - **Dragging a stem row from the Sounds dock onto its own lane** adds a restart point at the drop row.
  - **On a packed column:** Draw places a one-shot into the current keysound source's channel, creating the channel on first use. With a stem as the current source, Draw on a packed column is disabled, and its tooltip names the stem's lane.

  In Select mode, a click on empty space in a stem lane only deselects.
- **Read first:** `src/ui/nuklear/compose_object_input.cpp` (`add_bmson_stem_point`, `stem_lane_reference_marker`), `src/sys/compose_input.cpp` (`BMSON_STEM_POINT_*` dispatch), `include/wormedit/core/bmson_sound_channel_editor.h` (`insert_bmson_bgm_marker`), `include/wormedit/core/sound_slice.h`, the T-412 card.
- **Files owned:** those four source files, plus `src/core/bmson_sound_channel_editor.cpp`, `src/ui/frontend.h`, `src/sys/compose_view.cpp` (draw-source projection), core tests and the event harness, this card.
- **Steps:**
  1. Core: `insert_bmson_stem_point(channel, position, prefer_restart)`. The command decides the point kind from the derived slices: it is a restart if the row is outside every sounding span or `prefer_restart` is set, and a continue point otherwise. It takes the channel ID, so it works on an empty channel without a reference marker.
  2. Move the stem-lane click from Select to Draw. Shift becomes `prefer_restart`.
  3. Add the drag from a Sounds dock stem row, and Draw on packed columns with a keysound source.
  4. Write tests:
     - core: a point inside a span, inside a span with Shift, after the end of the audio, before the first point, and on an empty channel;
     - event harness: one case per gesture, each followed by undo.
- **Invariants:** existing points, razor gestures and slice-point drags are unchanged.
- **Reject/failure behavior:** a point on an existing point's row is rejected with the reason, and the document is unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'bmson|stem'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** razor mode.

## T-464 Resize stem lanes horizontally and label wide ones by file name

- **Status:** blocked-on T-457
- **Outcome:** dragging the right edge of a stem lane header resizes that lane from 36 px (the current width) up to 480 px, and double-clicking the edge restores 36 px. The waveform and cut lines scale with the lane. A lane 96 px or wider shows its stem's file name, truncated with an ellipsis, instead of `BGM n`, and the header tooltip always shows the full name. Packed columns keep a fixed width.
- **Read first:** `src/sys/compose_view.h` (`compose_bgm_lane_width_pixels`), `src/core/editor_scene.cpp` (BGM lane bounds), `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_bgm_lane_labels`, `draw_compose_stem_piece`), `src/sys/compose_view.cpp` (BGM pane width).
- **Files owned:** those four, plus `include/wormedit/core/editor_scene.h`, `src/ui/frontend.h` (per-lane width and label), `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_object_input.cpp` (edge drag), scene tests, frontend-state tests and the event harness, this card.
- **Steps:**
  1. Replace the single `bgm_lane_width_pixels` in the scene request with a per-lane width span, defaulting to 36 px. Lane bounds accumulate left to right, and hit testing uses the accumulated bounds.
  2. Store the widths as frontend view state keyed by (project, chart, lane ID), and keep them for the session. They are not stored in the document, so they never affect undo or export.
  3. Add the edge drag and double-click reset, with a resize cursor on hover.
  4. Label: a stem lane at least 96 px wide shows its channel's file name; a narrower lane or a packed column shows `BGM n`.
  5. Write tests: scene bounds with mixed widths, hit testing on a wide lane, the edge drag, and an event-harness case that resizes, places a point in the widened lane, and resets.
- **Invariants:** at default widths the scene is pixel-identical to today. The horizontal BGM scroll from T-11 covers the widened pane.
- **Reject/failure behavior:** a width outside the allowed range is clamped.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'scene|compose'`; `ctest --test-dir build -L event-harness`; an isolated capture of Kick Your Ass with one stem lane at 300 px.
- **Out of scope:** saving the widths in the project file (a later card, if wanted).
