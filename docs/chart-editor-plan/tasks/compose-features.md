# Task cards: Compose features

[Back to the task-card index](../tasks.md).

These cards replace the former monolithic resume item 1. The requirement text
lives in [editing/layout.md](../editing/layout.md); do not copy it here. Do the
[Compose panel split](compose-split.md) first when a card edits code that the
split moves, because line numbers and file names change. Every card begins with
an **audit** step because parts of each feature are already implemented; record
findings in the card's report, and do not rebuild what exists.

## T-10 Fit the inspector property widgets to the narrow panel

- **Status:** blocked on T-03
- **Outcome:** every widget in `compose_inspector.cpp` renders without clipped labels or overflowing fields at the minimum inspector width, and at 1x and 2x UI scale.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (lines about side panels), `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/chrome.h`, `src/ui/nuklear/theme.h`.
- **Files owned:** `compose_inspector.cpp`, `chrome.h` (only to add a shared width helper), `tests/ui/nuklear_compose_state_tests.cpp` (only if state logic changes).
- **Steps:**
  1. Audit: list every `render_*` inspector function and note which use fixed-width rows (`nk_layout_row_static`, literal pixel widths). Write the list in the report.
  2. For each fixed-width row, switch to a dynamic or ratio row (`nk_layout_row_dynamic`, `nk_layout_row`) with the label ratio taken from one shared constant.
  3. Shorten labels only where a ratio cannot fit; keep the same action emitted.
  4. Capture the smoke screenshot at default size and at the minimum inspector width.
- **Invariants:** emitted `WormUiWorkspaceAction` values and revision checks do not change. No widget is removed.
- **Reject/failure behavior:** a disabled or stale-revision widget stays disabled and emits nothing.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; `scripts/smoke-compose-virtual-x.sh /tmp/t10.png compose` and inspect the PNG for clipped text.
- **Out of scope:** new controls, new panels, theming.

## T-11 Give the BGM/BMSON slice pane independent horizontal scroll

- **Status:** blocked on T-04
- **Outcome:** scrolling the BGM pane sideways never moves playable columns, and both panes keep the same vertical scroll and zoom.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (BGM pane paragraph), `src/sys/compose_view.h` (`bgm_horizontal_scroll_pixels`), `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_state.h`.
- **Files owned:** `src/ui/nuklear/compose_input_panel.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_view.cpp` (only if the clamp is wrong), `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:**
  1. Audit: run `git grep -n bgm_horizontal_scroll` and list where the value is read, written, and clamped. State which of wheel, drag, and autoscroll already route to it.
  2. For each missing route, add the smallest change that routes it to the BGM scroll only.
  3. Add one `TEST_CASE` per newly routed input proving playable-column scroll is unchanged.
  4. Confirm collapsed or offscreen BGM lanes are still exported; do not touch export code.
- **Invariants:** BGM lanes remain present and exportable when offscreen. Scroll values are clamped to the visible-lane range.
- **Reject/failure behavior:** input captured by another pane, or after focus loss, does not change BGM scroll.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`; smoke screenshot showing a chart with more BGM lanes than fit.
- **Out of scope:** momentum and autoscroll acceleration, BMSON slice reset controls (T-12).

## T-12 Add the BMSON slice reset control

- **Status:** blocked on T-11
- **Outcome:** the BMSON slice pane exposes a reset action for the selected slice marker that emits one revision-checked action and is disabled when nothing is selected.
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (BMSON slicing), `src/core/bmson_sound_channel_editor.cpp`, `src/ui/nuklear/compose_inspector.cpp` (`render_staged_bmson_regions`).
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp`, `src/sys/compose_actions.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/core/` (only a new BMSON reset test file).
- **Steps:**
  1. Audit: find the existing BMSON continuation/restart command. If none matches the word "reset", STOP and ask the user which BMSON 1.0 behavior "reset" means.
  2. Add the button and route it through the existing command; do not add a new command unless the audit found none.
  3. Add a core test for the positive case and one for the no-selection rejection leaving the document unchanged.
- **Invariants:** BMSON 1.0 pulse and continuation rules stay normative.
- **Reject/failure behavior:** a stale revision or missing selection changes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^(core|frontend-state)\.'`.
- **Out of scope:** fade, gain, pan, or any BMSON field beyond 1.0.

## T-13 Show the full virtual BMS `#000`–`#999` canvas in Compose

- **Status:** blocked on T-04
- **Outcome:** in a BMS-family chart, Compose's measure rail lists measures up to `#999`, empty measures render as virtual rows, and the first edit in one materializes only that measure.
- **Read first:** `include/wormedit/core/virtual_canvas.h`, `tests/core/virtual_canvas_tests.cpp`, `docs/chart-editor-plan/editing/layout.md` (measure-number column paragraph), `src/core/editor_scene.cpp`.
- **Files owned:** `src/core/editor_scene.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/sys/compose_view.cpp`, `tests/core/virtual_canvas_tests.cpp`.
- **Steps:**
  1. Audit: open a BMS fixture (`tests/fixtures/bms/*.bms`) in the app, scroll past the last authored measure, and record whether virtual rows appear. If they do, mark this card done with the evidence and STOP.
  2. If not, make `editor_scene.cpp` emit virtual measure bands using the existing virtual-canvas helper; do not allocate measure buckets.
  3. Add one core test for a chart with 3 authored measures asserting the scene exposes bands through `#999` with zero new buckets.
  4. Add one test that the first placement in a virtual measure materializes exactly one chunk.
- **Invariants:** imported measure length, meter, branch, and bar-line data stay authoritative. Undo restores the virtual state. No empty source statements are emitted on export.
- **Reject/failure behavior:** a placement past row capacity is rejected, and the chart is byte-identical afterward (see the existing overflow test).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'core\..*virtual'`; smoke screenshot with a BMS fixture scrolled to the tail.
- **Out of scope:** non-BMS virtual tails, performance tuning.

## T-14 Add the waveform song overview and nearby sync controls

- **Status:** blocked-on-user (needs a decision on the overview layout)
- **Outcome:** a dense waveform strip with a viewport window that seeks on click and shows the sample-frame playhead.
- **Read first:** `docs/chart-editor-plan/playback-audio.md`, `src/ui/nuklear/compose_panel.cpp` (`render_compose_waveform_overview`), `src/sys/compose_waveform_overlays.h`.
- **Files owned:** decided with the user in step 1.
- **Steps:**
  1. STOP and ask the user where the overview docks (top strip, right panel, or overlay) and which sync controls beyond the existing offset nudge belong nearby. A reference screenshot from ArrowVortex helps.
  2. Then split into cards of one control each, using the template in the [task index](../tasks.md).
- **Invariants:** non-destructive; offset stays sample-frame exact.
- **Reject/failure behavior:** not applicable until split.
- **Verify:** defined when split.
- **Out of scope:** everything until step 1 is answered.
