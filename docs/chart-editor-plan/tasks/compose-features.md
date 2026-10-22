# Task cards: Compose features

[Back to the task-card index](../tasks.md).

These cards replace the former monolithic resume item 1. The requirement text
lives in [editing/layout.md](../editing/layout.md); do not copy it here. Do the
[Compose panel split](compose-split.md) first when a card edits code that the
split moves, because line numbers and file names change. Every card begins with
an **audit** step because parts of each feature are already implemented; record
findings in the card's report, and do not rebuild what exists.

## T-10 Fit the inspector property widgets to the narrow panel

- **Status:** done (2026-09-30; owned inspector widgets checked at 280 logical pixels and simulated 2x scale)
- **Outcome:** every widget in `compose_inspector.cpp` renders without clipped labels or overflowing fields at the minimum inspector width, and at 1x and 2x UI scale.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (lines about side panels), `src/ui/nuklear/compose/compose_inspector.cpp`, `src/ui/nuklear/chrome/chrome.h`, `src/ui/nuklear/render/theme.h`.
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

- **Status:** done (2026-09-30; existing wheel, toolbar scrollbar drag, and middle-button autoscroll verified with 32 BGM lanes)
- **Outcome:** scrolling the BGM pane sideways never moves playable columns, and both panes keep the same vertical scroll and zoom.
- **Read first:** `docs/chart-editor-plan/editing/layout.md` (BGM pane paragraph), `src/sys/compose/views/compose_view.h` (`bgm_horizontal_scroll_pixels`), `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/ui/nuklear/compose/compose_state.h`.
- **Files owned:** `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/ui/nuklear/compose/compose_canvas.cpp`, `src/sys/compose/views/compose_view.cpp` (only if the clamp is wrong), `tests/ui/nuklear_compose_state_tests.cpp`.
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

- **Status:** done (2026-09-30; explicit idempotent Restart/Continue, source offsets and waveform aligned to each restart; core and real-window verification)
- **Outcome:** the BMSON slice pane exposes separate, idempotent Restart and Continue actions for the selected marker, checked against the document revision and disabled without selection; source offsets and waveform cuts follow the last Restart.
- **Read first:** `docs/chart-editor-plan/formats-persistence.md` (BMSON slicing), `src/core/bmson_sound_channel_editor.cpp`, `src/ui/nuklear/compose/compose_inspector.cpp` (`render_staged_bmson_regions`).
- **Files owned:** `src/ui/nuklear/compose/compose_inspector.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`, `src/core/bmson_sound_channel_editor.cpp`, `include/wormedit/core/bmson_sound_channel_editor.h`, `src/core/sound_slice.cpp`, `src/audio/channel_waveform.cpp`, `tests/CMakeLists.txt`, `tests/ui/nuklear_compose_state_tests.cpp`, `tests/core/bmson_slice_reset_tests.cpp`, `tests/formats/bmson_tests.cpp`, `tests/core/audio_audition_resolver_tests.cpp`, `tests/core/audio_object_audition_tests.cpp`, `tests/core/audio_aligned_waveform_tests.cpp` (scope extensions authorized by the user on 2026-09-30).
- **Steps:**
  1. Audit the existing BMSON continuation/restart command. Decision resolved by the user: Reset means force Restart; Continue is a separate explicit action, and repeating either is a no-op.
  2. Add the two buttons and route them through an idempotent setter sharing validation with the existing toggle; derive source offsets and project waveform cuts relative to the latest Restart.
  3. Test Restart/Continue, repeated no-ops, missing selection, stale revisions, shared pulses, export/undo, source bounds, and waveform alignment.
- **Invariants:** BMSON 1.0 pulse and continuation rules stay normative.
- **Reject/failure behavior:** a stale revision or missing selection changes nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^(core|frontend-state)\.'`.
- **Out of scope:** fade, gain, pan, or any BMSON field beyond 1.0.

## T-13 Show the full virtual BMS `#000`–`#999` canvas in Compose

- **Status:** done (2026-09-30; existing virtual tail audited with the OCT/FP fixture; user-authorized boundary correction and regression)
- **Outcome:** in a BMS-family chart, Compose's measure rail lists measures up to `#999`, empty measures render as virtual rows, and the first edit in one materializes only that measure.
- **Read first:** `include/wormedit/core/virtual_canvas.h`, `tests/core/virtual_canvas_tests.cpp`, `docs/chart-editor-plan/editing/layout.md` (measure-number column paragraph), `src/core/editor_scene.cpp`.
- **Files owned:** `src/core/editor_scene.cpp`, `src/ui/nuklear/compose/compose_canvas.cpp`, `src/sys/compose/views/compose_view.cpp`, `tests/core/virtual_canvas_tests.cpp`.
- **Steps:**
  1. Audit: open a BMS fixture (`tests/fixtures/bms/*.bms`) in the app, scroll past the last authored measure, and record whether virtual rows appear. If they do, mark this card done with the evidence and STOP.
  2. If not, make `editor_scene.cpp` emit virtual measure bands using the existing virtual-canvas helper; do not allocate measure buckets.
  3. Add one core test for a chart with 3 authored measures asserting the scene exposes bands through `#999` with zero new buckets.
  4. Add one test that the first placement in a virtual measure materializes exactly one chunk.
- **Invariants:** imported measure length, meter, branch, and bar-line data stay authoritative. Undo restores the virtual state. No empty source statements are emitted on export.
- **Reject/failure behavior:** a placement past row capacity is rejected, and the chart is byte-identical afterward (see the existing overflow test).
- **Verify:** `ctest --test-dir build --output-on-failure -R 'core\..*virtual'`; smoke screenshot with a BMS fixture scrolled to the tail.
- **Out of scope:** non-BMS virtual tails, performance tuning.

## T-14 Define the song overview docking and sync-control scope

- **Status:** done (2026-09-30; user chose a status-bar minimap and keeping existing sync controls)
- **Outcome:** the overview implementation is split into the status-bar minimap (T-15) and an exact source-frame playhead (T-16).
- **Read first:** `docs/chart-editor-plan/playback-audio.md`, `src/ui/nuklear/compose/compose_panel.cpp`, `src/sys/compose/views/compose_waveform_overlays.h`.
- **Files owned:** `docs/chart-editor-plan/tasks/compose-features.md`, `memory/chart-editor-status/resume.md`.
- **Steps:**
  1. Record the user decision: dock as a minimap in the status bar; keep the existing synchronization controls.
  2. Split the remaining work into T-15 and T-16; do not treat session timeline frames as source frames.
- **Invariants:** existing synchronization actions and sample-frame precision remain unchanged.
- **Reject/failure behavior:** ambiguous source-frame positions are not presented as exact positions.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** adding synchronization controls.

## T-15 Move the existing song overview into a horizontal status-bar minimap

- **Status:** done (2026-09-30; real audio at 1280/800, click/drag navigation, offset preview/cancel/commit/undo, and unchanged-document dispatcher checks)
- **Outcome:** Compose exposes its existing waveform and viewport window as an interactive horizontal minimap in the status bar, preserving navigation and beat-zero dragging.
- **Read first:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/ui/nuklear/compose/compose_state.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, `src/ui/nuklear/compose/compose_panel.h`, `src/ui/nuklear/compose/compose_input_panel.cpp`, `src/ui/nuklear/compose/compose_panel_internal.h`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/compose/compose_state.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:**
  1. Audit the current overview data, layout, and capture lifecycle before changing its host.
  2. Move the overview to the status bar with horizontal drawing and input coordinates; use one input owner and preserve existing offset actions.
  3. Test actual coordinate and sample-frame helpers, cancellation, and action bounds; inspect the real window with audio at normal and narrow widths.
- **Invariants:** source peaks, chart timing, transport buttons, offset nudges, and rail fit remain unchanged; clicks reuse the revision-checked overview navigation action.
- **Reject/failure behavior:** missing audio, stale source identity, focus loss, modal capture, and resize cancel or reject input without changing the document.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `clang-tidy -p build` on changed C++ files; real-window smoke and PNG inspection.
- **Out of scope:** exact source-frame playhead (T-16), new synchronization controls, changing audio decoding or timing.

## T-16 Add an exact source-frame playhead to the status-bar minimap

- **Status:** done (2026-09-30; exact runtime voice position, source identity guards, restart/clip regressions and real-window playback/pause/Stop verified)
- **Outcome:** the minimap shows the position in its displayed audio source when runtime source-frame identity is known, and omits the marker when it is ambiguous.
- **Read first:** `src/sys/application_frame_projection.cpp`, `src/sys/project/workspace_frame_view.h`, `src/sys/project/workspace_frame_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_panel.cpp`.
- **Files owned:** `include/wormedit/audio/transport.h`, `src/audio/transport.cpp`, `include/wormedit/audio/preview_session.h`, `src/audio/preview_session.cpp`, `include/wormedit/audio/audition_session.h`, `src/audio/audition_session.cpp`, `src/sys/application_frame_projection.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_state.h`, `src/ui/nuklear/compose/compose_state.cpp`, `src/ui/nuklear/compose/compose_panel.cpp`, `tests/core/audio_transport_tests.cpp`, `tests/core/audio_preview_session_tests.cpp`, `tests/core/audio_audition_session_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp` (assigned after the source-position audit).
- **Steps:**
  1. Audit runtime source-frame positions for music preview, chart preview, and audition; STOP and report if a source position cannot be identified.
  2. Publish a bounded source identity and frame snapshot and draw its fraction in the minimap.
  3. Test identity mismatches, BMSON restarts, clip origins, and unavailable source positions.
- **Invariants:** timeline frames are not mislabeled as source frames; no timing or audio data is changed.
- **Reject/failure behavior:** a missing or ambiguous source position hides the marker; paused sessions currently have no live source position and hide it too.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure` (runtime source identity, clip origins, restarted voices, ambiguity and frontend fraction guards); `clang-tidy -p build` on changed C++ files; real-window audio playback screenshots showing movement and hiding on pause/Stop.
- **Out of scope:** transport behavior changes, new synchronization controls.
