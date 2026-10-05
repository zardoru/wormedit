# Draw tool: one way to place notes, an explicit layer brush (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

The user found the paint functions ambiguous. Four controls share the paintbrush
icon and the word "paint":

| Control | Mechanism today | What it does |
| --- | --- | --- |
| Toolbar brush | `note_paint_mode_active` | Drag draws, or erases, notes with some sound |
| Sounds dock brush | `resource_pointer_mode::place` (sound) | Click places a note with the selected sound |
| BGA dock brush | `resource_pointer_mode::place` (BGA) | Click places the selected BGA |
| Inspector "Paint: Armed/Off" | `semantic_layer_paint_mode_active` | Drag *toggles* layer membership |

The user's decision (2026-10-05): one **Draw** tool and one **Layer brush** in
the toolbar, next to **Select**. Draw always places the *current source*: the
sound or BGA the user last picked in a dock, or a silent note when none is
picked. The dock brush buttons are removed, and picking a row only sets the
source. Erasing is an explicit right-drag; a right-click without movement still
opens the context menu. The Layer brush adds membership, and Alt+drag removes
it. There is no toggle.

## T-412 Merge note paint and resource placement into one Draw tool

- **Status:** ready
- **Mockup:** [field-move-collision.svg](../mockups/field-move-collision.svg) (reference: match its structure and hierarchy)
- **Outcome:** the toolbar shows Select (Alt+1), Draw (Alt+2) and Layer (Alt+3). With Draw active, clicking or dragging on a key lane places notes with the current source, a BGA source places on the BGA lane, and right-drag erases. The Sounds and BGA docks have no brush button, and clicking a row sets the source.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp` (paint and select buttons), `src/ui/nuklear/compose_sounds_panel.cpp` (`toggle_paint`, `paint_allowed`), `src/ui/nuklear/compose_bga_panel.cpp` (brush), `include/wormedit/core/interaction.h` (`begin_note_paint_gesture_action_t`), `src/sys/compose_input.cpp` (note-paint stroke handling).
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/compose_object_input.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`, `src/ui/frontend.h`, `src/sys/compose_input.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/core/interaction.cpp` and `include/wormedit/core/interaction.h` (only if BGA strokes or the erase flag need core support), `src/core/command_registry.cpp` (Alt+2/Alt+3), `tests/core/`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Record on this card how a stroke today chooses between drawing and erasing, and where its sound comes from.
  2. Add a `draw_source` to the Compose UI state: none, a sound ID or a BGA ID. A click on a Sounds or BGA row sets it, and the view projects it.
  3. Send the Draw tool through the existing note-paint stroke with `sound = draw_source`, so a stroke started on an existing note no longer erases. Add an explicit erase flag for right-drag.
  4. Send a BGA source as a placement stroke on the BGA lane, reusing the resource-place commit, and retire `resource_pointer_mode::place` for Draw.
  5. Remove the dock brush buttons. Keep "Advance after place" in the Sounds dock as a Draw option.
  6. Add event-harness cases: draw a silent note, draw with a sound, draw a BGA, erase with right-drag, open the context menu with a right-click, and use Alt+1/2/3.
- **Invariants:** each stroke is one undo step. The 50,000-target stroke cap is unchanged. Replace mode, which swaps the sound of existing notes, stays reachable from the note context menu.
- **Reject/failure behavior:** in Timing focus or on a read-only chart, Draw is disabled and its tooltip says why. A source deleted mid-stroke cancels the stroke without committing.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`; `clang-tidy -p build` on changed sources.
- **Out of scope:** the layer brush (T-413), the status chip (T-414) and retiring the inspector resource list (T-389, which should land after this card).

## T-413 Make the layer brush add, with Alt+drag to remove

- **Status:** blocked-on T-412
- **Outcome:** with the Layer tool and a target layer chosen in the Layers dock, dragging over notes and BGM events adds them to the layer, and Alt+drag removes them. Dragging over a mix of members and non-members never flips any of them. The inspector "Paint: Armed/Off" button is gone.
- **Read first:** `include/wormedit/core/interaction.h` (`begin_semantic_layer_paint_gesture_action_t`), `src/core/interaction.cpp` (layer paint), `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp` (`render_semantic_layer_target_buttons`).
- **Files owned:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `src/sys/compose_input.cpp`, `src/ui/nuklear/compose_viewport_input.cpp`, `src/ui/nuklear/compose_layers_panel.cpp`, `src/ui/nuklear/compose_inspector.cpp`, `tests/core/` interaction tests, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Replace the core gesture's toggle with an `add | remove` mode. Keep `replace_memberships` and `clear_memberships`.
  2. The Layers dock row sets the target layer. The Layer tool is disabled until a target is set, and its tooltip says "Pick a layer in the Layers dock".
  3. Remove the inspector paint button. If T-393 has not landed, coordinate with it.
  4. Write core tests (mixed set with add, mixed set with remove, a locked layer rejects) and event-harness cases.
- **Invariants:** layer painting never creates, deletes or moves notes, and each stroke is one undo step.
- **Reject/failure behavior:** a locked or hidden layer rejects the stroke atomically.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\..*layer'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** layer color views.

## T-414 Show the active tool and source in a canvas chip

- **Status:** blocked-on T-412
- **Mockup:** [field-move-collision.svg](../mockups/field-move-collision.svg) (reference: match its structure and hierarchy)
- **Outcome:** while Draw or Layer is active, a small chip at the top left of the field reads, for example, "✎ Draw · 0A kick.wav · right-drag erases", "✎ Draw · silent note" or "◈ Layer · Drums · Alt+drag removes". Clicking the chip's source name reveals that row in its dock.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (the status hint near "Paint armed"), `src/ui/nuklear/chrome.h`.
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/frontend.h` (source label fields), `src/sys/compose_view.cpp`, `tests/ui/` event-harness tests, this card.
- **Steps:**
  1. Project the source label (sound ID and name, BGA ID and name, or "silent note") and the target layer name.
  2. Draw the chip with the menu surface style at 12 px from the field corner, and replace the old "Paint armed …" hint strings.
  3. Make a click on the source name reveal the row in its dock and scroll it into view.
  4. Take smoke captures of each state.
- **Invariants:** the chip never takes canvas input outside its own bounds.
- **Reject/failure behavior:** a source deleted since it was picked shows "missing source" in the warning color, and Draw falls back to silent notes only after the user picks again.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`; smoke captures.
- **Out of scope:** the toolbar icons themselves.
