# Project chart table: selection, row actions, reordering and batch edits (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-05 on the Project tab chart list (`src/ui/nuklear/setup_panel.cpp`):

- The full-width "New Chart" button should be a large-ish **+** over the list, with clear visual hierarchy.
- "Clear selection" should be a checkbox over the table.
- The row checkbox is a grey square with poor affordance.
- The user asked for a proposal for the five full-width buttons under every row.

Other problems seen in the screenshot: columns overflow into each other ("BilliumMoto & SoUnset"), and Delete is confirmed by inline text.

## Proposal

```text
Charts  1                                                     [ + ]   ← heading; + is a 32 px accent icon button
┌──────────────────────────────────────────────────────────────────────────┐
│ [–] Name        Author        Rating  Mode     Keys  Groups      Notes  Length │ ← tri-state header checkbox
├──────────────────────────────────────────────────────────────────────────┤
│ ⠿ [✓] Simurgh   BilliumMoto…  Unset   BME 7K   7     Unassigned  1002   2:30  ⧉ ⋯ │ ← hover row
│ ⠿ [ ] Another   …                                                         │
└──────────────────────────────────────────────────────────────────────────┘
 2 selected · [Set author…] [Set rating…] [Duplicate] [Delete…]      ✕    ← appears only with a selection
```

- **Rows:** a row is one selectable line. A click on the checkbox toggles it, a click on the row selects only that row, Ctrl+click and Shift+click extend the selection, and a double-click opens the chart in Compose. The selected row is filled with the `selected` color.
- **Checkbox:** a drawn 16 px box with a 1.5 px `border_hover` stroke, an accent fill and a white check when checked, and a dash when partly checked. The hit area is the whole 26 px cell.
- **Row actions:** they show on hover or for the focused row. **Open** (arrow icon) and **⋯**, which opens a `ContextMenu` with Open in Compose, Duplicate, a separator, then Delete…. Right-clicking a row opens the same menu. There are no Move up or Move down items, because the grip and Alt+↑/↓ cover reordering (user review 2026-10-05).
- **Reordering:** drag the **⠿** grip, or press Alt+↑/↓ on the focused row. The Up and Down buttons go away.
- **Delete…:** a `chrome::Dialog` ([T-407](dialogs.md#t-407-add-a-shared-chromedialog-component)): "Delete 'Simurgh'? 1,002 notes. It is removed from groups: …" with `[Cancel] [Delete]`, where Delete is destructive.
- **Batch bar:** the inline author and rating form is replaced by a contextual bar that appears when anything is selected. "Set author…" and "Set rating…" open small popovers with the existing fields. Esc or ✕ clears the selection.

## T-415 Rebuild the chart table header and rows with proper selection

- **Status:** done (2026-10-08; header select-all with partial state, Ctrl/Shift row selection, fixed ellipsized table, after-PNG of the Project tab taken in isolated Xvfb)
- **Stale-plan audit (2026-10-08):** `chrome::Checkbox` (T-436) existed; `fit_label_prefix` was a static helper in `compose_canvas.cpp`, so the same clipping rule is reimplemented locally as `fit_cell_text` in `setup_panel.cpp`. The "New Chart" text button became the "+" button. "Clear selection" was removed.
- **Done so far:** `setup_state.cpp`: `worm_ui_nuklear_click_project_chart` (plain, Ctrl toggle, Shift range from anchor; a Shift-click with no anchor on screen acts as plain), `worm_ui_nuklear_toggle_all_project_charts`, `worm_ui_nuklear_project_selection_summary` (none/partial/all), and the anchor reset in reconcile. `setup_panel.cpp`: a heading row with the count and "+"; a header row with a select-all checkbox (on/mixed/off); a row per chart with checkbox and selectable name; every text cell ellipsized with its full text in a tooltip. The table uses one dynamic row layout whose ratios come from the fixed template: grip 18 px, checkbox 26 px, the eight text columns as shares 22/18/8/12/6/14/8/8 of the remaining width (normalized, since the shares sum to 96), actions 64 px. Tests: `tests/ui/nuklear_setup_state_tests.cpp` (click, Ctrl, Shift, Shift without anchor, select-all and summary, anchor reset). After-PNG: `/home/zardoru/.claude/jobs/4e7ef06b/tmp/t59/out/project-after3-tabs.png`.
- **Deviations to review:** (1) The row's Name cell and checkbox are the click targets; the empty cells are not. (2) The percentage shares sum to 96, so the template normalizes them. (3) The row action strip under each row stays for now, and its "Open in Compose" label became "Open" with the full text as tooltip, so it fits; T-416 replaces the strip. (4) The batch block still prints "N charts selected" beside the heading's count, a duplicate that T-418 should remove. (5) The grip column is an empty cell until T-417.
- **Remaining:** none for this card.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** the table matches the proposal's heading, header and rows. The "+" opens New Chart, the header checkbox selects all, none or shows a partial state, "Clear selection" is gone, columns are ellipsized with the full text in a tooltip, and row selection follows the click rules.
- **Read first:** `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/setup_state.cpp` (selection helpers), `src/ui/nuklear/chrome.h`, `src/ui/nuklear/lucide_icons.h`.
- **Files owned:** `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/setup_state.cpp`, `src/ui/nuklear/setup_state.h`, `src/ui/nuklear/chrome.h` (a drawn `Checkbox` helper), `tests/ui/` setup state tests, `tests/ui/nuklear_event_harness_tests.cpp`, this card. Also touched: `tests/ui/nuklear_setup_state_tests.cpp` (the owned setup state tests).
- **Steps:**
  1. Use `chrome::Checkbox` from T-436.
  2. Use a fixed column template: grip 18, checkbox 26, Name 22%, Author 18%, Rating 8%, Mode 12%, Keys 6%, Groups 14%, Notes 8%, Length 8%, actions 64 px. Ellipsize text with `fit_label_prefix`-style trimming.
  3. Build the heading row with the count and the + icon button.
  4. Add row click, Ctrl and Shift selection to `setup_state.cpp`, and test them.
- **Invariants:** chart action stamps (revision and session generation) are unchanged. Selection reconciles when charts disappear.
- **Reject/failure behavior:** a chart removed mid-selection drops out of the selection silently.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; `ctest --test-dir build -L event-harness`; an Xvfb capture of the Project tab.
- **Out of scope:** row actions (T-416), reordering (T-417) and the batch bar (T-418).

## T-416 Replace the per-row button strip with hover actions and a row menu

- **Status:** partial (T-415 is done; compact actions, menu and dialog landed, hover-only cleanup remains)
- **Done so far:** Rows have compact **Open** and **⋯** actions, right-click and keyboard-opened
  `ContextMenu`, double-click Open, and a shared cancellable Delete dialog. Popup lifecycle closes on
  hidden/stale sessions. Production event-harness coverage is in the `project-charts` cases.
- **Remaining:** Remove the always-present two-button action group so Open/⋯ appear only for the
  hovered or focused row, and remove **Move up/Move down** from the menu once T-417 supplies its
  remaining Alt+arrow path.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** no row has a button strip. Hovering a row shows Open and ⋯, and ⋯ or a right-click opens a `ContextMenu` with Open in Compose, Duplicate and Delete…. There are no move items, because reordering is T-417's grip and Alt+↑/↓. A double-click opens the chart. Delete… asks in a `chrome::Dialog`.
- **Read first:** `src/ui/nuklear/setup_panel.cpp` (`render_chart_actions`, `render_delete_confirmation`), `src/ui/nuklear/context_menu.h`, [dialogs](dialogs.md).
- **Files owned:** `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/setup_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Remove `render_chart_actions` and the inline delete confirmation.
  2. Add the hover icons and the menu, using the existing `WORM_UI_PROJECT_CHART_*` actions.
  3. Ask for delete through `chrome::Dialog` if T-407 has landed. Otherwise, use a ContextMenu confirmation row and note that on this card.
  4. Add event-harness cases for double-click open, each menu item, and delete then cancel.
- **Invariants:** the action payloads are unchanged.
- **Reject/failure behavior:** a stale revision closes the menu or dialog without acting.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** drag reordering (T-417).

## T-417 Reorder charts by dragging the grip or with Alt+Up/Down

- **Status:** partial (T-415 is done; stable grip reorder landed, Alt+arrow remains)
- **Done so far:** The visible grip emits stable source/target/side identity; the core/sys reorder
  creates one Undo entry and rejects stale, missing, adjacent and cancelled drops. The event harness
  covers insertion and Escape/outside/stale cancellation.
- **Remaining:** Bind Alt+Up/Down for the focused row, remove the old Move up/down context-menu
  entries, and verify the insertion line plus keyboard path in isolated Xvfb.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** dragging a row's ⠿ grip shows an insertion line and moves the chart there in one undo step on release. Alt+↑/↓ moves the focused row by one.
- **Read first:** `src/ui/nuklear/setup_panel.cpp`, the core handler for `WORM_UI_PROJECT_CHART_UP`/`DOWN` (`grep -rn PROJECT_CHART_UP src/sys`).
- **Files owned:** `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/setup_state.cpp`,
  `src/ui/nuklear/setup_state.h`, `src/ui/frontend/project.h`, `src/sys/project_view.h`,
  `src/sys/project_view.cpp`, `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`,
  `tests/core/chart_editor_tests.cpp`, `tests/sys/project_view_tests.cpp`,
  `tests/ui/nuklear_event_harness_tests.cpp`, this card and
  `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Add a core command that moves a chart to an index, with a revision check, that rejects atomically. Test it.
  2. Send it through the standard wiring pattern as `WORM_UI_PROJECT_CHART_MOVE_TO`.
  3. Add the drag gesture with an insertion line, Escape to cancel, and the Alt+arrow keys.
- **Invariants:** chart IDs and contents are unchanged, and only the order changes.
- **Reject/failure behavior:** dropping on the row's own position is a no-op with no undo entry.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** reordering across songs.

## T-418 Move batch metadata edits into a contextual selection bar

- **Status:** ready (T-415 is done; dependency cleared 2026-10-19)
- **Stale-plan audit (2026-10-19):** The existing selected-chart block already emits the batch
  metadata action and exposes author/rating fields, but it is still an inline form with a duplicate
  selection count and **Clear selection** text. The SelectionBar, popovers, Duplicate/Delete and
  close affordance in this outcome remain unimplemented.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** with one or more charts selected, a bar under the table reads "N selected · [Set author…] [Set rating…] [Duplicate] [Delete…] ✕". Each "Set…" opens a small popover with the existing fields and an Apply button. The inline author and rating form is gone.
- **Read first:** `src/ui/nuklear/setup_panel.cpp` (`render_project_batch`), `src/ui/nuklear/popup_lifecycle.h`.
- **Files owned:** `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/setup_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Replace `render_project_batch` with a `chrome::SelectionBar` (T-436), shown at one selected chart or more.
  2. Build the author popover (text field and Apply) and the rating popover (numerator, denominator, an Unset checkbox and Apply). Both emit `WORM_UI_PROJECT_CHART_BATCH_METADATA` as now.
  3. Add event-harness cases for setting the author on two charts and unsetting the rating.
- **Invariants:** the batch metadata payload is unchanged.
- **Reject/failure behavior:** an invalid rating keeps the popover open with the reason shown under the fields.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** the StepMania export policy form below the table.
