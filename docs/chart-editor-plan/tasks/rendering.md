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
→ T-249 (circular waveform texture) → T-250 (static field tile cache) → T-251 (native-resolution
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

- **Status:** done
- **Outcome:** a project-owned copy of the SDL3 backend replaces the demo header: vertex and index buffers persist across frames (cleared, not reallocated), colors are 8-bit where SDL accepts them, indices are 32-bit (with T-244), and consecutive commands with the same texture and clip are merged into one `SDL_RenderGeometryRaw` call.
- **Read first:** the demo backend, `include/wormedit/nuklear_config.h`, `src/ui/nuklear/frontend.cpp`.
- **Files owned:** new `src/ui/nuklear/sdl_backend.cpp/.h` (with the upstream license notice), `src/ui/nuklear/frontend.cpp`, `include/wormedit/nuklear_config.h`, `CMakeLists.txt`, new `tests/ui/nuklear_sdl_backend_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Copy the backend verbatim, build, confirm identical screenshots. 2. Persistent buffers. 3. Command merging with a test on a synthetic command list. 4. Keep `NK_ANTI_ALIASING_ON` for now.
- **Invariants:** pixel-identical output at step 1; visually identical after merging.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test; T-246 draw-call count drops; smoke PNGs of every tab.
- **Out of scope:** changing what the field draws.

Verification (2026-09-30): the verbatim upstream implementation was copied with its full MIT notice (Micha Mettke, 2017), built, and compared before buffer or batching changes. All six available panels (Setup, Metadata, Timing, Compose, Audio / Slices, Compare / Validate) had zero differing RGB pixels. The same six panels remain pixel-identical after persistent buffers and merging. Storyboard is N/A: `workspace_tab_enabled` in `src/core/workspace.cpp:770–771` unconditionally disables it; its disabled tab chrome also matches. No feature was enabled for the test. All windows used isolated Xvfb and dummy audio.

The local `SDL_RenderGeometryRaw` API requires `const SDL_FColor*` (`SDL_render.h:2499`), so float RGBA is retained; it cannot accept packed 8-bit vertex colors. AA remains on and indices remain 32-bit. Vertex/index buffers retain capacity until backend shutdown; the actual dense-frame allocator regression confirms no Nuklear allocations after warm-up. Batching preserves index order and effective clip/texture boundaries, skips empty commands, and prevents merged index counts exceeding SDL's signed limit.

Steady hidden-overlay T-211 reports (240 samples at each zoom):

| Zoom | Vertices | Indices | Draw calls before → after | Clip changes | Texture switches |
| --- | ---: | ---: | ---: | ---: | ---: |
| 192 px/measure | 33,013 | 84,384 | 44 → 42 | 10 | 34 |
| 4096 px/measure | 31,608 | 70,581 | 44 → 42 | 10 | 34 |

Application builds pass; coordinated full suite 919/919 and focused backend tests 3/3 pass. Exact staged production syntax checks pass 3/3. `clang-tidy` reports no cognitive-complexity violation in the new backend or tests; pre-existing Nuklear dependency warnings remain. Pixel comparisons, captures, allocator/test logs and steady reports are in `/tmp/wormedit-t247/`.

## T-248 Draw the Compose field with a wormedit renderer outside Nuklear

- **Status:** done
- **Outcome:** everything inside the Compose field rectangle is drawn by a new frontend-private field renderer that builds per-layer vertex batches directly from the scalar Compose view and submits them with `SDL_RenderGeometryRaw` (no Nuklear shapes, no `nk_convert`, no anti-aliasing for axis-aligned primitives), culled to the viewport; Nuklear keeps only the surrounding chrome.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (all `draw_compose_*` functions), `src/ui/nuklear/arrow_glyph.h`, `src/ui/nuklear/sdl_backend.h` (T-247), `docs/references/ui-backend-boundary.md`, the file-header decision above.
- **Files owned:** new `src/ui/field/field_renderer.cpp/.h` (SDL-only, no Nuklear includes), new `src/ui/field/field_glyphs.cpp/.h` (arrow polygons and a small glyph-atlas text path reusing the Nuklear font atlas texture), `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/sdl_backend.cpp/.h` (header extension approved by user), `src/ui/nuklear/compose_area_menu.cpp/.h` (user-approved migration of the existing field band drawing helper), `CMakeLists.txt`, `cmake/check_ui_boundary.cmake` (allow `src/ui/field` to include SDL but not Nuklear), new `tests/ui/field_renderer_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:**
  0. Keep per-frame vertex scratch in a `frame_arena_t` (T-271) or persistent vectors; never allocate per primitive.
  1. Renderer API on plain types: `begin(viewport_px, scale)`, `rect(layer, rect, rgba8)`, `hline/vline(layer, …)`, `polygon(layer, points, rgba8, aa_fringe)`, `text(layer, pos, utf8, rgba8)`, `submit(SDL_Renderer*)`. Batches are persistent vectors, reused each frame; rectangles are pixel-snapped.
  2. Reserve the field in Nuklear with one empty widget and one `nk_push_custom` command whose callback (run by the T-247 backend in stream order) calls `submit`, with the field clip rect applied.
  3. Move each `draw_compose_*` layer to the renderer one at a time (grid → lanes → notes/holds → BGM → timing tiles → overlays), comparing smoke screenshots after each move; delete the Nuklear version once matched.
  4. Only arrow polygons get a 1-pixel AA fringe; text uses the same font atlas so labels look identical.
  5. Tests: vertex/batch counts for a known scene; culling; layer order; no allocation after warm-up.
- **Invariants:** hit-testing stays in the existing headless/controller code (it never depended on Nuklear shapes); colors, draw order, and z-order relative to menus and popups unchanged; the architecture boundary test passes.
- **Reject/failure behavior:** none at runtime; if a layer cannot be matched visually, STOP and report with screenshots.
- **Verify:** standard build/test; T-246 counts on the T-211 fixture; smoke PNGs of `compose` at normal and maximum zoom. The user accepted functional closure with the measured 5–6 field draw calls and the smaller vertex reduction below; the original 5× vertex target remains a performance goal carried forward to T-249/T-250, not a result of this card.
- **Out of scope:** waveform meshes (T-249), tile caching (T-250), replacing chrome widgets (T-252).

Verification (2026-09-30): migrated grid, lane backgrounds, notes/holds, BGM, timing tiles, overlays, and the existing waveform primitives in that order, comparing isolated Xvfb/dummy screenshots after each layer. Existing projection, hit-testing, semantic colors, rounded tile silhouettes, labels and canonical arrow geometry remain. Arrow fill/outline alone retain a centered 1-pixel AA fringe; other field primitives are crisp. Actual note/timing/BGM tooltips, Space preview, retained band, autoscroll and a Shift selection rectangle were captured before/after. A context-menu attempt did not open the menu and is not counted as visual verification; the backend regression verifies legacy → field → legacy submission order.

The field API contains SDL/plain types only. Persistent vertices, 32-bit indices and adjacent layer/texture batches reuse capacity. Text samples the existing font atlas; solid vertices sample its existing white pixel, retaining insertion order while avoiding texture alternation with labels. Atlas batching changes zero RGB pixels in original and PCM sample-mode normal/maximum captures. All field drawing, including viewport/status fallback and existing per-column waveform lines, now uses one final custom marker. `nk_convert` records the marker barrier; SDL submission flushes surrounding Nuklear work and draws the field in stream order. Waveform algorithms and meshes remain deferred to T-249.

The T-211 fixture contains 50,000 notes in 1,042 measures. `dense-compact.wormedit` is parsed-identical to `dense.wormedit`; both resolve `/tmp/wormedit-t211/tone.wav`. The BPM 6,000 derivative uses the same PCM and reaches sample mode at maximum zoom. T-246 counters include field and chrome; the final field-only sizes below were read from the actual renderer in isolated GDB, without using debugger timing as performance evidence.

| Fixture / zoom | Final total vertices | Final total indices | Final total draws | Final field vertices | Final field indices | Final field draws |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| T-211 / 192 px per measure | 24,929 | 59,418 | 48 | 16,475 | 39,177 | 6 |
| T-211 / 4096 px per measure | 21,972 | 41,553 | 47 | 13,347 | 20,817 | 6 |
| PCM sample derivative / 4096 px per measure | 12,024 | 26,631 | 47 | 3,414 | 6,006 | 5 |

The handful-of-field-draws target is met. The 5× vertex target is not met: total vertices fall 33,013 → 24,929 at normal zoom and 31,608 → 21,972 at maximum zoom, respectively 1.32× and 1.44×. These total comparisons do not establish an exact original field-only ratio because T-235 changed footer chrome between runs. The user explicitly accepted closing this functional migration and continuing T-249/T-250 with the original 5× goal still pending; waveform meshes were not brought into this cut.

Coordinated full build and integrated tests pass 965/965 (47.42 seconds), including all 9 field tests. Regressions cover culling, snapped subpixel lines, UTF-8/UV clipping, known geometry counts, layer/submission order, canonical arrows in all four directions, rounded shared outlines, no allocation after warm-up, and interleaved solid/text atlas batching. Owned production/test functions pass cognitive-complexity checks; vendored Nuklear implementation warnings remain unchanged. Captures, diffs, counts, build/test/tidy logs and the pre-migration binaries are in `/tmp/wormedit-t248/`.

## T-249 Cache the envelope waveform in a circular GPU texture

- **Status:** done
- **Outcome:** RMS and peak envelopes use a bounded circular render-target texture and one or two wrapped textured quads; CPU projection processes newly exposed stable world rows during scroll following; target refill touches exposed or changed boundary scanlines. Exact PCM sample mode retains two thin-quad curves and stays below 35,000 vertices for 4,096 rows.
- **Read first:** `src/ui/nuklear/compose_canvas.cpp` (waveform drawing), `src/ui/field/field_renderer.h`, `src/sys/waveform_view_projection.cpp`, `src/sys/bmson_waveform_projection.cpp`.
- **Files owned:** `src/ui/nuklear/compose_canvas.cpp`, `src/ui/field/field_renderer.cpp/.h`, `tests/ui/field_renderer_tests.cpp`; user-approved scope extensions: `src/ui/nuklear/sdl_backend.cpp/.h`, `src/sys/waveform_view_projection.cpp`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/compose_view.h`, `tests/sys/frame_timing_tests.cpp`, `tests/CMakeLists.txt` (production projection/input wiring for the regression target).
- **Steps:**
  1. Build plain bounded world-row ring planning with generation, content fingerprint, head and wrapped ranges; test forward/reverse movement, seeks, invalidation, detail changes and warm allocation reuse.
  2. Reuse CPU envelope projections in the application cache, projecting missing contiguous stable world spans; asynchronous data arrival retries incomplete rows without discarding resolved rows.
  3. Raster new or changed envelope scanlines into the circular SDL render target, then composite one or two quads in the existing field order. Retain direct fallback and exact PCM sample quads.
  4. Verify stable-grid pixels and the original fractional phase separately; record CPU projection/refill counters, T-246 vertex counts and isolated normal/maximum/PCM captures.
- **Invariants:** semantic colors, alpha, source timing and T-240 stable style rules remain; only envelope display uses an integer world-pixel origin in Compose projection coordinates. Nonunit GPU composition adds a deterministic 1/64 logical-pixel phase to resolve nearest-filter and rasterizer ties; this is separate from the previous fractional origin difference of less than one projection pixel. PCM, other field geometry, cursor and playhead keep their existing coordinates. Target compositing may differ from direct sequential blending by one RGB level; report the measured difference rather than claiming pixel identity.
- **Reject/failure behavior:** non-finite rows are skipped; unavailable targets, failed refill, or nonunit scaling on SDL's integer-mapped software renderer use current direct geometry without displaying stale envelopes. Source/revision/zoom/size/style changes and large seeks invalidate the matching cache.
- **Verify:** standard build/test and affected-file clang-tidy; delta projection/raster and wrap regressions; T-246 counts; isolated smoke PNGs at normal and maximum zoom including actual PCM mode.
- **Out of scope:** caching other static field layers (T-250), SDL_GPU migration. A future SDL_GPU path may use a mapped transfer buffer; this does not promise directly mapped VRAM.

Decision (user, 2026-09-30): replace per-row RMS geometry with a circular GPU texture, update only exposed rows as the view follows the centered playhead, and cache CPU RMS projection work too. The original strips-only design and its no-caching restriction are superseded for envelopes. Stable world-pixel sampling is necessary for retained rows; exact sample mode stays separate. The six application/backend scope extensions above were explicitly approved in two bundles; production BMSON selection/input wiring in `tests/CMakeLists.txt` was subsequently approved for the real chart/channel regression. Canonical projection-space texels allow the retained ring to operate when Nuklear's canvas differs from the projected viewport height; only the final composition scales them.

Verification (2026-09-30): final integrated build and full suite passed 1017/1017. Focused field/projection tests passed 36/36 before the final incomplete-detail case; that added case passed in the full suite and verifies fallback → arriving PCM → resolved rows, followed by zero retries for unrelated PCM arrival. Exact chart/project/channel identities are tested against real source timing, including two BMSON channels sharing one asset. Private guard rows stay unpublished: the real `1998.sm` regression now passes the unchanged Compose validator at height 532 with 425 rows (previously 426, ending at 532.5); actual Compose, scroll and playback were captured in isolated Xvfb. Affected project functions pass clang-tidy's complexity check.

The independent canonical full-target reference and wrapped ring match with **zero RGB difference** on actual OpenGL under isolated Xvfb at the real 756/792 canvas scale and at 1.25, including origins 0, 1, 65, 66, reverse/fractional 65.3, unchanged repeats and seek 1000 (278,884 assertions). UVs remain within [0,1]. The unit-scale software direct-geometry comparison retains its ≤1 RGB rounding gate. A 1/64 logical-pixel nonunit composition phase resolves both nearest-texel ties and GPU subpixel wrap seams; software's integer-mapped nonunit path uses direct drawing. Device/target reset exercises the actual backend event/API and recreates the ring before current pixels are drawn.

Actual application counters confirm reuse at the real scale: initialization projects 794 CPU rows in one request and rasterizes 795 texture scanlines; an unchanged window projects/rasterizes zero. An instrumented follow frame projects 95 exposed rows, reuses 699, and refreshes 99 scanlines (95 exposed plus four changing viewport boundary scanlines), rather than refilling the whole ring. The source-row scan/fingerprint pass remains O(viewport height); the expensive RMS projection and raster work are bounded deltas. Two textures have a combined 32 MiB ceiling; their plain ring plans retain head/wrap/generation state for a future transfer-buffer backend.

Steady 240-frame total-work windows on the same 50,000-note / 1,042-measure fixture:

| View | Matching direct geometry vertices | Circular texture vertices | Draw calls after |
| --- | ---: | ---: | ---: |
| Normal, 192 px/measure | 24,933 | 14,813 | 48 |
| Maximum, 4,096 px/measure | 21,992 | 9,328 | 47 |
| Maximum while following playback | — | 10,194 average / 10,752 p99 | 50.454 average |

Observed waveform projection averages are 0.054 ms normal, 0.083 ms maximum and 0.143 ms following, compared with the pre-cache normal window's 8.113 ms. Render/present timings were noisy and are not treated as a measured whole-frame or 144 Hz improvement. The original 5× total-vertex goal is still unmet and carries to T-250.

Actual legacy screen-stroke comparison is deliberately separate from the canonical reference: normal/max captures differ in 2,155/3,134 pixels (maximum RGB 46/103), all confined to the six-pixel envelope band at x=346–351; canonical scaling changes nearest sampling and overlapping stroke coverage. Compared with the older fractional-phase capture, 2,159/3,985 pixels differ (maximum RGB 51/157), in the same band. Other Compose field pixels match. Exact PCM maximum mode on the BPM-6000 fixture matches the current direct field with **zero differing RGB pixels**, uses 12,044 total vertices / 47 draws, and does not use an envelope texture. The 4,096-row exact stereo sample test uses 32,760 waveform vertices. Captures, phase reports, counters and strict reference logs are in `/tmp/wormedit-t249/`; the actual SM regression captures are in `/tmp/wormedit-sm-probe/`.

## T-250 Cache static field layers in tiles

- **Status:** ready
- **Outcome:** lane backgrounds, grid, notes, BGM, timing tiles, are rendered into cached render-target tiles (for example 512 logical px tall) keyed by document revision, chart, zoom, layout width, theme, and tile index; while only the scroll changes (playback following), a frame composites a few textured quads plus the dynamic overlays (playhead, cursor, hover, selection, ghosts, preview), so both CPU scene work and GPU vertex work stay near constant regardless of chart density.
- **Read first:** `src/ui/field/field_renderer.h`, `src/sys/compose_view.cpp` (T-213's scroll-only reuse), `src/ui/nuklear/player_preview_texture.cpp` (existing texture LRU pattern), SDL3 `SDL_SetRenderTarget`.
- **Files owned:** new `src/ui/field/field_tile_cache.cpp/.h`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/field/field_renderer.cpp`, `src/ui/field/field_renderer.h` (user-approved API extension, 2026-09-30), `CMakeLists.txt`, new `tests/ui/field_tile_cache_tests.cpp`, `tests/CMakeLists.txt`; user-approved exact semantic-filter generation extension (2026-09-30): `src/ui/frontend.h`, `src/sys/compose_view.h`, `src/sys/compose_view.cpp`, `tests/sys/frame_timing_tests.cpp`; user-approved shared retained-window projection extension (2026-10-01): `include/wormedit/core/editor_scene.h`, `src/core/editor_scene.cpp`, `tests/core/editor_scene_tests.cpp`; user-approved SDL-free shared interval-index extraction (2026-10-01): new `src/common/static_interval_index.h/.cpp`.
- **Steps:**
  1. Split the field into static layers (tile-cached) and dynamic overlays (drawn every frame).
  2. Tile LRU with a byte budget (for example 64 MiB); render missing tiles at most two per frame, falling back to direct drawing for tiles not ready.
  3. Invalidate on revision, zoom, width, theme, DPI, lane layout, or visibility-toggle changes.
  4. Tests for key invalidation and budget eviction (logic only, no GPU).
- **Invariants:** preserve layer order, colors, aspect, and dynamic cursor/playhead coordinates; retained texture translation may vary edge coverage by at most one logical pixel. Interior premultiplied composition differs by at most one RGB level. Device-loss/resize recreates tiles.
- **Reject/failure behavior:** if render targets are unsupported, keep direct drawing.
- **Verify:** standard build/test; T-246 numbers during Ctrl+Space before/after; smoke PNGs.
- **Functional checkpoint (2026-10-01):** Static layers use ordered 512-pixel tiles, a 64 MiB budget and a global two-tile preparation limit, with bounded source/index/scratch storage and direct fallback. Semantic filters carry an explicit style generation; the retained-window metadata path shares core validation and enforces primitive caps after extraction. Indexed edge redraw rejects timing marker centers outside the original viewport, eliminating phantom downscroll rail labels. Configured affected tidy has no new complexity violations. A clean HEAD-plus-17-owned-file build compiled/linked the application and passed 1,008/1,008 core/field-renderer/frame-timing/frontend-state cases, independently of remaining audio and main/application-frame changes. The combined checkpoint passed 1,217/1,217 (63.57 s). Actual isolated cached/direct comparisons passed all 20 up/down 20/35-percent-anchor first/hover/mid/reverse/resize cases: identical interior colors and changes only inside the approved one-pixel edge band. Evidence: `/tmp/wormedit-t250/functional-cut/root-isolated-build-v2.log` and `/tmp/wormedit-t250/boundary-final/pixel-comparison.json`.
- **Performance still pending:** The prior pre-minimap fixture measured normal direct/cached vertices 16,471.808→10,445.825 and Nuklear build 1.866065→0.846494 ms; dense 50,000-note scene time improved 0.108121→0.044300 ms, but maximum-zoom and density-independent frame cost did not meet the intended outcome. These historical measurements are not a current whole-frame speedup claim. T-250 stays ready/incomplete while T-272 arena conversion proceeds. Evidence: `/tmp/wormedit-t250/post-scalar-cap-metrics.md` and `.json`.
- **Current performance checkpoint (2026-10-01):** Exact `3ead35f` cached/direct applications share CPU scalar reuse, RMS and minimap storage; only the static tile draw branch differs. Six serial isolated Xvfb following runs use 50,000/5,209-note, 1,042-measure fixtures, validated 192/4,096 px/measure, equal warm-up and 480 fully post-warm frames each (2,880 total). Dense normal direct→cached vertices are 9,834.917→6,835.085 and Nuklear build 1.347520→0.940864 ms, while draws increase 50.567→62.746. Dense maximum vertices are 5,654.619→5,543.830; build regresses 0.560747→0.664654 ms and draws increase 49.967→59.815. Cached normal dense/sparse scene costs 0.084463/0.053861 ms and build 0.940864/0.586201 ms, so the density-independent outcome remains unproven. Shared CPU reuse and differing transport endpoints prevent attributing scene differences to the tile toggle. This is neither a whole-frame/FPS gain nor the original 5× target; T-250 remains ready/incomplete. Evidence: `/tmp/wormedit-t250/current-performance/metrics.json`, `performance-checkpoint.md` and per-case timing/ownership/window-boundary/capture artifacts.
- **Retained-source coverage checkpoint (2026-10-01):** Default core callers retain the original three-viewport source window; Compose requests extra bounded margin for complete in-chart 512-physical-pixel tile coverage plus glyph guard at scale ≥0.5. Incomplete coverage, smaller scales and expanded primitive-budget failures retain the direct correctness fallback. Revision/style keys, 64 MiB combined budget and two-tile preparation limit are preserved. Independent clean `e30fedd` baseline/prototype builds passed three new production cases / 2,026 assertions and 32 focused cases; configured affected tidy has no new-function complexity warnings. The shared application build and full checkpoint passed 1,257/1,257 (77.77 s). Thirty private field comparisons passed: 28 RGB-identical, with two normal downscroll resize captures differing only inside the approved one-pixel edge band and identical interiors; all ten maximum-zoom comparisons are identical. A separate current-worktree integration gate passed 20/20 RGB-identical normal/max up/down scroll/resize captures. Eight serial isolated cost runs cover dense/sparse charts at 192/4,096 px/measure, 480 wholly post-warm frames each. Dense normal baseline→padding scene cost is 0.083513→0.065687 ms, Nuklear build 0.951412→0.779745 ms and vertices 6,909.073→6,141.833, while draws increase 62.660→63.588. Dense maximum scene cost is 0.285138→0.229654 ms and build 0.649309→0.630494 ms; vertices barely change 5,537.438→5,494.981. Sparse normal/max scene costs are 0.044891→0.036609 / 0.282281→0.225808 ms. Workspace, Convert and Submit are separately recorded; no whole-frame/FPS or GPU elapsed-time claim is made. Untimed sampled traces show six tile hits and zero coverage misses at both zooms; observed combined storage peaks at 62.988 MiB, within the limit. The larger normal retained source has 1,058 versus 441 primitives, so this is a measured coverage/performance tradeoff, not elimination of source work. T-250 remains ready/incomplete: density independence and the original 5× target remain unproven. Evidence: `/home/zardoru/.cache/wormedit-verification/t250-coverage/` (`root-build.log`, `root-full.log`, private `coverage-checkpoint.md`, cost metrics/endpoint clocks, separate private pixel edge/interior comparisons and `root-gate/pixel-comparison.json`).
- **Out of scope:** Timing tab; the independently bounded envelope ring completed by T-249.

Decision (user, 2026-09-30): permit at most one pixel of static-layer raster variation and extend `field_renderer.h` for the retained tile API. The isolated OpenGL prototype established why exact fractional translation is incompatible with individually truncated authored arrow points. No additional quantization is applied to cursor, playhead, PCM, or the T-249 envelope contract. Completion requires separate interior-color and edge-coverage gates, actual fractional forward/reverse/zoom/reset verification, and measured CPU/GPU work; the functional gates above are verified, while the performance outcome remains pending.

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

## T-293 Never freeze the UI on a stalled swapchain present

- **Status:** ready
- **Outcome:** the application stays responsive (input, audio, autosave, status) even when the compositor stops releasing swapchain images; under a Wayland session it uses native Wayland by default; a watchdog reports presentation stalls instead of the window silently freezing.
- **Evidence (gdb, 2026-09-30, user's niri session; main-thread stack saved as `/tmp/wormedit-present-stall-bt.txt`):** three samples one second apart were identical: `main` → `render_application_frame` (`src/sys/application_frame_services.cpp:168`) → `worm_window_swap` (`src/sys/window.cpp:120`) → `SDL_RenderPresent` → `GPU_RenderPresent` → `SDL_WaitAndAcquireGPUSwapchainTexture` → `VULKAN_INTERNAL_AcquireSwapchainTexture(block=true)` → radv → `drmSyncobjTimelineWait`, i.e. an unbounded wait for a swapchain image. The video driver was **x11** (`SDL_GetCurrentVideoDriver()`), running through `xwayland-satellite` under niri (`XDG_SESSION_TYPE=wayland`); window flags `0x2030` (borderless, resizable, high pixel density — not hidden, minimized, or occluded). No wormedit mutex was involved; other threads were idle. The swapchain worker threads had been recreated shortly before (newer thread ids), suggesting a resize/recreate preceded the stall.
- **Follow-up evidence (same session, 2026-09-30):** the process was not permanently blocked: the user could still drag a selection rectangle and resize, but clicks on Nuklear widgets did nothing. Live state via gdb: one Nuklear window (`wormedit-workspace`, flags `0x1120`, no `NK_WINDOW_NO_INPUT`), no popups, no mouse grab, SDL and Nuklear mouse coordinates identical (scale 1), no modal open; `g_window_focused` was `false` (no `SDL_EVENT_WINDOW_FOCUS_GAINED` after a focus loss). A breakpoint on `dispatch_transport_player_action` fired only 31 times in ~20 s, and the frame-timing ring held many ~988–994 ms entries in submit/present: the loop was running at **~1 frame per second**, the throttle compositors apply to surfaces they consider invisible. At 1 fps a normal click's press and release arrive in the same frame and Nuklear's button behavior misses it (the same effect was seen with fast `xdotool click` under Xvfb), while multi-frame drags and resizes still work.
- **Read first:** `src/sys/window.cpp` (window/renderer creation, `SDL_SetRenderVSync`, `worm_window_swap`), `src/sys/application_frame_services.cpp` (`render_application_frame`), `src/sys/main.cpp` (main loop and event handling), SDL3 `SDL_hints.h` (`SDL_HINT_VIDEO_DRIVER`) and `SDL_render.h` (`SDL_SetRenderVSync`).
- **Files owned:** `src/sys/window.cpp`, `include/wormedit/window.h`, `src/sys/application_frame_services.cpp/.h`, `src/sys/main.cpp` (loop pacing only), new `src/sys/frame_watchdog.cpp/.h`, new `tests/sys/frame_watchdog_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Driver choice: when `WAYLAND_DISPLAY` is set and the user has not set `SDL_VIDEODRIVER`, set `SDL_HINT_VIDEO_DRIVER` to `"wayland,x11"` before `SDL_Init`. Verify the borderless custom title bar still moves/resizes on Wayland (use `SDL_SetWindowHitTest` for the title bar and edges if it does not), and that drag-and-drop, file dialogs, and high-DPI scale work.
  2. Non-blocking presentation: stop relying on the swapchain wait for pacing. Set renderer VSync off (or adaptive when available) and pace frames in the loop to the display refresh rate (`SDL_GetCurrentDisplayMode` refresh, `SDL_DelayPrecise` for the remainder), so a compositor that withholds images cannot block the thread indefinitely. Keep a setting to restore VSync (program option, T-256).
  3. Skip drawing while the window is minimized, hidden, or occluded (`SDL_EVENT_WINDOW_MINIMIZED/HIDDEN/OCCLUDED/EXPOSED`), while still pumping events, the audio service, autosave, and jobs at a low rate (`SDL_WaitEventTimeout`).
  4. Watchdog: the frame loop stores a heartbeat (monotonic time + current frame phase from `frame_timing`) in an atomic; a small thread checks it every 500 ms and, after 2 s without progress, writes one line to stderr and the crash log (phase, driver, window flags) and, once the loop resumes, posts a warning notification (T-219) "Display presentation stalled for N s".
  5. Never lose a click at low frame rates: when one frame's event batch contains both a press and a release of the same mouse button, feed the press this frame and defer the release (and any later events in the batch) to the next frame, so Nuklear sees down then up across two frames. Same for key press/release pairs used by shortcuts.
  6. Do not trust a missing focus-gained event: treat the window as focused again on any mouse button press or key press inside it (`SDL_GetKeyboardFocus() == window`), and re-query focus on `SDL_EVENT_WINDOW_EXPOSED` and on every input event instead of relying only on `SDL_EVENT_WINDOW_FOCUS_GAINED`.
  7. Watchdog also flags sustained low frame rate: if 10 consecutive frames exceed 250 ms, log once (driver, window flags, focus flag, present time) and post a warning notification "The compositor is throttling this window (≈N fps)".
  8. Tests: watchdog logic with an injected clock (stall detected once, cleared on resume); a unit test for the driver-hint decision function (env combinations).
- **Invariants:** rendering output unchanged; audio playback and Stop unaffected by presentation stalls; users can still force x11 with `SDL_VIDEODRIVER=x11`.
- **Reject/failure behavior:** if native Wayland initialization fails, fall back to x11 automatically with one warning.
- **Verify:** standard build/test; manual on niri: run without `SDL_VIDEODRIVER`, confirm `wayland` is used, resize repeatedly, move the window between workspaces/columns, play with Ctrl+Space for several minutes; no freeze. With `SDL_VIDEODRIVER=x11`, a stall (if it recurs) is reported by the watchdog and input still works after the compositor releases images.
- **Out of scope:** the field renderer (T-248), frame-time work (live-transport group).

## T-305 Cache the audio minimap waveform in a bounded GPU texture

- **Status:** done
- **Outcome:** the whole-source minimap waveform is drawn from a retained texture instead of rebuilding hundreds of stroke vertices each frame; playhead, viewport thumb and offset markers remain exact dynamic overlays.
- **Read first:** `src/ui/nuklear/compose_panel.cpp`, `src/sys/waveform_view_projection.cpp`, `src/ui/nuklear/sdl_backend.h`, `src/ui/frontend.h`.
- **Files owned:** `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/sdl_backend.cpp/.h`, `src/ui/frontend.h`, `src/sys/waveform_view_projection.cpp/.h`, `src/sys/compose_view.h`, `tests/ui/nuklear_sdl_backend_tests.cpp`, `tests/sys/frame_timing_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Attribute minimap-only vertices, conversion time and uploads with an isolated baseline. 2. Publish immutable overview generation and cache the entire waveform texture by source/detail/style/size/renderer generation, independently of the queued main-field geometry. 3. Verify direct-versus-texture pixels, resize/reset/source replacement and unchanged exact seek/drag markers, then report measured workload differences.
- **Geometry-only baseline (2026-10-01):** An isolated pinned-Nuklear conversion with the production vertex layout/AA configuration, 512 representative peak strokes and identical dynamic overlays measured 3,116 vertices / 6,300 indices (139.502 µs average conversion over 240 warmed samples). Overlay-only measured 44 / 156; substituting one texture quad measured 48 / 162 (3.238 µs). Thus waveform geometry alone is 3,072 vertices versus 4; these are synthetic conversion measurements, not a whole-application frame-time gain or an implemented texture/upload cache. Actual source-generation, renderer lifecycle, pixels and application workload verification remain required. Reproducer and real output: `/tmp/wormedit-t305/overview-baseline.cpp` and `overview-baseline.log`.
- **Implemented evidence (2026-10-01):** Immutable overview generation and a bounded physical-pixel texture retain the original opaque interior; compact original boundary geometry preserves fractional edge rasterization. One cold framebuffer readback/upload is followed by zero waveform readbacks/uploads during warm playback. Source/detail/style/size/logical-presentation/renderer changes invalidate; failure and unsupported presentation modes retain original strokes. Configured tidy and isolated HEAD-plus-eight-file projection/frontend test syntax checks passed; application build and full checkpoint passed 1,217/1,217 (63.57 s). Actual OpenGL Xvfb backend tests passed 10 cases / 377 assertions, including pixel parity at 1/1.25/2 scales, fractional phases, production STRETCH presentation, lifecycle and readback failure recovery. Actual generated-audio cached/direct paused 1,280×720 application captures were pixel-identical. Main-context vertices were 7,923→4,894 and indices 18,705→12,909, while draw calls increased 58→64; overview geometry was 3,080 cold→192 warm. The observed cold readback cost 18.55 ms (another run 24.98 ms), with 31,136 texture bytes, 1,876 retained geometry bytes and 114,368 peak CPU bytes. These are fixture observations, not a general GPU/FPS gain. Evidence: `/tmp/wormedit-t305/app/workload-result.json`, paired workload logs/PNGs, `opengl-logical-tests.log` and `root-selective-cut.log`. The final GUI driver verifies its private Xvfb process, authentication and exact application window PID before every interaction.
- **Invariants:** the whole-source overview needs no scrolling ring; normal playback performs no waveform texture upload; CPU/GPU storage is bounded and accounted; T-299 conditional placement and existing navigation remain intact.
- **Reject/failure behavior:** unsupported sizes or allocation failures use the original bounded stroke drawing without changing interaction or displaying stale source data.
- **Verify:** standard build/test; configured clang-tidy; isolated Xvfb pixel and workload checks using generated audio.
- **Out of scope:** flattened workspace allocation (T-272), PCM detail, changing minimap navigation.
