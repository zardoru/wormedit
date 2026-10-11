# Scratchpad inspector and lane readability

[Back to the task card index](../tasks.md).

## T-561 Group Quantize/Snap inspector controls

- **Status:** done
- **Outcome:** Quantize/Snap has a visible section header, spacing between Space, Halfway objects, Hold tails and Presets, and adjacent Preview/Apply controls on one row.
- **Read first:** src/ui/nuklear/compose_inspector.cpp; src/ui/nuklear/compose_inspector.h; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/compose_inspector.cpp; tests/ui/nuklear_event_harness_tests.cpp; tests/ui/bga_lane_event_harness_tests.cpp; tests/ui/quantize_report_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-inspector-lanes.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope within the outcome.
- **Steps:** 1. Audit current inspector chrome and quantize input guards. 2. Add the header and group spacing, placing Preview and Apply together. 3. Verify enabled/disabled actions, popup capture and narrow inspector layout.
- **Invariants:** Existing quantize semantics, selection stamps, popup ownership and keyboard shortcut remain intact.
- **Reject/failure behavior:** Invalid or blocked quantize remains disabled; popup clicks never leak through.
- **Verify:** normal/Release builds and full CTest; production event harness; affected default clang-tidy; isolated authenticated Xvfb; documentation and whitespace checks.
- **Out of scope:** Lane geometry and quantize math.

## T-562 Show useful labels on compact BGM lanes

- **Status:** done
- **Outcome:** Compact BGM lanes show the useful channel number or stem filename rather than wasting their width on a BGM prefix; hover retains the full filename.
- **Read first:** src/ui/nuklear/compose_canvas.cpp; src/ui/frontend.h; tests/ui/compose_stem_lane_render_tests.cpp.
- **Files owned:** src/ui/nuklear/compose_canvas.cpp; tests/ui/compose_stem_lane_render_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-inspector-lanes.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope within the outcome.
- **Steps:** 1. Audit compact/wide BGM label projection. 2. Render useful compact labels with safe UTF-8 clipping. 3. Verify short/wide stems, ordinary BGM and hovered full names.
- **Invariants:** Source lane identity, resize geometry, asset names and authored data remain unchanged.
- **Reject/failure behavior:** Empty filenames use the channel number; clipped labels remain inside their lanes.
- **Verify:** normal/Release builds and full CTest; production event harness; affected default clang-tidy; isolated authenticated Xvfb; documentation and whitespace checks.
- **Out of scope:** BGM editing, renaming and timing-lane sizing.

## T-563 Make timing lanes readable with shared geometry

- **Status:** ready
- **Outcome:** Timing lanes accommodate horizontal multi-digit values and use Lucide icons with discoverable labels; Kiai and Anchor remain compact. Drawing, picking and projection share the same lane geometry.
- **Read first:** src/sys/compose_view.cpp; src/sys/compose_view.h; src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; include/wormedit/core/timing_lane_controller.h.
- **Files owned:** src/sys/compose_view.cpp; src/sys/compose_view.h; src/ui/frontend.h; src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; src/ui/nuklear/lucide_icons.h; src/ui/nuklear/lucide_icons.cpp; src/sys/compose_timing_geometry.h; tests/sys/frame_timing_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-inspector-lanes.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope within the outcome.
- **Steps:** 1. Audit all timing lane bounds and width calculations. 2. Introduce shared per-kind geometry for projection, rendering and picking. 3. Add readable widths and icons, retaining compact Kiai/Anchor lanes. 4. Verify hidden lanes, narrow windows, precise clicks and horizontal value readability.
- **Invariants:** BPM/SV can show five significant digits and a decimal; pauses/warps show three significant digits. No vertical labels. Canvas minimum width, clipping and timing identities stay coherent.
- **Reject/failure behavior:** Hidden/clipped lane regions cannot receive timing edits; stale/modal guards remain active.
- **Verify:** normal/Release builds and full CTest; production event harness; affected default clang-tidy; isolated authenticated Xvfb; documentation and whitespace checks.
- **Out of scope:** Authored timing math and note-lane skin layout.

T-561 supporting scope: the BGA/timing event harness collapses the new Quantize/Snap section before exercising lower inspector controls; the quantize-report harness follows the concise Quantize action label. This preserves independent workflow checks after the additional grouping space.

T-561–T-562 verification (2026-10-11): normal/Release builds and all **3,381/3,381 CTest checks in each** pass. Focused production event/render checks pass **824 assertions / 27 cases**, covering quantize settings, preview and apply stamps, popup guards, section collapse, lower-inspector workflows, and compact/wide/empty stem headers with UTF-8 ellipsis and ordinary BGM numbers. Quantize/Snap uses the existing expandable section chrome and shared separators; Preview and Quantize share a row. Short setting captions retain detailed tooltips and fit the narrow inspector. Compact stems use the existing safe filename renderer at every width; ordinary and nameless lanes use the source number. Native isolated authenticated Xvfb Release captures at 1600×900 and 1200×750 verify grouped controls, shared action row and full-filename hover. The final Save leaves the complete normalized project unchanged (the first fixture load resolved missing-resource flags before this comparison). Affected default clang-tidy has no new/increased complexity violations; unchanged legacy speed-graph and Catch findings remain. Documentation and whitespace checks pass.

Evidence: `/tmp/inspector-lanes-{build,release-build,focused,final-ctest,release-ctest,tidy,final-tidy,captions-tidy,fallback-tidy}.log`; native `/tmp/inspector-lanes-{wide,actions,narrow,tooltip}.png` and before/after project snapshots. Full-suite output tails: `100% tests passed out of 3381` for both configurations; focused tail: `All tests passed (824 assertions in 27 test cases)`. T-563 remains ready.
