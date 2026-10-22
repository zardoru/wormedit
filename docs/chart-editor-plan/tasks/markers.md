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
- **Files owned:** `src/ui/nuklear/compose/compose_canvas.cpp`, user-approved scope extension (2026-10-02): `src/ui/nuklear/compose/compose_bookmark_label.cpp`, `src/ui/nuklear/frontend.cpp` (font atlas glyph ranges); `tests/ui/compose_bookmark_label_tests.cpp` (new), `tests/CMakeLists.txt`.
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

- **Status:** deferred (was: ready; user request 2026-10-19, not immediate work)
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
