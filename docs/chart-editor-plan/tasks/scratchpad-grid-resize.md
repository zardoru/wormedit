# Scratchpad grid and lane feedback

[Back to the task card index](../tasks.md).

## T-555 Offer fifth grids and find a selection fitting snap

- **Status:** done
- **Outcome:** Supported fifth-based edit/quantize grids are offered, and selected note heads/hold ends/BGM positions can choose an exact fitting measure-local grid without editing the document.
- **Read first:** include/wormedit/core/snap.h; src/core/snap.cpp; src/sys/compose/actions/compose_actions.cpp; src/ui/nuklear/compose/compose_snap_grid.cpp; src/ui/nuklear/compose/compose_inspector.cpp.
- **Files owned:** include/wormedit/core/snap.h; src/core/snap.cpp; src/sys/compose/actions/compose_actions.cpp; src/sys/compose/views/compose_view.h; src/ui/frontend.h; src/ui/nuklear/compose/compose_snap_grid.cpp; src/ui/nuklear/compose/compose_inspector.cpp; src/ui/nuklear/compose/compose_note_menu.cpp; src/ui/nuklear/compose/compose_area_menu.cpp; tests/core/snap_tests.cpp; tests/sys/compose_action_notification_tests.cpp; tests/sys/frame_timing_tests.cpp; tests/ui/snap_grid_event_harness_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-grid-resize.md; docs/chart-editor-plan/tasks.md; memory/chart-editor-status/resume.md. Root coordinates supporting files within scope.
- **Steps:** 1. Audit supported grid projection and menu routing. 2. Add fifth grids and exact shared fitting math. 3. Wire selection menus and snap popup through guarded session-only action. 4. Verify math, context guards, UI and authored document preservation.
- **Invariants:** measure-local snap phase; arbitrary shortened measures; chart-resolution divisibility; no note, revision or Undo changes; quantize follows the chosen grid.
- **Reject/failure behavior:** missing/empty/stale/gesturing selection leaves grid and document unchanged; unsupported denominators stay filtered.
- **Verify:** normal/Release builds; full CTest; production event harness; affected default clang-tidy; isolated native UI; documentation and whitespace checks.
- **Out of scope:** timing conversion, audio loop changes and performance work.

## T-556 Keep BMSON horizontal lane resize working through live projection

- **Status:** done
- **Outcome:** Dragging a visible BMSON stem-lane edge changes and retains its width during live application projection.
- **Read first:** src/ui/nuklear/compose/compose_object_input.cpp; src/ui/nuklear/compose/compose_panel.cpp; src/ui/nuklear/compose/compose_state.cpp; src/sys/compose/views/compose_view.cpp; tests/ui/nuklear_event_harness_tests.cpp.
- **Files owned:** src/ui/nuklear/compose/compose_object_input.cpp; src/ui/nuklear/compose/compose_panel.cpp; src/ui/nuklear/compose/compose_state.cpp; src/ui/nuklear/compose/compose_state.h; src/sys/compose/views/compose_view.cpp; src/core/editor_scene.cpp; src/sys/app/main.cpp; src/sys/commands/application_command_dispatch.h; src/sys/commands/application_command_dispatch.cpp; src/sys/compose/views/compose_view.h; tests/ui/nuklear_event_harness_tests.cpp; tests/sys/frame_timing_tests.cpp; docs/chart-editor-plan/tasks/scratchpad-grid-resize.md; memory/chart-editor-status/resume.md. Root may record necessary supporting files after reproducing the fault.
- **Steps:** 1. Reproduce with live geometry rather than static fixtures. 2. Fix the owner or projection responsible. 3. Verify held/moving/released and cancelled drags, clipped edges and real application geometry.
- **Invariants:** width remains a session preference; no authored changes; stable project/chart/lane IDs; unrelated canvas editing retains its input.
- **Reject/failure behavior:** Escape, focus loss, changed chart/project and modal ownership restore the original width.
- **Verify:** normal/Release builds; full CTest; production event harness; affected default clang-tidy; isolated native UI; documentation and whitespace checks.
- **Out of scope:** packed-lane width settings and asset edits.

## T-557 Investigate a later WASM browser target

- **Status:** partial (post-1.0 implementation and browser smoke verified, 2026-10-28; pinned complexity tool unavailable)
- **Outcome:** An Emscripten build plan covers SDL, audio/video decoders, files, threads and deployment, with a minimal browser codec smoke target.
- **Read first:** CMakeLists.txt; CMakePresets.json; docs/building.md.
- **Files owned:** CMakeLists.txt; CMakePresets.json; cmake/toolchains/wasm.cmake; docs/building.md; docs/chart-editor-plan/tasks/scratchpad-grid-resize.md; src/tools/wasm_smoke.cpp; packaging/wasm/index.html; tests/wasm/smoke.cjs; scripts/serve-wasm.py; src/core/bmson_sound_channel_editor.cpp; src/core/bmson_layer_defaults.cpp.
- **Supporting scope (coordinator, 2026-10-28):** the tool, HTML shell, local smoke server and browser regression implement and verify the minimal target. The BMSON assignment helpers must use `size_t` for a vector offset/source order: their unnecessary `uint64_t` widening fails wasm32 compilation. The layer-count guard uses the existing standard `std::cmp_greater` utility to compare `size_t` with a 64-bit limit without a wasm32 tautological-comparison error. No change to native serialization or authored behavior is intended. Main is feature-frozen; keep this in a separate post-1.0 draft. PR #64 also edits CMake/building docs to remove otoworm; preserve both sets of changes when integrating. PRs #56/#57/#58/#63 are independent and not prerequisites.
- **Steps:** 1. Start on the owner's explicit selection of this card (selected by its unique WASM description). 2. Audit browser-incompatible APIs and dependencies. 3. Record supported capability tradeoffs and implement a minimal target. 4. Verify browser startup and file round-trip.
- **Done so far:** stale-plan audit found no WASM target; browser/platform audit, preset/toolchain, browser shell, local server and regression are implemented. Emscripten 6.0.5 builds the complete core and the smoke JS/WASM/data successfully; incremental rebuild has no work. Chromium 151.0.7922.173 passes startup, actual download/re-upload with exact canonical bytes, Unicode, malformed JSON/UTF-8, future version, invalid identity, 64 MiB admission limit, reload volatility and missing isolation-header rejection. The native core builds; both affected BMSON test translation units were rebuilt against the current core/Catch2 and pass all 40 cases / 620 assertions. Documentation passes 5/5 and direct clang-tidy 23.1.5 finds no complexity warning in all three affected C++ units. The full GUI rebuild was deliberately stopped: this target excludes it, and no full application or global CTest pass is claimed. Official emsdk 4.0.18 binary download is denied by the network proxy; Debian 3.1.69 failed the C++23 capability probe, so an isolated Debian Emscripten 6.0.5/Clang 21.1.8 toolchain is used. The complete capability audit is in [building](../../building.md#experimental-browser-codec-smoke-post-10-t-557).
- **Remaining:** run `scripts/check-complexity.py` with the baseline-pinned clang-tidy 23.1.3. The available 21.1.8 and 23.1.5 tools are rejected by the checker before comparison; direct 23.1.5 analysis of all three affected units passes. Do not change the tracked baseline or claim the official comparison passed.
- **Invariants:** native builds retain their behavior; browser limitations are explicit; this target is separate from the 1.0 release.
- **Reject/failure behavior:** unsupported browser capabilities fail clearly without pretending to save externally; rejected uploads retain the current document.
- **Verify:** `cmake --preset wasm-smoke && cmake --build --preset wasm-smoke`; `NODE_PATH=<playwright-install>/node_modules node tests/wasm/smoke.cjs`; native `wormedit_core` build and the existing `bmson_sound_channel_editor_tests.cpp` / `bmson_layer_defaults_tests.cpp` suites compiled against that library; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; affected clang-tidy/complexity and `git diff --check`.
- **Out of scope:** full browser editor, browser distribution packaging, public deployment and changes to the 1.0 release.


Verification tails for T-557 (2026-10-28, base `d46ac0cd`; isolated SDK activation:
`/workspace/tooling/activate-wasm.sh`, output `build/wasm6-smoke` via the preset's `-B` override):

```text
[1/1] Linking CXX executable wormedit-wasm-smoke.js
ninja: no work to do.
PASS: 151.0.7922.173 startup, real download/upload, Unicode, rejection atomicity, size limit, reload and missing isolation
All tests passed (620 assertions in 40 test cases)
100% tests passed, 0 tests failed out of 5
complexity: ERROR: clang-tidy version differs from the baseline; review a complete new measurement
```

Evidence: `/tmp/wasm6-{configure-final,link-final,incremental}.log`,
`/tmp/wasm-browser-smoke.log`, `/tmp/wormedit-wasm-smoke.png`,
`/tmp/wasm-native-{core-build,bmson-tests}.log`, `/tmp/wasm-docs.log`,
`/tmp/wasm-tidy-{native,smoke}.log`, `/tmp/wasm-complexity23.log`.
The full application/global suite was not completed; its build was stopped at
728/1706 actions to use current-source focused native verification instead.
An emsdk Google Storage proxy denial and missing local SDK Node packages were
resolved by the isolated Debian toolchain plus its Acorn dependencies; neither
is a remaining build blocker. No public deployment or merge was performed.

T-556 supporting scope: move the existing project-session reset into the testable application dispatcher and preserve its complete application-lifetime service bundle. Native startup bypassed this reset; opening/creating a project exposed the missing callback.

Verification completed (2026-10-11): normal/Release builds and 3,363/3,363 full CTest checks in each; 464 production event cases / 6,385 assertions; affected default clang-tidy with no new/increased violations (unchanged legacy findings remain). Native isolated authenticated TCP Xvfb verifies 1/5 fitting selection and complete authored-document preservation after Save, plus File → Open using a deterministic picker fixture followed by retained 36→111 px stem edge resize. Shared service-bundle regression also covers dock geometry, bookmark transport and injected clock preservation, while chart-specific state clears. At that 2026-10-11 checkpoint T-557 was deliberately deferred; its later owner-selected pickup and current status are recorded above. Documentation and whitespace checks pass. Evidence: `/tmp/scratchpad-grid-{last-build,last-release,menu-build,menu-release,final-ctest,final-release-ctest,final-events,tidy,final-tests-tidy,last-tidy,area-tidy}.log`, `/tmp/fitting-grid-{popup,applied}.png`, `/tmp/stem-resize-open-{before,dragging,after}.png`.
