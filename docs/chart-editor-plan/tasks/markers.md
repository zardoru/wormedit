# Task cards: bookmark shortcuts and labels

[Back to the task-card index](../tasks.md).

Quick bookmark work from the keyboard while composing or listening, and
readable bookmark names in the Compose field (user request, 2026-10-01).
Bookmark editing already exists in the Markers panel
(`src/ui/nuklear/compose/compose_markers.cpp`) and marker navigation in core
(`include/wormedit/core/marker_navigation.h`). Apply the stale-plan check
(rule 9) first.

## T-308 Jump between bookmarks with Primary+Up/Down

- **Status:** done
- **Implementation notes (2026-10-02):** `navigation.previous_bookmark` / `navigation.next_bookmark` (Primary+Up/Down) are registered; `navigate_beat_bookmark` in `src/sys/compose_bookmark_commands.cpp` is tested headless (cursor jump, bookmarks only, one seek while playing). Step 2 was not needed: the Compose key handler already returns early for Ctrl/Cmd chords, so only the registry shortcut fires. The complete app build and 1,163 core/frontend-state/documentation/bookmark tests pass. Isolated Xvfb verified next/previous bookmark jumps in both scroll directions and playback continuing after a shortcut seek. The user-approved helper extension now searches from the live playhead during playback (floor forward, ceil backward for fractional rows), preserving the edit cursor. Consecutive live shortcut jumps were verified in Xvfb; new forward/fractional-backward regressions pass. Full integrated suite: 1,561/1,561.
- **Outcome:** in Compose, Primary+Up/Down (Ctrl, ⌘ on macOS) jumps to the previous or next beat bookmark in screen direction (downscroll flips them, like plain Up/Down); stopped or paused it moves the edit cursor and view to the bookmark row; while playing it seeks the playhead to the bookmark and playback continues.
- **Request (user, 2026-10-01):** "a ctrl+uparrow/downarrow to jump bookmarks".
- **Current behavior (2026-10-01):** Up/Down ignore modifiers (`compose_navigation_action_for_key`, `src/ui/nuklear/compose/compose_input_panel.cpp`), so Primary+Up/Down moves the cursor one snap. Alt+Left/Right (`navigation.previous_marker` / `next_marker`) already jump to bookmarks *and* section boundaries.
- **Stale-plan check:** grep `previous_bookmark` in `src/core/command_registry.cpp`.
- **Read first:** `src/ui/nuklear/compose/compose_input_panel.cpp` (`compose_navigation_action_for_key`, `process_compose_navigation_key`), `include/wormedit/core/marker_navigation.h` (`marker_navigation_filter_t`), `src/sys/compose/actions/compose_actions.cpp` (`navigate_audio_marker`), `src/core/command_registry.cpp` (`navigation.next_marker`).
- **Files owned:** `src/core/command_registry.cpp`, `tests/core/command_registry_tests.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `tests/sys/compose_marker_navigation_tests.cpp` (new), `tests/CMakeLists.txt`; user-approved extension (2026-10-02): `src/sys/compose_bookmark_commands.cpp`, `tests/sys/compose_bookmark_commands_tests.cpp`.
- **Steps:**
  1. Register `navigation.previous_bookmark` "Previous Bookmark" and `navigation.next_bookmark` "Next Bookmark" (Compose tab scope) with Primary+Up and Primary+Down. Run `diagnose_shortcut_conflicts` in a registry test; on a conflict, **STOP**.
  2. In `process_compose_navigation_key`, handle Up/Down with only the primary modifier before plain cursor movement: map the visual direction with `compose_visual_navigation_direction` and emit the bookmark action instead of a cursor step.
  3. Dispatch with `navigate_markers` and a filter that includes bookmarks only. Stopped or paused: move the cursor to the bookmark row as `navigation.next_marker` does. Playing: convert the bookmark row to a frame with `timing_at` and call `transport_seek`, applying the T-241 clamp.
  4. Tests: next/previous in upscroll and downscroll; section boundaries are skipped; no bookmark in that direction leaves the cursor unchanged and shows "No next bookmark." / "No previous bookmark."; while playing, exactly one seek to the bookmark frame.
- **Invariants:** plain Up/Down, PageUp/PageDown, and Alt+Left/Right behave as before; the document never changes.
- **Reject/failure behavior:** a failed seek while playing keeps playback at the prior position with the existing seek message.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^frontend-state\.|^documentation\.|compose_marker_navigation'`; `clang-tidy -p build` on each changed `.cpp`; isolated Xvfb check that Primary+Down moves the cursor to the next bookmark in upscroll and the previous one in downscroll.
- **Out of scope:** audio-time bookmarks (they join once T-77 step 3 passes the timing index), section navigation, playhead skip by snap or measure (T-307).

## T-309 Add a bookmark at the cursor or playhead with Primary+B

- **Status:** done
- **Implementation notes (2026-10-02):** `markers.add_bookmark` (Primary+B) is registered; `add_beat_bookmark_here` (cursor, or nearest snap row to the playhead while playing, duplicate row refused) is tested headless including undo. The real Xvfb/SDL-dummy run confirmed the bookmark edit stops playback because the revision changes. The user-approved extension now advances only the matching compiled preview revision after accepted bookmark addition and successful rebind; active playback, chart identity and exact prior revision are checked. Xvfb/SDL-dummy verified bookmark insertion while playback advances from 0:02 to 0:03. New compiled-preview revision guards and the full 1,561-test suite pass.
- **Outcome:** in Compose, Primary+B adds an unnamed beat bookmark at the edit cursor; while playing it adds it at the active-snap row nearest the playhead and playback continues; the new bookmark can be named later in the Markers panel.
- **Request (user, 2026-10-01):** "ctrl+b to place a bookmark".
- **Stale-plan check:** grep `markers.add_bookmark` in `src/core/command_registry.cpp`.
- **Read first:** `src/sys/compose/actions/compose_actions.cpp` (`make_beat_bookmark_edit`, `edit_beat_bookmark`), `src/ui/nuklear/compose/compose_markers.cpp` (`apply_bookmark_buttons`), `src/core/command_registry.cpp`, `src/sys/compose/views/compose_view.h` (`preview_playhead_absolute_row`).
- **Files owned:** `src/core/command_registry.cpp`, `tests/core/command_registry_tests.cpp`, `src/sys/application_commands.h/.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `tests/sys/application_commands_tests.cpp`; user-approved preview-preservation extension (2026-10-02): `src/sys/compose/views/compose_view.h`, `src/sys/app/main.cpp`, `src/sys/chart_preview_state.h/.cpp`, `tests/sys/sdl_preview_output_tests.cpp`.
- **Steps:**
  1. Register `markers.add_bookmark` "Add Bookmark" (Compose tab scope) with Primary+B; run `diagnose_shortcut_conflicts` in a registry test; on a conflict, **STOP**. Check that Primary+B does not also reach a lane key that is mapped to B (modified keys must not place notes); if it does, **STOP**.
  2. Dispatch the existing `WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK` path with an empty label. Stopped or paused: at `state.cursor`. Playing: at the active-snap row nearest `preview_playhead_absolute_row` (ties go to the earlier row).
  3. Confirm that playback keeps going after the edit (the document revision changes). If chart preview stops or restarts on any document edit, **STOP** and report; do not change the preview in this card.
  4. Tests: stopped adds at the cursor; playing adds at the nearest snap row; a second Primary+B on a row that already has a bookmark adds nothing and reports "A bookmark already exists on this row."; undo removes it.
- **Invariants:** bookmark IDs and existing bookmarks unchanged; one undo step per bookmark.
- **Reject/failure behavior:** a virtual BMS cursor row or an active gesture rejects with the existing messages; nothing is added.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.|^documentation\.|application_commands'`; `clang-tidy -p build` on each changed `.cpp`; isolated Xvfb/SDL-dummy check that Primary+B during playback adds a bookmark and the playhead keeps advancing.
- **Out of scope:** naming the bookmark on creation, audio-time bookmarks, deleting by shortcut.

## T-310 Show bookmark names on their lines in the Compose field

- **Status:** done
- **Implementation notes (2026-10-02):** playable-field alignment, shared prior-label overlap tracking, strict scalar UTF-8 rejection and the U+2026 atlas glyph are verified. The full application build passed; the focused core/frontend/documentation/osu filter passed 1156/1156. Actual isolated Xvfb captures in both scroll directions show named beat/audio labels above their lines, no text for unnamed bookmarks, and long labels ending in a rendered ellipsis. Clang-tidy passed for the canvas, helper, font-atlas frontend and tests. After correcting stale Kiai gesture test inputs, the complete 1548-test checkpoint passed (`/tmp/wormedit-after-kiai-full.log`). Captures: `/tmp/wormedit-t310/direction-1.png` and `/tmp/wormedit-t310/direction-2.png`.
- **Outcome:** every visible beat and audio-time bookmark line with a non-empty name shows that name as small text just above the line at the right edge of the playable field, in the line's color; long names end in "…"; empty names show nothing.
- **Request (user, 2026-10-01):** "a little bit of text on the bookmark line to read whatever name one puts on it."
- **Current behavior (2026-10-01):** `draw_compose_beat_bookmarks` and `draw_compose_audio_bookmarks` (`src/ui/nuklear/compose/compose_canvas.cpp`) draw lines only, although `WormUiComposeBookmarkView::label` and `WormUiComposeAudioBookmarkView::label` already carry the names.
- **Stale-plan check:** grep `field_text` inside `draw_compose_beat_bookmarks`.
- **Read first:** `src/ui/nuklear/compose/compose_canvas.cpp` (`draw_compose_beat_bookmarks`, `draw_compose_audio_bookmarks`, `field_text`, the note-label text drawing), `src/ui/frontend.h` (bookmark views).
- **Files owned:** `src/ui/nuklear/compose/compose_canvas.cpp`, user-approved scope extension (2026-10-02): `src/ui/nuklear/compose/compose_bookmark_label.cpp`, `src/ui/nuklear/app/frontend.cpp` (font atlas glyph ranges); `tests/ui/compose_bookmark_label_tests.cpp` (new), `tests/CMakeLists.txt`.
- **Steps:**
  1. A pure helper that, given label width, font height, line y, field bounds, and the previous label's box, returns the text box or none: right-aligned to the field's right edge with 4 px padding, sitting on the line, truncated with "…" to at most half the field width.
  2. Draw with `field_text` in the line color on the same layer as the line. Skip a label whose box would overlap the previous drawn label (keep the earlier one).
  3. Downscroll and upscroll both place the text above the line on screen.
  4. Tests for the helper: fits, truncates, empty name, overlap skip, a label near the top edge stays inside the field.
- **Invariants:** lines draw exactly as before; label text is a view of the document and never changes it.
- **Reject/failure behavior:** a null or invalid UTF-8 label draws nothing for that bookmark.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.|compose_bookmark_label|^documentation\.'`; `clang-tidy -p build src/ui/nuklear/compose/compose_canvas.cpp`; isolated Xvfb capture of a chart with a named, an unnamed, and a very long bookmark, then open the PNG.
- **Out of scope:** section labels, editing names in the field, label color preferences.

## T-729 Plan selection-created export-cut sections

- **Status:** done (2026-10-24: owner picked it up; design answered as [open question 27](../decisions/open-questions.md); implementation split into T-756–T-759 below)
- **Audit (step 2, 2026-10-24):** the area menu's Create section (`compose_area_menu.cpp`, T-200) and `compose_bookmark_actions.cpp` already turn a selection into a `section_t` anchored to beats or to an audio-millisecond span through `marker_editor.h`. However, sections are per chart (`chart_document_t::sections`) while cuts are song-wide, so a cut is a new song-level list that reuses the selection-to-audio-span conversion and not a flag on `section_t`. `compose_dead_spans.cpp` already builds the warp/pause dead-span overlay that the cut styling extends.
- **Outcome:** A reviewed design and bounded implementation cards cover creating a section from a selection, marking it as a song interval cut out on export, skipping it entirely in ordinary Compose waveform/timeline/playback/navigation, and exposing its distinct dead-span overlay only in Timing focus mode.
- **Read first:** `docs/chart-editor-plan/editing/metadata-interop.md`; `src/sys/compose/actions/compose_bookmark_actions.cpp`; `src/ui/nuklear/compose/compose_area_menu.cpp`; `src/sys/compose/compose_dead_spans.cpp`; `src/sys/project/output_group_export.cpp`.
- **Files owned:** `docs/chart-editor-plan/editing/metadata-interop.md`, `docs/chart-editor-plan/tasks/markers.md`, `docs/chart-editor-plan/tasks.md`; future implementation-card files only after recording their paths here.
- **Steps:**
  1. Start only when the user explicitly picks up T-729; preserve the current immediate-work queue.
  2. Audit the existing Create section area-menu action (T-200) and section editor; identify what selection-to-section behavior can be reused.
  3. Resolve the design questions below with the owner before assigning implementation defaults.
  4. Write separate bounded cards for section persistence/actions, export transformation/audio handling, Compose cut-timeline projection/playback/navigation, and Timing-focus visualization, with exact file ownership and verification.
- **Invariants:** The saved feature request includes selection-to-section creation, an export-cut designation, Timing-focus-only cut-out visualization, warp dead-span-like styling and a dedicated color distinct from warps. Ordinary Compose collapses each cut out of the waveform and displayed timeline, with playback/navigation skipping it as though it never existed. Timing focus exposes the original interval for editing. Mode switches retain the full authored source song and cut definitions; exported songs exclude designated intervals. Ordinary section behavior remains available.
- **Reject/failure behavior:** Unresolved policies keep implementation deferred; an eventual rejected cut/export must preserve the source project and previously published output.
- **Verify:** For this planning record, `ctest --test-dir build/release -R '^documentation\.chart_editor_state$' --output-on-failure` and `git diff --check` pass. Future implementation cards must specify GATE, native-project round-trip/Undo checks, export/audio boundary cases, continuous Compose waveform/skip playback/navigation checks, and mode-switch/Timing-focus visibility/color checks.
- **Design questions for pickup:** Are cuts song-wide across difficulties or scoped to charts/output groups? How are exported audio and chart timelines spliced/rebased together? What happens to holds, keysounds/BGM, BGA and timing state crossing a cut, and to overlapping/adjacent cuts? How does Timing-focus audition handle the original intervals (ordinary Compose skips them)? What selection and anchoring rules apply?
- **Out of scope:** Implementing the feature or selecting it as immediate work (explicit user deferral, 2026-10-19); final audio, boundary and export policies await the design pass above. Existing ordinary section creation is covered by T-200.

## T-756 Persist song-wide export cuts and create them from a selection

- **Status:** done (2026-10-25)
- **Implementation notes (2026-10-25):** Song-wide cuts persist in native schema22 with migration, bounded validation and global stable identities. Area-menu/Markers creation, label/interval editing, deletion and Undo/Redo use the marker transaction. Full GATE passes 5,176/5,176 tests and 88 changed-TU complexity checks without allowance increases; final event-harness and native round-trip coverage pass.
- **Implementation ownership (coordinator, 2026-10-25):** Core: `include/wormedit/core/export_cut.h`, `src/core/export_cut.cpp`, `src/core/id_ceiling.h`, native migration owner `src/formats/native_project_migrations.cpp`; focused tests `tests/core/export_cut_editor_tests.cpp`, `tests/formats/native_project_export_cut_tests.cpp`. Sys/UI: new `src/sys/compose/actions/compose_export_cut_actions.cpp`, route/internal headers, new `src/ui/nuklear/compose/compose_export_cuts.cpp/.h`, `compose_markers.cpp`, new cut projection file and `compose_view_internal.h`, `compose_view.cpp`, `workspace_view.cpp`, frontend compose/state contracts. Shared audio-span projection/capture is generalized rather than copied. Tests extend `tests/sys/compose_action_notification_tests.cpp` and new `tests/ui/export_cut_event_harness_tests.cpp`. Root owns CMake registration and manual/status/architecture updates.
- **Outcome:** `song_document_t` owns an ordered list of export cuts, each with an ID, an optional label and a source-song audio-millisecond interval ([open question 27](../decisions/open-questions.md)). The area menu offers Create export cut next to Create section. Cuts can be renamed, moved, resized and deleted with one Undo step each, and native projects round-trip them (schema bump with migration).
- **Read first:** `include/wormedit/core/document.h` (`song_document_t`, `section_t`), `include/wormedit/core/marker_editor.h`, `src/sys/compose/actions/compose_bookmark_actions.cpp` (the `ADD_AUDIO_SECTION` path), `src/ui/nuklear/compose/compose_area_menu.cpp`, `src/formats/native_project.cpp`, `docs/architecture.md`.
- **Files owned:** this card, `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/marker_editor.h`, `src/core/marker_editor.cpp`, `src/formats/native_project.cpp`, `src/sys/compose/actions/compose_bookmark_actions.cpp`, `src/ui/nuklear/compose/compose_area_menu.cpp`, `src/ui/frontend/actions.h`, `tests/core/marker_editor_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:**
  1. Stale-plan check: `grep -rn "export_cut" include src`.
  2. Add the song-level type and validation (positive length, nonnegative start, bounded count; overlap allowed).
  3. Add create/edit/delete commands through the shared transaction, reusing the selection-to-audio-span conversion of audio sections. With the Compose snap toggle on, edges come from snapped positions; with it off, from the exact pointer or selection time.
  4. Add the area-menu entry and a Markers-panel list.
  5. Native round trip, migration from the previous schema and Undo/Redo tests.
- **Invariants:** cuts never change chart content, timing or audio; ordinary sections behave as before.
- **Reject/failure behavior:** an empty, inverted or unmaterialized interval rejects with an inline reason and no edit.
- **Verify:** GATE; event-harness test for the area-menu entry.
- **Out of scope:** export (T-757), Compose skipping (T-758), Timing-focus drawing (T-759).

## T-757 Apply export cuts to exported audio and charts

- **Status:** done (2026-10-25)
- **Implementation notes (2026-10-25):** Common conversion rebases detached flat and bounded scoped outcomes, stages collision-safe cut audio at one 48 kHz clock, and preserves authored media/publication on rejection. Exact retained pause durations and SPEED pieces are checked against native timing semantics; genuinely unrepresentable carry positions reject with an object target. SM/SSC reuse the measure owner to arrange exact retained rows into standard bars. BMS, osu!mania, SM and SSC round trips pass; BMSON continuations and preview audio use the same preparation/validation owner. Full GATE passes 5,176/5,176 and 88 changed-TU complexity checks; optimized GCC compilation passes all 27 changed core/format units.
- **Implementation ownership (coordinator, 2026-10-25):** Pure transformation: `include/wormedit/core/export_cut_transform.h`, `src/core/export_cut_transform.cpp`, `src/core/export_cut_transform_internal.h`, optional scoped implementation split. Exact comparison is exposed by a thin `timing_arithmetic.h`/`timing.cpp` wrapper; bounded reachable evaluations by `bms_program_edit.h/.cpp`. All callers use `src/formats/conversion.cpp/.h`; decoded/staged audio uses new `src/formats/export_cut_audio.cpp/.h`, `slice_span_render.cpp`, `conversion_publication*.cpp`, shared `include/wormedit/audio/slice_render.h` and `src/audio/slice_render.cpp`, CLI `src/tools/wormedit_convert.cpp`, plus `src/sys/audio/preview_audio_export.cpp`. One canonical 48 kHz generated export clock keeps integer-ms anchors exact across every chart and music asset; no-cut exports retain the old path. Shared derived frame intervals live in core `export_cut_frames.h/.cpp`, also consumed by live preview at the output-device rate. Unknown/crossing media extents and genuinely unrepresentable conditional timing reject with targeted diagnostics; publication remains atomic. Tests include core transform, audio splicing, output-group, and preview export coverage.
- **Supporting ownership extension (coordinator, 2026-10-25):** `src/core/export_cut_transform_scoped.cpp` preserves bounded conditional BMS outcomes using the existing structure builder; `bms_native_content_internal.h` exposes its normalized event comparison. Writer guards cover `bms_program_document.cpp`, `bmson_writer.cpp`, `osu_mania.cpp`, and `stepmania.cpp`. Export diagnostics retain object identity through `conversion.h/.cpp`, `output_group_export.h/.cpp` and `resource_job_state.cpp`, with focused coverage in `tests/sys/resource_notification_tests.cpp`; conversion round trips extend `tests/formats/conversion_tests.cpp`, and scoped cases live in `tests/core/export_cut_scoped_tests.cpp`. Shared WAV/fade primitives are exposed by `slice_render.h/.cpp` and `offline_mix.h/.cpp` rather than duplicated. Preview audio reuses diagnostics-only common conversion preparation, fresh slice-source inspection and a fresh Lua evaluation scope; end-to-end cases extend `tests/core/audio_preview_export_tests.cpp`. The isolated visual script updates its Snap fill probe for the current toolbar. Manual and architecture updates belong to the coordinator.
- **Final supporting owners (coordinator, 2026-10-25):** `include/wormedit/core/measure_editor.h` and `src/core/measure_editor.cpp` expose exact rebar through the existing lift/place owner; `src/core/filename_reservations.h` and `src/formats/slice_spans.cpp` share generated-filename reservations. `include/wormedit/audio/audition_resolver.h`/`src/audio/audition_resolver.cpp` own effective per-chart Music selection for both `chart_preview_state.cpp` and preview export. Exact row/seam consumers and their regression coverage additionally extend `tests/sys/sound_selection_tests.cpp`, `tests/sys/sdl_preview_output_tests.cpp` and `tests/sys/audio_transport_notification_tests.cpp`.
- **Outcome:** every export target removes the union of the song's cuts. The exported song audio is spliced once, and every chart's notes, timing and events are rebased identically. Objects fully inside a cut are dropped with a diagnostic. Holds, keysounds/BGM and BGA that cross a cut boundary reject the export with targeted diagnostics. Timing state in effect at a cut end carries over. The source audio and the authored project are never modified.
- **Read first:** `src/sys/project/output_group_export.cpp`, `src/sys/audio/preview_audio_export.cpp` (audio render/encode path), `include/wormedit/core/notifications.h` (diagnostic targets, T-224), [open question 27](../decisions/open-questions.md).
- **Files owned:** this card, `src/sys/project/output_group_export.cpp`, `src/sys/project/output_group_export.h`, `include/wormedit/core/export_cut.h` (new), `src/core/export_cut.cpp` (new), `tests/core/export_cut_tests.cpp` (new), `tests/sys/output_group_export_tests.cpp`. The first step must name the exact audio-splice and chart-rebase consumers and record extensions here.
- **Steps:**
  1. Audit how exports read song audio and chart timing today; record the consumer list.
  2. Build one pure core transformation from a song and its cuts to a rebased export document plus diagnostics.
  3. Splice and encode the cut audio once per export, in the existing export job.
  4. Boundary tests: cut at song start or end, adjacent and overlapping cuts, a BPM change inside a cut, a stop or warp at a boundary, a hold crossing a boundary, and BGM or BGA starting inside a cut.
- **Invariants:** an export without cuts is byte-identical to today's; the source project and published outputs stay unchanged on rejection.
- **Reject/failure behavior:** crossing objects block the whole export with diagnostics that select the offending object; there is no partial publication.
- **Verify:** GATE; export round-trip tests for BMS, osu!mania and SM/SSC with cuts.
- **Out of scope:** Compose display (T-758, T-759); clipping holds or sounds (rejected by question 27).

## T-758 Skip export cuts in ordinary Compose

- **Status:** done (2026-10-25)
- **Implementation notes (2026-10-25):** One source-time union feeds row and physical-frame adapters. Field projection, picking, grid navigation, overview/waveforms, seeks and prepared callback schedules use the same seams. Sparse pause-boundary visibility and half-open warp ends retain exact event semantics, while no-cut arithmetic preserves its previous precision. Full GATE passes 5,176/5,176 and 88 changed-TU complexity checks. Actual isolated Compose and Timing-focus captures were inspected; ordinary Compose collapses the cut and Timing focus retains the original source interval.
- **Implementation ownership (coordinator, 2026-10-25):** Shared row geometry: new `include/wormedit/core/export_cut_axis.h`, `include/wormedit/core/editor_axis.h`, `src/core/export_cut_axis.cpp`; `interaction_state.h`, `interaction_cursor_pose.h`, core interaction geometry/cursor/selection/hit-test/dispatch and `editor_scene.cpp`. Projection: `compose_view.cpp/.h`, new `compose_cut_axis_view.cpp`, `compose_view_internal.h`, `compose_scene_view.cpp`, `compose_selection_view.cpp`, `compose_waveform_overlays.cpp`, related scalar marker projections. Shared waveform requests/sampling: `include/wormedit/audio/aligned_waveform.h`, `src/audio/aligned_waveform.cpp`, sys waveform/BMSON waveform owners. Playback: shared `include/wormedit/core/export_cut_frames.h`/`src/core/export_cut_frames.cpp`, `timing.h`, `include/wormedit/audio/preview_bridge.h`/`src/audio/preview_bridge.cpp`, new `src/sys/audio/export_cut_preview.cpp/.h`, chart preview state and transport frame/player services. Prepared schedules split voices around cuts; the audio callback keeps one contiguous collapsed frame clock, and source editing identities remain unchanged. Root handles direct UI row geometry sites after audit. Tests cover the axis, continuous playback, navigation, waveform seams and mode changes.
- **Outcome:** outside Timing focus, each cut collapses out of the waveform and the displayed timeline, and playback, seeking and navigation pass over it as though it never existed. Mode switches keep the full source song and the cut definitions.
- **Read first:** `src/sys/compose/views/compose_view.cpp`, `src/sys/compose/views/compose_waveform_overlays.cpp`, `src/sys/compose/compose_scroll_geometry.h`, `src/sys/audio/audio_transport_frame.cpp`, `src/sys/audio/transport_player_actions.cpp`, `src/ui/compose_mode.h`.
- **Files owned:** this card; the first step must name the exact projection, transport and navigation consumers and record them here before edits.
- **Steps:**
  1. Audit how Compose maps audio time to screen rows and how transport seeks; record the consumers.
  2. Introduce one cut-timeline mapping owned by a single type and route projection, waveform, playback and navigation through it.
  3. Event-harness tests for scrolling, seeking and bookmark jumps across a cut; continuous-playback test across a cut.
- **Invariants:** with no cuts, projection and transport are unchanged; editing still targets source positions.
- **Reject/failure behavior:** a cut covering the whole song leaves Compose on the full timeline and reports why.
- **Verify:** GATE; isolated Xvfb capture of a collapsed cut.
- **Out of scope:** Timing focus (T-759); export (T-757).

## T-759 Show export cuts in Timing focus

- **Status:** done (2026-10-25)
- **Implementation notes (2026-10-25):** Timing focus shows dedicated pink hatched bands and plays the original source clock. Existing audio-span capture now also handles cut edges, with snap conversion on release, Escape/capture cancellation and one marker transaction. Frame/input and production event-harness checks pass, including exact untouched endpoints and existing preview-drag behavior. Full GATE passes 5,176/5,176 and 88 changed-TU complexity checks; isolated final captures in both modes were inspected.
- **Implementation ownership (coordinator, 2026-10-25):** Root owns the common audio-span capture in `compose_state.h`, `compose_input_panel.cpp`, shared registry cancellation, and `compose_canvas.cpp`; new `src/sys/compose/input/compose_export_cut_input.cpp` plus existing input dispatch/internal headers commit only on release through the marker transaction. Cut projection is in new `src/sys/compose/views/compose_export_cut_view.cpp` with the existing audio-span projector reused; only Timing focus exposes its geometry. `src/ui/nuklear/render/theme.h/.cpp` and the bundled field scheme add a distinct `export_cut` color. Existing `compose_dead_spans.h/.cpp` mapping is adapted to the shared source axis where necessary. Tests extend `tests/sys/frame_timing_tests.cpp` and add `tests/ui/export_cut_drag_event_harness_tests.cpp`; existing preview capture behavior remains covered.
- **Outcome:** Timing focus shows each cut over its original interval with warp-dead-span styling in a dedicated color that is distinct from warps, plays the original audio through it, and allows editing the cut's edges.
- **Read first:** `src/sys/compose/compose_dead_spans.cpp`, `src/ui/compose_mode.h`, `src/ui/nuklear/compose/compose_canvas.cpp`, the color scheme files from [color-schemes.md](color-schemes.md).
- **Files owned:** this card, `src/sys/compose/compose_dead_spans.cpp`, `src/sys/compose/compose_dead_spans.h`, `src/ui/nuklear/compose/compose_canvas.cpp`; the first step must name the theme and gesture files and record them here.
- **Steps:**
  1. Audit the dead-span view and theme ownership.
  2. Add the cut kind with its own theme color; draw it only in Timing focus.
  3. Add edge-drag editing through the shared gesture runner, snapping edges when the snap toggle is on, with Escape cancel and one Undo step.
  4. Mode-switch tests: cuts are visible only in Timing focus and the transport plays through them there.
- **Invariants:** warp and pause spans are unchanged; ordinary Compose does not draw cuts.
- **Reject/failure behavior:** a drag that would produce an empty or inverted cut is clamped to the minimum length or cancelled.
- **Verify:** GATE; isolated Xvfb captures in both modes.
- **Out of scope:** export; ordinary Compose skipping.
