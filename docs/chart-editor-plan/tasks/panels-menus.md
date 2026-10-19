# Task cards: design system, menus, and dockable list panels

[Back to the task-card index](../tasks.md).

Findings (2026-09-30):

- `chrome::apply_theme` in `src/ui/nuklear/chrome.h` only styles window, text,
  button, and tab. Every other Nuklear style group (`menu_button`,
  `contextual_button`, `combo`, `selectable`, `property`, `edit`,
  `scrollv/scrollh`, `checkbox`, `option`, `window.popup_*`,
  `window.menu_*`, tooltips) keeps Nuklear's grey defaults, which is why the
  Project/View menus look like another program.
- The menus sit at the right because `render_title_bar` in
  `src/ui/nuklear/frontend.cpp` pushes a dynamic title column first, then the
  two menus, then the window buttons.
- The Project menu mixes file commands with workspace-tab navigation, and menu
  items show no shortcuts even though the command registry knows them.
- iBMSC and BMSE keep their WAV, BMP, and measure-length listboxes as their own
  side panels, one row per slot (`01`–`ZZ`), including empty slots. Wormedit
  squeezes resources into a section of the Compose inspector.

Apply the stale-plan check (rule 9) first.

## T-225 Theme every Nuklear widget from the design tokens

- **Status:** done (2026-10-09: combo popup surfaces, token row geometry and gallery state coverage complete)
- **Done so far:** (1) Font audit: one font, AdwaitaMono 15 px, loaded in `frontend.cpp` and set on the context after `apply_theme`; no code pushes another font in the app (the debug atlas is test-only), so nothing needed fixing. (2) `apply_theme` now calls `apply_field_theme` (edit, property, slider, progress: surface, border, radius 4, text and selection colors; sizes and padding untouched) and `apply_scrollbar_theme` (flat tracks, rounded thumbs); `chrome::menu_separator` draws in the `separator` role. Theme: `theme.h` and `theme.cpp` add `separator`, `ui-dark.json` sets it to `$neutral.800`, and `status` moves from `$neutral.900` to `$neutral.400`. (3) `ui_gallery.cpp` adds a "Stock widgets" section (combo, selectable, property, edit, slider, checkbox, option) with static values and the same row helpers; the existing Lime-accent note stays above it. (4) `tests/ui/components_tests.cpp` adds a full-size (1280x720) gallery draw test beside the existing one.
- **Remaining:** none. The shared combo begin/end pair now paints the popup role and restores the parent window style, including custom combo callers. Menubar and context-menu rows use `metrics::menu_item_height`; separators and row spacing use gap tokens.
- **Captures:** before `/home/zardoru/.claude/jobs/4e7ef06b/tmp/t225/gallery-before-x-gallery.png` and `menu-before-menu.png`; after `gallery-after4-gallery.png (the combo goes through chrome::safe_combo, as the dropdown-guard test requires)` and `menu-after-menu.png`. The smoke script's `gallery` mode captured black (it grabs before the first frame), so the captures use the isolated runner with `WORMEDIT_UI_GALLERY=1` and a 2 s wait. Layouts match before and after (the gallery gained the Stock widgets section below the existing rows).
- **Second pass captures (2026-10-09):** gallery `/home/zardoru/.claude/jobs/4e7ef06b/tmp/t225/after-gallery-gallery.png` (now with Tooltip, Tree header and the menu separator); open StepMania combo `after-app-combo.png`; Preview menu with its separator `after-preview-preview-menu.png`.
- **Merged with T-436 (user decision 2026-10-07):** this card is now the second half of [T-436](dialogs.md#t-436-regularize-the-ui-components-one-set-of-sizes-radii-gaps-and-state-colors). T-436 built the tokens and wormedit's own components; this card applies the same tokens to the stock Nuklear widgets that are still drawn with near-default styling. It keeps its ID because T-226, T-228 and T-257 wait on it.
- **Mockup:** [components.svg](../mockups/components.svg) (reference: match its structure and hierarchy)
- **Outcome:** menus, context menus, combos, selectables, properties, edit fields, scrollbars, Nuklear checkboxes and options, popups, tooltips and tree headers take their heights, radii, gaps, font and state colors from `ui_metrics.h` and the `theme::ui` roles, matching the T-436 components beside them. Every widget uses one font object. The existing `WORMEDIT_UI_GALLERY=1` window shows each of these widgets in normal, hover, active and disabled states next to the T-436 components.
- **Read first:** `src/ui/nuklear/ui_metrics.h`, `src/ui/nuklear/chrome.h` (`apply_theme`, `button_style`), `src/ui/nuklear/theme.cpp` (roles), `src/ui/nuklear/ui_gallery.cpp`, `src/ui/nuklear/frontend.cpp` (font setup: grep `nk_font`, `font_atlas`, `nk_style_set_font`).
- **Files owned:** `src/ui/nuklear/chrome.h`, `src/ui/nuklear/ui_metrics.h`, `src/ui/nuklear/theme.cpp/.h`, `src/ui/nuklear/ui_gallery.cpp`, `src/ui/nuklear/frontend.cpp` (font setup and menu row geometry), `tests/ui/components_tests.cpp`, `assets/themes/ui-dark.json` if roles are added, this card. Also touched: `src/ui/nuklear/theme.h` and `assets/themes/ui-dark.json` (added `separator` role), `src/ui/nuklear/tooltip` not touched. Second pass (approved 2026-10-09): `src/ui/nuklear/components.cpp` (the `popup` role in `paint_surface` only); `popup` added to `theme.h`, `theme.cpp` and `ui-dark.json`.
- **Design reference:** the T-436 tokens and state colors. Colors come from `assets/themes/ui-dark.json` (neutral, blue, coral and lime scales, 50–950, referenced by roles as `$scale.shade`), originally expanded from <https://octet.design/colors/palette/dark-header-color-palette-1731325758/>. T-436 left `destructive`, `destructive_text` and `text_faint` as constants in `ui_metrics.h`; move them into the theme roles here if this card needs them.
- **Steps:**
  1. Report which fonts are loaded and whether any code pushes a different font. If menus render with another font, find the cause and fix it (one font object for the whole context).
  2. Extend `apply_theme` one style group at a time, using the `ui_metrics.h` heights (24/28/32), radii (4 controls, 8 menus and popups) and gaps. Keep the existing roles and add at most `popup` and `separator` roles, both defined in `ui-dark.json` as `$scale.shade` references.
  3. Add a "Stock widgets" section to the existing gallery in `ui_gallery.cpp`. Do not add a second gallery window or environment variable.
  4. Extend the gallery draw test in `tests/ui/components_tests.cpp` so the new section draws inside the render target.
- **Invariants:** layout sizes and hit areas are unchanged except menu padding and where a token value changes a height. Smoke captures are compared before and after.
- **Reject/failure behavior:** none.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R 'components'`; `ctest --test-dir build -L event-harness`; `scripts/smoke-compose-virtual-x.sh /tmp/gallery.png gallery` and a `compose` capture with a menu open, compared against the T-436 buttons; `clang-tidy -p build src/ui/nuklear/ui_gallery.cpp`.
- **Out of scope:** new colors, a light theme, and migrating screens onto the T-436 components (T-407, T-411, T-415, T-419).

- **Supporting ownership extension (2026-10-09):** `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_bga_panel.cpp`, `src/ui/nuklear/compose_sounds_panel.cpp`, `src/ui/nuklear/metadata_audio_section.cpp`, `src/ui/nuklear/metadata_format_fields.cpp`, `src/ui/nuklear/sound_import_dialog.cpp`, `tests/ui/nuklear_popup_combo_tests.cpp`, `tests/CMakeLists.txt` (link the production surface/theme into combo and notification tests), `src/ui/nuklear/context_menu.h/.cpp` (share token row heights with the menubar), and `memory/chart-editor-status/resume.md` are approved for paired combo-end style restoration, lifecycle coverage and completion status, within the existing theming outcome. `docs/chart-editor-plan/tasks/options-charts.md` is approved only to remove T-257's completed T-225 prerequisite.
- **Completion (2026-10-09):** Dropdown bodies now use the same rounded popup surface and border as menus, with balanced style restoration on selection/dismissal and a guarded inline fallback inside other popups. Stock combo rows use compact/default height tokens; menubar and context-menu rows share the compact token. Built-in popup, separator and status colors match existing palette roles when theme files omit them. The existing gallery shows normal, hover-color, active-color and disabled stock controls, including progress, scrollbars, tree headers and menu rows; its body scrolls on smaller windows. Font object and application actions remain unchanged.
- **Final verification:** normal build; **3,111/3,111 full tests**; **23/23 focused component/combo/context-menu tests**; **12/12 dedicated component tests**; **324/324 dedicated event-harness cases**; documentation validation; affected-source clang-tidy with no new/increased project complexity findings. Native isolated Xvfb gallery, open combo and Compose/File menu captures inspected against the earlier T-225 gallery/menu references. Final artifacts: `/tmp/t225-gallery-final.png`, `/tmp/t225-gallery-final-combo.png`, `/tmp/t225-compose-final-menu.png`; logs `/tmp/t225-{build-verified,focused-final,all-tests,tidy,tidy-final,context-tidy}.log`. The required smoke script also ran; the delayed isolated runner produces the stable full-size gallery capture. Implementation and completion documentation are uncommitted.

## T-226 Move the menus to a conventional left menu bar

- **Status:** done (2026-10-09; shared six-menu bar and registry routing verified)
- **Outcome:** the title bar reads, left to right: app icon, menus (File, Edit, View, Chart, Playback, Help), then the project title (with `*` when dirty), then the window buttons; tab navigation leaves the menu (tabs are already visible).
- **Read first:** `src/ui/nuklear/frontend.cpp` (`render_title_bar`), `src/ui/nuklear/menu_bar.cpp`, `src/core/command_registry.cpp` (command ids).
- **Files owned:** `src/ui/nuklear/frontend.cpp`, new `src/ui/nuklear/menu_bar.cpp/.h`, `src/ui/frontend.h`, `CMakeLists.txt`, `src/ui/workspace_menu.h`, `src/sys/macos_menu.mm`, `src/sys/window.cpp`, `tests/ui/popup_menu_event_harness_tests.cpp`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/ui/notification_hit_region_event_harness_tests.cpp`, `tests/CMakeLists.txt`.
- **Ownership extension (coordinator, 2026-10-09):** shared menu definitions and existing macOS consumer must follow the same menu groups; the SDL title-bar drag exclusion shares the new menu widths so Playback/Help remain clickable; production event-harness coverage verifies routing, dismissal, and preserved actions. Verification exposed retained area-menu capture across frontend reinitialization; the existing area reset now runs at frontend startup alongside the global-menu reset. Stale audit: the left bar exists, but Chart/Playback/Help and several registry actions are missing.
- **Steps:**
  1. Move menu rendering into `menu_bar.cpp`; layout: static columns for each menu label, then a dynamic title column, then window buttons.
  2. Menu contents: **File** New project, Open…, Save, Save as…, Close, Quit. **Edit** Undo, Redo, Delete, Select all, Invert selection, Clear selection. **View** player preview, Reset editor view, panels (T-228), Notifications (T-220 when present). **Chart** New chart (T-52), timing lock, insert BPM/SV (T-50). **Playback** Play/Pause, Play from start, Stop, external player (T-214 when present). **Help** About (version and git hash already in the view).
  3. Items that dispatch registry commands go through the same invocation path as shortcuts.
- **Result (2026-10-09):** the existing left bar was completed rather than duplicated: Lucide app icon, File/Edit/View/Chart/Playback/Help, project title/dirty marker and window controls. Editing, chart and transport rows dispatch registry IDs through the keyboard invocation path; existing tool windows, clipboard, BGM/same-sound and external-player actions remain reachable. About uses the generated version and current build hash. The existing macOS adapter consumes the same groups; native macOS runtime verification remains unavailable on Linux. Real SDL verification found the old four-menu drag exclusion intercepting Playback/Help; the window hit-test now derives its boundary from shared title widths. Startup also resets retained area-menu capture, fixing the combined frontend-harness shortcut leak.
- **Verification (2026-10-09):** normal build ended `[100%] Built target wormedit`; full `ctest --test-dir build --output-on-failure` ended `100% tests passed out of 3116` (225.02 s). The combined frontend binary ended `All tests passed (5143 assertions in 362 test cases)`. Affected-source clang-tidy has no new/increased complexity violations; pre-existing warnings remain. Standard isolated Compose smoke and all six native popup/title-switch/About captures inspected, plus 800×560 minimum-window capture. Logs `/tmp/t226-build-window-final.log`, `/tmp/t226-all-final.log`, `/tmp/t226-events-verified.log`, `/tmp/t226-tidy-*.log`, `/tmp/t226-window-tidy.log`; screenshots `/tmp/t226-verified-*.png`, `/tmp/t226-switch-*.png`. Brain scratchpad/status synchronized. Uncommitted.
- **Invariants:** every action reachable today stays reachable.
- **Reject/failure behavior:** disabled commands render disabled, as in the registry.
- **Verify:** standard build/test; smoke PNG with the File menu open.
- **Out of scope:** a native OS menu bar.

## T-227 Show shortcuts, checks, and disabled state in menu items

- **Status:** done (2026-10-09; live shortcut, checked and disabled rows verified)
- **Outcome:** a `chrome::menu_item(label, shortcut_text, checked, enabled)` helper draws label left, shortcut right in muted text, an optional check mark, and the disabled style; all menus use it; shortcut text comes from the registry's current chords (so remapping in T-59 updates menus).
- **Read first:** `src/ui/nuklear/chrome.h`, `include/wormedit/core/command_registry.h` (command state, chords), `src/ui/nuklear/menu_bar.cpp`.
- **Files owned:** `src/ui/nuklear/chrome.h`, `src/ui/nuklear/menu_bar.cpp`, `src/ui/frontend.h`, `src/sys/main.cpp` (project command state/chord text and share menu dispatch), new `src/sys/menu_command_projection.cpp/.h`, `tests/ui/menu_bar_event_harness_tests.cpp`, `tests/CMakeLists.txt`, `CMakeLists.txt`.
- **Ownership extension (coordinator, 2026-10-09):** a testable projection unit copies bounded registry state and current shortcuts; build wiring and production event tests verify remaps, disabled items, and checked states. Stale audit: macOS has shortcuts/checks, but in-window rows currently only draw labels.
- **Steps:** 1. Project `{id, label, chord_text, enabled, checked}` for menu commands into a bounded view array. 2. Helper + use everywhere.
- **Result (2026-10-09):** a bounded, pointer-free command view carries current registry labels, shortcuts, enabled and checked state. One themed menu-row helper reserves a check gutter, right-aligns muted shortcut text and dims all row content when disabled. Unbound commands leave the shortcut column empty; remaps update on the next frame. Primary resolves to Ctrl or ⌘; Command is included in the native font range. Local panel/tool checks follow their actual visibility. Existing native menu duplicate-key retirement now resolves Primary per platform, and in-window command selections use that shared dispatch too.
- **Verification (2026-10-09):** the T-226 build, 3,116/3,116 full tests, 362/362 combined frontend cases, native visual checks and complexity gates also cover this card. Added production-event cases verify one invocation per click, remaps/unbinding/platform labels, disabled commands, missing projections, checked model state, all six title switches and About dismissal; existing popup and notification menu tests now assert registry dispatch. Evidence paths are recorded under T-226. Uncommitted.
- **Invariants:** platform primary modifier shown as Ctrl or ⌘ per platform.
- **Reject/failure behavior:** commands without a chord show no shortcut text.
- **Verify:** standard build/test; smoke PNG.
- **Out of scope:** submenus.

## T-228 Add a right dock that hosts several panels

- **Status:** ready (T-225 completed 2026-10-09; audit current implementation before starting)
- **Outcome:** Compose's right side becomes a dock with a narrow vertical strip of panel buttons (Inspector, Sounds, Images, Measures, Layers, Markers); each panel shows alone or stacked (up to two, split vertically), has its own scroll, and can be hidden; visibility, order, split, and dock width persist in the preferences file.
- **Read first:** `src/ui/nuklear/compose_panel.cpp` (inspector layout and width), `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_state.h`, `src/core/shortcut_profile_storage.cpp` (how preferences are saved).
- **Files owned:** new `src/ui/nuklear/dock.cpp/.h`, new `src/core/ui_layout_preferences.cpp`, new `include/wormedit/core/ui_layout_preferences.h`, new `tests/core/ui_layout_preferences_tests.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `src/ui/frontend.h`, `src/sys/main.cpp` (load/save only), `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Core: a small preferences record and atomic JSON load/save next to the shortcut profile, bounded and versioned; tests for missing/corrupt files.
  2. Dock: panel registry (id, title, icon, render function), strip on the dock's outer edge, drag handle to resize width (clamped 240–520 logical px).
  3. Initially wrap the existing inspector as the only panel (pure move of its call); later cards add panels.
- **Invariants:** the chart canvas keeps the remaining width; resize/high-DPI behavior unchanged.
- **Reject/failure behavior:** a corrupt preferences file falls back to defaults with a warning notification.
- **Verify:** standard build/test; smoke PNGs with one and two panels open.
- **Out of scope:** floating/undocked windows, left dock.

## T-229 Add the Sounds panel (iBMSC/BMSE-style WAV slot list)

- **Status:** done (merged into [T-358](compose-followups.md), 2026-10-04; historical card)
- **Completion:** Optional Sounds-dock slots mode and direct BMS ID assignment landed through
  T-358. Use the existing Sounds dock rather than the obsolete T-228 panel; do not execute the
  historical steps below.
- **Outcome:** for BMS charts the Sounds panel lists every slot `01`–`ZZ` (or base-62 range when the chart uses it) with its file name or blank, uses count, and missing state; only visible rows are built; double-click arms the slot for placement; Enter/F2 or a file drop assigns a file to the slot (creating or relinking the resource with that exact BMS identifier); a play button auditions it. For formats without slots (BMSON, SM, osu!, Quaver) the panel shows the compact table from T-206.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose_resource_presenter.cpp`, `include/wormedit/core/resource_assignment.h` (`apply_resource_creation` with BMS identifier, `apply_resource_asset_relink`), `docs/references/bms-command-memo.md`.
- **Files owned:** new `src/ui/nuklear/sounds_panel.cpp/.h`, `src/sys/compose_resource_presenter.cpp/.h`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `tests/core/compose_resource_presenter_tests.cpp`, `CMakeLists.txt`.
- **Steps:**
  1. Presenter: virtualized slot window (first visible slot, count ≤ 64) with slot label, resource id or empty, name, uses, state.
  2. Row rendering with fixed row height; keyboard Up/Down moves the highlighted slot when the panel has focus.
  3. Assign-to-slot command: creates the resource with that identifier, or relinks the existing one; one undo step.
  4. Remove the resource list from the inspector once the panel works (the inspector keeps only the armed resource and placement controls).
- **Invariants:** BMS identifiers of other slots never change.
- **Reject/failure behavior:** assigning a file to an occupied slot asks for confirmation (relink) instead of silently replacing it.
- **Verify:** standard build/test; smoke PNG with a BMS fixture that has gaps between `#WAV` slots.
- **Out of scope:** drag from the list onto the chart (later card).

## T-230 Add the Images panel (BMP/BGA slot list)

- **Status:** done (merged into [T-347 and T-352](compose-followups.md), 2026-10-03; historical card)
- **Completion:** The BGA dock follows the T-335 keysound-column design; T-347 owns its list and
  thumbnails and T-352 owns natural order and ID capacity. Optional BMP-slot authoring remains a
  separate future scope; do not execute the historical steps below.
- **Outcome:** same behavior as T-229 for `#BMP` slots and BGA resources, with thumbnails (T-49) at 32 px.
- **Read first:** `src/ui/nuklear/sounds_panel.cpp`, `src/ui/nuklear/player_preview_texture.h`, `include/wormedit/visual/image_job.h`.
- **Files owned:** new `src/ui/nuklear/images_panel.cpp/.h`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/frontend.h`, `CMakeLists.txt`.
- **Steps:** reuse the T-229 presenter with the image role; thumbnails only for visible rows.
- **Invariants:** as T-229.
- **Reject/failure behavior:** as T-229.
- **Verify:** standard build/test; smoke PNG.
- **Out of scope:** video thumbnails.

## T-231 Add the Measures panel (iBMSC measure-length list)

- **Status:** blocked-on T-228
- **Outcome:** a list of measures `#000`–`#999` (virtual BMS measures included) showing length as ratio and meter; multi-select with Shift/Ctrl; an editor row applies a length or meter to all selected measures in one command; clicking a row jumps the cursor there.
- **Read first:** `include/wormedit/core/measure_editor.h`, `include/wormedit/core/virtual_canvas.h`, `src/sys/main.cpp` (existing meter form handler), `docs/chart-editor-plan/document-model.md` (measure length paragraph).
- **Files owned:** new `src/ui/nuklear/measures_panel.cpp/.h`, `include/wormedit/core/measure_editor.h`, `src/core/measure_editor.cpp`, `tests/core/measure_editor_tests.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_view.cpp`, `src/ui/frontend.h`, `CMakeLists.txt`.
- **Steps:** 1. Core batch definition update over a bounded measure list (materializing virtual BMS measures as needed). 2. Virtualized list. 3. Editor row with decimal ratio and meter fields (T-73 suggestions when present).
- **Invariants:** exact ratios; stable measure IDs.
- **Reject/failure behavior:** any invalid measure rejects the batch.
- **Verify:** standard build/test.
- **Out of scope:** inserting/removing measures (T-70).

## T-232 Move Layers and Markers into their own panels

- **Status:** blocked-on T-228, T-207
- **Outcome:** Semantic layers and Markers/sections leave the Compose inspector and become dock panels; the Inspector panel keeps only properties of the current selection (note label, BPM value, recipe, BMSON slice state).
- **Read first:** `src/ui/nuklear/compose_inspector.cpp`, `src/ui/nuklear/compose_markers.cpp`, `src/ui/nuklear/dock.h`.
- **Files owned:** `src/ui/nuklear/compose_inspector.cpp/.h`, `src/ui/nuklear/compose_markers.cpp/.h`, new `src/ui/nuklear/layers_panel.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`, `CMakeLists.txt`.
- **Steps:** pure moves of the render functions into panel units, then register them with the dock.
- **Invariants:** identical actions and behavior.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; smoke PNGs.
- **Out of scope:** new layer features.

## T-312 Move the note palette into a tall left toolbar in Compose

- **Status:** done (2026-10-02; `render_compose_note_palette_column` in `compose_toolbar.cpp`, strip layout in `compose_panel.cpp`)
- **Implementation notes (2026-10-02, Claude):** the 36-px strip is a `Compose note palette` group at x = 0 of the Compose layout space and the editor group starts at x = 36. SDL/Nuklear side compiled only on the user's machine; no automated test.
- **Outcome:** the note-kind buttons (Tap, Hold, Mine, Invisible, Lift, Fake, Roll) form one vertical column along the left edge of the Compose pane, running the full height of the editor area, in the style of the osu! editor, Affinity, and paint programs; the horizontal palette row under the toolbar is gone, so the chart field gains that row's height.
- **Request (user, 2026-10-01):** "The note palette should [be] a sort of tall left toolbar a la osu or affinity or paint programs in general"; side confirmed: "Left by default."
- **Current behavior (2026-10-01):** `render_note_kind_palette` (`src/ui/nuklear/compose_toolbar.cpp`) draws a second toolbar row of 30-px icon buttons when `note_kind_palette_visible` is true.
- **Stale-plan check:** grep `render_note_kind_palette` in `src/ui/nuklear/compose_panel.cpp`.
- **Read first:** `src/ui/nuklear/compose_toolbar.cpp` (`render_note_kind_palette`, `render_compose_toolbar`), `src/ui/nuklear/compose_panel.cpp` (the `Compose editor` / `Compose inspector` layout space), `src/ui/nuklear/chrome.h` (`IconButton`).
- **Files owned:** `src/ui/nuklear/compose_toolbar.cpp/.h`, `src/ui/nuklear/compose_panel.cpp`.
- **Steps:**
  1. Expose the palette as `render_compose_note_palette_column(context, view, action)` and stop calling it from `render_compose_toolbar`.
  2. In the Compose body layout space, when `note_kind_palette_visible`, push a strip 36 px wide at x = 0 for the full body height and move the editor group right by that width; otherwise the editor keeps the whole width.
  3. Inside the strip, a group without scrollbars or border, one 30 × 30 `IconButton` per allowed kind, top-aligned, same icons, tooltips, order, armed highlight, and `WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND` input as today.
  4. Check that pointer input in the field still lands on the right lane, since the field's left edge moved (the canvas uses its own bounds, so expect no change).
- **Invariants:** arming behavior, Alt+1…Alt+7, and allowed kinds per chart are unchanged; charts that hide the palette lose no width.
- **Reject/failure behavior:** a window too short for every button clips the column at the bottom; it never overlaps the field.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure -R '^frontend-state\.|^documentation\.'`; `clang-tidy -p build` on the changed `.cpp` files; `scripts/smoke-compose-virtual-x.sh /tmp/out.png compose`, then open the PNG and confirm the column sits at the left edge, the old row is gone, and a click on a lane still places a note there.
- **Out of scope:** a preference to put the column on the right (follow-up; left stays the default), selection or eraser tools in the column, a left dock that hosts other panels, palette customization.

## T-290 Add a CJK-capable font and UI localization (ja, zh, ko)

- **Status:** deferred (late ambition, user decision 2026-09-30; was: ready)
- **Outcome:** the UI font covers Japanese, Chinese (simplified and traditional), and Korean glyphs, so chart titles, artists, genres, `#WAV`/`#BMP` names, and paths render correctly; UI strings go through a localization table with English, Japanese, Simplified Chinese, Traditional Chinese, and Korean catalogs, selectable in Options (program scope, T-253).
- **Note:** CJK *display* of chart data (BMS metadata and file names are often Japanese) may be wanted before UI translation; if so, split steps 1–3 into their own near-term card.
- **Read first:** `src/ui/nuklear/frontend.cpp` (font atlas setup), `src/ui/nuklear/theme.cpp`, `include/wormedit/core/preferences.h` (T-253), the command registry's localization keys (`command.<id>` in `src/core/command_registry.cpp`).
- **Files owned:** `assets/fonts/` (font files and their license), `src/ui/nuklear/frontend.cpp`, new `src/ui/nuklear/font_atlas.cpp/.h`, new `assets/locales/*.json`, new `src/core/localization.cpp`, new `include/wormedit/core/localization.h`, new `tests/core/localization_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Choose an OFL-licensed pan-CJK font (for example Noto Sans CJK or Source Han Sans subsets) and record the license; keep the Latin UI font and add CJK as a fallback.
  2. Nuklear bakes atlases ahead of time, so do not bake all CJK glyphs: build the atlas from the glyph set actually present in the current UI strings plus document text (re-bake on project open or locale change), with a size cap; missing glyphs fall back to a box and one diagnostic.
  3. Verify rendering with Japanese, Chinese, and Korean BMS/osu! metadata fixtures (smoke PNG).
  4. Localization table keyed by stable string ids (reuse the registry's `command.<id>` keys); English is the source catalog; missing translations fall back to English.
  5. Locale choice in Options; tests for lookup, fallback, and plural-free formatting with arguments.
- **Invariants:** document text is never modified by display; Latin text rendering unchanged.
- **Reject/failure behavior:** a missing or corrupt locale file falls back to English with a warning.
- **Verify:** standard build/test; smoke PNGs with each locale.
- **Out of scope:** right-to-left scripts, IME improvements.
