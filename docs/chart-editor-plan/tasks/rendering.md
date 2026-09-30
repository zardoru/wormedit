# Task cards: GPU workload of the Compose field

[Back to the task-card index](../tasks.md).

Findings (2026-09-30, code reading; numbers are upper bounds from capacities):

- Every frame, `nk_sdl_render` (the stock SDL3 demo backend in
  `build/_deps/nuklear-src/demo/sdl3_renderer/nuklear_sdl3_renderer.h`, called
  from `src/ui/nuklear/frontend.cpp` with `NK_ANTI_ALIASING_ON`) converts the whole
  command list with anti-aliasing for every shape and line, allocates fresh
  vertex and index buffers (`nk_buffer_init`), and issues one
  `SDL_RenderGeometryRaw` per Nuklear draw command; each clip change breaks the
  batch. Vertices carry float RGBA color (32 bytes per vertex).
- `draw_compose_waveform` (`src/ui/nuklear/compose_canvas.cpp`) draws each of up
  to 4,096 rows (`WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY`) as **four** AA
  `nk_stroke_line` calls; sample mode draws two AA lines per sample. An AA line
  costs about 8 vertices, so the waveform alone can reach ~130,000 vertices,
  which also explains the 16-bit index assertion (T-244).
- Grid lines, lane backgrounds, and note tiles are axis-aligned rectangles and
  lines where AA only adds fringe triangles.
- `SDL_SetRenderLogicalPresentation(..., SDL_LOGICAL_PRESENTATION_STRETCH)` renders
  the UI at logical size into an intermediate target and scales it to the
  backbuffer every frame: an extra full-screen pass, and a soft image on
  fractional high-DPI scales.
- During playback nothing in the field changes except the scroll offset and the
  playhead, yet everything is regenerated and re-uploaded each frame.

The architecture reference already expects this: the field should be
"batched, culled, with waveform meshes" independent of the panel toolkit
([ui-backend-boundary](../../references/ui-backend-boundary.md)).

**Decision (user, 2026-09-30):** the load is unreasonable for this program, so
the Compose field bypasses Nuklear. The field (lanes, grid, notes, holds, BGM,
timing tiles, waveform, overlays) and later the status-bar minimap and player
preview are drawn by wormedit-owned components that emit SDL geometry directly.
Nuklear only reserves the field's rectangle and keeps drawing chrome (toolbar,
panels, menus, forms, popups). The field's primitives never pass through
Nuklear's command buffer or `nk_convert`. Z-order is kept by a single custom
command (`nk_push_custom`) at the field's position in Nuklear's stream: when the
project-owned backend (T-247) reaches that command, it calls the field renderer,
which submits its own batches, then Nuklear drawing continues, so menus and
popups still appear above the field.

Order: T-246 (measure) → T-247 (own backend) → T-248 (field renderer without AA)
→ T-249 (waveform meshes) → T-250 (tile cache) → T-251 (native-resolution
rendering). Apply the stale-plan check (rule 9) first.

## T-246 Count GPU work per frame

- **Status:** done
- **Outcome:** the frame-timing overlay and `WORMEDIT_FRAME_TIMING_REPORT` also show, per frame, vertices, indices, draw calls, clip-rect changes, and texture switches, and split "Render/present" into convert, submit, and present (VSync wait).
- **Read first:** `src/sys/frame_timing.h/.cpp`, `src/ui/nuklear/frontend.cpp` (the `nk_sdl_render` call and `SDL_RenderPresent` in `src/sys/window.cpp`), the SDL3 demo backend named above.
- **Files owned:** `src/sys/frame_timing.h/.cpp`, `src/ui/nuklear/frontend.cpp`, `src/sys/window.cpp` (timer call only), `tests/sys/frame_timing_tests.cpp`, `src/ui/frontend.h` (user-approved scalar counter and 24-phase view contract extension).
- **Steps:** 1. Counters filled where the draw list is converted and submitted. 2. Sub-phase timers. 3. Record the T-211 fixture at normal zoom and at maximum zoom (sample mode) and put both tables in the report.
- **Invariants:** no allocation when the overlay is hidden.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; the two tables.
- **Out of scope:** fixes.

Isolated Xvfb, SDL x11, dummy audio, 1280×980 window, idle Compose, hidden overlay. Each table is the last complete steady 240-frame report after the view settles. Vertices/indices are unique converted-buffer counts; draw calls are submissions; clip/texture switches compare consecutive submissions, excluding the initial binding and clip restoration. Duration children exclude each other; Render/present remains inclusive. GPU work describes the Nuklear SDL submission stream.

### Original T-211: normal zoom

BPM 120; 192 px/measure; 500 frames/pixel at 48 kHz.

| Metric | Average | p99 |
| --- | ---: | ---: |
| Vertices | 33013.000 | 33013.000 |
| Indices | 84384.000 | 84384.000 |
| Draw calls | 44.000 | 44.000 |
| Clip-rect changes | 10.000 | 10.000 |
| Texture switches | 34.000 | 34.000 |
| Convert (ms) | 5.354 | 5.817 |
| Submit (ms) | 5.256 | 5.571 |
| Present (ms) | 11.471 | 12.716 |
| Render/present (ms) | 23.139 | 24.353 |

### Original T-211: maximum zoom

BPM 120; 4096 px/measure; 23.4375 frames/pixel at 48 kHz.

| Metric | Average | p99 |
| --- | ---: | ---: |
| Vertices | 31608.000 | 31608.000 |
| Indices | 70581.000 | 70581.000 |
| Draw calls | 44.000 | 44.000 |
| Clip-rect changes | 10.000 | 10.000 |
| Texture switches | 34.000 | 34.000 |
| Convert (ms) | 4.762 | 5.084 |
| Submit (ms) | 4.945 | 5.080 |
| Present (ms) | 10.234 | 11.673 |
| Render/present (ms) | 21.000 | 22.499 |

### Temporary derivative: maximum zoom, actual PCM sample mode

BPM 6000; 4096 px/measure; 0.46875 frames/pixel at 48 kHz.

| Metric | Average | p99 |
| --- | ---: | ---: |
| Vertices | 13516.000 | 13516.000 |
| Indices | 31227.000 | 31227.000 |
| Draw calls | 44.000 | 44.000 |
| Clip-rect changes | 10.000 | 10.000 |
| Texture switches | 34.000 | 34.000 |
| Convert (ms) | 2.024 | 2.109 |
| Submit (ms) | 2.117 | 2.314 |
| Present (ms) | 8.921 | 10.001 |
| Render/present (ms) | 14.121 | 15.191 |

The original compact JSON decodes identically to `/tmp/wormedit-t211/dense.wormedit`. Its 120 BPM/48 kHz audio never reaches PCM sample mode at the 4096 px/measure cap (23.4375 frames/pixel versus the <0.9 entry threshold). The third fixture preserves its notes and tone, changes only BPM to 6000, and resolves the same asset through absolute `/tmp/wormedit-t211/tone.wav`. At the exact cap, the isolated GDB probe entered `draw_compose_waveform_samples`: 341 rows, first sample-point flag true, stereo PCM values 0.0285475. The probe disables itself after one hit; the steady table follows it. PNGs show normal/max zoom and the PCM curve; `maximum-overlay.png` shows real scalar counters.

Verification: parent coordinated integrated build and full suite 913/913; focused independent GPU tests 4/4 (21 assertions); affected production/tests syntax checks pass; clang-tidy reports no new project complexity violation. Logs and captures reside in `/tmp/wormedit-t246/`.

## T-247 Own the Nuklear SDL3 render backend

- **Status:** blocked-on T-246
- **Outcome:** a project-owned copy of the SDL3 backend replaces the demo header: vertex and index buffers persist across frames (cleared, not reallocated), colors are 8-bit where SDL accepts them, indices are 32-bit (with T-244), and consecutive commands with the same texture and clip are merged into one `SDL_RenderGeometryRaw` call.
- **Read first:** the demo backend, `include/wormedit/nuklear_config.h`, `src/ui/nuklear/frontend.cpp`.
- **Files owned:** new `src/ui/nuklear/sdl_backend.cpp/.h` (with the upstream license notice), `src/ui/nuklear/frontend.cpp`, `include/wormedit/nuklear_config.h`, `CMakeLists.txt`, new `tests/ui/nuklear_sdl_backend_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Copy the backend verbatim, build, confirm identical screenshots. 2. Persistent buffers. 3. Command merging with a test on a synthetic command list. 4. Keep `NK_ANTI_ALIASING_ON` for now.
- **Invariants:** pixel-identical output at step 1; visually identical after merging.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; T-246 draw-call count drops; smoke PNGs of every tab.
- **Out of scope:** changing what the field draws.

## T-248 Draw the Compose field with a wormedit renderer outside Nuklear

- **Status:** blocked-on T-247
- **Outcome:** everything inside the Compose field rectangle is drawn by a new frontend-private field renderer that builds per-layer vertex batches directly from the scalar Compose view and submits them with `SDL_RenderGeometryRaw` (no Nuklear shapes, no `nk_convert`, no anti-aliasing for axis-aligned primitives), culled to the viewport; Nuklear keeps only the surrounding chrome.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (all `draw_compose_*` functions), `src/ui/nuklear/arrow_glyph.h`, `src/ui/nuklear/sdl_backend.h` (T-247), `docs/references/ui-backend-boundary.md`, the file-header decision above.
- **Files owned:** new `src/ui/field/field_renderer.cpp/.h` (SDL-only, no Nuklear includes), new `src/ui/field/field_glyphs.cpp/.h` (arrow polygons and a small glyph-atlas text path reusing the Nuklear font atlas texture), `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/sdl_backend.cpp`, `CMakeLists.txt`, the architecture test (allow `src/ui/field` to include SDL but not Nuklear), new `tests/ui/field_renderer_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Renderer API on plain types: `begin(viewport_px, scale)`, `rect(layer, rect, rgba8)`, `hline/vline(layer, …)`, `polygon(layer, points, rgba8, aa_fringe)`, `text(layer, pos, utf8, rgba8)`, `submit(SDL_Renderer*)`. Batches are persistent vectors, reused each frame; rectangles are pixel-snapped.
  2. Reserve the field in Nuklear with one empty widget and one `nk_push_custom` command whose callback (run by the T-247 backend in stream order) calls `submit`, with the field clip rect applied.
  3. Move each `draw_compose_*` layer to the renderer one at a time (grid → lanes → notes/holds → BGM → timing tiles → overlays), comparing smoke screenshots after each move; delete the Nuklear version once matched.
  4. Only arrow polygons get a 1-pixel AA fringe; text uses the same font atlas so labels look identical.
  5. Tests: vertex/batch counts for a known scene; culling; layer order; no allocation after warm-up.
- **Invariants:** hit-testing stays in the existing headless/controller code (it never depended on Nuklear shapes); colors, draw order, and z-order relative to menus and popups unchanged; the architecture boundary test passes.
- **Reject/failure behavior:** none at runtime; if a layer cannot be matched visually, STOP and report with screenshots.
- **Verify:** standard build/test; T-246 counts on the T-211 fixture (target: field vertices down by at least 5× and draw calls to a handful); smoke PNGs of `compose` at normal and maximum zoom.
- **Out of scope:** waveform meshes (T-249), tile caching (T-250), replacing chrome widgets (T-252).

## T-249 Draw the waveform as meshes

- **Status:** blocked-on T-248
- **Outcome:** the envelope waveform is four triangle strips (RMS and peak, left and right) with two vertices per row and no AA; sample mode is two line strips (one per channel) drawn as thin quads, one quad per segment; worst case drops from ~130,000 to under 35,000 vertices.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (`draw_compose_waveform`, `draw_compose_waveform_samples`, `draw_compose_channel_waveform`), `src/ui/field/field_renderer.h`.
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/ui/field/field_renderer.cpp/.h`, `tests/ui/field_renderer_tests.cpp`.
- **Steps:** 1. Strip builder: for rows sorted by y, emit (center, y) and (center ± value × half_width, y). 2. Same for the BGM channel waveform. 3. Test vertex counts for 4,096 rows.
- **Invariants:** same colors and alpha as today; matches T-240's stable style rules.
- **Reject/failure behavior:** rows with non-finite values are skipped.
- **Verify:** standard build/test; T-246 counts; smoke PNGs at normal and maximum zoom.
- **Out of scope:** caching (T-250).

## T-250 Cache static field layers in tiles

- **Status:** blocked-on T-249, T-213
- **Outcome:** lane backgrounds, grid, notes, BGM, timing tiles, and the waveform are rendered into cached render-target tiles (for example 512 logical px tall) keyed by document revision, chart, zoom, layout width, theme, and tile index; while only the scroll changes (playback following), a frame composites a few textured quads plus the dynamic overlays (playhead, cursor, hover, selection, ghosts, preview), so both CPU scene work and GPU vertex work stay near constant regardless of chart density.
- **Read first:** `src/ui/field/field_renderer.h`, `src/sys/compose_view.cpp` (T-213's scroll-only reuse), `src/ui/nuklear/player_preview_texture.cpp` (existing texture LRU pattern), SDL3 `SDL_SetRenderTarget`.
- **Files owned:** new `src/ui/field/field_tile_cache.cpp/.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/field/field_renderer.cpp`, `CMakeLists.txt`, new `tests/ui/field_tile_cache_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Split the field into static layers (tile-cached) and dynamic overlays (drawn every frame).
  2. Tile LRU with a byte budget (for example 64 MiB); render missing tiles at most two per frame, falling back to direct drawing for tiles not ready.
  3. Invalidate on revision, zoom, width, theme, DPI, lane layout, or visibility-toggle changes.
  4. Tests for key invalidation and budget eviction (logic only, no GPU).
- **Invariants:** screenshots identical to direct drawing; device-loss/resize recreates tiles.
- **Reject/failure behavior:** if render targets are unsupported, keep direct drawing.
- **Verify:** standard build/test; T-246 numbers during Ctrl+Space before/after; smoke PNGs.
- **Out of scope:** Timing tab.

## T-251 Render at native resolution instead of a stretched logical target

- **Status:** blocked-on T-246
- **Outcome:** the UI renders directly into the backbuffer at its pixel size with `SDL_SetRenderScale(pixel / logical)` and logical presentation disabled, removing the per-frame intermediate target and scaling pass and making text and lines sharp on fractional scales; input keeps using window points.
- **Read first:** `src/ui/nuklear/frontend.cpp` (the `SDL_SetRenderLogicalPresentation` block and its comment), `memory/chart-editor-status/implemented/editing-frontend.md` (the resize/high-DPI paragraph), the architecture test that guards the resize mapping.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/sys/window.cpp`, the architecture test source that checks this mapping.
- **Steps:** 1. Replace the logical presentation with render scale plus a clip-rect conversion in the backend (clip rects must be scaled too). 2. Update the architecture test to guard the new mapping. 3. Check fractional scales 1.25, 1.5, and 2.0 with smoke captures (no unpainted bands, which was the original reason for the logical presentation).
- **Invariants:** hit-testing, clipping, and full-window clear stay aligned (the original bug must not return).
- **Reject/failure behavior:** if the renderer rejects the scale, fall back to the current logical presentation.
- **Verify:** standard build/test; smoke PNGs at each scale; T-246 present time.
- **Out of scope:** font atlas resolution changes.

## T-252 Decide whether chrome widgets leave Nuklear too

- **Status:** blocked-on T-250
- **Outcome:** with the field, waveform, minimap, and player preview on wormedit renderers, a measured decision on the rest of the UI: keep Nuklear for chrome, or replace it with wormedit components (buttons, lists, fields, menus) on the same renderer.
- **Read first:** `docs/references/ui-backend-boundary.md`, T-246 reports before and after T-250, `docs/chart-editor-plan/tasks/panels-menus.md`.
- **Files owned:** this card, `docs/references/ui-backend-boundary.md`.
- **Steps:**
  1. Measure what Nuklear still costs (T-246 counters restricted to chrome) during Ctrl+Space and while idle.
  2. STOP and present the numbers with two options: (a) keep Nuklear for chrome, themed by T-225; (b) own components, starting with the dock lists (T-229–T-231), which are the heaviest chrome. Record the decision.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** the recorded decision.
- **Out of scope:** implementation.
