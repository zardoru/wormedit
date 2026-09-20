# Plan slice: workspace layout and frontend boundary

[Back to the editing-plan index](../editing.md).

## Editor layout

Use an osu!-style tabbed song workspace. Tabs are focused views over the same `song_document_t`, selected difficulty, transport clock, edit cursor, assets, and command history:

- **Setup** for project files/assets, format/dialect profiles, difficulty management, and output grouping;
- **Metadata** for shared song fields, per-difficulty fields, and clearly labeled target-format groups;
- **Timing** for waveform synchronization, BPM, stops, delays, warps, SV/scroll/speed, kiai, meter, and anchors;
- **Compose** for playable notes, BGM lanes, labels, semantic layers, selection, and lane layouts;
- **Compose** also docks the sample, BGM, and BGA resource lists for drag/place/assign workflows;
- **Storyboard** for OSB/OSBX canvas, hierarchy, keyframes, and storyboard preview;
- **Audio/Slices** for source assets, waveform inspection, BMSON continuation-derived slices, and audition;
- **Compare/Validate** for synchronized side-by-side difficulties, yellow differing rows, conversion preview, and diagnostics.

Keep the application chrome deliberately compact. A conventional **File / Edit / View / Chart / Timing / Tools / Help** menu exposes infrequent commands and their shortcuts without turning the main surface into a wall of buttons. Compose keeps frequent placement state in one content-sized toolbar, including the armed resource index and auto-increment toggle, rather than burying sequential-entry controls in a modal. The visible tab buttons are content-sized, use tight vertical padding, stay grouped instead of expanding across the window, and show the active tab through a persistent selected treatment rather than relying on a separate title. Center forms and non-chart panels inside a configurable maximum content width with side margins; wide tables may scroll inside that region instead of making every control span the monitor. The bottom status bar is one compact line by default, expanding only for an actionable diagnostic or progress detail. Hide the complete recovery heading/body/action region when there are no recovery candidates or recovery diagnostics.

All reusable Nuklear text-entry wrappers follow native editing expectations. Double-click selects the entire field value, a subsequent drag may refine the selection, and typing replaces the selected text; single-click caret placement, keyboard selection, clipboard shortcuts, IME composition, password/non-text exceptions, and focus behavior remain intact. Implement and test this once in the frontend adapter rather than patching individual Metadata or timing fields.

The difficulty selector is independent of the active tab, so changing from Compose to Storyboard does not accidentally change the chart being previewed. Switching tabs retains playback position, loop, zoom where coordinates are compatible, and selection relevant to that view. A storyboard may be a separate included OSBX/OSB document while still sharing song assets and transport with chart editing.

Provide a modeless **Timing Settings** panel reachable from both Timing and Compose. It controls metronome enable/volume/accent/subdivision/count-in, note/BGM/BGA visibility during timing work, independent visibility for every timing lane including BPM, and timing lock state. Hiding BPM or another lane changes rendering and hit testing only; the hidden events remain effective in timing and preview. **Show/hide notes** likewise never mutates chart data. **Lock timing** prevents creation, deletion, drag, paste, quantization, or generator replacement of BPM, scroll/SV/speed, stop, delay, warp, kiai, offset, meter, and timing-anchor data until unlocked. Commands validate the lock in the controller/model layer so hidden lanes, keyboard shortcuts, Lua results, and clipboard paste cannot bypass it.

Each tab has a separate controller and focus/shortcut context. Text entry, modal dialogs, and Lua editors consume keys before chart lane bindings; hidden tabs cannot react to input. Mutations still enter one document-level command history so a timing edit observed from several tabs is one atomic undo step. Dirty/save state belongs to the document or explicitly separate included file, not to ephemeral tab widgets.

Compose is the primary hybrid BMS/StepMania authoring surface rather than a note-only screen. Timing remains a focused tab for waveform fitting and dense timing work, but both tabs render the same chart-field projection, exact grid, edit-head row, measure-number column, and toggleable timing lanes. Compose keeps a fixed timing rail on the left in the style of BMSE/iBMSC, a compact mode-width playable field in the center, ordered BGM lanes immediately to its right, and an extendable/collapsible sound/resource sidebar at the outer right. Showing timing lanes in Compose is a view setting, not a second copy of their data; hiding them changes neither evaluation nor export.

Every timing-aligned pane uses one vertical coordinate system and one shared viewport transform. Up/Down cursor navigation changes the exact edit row and scrolls the entire document projection—timing rail, measure numbers, playable field, BGM/BGA lanes, and waveform—together so the row remains under one horizontal edit-head line drawn across the full authoring canvas. The line must not stop at the playable lanes or become misaligned across independently horizontally scrolled panes. Mouse/trackpad scrolling, page navigation, seeking, and playback update that same transform, with an explicit policy for whether the edit head is fixed at a preferred screen row or allowed to approach an edge before recentering.

```text
compact menu + content-sized active tabs
transport / song position / snap controls
waveform overview and detailed waveform (optional)
left rails
  measure number column
  measure/meter lane
  BPM lane       value markers and segment readout
  scroll/SV lane step markers and effective-value curve
  speed lane     transition value/duration/mode
  stop lane      duration markers
  delay lane     duration markers
  warp lane      interval markers
  kiai/anchor/section lanes
center field      bounded-width mode/profile-specific playable columns
right tracks
  BGM lanes       autoplay clips, ordered and horizontally scrollable
  BGA lane        timed background/layer/video events
outer-right collapsible sound/resource sidebar
compact selection/validation status
```

Each timing lane supports selecting, inserting, dragging, deleting, and numeric/property editing for its event type. At every cursor position, show authored BPM/scroll/speed values and the resulting effective scroll rate. The left rail remains visible while the BGM pane scrolls horizontally, shares vertical zoom/scroll with the note field and waveform, honors independent lane show/hide toggles in both Compose and Timing, and clearly marks locked events. The dedicated Timing tab must draw the same measure/beat/subdivision grid as Compose; it is not an ungridded list of markers.

The measure-number column renders the source-facing measure identity independently of whether a measure contains an object. In BMS-family profiles the initial canvas exposes the complete addressable `#000` through `#999` range immediately. Empty rows/measures are virtual view/index entries and do not allocate 1,000 populated note buckets or emit empty source statements; the first edit in a virtual measure materializes only the required stable measure/chunk and any necessary intervening geometry through one command. Imported explicit measure-length, meter, branch, or bar-line data remains authoritative. Non-BMS profiles derive a bounded virtual tail from their format/profile policy and can extend it without requiring one-by-one measure insertion. Navigation, selection, grid drawing, export, undo, and stress tests must distinguish virtual canvas extent from authored/materialized chart data.

Grid lines are a first-class shared chart-field feature. Draw a clear hierarchy of measure boundaries, authored beat/group boundaries, and exact subdivision rows across the timing rail, note field, BGM/BGA panes, and timing-aligned waveform. Emphasize rows belonging to the active snap without implying that off-snap events are invalid. Variable measure lengths and retained meters/groupings determine the musical hierarchy from exact chart coordinates; renderer pixels never determine row placement. Zoom changes the visible distance between rows but never chart timing. At low density, omit or coalesce subdivision lines before they become indistinguishable; at high zoom, reveal the active exact subdivision down to sample-alignment views. Keep measure/beat lines visually distinguishable from snap lines, keep the edit cursor legible over all of them, and expose independent visibility/intensity controls without changing snapping or data.

The note field derives its column layout from layout segments rather than from a global maximum channel count. A chart may use any supported number of columns, and the layout may change at a measure boundary or other explicitly supported position. Each segment defines ordered semantic lane IDs, roles, and presentation. The renderer must handle transitions without reinterpreting notes in surrounding measures. Importers map source columns to stable semantic lane IDs; exporters map them back and validate formats that require one fixed key count for the entire chart.

Playable columns use a profile-defined compact width and never stretch merely to consume the remaining window. The default neutral/BMS presentation draws tap and hold endpoints as square tiles with enough interior room for a bounded sound/resource index plus small note-kind, keysound-layer, warning, and semantic-layer icons. Grid lines remain behind the tile instead of serving as the note glyph. At low zoom, labels/icons reduce by an explicit priority order before the square loses hit-test size; hover or the inspector exposes any elided identity. Arrow/skin profiles may replace the square artwork, but the neutral square view is the default for BMSE/iBMSC-style authoring.

Scroll direction belongs to the active mode/lane-presentation profile and remains user-overridable without changing chart data. StepMania-style 4K defaults to upscroll; beatmania/BMS key modes such as beat-7K default to downscroll; every built-in profile declares its default rather than inferring it only from key count. Upscroll and downscroll mirror the viewport transform, edit-head orientation, hold body, waveform/timing/BGM alignment, wheel/navigation signs, and edge autoscroll consistently. Store a project/view override separately from the imported gameplay mode so changing editor preference does not rewrite format metadata.

Support StepMania-style directional arrows as a note presentation for 4K, 5K, and 6K modes, alongside square/bar note styles. A mode profile maps semantic lanes to arrow artwork and orientation; column count alone must not guess the mapping when multiple layouts use the same count. Holds use the corresponding arrow head plus a readable body and tail. Imported format/mode metadata selects a profile when unambiguous, and the user can choose or customize the profile otherwise. Note presentation is separate from lane identity so changing the skin does not remap chart data.

The note field has two switchable viewing modes:

- **Skin mode** renders the target game/mode skin, including directional arrows, scratches, hold bodies, mines, and other supported note kinds.
- **Note-snap mode** renders a neutral note glyph colored by the note's quantization class using a StepMania-editor-style palette, including red for wholes, blue for halves, yellow for quarters, and defined colors for finer straight and tuplet divisions.

Snap color derives from the note's exact rational position within the musical grid, not from a rounded screen coordinate. The palette and classification table must be explicit and testable, especially for tuplets and positions representable only on a 960-division grid. In note-snap mode, semantic note-layer color moves to an outline, stripe, or badge so both quantization and authored musical-source meaning remain visible. The view toggle changes presentation only and must not rewrite notes or layers.

For double-style charts, let the user switch between a continuous field and a split-stage presentation. A split-stage layout assigns columns to semantic stage groups and draws a configurable gutter/divider between them while retaining one shared timing axis. Built-in profiles should cover BMS double-play layouts such as 10K and 14K, including player-side scratch placement, and osu!mania layouts split at a user-selected boundary. Stage grouping is an editor presentation/property of the lane profile and must not change note timing or source column identity.

Ship fixture-backed BMS-family lane profiles from the command memo rather than deriving a layout from the largest channel used. The initial set includes BMS 5-key/10-key, BME 7-key/14-key, standard PMS 9-button and 5-button, BME-style PMS 9-button/18-button, and named foot-pedal/free-zone, double-scratch/FPP, octave, and Project2DX variants. Each profile declares ordered semantic lanes, raw visible channels, corresponding invisible/LN/mine channels where defined, scratch/free-zone/pedal roles, player stage, display order, default input binding, and target compatibility. Historical MGQ data may be retained through a custom BMS map but is not a Keyboardmania editor profile.

In particular, BMS keys use `11-15`/`21-25` with scratches `16`/`26`; BME adds keys 6–7 at `18-19`/`28-29`; standard PMS 9-button uses `11-15,22-25`, while BME-style PMS uses `11-15,18,19,16,17`. Preserve `17`/`27` as free-zone or foot-pedal roles according to the selected profile. The broader visible `11-1Z`/`21-2Z`, invisible `31-3Z`/`41-4Z`, LN `51-5Z`/`61-6Z`, and mine `D1-D9`/`E1-E9` families are retained as raw channel identities; a corresponding semantic lane is assigned only when the dialect/profile defines one. See the [command-memo lane-map audit](../../references/bms-command-memo.md#playable-channel-maps).

Invisible notes, long notes, and mines are first-class editable note kinds. Invisible notes keep lane/sample/timing identity, render with a distinct editor-only treatment, can be selected and auditioned, and do not become ordinary playable taps unless an explicit transform requests it. Holds retain separate head/tail positions and source notation such as BMS LN channels or `#LNOBJ`. Mines retain target-specific damage/parameter provenance where available. Skin and snap-color views must overlay these kinds without hiding their semantic layer color, selection, hover, or validation state.

Profile detection returns evidence and confidence from filename extension, `#PLAYER`, used channel families, and explicit project settings. Ambiguous combinations open with preserved raw channels and a proposed mapping for user review. Selecting or changing a profile produces a preview of unmapped, duplicated, reserved, or role-changing channels and is one undoable operation. Custom channel maps remain first-class, so unusual charts and future layouts do not require corrupting data to fit a built-in profile.

Selection, keyboard input, copy/paste, lane remapping, and hold drawing must work across the split. Timing, BPM, SV, waveform, and BGM tracks span the entire chart rather than being duplicated per stage. When a source format has an unambiguous double-play mode, import selects its split profile automatically; otherwise the user can choose the partition without rewriting chart data.

Provide a dedicated BGM lane area in the style of BMS and BMSON editors. This is a first-class BMS editing surface for autoplay keysounds, including the events represented by repeated BMS `#xxx01` lines; BMSON `x: 0`/`null` events use the same surface. Autoplay events stay separate from playable columns and can coexist at the same position. The area must support selection, move/copy/delete, sound assignment, and clear display of the source or derived slice name.

For BMS, preserve each repeated `#xxx01` statement as a distinct ordered BGM lane for that measure. Preserve every line's subdivision and its `00` empty slots; do not compact or merge them into a single time-keyed stream. When placing an event into an occupied BGM cell, the editor advances to the next BGM lane and emits the needed `00` cells to retain its row. Export should keep stable source-lane assignments where possible and allocate new repeated lines deterministically.

The BGM pane shares vertical/time scrolling and zoom with the note field so rows stay aligned, but it has its own horizontal scroll position and visible-lane range. Authors can inspect a large BGM stack without moving playable columns off screen. Collapsed or offscreen BGM lanes remain present and exportable.

The Compose sound sidebar can be collapsed to an icon/tab strip or extended to a bounded resizable panel without changing playable-field width. It contains the armed sound/slice, sound index and layered-hit icons, audition/stop, volume and preview controls, searchable Samples/BGM/BGA views, usage/missing state, and create/assign/replace mode. Dragging or clicking a resource targets the focused playable or BGM/BGA lane through the same typed command path; closing the sidebar never clears the armed resource implicitly.

Clicking a BGM event previews its assigned clip immediately, independent of chart transport. Preview playback must honor slice start/end frames and audition all layers when the event references a mixed BMSON sound. The preview interaction should also work while the waveform/slice editor is open and should stop or replace the previous click-preview predictably.
