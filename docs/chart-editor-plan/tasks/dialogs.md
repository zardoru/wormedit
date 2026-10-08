# Dialogs: one shared dialog surface, Unsaved changes, Tap timing and tooltip occlusion (2026-10-05 feedback)

[Back to the task card index](../tasks.md).

User feedback from 2026-10-05 on two screenshots. The Tap timing window is badly
designed. Modal padding is uneven left to right with gaps at the bottom. Unsaved
changes shows an unrelated import diagnostic and empty space below it. Nuklear's
draggable title bar is useless on a dialog nobody moves, and in fact cannot move.

The causes, found while planning:

- Every dialog calls `nk_begin` with `NK_WINDOW_TITLE` and a hard-coded height, so leftover height becomes bottom gaps.
- Windows without `NK_WINDOW_NO_SCROLLBAR` reserve a scrollbar gutter on the right, so the padding looks uneven.
- `render_close_confirmation` prints `close_feedback` as `status_text`, which is the last routine notification, not anything about closing.
- The stray "\udio is unavailable…" box over Tap timing is the status-bar overview tooltip (`render_status_minimap`). Its hover check is a raw rectangle test, so it fires through windows on top.

## Dialog design rules (apply to every card here)

- **Surface:** `chrome::paint_surface` at elevation 2 (T-436): rounded corners of 8 px, a soft shadow that falls over the workspace, and a 1 px `border_hover` stroke. No Nuklear title bar, and no dragging.
- **Padding:** 16 px on every side, and 8 px between rows. No scrollbar gutter unless the body really scrolls.
- **Header:** a 15 px title row (bold or `text_strong` color) with a ghost close icon at the right for non-modal tools. Confirmation dialogs have no close icon, and Escape cancels them.
- **Body:** say a thing once, and put secondary explanations in tooltips. No wall of help sentences.
- **Footer:** buttons are right-aligned and the same height (28 px). The primary action is rightmost, uses the accent fill, and Enter triggers it. A destructive action sits at the far left, separated by space. Escape triggers Cancel.
- **Size:** the height fits the content, measured on the previous frame (two-pass). The dialog is centered in the window. Modal dialogs dim the window behind them with `rgba(0,0,0,0.35)`.

## T-407 Add a shared `chrome::Dialog` component

- **Status:** ready
- **Mockup:** [unsaved-changes.svg](../mockups/unsaved-changes.svg) (reference: match its structure and hierarchy)
- **Outcome:** `chrome::Dialog` draws a dialog that follows the rules above. Unit tests show equal left and right insets, a height that fits its content within 1 px after the second frame, Escape mapped to cancel and Enter mapped to the primary action, and no dragging.
- **Read first:** `src/ui/nuklear/chrome.h` (`menu_panel`, `menu_surface_begin`), `src/ui/nuklear/context_menu.h` (a lifecycle component with the same shape), `src/ui/nuklear/frontend.cpp` (`begin_project_modal`).
- **Files owned:** new `src/ui/nuklear/dialog.h`, new `src/ui/nuklear/dialog.cpp`, `src/ui/nuklear/chrome.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, new `tests/ui/dialog_tests.cpp`, this card.
- **Steps:**
  1. Give the API `begin(context, id, title, {modal, closable, width})` → bool, `footer_begin()`, `button(label, role)` with roles primary, secondary and destructive, and `end()` → a result enum.
  2. Begin with `NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER`, transparent window fills and a painted surface. Set `window.padding` to (16, 16) and `spacing` to (8, 8) for the duration, and restore them afterwards.
  3. Remember each dialog's measured content height from the last frame (`layout->at_y` minus the start), and use it for this frame's bounds.
  4. Draw the backdrop for modal dialogs on the background window's canvas.
  5. Write the tests listed in **Outcome**.
- **Invariants:** input never passes through a modal dialog to the workspace.
- **Reject/failure behavior:** a window too small for the dialog clamps it to the window with a 16 px margin, and the body scrolls.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'dialog'`; `clang-tidy -p build src/ui/nuklear/dialog.cpp`.
- **Out of scope:** migrating existing dialogs (T-408, T-409, T-411).

## T-408 Rebuild Unsaved changes on the shared dialog

- **Status:** blocked-on T-407
- **Mockup:** [unsaved-changes.svg](../mockups/unsaved-changes.svg) (reference: match its structure and hierarchy)
- **Outcome:** the dialog shows the title "Unsaved changes", then "Save your changes before quitting WormEdit?" (or "…closing this project?"), then a footer with `[Discard changes]` at the left and `[Cancel] [Save]` at the right, where Save is primary. It shows no import or other notification text and leaves no empty space.
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_close_confirmation`), `src/sys/main.cpp` (`close_confirmation_view`, `close_feedback`), `src/ui/frontend.h` (`WormUiCloseConfirmationView`).
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/sys/main.cpp`, `src/ui/frontend.h` (`status_text` of the close view only), event-harness tests, this card.
- **Steps:**
  1. Stop passing `close_feedback` into the view. Show a status line only for a failed save from this dialog, such as "Save failed: <reason>".
  2. Render with `chrome::Dialog` and modal mode on. Escape cancels and Enter saves.
  3. Add event-harness cases for each button, Escape, Enter and the save-failure line.
- **Invariants:** the decision, project and revision stamps are unchanged. Save without a native path still opens the file dialog.
- **Reject/failure behavior:** a stale revision closes the dialog without acting, as now.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`; an Xvfb capture of the dialog, checked against the rules.
- **Out of scope:** other dialogs.

## T-409 Redesign the Tap timing tool

- **Status:** blocked-on T-407
- **Mockup:** [tap-timing.svg](../mockups/tap-timing.svg) (reference: match its structure and hierarchy)
- **Outcome:** Tap timing matches the layout below in an Xvfb capture. It is 420 px wide and fits its content. Each tap pulses the ring, and the result is a large BPM readout instead of five help sentences.
- **Read first:** `src/ui/nuklear/tap_timing_panel.cpp`, `src/ui/frontend.h` (`WormUiTapTimingView`), `src/ui/nuklear/frontend.cpp` (font atlas setup near `nk_font_atlas_add_from_file`).
- **Files owned:** `src/ui/nuklear/tap_timing_panel.cpp`, `src/ui/nuklear/frontend.cpp` (second font size only), `src/ui/nuklear/sdl_backend.cpp` (if the atlas lives there), `src/ui/nuklear/theme.h`, `src/ui/frontend.h` (tap view fields such as last-tap time), `src/sys/tap_timing_runtime.cpp` (projecting those fields), event-harness tests, this card.
- **Layout:**

  ```text
  Tap tempo                                         ✕
  ┌────────────┐
  │  ◜ ◝      │   174.02 BPM            ← 28 px font
  │  ◟ ◞      │   ±1.8 ms · stable      ← muted, green when stable
  └────────────┘   beat 0 at 12.3 ms
  ▮▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯  9 / 16 taps          ← thin progress bar
  Tap T or Space on the beat              ← one hint line; it changes by state
  Round to   ( Raw | 1 | 0.5 )            ← segmented control, not a combo
  ───────────────────────────────────────────────────
  [Reset]                                  [Apply 174 BPM]
  ```

- **Steps:**
  1. Add a second font atlas entry at 28 px for numeric readouts, exposed as `theme::display_font`.
  2. Build the ring at 96 px beside the readout. Flash the segment of the last tap for 120 ms, and keep the beat follow-light while playing.
  3. Until four taps exist, the readout shows "— BPM" and the hint says "Play the song and tap T or Space on the beat". When the song is stopped, the hint says "Start playback to tap". With no music, it says "Add a music track to tap".
  4. Replace the BPM combo with a three-way `chrome::Segmented` (T-436).
  5. Make the footer a ghost Reset button on the left and a primary "Apply <n> BPM" on the right. When Apply is disabled, its tooltip gives the reason, such as unstable taps or locked timing. Move "First tap assigns the downbeat…" into that tooltip.
  6. Use `chrome::Dialog` with non-modal and closable set.
- **Invariants:** the tap capture keys, estimate math and Apply action payload are unchanged.
- **Reject/failure behavior:** Apply against a stale revision shows the reason in the hint line and keeps the taps.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'tap'`; `ctest --test-dir build -L event-harness`; `scripts/smoke-compose-virtual-x.sh` with the tool open, compared with the layout.
- **Out of scope:** the estimate algorithm.

## T-410 Hide tooltips whose widget is covered by another window

- **Status:** ready
- **Outcome:** with Tap timing (or any window) over the status-bar overview, hovering the dialog never shows "Audio is unavailable for this chart overview." An event-harness case checks a covered widget and an uncovered one.
- **Read first:** `src/ui/nuklear/tooltip.h`, `src/ui/nuklear/tooltip.cpp`, `src/ui/nuklear/frontend.cpp` (`render_status_minimap`).
- **Files owned:** `src/ui/nuklear/tooltip.cpp`, `src/ui/nuklear/tooltip.h`, `src/ui/nuklear/frontend.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, this card.
- **Steps:**
  1. Add `tooltip::hovered(context, bounds)`. It requires `nk_input_is_mouse_hovering_rect` and that the current window is the hovered one, using `nk_window_is_hovered` together with Nuklear's top-window check.
  2. Replace the raw rectangle checks that feed tooltips in `frontend.cpp`, then grep the other Nuklear units for the same pattern.
  3. Add the event-harness cases.
- **Invariants:** tooltips on uncovered widgets keep their timing and placement.
- **Reject/failure behavior:** not applicable.
- **Verify:** `cmake --build build`; `ctest --test-dir build -L event-harness`.
- **Out of scope:** tooltip styling.

## T-411 Move the remaining dialogs onto the shared dialog

- **Status:** blocked-on T-407
- **Mockup:** [unsaved-changes.svg](../mockups/unsaved-changes.svg) (reference: match its structure and hierarchy)
- **Outcome:** Timing Settings, External player preferences, Review sound import, Group selected sounds, Assign BMS WAV slots, the new project wizard and the project modals use `chrome::Dialog`. `grep -n NK_WINDOW_TITLE src/ui/nuklear` lists only windows the user may drag: Options, Notifications and Player preview.
- **Read first:** `src/ui/nuklear/dialog.h`, each listed source (`grep -rn 'nk_begin(' src/ui/nuklear`).
- **Files owned:** `src/ui/nuklear/timing_settings_panel.cpp`, `src/ui/nuklear/external_player_panel.cpp`, `src/ui/nuklear/sound_import_dialog.cpp`, `src/ui/nuklear/sound_slot_batch.cpp`, `src/ui/nuklear/frontend.cpp` (`begin_project_modal` and wizard), affected tests, this card.
- **Steps:**
  1. Migrate one dialog per commit, then rebuild and run the event harness after each.
  2. For each one, delete help text that repeats a label, and move explanations into tooltips.
  3. Take one Xvfb capture of each dialog, and record any rule it still breaks on this card.
- **Invariants:** dialog actions and stamps are unchanged.
- **Reject/failure behavior:** not applicable.
- **Verify:** `cmake --build build`; `env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build --output-on-failure`; Xvfb captures.
- **Out of scope:** the Options window layout.

## T-436 Regularize the UI components: one set of sizes, radii, gaps and state colors

- **Status:** done (2026-10-05; headless suite and event harness pass, 1829/1829; SDL window not built or captured in the cloud: Zar to run the gallery capture locally)
- **Mockup:** [components.svg](../mockups/components.svg) (reference: match its structure and hierarchy)
- **Outcome:** `src/ui/nuklear/ui_metrics.h` defines the tokens, and `chrome` provides each component once, with all its states, built on those tokens. A hidden "UI gallery" window (opened with `--ui-gallery` or a debug command) draws every component in every state, and an Xvfb capture of it matches the mockup's structure. The components later cards use (Dialog, Checkbox, Segmented, ChipGrid, SelectionBar) come only from here.
- **Why:** the 2026-10-05 designs add a dialog, segmented controls, chips, drawn checkboxes and a selection bar. Built separately in five cards, they would drift in height, radius and state color, the way today's `mode_button`, `IconButton`, `DrawnIconButton` and ad-hoc `button_style` calls already have.
- **Tokens:**
  - **Control heights:** 24 (compact rows and chips), 28 (default) and 32 (primary actions).
  - **Radii:** 4 (controls), 12 (chips), 6 (bars and inspector groups) and 8 (menus and dialogs).
  - **Gaps:** 4, 8 and 16.
  - **Font sizes:** 12, 13, 15 and a 28–30 display size.
  - **Elevation (shadows):** three levels. 0 is flat (inspector groups and bars). 1 is menus, context menus and tooltips: an 8-step soft shadow offset 4 px down (today's `menu_panel`). 2 is dialogs and floating tools: 12 steps offset 6 px down, slightly darker. One function, `chrome::paint_surface(context, bounds, elevation)`, draws every level.
  - **List groups:** a group's header actions end flush with the row-highlight edge (8 px inset), and the header title starts at the row-highlight edge too (user, 2026-10-05: its left margin matches the buttons' right margin; row text keeps its 8 px inset inside the highlight). Headers and rows share both edges, as in [bookmarks-sections.svg](../mockups/bookmarks-sections.svg).
  - **State colors:** the lime `accent` means a mode or toggle is on (tools, W/F/C). Blue `selected` means the chosen value, the selection, or the primary action. Coral is destructive or a conflict. Disabled is the surface fill with faint text, and its tooltip gives the reason.
- **Read first:** `src/ui/nuklear/chrome.h` (`IconButton`, `DrawnIconButton`, `mode_button`, `button_style`, `menu_panel`), `src/ui/nuklear/theme.h`, `src/ui/nuklear/context_menu.h`, [mockups](../mockups/README.md).
- **Files owned:** new `src/ui/nuklear/ui_metrics.h`, `src/ui/nuklear/chrome.h`, new `src/ui/nuklear/components.cpp`, new `src/ui/nuklear/ui_gallery.cpp`, new `src/ui/nuklear/ui_gallery.h`, `src/ui/nuklear/frontend.cpp` (gallery entry point only), `CMakeLists.txt`, `tests/CMakeLists.txt`, new `tests/ui/components_tests.cpp`, this card.
- **Steps:**
  1. Write `ui_metrics.h` with the tokens.
  2. Add `Button` (primary, secondary, ghost and destructive), `Checkbox` (on, off and mixed), `Segmented`, `ChipGrid` (fixed columns, so rows align), `SelectionBar` and `Surface` (menu and dialog), each with hover, active, focus and disabled states, keyboard support where it applies (Segmented takes Left/Right), and tooltips.
  3. Re-express `IconButton`, `DrawnIconButton` and `mode_button` on the tokens without changing their call sites.
  4. Generalize `chrome::menu_panel` into `paint_surface`:
     - make the window's own background transparent, as `menu_surface_begin` does;
     - widen the scissor around the bounds by the elevation's reach, clamped to the **render target** rather than the parent window (backends reject scissors outside the target);
     - draw the shadow steps, then the rounded fill and stroke, and restore the window's clip.

     Top-level windows (dialogs, Tap timing, toasts) can then cast shadows over the workspace, because their command buffers render above lower windows. Popups keep their parent clamp only where Nuklear requires it. Record on this card whether nested popups need it.
  5. Build the gallery window and an Xvfb capture of it, including a level-2 dialog over the workspace and a level-1 menu near the window edge.
  6. Write unit tests for the hit areas, keyboard handling and disabled non-activation, plus a headless draw-command test: a level-2 surface emits shadow fills outside its window bounds, and every scissor stays inside the render target.
- **Invariants:** existing screens keep their behavior. Visual changes to existing widgets are limited to the token values, and smoke captures are compared before and after. Shadows never take input, because hit-testing stays on the window bounds.
- **Reject/failure behavior:** a disabled component never emits an action.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'components'`; `ctest --test-dir build -L event-harness`; gallery capture; `clang-tidy -p build src/ui/nuklear/components.cpp`.
- **Out of scope:** migrating whole screens. That is done in T-407, T-411, T-415 and T-419, and in later cleanup.
- **Result:**
  - `ui_metrics.h` holds the tokens, `chrome::paint_surface` (levels 0-2) replaces `menu_panel`'s body (`menu_panel` now calls it at level 1 with its parent window as the clamp), and `chrome::surface_window_begin/end` give top-level windows a transparent fill plus the shadow.
  - `components.cpp` implements `Button`, `Checkbox`, `Segmented`, `ChipGrid` and `SelectionBar` once, from the tokens. Looks come from the pure functions `button_look` and `chip_look`. Segmented keeps keyboard focus in a caller-owned flag (a click takes it, a click elsewhere drops it) and Left/Right step the selection without wrapping.
  - `IconButton`, `DrawnIconButton` and `mode_button` already shared `button_style`; its literals now read the tokens, so call sites and colors are unchanged.
  - Gallery: `ui_gallery.cpp`, drawn instead of the workspace when `WORMEDIT_UI_GALLERY=1` (environment variable, not a `--ui-gallery` flag, to avoid touching `main.cpp`). `scripts/smoke-compose-virtual-x.sh <out.png> gallery` captures it.
  - Tests: `tests/ui/components_tests.cpp` (11 cases, `ctest -R components`): tokens, shadow clip, a level-2 surface emits at least 12 shadow fills outside its window and every scissor stays inside the render target (our scissors; Nuklear's own `nk_null_rect` reset is skipped), button, checkbox, segmented, chip and bar hit areas, disabled components never act, and the whole gallery draws inside the target. `clang-tidy` on `components.cpp` and `ui_gallery.cpp` is clean.
  - Supporting-file extensions (within the card's outcome): `tooltip::viewport()` getter (the render-target clamp for top-level surfaces), `scripts/smoke-compose-virtual-x.sh` gallery mode, and the `ui_gallery.cpp` entry in `CMakeLists.txt`.
  - Disabled/destructive/faint colors are constants in `ui_metrics.h` (`destructive`, `destructive_text`, `text_faint`) because the theme files do not carry them yet; T-270 should move them.
  - Nested popups: they keep the parent-window clamp (`menu_panel`); Nuklear clips popup content to the parent, so widening to the render target would not show the shadow anyway. Not verified visually.
- **Not done / unverified:** no Xvfb gallery capture and no before/after smoke comparison (this container has no Vulkan ICD or `xdotool`, and the SDL app is not built here). Buttons, chips and checkboxes have no keyboard focus because Nuklear has no widget focus; only Segmented takes keys. The chip row uses a fixed 4 px gap and the group that wraps it in the gallery may need its height tuned after the first capture.
