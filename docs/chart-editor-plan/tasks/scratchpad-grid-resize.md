# Scratchpad grid and lane feedback

[Back to the task card index](../tasks.md).

## T-555 Offer fifth grids and find a selection fitting snap

- **Status:** done
- **Outcome:** Supported fifth-based edit/quantize grids are offered, and selected note heads/hold ends/BGM positions can choose an exact fitting measure-local grid without editing the document.
- **Read first:** include/wormedit/core/snap.h; src/core/snap.cpp; src/sys/compose/actions/compose_actions.cpp; src/ui/nuklear/compose_snap_grid.cpp; src/ui/nuklear/compose_inspector.cpp.
- **Files owned:** include/wormedit/core/snap.h; src/core/snap.cpp; src/sys/compose/actions/compose_actions.cpp; src/sys/compose/views/compose_view.h; src/ui/frontend.h; src/ui/nuklear/compose_snap_grid.cpp; src/ui/nuklear/compose_inspector.cpp; src/ui/nuklear/compose_note_menu.cpp; src/ui/nuklear/compose_area_menu.cpp; tests/core/snap_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/snap_grid_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-grid-resize.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root coordinates supporting files within scope.
- **Steps:** 1. Audit supported grid projection and menu routing. 2. Add fifth grids and exact shared fitting math. 3. Wire selection menus and snap popup through guarded session-only action. 4. Verify math, context guards, UI and authored document preservation.
- **Invariants:** measure-local snap phase; arbitrary shortened measures; chart-resolution divisibility; no note, revision or Undo changes; quantize follows the chosen grid.
- **Reject/failure behavior:** missing/empty/stale/gesturing selection leaves grid and document unchanged; unsupported denominators stay filtered.
- **Verify:** normal/Release builds; full CTest; production event harness; affected default clang-tidy; isolated native UI; documentation and whitespace checks.
- **Out of scope:** timing conversion, audio loop changes and performance work.

## T-556 Keep BMSON horizontal lane resize working through live projection

- **Status:** done
- **Outcome:** Dragging a visible BMSON stem-lane edge changes and retains its width during live application projection.
- **Read first:** src/ui/nuklear/compose_object_input.cpp; src/ui/nuklear/compose_panel.cpp; src/ui/nuklear/compose_state.cpp; src/sys/compose/views/compose_view.cpp; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/compose_object_input.cpp; src/ui/nuklear/compose_panel.cpp; src/ui/nuklear/compose_state.cpp; src/ui/nuklear/compose_state.h; src/sys/compose/views/compose_view.cpp; src/core/editor_scene.cpp; src/sys/main.cpp; src/sys/commands/application_command_dispatch.h; src/sys/commands/application_command_dispatch.cpp; src/sys/compose/views/compose_view.h; tests/ui/nuklear_event_harness_tests.cpp; tests/sys/frame_timing_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-grid-resize.md; memory/chart-editor-status/resume.md. Root may record necessary supporting files after reproducing the fault.
- **Steps:** 1. Reproduce with live geometry rather than static fixtures. 2. Fix the owner or projection responsible. 3. Verify held/moving/released and cancelled drags, clipped edges and real application geometry.
- **Invariants:** width remains a session preference; no authored changes; stable project/chart/lane IDs; unrelated canvas editing retains its input.
- **Reject/failure behavior:** Escape, focus loss, changed chart/project and modal ownership restore the original width.
- **Verify:** normal/Release builds; full CTest; production event harness; affected default clang-tidy; isolated native UI; documentation and whitespace checks.
- **Out of scope:** packed-lane width settings and asset edits.

## T-557 Investigate a later WASM browser target

- **Status:** deferred (was: ready)
- **Outcome:** A later explicit pickup produces an Emscripten build plan covering SDL, audio/video decoders, files, threads and deployment, with a minimal browser smoke target.
- **Read first:** CMakeLists.txt; CMakePresets.json; docs/building.md.
- **Files owned:** CMakeLists.txt; CMakePresets.json; cmake/toolchains/wasm.cmake; docs/building.md; docs/chart-editor-plan/tasks/scratchpad-grid-resize.md.
- **Steps:** 1. Start only when the user explicitly names this card. 2. Audit browser-incompatible APIs and dependencies. 3. Record supported capability tradeoffs and implement a minimal target. 4. Verify browser startup and file round-trip.
- **Invariants:** native builds retain their behavior; browser limitations are explicit.
- **Reject/failure behavior:** unsupported browser capabilities fail clearly without pretending to save externally.
- **Verify:** future Emscripten build plus browser smoke; documentation checks now validate the deferred card.
- **Out of scope:** current batch implementation, browser packaging and deployment now.

T-556 supporting scope: move the existing project-session reset into the testable application dispatcher and preserve its complete application-lifetime service bundle. Native startup bypassed this reset; opening/creating a project exposed the missing callback.

Verification completed (2026-10-11): normal/Release builds and 3,363/3,363 full CTest checks in each; 464 production event cases / 6,385 assertions; affected default clang-tidy with no new/increased violations (unchanged legacy findings remain). Native isolated authenticated TCP Xvfb verifies 1/5 fitting selection and complete authored-document preservation after Save, plus File → Open using a deterministic picker fixture followed by retained 36→111 px stem edge resize. Shared service-bundle regression also covers dock geometry, bookmark transport and injected clock preservation, while chart-specific state clears. T-557 is deliberately deferred, fulfilling the requested planning item only. Documentation and whitespace checks pass. Evidence: `/tmp/scratchpad-grid-{last-build,last-release,menu-build,menu-release,final-ctest,final-release-ctest,final-events,tidy,final-tests-tidy,last-tidy,area-tidy}.log`, `/tmp/fitting-grid-{popup,applied}.png`, `/tmp/stem-resize-open-{before,dragging,after}.png`.
