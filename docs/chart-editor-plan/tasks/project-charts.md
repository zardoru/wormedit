# Project chart table: selection, row actions, reordering and batch edits (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-05 on the Project tab chart list (`src/ui/nuklear/project/setup_panel.cpp`):

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
- **Read first:** `src/ui/nuklear/project/setup_panel.cpp`, `src/ui/nuklear/project/setup_state.cpp` (selection helpers), `src/ui/nuklear/chrome.h`, `src/ui/nuklear/render/lucide_icons.h`.
- **Files owned:** `src/ui/nuklear/project/setup_panel.cpp`, `src/ui/nuklear/project/setup_state.cpp`, `src/ui/nuklear/project/setup_state.h`, `src/ui/nuklear/chrome.h` (a drawn `Checkbox` helper), `tests/ui/` setup state tests, `tests/ui/nuklear_event_harness_tests.cpp`, this card. Also touched: `tests/ui/nuklear_setup_state_tests.cpp` (the owned setup state tests).
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

- **Status:** done (2026-10-21; hover/focus icons, row menu and dialog verified)
- **Done so far:** Rows have compact **Open** and **⋯** actions, right-click and keyboard-opened
  `ContextMenu`, double-click Open, and a shared cancellable Delete dialog. Popup lifecycle closes on
  hidden/stale sessions. Production event-harness coverage is in the `project-charts` cases.
- **Completion:** Open and menu are shared native icon cells, shown only on hovered/focused
  rows. Hidden cells reserve the same table space; hover cannot change row heights. Right-click
  works across the row; raw focus and double-click handlers cannot act through an open popup.
  A new/hidden session drops chart focus even when stable IDs are reused. T-417 removed the
  old Move items; Open, Duplicate, Delete confirmation/cancel and double-click Open remain.
- **Verification:** GATE passes **4,409/4,409**, build, whitespace and three-TU complexity
  (`/tmp/t416-gate-final.log`, `/tmp/wormedit-gate.R11BXK`, `/tmp/wormedit-complexity.ae_mbi4c`).
  Explicit event harness **603/603** and focused project/tooltip **432 assertions / 26 cases**
  pass (`/tmp/t416-events.log`, `/tmp/t416-focused-final4.log`). Default production clang-tidy
  passes (`/tmp/t416-tidy-final2.log`). Isolated wide and 800×700 captures inspected:
  `/tmp/t416-{idle,hover,focus,menu,narrow}.png`; reproduction `/tmp/t416-visual-steps.sh`.
- **Supporting ownership (2026-10-21):** add
  `tests/ui/project_chart_actions_event_harness_tests.cpp` to **Files owned** for visibility,
  focus and action-emission regressions beside T-417's feature cases. Existing general harness
  tooltip/menu tests switch from persistent text buttons to native icon lookup after row hover.
  `cmake/complexity_baseline.txt` is also owned for reviewed decreases only: one legacy test's
  allowance falls **65 → 61**, and obsolete **26/28** allowances are removed. No limit increases.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** no row has a button strip. Hovering a row shows Open and ⋯, and ⋯ or a right-click opens a `ContextMenu` with Open in Compose, Duplicate and Delete…. There are no move items, because reordering is T-417's grip and Alt+↑/↓. A double-click opens the chart. Delete… asks in a `chrome::Dialog`.
- **Read first:** `src/ui/nuklear/project/setup_panel.cpp` (`render_chart_actions`, `render_delete_confirmation`), `src/ui/nuklear/context_menu.h`, [dialogs](dialogs.md).
- **Files owned:** `src/ui/nuklear/project/setup_panel.cpp`, `src/ui/nuklear/project/setup_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`, `src/sys/project/project_view.h`, `src/sys/project/project_view.cpp`, `tests/core/chart_editor_tests.cpp`, `tests/sys/project_view_tests.cpp`, `include/wormedit/core/metadata_rating.h`, `include/wormedit/core/metadata_editor.h`, `src/ui/nuklear/popup_lifecycle.h`, `src/ui/nuklear/context_menu.cpp`, `tests/ui/popup_menu_event_harness_tests.cpp`, `tests/ui/project_chart_actions_event_harness_tests.cpp`, this card and `memory/chart-editor-status/resume.md`.
- **Supporting ownership (2026-10-21):** selection-bar Duplicate/Delete must apply the entire selection in one transaction; extend the existing commands and reuse their ID remapping/membership rules. Expose the existing exact rating parser through a vocabulary-only header so the popover can show invalid-input feedback without importing document/command types or duplicating parsing. Metadata action payloads stay unchanged. Reuse the existing context-menu bounds calculation through the shared popup header; the keyboard-menu regression must distinguish menu-only controls from the new persistent selection-bar buttons.
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

- **Status:** done (2026-10-21; focused Alt+arrow, stable grip reorder and single-step Undo verified)
- **Done so far:** The visible grip emits stable source/target/side identity; the core/sys reorder
  creates one Undo entry and rejects stale, missing, adjacent and cancelled drops. The event harness
  covers insertion and Escape/outside/stale cancellation.
- **Completion:** Alt+Up/Down consumes admitted shortcuts for the focused stable chart ID,
  using the current revision/session and existing UP/DOWN command path. Boundaries do nothing;
  popup/modal, focus, session and removed-row guards publish nothing. The old Move up/down
  menu items are removed. No core edit or drag lifecycle was duplicated.
- **Verification:** GATE passes **4,397/4,397**, build, whitespace and five-TU complexity
  (`/tmp/t417-gate.log`, `/tmp/wormedit-gate.hSuGZK`, `/tmp/wormedit-complexity.ng2cr6p8`).
  Explicit core **2,418/2,418** and event harness **599/599** pass; focused project cases pass
  **123 assertions / 10 cases**. Default clang-tidy passes on both affected production TUs
  (`/tmp/t417-tidy-{setup,frontend}.log`). Isolated Xvfb captures inspected:
  `/tmp/t417-keyboard-{up,undo}.png`, `/tmp/t417-drag-{preview,committed,undo}.png`.
- **Supporting ownership (2026-10-21):** `src/ui/nuklear/frontend.cpp` and
  `src/ui/nuklear/project/setup_panel.h` consume admitted shortcuts through the existing focus/modal
  gates, before command dispatch. New `tests/ui/project_chart_actions_event_harness_tests.cpp`
  and `tests/CMakeLists.txt` keep new feature regressions out of the general harness file;
  `tests/ui/popup_menu_event_harness_tests.cpp` must use a remaining menu-only label.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** dragging a row's ⠿ grip shows an insertion line and moves the chart there in one undo step on release. Alt+↑/↓ moves the focused row by one.
- **Read first:** `src/ui/nuklear/project/setup_panel.cpp`, the core handler for `WORM_UI_PROJECT_CHART_UP`/`DOWN` (`grep -rn PROJECT_CHART_UP src/sys`).
- **Files owned:** `src/ui/nuklear/project/setup_panel.cpp`, `src/ui/nuklear/project/setup_state.cpp`,
  `src/ui/nuklear/project/setup_state.h`, `src/ui/frontend/project.h`, `src/sys/project/project_view.h`,
  `src/sys/project/project_view.cpp`, `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`,
  `tests/core/chart_editor_tests.cpp`, `tests/sys/project_view_tests.cpp`,
  `tests/ui/nuklear_event_harness_tests.cpp`, this card and
  `memory/chart-editor-status/resume.md`, `src/ui/nuklear/frontend.cpp`,
  `src/ui/nuklear/project/setup_panel.h`, `tests/ui/project_chart_actions_event_harness_tests.cpp`,
  `tests/CMakeLists.txt`, `tests/ui/popup_menu_event_harness_tests.cpp`.
- **Steps:**
  1. Add a core command that moves a chart to an index, with a revision check, that rejects atomically. Test it.
  2. Send it through the standard wiring pattern as `WORM_UI_PROJECT_CHART_MOVE_TO`.
  3. Add the drag gesture with an insertion line, Escape to cancel, and the Alt+arrow keys.
- **Invariants:** chart IDs and contents are unchanged, and only the order changes.
- **Reject/failure behavior:** dropping on the row's own position is a no-op with no undo entry.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^core\.'`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** reordering across songs.

## T-418 Move batch metadata edits into a contextual selection bar

- **Status:** done (2026-10-21)
- **Stale-plan audit (2026-10-19):** The existing selected-chart block already emits the batch
  metadata action and exposes author/rating fields, but it is still an inline form with a duplicate
  selection count and **Clear selection** text. The SelectionBar, popovers, Duplicate/Delete and
  close affordance in this outcome remain unimplemented.
- **Mockup:** [chart-table.svg](../mockups/chart-table.svg) (reference: match its structure and hierarchy)
- **Outcome:** with one or more charts selected, a bar under the table reads "N selected · [Set author…] [Set rating…] [Duplicate] [Delete…] ✕". Each "Set…" opens a small popover with the existing fields and an Apply button. The inline author and rating form is gone.
- **Read first:** `src/ui/nuklear/project/setup_panel.cpp` (`render_project_batch`), `src/ui/nuklear/popup_lifecycle.h`.
- **Files owned:** `src/ui/nuklear/project/setup_panel.cpp`, `src/ui/nuklear/project/setup_state.h`, `tests/ui/nuklear_event_harness_tests.cpp`, `include/wormedit/core/chart_editor.h`, `src/core/chart_editor.cpp`, `src/sys/project/project_view.h`, `src/sys/project/project_view.cpp`, `tests/core/chart_editor_tests.cpp`, `tests/sys/project_view_tests.cpp`, `include/wormedit/core/metadata_rating.h`, `include/wormedit/core/metadata_editor.h`, `src/ui/nuklear/popup_lifecycle.h`, `src/ui/nuklear/context_menu.cpp`, `tests/ui/popup_menu_event_harness_tests.cpp`, this card and `memory/chart-editor-status/resume.md`.
- **Supporting ownership (2026-10-21):** selection-bar Duplicate/Delete must apply the entire selection in one transaction; extend the existing commands and reuse their ID remapping/membership rules. Expose the existing exact rating parser through a vocabulary-only header so the popover can show invalid-input feedback without importing document/command types or duplicating parsing. Metadata action payloads stay unchanged. Reuse the existing context-menu bounds calculation through the shared popup header; the keyboard-menu regression must distinguish menu-only controls from the new persistent selection-bar buttons.
- **Steps:**
  1. Replace `render_project_batch` with a `chrome::SelectionBar` (T-436), shown at one selected chart or more.
  2. Build the author popover (text field and Apply) and the rating popover (numerator, denominator, an Unset checkbox and Apply). Both emit `WORM_UI_PROJECT_CHART_BATCH_METADATA` as now.
  3. Add event-harness cases for setting the author on two charts and unsetting the rating.
- **Invariants:** the batch metadata payload is unchanged.
- **Reject/failure behavior:** an invalid rating keeps the popover open with the reason shown under the fields.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`.
- **Verification (2026-10-21):** `scripts/gate.sh --jobs 8` passes build, **4,395/4,395 tests**, whitespace and complexity across eight changed translation units (`/tmp/t418-gate2.log`, `/tmp/wormedit-gate.EqJZMJ`). The shared popup-bounds helper was renamed with `clang-refactor local-rename` before moving it unchanged to the lifecycle header. Focus waits for the opening click to release before selecting the text field. Core regressions cover linked-ID remapping, complete membership removal, one Undo boundary and rollback when a later copy exhausts IDs. Event regressions cover two-chart author/unset edits, invalid rating feedback, stale/Escape/cancel/clear handling and selected Duplicate/Delete confirmation. Real isolated Xvfb screenshots `/tmp/t418-{bar-one,bar-two,author,rating,narrow}.png` were inspected, including the 800×700 bar; deleting every chart is disabled. No complexity allowance or metadata payload changed.
- **Out of scope:** the StepMania export policy form below the table.
