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

- **Status:** done
- **Outcome:** Timing lanes accommodate horizontal multi-digit values and use Lucide icons with discoverable labels; Kiai and Anchor remain compact. Drawing, picking and projection share the same lane geometry.
- **Read first:** src/sys/compose/views/compose_view.cpp; src/sys/compose/views/compose_view.h; src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; include/wormedit/core/timing_lane_controller.h.
- **Files owned:** src/sys/compose/views/compose_view.cpp; src/sys/compose/views/compose_view.h; src/ui/frontend.h; src/ui/nuklear/compose_canvas.cpp; src/ui/nuklear/compose_input_panel.cpp; src/ui/nuklear/render/lucide_icons.h; src/ui/nuklear/render/lucide_icons.cpp; src/ui/compose_timing_geometry.h; src/ui/nuklear/compose_timing_input.cpp; src/ui/nuklear/compose_panel_internal.h; src/sys/compose/input/compose_input.cpp; src/ui/field/field_renderer.h; src/ui/field/field_renderer.cpp; src/ui/nuklear/render/lucide_icon_pixels.h; scripts/generate-lucide-icons.py; assets/icons/lucide/waves-horizontal.svg; assets/icons/lucide/gauge.svg; assets/icons/lucide/hourglass.svg; assets/icons/lucide/fast-forward.svg; assets/icons/lucide/flame.svg; assets/icons/lucide/anchor.svg; assets/icons/lucide/README.md; tests/sys/compose_notification_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/ui/compose_stem_lane_render_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/nuklear_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-inspector-lanes.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root records supporting scope within the outcome.
- **Steps:** 1. Audit all timing lane bounds and width calculations. 2. Introduce shared per-kind geometry for projection, rendering and picking. 3. Add readable widths and icons, retaining compact Kiai/Anchor lanes. 4. Verify hidden lanes, narrow windows, precise clicks and horizontal value readability.
- **Invariants:** BPM/SV can show five significant digits and a decimal; pauses/warps show three significant digits. No vertical labels. Canvas minimum width, clipping and timing identities stay coherent.
- **Reject/failure behavior:** Hidden/clipped lane regions cannot receive timing edits; stale/modal guards remain active.
- **Verify:** normal/Release builds and full CTest; production event harness; affected default clang-tidy; isolated authenticated Xvfb; documentation and whitespace checks.
- **Out of scope:** Authored timing math and note-lane skin layout.

T-561 supporting scope: the BGA/timing event harness collapses the new Quantize/Snap section before exercising lower inspector controls; the quantize-report harness follows the concise Quantize action label. This preserves independent workflow checks after the additional grouping space.

T-561–T-562 verification (2026-10-11): normal/Release builds and all **3,381/3,381 CTest checks in each** pass. Focused production event/render checks pass **824 assertions / 27 cases**, covering quantize settings, preview and apply stamps, popup guards, section collapse, lower-inspector workflows, and compact/wide/empty stem headers with UTF-8 ellipsis and ordinary BGM numbers. Quantize/Snap uses the existing expandable section chrome and shared separators; Preview and Quantize share a row. Short setting captions retain detailed tooltips and fit the narrow inspector. Compact stems use the existing safe filename renderer at every width; ordinary and nameless lanes use the source number. Native isolated authenticated Xvfb Release captures at 1600×900 and 1200×750 verify grouped controls, shared action row and full-filename hover. The final Save leaves the complete normalized project unchanged (the first fixture load resolved missing-resource flags before this comparison). Affected default clang-tidy has no new/increased complexity violations; unchanged legacy speed-graph and Catch findings remain. Documentation and whitespace checks pass.

Evidence: `/tmp/inspector-lanes-{build,release-build,focused,final-ctest,release-ctest,tidy,final-tidy,captions-tidy,fallback-tidy}.log`; native `/tmp/inspector-lanes-{wide,actions,narrow,tooltip}.png` and before/after project snapshots. Full-suite output tails: `100% tests passed out of 3381` for both configurations; focused tail: `All tests passed (824 assertions in 27 test cases)`. T-563 verification follows.

T-563 supporting scope: replace the horizontal-layout count parameter with the visibility mask in both production dispatch and projection (plus the notification fixture), so per-kind widths remain authoritative. Staged timing drags also use the shared view geometry, excluding BGA. The field renderer gains a tinted image primitive and Lucide exposes its existing texture handle, allowing headers to participate in the existing cached field pass. Pinned Lucide sources and the deterministic generator supply timing icons. The shared helper belongs under backend-neutral src/ui, rather than src/sys, for both frontend and application consumers.

T-563 geometry audit also removes the private timing-right calculation and aliases the measure-rail width in Compose to the shared helper. Compact numeric timing labels now retain five significant digits instead of two decimal places; authored values and exact hover details remain unchanged.

T-563 action-fixture extension: note Draw/Erase dispatcher tests now target the center of the projected playable pane, replacing a fixed x=400 that moved into a timing lane when numeric lanes widened. The assertions still verify actual note creation/erase and Undo.

T-563 verification (2026-10-11): the backend-neutral timing geometry owns per-kind widths, rail origin, BGA exclusion, inner extents and marker rectangles. Application projection, command dispatch, rendering, picking, staged drags, span gestures and area selection consume it. BPM/SV/SPEEDS use 64 px, Stop/Delay/Warp 48 px, Kiai/Anchor 24 px; narrow fields compress proportionally while marker labels clip inside their own lane and retain full hover details. Headers use pinned Lucide icons through the existing cached field pass, with live descriptive tooltips. Compact decimal labels retain five significant digits. No authored values change.

Normal/Release builds and **3,386/3,386 full CTest checks in each** pass. Focused geometry/dispatcher checks pass **27,023 assertions / 18 cases**, including all 256 masks at normal and compressed widths, BGA exclusion, precise boundaries and numeric precision. Production event checks pass **259 assertions / 2 cases**, including per-kind double-click editing with visible/hidden peers and with/without BGA, hidden marker rejection, and staged SV drags rejecting neighboring BGA. Final default clang-tidy has no new/increased complexity violations; legacy surface-height, speed-graph and Catch findings remain. Native isolated authenticated Xvfb Release at 1600×900 and 1200×750 shows complete 123.45 BPM, 1.2345 SV and 2.3456 SPEEDS values, distinct header icons and a full BPM tooltip. Final Save preserves the complete authored document. Icon generation is deterministic. Documentation and whitespace checks pass.

Evidence: `/tmp/timing-lanes-{final-build,release-final-build,final-ctest,release-ctest,geometry-focused,events-focused,source-tidy,tests-tidy,final-tidy,actions-tidy}.log`; `/tmp/timing-lanes-{wide,narrow,bpm-tooltip}.png`, native before/after project snapshots. Output tails: `100% tests passed out of 3386` in both configurations, `All tests passed (27023 assertions in 18 test cases)` and `All tests passed (259 assertions in 2 test cases)`. The first broad run caught two fixed-coordinate Draw/Erase fixtures; the corrected projected-coordinate fixtures and both full final suites pass. Earlier new Catch complexity findings were resolved through the existing assertion helpers before the final tidy run.
