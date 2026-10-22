# Task cards: scratchpad presentation feedback

[Back to the task-card index](../tasks.md).

User-authorized batch, 2026-10-11. Fill as well as outlines is explicitly in scope.

## T-541 Toggle Layers and Sounds docks consistently

- **Status:** done
- **Outcome:** Repeated shortcut/menu invocation closes the active dock; compact layout switches to the requested dock; Create layer always opens Layers.
- **Read first:** `src/ui/nuklear/app/frontend.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`.
- **Files owned:** `src/ui/nuklear/app/frontend.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, this card, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Audit existing behavior. 2. Correct the live path. 3. Verify input and native presentation.
- **Invariants:** no chart edits or history changes from presentation/toggle actions; preserve modal guards.
- **Reject/failure behavior:** stale or occluded commands remain rejected.
- **Verification (2026-10-11):** normal/Release builds, 3,323/3,323 CTest checks and 446 event cases / 6,186 assertions pass. Default clang-tidy reports no new or increased project complexity; unchanged legacy test warnings remain. Regeneration verifies all eight imported skins. Native authenticated TCP Xvfb confirms Project headers/margins, Layers toggle and muted counts, and Forme box-selection fill/border. Evidence: `/tmp/presentation-complete-{ctest,events,release,tidy}.log`, `/tmp/presentation-margin-build.log`, `/tmp/presentation-{project-1280,layers-1280,layers-closed,selection-fill}.png`.
- **Verify:** normal and Release builds; full CTest and production event harness; affected default clang-tidy; isolated Xvfb capture; documentation and whitespace checks.
- **Out of scope:** assist ticks, layout compatibility, BGA visibility, loop gaps and timing dock redesign.

## T-542 Soften selection box fill and outline across skins

- **Status:** done
- **Outcome:** Replace/add/toggle box selection use subtle blue/green/violet fills and readable matching borders across imported skins; preserve note palettes.
- **Read first:** `scripts/import-ibmsc-theme.py`, `assets/themes/field/ibmsc-*.json`, `tests/ui/theme_tests.cpp`.
- **Files owned:** `scripts/import-ibmsc-theme.py`, `assets/themes/field/ibmsc-*.json`, `tests/ui/theme_tests.cpp`, this card, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Audit existing behavior. 2. Correct the live path. 3. Verify input and native presentation.
- **Invariants:** no chart edits or history changes from presentation/toggle actions; preserve modal guards.
- **Reject/failure behavior:** stale or occluded commands remain rejected.
- **Verification (2026-10-11):** normal/Release builds, 3,323/3,323 CTest checks and 446 event cases / 6,186 assertions pass. Default clang-tidy reports no new or increased project complexity; unchanged legacy test warnings remain. Regeneration verifies all eight imported skins. Native authenticated TCP Xvfb confirms Project headers/margins, Layers toggle and muted counts, and Forme box-selection fill/border. Evidence: `/tmp/presentation-complete-{ctest,events,release,tidy}.log`, `/tmp/presentation-margin-build.log`, `/tmp/presentation-{project-1280,layers-1280,layers-closed,selection-fill}.png`.
- **Verify:** normal and Release builds; full CTest and production event harness; affected default clang-tidy; isolated Xvfb capture; documentation and whitespace checks.
- **Out of scope:** assist ticks, layout compatibility, BGA visibility, loop gaps and timing dock redesign.

## T-543 Show muted object counts and centered layer bullets

- **Status:** done
- **Outcome:** Every layer row shows its name and a muted object-count second line; the color bullet is vertically centered.
- **Read first:** `src/ui/nuklear/compose/compose_layers_panel.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_bgm_layer_tests.cpp`.
- **Files owned:** `src/ui/nuklear/compose/compose_layers_panel.cpp`, `tests/ui/dock_shortcut_event_harness_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `tests/ui/compose_bgm_layer_tests.cpp`, this card, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Audit existing behavior. 2. Correct the live path. 3. Verify input and native presentation.
- **Invariants:** no chart edits or history changes from presentation/toggle actions; preserve modal guards.
- **Reject/failure behavior:** stale or occluded commands remain rejected.
- **Verification (2026-10-11):** normal/Release builds, 3,323/3,323 CTest checks and 446 event cases / 6,186 assertions pass. Default clang-tidy reports no new or increased project complexity; unchanged legacy test warnings remain. Regeneration verifies all eight imported skins. Native authenticated TCP Xvfb confirms Project headers/margins, Layers toggle and muted counts, and Forme box-selection fill/border. Evidence: `/tmp/presentation-complete-{ctest,events,release,tidy}.log`, `/tmp/presentation-margin-build.log`, `/tmp/presentation-{project-1280,layers-1280,layers-closed,selection-fill}.png`.
- **Verify:** normal and Release builds; full CTest and production event harness; affected default clang-tidy; isolated Xvfb capture; documentation and whitespace checks.
- **Out of scope:** assist ticks, layout compatibility, BGA visibility, loop gaps and timing dock redesign.

## T-544 Give Project sections headers and side margins

- **Status:** done
- **Outcome:** Project uses responsive side margins capped at 48 px to preserve table width; Charts and StepMania export policy have clear section headers while chart controls remain functional.
- **Read first:** `src/ui/nuklear/app/frontend.cpp`, `src/ui/nuklear/project/setup_panel.cpp`.
- **Files owned:** `src/ui/nuklear/app/frontend.cpp`, `src/ui/nuklear/project/setup_panel.cpp`, this card, `memory/chart-editor-status/resume.md`.
- **Steps:** 1. Audit existing behavior. 2. Correct the live path. 3. Verify input and native presentation.
- **Invariants:** no chart edits or history changes from presentation/toggle actions; preserve modal guards.
- **Reject/failure behavior:** stale or occluded commands remain rejected.
- **Verification (2026-10-11):** normal/Release builds, 3,323/3,323 CTest checks and 446 event cases / 6,186 assertions pass. Default clang-tidy reports no new or increased project complexity; unchanged legacy test warnings remain. Regeneration verifies all eight imported skins. Native authenticated TCP Xvfb confirms Project headers/margins, Layers toggle and muted counts, and Forme box-selection fill/border. Evidence: `/tmp/presentation-complete-{ctest,events,release,tidy}.log`, `/tmp/presentation-margin-build.log`, `/tmp/presentation-{project-1280,layers-1280,layers-closed,selection-fill}.png`.
- **Verify:** normal and Release builds; full CTest and production event harness; affected default clang-tidy; isolated Xvfb capture; documentation and whitespace checks.
- **Out of scope:** assist ticks, layout compatibility, BGA visibility, loop gaps and timing dock redesign.
