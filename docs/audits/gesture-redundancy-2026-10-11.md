# Gesture and verb redundancy audit (2026-10-11)

[Back to building](../building.md) · [Task card rules](../chart-editor-plan/tasks.md) · [Cursor movement](../cursor-movement.md) · [Event harness](../frontend-event-harness.md)

**Audience:** the Codex implementer turning this into plan cards.
**Scope:** Compose input. Gestures, verbs, and their cancel and reset paths.
**Status:** read-only audit. No production code was changed.

## Why this exists

Bugs keep showing up in one workflow after being fixed in another. This audit
looks for the structure that causes that:

- **Workflow redundancy.** Two gestures produce nearly the same result through
  different code, such as Shift+X and Alt+X both building a thing.
- **Verb redundancy.** Two actions or commands perform the same mutation with
  their own validation, snapping, undo grouping or feedback.
- **Dangling gestures.** Gesture state survives a failed, rejected or
  interrupted workflow.

Reaching one verb by keyboard, mouse and menu is fine when all three funnel into
one path. It is only flagged when the paths actually diverge.

## Method and confidence

Seven Claude Haiku auditors each covered one workflow family:

1. Holds and creation
2. Selection and move
3. Timing-lane drags
4. Viewport and navigation
5. Resources and drag-and-drop
6. Menus and command verbs
7. Pointer routing and latches

Two Codex `gpt-6-luna` runs covered the gesture lifecycle matrix and git-history
archaeology.

Claude then re-read the source for every high-impact claim. Each finding carries
one of these marks:

| Mark | Meaning |
| --- | --- |
| **V** | Verified by Claude in source, with the cited lines read. |
| **A** | Agent-reported and plausible, but not re-verified. Re-check before fixing. |
| **R** | Rejected or downgraded. Listed in [Rejected claims](#rejected-or-downgraded-claims) so nobody chases them. |

Line numbers are as of commit `5338c71` plus the uncommitted T-504..T-506 work
in the tree. Re-grep the named symbol before editing.

## Root causes

Most individual findings come from six structural problems. Fix these and most
of the findings below disappear or become one-line changes.

### RC1: No single owner for gesture lifecycle (V)

There are three disagreeing layers, and each needs a new gesture added by hand.

**UI reset-all lists.** Six of them, each clearing a different subset of
`compose_state.h` flags and deciding differently whether to emit a core
`CANCEL_GESTURE`:

| Reset site | Where | Notable omissions |
| --- | --- | --- |
| `process_compose_escape` | `compose_keyboard_input.cpp:286` | Pointer hold, note paint, Draw erase, semantic paint, Kiai/Speed/recipe drags, timing-stage and timing-marker drags, overview seek, stem-lane resize |
| `reset_compose_revision_state` | `compose_input_panel.cpp:533` | Cancel condition omits `pointer_hold_active`, `lane_hold_active`, `area_selection_active`. Clears `lane_hold_*` only on chart change. Omits `offset_fit_drag_active`. |
| `clear_inactive_compose_panel` | `compose_input_panel.cpp:574` | Cancel condition omits note move, BMSON marker move, BGA event move. Omits offset fit, beat zero, overview. |
| `cancel_pending_compose_panel_actions` | `compose_input_panel.cpp:500` | Different subset again |
| `compose_handle_focus_lost_input` | `compose_input_panel.cpp:320` | Different subset again |
| Action-overflow block | `compose_input_panel.cpp:~108-151` | Offset fit, beat zero, overview, timing marker |

**Core "is any gesture pending" predicates.** Five copies:

- `has_pending_editor_gesture` (`interaction.cpp:6259`)
- `playhead_gesture_active` (`interaction.cpp:1975`)
- `note_conversion_pending` (`note_to_bgm.cpp:600`)
- `playhead_cursor_edit_blocked` (`compose_playhead_cursor.h:21`)
- An inline block at `compose_input.cpp:4415`

They have already drifted. `has_pending_editor_gesture` omits
`additional_keyboard_holds`, and it is the one used by `compose_area_actions.cpp:249`
and `compose_view.cpp:827`.

**Core cancel and validation.** `cancel_editor_gesture` (`interaction.cpp:5340`)
and `validate_active_gesture_action` (`interaction.cpp:5366`) list the pending
fields again. Several core terminal paths return a diagnostic without clearing
their pending field:

- Space-range commit (`:4378-4381`)
- Area-selection update and commit (`:5243-5246`)
- Note-duration release on collision (A)
- Toggle note-paint end (A)

The editor then rejects every other action with "finish or cancel the active …
gesture first" until something sends a cancel.

The core modal and text-input hooks are unwired. `viewport_input_controller_t::modal_opened`
and `editor_navigation_input_controller_t::modal_opened` / `text_input_started`
have no callers; only `focus_lost` is called (`compose_input.cpp:1219`).

**Target shape:**

- One table-driven UI gesture registry. Each entry has its flag, its reset
  function, and the cancel action it emits. Escape, focus loss, revision change,
  inactive panel, overflow and modal-open all iterate the same table.
- One core `pending_gesture_fields` enumeration that backs all five predicates
  and the cancel and validate lists.
- A rule that every core terminal error path either clears its pending field or
  is explicitly documented as retryable, and the UI then cancels on the next
  press.

### RC2: Verbs whose registered command, context menu, panel and input kind each build their own request (V/A)

The registered command path (`application_command_dispatch.cpp`) is usually the
most complete. Menus and panels bypass it with zeroed or differently guarded
requests:

- Quantize (MV1)
- Delete vs Cut vs `DELETE_SELECTION` (MV2, MV3)
- Add bookmark (MV4)
- Select same sound (MV5)
- Move to BGM (MV6)
- Loop selection (MV7)
- Measure edits (MV10)

Two input kinds have tests but no production emitter:

- `WORM_UI_COMPOSE_INPUT_DELETE_SELECTION` (only `frame_timing_tests.cpp:2382,2499`,
  `compose_notification_tests.cpp:438`, `compose_action_notification_tests.cpp:1688`)
- `WORM_UI_COMPOSE_INPUT_NOTE_MOVE_TO_BGM` (only `nuklear_event_harness_tests.cpp:4602`)

So those tests cover behaviour the shipped UI cannot reach. See RC6.

**Target shape:** every menu, panel and context entry emits the registered
command ID, or calls the same request builder the registered command uses.
Delete the orphan input kinds, or make them the single path.

### RC3: Hold creation has five entry points and four state representations (V)

**Entry points:**

- Click-click: `place_tap` → `begin_hold_action` → `commit_hold`, with the
  redirect at `interaction.cpp:5904`.
- Press-drag with Hold/Roll armed, and Alt+drag: `begin_alt_pointer_hold_gesture`
  → `begin_hold_gesture(alt_pointer)`.
- Keyboard lane hold: `begin_lane_key_hold_gesture` / `release_lane_key_hold_gesture`.
- Virtual BMS click-click: `pending_virtual_lane_key` → `place_virtual_bms_hold`
  at `compose_input.cpp:2814-2840`. On failure this path restores pending state,
  which the authored path does not.
- `finish_virtual_hold_at_authored_row` (`compose_input.cpp:2941`).
- The keyboard-release branch at `compose_input.cpp:841-878`, which turns into a
  virtual tap when the release row equals the press row.

**State representations:**

- `pending_hold`, with optional `gesture_source`
- `additional_keyboard_holds`
- `pending_virtual_lane_key`
- Canvas-row fields on `pending_hold`

**Divergences already visible:**

- Roll on the virtual canvas: HC5 (V)
- Stem channel routing: HC7 (V)
- Kind and layers read at release but sound captured at press: HC9 (A)
- Empty-space release of note paint vs semantic paint: HC8 (A)

`handle_cancel_hold` (`interaction.cpp:5920`) is a dead no-op lambda; the
pre-dispatch block at `:5351` handles cancel-hold.

**Target shape:**

- One `resolve_row_placement` (tap / begin hold / commit hold) shared by
  authored and virtual rows and by click and key.
- One stem-and-geometry resolver, so `entry_stem_channel` is used by every
  creation path.
- Kind, layers and sound captured together at begin.

### RC4: About ten hand-rolled drag state machines (V/A)

The UI-local drags are:

- Kiai / Speed span (three begin paths)
- Speed stretch
- SV recipe
- Timing marker
- Timing stage
- Beat fit
- Offset fit
- Beat zero
- Overview seek
- Measure resize
- Stem-lane resize
- BGA / bookmark / BMSON marker moves
- The audio panel's own offset drag

Each has its own anchor, its own moved threshold (scaled 4px, raw 4px, or
none), its own hit tolerance (6, 7 or 8px), its own staleness capture (at press
vs at release) and its own lock gate. Several draw previews while locked.

Copy-paste evidence:

- `cancel_offset_fit_drag` zeroes `beat_fit_anchor_screen_y`
  (`compose_input_panel.cpp:198-203`).
- A second `cancel_compose_offset_fit_drag` exists at `compose_viewport_input.cpp:324`.

**Target shape:** a small `drag_capture_t` with these parts:

- Stamp: project, chart, revision and asset, captured at press.
- Anchor.
- Shared `moved_past_threshold`.
- Lock predicate checked at begin.
- `cancel()` and `commit()` hooks.

These hooks register with the RC1 table. Pointer-to-snapped-row conversion
needs one helper with an explicit failure policy (TL6).

### RC5: Modifier semantics decided in several places (V)

Alt is read from two sources in the same frame:

- `NK_KEY_ALT` (`compose_panel.cpp:1303`) gates pointer-hold start and cancel.
- `SDL_GetModState()` (`:1306`) gates quick insert, beat-fit snap and
  same-sound select.

Selection modifiers come from `compose_selection_mode` (Ctrl → toggle,
Shift → add), but individual handlers reinterpret chords:

- Shift on a BMSON BGM slice toggles restart (PR-B1).
- Ctrl+Shift on a sound object means "same sound visible" but means toggle on a
  note (PR-B2).
- Alt on the Anchor lane inserts or starts a hold depending on whether the press
  began inside the note field (PR-B3).

Pointer priority is implicit first-consumer-wins via `paint_pointer_consumed`.

**Target shape:** one per-frame `pointer_modifiers` snapshot from a single
source, and one documented chord table for the canvas.

### RC6: Tests that pass on unreachable paths (V)

See RC2. The Kiai and recipe delete branches are covered only through
`DELETE_SELECTION`, which no UI emits, while the real Delete key
(`edit.delete` → `edit_commands`, `application_command_dispatch.cpp:403`) has no
Kiai or recipe branch.

**Rule for the fix cards:** every regression test for a verb must enter through
the production emitter. Use the event harness or the registered command, not a
synthetic input kind.

## Findings by priority

The ID prefix shows which auditor reported the finding:

| Prefix | Source |
| --- | --- |
| HC | Holds and creation |
| SM | Selection and move |
| TL | Timing-lane drags |
| NV | Navigation |
| RS | Resources |
| MV | Menu verbs |
| PR | Pointer routing |
| LC | Lifecycle (Luna) |
| GH | Git history (Luna) |

### P0: confirmed user-visible bugs

| ID | Mark | Finding | Repro |
| --- | --- | --- | --- |
| MV2 | V | Delete key / Edit›Delete cannot delete a selected Kiai span or SV recipe. Those branches exist only behind the unemitted `DELETE_SELECTION` input (`compose_input.cpp:3719-3733`, `2761`). Cut does honour Kiai (`application_command_dispatch.cpp:384`). | Click a Kiai area, press Delete: "No object at the cursor." |
| MV3 | V | Delete on a mixed timing+note selection deletes only the timing markers and returns early (`application_command_dispatch.cpp:413-426`). Cut deletes both as one undo step. | Box-select a BPM marker and notes, press Delete: notes survive, and a second Delete plus two Undos are needed. |
| MV1 | V | Area-menu "Quantize selection" (`compose_area_menu.cpp:157-160`) sends zeroed quantize fields. That hits the legacy branch (`compose_area_actions.cpp:72-83`: snap denominator, `both_endpoints`), ignoring the Quantize panel that `edit.apply_quantize` and the preview use. | Set the panel to 1/3 with tie "later", then right-click › Quantize: the result differs from the preview. |
| TL1 / LC-D1 | V | Escape does not cancel Kiai, Speed, recipe, timing-stage or timing-marker drags, pointer holds, paint strokes, Draw erase or overview seek (see RC1). The release still commits. | Drag a Kiai edge, press Escape, release: the edit is committed. |
| LC-D2 / HC2 | V | A revision change during a pointer hold, lane-key hold or area selection clears the UI flags without emitting a core cancel. The core gesture stays pending, and the editor rejects other actions until a cancel. | Hold a lane key with Hold armed, press Ctrl+Z, release the key: no release is sent and later actions fail with "finish or cancel …". |
| SM4 | V (mechanism) | Space-range and area-selection combine failure returns a diagnostic but keeps `pending_*_selection`, so the preview stays on screen until Escape. | Agent repro: an audio-anchored bookmark left in the selection, then Ctrl+Space. |
| HC5 | V | On the virtual BMS canvas, Roll armed + click places a tail-less roll-kind note. Only `armed == hold` starts a virtual hold (`compose_input.cpp:2832`), and `place_virtual_bms_tap` writes `armed_note_kind` (`interaction.cpp:4640`). The authored path routes Hold *or* Roll to `begin_hold_action` (`:5905`). A same-row lane-key release with Hold armed also creates a hold-kind note with no end (`compose_input.cpp:862-864`). | BMS chart, Roll armed, click a virtual row. |
| NV7 | V | **Uncommitted T-506 code.** `navigation_travel::advance()` (`compose_navigation_travel.h:46-52`) cancels on playback, pose, revision, scale or scroll mismatch without `finish()`. The viewport is left part-way while the cursor sits at the destination. | Press Tab, then Space within 75 ms. |
| RS3 | V | The armed-resource triple (`armed_resource_id`, `armed_from_sounds_dock`, `armed_from_bga_dock`) is written at about ten sites with no single setter (see the bullets below this table). | Arm in project A, open project B, select a sampled note: it does not re-arm. |
| SM8 | V | Ctrl+click deselecting a note still auditions it: `select_editor_objects` uses `selected.front()` regardless of mode (`interaction.cpp:5107-5110`). | Turn on audition-on-click, Ctrl+click a selected note. |
| PR-C1 | V | `handle_compose_alt_pointer` sets `pointer_hold_active` before sys can refuse the begin (`compose_object_input.cpp:1011-1018` vs `compose_input.cpp:1497-1504`). Every move then emits an update that the core rejects, until release. | Alt+drag starting on a mine or BGM note. |

The RS3 write sites are:

- `compose_input_panel.cpp:650,762`
- `compose_state.cpp:768`
- `compose_sound_picker.cpp:31`
- `compose_bga_panel.cpp:252,376`
- `compose_sounds_panel.cpp:886,894-895,1419`

A project switch clears the ID but not the flags. A BGA chart switch clears the
flag but not the ID. `compose_resources.cpp:44` then refuses to re-arm from the
selection.

### P1: confirmed divergences with real risk

| ID | Mark | Finding |
| --- | --- | --- |
| RC1-pred | V | `has_pending_editor_gesture` omits `additional_keyboard_holds` (see RC1). |
| HC7 | V | `entry_stem_channel` is used by keyboard entry (`interaction.cpp:3759`), paint (`:3404`) and the stem hold (`:3998`), but not by `place_tap` or the virtual paths. Plain click-place on a BMSON stem sample therefore makes an unlinked note. T-572/T-574 unified paint and Alt-click but not plain click. Click-click and keyboard holds also commit without `source_geometry`. |
| RS1 | V | Replacing an occupied WAV slot: the single-file path renames the occupant (`resource_assignment.cpp:~1962`). The batch path (`import_slot_batch_entry`, `:1873-1897`) keeps the old name. |
| MV-cut | V | Live Cut (`compose_clipboard_selection.h:24-78`) commits Kiai and timing pieces before the final delete. If the delete fails, the earlier commits stay, are merged into a "Cut selection" undo record, the error is shown, and the clipboard is not updated. The core `cut_chart_selection` (`chart_clipboard.cpp:196`) is used only by tests and is a second Cut implementation. |
| NV4 | V/A | The go-to-measure modal returns before the viewport queues drain (`compose_panel.cpp:1288-1292`). Wheel, middle-click and pinch queued during the dialog replay after it closes (replay is A). The core `modal_opened` hooks are unwired (V). |
| PR-C3 | V | Alt is read from two sources in one frame (see RC5). |
| TL6 | V | `kiai_pointer_row` falls back to the unsnapped row (`compose_input.cpp:2527`). The timing-marker move hard-fails on the same snap expression. |
| TL2-slip | V | `cancel_offset_fit_drag` zeroes the beat-fit anchor, and there are two offset-fit cancel helpers. |
| GH-1 | V (direction reversed) | Pointer placement refuses a fixed 14px band after Stop and before Delay rows (`compose_dead_spans.cpp:127-131`, `compose_input.cpp:1346-1367`). The band depends on zoom and covers valid rows, including the Stop row itself. Lane keys and paste apply no such rule. Luna proposed adding the rule to lane keys. **Recommendation: drop the pointer-only pixel rule, or express it in rows in the shared placement rule.** The Warp rule was already unified via `head_blocked_by_warp` after `bfb8f60` / `1bac614`, so this is the next sibling of that cluster. |

### P2: agent-reported findings (A), re-check before fixing

These were reported with file:line evidence and look plausible, but Claude did
not re-read them.

**Selection and move**

- **SM5.** Marquee, Space range and select-contents disagree on selectable
  families: the marquee never selects BGA events, bookmarks or sections. With no
  geometry, the marquee selects nothing but reports "started".
- **SM6.** The Space-range preview stores raw hits, while the commit applies the
  add/toggle combine.
- **SM7.** Mirror, rotate and shift act on `selected_notes` only. Delete and lane
  moves act on `selected_objects`.
- **SM9.** Select-all and invert bypass `select_editor_objects`, skip
  `find_active_lane`, and drop non-note selections.
- **SM10.** The note-move preview plans with `exact=false`; the release plans
  with `exact=true`.

**Holds and creation**

- **HC3.** A duration drag released on a collision keeps
  `pending_note_duration_gesture`, which locks the editor.
- **HC4.** Toggle note paint keeps its pending stroke after a zero-change or
  rejected release.
- **HC6.** Virtual tap has no toggle, collision or duplicate check, unlike
  `place_tap`.
- **HC8.** Releasing note paint in empty space cancels the stroke; semantic paint
  commits it.
- **HC9.** Kind and layers are read at release; the sound is captured at press.
- **HC10.** `update_hold_preview_action_t` and `resize_note_durations_action_t`
  appear to have no production emitter.
- **HC11.** `append_note_paint_target` divides by a snap step that may be 0.

**Timing-lane drags**

- **TL3.** Beat fit and offset fit are two copies of one state machine.
- **TL4.** Three entries reach the audio-offset drag with different lock gates.
  Beat-zero commits on a zero-distance click.
- **TL5.** A timing-marker drag commits on release with multi-select. A stage drag
  previews only, has no lock gate, and its lock rejection reads "cannot move to
  that row".
- **TL7.** Kiai, Speed and recipe drags start and draw previews while timing is
  locked; the lock is enforced only at commit.
- **TL8.** Kiai and recipe read chart and revision at release, not at press.
- **TL9.** There are five different moved-threshold and hit-tolerance rules.
- **TL10.** Kiai, Speed and recipe resolve span hits differently, including
  empty-click selection side effects.
- **TL11.** A Speed begin-edge drag recomputes the transition length; a
  Speed-marker drag does not.
- **TL12.** After action overflow, a stale offset-fit preview can remain.

**Navigation**

- **NV1.** Alt+wheel moves the edit cursor during playback, while the plain
  wheel and arrows skip the playhead. The `MOVE_CURSOR_*` dispatch at
  `compose_input.cpp:733-742` has no playing branch.
- **NV2.** Overview drag, Shift-click and reset view scroll without a fixed
  anchor, leaving the logical row off the yellow line.
- **NV3.** Go to measure while playing moves only the cursor. Bookmark and
  measure steps seek audio.
- **NV5.** Playing steps use a fixed beat, stopped steps use the snap.
  `playhead_skip_unit_t::snap` and `::rows` are unused.
- **NV6.** There are four seek-to-position rule sets. A playing minimap click
  seeks an unsnapped frame, and a paused plain click stops the transport.
- **NV8.** Ctrl+Alt+wheel steps the snap instead of zooming.
- **NV9.** Navigation repeat state has three owners. The Alt-wheel latch survives
  Escape and modal open.

**Resources**

- **RS2.** Menu "Replace sound" accepts a BGM-only resource on a note; pointer
  Replace rejects it.
- **RS4.** Dropping a file while the slot confirmation is open overwrites the
  pending confirmation. The confirmation survives a chart switch for one frame.
- **RS5.** UI and core WAV-occupancy matchers differ in case sensitivity.
- **RS6.** The BGA drop applies with no review or origin stamp; the sound drop
  reviews. The `import_picked_files` path looks dead.
- **RS7.** The BGA drop reads x/y from `DROP_FILE`; the sound drop tracks
  `DROP_POSITION`.
- **RS8.** Lane-key assignment of BMSON-linked notes skips `reassign_bmson_notes`.
  The command and drag paths stamp the current revision.
- **RS9.** The WAV and BMP renumber confirmations are one state machine written
  twice.
- **RS10.** Metadata channel-map and lane-profile previews survive project close;
  `discard_if_stale` ignores the project ID.
- **RS11.** There are three stem-arm entry points. The note double-click and BGM
  double-click paths lack the missing-sound guard that the list click has.

**Menu verbs**

- **MV4.** Add bookmark has two implementations, differing in placement,
  duplicate check, revision guard and label.
- **MV5.** Select same sound: the command and the menu/input path differ in key
  resolution, empty-result handling and visible-only scope.
- **MV6.** Move-to-BGM enablement has three predicates. The context menu is
  enabled in non-editable modes.
- **MV7.** Area-menu and toolbar loop skip the command's gesture and paste guards
  and use view rows.
- **MV8.** Area "Delete contents" enablement uses a blacklist; dispatch uses a
  whitelist.
- **MV9.** Lane-key and drop-on-lane reach lane assignment through different
  wrappers.
- **MV10.** Measure split has a paste guard but no revision guard; panel
  insert/remove has the opposite.

**Pointer routing**

- **PR-A1.** Shift+drag on a Speed marker both stretches it and ADD-selects it,
  depending on routing order.
- **PR-B1.** Shift+click on a BMSON BGM slice toggles restart; elsewhere it adds
  to the selection.
- **PR-B2.** Ctrl+Shift means "same sound visible" on sound objects and toggle on
  notes.
- **PR-B3.** Alt on an empty Anchor lane inserts an anchor or starts a hold
  depending on where the press began.
- **PR-C2.** Pressing Ctrl+Alt during a note-move candidate hands the release to
  the same-sound handler, silently dropping the move.
- **PR-D1.** Release latches: `popup_lifecycle.h` is the shared primitive, plus at
  least five hand-rolled copies with different quiet predicates:
  - `compose_bookmark_chooser.cpp:8-23`
  - `compose_sound_picker.cpp:10-16`
  - `compose_bookmark_rename.cpp:75-90`
  - `bga_renumber_release_pending` (`compose_bga_panel.cpp:452`, cleared only
    while that panel renders)
  - `bgm_column_count_edit`

  The `draw_right_*` reset line is copied at about eight sites.

**Lifecycle**

- **LC-D3 / D4.** `waveform_overview_gesture` and its capture are never reset by
  Escape, focus loss or a revision change. Only the `_active` flag is.

## What git history says

Two Luna archaeology passes looked for one class of bug being fixed in one
workflow and later in a sibling.

**One clean recurrence: Warp placement.** The rule was first enforced for pointer
placement (`04f25aa`). The T-314 audit later found lane-key and paste placement
missing it (`bfb8f60`, `1bac614`). It is now shared via `head_blocked_by_warp`.
Its unshared sibling is the Stop/Delay pixel band (GH-1).

**Clusters that were later centralized.** These are no live gap now, so don't
reopen them:

- Stale IDs after removal: `92c830c`, now `prune_stale_selection`.
- Measure-local snapping: `e7dbf77`, shared snap helpers.
- Stem source geometry on paint, holds and lane keys: `98785ed`, T-572, T-574.
- Selection loop and Shift-Space: `9fbf020`, one revision-checked setter.
- Context-menu release latches: the shared `ContextMenu` lifecycle.

**Takeaway.** The recurring bugs are not coming from old placement or selection
splits. Those were mostly unified in recent cards. They come from the lifecycle
layer (RC1) and the newer verb/menu entry points (RC2), which no history fix has
touched yet.

The second pass added two current-code findings:

| ID | Mark | Finding |
| --- | --- | --- |
| GH-A3 | V | Inconsistent stale-revision policy across core gestures. Note-move update resets its pending gesture when stale (`interaction.cpp:3013-3014`). Semantic-layer paint (`:4886-4890`), toggle note paint (`:~3157`) and area selection keep theirs, so `validate_active_gesture_action` then blocks unrelated actions. This belongs in card 1. |
| GH-A4 | V | The BGA event move capture (`compose_input_panel.cpp:1151-1156`) does not check `input_occluded`; the bookmark move capture (`:928-932`) does. A release under a popup can still submit a BGA move. This belongs in card 5. |

## Rejected or downgraded claims

| Claim | Verdict |
| --- | --- |
| SM1: a bare lane key turns selected BGM events into note selections | **Rejected.** The non-successive lane assignment *converts* BGM events into notes (`note_to_bgm.cpp:543` `convert_bgm_event`), so re-selecting them as notes is intended. One residual is SUSPECT: the BMSON branch (`assign_bmson_sound_event`, `:534`) may not convert, and `push_back` at `interaction.cpp:2745` has no duplicate guard. |
| SM2: `assign_selected_notes_to_lane` is dead | **Rejected.** It is layered under `move_selected_objects_to_lane` via `assign_note_lanes` (`interaction.cpp:6227`). Only the handler name `handle_assign_selected_notes_to_lane` is misleading. |
| HC1: a click commit leaves keyboard holds and later dereferences an empty `pending_hold` (UB) | **Downgraded to latent.** `commit_hold` does not clear `additional_keyboard_holds`, and `update_keyboard_holds` (`interaction.cpp:4107`) dereferences without a guard. But primary keyboard holds always carry `gesture_source = keyboard_lane`, and `validate_active_gesture_action` rejects the click commit, so the repro is blocked. Add the null guard while doing RC3. |
| GH-1 as proposed (add the Stop/Delay pixel band to lane keys) | **Reversed.** See P1. The pointer rule is the outlier. |

## Suggested card breakdown

Order matters: RC1 first, because it turns most P0 lifecycle bugs into table
entries. Each card should add **event-harness regressions that enter through the
production emitter** (RC6) and follow the normal verify gate:

- Build
- Full CTest
- `-L event-harness`
- `documentation.chart_editor_state`
- clang-tidy complexity on the touched files
- Isolated Xvfb for visible behaviour

1. **Gesture registry and unified cancel (RC1).**
   - UI: a table of drag/capture entries with reset and cancel hooks. Replace the
     six reset lists. Wire the core `modal_opened` / `text_input_started`.
   - Core: one enumeration behind the five predicates and the cancel/validate
     lists. Clear pending state on terminal errors (SM4, HC3, HC4), with one stale-revision policy for every core gesture (GH-A3).
   - Fixes: TL1, LC-D1..D4, LC-D2/HC2, RC1-pred, TL12, NV4 (queues).
   - Tests: Escape, focus loss, a revision change and modal open during *each*
     registered gesture.
2. **Delete/Cut single path (MV2, MV3, MV-cut, RC6).** One
   `delete_selection_all` helper used by `edit.delete` and Cut. Validate all
   pieces before the first commit. Remove `DELETE_SELECTION` and the core
   `cut_chart_selection`, or make them the single path. Port the tests to
   `edit.delete`.
3. **Menu verbs through registered commands (MV1, MV4–MV7, MV10).** Area
   Quantize via `invoke_compose_quantize_command`. One bookmark-add helper. One
   same-sound helper. One move-to-BGM predicate, with the orphan
   `NOTE_MOVE_TO_BGM` removed. Loop via `transport.loop_selection`.
4. **Row placement unification (RC3: HC5, HC6, HC7, HC9, HC10, HC1 guard).**
   One `resolve_row_placement` for authored and virtual rows, click and key; one
   stem/geometry resolver; capture kind, layers and sound at begin; delete
   `cancel_hold` and `update_hold_preview` if they stay unused. Resolve the Stop
   rule direction here (GH-1).
5. **Drag-capture primitive (RC4: TL2-slip, TL3, TL4, TL5, TL6, TL7, TL8, TL9,
   TL10, GH-A4).** Shared stamp, threshold, lock-at-begin and snap-row helper. Merge
   beat fit with offset fit.
6. **Armed-resource setter (RS3, RS11, RS2).** One
   `set_armed_resource(state, id, origin)` / `reset_armed_resource(reason)`, and
   one role validator in core for note vs BGM assignment.
7. **Modifier and chord table (RC5: PR-C3, PR-A1, PR-B1..B3, PR-C1, PR-C2,
   NV8).** Single modifier snapshot. Pointer-hold active only after sys accepts
   the begin. Documented chord table in `docs/`.
8. **Navigation transport policy (NV1, NV2, NV3, NV5, NV6, NV7).** One "step"
   helper and one "go to position" helper that decide cursor vs playhead and the
   snap unit. Travel cancel finishes or restores. **NV7 should be fixed before
   T-506 is committed.**
9. **Selection target resolver (SM5–SM10, SM8).** One selectable-families
   mapping; one selection-to-target resolver for transforms, delete and lane
   moves; audition only for objects that stay selected.
10. **Resource import consistency (RS1, RS4–RS10).** Lower priority; mostly drift.
    Spot-check RS7 (BGA drop coordinates) at runtime first.
11. **Latch primitive (PR-D1).** Fold the hand-rolled release latches into
    `popup_lifecycle.h`.

## Raw agent reports

Kept outside the repo in the auditing session's scratchpad. The findings above
are the curated set. Agent-only claims are marked **A** and should be treated as
leads, not facts.
