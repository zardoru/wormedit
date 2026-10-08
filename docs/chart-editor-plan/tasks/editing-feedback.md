# Editing feedback: one-click quantize controls and visible move collisions (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-05:

- The quantize "Presets and settings" block makes you select text and type numbers. Quick snap access is high value, so it should be single clicks with a mouse.
- Moving notes onto occupied cells silently stops following the pointer. Collisions should show as red ghosts instead. An optional "overwrite" setting should let the move win, sending each displaced note to the first free BGM lane rather than deleting it.

Cause of the frozen move, found while planning: `update_note_move_gesture`
(`src/core/interaction.cpp`) returns only diagnostics when `make_move_plan`
rejects a collision. It never updates `preview`, so the ghosts stay at the last
valid position.

## T-419 Replace the quantize settings form with one-click controls

- **Status:** done
- **Mockup:** [quantize.svg](../mockups/quantize.svg) (reference: match its structure and hierarchy)
- **Outcome:** the quantize section matches the layout below. Every setting changes with one click, a custom grid is the only typed value and sits behind "⋯", and presets are chips. An event-harness case sets 1/16, Authored and Later with three clicks and applies.
- **Layout:** see the mockup. Changes from the user's review on 2026-10-05:
  - **Grid on a grid:** even subdivisions on one row (1/4, 1/8, 1/16, 1/32, 1/64), and their triplet counterparts column-aligned below (1/6, 1/12, 1/24, 1/48, 1/96), so each triplet sits under the value it divides. "custom…" spans the width below. "1/n" means n divisions per standard measure, using the same value the text field takes today.
  - **Ties renamed "Halfway notes":** Round earlier | Round later | Block apply. The header shows how many selected notes sit exactly halfway between two grid lines (for example "2 in selection"). The row dims when the count is 0.
  - **Holds renamed "Hold tails":** Leave in place | Snap too, with the count of holds in the selection. Leave in place is today's `start_only`, which snaps only the head, so the hold's length changes. Snap too is `both_endpoints`.
  - **Tooltips:** each segment states its effect in one sentence. For example, Block apply's tooltip is "Don't quantize if any note is exactly halfway; those notes are listed."
  - **Field preview:** before and after ghosts on the field are T-437.
- **Read first:** `src/ui/nuklear/compose_inspector.cpp` (`render_quantize_preset_picker`, `render_quantize_setting_fields`, `render_quantize_preset_actions`, `render_compose_quantize_section`), `src/ui/nuklear/chrome.h` (`button_style`, `mode_button`), `src/ui/nuklear/context_menu.h`.
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp`, `src/sys/compose_quantize_preview.cpp`, `src/sys/compose_quantize_preview.h`, `src/sys/compose_view.cpp`, `src/ui/frontend.h` (count fields), `tests/ui/nuklear_event_harness_tests.cpp`, `src/sys/compose_actions.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/ui/quantize_report_event_harness_tests.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `src/ui/nuklear/compose_inspector.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/frontend.cpp`, this card. Supporting extensions approved by root on 2026-10-08: the existing dispatcher needs Rename; existing preview tests verify cached counts; existing action-notification tests exercise the real preset dispatcher; existing report harness assertions cover the expanded inspector. Popup hooks in inspector header, Compose panel, and frontend are an approved sequential support loan for input ownership and reset; T-414 waits for their release. Dispatcher editing waits for T-413 release.
- **Steps:**
  1. Use `chrome::Segmented` and `chrome::ChipGrid` from T-436.
  2. Replace the grid text field with the two-row chip grid and a "custom…" popover holding the existing validated field and an Apply button.
  3. Replace the three combos with segmented controls.
  4. Replace the preset combo and the Save and Delete buttons with chips. "+" opens a name popover. A right-click on a chip opens a `ContextMenu` with Rename and Delete.
  5. Project the halfway-note count and hold count from the existing quantize preview (add the fields if they are missing). Show the preview counts in the heading line, and show "Apply quantization (Q)" if Q is bound. Otherwise leave out the shortcut hint.
  6. Remove the "Presets and settings" tree, so the controls are always visible.
- **Invariants:** `WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS` and the preset action payloads are byte-identical. A custom grid is validated exactly as now.
- **Reject/failure behavior:** an invalid custom grid keeps the popover open with the reason shown under the field. A blocked preview disables Apply, and its tooltip gives the reason.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'quantize'`; `ctest --test-dir build -L event-harness`; an Xvfb capture of the inspector; `clang-tidy -p build src/ui/nuklear/compose_inspector.cpp`.
- **Completion (2026-10-08):** implemented the aligned grid chips, segmented settings, retained halfway/hold counts, custom validation, preset save/select/rename/delete, and popup input/lifecycle ownership. Existing action values and payloads remain unchanged; Rename is appended. Q is currently unbound and its hint is omitted. Discarding an invalid draft restores the current settings, and underlying controls cannot replace the popup or open competing tooltips.
- **Validation:** ordinary optimized app/core/event/frame-dispatcher builds passed; the actual three-click and lifecycle quantize event cases passed (11 cases, 185 assertions), the full event label passed (234 tests), core quantize/preview cases passed (34 cases, 169 assertions), and real rename dispatcher regressions passed (3 cases, 30 assertions). The card-required quantize CTest filter passed 36 tests. Configured header-inclusive complexity checks added no violations and increased no existing scores. Documentation checks and diff whitespace checks passed. An isolated SDL dummy/software capture linked the normal application objects; no desktop session was opened.
- **Count cost:** counts run only on retained-preview cache misses and reuse validated measure geometry. The matched dense no-move/blocked fixture retained its early report return and added approximately 0.33 ms (50k notes) / 0.66 ms (100k notes) of cold index/count work; 240 warm hits at each size made zero core calls. Bounded arithmetic rejects invalid/nonrepresentable grid steps instead of wrapping halfway counts.
- **Out of scope:** the toolbar edit-snap control (consider the same chips there later) and the quantize algorithm.

## T-437 Preview quantization as before and after ghosts on the field

- **Status:** ready
- **Mockup:** [quantize.svg](../mockups/quantize.svg) (reference: match its structure and hierarchy)
- **Outcome:** while notes are selected and the quantize section is visible, every note that would move shows a ghost at its quantized position, joined by a thin line to its current one. Halfway notes are outlined in amber, and so are hold tails that stay in place. Colliding results are red, as in T-420. Changing any setting updates the ghosts on the next frame, so the effect of Halfway notes and Hold tails can be seen rather than read about.
- **Read first:** `src/sys/compose_quantize_preview.h` (`quantize_report_row_t`), `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp` (ghost drawing from T-420).
- **Files owned:** `src/sys/compose_quantize_preview.cpp`, `src/sys/compose_quantize_preview.h`, `src/sys/compose_view.cpp`, `src/sys/compose_view.h`, `src/ui/frontend.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_canvas.h`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_inspector.h`, `tests/sys/compose_quantize_preview_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card. Supporting files approved by root on 2026-10-08: projection header retains bounded geometry; existing frame tests cover actual dispatcher/projection; panel and narrow inspector accessors gate ghosts on actual section visibility and clipping; the canvas header exposes a post-dock dynamic overlay entry so current-frame visibility never uses a stale borrowed view.
- **Steps:**
  1. Add a `halfway` flag to each report row, and project the visible rows (the before and after rows, tails, and the collision flag) into the Compose view.
  2. Draw a dashed ghost at each new position, joined to the note's current position, with the amber or red variants. Clip the drawing to the visible rows.
  3. Show ghosts only while the inspector's quantize section is on screen and target geometry is valid. Collision-only command rejection still shows red target ghosts while Apply stays disabled; other blocked previews draw none.
  4. Add a test: a halfway note flips its ghost between the rows above and below when the setting changes from Round earlier to Round later.
- **Invariants:** the preview never changes the document. Ghost drawing is bounded by the report capacity (256 rows). Notes beyond that capacity are counted but not drawn.
- **Reject/failure behavior:** invalid settings, unsupported geometry or Block-halfway rejection draw no ghosts and show the reason. A collision-only rejection retains valid red preview geometry but does not enable Apply or change command policy.
- **Coordinator clarification (2026-10-08):** the original outcome requested red collisions while step 3 hid every rejected command. Root distinguishes valid preview geometry from commit eligibility so the default collision-reject policy remains useful to preview. Supporting scope approved: `src/sys/compose_view.h` for bounded retained geometry, `tests/sys/frame_timing_tests.cpp` for actual dispatcher/projection regressions, `src/ui/nuklear/compose_panel.cpp` and narrow `compose_inspector.cpp`/`.h` accessors for section visibility/clipping. Feedback records these exact files in ownership before implementation; unrelated inspector and canvas input behavior stay unchanged.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'quantize'`; `ctest --test-dir build -L event-harness`; smoke capture.
- **Metadata checkpoint (2026-10-08):** retained rows now carry selected/head-tail halfway/stationary-tail flags, checked native/promoted head and tail coordinates, and typed geometry eligibility. Collision-only failures are drawable while Apply remains blocked; Block-halfway and non-note/invalid failures remain hidden. Stationary tail annotations reuse the cold selected-count scan, retain the 256-row cap, and preserve the no-move collision-group shortcut. Ordinary core target passes; focused preview suite passes 33 cases/189 assertions and the card quantize filter 35/35. Configured header-inclusive complexity checks emit no violations. Live projection/rendering and final cost verification remain pending.
- **Out of scope:** previewing quantization of areas without a selection.

## T-420 Show colliding move targets as red ghosts instead of freezing

- **Status:** done
- **Mockup:** [field-move-collision.svg](../mockups/field-move-collision.svg) (reference: match its structure and hierarchy)
- **Outcome:** while dragging notes, the ghosts always follow the pointer. Any ghost whose target cell is occupied, or illegal, is drawn red with the occupant outlined in red. Releasing over a collision commits nothing and returns the notes to where they were, and the status line says "N notes would overlap". A core test and an event-harness case cover a drag into, through and back out of a collision.
- **Read first:** `src/core/interaction.cpp` (`update_note_move_gesture`, `move_previews`, `make_move_plan`), `include/wormedit/core/interaction.h` (`note_move_preview_t`, `pending_note_move_gesture_t`), `src/ui/nuklear/compose_canvas.cpp` (move ghost drawing), `src/sys/compose_view.cpp` (ghost projection).
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h` (ghost conflict flag and occupant IDs), `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/theme.h` (a `conflict` color role), `src/ui/nuklear/theme.cpp`, `assets/themes/ui-dark.json` (the builtin and bundled conflict role), `tests/core/interaction_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `tests/core/editor_scene_tests.cpp`, `src/sys/compose_input.cpp`, `tests/sys/frame_timing_tests.cpp`, this card. Exact scene/input/test/theme support extensions approved by root on 2026-10-08: scene geometry must retain invalid lane proposals, the existing input dispatcher tests verify release feedback, and the appended theme role needs builtin/bundled defaults with existing user-theme fallback. T-420 begins with interaction/scene core and its tests only; frontend/projection/canvas/Compose input and shared UI/sys tests wait for T-414 integration and release.
- **Steps:**
  1. Split planning into geometry, which always succeeds while the target is inside the chart, and validation, which yields a per-note conflict and the occupying object ID.
  2. Give `note_move_preview_t` a `conflict` field (none, occupied, out of lane or other) and `std::optional<object_id_t> occupant`. Update the preview on every pointer update, colliding or not.
  3. On end, commit only if no preview has a conflict. Otherwise, cancel with a warning and leave the document revision unchanged.
  4. Draw conflicting ghosts with the conflict color at 60% alpha and outline their occupants with 2 px of the conflict color. Valid ghosts keep today's style.
  5. Test that a drag into a collision, back out, and release commits at the final valid cell, and that a release while colliding changes nothing.
- **Invariants:** a non-colliding move commits exactly as today, in one undo step. Hold endpoints keep their spacing.
- **Reject/failure behavior:** a stale revision cancels the gesture as now, and a colliding release is a no-op.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\..*(move|interaction)'`; `ctest --test-dir build -L event-harness`; `clang-tidy -p build src/core/interaction.cpp`.
- **Verification:** ordinary optimized app/core/event/frame targets pass. Core `[t420]` passes 18 cases/344 assertions, card move/interaction ctest 110/110, and scene suite 48 cases/8,595 assertions. Native event harness passes 242/242, including collision/recovery and rejected release; actual dispatcher `[t420]` passes 5 cases/74 assertions and the full frame suite 322 cases/240,373 assertions. Dispatcher coverage proves repeated motion has no notifications, conflict release emits one warning without history changes, recovery commits one undo with exact hold spacing, and unavailable-lane/outside geometry routes correctly. Configured header-inclusive tidy retains all 120 existing warning identities/scores with no new or increased violations. Root inspected actual production-object dummy/software conflict and recovered hold/tap captures.
- **Performance evidence:** matched optimized 100k-note move updates filter unrelated existing intervals against merged moved ranges before the closed sweep. Valid 1-note CPU p99 changes from 11.409 to 1.796 ms and valid 256-note from 14.375 to 2.474 ms; remaining allocations scale with the selected/overlapping notes. Collision timings differ in behavior because the new path retains all red proposals and occupants whereas the baseline rejects early. Scratch probe logs/manifests and measured/final code-equivalence proof are retained; no display frame-time claim.
- **Out of scope:** the overwrite policy (T-421). Paste and duration drags can reuse the conflict field in a later card.

## T-421 Add an "overwrite on move" setting that sends displaced notes to BGM

- **Status:** blocked-on T-423
- **Mockup:** [field-move-collision.svg](../mockups/field-move-collision.svg) (reference: match its structure and hierarchy)
- **Outcome:** with Options → Editing → "When a move lands on a note" set to "Move it to a BGM lane", releasing over occupied cells commits the move and moves each occupant to the first BGM lane free at its row, keeping its sound, in one undo step. During the drag, those occupants show an amber "will move to BGM" ghost in their BGM destination instead of red. The default stays "Reject".
- **Read first:** `src/core/interaction.cpp` (after T-420), `src/sys/preferences_runtime.cpp`, `src/ui/nuklear/options_window.cpp`, the core BGM lane model (`grep -rn bgm_lane include/wormedit/core`).
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/preferences_runtime.cpp`, `src/sys/preferences_runtime.h`, `src/ui/nuklear/options_window.cpp`, `src/ui/frontend.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `tests/core/` interaction tests, settings round-trip tests, this card.
- **Steps:**
  1. Add a persisted preference `move_collision_policy` with the values `reject` and `displace_to_bgm`, and test that it round-trips.
  2. In validation, under `displace_to_bgm`, resolve the occupants' destinations with the group-aware allocator from [T-423](bms-editing.md#t-423-add-the-group-aware-convert-notes-to-bgm-events-core-command), using the same rule as Move to BGM. Record each one as a `displace` conflict with its destination.
  3. Commit the move and the displacements as one document command.
  4. Draw the amber destination ghosts, and say "N notes will move to BGM" in the status line.
  5. Write core tests: one displacement, displacement when all BGM lanes are busy (a new lane is created), a format without extra BGM lanes (the move is rejected, as under `reject`), and undo restoring everything.
- **Invariants:** no note is ever deleted by a move. A displaced note keeps its sound, row, length (as a BGM event, without a hold tail) and layer memberships.
- **Reject/failure behavior:** if any displacement has no destination, the whole move is rejected and its ghosts show red, as in T-420.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** applying the policy to paste and draw strokes. Add a follow-up card once the user has tried it on moves.
