# BMS editing: move to BGM with group-aware lanes, move-and-deselect, select by sound (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User request from 2026-10-05, with iBMSC as the reference:

- **Move notes to BGM:** a gesture that moves notes from play lanes into BGM lanes.
- **Move and deselect:** confirm the gesture is wired, and make it findable.
- **Select all with sound:** select every note with the same sound, either in the visible area or in the whole chart.

The BGM allocation rule the user set:

- **Keep groups together:** notes whose sounds share a sound group go into the same BGM lane whenever they are not simultaneous.
- **Prefer existing lanes:** if the chart already has BGM events from that group, try those lanes first.
- **Fall back:** if those lanes are taken at that row, find other space.

Found while planning: Shift+lane key already sends `assign_selected_notes_to_lane_action_t{successive = true}`. It goes from `process_compose_lane_key` (`compose_keyboard_input.cpp`) through `dispatch_compose_lane_key_press` (`compose_input.cpp`) into the core (`interaction.cpp`). No event-harness case covers the full path, and no UI mentions it.

## Allocation rule (used by T-423, T-424, T-425 and T-421)

The **group key** of a note is its sound's `sound_group` when it has one. Otherwise it is the sound itself, so repeated plain sounds stay together too. This implicit key exists only inside the allocator: it never creates a `sound_group_t`, a folder, or anything else the user sees or has to manage. Silent notes have no key.

For each group key, in first-appearance document order:

1. Its **preferred lanes** are BGM lanes that already hold events with that key in this chart. They are ordered by event count (descending), then lane index.
2. Each note, in row order, takes the first preferred lane free at its row.
3. If none is free, it takes the lowest-index BGM lane free at that row. That lane is appended to the group's preferred lanes, so the group's later notes follow it.
4. If no lane is free, it creates a new BGM lane where the format allows it. Otherwise the whole operation rejects.

Notes with the same key at the same row necessarily spill to different lanes, and each spill lane joins the preferred list. A silent note takes the lowest free lane. The result is deterministic for the same document and selection.

## T-422 Cover and surface Shift+lane key move-and-deselect

- **Status:** done (2026-10-08; real native-key dispatcher/document/undo proof, context hint and keyboard reference verified)
- **Outcome:** with three notes selected, Shift+lane key moves the first one in document order to that lane, keeps its position and length, deselects it, and leaves the other two selected. Each press is one undo step. An event-harness case covers the path from the key event to the document. The note context menu has a hint, and the Compose keyboard reference lists it.
- **Read first:** `src/ui/nuklear/compose_keyboard_input.cpp` (`process_compose_lane_key`), `src/sys/compose/input/compose_input.cpp` (`dispatch_compose_lane_key_press`), `src/core/interaction.cpp` (successive branch near "Assign next selected note to lane"), `docs/chart-editor-plan/editing/interactions.md`.
- **Files owned:** `tests/ui/nuklear_event_harness_tests.cpp`, `src/ui/nuklear/compose_note_menu.cpp` (hint only), `docs/chart-editor-plan/editing/interactions.md` (keyboard reference). Root owns this card. Any actual helper/target extension or fix exposed by the harness requires exact recorded support approval before editing.
- **Display gate feasibility (2026-10-08):** isolated authenticated Xvfb could not establish any tested listener (inet6/inet/local), and its display remained unreachable; evidence is retained under the batch feedback scratch logs. Keep the native event harness as the input proof and isolated SDL dummy/software rendering as capture evidence. Report the Xvfb gate as technically unavailable, never as passed; do not open a user-desktop test window.
- **Supporting target approval (2026-10-08):** root owns `tests/CMakeLists.txt`. Add the exact 20-source production dispatcher/projection closure listed in `memory/coordination/oct8-quantize-bgm-batch.md` to the existing native event harness, using its existing conditional decoder links. No test substitute for the dispatcher or production callback is permitted.
- **Stale audit / adaptation (2026-10-08):** current native coverage checks the successive payload only; core and application coverage exist separately. The missing proof is three native presses through the real application dispatcher and undo. No note-menu lane submenu exists (Layer means semantic membership), so add the hint to Note actions without inventing a lane submenu. The existing interaction document describes Shift sequencing; use it for a focused keyboard reference rather than presenting harness documentation as a user guide.
- **Modeled rejection scope (2026-10-08):** lane definitions have no lock attribute and assignment has no note-kind compatibility policy. Verify the existing effective-target availability, inclusive overlap and input ownership guards; preserve valid note kinds. The older locked-lane/incompatible-kind wording below does not authorize new fields or policies, and those nonexistent guards must not be claimed as tested. Semantic-layer deletion/merge locks are separate behavior.
- **Acceptance (2026-10-08):** ordinary optimized app/core/event/frame build passes. Six focused native cases/461 assertions pass, independently repeated by root; full event executable passes 279 cases/3,775 assertions and the CTest event label passes 249/249. Production 4K profile bindings feed three native Shift presses through the actual dispatcher, exact saved-document/selection checks, one undo per press and actual Undo restoration. Plain assignment, inclusive collision, missing effective lane, repeat/unbound/text/IME/focus/menu guards pass. Header-inclusive configured tidy retains five existing test findings and the unchanged existing `build_speed_graph` header finding, with no new/increased findings. Docs and diff checks pass. Three normal-object hidden SDL/software captures prove moved notes, Undo and a readable native-right-click hint; root independently inspects the hint image. Xvfb remains technically unavailable, not passed. Final ordinary main batch gates remain separate.
- **Steps:**
  1. Write the event-harness case: three notes selected, Shift+lane key pressed three times, then undo once.
  2. If it fails, fix the cause and record it. Likely suspects are Shift being swallowed by the shortcut layer and the lane-hold bookkeeping on Shift press.
  3. Run an isolated Xvfb check with xdotool `shift+<lane key>`.
  4. Add the hint and the documentation entry.
- **Invariants:** a plain lane key still moves all the selected notes and keeps them selected.
- **Reject/failure behavior:** a collision, a locked lane or an incompatible kind leaves the notes and the selection unchanged, and shows the reason.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`; isolated Xvfb check.
- **Out of scope:** moving to BGM (T-424).

## T-423 Add the group-aware "convert notes to BGM events" core command

- **Status:** done
- **Outcome:** the existing `move_notes_to_bgm` turns play-lane notes into BGM events at the same positions with the same sounds, using the allocation rule above for automatic placement, in one undo step. Its pure allocation plan exposes retained event IDs, and core tests cover every rule step.
- **Read first:** `include/wormedit/core/note_to_bgm.h`, `src/core/note_to_bgm.cpp`, `tests/core/note_to_bgm_tests.cpp`, `include/wormedit/core/document.h` (`bgm_event_t`, `resource_entry_t::sound_group`).
- **Files owned:** initial allocator phase: `include/wormedit/core/note_to_bgm.h`, `src/core/note_to_bgm.cpp`, `tests/core/note_to_bgm_tests.cpp`. Root owns this card and manifests. Later schema/native/validation/resource-assignment/export/projection paths require exact recorded root approval and a sequential handoff from T-437/T-422 before editing.
- **Allocator checkpoint (2026-10-08):** worker `bb6f713` is reviewed and integrated as main `62bcb0c`. Ordinary optimized app/core build passes, focused tests pass 23 cases/191 assertions (independently repeated by root), full core passes 1,632/1,632, and docs pass. Header-inclusive configured tidy keeps core at zero findings and the four existing test title/score identities unchanged. Automatic grouping, manual fallback, atomic capacity, retained IDs/provenance and mixed stem reservations are covered. Holds/schema/export/tail rendering remain pending; this card is not complete. Worker Lua/video settings are a feature subset; final ordinary main gates remain required.
- **Done so far:** allocator checkpoint above; native hold worker `5814624` integrated as main `eadff84`. Schema 6 saves/loads optional positive BGM ends, migrates older projects and supports recovery; silent legacy normalization retains end-bearing events. Ordinary hold conversion preserves endpoints and native fields, reverse conversion checks inclusive overlap and editable lane bands, and head moves translate tails exactly with Undo/Redo. Ordinary optimized app/core build, 27 conversion cases/244 assertions, 7 move cases/80 assertions, 54 native cases/722 assertions, 94 resource cases/867 assertions, full core 1,651/1,651 and docs pass. Root independently repeats 34 conversion/move cases/324 assertions. Configured header-inclusive tidy retains all 75 baseline finding identities/scores, with no new/increased findings. All readers released; this remains a partial card.
- **Completion (2026-10-09):** reviewed 02d linked-hold/layer reassignment is integrated. Retained and authored BMSON exports preserve BGM ends; BMS/osu! export heads with typed tail-loss warnings. Conditional BMS edits clear the retained playable endpoints and insert the BGM head within the original branch; inactive branches remain intact. Actual editor publication reports tail loss as a warning; generic strict conversion rejects it. BGM bars use a revision-bound max-end index and preserve visible bodies with offscreen heads, including cached scalar/tile projection. Normal main build and all **2,926/2,926 tests pass**, including **263 event-harness cases**; selected/unselected software painter checks and two reviewed main-object captures pass. No new/increased configured source complexity findings. See [completion evidence](../../../memory/coordination/oct9-bgm-conversion-batch.md).
- **Pure planner scope clarification (2026-10-08):** the pure allocation plan covers ordinary notes, including unlinked BMSON notes. It rejects retained-linked BMSON selections explicitly, rather than returning assignments that disagree with stem ownership. The existing command retains the specialized stem reassignment route, restores linked markers before planning ordinary occupancy, and handles mixed selections atomically. This is an object-link restriction, not a source-family ban.
- **Allocator order clarification (2026-10-08):** automatic keyed groups follow first selected document appearance; each silent note occupies its own singleton cohort at its document ordinal. Automatic assignment ignores caller-ID permutations. Explicit manual placement preserves legacy caller-ID order and requested/right/left/new fallback, ignoring group preferences.
- **Linked hold / scene support approval (2026-10-08):** root records exact BMSON parser/linked-assignment and retained scene/frontend support in the batch coordination document. Preserve existing stem slices and branch restrictions. Revision-built tail interval queries must retain bars whose heads are offscreen without a per-frame chart scan. Head/body geometry, tile hashes and window invalidation must include tails. Shared frontend edits wait for T-437/T-422 release; this is scope approval, not a concurrent writer grant.
- **Later-phase support approval (2026-10-08):** root approves the exact schema/native/validation/move/export paths recorded in `memory/coordination/oct8-quantize-bgm-batch.md`, including autosave schema acceptance, native tail validation and checked tail translation. This records scope only: shared document-header edits wait for the feedback source/build handoff. BMSON preserves representable tails; osu! Sample export reports tail loss using its existing typed warning. BMS conditional export must clear only converted retained playable endpoints and emit the BGM head in the same branch while preserving native provenance. Generic strict loss policy stays strict. New UI affordances remain out of scope.
- **Stale audit / policy (2026-10-08):** the existing command already supports one undo, BMSON stem preservation and reverse/mixed moves. Extend it rather than duplicate a `bgm_conversion` API. Automatic placement follows the canonical group rule; an explicit `preferred_lane` remains manual placement with its existing right/left fallback. Use explicit planner capacity policy and existing native lane/ID/order limits, without inventing unsupported-family restrictions. Preserve the existing command result and routing; expose event IDs through the plan. Ordinary conversion currently drops provenance and hold tails, and ignores sound groups. Schema work waits until shared-header consumers are frozen/released. Editor BMS export reports tail loss and continues; a generic strict conversion policy remains strict.
- **Steps:**
  1. Add a pure allocator: (chart, notes, catalog) → a list of (note, BGM lane or new-lane index), or a rejection. Test it without a document history.
  2. Holds keep their tails. Add `std::optional<local_position_t> end` to `bgm_event_t`, and persist it natively with a migration where absent means none. Draw it as a bar in the BGM lane. BMS export writes the head only, and its loss report lists the dropped tails, because BMS BGM channels cannot hold long notes. Play-lane moves (lane keys and drags) turn a BGM event with `end` back into a hold.
  3. Add the revision-checked command. It removes the notes, inserts the events (keeping semantic layers, branch, provenance and hold ends where they apply), and creates any new lanes.
  4. Write tests:
     - a group with an existing lane reuses it;
     - that lane being busy at one row makes the note spill and the group follow;
     - two same-group notes at the same row go to two lanes;
     - ungrouped notes with the same sound stay together;
     - a silent note takes the lowest lane;
     - a format without new lanes rejects atomically;
     - running it twice on equal input gives equal output;
     - a hold keeps its tail as the BGM event's `end`, and moving it back to a play lane restores the same hold.
- **Invariants:** sounds, positions, layers, branches and hold tails survive in the native project. Nothing outside the listed notes changes. The only loss is at BMS export, and the loss report states it.
- **Reject/failure behavior:** any unplaceable note rejects the whole command with no partial change.
- **Verify:** `cmake --build build`; run the existing core note-to-BGM test cases with a nonempty matched count and required full core suite; `clang-tidy -p build src/core/note_to_bgm.cpp`.
- **Out of scope:** UI (T-424, T-425).

## T-424 Add the Move to BGM key and menu item, with a successive Shift variant

- **Status:** done
- **Outcome:** on BMS charts, the "BGM lane key" moves every selected play note to BGM lanes using T-423. It defaults to `` ` `` (Backquote) and can be rebound with the other lane keys. Shift plus that key moves only the first selected note in document order, deselects it, and keeps the rest selected, mirroring T-422. The note context menu gains "Move to BGM" with the shortcut shown, and Edit gains "Move Selection to BGM". The moved events end up selected.
- **Read first:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/core/command_registry.cpp`, `include/wormedit/core/note_to_bgm.h`.
- **Files owned:** `src/ui/nuklear/compose_keyboard_input.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/frontend.h` (input kind and binding), `src/sys/compose/input/compose_input.cpp`, `src/sys/compose/views/compose_view.cpp` (binding projection), `src/core/command_registry.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/main.cpp` (command enablement), `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Implementation / verification (2026-10-09):** Backquote and Shift+Backquote are registered, rebindable Compose commands (`edit.move_to_bgm`, `edit.move_next_to_bgm`), using the existing shared helper. The chart lane-key table holds physical play-lane bindings; the pseudo-lane commands use the editor command registry to share persistence, focus gating, conflict diagnosis and menu dispatch. Supporting scope is recorded in the [batch evidence](../../../memory/coordination/oct9-bgm-conversion-batch.md). Both menus gate non-BMS charts; the note menu displays the current shortcut. Disabled hints render after the parent menu closes its popup context. Real key/selection/Undo and menu cases, application format/rejection gates, all 263 event-harness cases, normal build and all 2,926 tests pass. New functions meet the configured complexity limit.
- **Steps:**
  1. Add the binding to the lane-key table as a pseudo-lane, and check it with `diagnose_shortcut_conflicts`.
  2. Send both variants through the standard wiring pattern into `move_notes_to_bgm`, with the first note only when Shift is held.
  3. Add the menu items, enabled only when play notes are selected on a BMS chart.
  4. Add event-harness cases: move all, Shift three times, and the menu item, each followed by undo.
- **Invariants:** BGM events already in the selection are ignored. Non-BMS charts show the items disabled with a tooltip.
- **Reject/failure behavior:** a rejection from T-423 leaves the notes and the selection unchanged and shows the reason.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** dragging into the BGM area (T-425) and moving BGM back to play lanes.

## T-425 Drag selected notes into the BGM area to convert them

- **Status:** done (2026-10-09)
- **Implementation / verification (2026-10-09):** Live move updates carry the BGM target/preferred lane and row delta; preview destinations come from the shared conversion command on a scratch document. Accepted ghosts are amber, rejected ghosts red, including tails and new lane headers. Returning to the playfield restores ordinary move previews. Release commits once and selects the resulting BGM events; rejection changes nothing. Normal main build and full **2,939/2,939** suite pass, including 265 event-harness cases. Source/test complexity checks introduce no new/increased findings; three reviewed main-object software captures and release/Undo checks pass. Native-window smoke remains blocked by Xvfb sockets. See [batch evidence](../../../memory/coordination/oct9-bgm-move-batch.md).
- **Outcome:** during a note move, crossing into the BGM area shows amber ghosts in the lanes T-423 would choose, offset by the drag's row delta. Releasing there converts the notes in one undo step, and moving back out returns to a normal move.
- **Read first:** `src/core/interaction.cpp` (`update_note_move_gesture` after T-420), `include/wormedit/core/note_to_bgm.h`, `src/ui/nuklear/compose_canvas.cpp` (move ghosts, BGM lanes).
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `tests/core/` interaction tests, `tests/ui/nuklear_event_harness_tests.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/bga_lane_event_harness_tests.cpp`, this card.
- **Supporting-file ownership (2026-10-09):** coordinator extends Files owned to `tests/sys/frame_timing_tests.cpp` (production input routing and projection regressions); `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_object_input.cpp` (route live BGM move updates/releases); `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp` (transient destination geometry); `src/ui/nuklear/compose_input_panel.cpp` (ignore ghosts during picking). These support the stated gesture outcome. T-420/T-423 are complete; the old blocked status was stale.
- **Additional supporting ownership (2026-10-09):** `tests/ui/bga_lane_event_harness_tests.cpp` contains the legacy drop routing regression; update it to verify live BGM updates/releases instead of the removed cancel-and-convert route.
- **Steps:**
  1. Give the move gesture a `to_bgm` target state when the pointer's lane is a BGM lane. The preview runs the T-423 allocator on the shifted positions.
  2. Draw the amber ghosts, and on release commit through `move_notes_to_bgm` with the row delta applied.
  3. Add tests for in-and-out without a commit and for converting with a row delta.
- **Invariants:** a move that stays in play lanes behaves exactly as in T-420.
- **Reject/failure behavior:** if the allocator rejects, the ghosts show red, and releasing changes nothing.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** dragging BGM events into play lanes.

## T-426 Select all with the same sound, as in uBMSC and pBMSC

- **Status:** done (2026-10-09; normal application build, 2,949/2,949 CTest and 270/270 event-harness cases pass)
- **Result:** primary+Shift/Alt clicks toggle visible/whole-chart sound matches; primary+Shift+A adds from hovered object, selection or Sounds dock rows. Typed note/BGM selection shares a pure indexed selector, preserves hold intersections and visible-layer filters, matches resources or assets according to existing labels, and adds no Undo. Note/Sounds menus and the disabled Edit-menu tooltip are covered. The shortcut audit and user decision are recorded below. See [batch evidence](../../../memory/coordination/oct9-sound-selection-batch.md).
- **Reference:** [pinned pBMSC multiselect notes](../../references/pbmsc-input-handling.md#select-all-with-the-same-label-multiselect).
- **Outcome:** the three pBMSC gestures, adapted to wormedit:
  - **Ctrl+Shift+Click on a note or BGM event** toggles the selection of every *visible* object with the same sound (rows in the viewport, unhidden lanes and layers). A hold counts as visible when it intersects the viewport.
  - **Ctrl+Alt+Click** toggles every object with the same sound in the *whole chart*.
  - **Ctrl+Shift+A** (`selection.select_same_sound_chart`) *adds* every object with the hovered object's sound in the whole chart. With nothing hovered, it uses the sounds of the current selection, then the Sounds dock's selected rows.

  "Same sound" means the same resource. When note labels show file names, it means the same audio asset, as in pBMSC's Show File Name mode. Notes and BGM events are both included. The note context menu gains "Select same sound (visible)" and "Select same sound (whole chart)", and the Sounds dock row menu gains the second one.
- **Read first:** [pinned reference](../../references/pbmsc-input-handling.md), `src/ui/nuklear/compose_input_panel.cpp` (pointer modifiers near `pointer_modifiers`), `src/ui/nuklear/compose_viewport_input.cpp` (Ctrl/Alt uses), `src/sys/commands/application_command_dispatch.cpp` (`selection_select_all`), `src/core/command_registry.cpp`.
- **Files owned:** `src/core/command_registry.cpp`, `src/sys/commands/application_commands.h`, `src/sys/commands/application_commands.cpp`, `src/sys/commands/application_command_dispatch.cpp`, `src/sys/main.cpp` (enablement), `src/sys/compose/input/compose_input.cpp`, `src/ui/frontend.h` (input kind and hovered object), `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_note_menu.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/sys/commands/platform_shortcut_profile.cpp` (default chord), `tests/sys/` selector tests, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/core/sound_selection.h`, `src/core/sound_selection.cpp`, `src/sys/compose/compose_sound_selection.h`, `src/sys/compose/views/compose_view.h`, `src/ui/nuklear/compose_input_panel.h`, `src/ui/nuklear/compose_panel.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/sys/sound_selection_tests.cpp`, `memory/coordination/oct9-sound-selection-batch.md`, `memory/chart-editor-status/resume.md`, `src/sys/compose/views/compose_view.cpp`, `src/ui/workspace_menu.h`, `src/ui/nuklear/frontend.cpp`, `tests/sys/frame_timing_tests.cpp`, this card.
- **Shortcut audit / user decision (2026-10-09):** Ctrl+Shift+Click previously toggled one object through the generic primary-modifier selection path; Ctrl+Alt+Click entered the Alt duration gesture on notes. Ctrl+Shift+A has no built-in binding; no local shortcuts.json override was located. The user approved giving the combined clicks their new sound-selection meanings. Plain Shift/Alt and active beat-fit gestures retain their behavior; macOS uses Cmd.
- **Supporting-file ownership (2026-10-09):** coordinator approved the core selector header/source and build lists for a reusable pure query, the sys helper/state header for shared command/input context, input-panel header and panel for projected hover/source hit testing, selector tests for deterministic coverage, and coordination/resume files for evidence and status. Note labels currently choose BMS identities or file stems automatically; asset matching follows the existing file-stem presentation.
- **Supporting menu ownership (2026-10-09):** coordinator approved `src/sys/compose/views/compose_view.cpp`, `src/ui/workspace_menu.h` and `src/ui/nuklear/frontend.cpp` to expose the registered selection command in Edit and show the required disabled-source tooltip.
- **Supporting command-test ownership (2026-10-09):** coordinator approved `tests/sys/frame_timing_tests.cpp`, whose target already links the application command dispatcher, for real registered-command dispatch and selection/history invariants.
- **Steps:**
  1. Check for conflicts: list the current uses of Ctrl+Shift+Click, Ctrl+Alt+Click and Ctrl+Shift+A on the canvas and in the shortcut profile. **STOP** if one is bound to something else, and ask the user which one wins. On macOS, map Ctrl to Cmd as the platform profile does.
  2. Add a pure selector: (chart, sound or asset key, optional visible range, hidden lanes and layers) → IDs. Test it, including notes and BGM together, hidden-layer exclusion, holds that intersect the viewport, and asset-key matching across two resources that share a file.
  3. Add the two click gestures as compose inputs with toggle semantics, and register the add-only chart command with default Ctrl+Shift+A.
  4. Add the menu entries.
  5. Add event-harness cases for each gesture, toggling twice (back to the start), and Ctrl+Shift+A with hover, with a selection, and with the Sounds dock as the source.
- **Invariants:** selection only, with no document change and no undo entry.
- **Reject/failure behavior:** with no source sound, the command is disabled, and its tooltip says "Hover a note or select a sound first". Clicking a silent note selects nothing.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'select'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** selecting by sound group.

## T-427 Insert lane-key notes with the armed sound

- **Status:** done (2026-10-05; core and event-harness regressions, application build and isolated real-window saved-state checks pass)
- **Result:** lane-key presses carry the Sounds dock source only for insertion. Each held key captures its sound independently; authored and virtual BMS taps/holds validate the sample and apply the same unlocked sound-layer defaults (or explicit palette override) as painting. Placement reports the created note so only a successful insertion advances the dock; removal, rejection and selected-note movement do not. Seven core regressions cover sound/layers, virtual geometry, invalid resources, simultaneous keys, palette kind/override, deletion, locked notes, merge rejection and stale edits; the event harness clicks a sound and checks insertion, selected-note and silent input payloads. The real window saved kick tap resource 21 and snare hold resource 23, advanced the dock, and one Undo restored the exact previous note inventory. Evidence: `/tmp/t427-evidence/` and `/tmp/t427-*.log`.
- **Outcome:** with a sound armed in the Sounds dock and a note kind picked in the palette, pressing a lane key with no notes selected inserts a note (or a hold, if the key is held) that carries the armed sound and its `sound_layer_defaults`, exactly as a Draw stroke would. With "Advance after place" on, the dock then arms the next sound. With no sound armed, lane keys still insert silent notes. After T-412, the armed sound is the Draw tool's current source.
- **Read first:** `include/wormedit/core/interaction.h` (`begin_lane_key_hold_gesture_action_t`, `begin_note_paint_gesture_action_t::sound`), `src/core/interaction.cpp` (lane-key hold begin and release), `src/sys/compose/input/compose_input.cpp` (`dispatch_compose_lane_key_press`, `note_paint_sound`, `advance_sound_after_paint_commit`), `src/ui/nuklear/compose_keyboard_input.cpp` (`process_compose_lane_key`).
- **Cause (found while planning):** Draw strokes carry `input.resource_id` into `begin_note_paint_gesture_action_t::sound`, but `begin_lane_key_hold_gesture_action_t` holds only a lane, and `process_compose_lane_key` never fills `resource_id`.
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose/input/compose_input.cpp`, `src/ui/nuklear/compose_keyboard_input.cpp`, `tests/core/` interaction tests, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, this card.
- **Supporting-file ownership (2026-10-05):** coordinator approved `tests/sys/frame_timing_tests.cpp` in place of the initially proposed notification test file. It owns the existing resource-placement callback recorder, so regressions verify exactly one captured-sound notification per authored/virtual tap or hold, independent concurrent keys, and no notification on rejection, cancellation, silent insertion, deletion, selected-note movement or Undo. Real dock advancement is also verified in authenticated Xvfb.
- **Steps:**
  1. Add `std::optional<resource_id_t> sound` to `begin_lane_key_hold_gesture_action_t`, and write it into the created note, validated as the paint stroke does ("paint sound is not an existing sample resource").
  2. In `process_compose_lane_key`, pass the armed sound ID when no notes are selected, the same value the Draw tool sends.
  3. After a commit that created a note, call `advance_sound_after_paint_commit`.
  4. Write a core test (the inserted note has the sound and layer defaults, and a missing sound rejects atomically) and an event-harness case (arm a sound, pick a note kind, press a lane key, check the note's sound, then undo).
- **Invariants:** with notes selected, lane keys still move them (and Shift does move-and-deselect, T-422). A lane key on an occupied cell still removes the note there, and never re-sounds it.
- **Reject/failure behavior:** a sound deleted after arming makes the insert reject with the reason, and the document is unchanged.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\..*interaction'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** replacing the sound of an existing note from a lane key.
